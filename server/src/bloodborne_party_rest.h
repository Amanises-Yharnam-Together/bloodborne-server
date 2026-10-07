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
};

// "<member> <since> [<flag>:<0|1>...]".
std::optional<FlagUpdate> ParseFlagUpdate(const QByteArray& body);
// A "<latest>" line, then one "<flag> <0|1>" line per change.
QByteArray FormatFlagBoard(const FlagBoardAnswer& answer);

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

} // namespace Bloodborne
