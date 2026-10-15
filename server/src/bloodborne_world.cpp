// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bloodborne_world.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QtEndian>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "coop/coop_messages.h"
#include "coop/packet_routes.h"

namespace BloodborneWorld {

using namespace BloodborneCoop;

namespace {

constexpr quint32 StoreVersion = 1;
constexpr int Backups = 3;
constexpr int DatedBackups = 7; // phase 11: a week of daily copies
const QString WorldFile = QStringLiteral("world.pb");
const QString JournalFile = QStringLiteral("journal.pb");
const QString TradesFile = QStringLiteral("trades.pb");

std::optional<bbcoop::StoredWorld> ReadWorld(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    bbcoop::StoredWorld world;
    if (!world.ParseFromArray(bytes.constData(), static_cast<int>(bytes.size())) ||
        world.version() != StoreVersion || world.world() == 0) {
        return std::nullopt;
    }
    return world;
}

QByteArray Serialized(const google::protobuf::MessageLite& message) {
    QByteArray bytes(static_cast<qsizetype>(message.ByteSizeLong()), Qt::Uninitialized);
    if (!bytes.isEmpty()) {
        message.SerializeToArray(bytes.data(), static_cast<int>(bytes.size()));
    }
    return bytes;
}

QString Hex(quint64 value) {
    return QStringLiteral("%1").arg(value, 0, 16);
}

// Phase 11: what was written reaches the disk, not only the system's cache (a power cut or a
// crash of the PC keeps it).
bool SyncToDisk(QFile& file) {
    if (!file.flush()) {
        return false;
    }
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    return handle != INVALID_HANDLE_VALUE && FlushFileBuffers(handle) != 0;
#else
    return fsync(file.handle()) == 0;
#endif
}

} // namespace

QString PartyFolderName(const QByteArray& party) {
    return QString::fromLatin1(
        QCryptographicHash::hash(party, QCryptographicHash::Sha256).toHex().left(32));
}

WorldStore::WorldStore(QString folder) : folder{std::move(folder)} {}

std::optional<WorldStore::Loaded> WorldStore::Load(QStringList& log) const {
    const QDir dir{folder};
    std::optional<bbcoop::StoredWorld> world = ReadWorld(dir.filePath(WorldFile));
    for (int backup = 1; !world && backup <= Backups; ++backup) {
        const QString path = dir.filePath(QStringLiteral("%1.%2").arg(WorldFile).arg(backup));
        if (!QFileInfo::exists(path)) {
            continue;
        }
        world = ReadWorld(path);
        if (world) {
            log.append(QStringLiteral("world store %1: world.pb does not read, took backup %2")
                           .arg(folder)
                           .arg(backup));
        }
    }
    if (!world) {
        return std::nullopt;
    }
    Loaded loaded{*world, {}};
    QFile journal{dir.filePath(JournalFile)};
    if (!journal.open(QIODevice::ReadOnly)) {
        return loaded;
    }
    const QByteArray bytes = journal.readAll();
    qsizetype at = 0;
    while (bytes.size() - at >= 4) {
        const quint32 size = qFromLittleEndian<quint32>(bytes.constData() + at);
        if (bytes.size() - at - 4 < static_cast<qsizetype>(size)) {
            log.append(QStringLiteral("world store %1: the journal ends in a cut record (%2 "
                                      "bytes left), read up to it")
                           .arg(folder)
                           .arg(bytes.size() - at));
            break;
        }
        bbcoop::JournalRecord record;
        if (!record.ParseFromArray(bytes.constData() + at + 4, static_cast<int>(size))) {
            log.append(QStringLiteral("world store %1: a journal record does not read, stopped "
                                      "there")
                           .arg(folder));
            break;
        }
        at += 4 + static_cast<qsizetype>(size);
        if (record.number() >= world->journal_from()) {
            loaded.journal.append(std::move(record));
        }
    }
    return loaded;
}

bool WorldStore::Append(const bbcoop::JournalRecord& record) {
    QDir{}.mkpath(folder);
    QFile journal{QDir{folder}.filePath(JournalFile)};
    if (!journal.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return false;
    }
    const QByteArray bytes = Serialized(record);
    char size[4];
    qToLittleEndian<quint32>(static_cast<quint32>(bytes.size()), size);
    return journal.write(size, 4) == 4 && journal.write(bytes) == bytes.size() &&
           SyncToDisk(journal);
}

bool WorldStore::Compact(const bbcoop::StoredWorld& world, const QDate& today) {
    QDir{}.mkpath(folder);
    const QDir dir{folder};
    const QString path = dir.filePath(WorldFile);
    if (QFileInfo::exists(path)) {
        const auto backup = [&](int number) {
            return dir.filePath(QStringLiteral("%1.%2").arg(WorldFile).arg(number));
        };
        QFile::remove(backup(Backups));
        for (int number = Backups - 1; number >= 1; --number) {
            QFile::rename(backup(number), backup(number + 1));
        }
        QFile::copy(path, backup(1));
    }
    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(Serialized(world));
    if (!file.commit()) {
        return false;
    }
    // The day's copy, once a day; the oldest beyond a week go.
    const QDir backups{dir.filePath(QStringLiteral("backups"))};
    const QString dated = backups.filePath(
        QStringLiteral("world-%1.pb").arg(today.toString(QStringLiteral("yyyyMMdd"))));
    if (!QFileInfo::exists(dated)) {
        QDir{}.mkpath(backups.path());
        QFile::copy(path, dated);
        QStringList days =
            backups.entryList({QStringLiteral("world-*.pb")}, QDir::Files, QDir::Name);
        while (days.size() > DatedBackups) {
            QFile::remove(backups.filePath(days.takeFirst()));
        }
    }
    // The journal's records are in the world now (journal_from); a crash before this leaves them
    // to be skipped on the next load.
    QFile journal{dir.filePath(JournalFile)};
    return !journal.exists() || journal.resize(0);
}

std::optional<bbcoop::StoredTrades> WorldStore::LoadTrades() const {
    QFile file{QDir{folder}.filePath(TradesFile)};
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    bbcoop::StoredTrades trades;
    if (!trades.ParseFromArray(bytes.constData(), static_cast<int>(bytes.size()))) {
        return std::nullopt;
    }
    return trades;
}

bool WorldStore::SaveTrades(const bbcoop::StoredTrades& trades) {
    QDir{}.mkpath(folder);
    QSaveFile file{QDir{folder}.filePath(TradesFile)};
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(Serialized(trades));
    return file.commit();
}

Service::Service(QString folder, qint64 heartbeat_ms, int compact_records, qint64 compact_ms)
    : folder{std::move(folder)}, heartbeat_ms{heartbeat_ms}, compact_records{compact_records},
      compact_ms{compact_ms}, lobby{[] {
          return QStringLiteral("%1")
              .arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0'))
              .toStdString();
      }} {}

const WorldLogHost* Service::World(const QByteArray& party) const {
    const auto found = worlds.find(PartyFolderName(party));
    return found != worlds.end() ? &found->second.host : nullptr;
}

std::string Service::Who(const Member& member) const {
    return member.name.empty() ? member.online_id.toStdString() : member.name;
}

Service::Result Service::Load(qint64 now_ms) {
    Result result;
    const auto loaded = [&] {
        result.log.append(QStringLiteral("party worlds: %1, %2 loaded")
                              .arg(folder.isEmpty() ? QStringLiteral("kept in memory only")
                                                    : QStringLiteral("kept in ") + folder)
                              .arg(worlds.size()));
    };
    if (folder.isEmpty()) {
        loaded();
        return result;
    }
    LoadTeams(result);
    const QDir dir{folder};
    for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        auto store = std::make_unique<WorldStore>(dir.filePath(name));
        const auto loaded = store->Load(result.log);
        if (!loaded) {
            continue;
        }
        PartyWorld& world = worlds[name];
        world.name = name;
        world.world = loaded->world.world();
        world.logs = loaded->world.logs();
        world.last_wipe_ms = loaded->world.last_wipe_ms();
        world.journal_from = loaded->world.journal_from();
        world.records = world.journal_from;
        world.host.Restore(StateOf(loaded->world.host()));
        for (const auto& [member, state] : world.host.Save().members) {
            world.proposed[member] = state.kills;
        }
        for (const bbcoop::JournalRecord& record : loaded->journal) {
            Replay(world, record);
            world.records = std::max<quint64>(world.records, record.number() + 1);
        }
        if (const auto trades = store->LoadTrades()) {
            world.trades.Restore(*trades, now_ms);
            result.log.append(QStringLiteral("party world %1: %2 open trades, %3 grants owed")
                                  .arg(name)
                                  .arg(world.trades.OpenTrades())
                                  .arg(world.trades.PendingGrants()));
        }
        world.store = std::move(store);
        world.compacted_ms = now_ms;
        result.log.append(QStringLiteral("party world %1 loaded: log %2 at seq %3, %4 kills "
                                         "(%5 journal records)")
                              .arg(name, Hex(world.host.Epoch()))
                              .arg(world.host.Last())
                              .arg(WorldLogKillCount(world.host.Kills()))
                              .arg(loaded->journal.size()));
        if (!loaded->journal.isEmpty()) {
            Compact(world, now_ms);
        }
    }
    loaded();
    return result;
}

void Service::Replay(PartyWorld& world, const bbcoop::JournalRecord& record) {
    switch (record.change_case()) {
    case bbcoop::JournalRecord::kStart:
        world.host.Start(record.start().epoch(), record.start().after_wipe());
        world.logs = std::max(world.logs, record.start().logs());
        if (record.start().after_wipe()) {
            world.last_wipe_ms = record.at_ms();
        }
        world.proposed.clear();
        break;
    case bbcoop::JournalRecord::kPropose: {
        const WorldLogProposal proposal =
            ProposalOf(record.propose().kills(), record.propose().member());
        if (!world.host.Propose(proposal).stale) {
            world.proposed[proposal.member] = proposal.kills;
        }
        break;
    }
    case bbcoop::JournalRecord::kDrop:
        world.host.Drop(record.drop().member());
        world.proposed.erase(record.drop().member());
        break;
    case bbcoop::JournalRecord::kShown:
        world.host.Show(record.shown().map(), CharactersOf(record.shown()));
        break;
    case bbcoop::JournalRecord::kFlags:
        world.host.ProposeFlags(FlagsOf(record.flags().flags(), record.flags().member()),
                                record.at_ms());
        break;
    case bbcoop::JournalRecord::kMapState: {
        const std::string& bits = record.map_state().bits();
        world.host.MapState(record.map_state().map(), std::vector<u8>(bits.begin(), bits.end()));
        break;
    }
    case bbcoop::JournalRecord::kReturnPoint:
        world.host.ReturnPoint(record.return_point().point(), record.return_point().map());
        break;
    case bbcoop::JournalRecord::kHp:
        world.host.ProposeHp(HpOf(record.hp().hp(), record.hp().member()));
        break;
    default:
        break;
    }
}

void Service::Journal(PartyWorld& world, bbcoop::JournalRecord record, qint64 now_ms) {
    record.set_number(world.records++);
    record.set_at_ms(now_ms);
    if (world.store) {
        world.store->Append(record);
    }
    if (world.records - world.journal_from >= static_cast<quint64>(compact_records)) {
        Compact(world, now_ms);
    }
}

void Service::Compact(PartyWorld& world, qint64 now_ms) {
    world.compacted_ms = now_ms;
    world.journal_from = world.records;
    if (!world.store) {
        return;
    }
    bbcoop::StoredWorld stored;
    stored.set_version(StoreVersion);
    stored.set_world(world.world);
    stored.set_logs(world.logs);
    stored.set_last_wipe_ms(world.last_wipe_ms);
    stored.set_journal_from(world.journal_from);
    *stored.mutable_host() = ToMessage(world.host.Save());
    world.store->Compact(stored);
}

Service::PartyWorld& Service::WorldFor(const QString& name, qint64 now_ms, Result& result) {
    const auto found = worlds.find(name);
    if (found != worlds.end()) {
        return found->second;
    }
    PartyWorld& world = worlds[name];
    world.name = name;
    world.world = QRandomGenerator::global()->generate() | 1u;
    world.compacted_ms = now_ms;
    if (!folder.isEmpty()) {
        world.store = std::make_unique<WorldStore>(QDir{folder}.filePath(name));
    }
    result.log.append(QStringLiteral("party world %1: a new world %2").arg(name, Hex(world.world)));
    StartLog(world, false, now_ms, result);
    Compact(world, now_ms); // on disk at once: a journal alone is no world
    return world;
}

void Service::StartLog(PartyWorld& world, bool after_wipe, qint64 now_ms, Result& result) {
    const u64 epoch = WorldLogEpoch(world.world, ++world.logs);
    const std::vector<WorldLogEntry> entries = world.host.Start(epoch, after_wipe);
    world.proposed.clear();
    if (after_wipe) {
        world.last_wipe_ms = now_ms;
    }
    bbcoop::JournalRecord record;
    record.mutable_start()->set_epoch(epoch);
    record.mutable_start()->set_after_wipe(after_wipe);
    record.mutable_start()->set_logs(world.logs);
    Journal(world, std::move(record), now_ms);
    result.log.append(QStringLiteral("party world %1: log %2 starts (%3, no kills stand)")
                          .arg(world.name, Hex(epoch), after_wipe ? "a wipe" : "a new session"));
    SendEntries(world, entries, now_ms);
}

void Service::SendTo(Member& member, const bbcoop::Envelope& envelope, qint64 now_ms) {
    member.stream.Send(EncodeEnvelope(envelope), now_ms);
}

void Service::SendEntries(PartyWorld& world, const std::vector<WorldLogEntry>& entries,
                          qint64 now_ms) {
    bbcoop::Envelope envelope;
    *envelope.mutable_entries() = ToMessage(world.host.Epoch(), world.host.Last(), entries);
    for (auto& [key, member] : members) {
        if (member.party == world.name) {
            SendTo(member, envelope, now_ms);
        }
    }
    world.heartbeat_ms = now_ms;
}

void Service::OnHello(Member& member, const bbcoop::Hello& hello, qint64 now_ms, Result& result) {
    bbcoop::Envelope answer;
    bbcoop::Welcome& welcome = *answer.mutable_welcome();
    const QByteArray party = QByteArray::fromStdString(hello.party()).trimmed();
    if (hello.protocol() != CoopProtocol) {
        welcome.set_text("co-op protocol " + std::to_string(hello.protocol()) +
                         ", this server speaks " + std::to_string(CoopProtocol));
    } else if (party.indexOf(':') <= 0 || hello.member().empty()) {
        welcome.set_text("no party key or member name");
    }
    if (!welcome.text().empty()) {
        SendTo(member, answer, now_ms);
        result.log.append(
            QStringLiteral("party world: refused %1: %2")
                .arg(QString::fromStdString(Who(member)), QString::fromStdString(welcome.text())));
        return;
    }
    // The same run saying hello again (a guest whose game took the session host's role: phase 7).
    const bool same_run =
        !member.name.empty() && member.name == hello.member() && member.run == hello.run();
    member.party = PartyFolderName(party);
    const bool team = party.startsWith("team:");
    member.name = hello.member();
    member.run = hello.run();
    member.host = hello.host();
    member.enemy_owner = hello.enemy_owner();
    PartyWorld& world = WorldFor(member.party, now_ms, result);
    // Phase 7, host migration: the session's host is whoever hosts its open session. The room's
    // owner passes to a guest when the host's game goes, and that guest's game says hello again,
    // hosting now; a new run saying it hosts (its team's leader, back) is a guest of that one.
    if (member.host && !same_run && world.session && world.session->host() != member.name &&
        SessionHostLinked(world)) {
        member.host = false;
        result.log.append(QStringLiteral("party world %1: %2 says it hosts, but %3 hosts the "
                                         "session: it joins as a guest")
                              .arg(world.name, QString::fromStdString(member.name),
                                   QString::fromStdString(world.session->host())));
    }
    // The host's game started again: the session its last run opened is gone with it.
    if (world.session && world.session->host() == member.name && world.session_run != member.run) {
        CloseSession(world, now_ms, result);
    }
    welcome.set_accepted(true);
    welcome.set_trades(true);
    welcome.set_epoch(world.host.Epoch());
    welcome.set_last(world.host.Last());
    SendTo(member, answer, now_ms);
    bbcoop::Envelope snapshot;
    *snapshot.mutable_snapshot() = SnapshotOf(world.host);
    SendTo(member, snapshot, now_ms);
    // The boards as they are, and the others' portraits.
    SendBoards(world,
               PartyBoards::Maps | PartyBoards::Rests | PartyBoards::TrophyBoard |
                   PartyBoards::VitalsBoard,
               now_ms, &member);
    for (const auto& [name, portrait] : world.portraits) {
        if (name != member.name) {
            bbcoop::Envelope envelope;
            *envelope.mutable_portrait() = portrait;
            SendTo(member, envelope, now_ms);
        }
    }
    SendOwners(world, now_ms, result, &member);
    // The others' loads (the team loading screen).
    for (const auto& [name, load] : world.loads) {
        if (name != member.name) {
            bbcoop::Envelope envelope;
            *envelope.mutable_load_progress() = load;
            SendTo(member, envelope, now_ms);
        }
    }
    // Phase 7: where a guest last stood, for its run to come back there (the host's save is its
    // own place).
    if (const auto place = world.places.find(member.name);
        !member.host && place != world.places.end() &&
        place->second.character == CharacterOf(member.name)) {
        bbcoop::Envelope envelope;
        *envelope.mutable_resume_at() = place->second.at;
        SendTo(member, envelope, now_ms);
    }
    // The host's open session: a guest joins it once it stands in its world.
    if (world.session && world.session->host() != member.name) {
        bbcoop::Envelope envelope;
        *envelope.mutable_team_session() = *world.session;
        SendTo(member, envelope, now_ms);
    }
    // What its trades owe it: grants not yet added, offers waiting for its answer.
    Deliver(world, world.trades.Resume(member.name, now_ms), now_ms, result);
    world.team = world.team || team;
    if (world.team) {
        SendTeamWorld(world, member, now_ms);
    }
    result.log.append(QStringLiteral("party world %1: %2 (run %3) follows log %4 at seq %5")
                          .arg(world.name, QString::fromStdString(member.name))
                          .arg(Hex(member.run), Hex(world.host.Epoch()))
                          .arg(world.host.Last()));
}

void Service::OnLifecycle(Member& member, const bbcoop::Lifecycle& lifecycle, qint64 now_ms,
                          Result& result) {
    PartyWorld& world = worlds.at(member.party);
    const QString who = QString::fromStdString(member.name);
    switch (lifecycle.event()) {
    case bbcoop::Lifecycle::WIPE:
        if (world.last_wipe_ms != 0 && now_ms - world.last_wipe_ms < WorldLogWipeGraceMs) {
            result.log.append(QStringLiteral("party world %1: %2's wipe: the party's wipe log "
                                             "already started")
                                  .arg(world.name, who));
            return;
        }
        result.log.append(QStringLiteral("party world %1: %2 wiped").arg(world.name, who));
        StartLog(world, true, now_ms, result);
        return;
    case bbcoop::Lifecycle::JOIN:
    case bbcoop::Lifecycle::LEAVE:
    case bbcoop::Lifecycle::NEW_CHARACTER: {
        const QString event =
            QString::fromStdString(bbcoop::Lifecycle::Event_Name(lifecycle.event()));
        if (lifecycle.host()) {
            result.log.append(QStringLiteral("party world %1: the host %2: %3")
                                  .arg(world.name, who, event.toLower()));
            StartLog(world, false, now_ms, result);
            return;
        }
        const std::vector<WorldLogEntry> entries = world.host.Drop(member.name);
        world.proposed.erase(member.name);
        if (lifecycle.event() == bbcoop::Lifecycle::LEAVE) {
            SendBoards(world, world.boards.Forget(member.name), now_ms);
            if (world.owners.Forget(member.name)) {
                SendOwners(world, now_ms, result);
            }
        }
        bbcoop::JournalRecord record;
        record.mutable_drop()->set_member(member.name);
        Journal(world, std::move(record), now_ms);
        result.log.append(QStringLiteral("party world %1: %2: %3, its share goes (%4 entries)")
                              .arg(world.name, who, event.toLower())
                              .arg(entries.size()));
        if (!entries.empty()) {
            SendEntries(world, entries, now_ms);
        }
        return;
    }
    default:
        return;
    }
}

void Service::Take(Member& member, const bbcoop::Envelope& envelope, qint64 now_ms,
                   Result& result) {
    if (envelope.has_hello()) {
        OnHello(member, envelope.hello(), now_ms, result);
        return;
    }
    if (envelope.has_lobby_hello()) {
        OnLobbyHello(member, envelope.lobby_hello(), now_ms, result);
        return;
    }
    if (envelope.has_team_request()) {
        OnTeamRequest(member, envelope.team_request(), now_ms, result);
        return;
    }
    if (member.party.isEmpty()) {
        return; // nothing before its hello
    }
    PartyWorld& world = worlds.at(member.party);
    switch (envelope.body_case()) {
    case bbcoop::Envelope::kLifecycle:
        OnLifecycle(member, envelope.lifecycle(), now_ms, result);
        break;
    case bbcoop::Envelope::kKills: {
        WorldLogProposal proposal = ProposalOf(envelope.kills(), member.name);
        // Phase 4: a new kill on an area another member runs is not this member's to make (the
        // kills it made there before stand).
        const auto before = world.proposed.find(member.name);
        size_t refused = 0;
        for (auto& [map, handles] : proposal.kills) {
            if (Decides(world, member, map)) {
                continue;
            }
            refused += std::erase_if(handles, [&](u32 handle) {
                return before == world.proposed.end() || !before->second.contains(map) ||
                       !before->second.at(map).contains(handle);
            });
        }
        std::erase_if(proposal.kills, [](const auto& entry) { return entry.second.empty(); });
        if (refused > 0) {
            result.log.append(QStringLiteral("party world %1: %2's %3 new kills are not its to "
                                             "make (another member runs their area)")
                                  .arg(world.name, QString::fromStdString(member.name))
                                  .arg(refused));
        }
        bbcoop::Envelope taken_envelope = envelope;
        *taken_envelope.mutable_kills() = ToMessage(proposal);
        const WorldLogHost::Taken taken = world.host.Propose(proposal);
        if (taken.stale) {
            break;
        }
        const auto proposed = world.proposed.find(member.name);
        if (proposed == world.proposed.end() || proposed->second != proposal.kills) {
            world.proposed[member.name] = proposal.kills;
            bbcoop::JournalRecord record;
            record.mutable_propose()->set_member(member.name);
            *record.mutable_propose()->mutable_kills() = taken_envelope.kills();
            Journal(world, std::move(record), now_ms);
        }
        if (!taken.entries.empty()) {
            QStringList kills;
            for (const WorldLogEntry& entry : taken.entries) {
                kills.append(QStringLiteral("%1 %2 on %3")
                                 .arg(entry.kind == WorldLogKind::Kill ? "kill" : "unkill",
                                      Hex(entry.subject), Hex(entry.map)));
            }
            result.log.append(QStringLiteral("party world %1: %2's kills: %3")
                                  .arg(world.name, QString::fromStdString(member.name),
                                       kills.join(QStringLiteral(", "))));
            SendEntries(world, taken.entries, now_ms);
        }
        break;
    }
    case bbcoop::Envelope::kTeamWorld:
        TakeTeamWorld(member, world, envelope.team_world(), now_ms, result);
        break;
    case bbcoop::Envelope::kEnemyHit: {
        bbcoop::Envelope relayed = envelope;
        relayed.mutable_enemy_hit()->set_member(member.name);
        SendToOthers(world, member, relayed, now_ms);
        break;
    }
    case bbcoop::Envelope::kLoadProgress: {
        bbcoop::Envelope relayed = envelope;
        relayed.mutable_load_progress()->set_member(member.name);
        world.loads[member.name] = relayed.load_progress();
        SendToOthers(world, member, relayed, now_ms);
        break;
    }
    case bbcoop::Envelope::kTeamSession: {
        // Only the host's session is the team's to join (a guest's says nothing).
        if (!member.host) {
            break;
        }
        // A former host's close says nothing about the session another member hosts now.
        if (envelope.team_session().join().empty() && world.session &&
            world.session->host() != member.name) {
            break;
        }
        bbcoop::Envelope relayed = envelope;
        relayed.mutable_team_session()->set_host(member.name);
        if (relayed.team_session().join().empty()) {
            world.session.reset();
        } else {
            world.session = relayed.team_session();
            world.session_run = member.run;
        }
        result.log.append(QStringLiteral("party world %1: %2's session %3 %4")
                              .arg(world.name, QString::fromStdString(member.name))
                              .arg(relayed.team_session().session())
                              .arg(world.session ? QStringLiteral("open (%1 bytes)")
                                                       .arg(relayed.team_session().join().size())
                                                 : QStringLiteral("closed")));
        SendToOthers(world, member, relayed, now_ms);
        break;
    }
    case bbcoop::Envelope::kShown:
    case bbcoop::Envelope::kFlags:
    case bbcoop::Envelope::kMapState:
    case bbcoop::Envelope::kReturnPoint:
    case bbcoop::Envelope::kHp:
        TakeWorld(member, world, envelope, now_ms, result);
        break;
    case bbcoop::Envelope::kMap:
    case bbcoop::Envelope::kRest:
    case bbcoop::Envelope::kTrophies:
    case bbcoop::Envelope::kEchoes:
    case bbcoop::Envelope::kVitals:
    case bbcoop::Envelope::kPortrait:
        TakeBoard(member, world, envelope, now_ms, result);
        break;
    case bbcoop::Envelope::kTradeOffer:
    case bbcoop::Envelope::kTradeAnswer:
    case bbcoop::Envelope::kTradeCancel:
    case bbcoop::Envelope::kTradeGranted:
        TakeTrade(member, world, envelope, now_ms, result);
        break;
    case bbcoop::Envelope::kSnapshotRequest: {
        bbcoop::Envelope snapshot;
        *snapshot.mutable_snapshot() = SnapshotOf(world.host);
        SendTo(member, snapshot, now_ms);
        break;
    }
    default:
        break; // the games' messages only
    }
}

void Service::TakeWorld(Member& member, PartyWorld& world, const bbcoop::Envelope& envelope,
                        qint64 now_ms, Result& result) {
    bbcoop::JournalRecord record;
    std::vector<WorldLogEntry> entries;
    bool changed = false;
    switch (envelope.body_case()) {
    case bbcoop::Envelope::kShown:
        if (!member.host) {
            return; // only the host's world is the party's (a guest's follows it)
        }
        entries = world.host.Show(envelope.shown().map(), CharactersOf(envelope.shown()));
        *record.mutable_shown() = envelope.shown();
        changed = !entries.empty();
        break;
    case bbcoop::Envelope::kMapState: {
        if (!member.host) {
            return;
        }
        const std::string& bits = envelope.map_state().bits();
        entries = world.host.MapState(envelope.map_state().map(),
                                      std::vector<u8>(bits.begin(), bits.end()));
        *record.mutable_map_state() = envelope.map_state();
        changed = !entries.empty();
        break;
    }
    case bbcoop::Envelope::kReturnPoint:
        if (!member.host) {
            return;
        }
        entries =
            world.host.ReturnPoint(envelope.return_point().point(), envelope.return_point().map());
        *record.mutable_return_point() = envelope.return_point();
        changed = !entries.empty();
        break;
    case bbcoop::Envelope::kFlags: {
        const WorldLogHost::Taken taken =
            world.host.ProposeFlags(FlagsOf(envelope.flags(), member.name), now_ms);
        if (taken.stale) {
            return;
        }
        entries = taken.entries;
        record.mutable_flags()->set_member(member.name);
        *record.mutable_flags()->mutable_flags() = envelope.flags();
        // Resends of events taken already change nothing (a restart that loses a repeat's number
        // takes the repeat again: a flag set twice).
        changed = !entries.empty();
        if (!entries.empty()) {
            QStringList flags;
            for (const WorldLogEntry& entry : entries) {
                flags.append(QStringLiteral("%1 %2")
                                 .arg(static_cast<qint32>(entry.subject))
                                 .arg(entry.kind == WorldLogKind::FlagSet ? "on" : "off"));
            }
            result.log.append(QStringLiteral("party world %1: %2's flags: %3")
                                  .arg(world.name, QString::fromStdString(member.name),
                                       flags.join(QStringLiteral(", "))));
        }
        break;
    }
    case bbcoop::Envelope::kHp: {
        // Another member runs that area: its HP is the owner's to report (phase 8: no guest runs
        // an enemy on a map that has an owner).
        const bbcoop::HpProposal& hp = envelope.hp();
        if (!Decides(world, member, hp.map())) {
            return;
        }
        const WorldLogHost::Taken taken = world.host.ProposeHp(HpOf(hp, member.name));
        if (taken.stale) {
            return;
        }
        entries = taken.entries;
        record.mutable_hp()->set_member(member.name);
        *record.mutable_hp()->mutable_hp() = envelope.hp();
        changed = !entries.empty();
        break;
    }
    default:
        return;
    }
    if (changed) {
        Journal(world, std::move(record), now_ms);
    }
    if (!entries.empty()) {
        SendEntries(world, entries, now_ms);
    }
}

void Service::TakeBoard(Member& member, PartyWorld& world, const bbcoop::Envelope& envelope,
                        qint64 now_ms, Result& result) {
    // A member's part changes its board for everyone; else the board goes back to the member
    // alone (as the HTTP boards answered every post): its copy stays fresh.
    const auto answer = [&](unsigned changed, unsigned board) {
        if (changed != PartyBoards::None) {
            SendBoards(world, changed, now_ms);
        } else {
            SendBoards(world, board, now_ms, &member);
        }
    };
    switch (envelope.body_case()) {
    case bbcoop::Envelope::kMap: {
        member.map = envelope.map().map();
        // A load (map 0) is no place on the boards, only for the enemy owners.
        if (envelope.map().map() != 0) {
            answer(world.boards.Map(member.name, envelope.map().map(), now_ms), PartyBoards::Maps);
        }
        // The owners go with every answer to a member's map (to everyone when they changed).
        const bool changed =
            world.owners.Place(member.name, envelope.map().map(), now_ms, member.host);
        SendOwners(world, now_ms, result, changed ? nullptr : &member);
        return;
    }
    case bbcoop::Envelope::kRest:
        answer(world.boards.Rest(member.name, envelope.rest(), now_ms), PartyBoards::Rests);
        return;
    case bbcoop::Envelope::kTrophies:
        answer(world.boards.Trophies(envelope.trophies()), PartyBoards::TrophyBoard);
        return;
    case bbcoop::Envelope::kVitals: {
        if (const bbcoop::Vitals& vitals = envelope.vitals();
            vitals.map() != 0 && (vitals.vital() == 1 || vitals.vital() == 2) &&
            vitals.has_at()) { // up or down where it stands
            PartyWorld::Place& place = world.places[member.name];
            place.at.set_map(vitals.map());
            *place.at.mutable_at() = vitals.at();
            place.character = CharacterOf(member.name);
        }
        answer(world.boards.Vitals(member.name, envelope.vitals(), now_ms),
               PartyBoards::VitalsBoard);
        // Logged when someone's state or the wipes change, not for every HP change.
        static constexpr const char* Vitals[] = {"off", "up", "down", "dead"};
        const bbcoop::VitalsBoard board = world.boards.VitalsMessage();
        QStringList parts;
        for (const auto& entry : board.members()) {
            parts.append(QStringLiteral("%1 %2 %3")
                             .arg(QString::fromStdString(entry.member()))
                             .arg(Vitals[std::min<u32>(entry.vitals().vital(), 3)])
                             .arg(Hex(entry.vitals().map())));
        }
        for (const auto& wipe : board.wipes()) {
            QStringList members;
            for (const std::string& name : wipe.members()) {
                members.append(QString::fromStdString(name));
            }
            parts.append(QStringLiteral("wipe %1 of %2 (%3)")
                             .arg(wipe.id())
                             .arg(Hex(wipe.map()), members.join(' ')));
        }
        const QString logged = parts.join(QStringLiteral(", "));
        if (logged != world.vitals_logged) {
            world.vitals_logged = logged;
            result.log.append(QStringLiteral("party world %1: vitals: %2").arg(world.name, logged));
        }
        return;
    }
    case bbcoop::Envelope::kEchoes:
        // Each kill's echoes to every other member once (the stream delivers them once).
        if (envelope.echoes().kills_size() > 0) {
            SendToOthers(world, member, envelope, now_ms);
        }
        return;
    case bbcoop::Envelope::kPortrait: {
        bbcoop::Envelope relayed = envelope;
        relayed.mutable_portrait()->set_member(member.name);
        world.portraits[member.name] = relayed.portrait();
        SendToOthers(world, member, relayed, now_ms);
        return;
    }
    default:
        return;
    }
}

void Service::SendBoards(PartyWorld& world, unsigned changed, qint64 now_ms, Member* only) {
    std::vector<bbcoop::Envelope> boards;
    if ((changed & PartyBoards::Maps) != 0) {
        *boards.emplace_back().mutable_map_board() = world.boards.MapMessage();
    }
    if ((changed & PartyBoards::Rests) != 0) {
        *boards.emplace_back().mutable_rest_board() = world.boards.RestMessage();
    }
    if ((changed & PartyBoards::TrophyBoard) != 0) {
        *boards.emplace_back().mutable_trophies() = world.boards.TrophyMessage();
    }
    if ((changed & PartyBoards::VitalsBoard) != 0) {
        *boards.emplace_back().mutable_vitals_board() = world.boards.VitalsMessage();
    }
    for (const bbcoop::Envelope& board : boards) {
        if (only != nullptr) {
            SendTo(*only, board, now_ms);
            continue;
        }
        for (auto& [key, member] : members) {
            if (member.party == world.name) {
                SendTo(member, board, now_ms);
            }
        }
    }
}

void Service::SendOwners(PartyWorld& world, qint64 now_ms, Result& result, Member* only) {
    bbcoop::Envelope envelope;
    *envelope.mutable_enemy_owners() = world.owners.Message();
    QStringList owners;
    for (const bbcoop::EnemyOwner& owner : envelope.enemy_owners().owners()) {
        owners.append(QStringLiteral("%1 %2 (epoch %3)")
                          .arg(Hex(owner.map()), QString::fromStdString(owner.member()))
                          .arg(owner.epoch()));
    }
    const QString logged = owners.isEmpty() ? QStringLiteral("none") : owners.join(", ");
    if (logged != world.owners_logged) {
        world.owners_logged = logged;
        result.log.append(
            QStringLiteral("party world %1: enemy owners: %2").arg(world.name, logged));
    }
    if (only != nullptr) {
        SendTo(*only, envelope, now_ms);
        return;
    }
    for (auto& [key, member] : members) {
        if (member.party == world.name) {
            SendTo(member, envelope, now_ms);
        }
    }
}

bool Service::Decides(const PartyWorld& world, const Member& member, u32 map) const {
    if (!member.enemy_owner) {
        return true;
    }
    const std::string owner = world.owners.OwnerOf(map);
    return owner.empty() || owner == member.name;
}

void Service::TakeTrade(Member& member, PartyWorld& world, const bbcoop::Envelope& envelope,
                        qint64 now_ms, Result& result) {
    switch (envelope.body_case()) {
    case bbcoop::Envelope::kTradeOffer: {
        std::set<std::string> online;
        for (const auto& [key, other] : members) {
            if (other.party == world.name) {
                online.insert(other.name);
            }
        }
        Deliver(world, world.trades.Offer(member.name, envelope.trade_offer(), online, now_ms),
                now_ms, result);
        return;
    }
    case bbcoop::Envelope::kTradeAnswer:
        Deliver(world, world.trades.Answer(member.name, envelope.trade_answer()), now_ms, result);
        return;
    case bbcoop::Envelope::kTradeCancel:
        Deliver(world, world.trades.Cancel(member.name, envelope.trade_cancel().id()), now_ms,
                result);
        return;
    case bbcoop::Envelope::kTradeGranted:
        Deliver(world, world.trades.Granted(member.name, envelope.trade_granted().id()), now_ms,
                result);
        return;
    default:
        return;
    }
}

void Service::Deliver(PartyWorld& world, TradeDesk::Result trades, qint64 now_ms, Result& result) {
    for (const TradeDesk::Send& send : trades.sends) {
        for (auto& [key, member] : members) {
            if (member.party == world.name && member.name == send.to) {
                SendTo(member, send.envelope, now_ms);
            }
        }
    }
    for (const std::string& line : trades.log) {
        result.log.append(
            QStringLiteral("party world %1: %2").arg(world.name, QString::fromStdString(line)));
    }
    if (trades.changed && world.store) {
        world.store->SaveTrades(world.trades.Stored());
    }
}

void Service::SendToOthers(PartyWorld& world, const Member& from, const bbcoop::Envelope& envelope,
                           qint64 now_ms) {
    for (auto& [key, member] : members) {
        if (member.party == world.name && &member != &from) {
            SendTo(member, envelope, now_ms);
        }
    }
}

Service::Result Service::Receive(const ServerLink::Endpoint& from, const QByteArray& online_id,
                                 const QByteArray& body, qint64 now_ms) {
    Result result;
    auto [found, added] =
        members.try_emplace(from.Key(), QRandomGenerator::global()->generate() | 1u);
    Member& member = found->second;
    endpoints[from.Key()] = from;
    member.online_id = online_id;
    const auto bytes =
        std::span{reinterpret_cast<const u8*>(body.constData()), static_cast<size_t>(body.size())};
    const std::vector<std::vector<u8>> messages = member.stream.Receive(bytes, now_ms);
    if (member.stream.TakeRestarted()) {
        result.log.append(QStringLiteral("party world: %1's game started over: waiting for its "
                                         "hello")
                              .arg(QString::fromStdString(Who(member))));
        member.party.clear();
    }
    for (const std::vector<u8>& message : messages) {
        if (const auto envelope = DecodeEnvelope(message)) {
            Take(member, *envelope, now_ms, result);
        }
    }
    Flush(now_ms, result);
    return result;
}

void Service::Flush(qint64 now_ms, Result& result) {
    for (auto& [key, member] : members) {
        for (const std::vector<u8>& body : member.stream.Flush(now_ms)) {
            result.sends.append(
                {endpoints.at(key), QByteArray(reinterpret_cast<const char*>(body.data()),
                                               static_cast<qsizetype>(body.size()))});
        }
    }
}

Service::Result Service::Tick(qint64 now_ms) {
    Result result;
    if (lobby_changed) {
        SendLobby(now_ms);
    }
    for (auto& [name, world] : worlds) {
        if (world.records > world.journal_from && now_ms - world.compacted_ms >= compact_ms) {
            Compact(world, now_ms);
        }
        if (now_ms - world.heartbeat_ms >= heartbeat_ms) {
            SendEntries(world, {}, now_ms);
        }
        SendBoards(world, world.boards.Expire(now_ms), now_ms);
        if (world.owners.Expire(now_ms)) {
            SendOwners(world, now_ms, result);
        }
        Deliver(world, world.trades.Expire(now_ms), now_ms, result);
    }
    Flush(now_ms, result);
    return result;
}

std::string Service::CharacterOf(const std::string& member) const {
    if (const bbcoop::Team* team = lobby.TeamOf(member)) {
        for (const bbcoop::TeamMember& m : team->members()) {
            if (m.member() == member) {
                return std::to_string(m.slot()) + ":" + m.character();
            }
        }
    }
    return {};
}

bool Service::SessionHostLinked(const PartyWorld& world) const {
    return world.session && std::any_of(members.begin(), members.end(), [&](const auto& entry) {
               return entry.second.party == world.name &&
                      entry.second.name == world.session->host();
           });
}

void Service::CloseSession(PartyWorld& world, qint64 now_ms, Result& result) {
    bbcoop::Envelope closed;
    closed.mutable_team_session()->set_host(world.session->host());
    closed.mutable_team_session()->set_session(world.session->session());
    result.log.append(QStringLiteral("party world %1: %2's session %3 closed with its game")
                          .arg(world.name, QString::fromStdString(world.session->host()))
                          .arg(world.session->session()));
    world.session.reset();
    for (auto& [key, member] : members) {
        if (member.party == world.name && member.name != closed.team_session().host()) {
            SendTo(member, closed, now_ms);
        }
    }
}

void Service::Forget(const ServerLink::Endpoint& link) {
    const auto found = members.find(link.Key());
    if (found != members.end() && !found->second.lobby.empty()) {
        const std::string name = found->second.lobby;
        const bool elsewhere = std::any_of(members.begin(), members.end(), [&](const auto& m) {
            return m.first != link.Key() && m.second.lobby == name;
        });
        if (!elsewhere) {
            lobby.Leave(name);
            lobby_changed = true;
        }
    }
    members.erase(link.Key());
    endpoints.erase(link.Key());
}

QString Service::PartyOf(const ServerLink::Endpoint& link) const {
    const auto found = members.find(link.Key());
    return found == members.end() ? QString{} : found->second.party;
}

bool Service::RoutesPacket(const ServerLink::Endpoint& from, const ServerLink::Endpoint& to,
                           std::uint32_t channel) const {
    using BloodborneCoop::EnemyPart;
    const auto sender = members.find(from.Key());
    const auto target = members.find(to.Key());
    if (sender == members.end() || target == members.end()) {
        return BloodborneCoop::NativeChannelPolicy(channel) ==
                   BloodborneCoop::ChannelPolicy::Pass &&
               channel < BloodborneCoop::NativeChannelCount;
    }
    const auto world = worlds.find(sender->second.party);
    const auto part = [&](const Member& member) {
        if (member.map == 0 || world == worlds.end()) {
            return EnemyPart::None;
        }
        const std::string owner = world->second.owners.OwnerOf(member.map);
        return owner.empty()          ? EnemyPart::None
               : owner == member.name ? EnemyPart::Owner
                                      : EnemyPart::Follower;
    };
    return BloodborneCoop::RoutesNativePacket(
        channel,
        {sender->second.map, target->second.map, part(sender->second), part(target->second)});
}

namespace {
const QString TeamsFile = QStringLiteral("teams.pb");
const QString TeamWorldFile = QStringLiteral("team_world.pb");
} // namespace

const std::optional<bbcoop::TeamWorld>& Service::TeamWorldOf(PartyWorld& world) {
    if (!world.team_world_read) {
        world.team_world_read = true;
        if (!folder.isEmpty()) {
            QFile file{QDir{QDir{folder}.filePath(world.name)}.filePath(TeamWorldFile)};
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray bytes = file.readAll();
                bbcoop::TeamWorld stored;
                if (stored.ParseFromArray(bytes.constData(), static_cast<int>(bytes.size()))) {
                    world.team_world = std::move(stored);
                }
            }
        }
    }
    return world.team_world;
}

void Service::SendTeamWorld(PartyWorld& world, Member& member, qint64 now_ms) {
    bbcoop::Envelope envelope;
    if (const auto& team_world = TeamWorldOf(world)) {
        *envelope.mutable_team_world() = *team_world;
        envelope.mutable_team_world()->set_exists(true);
    } else {
        envelope.mutable_team_world()->set_exists(false);
    }
    SendTo(member, envelope, now_ms);
}

void Service::TakeTeamWorld(Member& member, PartyWorld& world, const bbcoop::TeamWorld& report,
                            qint64 now_ms, Result& result) {
    if (!world.team || !member.host) {
        return; // only the team's host's world is the team's
    }
    const auto& before = TeamWorldOf(world);
    if (before &&
        std::equal(before->on().begin(), before->on().end(), report.on().begin(),
                   report.on().end()) &&
        std::equal(before->blocks().begin(), before->blocks().end(), report.blocks().begin(),
                   report.blocks().end())) {
        return; // the same world
    }
    bbcoop::TeamWorld taken;
    *taken.mutable_on() = report.on();
    std::sort(taken.mutable_on()->begin(), taken.mutable_on()->end());
    *taken.mutable_blocks() = report.blocks();
    taken.set_exists(true);
    taken.set_version((before ? before->version() : 0) + 1);
    world.team_world = taken;
    if (!folder.isEmpty()) {
        const QString path = QDir{folder}.filePath(world.name);
        QDir{}.mkpath(path);
        QSaveFile file{QDir{path}.filePath(TeamWorldFile)};
        if (!file.open(QIODevice::WriteOnly) || file.write(Serialized(taken)) < 0 ||
            !file.commit()) {
            result.log.append(QStringLiteral("team world %1: could not write %2")
                                  .arg(world.name, file.fileName()));
        }
    }
    result.log.append(QStringLiteral("team world %1: %2's world taken (%3 flags on, version %4)")
                          .arg(world.name, QString::fromStdString(member.name))
                          .arg(taken.on_size())
                          .arg(taken.version()));
    for (auto& [key, other] : members) {
        if (other.party == world.name) {
            SendTeamWorld(world, other, now_ms);
        }
    }
}

void Service::OnLobbyHello(Member& member, const bbcoop::LobbyHello& hello, qint64 now_ms,
                           Result& result) {
    if (hello.protocol() != CoopProtocol || hello.member().empty()) {
        bbcoop::Envelope answer;
        answer.mutable_lobby_state()->set_notice(
            hello.member().empty() ? "no member name"
                                   : "co-op protocol " + std::to_string(hello.protocol()) +
                                         ", this server speaks " + std::to_string(CoopProtocol));
        SendTo(member, answer, now_ms);
        result.log.append(QStringLiteral("team lobby: refused %1: %2")
                              .arg(QString::fromStdString(Who(member)),
                                   QString::fromStdString(answer.lobby_state().notice())));
        return;
    }
    const bool arrived = member.lobby.empty();
    member.lobby = hello.member();
    if (arrived) {
        result.log.append(
            QStringLiteral("team lobby: %1 (%2) signed in")
                .arg(QString::fromStdString(hello.member()), QString::fromStdString(hello.name())));
    }
    Settle(lobby.Arrive(hello.member(), hello.name(), hello.playing()), now_ms, result);
    if (arrived) {
        // Its state even when nothing changed for the others.
        bbcoop::Envelope envelope;
        *envelope.mutable_lobby_state() = lobby.StateFor(member.lobby);
        SendTo(member, envelope, now_ms);
    }
}

void Service::OnTeamRequest(Member& member, const bbcoop::TeamRequest& request, qint64 now_ms,
                            Result& result) {
    if (member.lobby.empty()) {
        return; // nothing before its lobby hello
    }
    const TeamLobby::Outcome outcome = lobby.Request(member.lobby, request);
    result.log.append(
        QStringLiteral("team lobby: %1 %2%3")
            .arg(QString::fromStdString(member.lobby),
                 QString::fromStdString(bbcoop::TeamRequest::Action_Name(request.action())))
            .arg(outcome.notice.empty()
                     ? QString{}
                     : QStringLiteral(": refused, ") + QString::fromStdString(outcome.notice)));
    if (!outcome.notice.empty()) {
        bbcoop::Envelope envelope;
        *envelope.mutable_lobby_state() = lobby.StateFor(member.lobby);
        envelope.mutable_lobby_state()->set_notice(outcome.notice);
        SendTo(member, envelope, now_ms);
    }
    if (outcome.changed) {
        StoreTeams(result);
    }
    Settle(outcome, now_ms, result);
}

void Service::Settle(const TeamLobby::Outcome& outcome, qint64 now_ms, Result& result) {
    if (outcome.changed) {
        SendLobby(now_ms);
    }
    for (const std::string& name : outcome.starts) {
        bbcoop::TeamStart start = lobby.StartFor(name);
        // Phase 7: the team's session is hosted by whoever hosts it now (it moves to a guest when
        // the host's game goes), not always by the leader: a leader back joins that one.
        if (const auto world =
                worlds.find(PartyFolderName(QByteArray::fromStdString(start.party())));
            world != worlds.end() && world->second.session &&
            world->second.session->host() != start.host() && SessionHostLinked(world->second)) {
            start.set_host(world->second.session->host());
        }
        result.log.append(
            QStringLiteral("team lobby: %1 starts in team %2 (slot %3, host %4)")
                .arg(QString::fromStdString(name), QString::fromStdString(start.team()))
                .arg(start.slot())
                .arg(QString::fromStdString(start.host())));
        for (auto& [key, member] : members) {
            if (member.lobby == name) {
                bbcoop::Envelope envelope;
                *envelope.mutable_team_start() = start;
                SendTo(member, envelope, now_ms);
            }
        }
    }
}

void Service::SendLobby(qint64 now_ms) {
    lobby_changed = false;
    for (auto& [key, member] : members) {
        if (!member.lobby.empty()) {
            bbcoop::Envelope envelope;
            *envelope.mutable_lobby_state() = lobby.StateFor(member.lobby);
            SendTo(member, envelope, now_ms);
        }
    }
}

void Service::StoreTeams(Result& result) {
    if (folder.isEmpty()) {
        return;
    }
    QDir{}.mkpath(folder);
    QSaveFile file{QDir{folder}.filePath(TeamsFile)};
    if (!file.open(QIODevice::WriteOnly) || file.write(Serialized(lobby.Store())) < 0 ||
        !file.commit()) {
        result.log.append(QStringLiteral("team lobby: could not write %1").arg(file.fileName()));
    }
}

void Service::LoadTeams(Result& result) {
    QFile file{QDir{folder}.filePath(TeamsFile)};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray bytes = file.readAll();
    bbcoop::StoredTeams stored;
    if (!stored.ParseFromArray(bytes.constData(), static_cast<int>(bytes.size()))) {
        result.log.append(QStringLiteral("team lobby: %1 does not read").arg(file.fileName()));
        return;
    }
    lobby.Restore(stored);
    result.log.append(QStringLiteral("team lobby: %1 teams loaded").arg(stored.teams_size()));
}

} // namespace BloodborneWorld
