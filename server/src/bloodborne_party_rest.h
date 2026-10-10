// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <optional>

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>

namespace Bloodborne {

// Lamps as campfires: which party members rest at which lamp right now (POST
// /bloodborne/party/rest). A lamp is the event flag its travel dialog sets (7MMM01LL).
struct LampRest {
    QByteArray member;
    qint32 lamp = 0;
    QByteArray at; // "<x> <y> <z>" where the member rests, or empty
};

// "<member> <lamp> [<x> <y> <z>]", member 1-64 letters or digits; lamp 0 means not resting.
std::optional<LampRest> ParseRestUpdate(const QByteArray& body);
// One "<member> <lamp>\n" line per rest.
QByteArray FormatRestBoard(const QList<LampRest>& rests);

class PartyRestBoard {
public:
    // Members post about once a second while they rest; older posts are from members that left.
    static constexpr qint64 FreshMs = 5'000;

    // Records the member's rest and returns its party's current rests, sorted by member.
    QList<LampRest> Update(const QByteArray& party, const QByteArray& member, qint32 lamp,
                           qint64 nowMs, const QByteArray& at = {});

private:
    struct Entry {
        qint32 lamp = 0;
        QByteArray at;
        qint64 updatedAtMs = 0;
    };

    QMutex m_mutex;
    QHash<QByteArray, QHash<QByteArray, Entry>> m_parties;
};

// Seamless travel: the map each party member is on (POST /bloodborne/party/map), so a member
// knows when the others share its map.
struct MemberMap {
    QByteArray member;
    quint32 map = 0; // packed map, e.g. 0x18010000
};

// "<member> <map>", the map in hex.
std::optional<MemberMap> ParseMapUpdate(const QByteArray& body);
// One "<member> <map>\n" line per member, the map in hex.
QByteArray FormatMapBoard(const QList<MemberMap>& maps);

class PartyMapBoard {
public:
    static constexpr qint64 FreshMs = 5'000;

    // Records the member's map and returns its party's members, sorted by member.
    QList<MemberMap> Update(const QByteArray& party, const QByteArray& member, quint32 map,
                            qint64 nowMs);

private:
    struct Entry {
        quint32 map = 0;
        qint64 updatedAtMs = 0;
    };

    QMutex m_mutex;
    QHash<QByteArray, QHash<QByteArray, Entry>> m_parties;
};

// Shared trophies: every trophy a party member unlocked in the party (POST
// /bloodborne/party/trophy), so the other members unlock it too.
struct TrophyUpdate {
    QByteArray member;
    QList<qint32> trophies;
};

// "<member> [<id>...]", ids 0-127 in decimal.
std::optional<TrophyUpdate> ParseTrophyUpdate(const QByteArray& body);
// One "<id>\n" line per trophy.
QByteArray FormatTrophyBoard(const QList<qint32>& trophies);

class PartyTrophyBoard {
public:
    static constexpr qint32 MaxTrophyId = 127;

    // Adds the member's unlocks and returns every trophy its party unlocked, sorted. Unlocks
    // stay for as long as the server runs, so a member that joins later still gets them.
    QList<qint32> Update(const QByteArray& party, const QByteArray& member,
                         const QList<qint32>& trophies);

private:
    QMutex m_mutex;
    QHash<QByteArray, QList<qint32>> m_parties;
};

// Shared world changes: the event flags a party member changed in the session (POST
// /bloodborne/party/flags), so the host's world gets what a guest did there.
struct FlagChange {
    qint32 flag = 0;
    bool on = false;
};

struct FlagUpdate {
    QByteArray member;
    qint64 since = 0;
    QList<FlagChange> changes;
};

struct FlagBoardAnswer {
    qint64 latest = 0;
    QList<FlagChange> changes;
    QList<QByteArray> members; // who posted each change
};

// "<member> <since> [<flag>:<0|1>...]".
std::optional<FlagUpdate> ParseFlagUpdate(const QByteArray& body);
// A "<latest>" line, then one "<flag> <0|1>" line per change.
QByteArray FormatFlagBoard(const FlagBoardAnswer& answer);
// The kill board's answer: a "<latest>" line, then one "<handle> <0|1> <member>" line per change.
QByteArray FormatKillBoard(const FlagBoardAnswer& answer);

class PartyFlagBoard {
public:
    // Kept per party for as long as the server runs; the oldest go past this many.
    static constexpr qsizetype MaxChanges = 4096;

    // Adds the member's changes and returns the latest sequence number and the other members'
    // changes after `since`, oldest first.
    FlagBoardAnswer Update(const QByteArray& party, const QByteArray& member, qint64 since,
                           const QList<FlagChange>& changes);

private:
    struct Entry {
        qint64 sequence = 0;
        QByteArray member;
        FlagChange change;
    };
    struct Party {
        qint64 latest = 0;
        QList<Entry> entries;
    };

    QMutex m_mutex;
    QHash<QByteArray, Party> m_parties;
};

// World state per map (POST /bloodborne/party/mapstate): the latest flag bits each member
// posted for the map it is on, so a guest loading the host's map takes the host's state.
struct MapStateUpdate {
    QByteArray member;
    quint32 map = 0; // 0: only reading
    QByteArray bits; // lowercase hex, two digits per byte
};

struct MapState {
    quint32 map = 0;
    QByteArray bits;
};

// "<member> <map> [<hex>]".
std::optional<MapStateUpdate> ParseMapStateUpdate(const QByteArray& body);
// One "<map> <hex>" line per other member.
QByteArray FormatMapStateBoard(const QList<MapState>& states);

class PartyMapStateBoard {
public:
    // `freshMs` > 0: a member's state is dropped once it has not posted for that long.
    explicit PartyMapStateBoard(qint64 freshMs = 0) : m_freshMs(freshMs) {}

    // Keeps the member's state (when it posts one) and returns the other members' latest.
    QList<MapState> Update(const QByteArray& party, const QByteArray& member, quint32 map,
                           const QByteArray& bits, qint64 nowMs = 0);

private:
    struct Entry {
        MapState state;
        qint64 postedAtMs = 0;
    };

    qint64 m_freshMs;
    QMutex m_mutex;
    QHash<QByteArray, QHash<QByteArray, Entry>> m_parties;
};

// Shared kill echoes: the echoes a party member got from a kill with no other member on its map
// (POST /bloodborne/party/echoes), so the others get them too, as they would side by side.
struct EchoUpdate {
    QByteArray member;
    qint64 since = 0;
    QList<qint32> echoes;
    QList<quint32> maps; // the map each kill was made on, 0 if not given
};

struct EchoBoardAnswer {
    qint64 latest = 0;
    QList<qint32> echoes;
    QList<quint32> maps;
};

// "<member> <since> [<echoes>[:<map>]...]" (each a positive amount, the map in decimal).
std::optional<EchoUpdate> ParseEchoUpdate(const QByteArray& body);
// A "<latest>" line, then one "<echoes>" or "<echoes> <map>" line per kill.
QByteArray FormatEchoBoard(const EchoBoardAnswer& answer);

class PartyEchoBoard {
public:
    // Kept per party for as long as the server runs; the oldest go past this many.
    static constexpr qsizetype MaxEntries = 4096;

    // Adds the member's echoes and returns the latest sequence number and the other members'
    // echoes after `since`, oldest first.
    EchoBoardAnswer Update(const QByteArray& party, const QByteArray& member, qint64 since,
                           const QList<qint32>& echoes, const QList<quint32>& maps = {});

private:
    struct Entry {
        qint64 sequence = 0;
        QByteArray member;
        qint32 echoes = 0;
        quint32 map = 0;
    };
    struct Party {
        qint64 latest = 0;
        QList<Entry> entries;
    };

    QMutex m_mutex;
    QHash<QByteArray, Party> m_parties;
};

// Party death: each member's state, HP, revive progress and position (POST
// /bloodborne/party/vitals), so the others draw it and revive it, and the server decides when
// a map's party is wiped: when nobody on a map stands any more, everyone down there dies.
struct ReviveHits {
    QByteArray target;
    quint32 down = 0;
    quint32 hits = 0;
};

struct MemberVitals {
    QByteArray member;
    quint32 map = 0;  // packed map, 0 while loading
    QByteArray vital; // off, up, down or dead
    qint32 hp = 0;
    qint32 max = 0;
    quint32 down = 0;   // the id of its current or last down
    quint32 landed = 0; // revive progress
    quint32 needed = 0;
    QByteArray at;          // "<x> <y> <z>"
    quint32 reviveHits = 0; // the party's revive hits on its down (in the answer)
};

struct VitalsUpdate {
    MemberVitals self;
    QList<ReviveHits> hits;
};

struct PartyWipe {
    quint32 id = 0;
    quint32 map = 0;
    QList<QByteArray> members;
};

struct VitalsBoardAnswer {
    QList<MemberVitals> members;
    QList<PartyWipe> wipes;
};

// "<member> <map> <vital> <hp> <max> <down> <landed> <needed> <x> <y> <z>
// [<target>:<down>:<hits>...]", the map in hex.
std::optional<VitalsUpdate> ParseVitalsUpdate(const QByteArray& body);
// One line per member as posted, with its revive hits added at the end, then one
// "wipe <id> <map> <member>..." line per wipe.
QByteArray FormatVitalsBoard(const VitalsBoardAnswer& answer);

class PartyVitalsBoard {
public:
    // Members post a few times a second while someone is down, else every second.
    static constexpr qint64 FreshMs = 5'000;
    // A wipe stays on the board this long, so every member it takes reads it.
    static constexpr qint64 WipeMs = 20'000;

    // Records the member's vitals, declares a wipe for its map when nobody there stands any
    // more, and returns its party's members (sorted) and the wipes of the last WipeMs.
    VitalsBoardAnswer Update(const QByteArray& party, const VitalsUpdate& update, qint64 nowMs);

private:
    struct Entry {
        VitalsUpdate update;
        qint64 postedAtMs = 0;
    };
    struct Wipe {
        PartyWipe wipe;
        qint64 declaredAtMs = 0;
    };
    struct Party {
        QHash<QByteArray, Entry> members;
        QList<Wipe> wipes;
        quint32 lastWipe = 0;
    };

    QMutex m_mutex;
    QHash<QByteArray, Party> m_parties;
};

} // namespace Bloodborne
