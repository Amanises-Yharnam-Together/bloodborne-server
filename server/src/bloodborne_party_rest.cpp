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

} // namespace Bloodborne
