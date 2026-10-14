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
#include <random>
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
    envelope.mutable_hello()->set_enemy_owner(enemy_owner);
    Send(envelope, now_ms);
  }
  void Map(u32 map, qint64 now_ms) {
    bbcoop::Envelope envelope;
    envelope.mutable_map()->set_map(map);
    Send(envelope, now_ms);
  }
  // The owner of an area as this game heard it ("" for none).
  std::string OwnerOf(u32 area) const {
    for (const bbcoop::EnemyOwner &owner : owners.owners()) {
      if (owner.map() == area) {
        return owner.member();
      }
    }
    return {};
  }
  u64 EpochOf(u32 area) const {
    for (const bbcoop::EnemyOwner &owner : owners.owners()) {
      if (owner.map() == area) {
        return owner.epoch();
      }
    }
    return 0;
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
  void LobbyHello(bool playing, qint64 now_ms) {
    bbcoop::Envelope envelope;
    envelope.mutable_lobby_hello()->set_protocol(CoopProtocol);
    envelope.mutable_lobby_hello()->set_member(name);
    envelope.mutable_lobby_hello()->set_name("Player " + name);
    envelope.mutable_lobby_hello()->set_playing(playing);
    Send(envelope, now_ms);
  }
  void Team(bbcoop::TeamRequest::Action action, qint64 now_ms,
            std::string other = {}, std::string team = {}, u32 slot = 0) {
    bbcoop::Envelope envelope;
    bbcoop::TeamRequest &request = *envelope.mutable_team_request();
    request.set_action(action);
    request.set_member(other);
    request.set_team(team);
    request.set_slot(slot);
    Send(envelope, now_ms);
  }
  void Take(const bbcoop::Envelope &envelope, qint64 now_ms) {
    if (envelope.has_lobby_state()) {
      lobby = envelope.lobby_state();
      ++lobby_states;
    } else if (envelope.has_team_start()) {
      starts.push_back(envelope.team_start());
    } else if (envelope.has_map_board()) {
      map_board = envelope.map_board();
    } else if (envelope.has_rest_board()) {
      rest_board = envelope.rest_board();
    } else if (envelope.has_trophies()) {
      trophies = envelope.trophies();
    } else if (envelope.has_vitals_board()) {
      vitals_board = envelope.vitals_board();
    } else if (envelope.has_echoes()) {
      for (const bbcoop::EchoKill &kill : envelope.echoes().kills()) {
        echoes.push_back(kill.echoes());
      }
    } else if (envelope.has_portrait()) {
      portraits[envelope.portrait().member()] = envelope.portrait();
    } else if (envelope.has_enemy_owners()) {
      owners = envelope.enemy_owners();
      ++owner_messages;
    } else if (envelope.has_welcome()) {
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
  bbcoop::MapBoard map_board;
  bbcoop::RestBoard rest_board;
  bbcoop::Trophies trophies;
  bbcoop::VitalsBoard vitals_board;
  std::vector<s32> echoes;
  std::map<std::string, bbcoop::Portrait> portraits;
  bbcoop::EnemyOwners owners;
  int owner_messages = 0;
  bbcoop::LobbyState lobby;
  int lobby_states = 0;
  std::vector<bbcoop::TeamStart> starts;
  bool enemy_owner = true; // runs phase 4's owners (its hello says)
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

bbcoop::Envelope VitalsOf(u32 map, u32 vital, u32 down = 0,
                          std::vector<bbcoop::ReviveHit> hits = {}) {
  bbcoop::Envelope envelope;
  bbcoop::Vitals &vitals = *envelope.mutable_vitals();
  vitals.set_map(map);
  vitals.set_vital(vital);
  vitals.set_down(down);
  for (const bbcoop::ReviveHit &hit : hits) {
    *vitals.add_hits() = hit;
  }
  return envelope;
}

const bbcoop::VitalsBoard::Member *VitalsFor(const Game &game,
                                             const std::string &member) {
  for (const auto &entry : game.vitals_board.members()) {
    if (entry.member() == member) {
      return &entry;
    }
  }
  return nullptr;
}

int TheBoardsGoToEveryMember() {
  Service service{{}};
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  qint64 now = 1'000'000;
  host.Hello(now);
  guest.Hello(now);
  Pump(service, {&host, &guest}, now);

  // Maps: every member's, to everyone.
  bbcoop::Envelope map;
  map.mutable_map()->set_map(0x18010000);
  host.Send(map, now);
  map.mutable_map()->set_map(0x18000000);
  guest.Send(map, now);
  // A rest with its place.
  bbcoop::Envelope rest;
  rest.mutable_rest()->set_lamp(2412950);
  rest.mutable_rest()->mutable_at()->set_x(-215.5f);
  guest.Send(rest, now);
  // Trophies: the party's union.
  bbcoop::Envelope trophies;
  trophies.mutable_trophies()->add_ids(5);
  trophies.mutable_trophies()->add_ids(500); // no such trophy
  host.Send(trophies, now);
  trophies.mutable_trophies()->set_ids(0, 7);
  guest.Send(trophies, now);
  // Echoes: to the others only, once.
  bbcoop::Envelope echoes;
  bbcoop::EchoKill *kill = echoes.mutable_echoes()->add_kills();
  kill->set_echoes(120);
  kill->set_map(0x18000000);
  guest.Send(echoes, now);
  Pump(service, {&host, &guest}, now);
  CHECK(host.map_board.members_size() == 2);
  CHECK(guest.map_board.members_size() == 2);
  CHECK(host.rest_board.rests_size() == 1);
  CHECK(host.rest_board.rests(0).member() == "guest");
  CHECK(host.rest_board.rests(0).lamp() == 2412950);
  CHECK(host.rest_board.rests(0).at().x() == -215.5f);
  CHECK(host.trophies.ids_size() == 2);
  CHECK(host.trophies.ids(0) == 5 && host.trophies.ids(1) == 7);
  CHECK(host.echoes == std::vector<s32>{120});
  CHECK(guest.echoes.empty());

  // Standing up takes the rest off; a member not heard of goes from the boards.
  rest.mutable_rest()->set_lamp(0);
  guest.Send(rest, now + 100);
  Pump(service, {&host, &guest}, now + 100);
  CHECK(host.rest_board.rests_size() == 0);
  map.mutable_map()->set_map(0x18010000);
  host.Send(map, now + 4000);
  Pump(service, {&host, &guest}, now + 4000);
  Pump(service, {&host, &guest}, now + 6000);
  CHECK(host.map_board.members_size() == 1);
  CHECK(host.map_board.members(0).member() == "host");

  // A game that comes later gets the boards as they are.
  Game late{1002, "late", 13};
  late.Hello(now + 6100);
  Pump(service, {&host, &guest, &late}, now + 6100);
  CHECK(late.trophies.ids_size() == 2);
  CHECK(late.map_board.members_size() == 1);
  return 0;
}

int ThePartyIsWipedWhenNobodyOnTheMapStands() {
  Service service{{}};
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  qint64 now = 1'000'000;
  host.Hello(now);
  guest.Hello(now);
  Pump(service, {&host, &guest}, now);
  constexpr u32 Up = 1;
  constexpr u32 Down = 2;
  host.Send(VitalsOf(0x18010000, Up), now);
  guest.Send(VitalsOf(0x18010000, Down, 4), now);
  Pump(service, {&host, &guest}, now);
  CHECK(host.vitals_board.members_size() == 2);
  CHECK(host.vitals_board.wipes_size() == 0);
  // The host's revive hits count on the guest's down.
  bbcoop::ReviveHit hit;
  hit.set_target("guest");
  hit.set_down(4);
  hit.set_damage(90);
  host.Send(VitalsOf(0x18010000, Up, 0, {hit}), now + 100);
  Pump(service, {&host, &guest}, now + 100);
  const auto *downed = VitalsFor(guest, "guest");
  CHECK(downed != nullptr && downed->revive_damage() == 90);
  CHECK(VitalsFor(guest, "host")->vitals().hits_size() == 0);
  // Both down on the map: one wipe, for both.
  host.Send(VitalsOf(0x18010000, Down, 9), now + 200);
  Pump(service, {&host, &guest}, now + 200);
  CHECK(guest.vitals_board.wipes_size() == 1);
  CHECK(guest.vitals_board.wipes(0).members_size() == 2);
  guest.Send(VitalsOf(0x18010000, Down, 4), now + 300);
  Pump(service, {&host, &guest}, now + 300);
  CHECK(guest.vitals_board.wipes_size() == 1); // not again
  // It goes after a while.
  host.Send(VitalsOf(0x18010000, Up), now + 21'000);
  guest.Send(VitalsOf(0x18010000, Up), now + 21'000);
  Pump(service, {&host, &guest}, now + 21'000);
  CHECK(guest.vitals_board.wipes_size() == 0);
  return 0;
}

// Whatever a client sends (the HTTP boards' parsers had a fuzz test; the
// stream's frames and our messages have this one): random bytes, random frames
// and messages with random fields never crash the service, and a member that
// behaves still follows the log after them.
int WhateverAGameSendsTheServiceKeepsWorking() {
  Service service{{}};
  std::mt19937 random{20261013};
  const auto bytes = [&](size_t size) {
    std::vector<u8> out(size);
    for (u8 &byte : out) {
      byte = static_cast<u8>(random());
    }
    return out;
  };
  qint64 now = 1'000'000;
  for (int round = 0; round < 3000; ++round) {
    const ServerLink::Endpoint from{0x7F000001,
                                    static_cast<quint16>(2000 + round % 7)};
    std::vector<u8> body;
    switch (round % 3) {
    case 0:
      body = bytes(random() % 300);
      break;
    case 1: {
      // A well-formed frame with random chunks in it.
      GameFrame frame{"fuzz", 77, 0, 0, {}};
      for (u32 entry = 0; entry < random() % 4; ++entry) {
        frame.entries.push_back(
            {PacketClass::Reliable, entry, bytes(random() % 200)});
      }
      body = EncodeGameFrame(frame);
      break;
    }
    default: {
      // A hello, then a well-formed message of a random kind with random
      // fields.
      CoopStream stream{"fuzz", static_cast<u32>(round + 1)};
      bbcoop::Envelope hello;
      hello.mutable_hello()->set_protocol(CoopProtocol);
      hello.mutable_hello()->set_party("fuzz:party");
      hello.mutable_hello()->set_member("f" + std::to_string(round % 5));
      hello.mutable_hello()->set_host(round % 2 == 0);
      stream.Send(EncodeEnvelope(hello), now);
      std::vector<u8> message = bytes(random() % 120);
      if (!message.empty()) {
        message[0] =
            static_cast<u8>(((random() % 21 + 1) << 3) | 2); // a field's tag
      }
      stream.Send(message, now);
      for (const std::vector<u8> &frame_body : stream.Flush(now)) {
        service.Receive(
            from, "fuzz",
            QByteArray(reinterpret_cast<const char *>(frame_body.data()),
                       static_cast<qsizetype>(frame_body.size())),
            now);
      }
      continue;
    }
    }
    service.Receive(from, "fuzz",
                    QByteArray(reinterpret_cast<const char *>(body.data()),
                               static_cast<qsizetype>(body.size())),
                    now);
    if (round % 100 == 0) {
      now += 700;
      service.Tick(now);
    }
  }
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  host.Hello(now);
  guest.Hello(now);
  Pump(service, {&host, &guest}, now);
  host.Propose({{0x18000000, {42}}}, now + 10);
  Pump(service, {&host, &guest}, now + 10);
  CHECK(Killed(guest, 42));
  return 0;
}

int APortraitGoesToTheOthersAndToLaterMembers() {
  Service service{{}};
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  qint64 now = 1'000'000;
  host.Hello(now);
  guest.Hello(now);
  Pump(service, {&host, &guest}, now);
  bbcoop::Envelope portrait;
  portrait.mutable_portrait()->set_member("spoofed");
  portrait.mutable_portrait()->set_id(0xabcd);
  portrait.mutable_portrait()->set_pixels(std::string(8192, 'p'));
  host.Send(portrait, now);
  Pump(service, {&host, &guest}, now);
  CHECK(guest.portraits.contains("host"));
  CHECK(guest.portraits.at("host").pixels().size() == 8192);
  CHECK(!guest.portraits.contains("spoofed"));
  CHECK(host.portraits.empty());
  Game late{1002, "late", 13};
  late.Hello(now + 10);
  Pump(service, {&host, &guest, &late}, now + 10);
  CHECK(late.portraits.contains("host"));
  CHECK(late.portraits.at("host").id() == 0xabcd);
  return 0;
}

int TheFirstMemberOnAnAreaRunsItsEnemies() {
  Service service{{}};
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  Game guest2{1002, "guest2", 13};
  const std::vector<Game *> games{&host, &guest, &guest2};
  qint64 now = 1'000'000;
  for (Game *game : games) {
    game->Hello(now);
  }
  Pump(service, games, now);
  constexpr u32 Yharnam = 0x18010000; // Central Yharnam
  constexpr u32 Ward = 0x18000000; // Cathedral Ward: another block of its area
  constexpr u32 Woods = 0x15010000;

  // The first one there runs the map.
  host.Map(0x18010000, now);
  Pump(service, games, now);
  CHECK(host.OwnerOf(Yharnam) == "host");
  CHECK(guest2.OwnerOf(Yharnam) == "host");
  const u64 first = host.EpochOf(Yharnam);
  CHECK(first != 0);
  guest.Map(Yharnam, now + 100);
  guest2.Map(Ward, now + 100);
  Pump(service, games, now + 100);
  CHECK(guest.OwnerOf(Yharnam) == "host");
  CHECK(guest.EpochOf(Yharnam) == first);
  // Another block of the area has its own: the game wakes a block's enemies
  // only near a player in that world, so the host's copies of the Ward's stand
  // still.
  CHECK(host.OwnerOf(Ward) == "guest2");
  guest2.Map(Woods, now + 150);
  Pump(service, games, now + 150);
  CHECK(host.OwnerOf(Ward).empty());
  CHECK(host.OwnerOf(Woods) == "guest2");

  // Every answer to a member's map carries the owners, changed or not.
  const int heard = guest.owner_messages;
  guest.Map(Yharnam, now + 200);
  Pump(service, games, now + 200);
  CHECK(guest.owner_messages > heard);

  // A load hands the area on at once, to the member there longest.
  host.Map(0, now + 300);
  Pump(service, games, now + 300);
  CHECK(guest2.OwnerOf(Yharnam) == "guest");
  const u64 second = guest2.EpochOf(Yharnam);
  CHECK(second > first);
  guest2.Map(Yharnam, now + 400);
  Pump(service, games, now + 400);
  CHECK(host.OwnerOf(Yharnam) == "guest");
  CHECK(host.OwnerOf(Woods).empty()); // nobody is there any more
  // The session's host takes back the area it comes to: its game is the one the
  // others' are built to follow.
  host.Map(0x18010000, now + 1300);
  guest.Map(Yharnam, now + 1300);
  guest2.Map(0x18010000, now + 1300);
  Pump(service, games, now + 1300);
  CHECK(guest.OwnerOf(Yharnam) == "host");
  CHECK(guest.EpochOf(Yharnam) > second);

  // An owner not heard of for a while (its game died) hands it on.
  host.Map(0, now + 1400);
  Pump(service, games, now + 1400);
  CHECK(guest2.OwnerOf(Yharnam) == "guest");
  guest2.Map(0x18010000, now + 2300);
  Pump(service, games, now + 2300);
  guest2.Map(0x18010000, now + 3300);
  Pump(service, games, now + 3300);
  CHECK(guest2.OwnerOf(Yharnam) == "guest"); // 2 s: not yet
  guest2.Map(0x18010000, now + 4300);
  Pump(service, games, now + 4300);
  CHECK(guest2.OwnerOf(Yharnam) == "guest2");

  // An area nobody is on has no owner.
  guest2.Map(0, now + 4400);
  Pump(service, games, now + 4400);
  CHECK(host.OwnerOf(Yharnam).empty());
  return 0;
}

int OnlyTheOwnersKillsAndHpCount() {
  Service service{{}};
  Game host{1000, "host", 11};
  Game guest{1001, "guest", 12};
  Game old{1002, "old", 13};
  old.enemy_owner = false; // a game without phase 4's owners: counts as before
  const std::vector<Game *> games{&host, &guest, &old};
  qint64 now = 1'000'000;
  for (Game *game : games) {
    game->Hello(now);
  }
  Pump(service, games, now);
  host.Map(0x18010000, now);
  Pump(service, games, now);
  guest.Map(0x18010000, now + 100);
  old.Map(0x18010000, now + 100);
  Pump(service, games, now + 100);
  CHECK(guest.OwnerOf(0x18010000) == "host");

  // A kill on an area another member runs is not the guest's to make.
  now += 200;
  guest.Propose({{0x18010000, {0x10014066}}}, now);
  Pump(service, games, now);
  CHECK(!Killed(host, 0x10014066));
  host.Propose({{0x18010000, {0x10014066}}}, now + 10);
  Pump(service, games, now + 10);
  CHECK(Killed(guest, 0x10014066));
  // Its HP report neither.
  guest.Hp(0x2222000000000001, {{0x1001400b, 100, 484}}, now + 20);
  host.Hp(0x1111000000000001, {{0x1001400b, 300, 484}}, now + 20);
  Pump(service, games, now + 20);
  CHECK(!guest.member.Hp().contains(0x2222000000000001));
  CHECK(guest.member.Hp().contains(0x1111000000000001));
  // A game without the owners counts everywhere.
  old.Propose({{0x18010000, {0x1001402e}}}, now + 30);
  Pump(service, games, now + 30);
  CHECK(Killed(host, 0x1001402e));
  // An area nobody runs (nobody is there yet, or the server just started):
  // counted.
  guest.Propose({{0x21000000, {0x10210001}}}, now + 40);
  Pump(service, games, now + 40);
  CHECK(Killed(host, 0x10210001));

  // The owner leaving keeps the kills it made there, but makes no new ones.
  host.Map(0, now + 100);
  Pump(service, games, now + 100);
  CHECK(guest.OwnerOf(0x18010000) == "guest");
  host.Propose({{0x18010000, {0x10014066, 0x10014009}}}, now + 110);
  Pump(service, games, now + 110);
  CHECK(Killed(guest, 0x10014066));
  CHECK(!Killed(guest, 0x10014009));
  guest.Propose({{0x18010000, {0x10014009}}, {0x21000000, {0x10210001}}},
                now + 120);
  Pump(service, games, now + 120);
  CHECK(Killed(host, 0x10014009));
  return 0;
}

} // namespace

// Phase 5: the lobby on the service. Players sign in before any party, form a
// team, ready up and start; the team outlives the server.
int ATeamFormsAndStartsThroughTheService() {
  QTemporaryDir dir;
  CHECK(dir.isValid());
  qint64 now = 1'000'000;
  std::string team;
  {
    Service service{dir.path()};
    Game a{1000, "aa", 11};
    Game b{1001, "bb", 12};
    a.LobbyHello(false, now);
    b.LobbyHello(false, now);
    Pump(service, {&a, &b}, now);
    CHECK(a.lobby.players_size() == 1 && a.lobby.players(0).name() == "Player bb");
    a.Team(bbcoop::TeamRequest::CREATE, now);
    Pump(service, {&a, &b}, now);
    CHECK(a.lobby.has_team() && a.lobby.team().leader() == "aa");
    team = a.lobby.team().id();
    a.Team(bbcoop::TeamRequest::INVITE, now, "bb");
    Pump(service, {&a, &b}, now);
    CHECK(b.lobby.invites_size() == 1 && b.lobby.invites(0).team() == team);
    b.Team(bbcoop::TeamRequest::ACCEPT, now, {}, team);
    Pump(service, {&a, &b}, now);
    CHECK(a.lobby.team().members_size() == 2);
    // A refused request comes back with a notice to that player only.
    const int a_states = a.lobby_states;
    b.Team(bbcoop::TeamRequest::START, now);
    Pump(service, {&a, &b}, now);
    CHECK(!b.lobby.notice().empty());
    CHECK(a.lobby_states == a_states);
    a.Team(bbcoop::TeamRequest::READY, now, {}, {}, 3);
    b.Team(bbcoop::TeamRequest::READY, now, {}, {}, 1);
    a.Team(bbcoop::TeamRequest::START, now);
    Pump(service, {&a, &b}, now);
    CHECK(a.starts.size() == 1 && b.starts.size() == 1);
    CHECK(b.starts[0].party() == "team:" + team && b.starts[0].host() == "aa");
    CHECK(b.starts[0].slot() == 1);
    // The team's party hello plays its world like any party's.
    a.Hello(now, QByteArray::fromStdString(b.starts[0].party()));
    Pump(service, {&a, &b}, now);
    CHECK(a.welcome && a.welcome->accepted());
    // b's game goes away: offline in a's lobby.
    b.online = false;
    service.Forget(b.endpoint);
    now += 2000;
    Pump(service, {&a}, now);
    CHECK(a.lobby.team().members(1).state() == bbcoop::LobbyPlayer::OFFLINE);
  }
  // A new server: the team is still there for its members.
  Service service{dir.path()};
  service.Load(now);
  Game b{1001, "bb", 22};
  b.LobbyHello(false, now);
  Pump(service, {&b}, now);
  CHECK(b.lobby.has_team() && b.lobby.team().id() == team);
  CHECK(b.lobby.team().members_size() == 2);
  return 0;
}

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
      {"TheBoardsGoToEveryMember", TheBoardsGoToEveryMember},
      {"ThePartyIsWipedWhenNobodyOnTheMapStands",
       ThePartyIsWipedWhenNobodyOnTheMapStands},
      {"APortraitGoesToTheOthersAndToLaterMembers",
       APortraitGoesToTheOthersAndToLaterMembers},
      {"WhateverAGameSendsTheServiceKeepsWorking",
       WhateverAGameSendsTheServiceKeepsWorking},
      {"TheFirstMemberOnAnAreaRunsItsEnemies",
       TheFirstMemberOnAnAreaRunsItsEnemies},
      {"OnlyTheOwnersKillsAndHpCount", OnlyTheOwnersKillsAndHpCount},
      {"ATeamFormsAndStartsThroughTheService",
       ATeamFormsAndStartsThroughTheService},
  };
  int failed = 0;
  for (const auto &[name, test] : tests) {
    const int result = test();
    std::cout << (result == 0 ? "PASS " : "FAIL ") << name << '\n';
    failed += result != 0 ? 1 : 0;
  }
  return failed == 0 ? 0 : 1;
}
