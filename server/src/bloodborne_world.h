// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// The party's world on the server (docs/SERVER_COOP_PLAN.md phase 3): shadNet keeps each party's
// world log (coop/world_log.h, WorldLogHost) instead of the host's game, orders what the members
// propose, stores it on disk and sends every member the entries, and a snapshot when it asks.
//
// The messages (coop/coop.proto) come in the server link's Coop datagrams, in each link's
// reliable stream (coop/coop_stream.h). A party is its key (SHADPS4_BLOODBORNE_PARTY, from the
// member's hello); on disk it goes by a hash of the key, never the key itself.
//
// The epochs are the server's: the world's id (random, kept with the world) and its count of logs
// started. A wipe starts a new log once for the party; the session's host joining, leaving or
// becoming another character starts one too (a new session); a guest's own events take only its
// share out of the log.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "bloodborne_party_boards.h"
#include "coop.pb.h"
#include "coop/coop_stream.h"
#include "coop/world_log.h"
#include "server_link.h"

namespace BloodborneWorld {

// One party's world on disk, in its folder: world.pb (the world as of its journal_from), the
// journal of changes since (journal.pb: each record its size, u32 little-endian, then the record),
// and the last three worlds before (world.pb.1-3). A world is written whole to a new file and
// renamed over the old one (QSaveFile), so a crash leaves the old one or the new one.
class WorldStore {
public:
    explicit WorldStore(QString folder);

    struct Loaded {
        bbcoop::StoredWorld world;
        QList<bbcoop::JournalRecord> journal; // the records from world.journal_from on
    };
    // The world and its journal, or nothing (no world here yet). A world file that does not read
    // falls back to the backups; a journal cut short ends at its last whole record.
    std::optional<Loaded> Load(QStringList& log) const;
    bool Append(const bbcoop::JournalRecord& record);
    // The world written whole (the old one to the backups) and the journal emptied.
    bool Compact(const bbcoop::StoredWorld& world);
    const QString& Folder() const {
        return folder;
    }

private:
    QString folder;
};

// The world key of a party: a hash of its key, safe as a folder name.
QString PartyFolderName(const QByteArray& party);

class Service {
public:
    // `folder`: where the worlds are kept (one folder per party); empty keeps them in memory only.
    explicit Service(QString folder, qint64 heartbeat_ms = 1000, int compact_records = 200,
                     qint64 compact_ms = 30'000);

    struct Send {
        ServerLink::Endpoint to;
        QByteArray body; // a Coop datagram's body
    };
    struct Result {
        QList<Send> sends;
        QStringList log;
    };

    // Every party's world in the folder (shadNet's start).
    Result Load(qint64 now_ms);
    // A Coop datagram's body from a linked game.
    Result Receive(const ServerLink::Endpoint& from, const QByteArray& online_id,
                   const QByteArray& body, qint64 now_ms);
    // Resends, acks and heartbeats; worlds written whole when their journal grew.
    Result Tick(qint64 now_ms);
    // A link went (its game is gone).
    void Forget(const ServerLink::Endpoint& link);

    // For the tests and the log.
    const BloodborneCoop::WorldLogHost* World(const QByteArray& party) const;
    int Parties() const {
        return static_cast<int>(worlds.size());
    }

private:
    struct PartyWorld {
        QString name; // PartyFolderName
        BloodborneCoop::WorldLogHost host;
        quint32 world = 0;
        quint32 logs = 0;
        qint64 last_wipe_ms = 0;
        quint64 records = 0;      // journal records written (the next one's number)
        quint64 journal_from = 0; // the first record not in the stored world
        qint64 compacted_ms = 0;
        std::unique_ptr<WorldStore> store;
        qint64 heartbeat_ms = 0;
        // Each member's last kills taken into the log: a proposal that changes them is journaled.
        std::map<std::string, BloodborneCoop::WorldLogKills> proposed;
        // Phase 3c: the boards (in memory only: they are about the games running now) and each
        // member's last portrait.
        PartyBoards boards;
        std::map<std::string, bbcoop::Portrait> portraits;
        // Phase 4: who runs each area's enemies, and what was logged of it.
        EnemyOwners owners;
        QString owners_logged;
        QString vitals_logged; // the members' states and the wipes, as last logged
    };
    struct Member {
        explicit Member(quint32 instance) : stream{"shadnet", instance} {}
        BloodborneCoop::CoopStream stream;
        QByteArray online_id;
        QString party; // PartyFolderName, empty before its hello
        std::string name;
        quint32 run = 0;
        bool host = false; // hosts the session (its hello says): its characters and map state count
        // Runs phase 4's enemy owners (its hello says): on an area another member owns its new
        // kills and its HP reports do not count.
        bool enemy_owner = false;
    };

    PartyWorld& WorldFor(const QString& name, qint64 now_ms, Result& result);
    void Take(Member& member, const bbcoop::Envelope& envelope, qint64 now_ms, Result& result);
    void OnHello(Member& member, const bbcoop::Hello& hello, qint64 now_ms, Result& result);
    void OnLifecycle(Member& member, const bbcoop::Lifecycle& lifecycle, qint64 now_ms,
                     Result& result);
    void StartLog(PartyWorld& world, bool after_wipe, qint64 now_ms, Result& result);
    void Journal(PartyWorld& world, bbcoop::JournalRecord record, qint64 now_ms);
    void Compact(PartyWorld& world, qint64 now_ms);
    void Replay(PartyWorld& world, const bbcoop::JournalRecord& record);
    // The host's reports and every member's flags and HP (phase 3b): taken into the log, journaled
    // and the entries sent.
    void TakeWorld(Member& member, PartyWorld& world, const bbcoop::Envelope& envelope,
                   qint64 now_ms, Result& result);
    // The boards' messages (phase 3c), and the changed boards to the party.
    void TakeBoard(Member& member, PartyWorld& world, const bbcoop::Envelope& envelope,
                   qint64 now_ms, Result& result);
    void SendBoards(PartyWorld& world, unsigned changed, qint64 now_ms, Member* only = nullptr);
    // The enemy owners to the party (or `only`), logged when they changed.
    void SendOwners(PartyWorld& world, qint64 now_ms, Result& result, Member* only = nullptr);
    // Whether `member` may decide the enemies of `map` (phase 4): a game without the owners, or
    // on an area nobody or itself runs.
    bool Decides(const PartyWorld& world, const Member& member, u32 map) const;
    void SendToOthers(PartyWorld& world, const Member& from, const bbcoop::Envelope& envelope,
                      qint64 now_ms);
    void SendTo(Member& member, const bbcoop::Envelope& envelope, qint64 now_ms);
    void SendEntries(PartyWorld& world, const std::vector<BloodborneCoop::WorldLogEntry>& entries,
                     qint64 now_ms);
    void Flush(qint64 now_ms, Result& result);
    std::string Who(const Member& member) const;

    QString folder;
    qint64 heartbeat_ms;
    int compact_records;
    qint64 compact_ms;
    std::map<QString, PartyWorld> worlds;
    std::map<quint64, Member> members; // by link (Endpoint::Key)
    std::map<quint64, ServerLink::Endpoint> endpoints;
};

} // namespace BloodborneWorld
