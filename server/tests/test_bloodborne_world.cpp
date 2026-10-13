// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// The party's world on the server (bloodborne_world.h): games that follow it
// through the reliable stream as the real ones do (coop/coop_stream.h,
// coop/world_log.h WorldLogMember), the store on disk across a restart of the
// service, and the lifecycle rules.
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "bloodborne_world.h"
#include "coop/coop_messages.h"

using namespace BloodborneCoop;
using BloodborneWorld::Service;

namespace {

bool Check(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "check failed at line " << line << ": " << expression << '\n';
  }
  return condition;
}

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!Check((expression), #expression, __LINE__))                           \
      return 1;                                                                \
  } while (false)

const QByteArray Party = "kaniel:secret";

// A game as the emulator runs it against the server: its stream, its copy of
// the log.
struct Game {
  Game(quint32 port, std::string name, quint32 instance)
      : endpoint{0x7F000001, static_cast<quint16>(port)}, name{std::move(name)},
        stream{this->name, instance} {}

  void Send(const bbcoop::Envelope &envelope, qint64 now_ms) {
    stream.Send(EncodeEnvelope(envelope), now_ms);
  }
  void Hello(qint64 now_ms, const QByteArray &party = Party) {
    bbcoop::Envelope envelope;
    envelope.mutable_hello()->set_protocol(CoopProtocol);
    envelope.mutable_hello()->set_party(party.toStdString());
    envelope.mutable_hello()->set_member(name);
    envelope.mutable_hello()->set_run(run);
    envelope.mutable_hello()->set_host(name == "host");
    Send(envelope, now_ms);
  }
  void Show(u32 map, const std::vector<CharacterShown> &characters,
            qint64 now_ms) {
    bbcoop::Envelope envelope;
    *envelope.mutable_shown() = ToMessage(map, characters);
    Send(envelope, now_ms);
  }
  void MapState(u32 map, const std::vector<u8> &bits, qint64 now_ms) {
    bbcoop::Envelope envelope;
    envelope.mutable_map_state()->set_map(map);
    envelope.mutable_map_state()->set_bits(
        std::string(bits.begin(), bits.end()));
    Send(envelope, now_ms);
  }
  void ReturnPoint(s32 point, u32 map, qint64 now_ms) {
    bbcoop::Envelope envelope;
    envelope.mutable_return_point()->set_point(point);
    envelope.mutable_return_point()->set_map(map);
    Send(envelope, now_ms);
  }
  void Flags(std::vector<WorldLogFlagEvent> events, qint64 now_ms) {
    bbcoop::Envelope envelope;
    *envelope.mutable_flags() = ToMessage(
        WorldLogFlagProposal{name, member.Epoch(), run, std::move(events)});
    Send(envelope, now_ms);
  }
  void Hp(u64 source, std::vector<EnemyHp> enemies, qint64 now_ms) {
    bbcoop::Envelope envelope;
    *envelope.mutable_hp() =
        ToMessage(WorldLogHpProposal{name,
                                     member.Epoch(),
                                     run,
                                     ++number,
                                     0x18010000,
                                     {source, std::move(enemies)}});
    Send(envelope, now_ms);
  }
  void Propose(const WorldLogKills &kills, qint64 now_ms) {
    bbcoop::Envelope envelope;
    *envelope.mutable_kills() =
        ToMessage(WorldLogProposal{name, member.Epoch(), run, ++number, kills});
    Send(envelope, now_ms);
  }
  void Lifecycle(bbcoop::Lifecycle::Event event, bool host, qint64 now_ms) {
    bbcoop::Envelope envelope;
    envelope.mutable_lifecycle()->set_event(event);
    envelope.mutable_lifecycle()->set_host(host);
    Send(envelope, now_ms);
  }
  void Take(const bbcoop::Envelope &envelope, qint64 now_ms) {
    if (envelope.has_welcome()) {
      welcome = envelope.welcome();
    } else if (envelope.has_entries()) {
      const auto step = member.Take(BatchOf(envelope.entries()), now_ms);
      applied.insert(applied.end(), step.applied.begin(), step.applied.end());
    } else if (envelope.has_snapshot()) {
      const SnapshotParts parts = PartsOf(envelope.snapshot());
      member.Take(parts.kills, now_ms);
      member.Take(parts.shown, now_ms);
      member.Take(parts.map_state, now_ms);
      member.Take(parts.hp, now_ms);
      member.Take(parts.flags, now_ms);
      ++snapshots;
    }
  }

  ServerLink::Endpoint endpoint;
  std::string name;
  CoopStream stream;
  WorldLogMember member;
  std::optional<bbcoop::Welcome> welcome;
  std::vector<WorldLogEntry> applied;
  int snapshots = 0;
  u32 run = 0x1001;
  u32 number = 0;
  bool online = true; // false: its datagrams are lost
};

// Datagrams both ways until nothing moves (the service's sends go to the game
// at their endpoint).
void Pump(Service &service, const std::vector<Game *> &games, qint64 now_ms) {
  for (int round = 0; round < 20; ++round) {
    bool moved = false;
    QList<Service::Send> sends;
    for (Game *game : games) {
      for (const std::vector<u8> &body : game->stream.Flush(now_ms)) {
        if (!game->online) {
          continue;
        }
        moved = true;
        const auto result = service.Receive(
            game->endpoint, QByteArray::fromStdString(game->name),
            QByteArray(reinterpret_cast<const char *>(body.data()),
                       static_cast<qsizetype>(body.size())),
            now_ms);
        sends.append(result.sends);
      }
    }
    sends.append(service.Tick(now_ms).sends);
    for (const Service::Send &send : sends) {
      for (Game *game : games) {
        if (game->online && game->endpoint == send.to) {
          moved = true;
          const auto bytes =
              std::span{reinterpret_cast<const u8 *>(send.body.constData()),
                        static_cast<size_t>(send.body.size())};
          for (const std::vector<u8> &message :
               game->stream.Receive(bytes, now_ms)) {
            if (const auto envelope = DecodeEnvelope(message)) {
              game->Take(*envelope, now_ms);
            }
          }
        }
      }
    }
    if (!moved) {
      return;
    }
  }
}

bool Killed(const Game &game, u32 handle) {
  return WorldLogHandles(game.member.Kills()).contains(handle);
}

int MembersFollowTheServersLog() {
  Service service{{}};
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  qint64 now = 1'000'000;
  host.Hello(now);
  guest.Hello(now);
  Pump(service, {&host, &guest}, now);
  CHECK(host.welcome && host.welcome->accepted());
  CHECK(guest.welcome && guest.welcome->accepted());
  CHECK(host.member.Epoch() != 0);
  CHECK(host.member.Epoch() == guest.member.Epoch());
  CHECK(service.Parties() == 1);

  // A kill by either member stands for both; the second member's kill of it is
  // no new entry.
  now += 100;
  host.Propose({{0x18000000, {0x10014066}}}, now);
  Pump(service, {&host, &guest}, now);
  CHECK(Killed(host, 0x10014066));
  CHECK(Killed(guest, 0x10014066));
  now += 100;
  guest.Propose({{0x18000000, {0x10014066}}}, now);
  host.Propose({}, now);
  Pump(service, {&host, &guest}, now);
  CHECK(Killed(guest, 0x10014066)); // the guest's kill keeps it
  now += 100;
  guest.Propose({}, now);
  Pump(service, {&host, &guest}, now);
  CHECK(!Killed(host, 0x10014066));
  CHECK(!Killed(guest, 0x10014066));
  return 0;
}

int AWrongPartyOrProtocolIsRefused() {
  Service service{{}};
  Game game{1000, "host", 11};
  qint64 now = 1'000'000;
  game.Hello(now, "no-secret");
  Pump(service, {&game}, now);
  CHECK(game.welcome && !game.welcome->accepted());
  CHECK(service.Parties() == 0);
  // Nothing it proposes counts before a hello is taken.
  game.Propose({{0x18000000, {1}}}, now);
  Pump(service, {&game}, now);
  CHECK(service.Parties() == 0);
  return 0;
}

int AWipeStartsOneLogForTheParty() {
  Service service{{}};
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  qint64 now = 1'000'000;
  host.Hello(now);
  guest.Hello(now);
  Pump(service, {&host, &guest}, now);
  host.Propose({{0x18000000, {7}}}, now);
  Pump(service, {&host, &guest}, now);
  const u64 before = guest.member.Epoch();
  CHECK(Killed(guest, 7));

  now += 1000;
  host.Lifecycle(bbcoop::Lifecycle::WIPE, true, now);
  guest.Lifecycle(bbcoop::Lifecycle::WIPE, false, now + 10);
  Pump(service, {&host, &guest}, now + 10);
  const u64 after = guest.member.Epoch();
  CHECK(after != before);
  CHECK(WorldLogEpochFollows(after, before));
  CHECK((after & 0xFFFFFFFF) ==
        (before & 0xFFFFFFFF) + 1); // one new log, not two
  CHECK(!Killed(guest, 7));
  CHECK(StartsAfterAWipe(guest.applied));
  // A member's set from before its wipe, proposed for the old log, does not
  // stand.
  host.number = 100;
  bbcoop::Envelope late;
  *late.mutable_kills() = ToMessage(
      WorldLogProposal{"host", before, host.run, 101, {{0x18000000, {7}}}});
  host.Send(late, now + 20);
  Pump(service, {&host, &guest}, now + 20);
  CHECK(!Killed(guest, 7));
  // A wipe after the grace is a new one.
  now += WorldLogWipeGraceMs + 1000;
  guest.Lifecycle(bbcoop::Lifecycle::WIPE, false, now);
  Pump(service, {&host, &guest}, now);
  CHECK((guest.member.Epoch() & 0xFFFFFFFF) == (after & 0xFFFFFFFF) + 1);
  return 0;
}

int TheHostsSessionEventsStartANewLogAGuestsTakeItsShare() {
  Service service{{}};
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  qint64 now = 1'000'000;
  host.Hello(now);
  guest.Hello(now);
  Pump(service, {&host, &guest}, now);
  host.Propose({{0x18000000, {1}}}, now);
  guest.Propose({{0x18000000, {2}}}, now);
  Pump(service, {&host, &guest}, now);
  CHECK(Killed(host, 1) && Killed(host, 2));
  const u64 epoch = host.member.Epoch();

  now += 100;
  guest.Lifecycle(bbcoop::Lifecycle::LEAVE, false, now);
  Pump(service, {&host, &guest}, now);
  CHECK(host.member.Epoch() == epoch); // the same log
  CHECK(Killed(host, 1));
  CHECK(!Killed(host, 2)); // its share went

  now += 100;
  host.Lifecycle(bbcoop::Lifecycle::JOIN, true, now);
  Pump(service, {&host, &guest}, now);
  CHECK(host.member.Epoch() != epoch);
  CHECK(!Killed(host, 1));
  return 0;
}

int TheWorldStandsThroughARestartOfTheServer() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  qint64 now = 1'000'000;
  u64 epoch = 0;
  {
    Service service{dir.path(), 1000, 3,
                    30'000}; // writes the world whole every 3 records
    Game host{1000, "host", 11};
    Game guest{1001, "guest", 12};
    host.Hello(now);
    guest.Hello(now);
    Pump(service, {&host, &guest}, now);
    for (u32 handle = 1; handle <= 5; ++handle) {
      now += 100;
      host.Propose({{0x18000000,
                     [&] {
                       std::set<u32> handles;
                       for (u32 at = 1; at <= handle; ++at) {
                         handles.insert(at);
                       }
                       return handles;
                     }()}},
                   now);
      Pump(service, {&host, &guest}, now);
    }
    guest.Propose({{0x18010000, {9}}}, now);
    Pump(service, {&host, &guest}, now);
    CHECK(Killed(guest, 5) && Killed(host, 9));
    epoch = host.member.Epoch();
  }
  const QString folder =
      QDir{dir.path()}.filePath(BloodborneWorld::PartyFolderName(Party));
  CHECK(QFile::exists(QDir{folder}.filePath("world.pb")));
  CHECK(QFile::exists(QDir{folder}.filePath("world.pb.1")));

  // A new server process: the world comes back from the folder, the games say
  // hello again.
  Service service{dir.path()};
  const auto loaded = service.Load(now);
  CHECK(service.Parties() == 1);
  const WorldLogHost *world = service.World(Party);
  CHECK(world != nullptr);
  CHECK(world->Epoch() == epoch);
  CHECK(WorldLogKillCount(world->Kills()) == 6);
  Game host{1000, "host", 21};
  Game guest{1001, "guest", 22};
  host.run = guest.run = 0x2001; // the games restarted too
  now += 5000;
  host.Hello(now);
  guest.Hello(now);
  Pump(service, {&host, &guest}, now);
  CHECK(host.member.Epoch() == epoch);
  CHECK(Killed(guest, 5) && Killed(guest, 9) && Killed(host, 1));
  // The members' shares came back too: the host's withdrawal takes its kills
  // only.
  now += 100;
  host.Propose({}, now);
  Pump(service, {&host, &guest}, now);
  CHECK(!Killed(guest, 1));
  CHECK(Killed(guest, 9));
  return 0;
}

int AJournalCutShortLoadsUpToItsLastRecord() {
  QTemporaryDir dir;
  qint64 now = 1'000'000;
  {
    Service service{dir.path(), 1000, 1000,
                    1'000'000}; // never compacted: the journal only
    Game host{1000, "host", 11};
    host.Hello(now);
    Pump(service, {&host}, now);
    host.Propose({{0x18000000, {1}}}, now + 1);
    Pump(service, {&host}, now + 1);
    host.Propose({{0x18000000, {1, 2}}}, now + 2);
    Pump(service, {&host}, now + 2);
  }
  const QString folder =
      QDir{dir.path()}.filePath(BloodborneWorld::PartyFolderName(Party));
  // The new world was written whole at once; the kills are in its journal. The
  // last record is cut short (a crash in the write).
  CHECK(QFile::exists(QDir{folder}.filePath("world.pb")));
  QFile journal{QDir{folder}.filePath("journal.pb")};
  CHECK(journal.exists());
  CHECK(journal.resize(journal.size() - 3));
  Service service{dir.path()};
  const auto loaded = service.Load(now + 10);
  CHECK(loaded.log.join(QStringLiteral(" | ")).contains("cut record"));
  const WorldLogHost *world = service.World(Party);
  CHECK(world != nullptr);
  CHECK(WorldLogHandles(world->Kills()) == std::unordered_set<u32>{1});
  // A world file that does not read: the backup.
  return 0;
}

int ABrokenWorldFileFallsBackToABackup() {
  QTemporaryDir dir;
  qint64 now = 1'000'000;
  {
    Service service{dir.path(), 1000, 1, 30'000}; // every record written whole
    Game host{1000, "host", 11};
    host.Hello(now);
    Pump(service, {&host}, now);
    host.Propose({{0x18000000, {1}}}, now + 1);
    Pump(service, {&host}, now + 1);
    host.Propose({{0x18000000, {1, 2}}}, now + 2);
    Pump(service, {&host}, now + 2);
  }
  const QString folder =
      QDir{dir.path()}.filePath(BloodborneWorld::PartyFolderName(Party));
  QFile world_file{QDir{folder}.filePath("world.pb")};
  CHECK(world_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  world_file.write("not a world");
  world_file.close();
  Service service{dir.path()};
  const auto loaded = service.Load(now + 10);
  CHECK(loaded.log.join(QStringLiteral(" | ")).contains("backup 1"));
  const WorldLogHost *world = service.World(Party);
  CHECK(world != nullptr);
  CHECK(WorldLogHandles(world->Kills()) ==
        std::unordered_set<u32>{1}); // the world before
  return 0;
}

int TheServerRestartingMidSessionLosesNothing() {
  QTemporaryDir dir;
  qint64 now = 1'000'000;
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  auto service = std::make_unique<Service>(dir.path());
  host.Hello(now);
  guest.Hello(now);
  Pump(*service, {&host, &guest}, now);
  host.Propose({{0x18000000, {0x10014066}}}, now);
  Pump(*service, {&host, &guest}, now);
  CHECK(Killed(guest, 0x10014066));
  const u64 epoch = guest.member.Epoch();

  // shadNet stops and starts again; the games keep running and find a new
  // stream.
  service.reset();
  service = std::make_unique<Service>(dir.path());
  service->Load(now);
  now += 3000;
  // The first frames of the games are for the old server: the new one answers,
  // they start over.
  host.Propose({{0x18000000, {0x10014066}}}, now);
  Pump(*service, {&host, &guest}, now);
  CHECK(host.stream.TakeRestarted());
  CHECK(guest.stream.TakeRestarted());
  host.Hello(now);
  guest.Hello(now);
  Pump(*service, {&host, &guest}, now);
  CHECK(guest.member.Epoch() == epoch);
  CHECK(Killed(guest, 0x10014066)); // the kill stands for all
  now += 100;
  guest.Propose({{0x18000000, {0x10014067}}}, now);
  Pump(*service, {&host, &guest}, now);
  CHECK(Killed(host, 0x10014067));
  return 0;
}

int ALostDatagramIsSentAgain() {
  Service service{{}};
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  qint64 now = 1'000'000;
  host.Hello(now);
  guest.Hello(now);
  Pump(service, {&host, &guest}, now);
  guest.online = false;
  host.Propose({{0x18000000, {3}}}, now);
  Pump(service, {&host, &guest}, now);
  CHECK(!Killed(guest, 3));
  guest.online = true;
  now += 500; // past the resend time
  Pump(service, {&host, &guest}, now);
  CHECK(Killed(guest, 3));
  return 0;
}

bool FlagApplied(const Game &game, s32 flag, bool on) {
  return std::ranges::any_of(game.applied, [&](const WorldLogEntry &entry) {
    return entry.kind ==
               (on ? WorldLogKind::FlagSet : WorldLogKind::FlagClear) &&
           static_cast<s32>(entry.subject) == flag;
  });
}

std::vector<u8> MapBits(size_t lit) {
  std::vector<u8> bits((MapFlagCount + 7) / 8);
  bits[lit / 8] = static_cast<u8>(0x80 >> (lit % 8));
  return bits;
}

int TheHostsWorldAndEveryonesFlagsAndHpReachEveryMember() {
  QTemporaryDir dir;
  qint64 now = 1'000'000;
  const std::vector<CharacterShown> characters{
      {0x10014000, false, true, false, false}, // in handle order, as the log
      {0x10014005, true, false, false, false}};
  {
    Service service{dir.path()};
    Game host{1000, "host", 11};
    Game guest{1001, "guest", 12};
    host.Hello(now);
    guest.Hello(now);
    Pump(service, {&host, &guest}, now);
    host.Show(0x18010000, characters, now);
    host.MapState(0x18010000, MapBits(13), now);
    host.ReturnPoint(2412950, 0x18010000, now);
    // A guest's world is not the party's: its characters and map state count
    // for nothing.
    guest.Show(0x18010000, {{0x10014005, false, false, false, false}}, now);
    guest.MapState(0x18010000, MapBits(2), now);
    guest.Flags({{1, 2412950, true}}, now);
    guest.Hp(0x1111000000000003, {{0x10014066, 49, 450}}, now);
    Pump(service, {&host, &guest}, now);
    CHECK(guest.member.ShownKnown());
    CHECK(WorldLogShownCharacters(guest.member.Shown()) == characters);
    CHECK(guest.member.MapState() ==
          (WorldLogMapState{0x18010000, MapBits(13)}));
    CHECK(guest.member.HostReturnPoint() ==
          (WorldLogReturnPoint{2412950, 0x18010000}));
    CHECK(FlagApplied(host, 2412950, true));
    CHECK(FlagApplied(guest, 2412950, true));
    CHECK(host.member.Hp().contains(0x1111000000000003));
    // The same event again (a resend) is no new entry.
    const size_t applied = host.applied.size();
    guest.Flags({{1, 2412950, true}}, now + 100);
    Pump(service, {&host, &guest}, now + 100);
    CHECK(host.applied.size() == applied);
  }
  // The world comes back from the disk with all of it: a game after the restart
  // takes it whole.
  Service service{dir.path()};
  service.Load(now + 1000);
  Game late{1002, "late", 13};
  late.Hello(now + 1000);
  Pump(service, {&late}, now + 1000);
  CHECK(late.snapshots >= 1);
  CHECK(WorldLogShownCharacters(late.member.Shown()) == characters);
  CHECK(late.member.MapState() == (WorldLogMapState{0x18010000, MapBits(13)}));
  CHECK(late.member.HostReturnPoint() ==
        (WorldLogReturnPoint{2412950, 0x18010000}));
  CHECK(late.member.Hp().contains(0x1111000000000003));
  return 0;
}

} // namespace

int main() {
  const std::vector<std::pair<const char *, std::function<int()>>> tests{
      {"MembersFollowTheServersLog", MembersFollowTheServersLog},
      {"AWrongPartyOrProtocolIsRefused", AWrongPartyOrProtocolIsRefused},
      {"AWipeStartsOneLogForTheParty", AWipeStartsOneLogForTheParty},
      {"TheHostsSessionEventsStartANewLogAGuestsTakeItsShare",
       TheHostsSessionEventsStartANewLogAGuestsTakeItsShare},
      {"TheWorldStandsThroughARestartOfTheServer",
       TheWorldStandsThroughARestartOfTheServer},
      {"AJournalCutShortLoadsUpToItsLastRecord",
       AJournalCutShortLoadsUpToItsLastRecord},
      {"ABrokenWorldFileFallsBackToABackup",
       ABrokenWorldFileFallsBackToABackup},
      {"TheServerRestartingMidSessionLosesNothing",
       TheServerRestartingMidSessionLosesNothing},
      {"ALostDatagramIsSentAgain", ALostDatagramIsSentAgain},
      {"TheHostsWorldAndEveryonesFlagsAndHpReachEveryMember",
       TheHostsWorldAndEveryonesFlagsAndHpReachEveryMember},
  };
  int failed = 0;
  for (const auto &[name, test] : tests) {
    const int result = test();
    std::cout << (result == 0 ? "PASS " : "FAIL ") << name << '\n';
    failed += result != 0 ? 1 : 0;
  }
  return failed == 0 ? 0 : 1;
}
