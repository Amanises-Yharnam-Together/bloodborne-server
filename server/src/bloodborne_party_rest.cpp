// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bloodborne_party_rest.h"

#include <algorithm>

#include <QMutexLocker>

namespace Bloodborne {

namespace {

constexpr qsizetype MaxMemberSize = 64;

bool IsMemberName(const QByteArray& member) {
    return !member.isEmpty() && member.size() <= MaxMemberSize &&
           std::all_of(member.begin(), member.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
           });
}

} // namespace

std::optional<LampRest> ParseRestUpdate(const QByteArray& body) {
    const QList<QByteArray> parts = body.trimmed().split(' ');
    if ((parts.size() != 2 && parts.size() != 5) || !IsMemberName(parts[0])) {
        return std::nullopt;
    }
    bool ok = false;
    const qint32 lamp = parts[1].toInt(&ok);
    if (!ok) {
        return std::nullopt;
    }
    LampRest rest{parts[0], lamp, {}};
    if (parts.size() == 5) {
        for (qsizetype axis = 2; axis < 5; ++axis) {
            parts[axis].toFloat(&ok);
            if (!ok) {
                return std::nullopt;
            }
        }
        rest.at = parts[2] + ' ' + parts[3] + ' ' + parts[4];
    }
    return rest;
}

QByteArray FormatRestBoard(const QList<LampRest>& rests) {
    QByteArray body;
    for (const LampRest& rest : rests) {
        body += rest.member + ' ' + QByteArray::number(rest.lamp);
        if (!rest.at.isEmpty()) {
            body += ' ' + rest.at;
        }
        body += '\n';
    }
    return body;
}

QList<LampRest> PartyRestBoard::Update(const QByteArray& party, const QByteArray& member,
                                       qint32 lamp, qint64 nowMs, const QByteArray& at) {
    if (party.isEmpty()) {
        return {};
    }
    QMutexLocker lock(&m_mutex);
    auto& members = m_parties[party];
    if (lamp == 0) {
        members.remove(member);
    } else {
        members.insert(member, Entry{lamp, at, nowMs});
    }
    QList<LampRest> rests;
    for (auto it = members.begin(); it != members.end();) {
        if (nowMs - it->updatedAtMs > FreshMs) {
            it = members.erase(it);
            continue;
        }
        rests.append(LampRest{it.key(), it->lamp, it->at});
        ++it;
    }
    if (members.isEmpty()) {
        m_parties.remove(party);
    }
    std::sort(rests.begin(), rests.end(),
              [](const LampRest& a, const LampRest& b) { return a.member < b.member; });
    return rests;
}

std::optional<MemberMap> ParseMapUpdate(const QByteArray& body) {
    const QList<QByteArray> parts = body.trimmed().split(' ');
    if (parts.size() != 2 || !IsMemberName(parts[0])) {
        return std::nullopt;
    }
    bool ok = false;
    const quint32 map = parts[1].toUInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    return MemberMap{parts[0], map};
}

QByteArray FormatMapBoard(const QList<MemberMap>& maps) {
    QByteArray body;
    for (const MemberMap& entry : maps) {
        body += entry.member + ' ' + QByteArray::number(entry.map, 16) + '\n';
    }
    return body;
}

QList<MemberMap> PartyMapBoard::Update(const QByteArray& party, const QByteArray& member,
                                       quint32 map, qint64 nowMs) {
    if (party.isEmpty()) {
        return {};
    }
    QMutexLocker lock(&m_mutex);
    auto& members = m_parties[party];
    members.insert(member, Entry{map, nowMs});
    QList<MemberMap> maps;
    for (auto it = members.begin(); it != members.end();) {
        if (nowMs - it->updatedAtMs > FreshMs) {
            it = members.erase(it);
            continue;
        }
        maps.append(MemberMap{it.key(), it->map});
        ++it;
    }
    std::sort(maps.begin(), maps.end(),
              [](const MemberMap& a, const MemberMap& b) { return a.member < b.member; });
    return maps;
}

std::optional<TrophyUpdate> ParseTrophyUpdate(const QByteArray& body) {
    const QList<QByteArray> parts = body.trimmed().split(' ');
    if (parts.isEmpty() || !IsMemberName(parts[0])) {
        return std::nullopt;
    }
    TrophyUpdate update{parts[0], {}};
    for (qsizetype index = 1; index < parts.size(); ++index) {
        bool ok = false;
        const qint32 trophy = parts[index].toInt(&ok);
        if (!ok || trophy < 0 || trophy > PartyTrophyBoard::MaxTrophyId) {
            return std::nullopt;
        }
        update.trophies.append(trophy);
    }
    return update;
}

QByteArray FormatTrophyBoard(const QList<qint32>& trophies) {
    QByteArray body;
    for (const qint32 trophy : trophies) {
        body += QByteArray::number(trophy) + '\n';
    }
    return body;
}

QList<qint32> PartyTrophyBoard::Update(const QByteArray& party, const QByteArray& member,
                                       const QList<qint32>& trophies) {
    Q_UNUSED(member);
    if (party.isEmpty()) {
        return {};
    }
    QMutexLocker lock(&m_mutex);
    QList<qint32>& unlocked = m_parties[party];
    for (const qint32 trophy : trophies) {
        if (!unlocked.contains(trophy)) {
            unlocked.append(trophy);
        }
    }
    std::sort(unlocked.begin(), unlocked.end());
    return unlocked;
}

std::optional<FlagUpdate> ParseFlagUpdate(const QByteArray& body) {
    const QList<QByteArray> parts = body.trimmed().split(' ');
    if (parts.size() < 2 || !IsMemberName(parts[0])) {
        return std::nullopt;
    }
    bool ok = false;
    FlagUpdate update{parts[0], parts[1].toLongLong(&ok), {}};
    if (!ok || update.since < 0) {
        return std::nullopt;
    }
    for (qsizetype index = 2; index < parts.size(); ++index) {
        const QList<QByteArray> pair = parts[index].split(':');
        if (pair.size() != 2 || (pair[1] != "0" && pair[1] != "1")) {
            return std::nullopt;
        }
        const qint32 flag = pair[0].toInt(&ok);
        if (!ok || flag <= 0) {
            return std::nullopt;
        }
        update.changes.append(FlagChange{flag, pair[1] == "1"});
    }
    return update;
}

QByteArray FormatFlagBoard(const FlagBoardAnswer& answer) {
    QByteArray body = QByteArray::number(answer.latest) + '\n';
    for (const FlagChange& change : answer.changes) {
        body += QByteArray::number(change.flag) + (change.on ? " 1\n" : " 0\n");
    }
    return body;
}

FlagBoardAnswer PartyFlagBoard::Update(const QByteArray& party, const QByteArray& member,
                                       qint64 since, const QList<FlagChange>& changes) {
    if (party.isEmpty()) {
        return {};
    }
    QMutexLocker lock(&m_mutex);
    Party& board = m_parties[party];
    for (const FlagChange& change : changes) {
        board.entries.append(Entry{++board.latest, member, change});
    }
    while (board.entries.size() > MaxChanges) {
        board.entries.removeFirst();
    }
    FlagBoardAnswer answer{board.latest, {}};
    for (const Entry& entry : board.entries) {
        if (entry.sequence > since && entry.member != member) {
            answer.changes.append(entry.change);
        }
    }
    return answer;
}

} // namespace Bloodborne
