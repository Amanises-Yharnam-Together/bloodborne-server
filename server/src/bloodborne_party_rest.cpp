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

QByteArray FormatKillBoard(const FlagBoardAnswer& answer) {
    QByteArray body = QByteArray::number(answer.latest) + '\n';
    for (qsizetype index = 0; index < answer.changes.size(); ++index) {
        const FlagChange& change = answer.changes[index];
        body += QByteArray::number(change.flag) + (change.on ? " 1 " : " 0 ") +
                (index < answer.members.size() ? answer.members[index] : QByteArray{}) + '\n';
    }
    return body;
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
            answer.members.append(entry.member);
        }
    }
    return answer;
}

void PartyFlagBoard::WithdrawMember(const QByteArray& party, const QByteArray& member) {
    if (party.isEmpty()) {
        return;
    }
    QMutexLocker lock(&m_mutex);
    Party& board = m_parties[party];
    QList<qint32> order;
    QHash<qint32, bool> standing;
    for (const Entry& entry : board.entries) {
        if (entry.member != member) {
            continue;
        }
        if (!standing.contains(entry.change.flag)) {
            order.append(entry.change.flag);
        }
        standing[entry.change.flag] = entry.change.on;
    }
    for (const qint32 flag : order) {
        if (standing.value(flag)) {
            board.entries.append(Entry{++board.latest, member, FlagChange{flag, false}});
        }
    }
    while (board.entries.size() > MaxChanges) {
        board.entries.removeFirst();
    }
}

std::optional<EchoUpdate> ParseEchoUpdate(const QByteArray& body) {
    const QList<QByteArray> parts = body.trimmed().split(' ');
    if (parts.size() < 2 || !IsMemberName(parts[0])) {
        return std::nullopt;
    }
    bool ok = false;
    EchoUpdate update{parts[0], parts[1].toLongLong(&ok), {}};
    if (!ok || update.since < 0) {
        return std::nullopt;
    }
    for (qsizetype index = 2; index < parts.size(); ++index) {
        const QList<QByteArray> kill = parts[index].split(':');
        if (kill.size() > 2) {
            return std::nullopt;
        }
        const qint32 echoes = kill[0].toInt(&ok);
        if (!ok || echoes <= 0) {
            return std::nullopt;
        }
        quint32 map = 0;
        if (kill.size() == 2) {
            map = kill[1].toUInt(&ok);
            if (!ok) {
                return std::nullopt;
            }
        }
        update.echoes.append(echoes);
        update.maps.append(map);
    }
    return update;
}

QByteArray FormatEchoBoard(const EchoBoardAnswer& answer) {
    QByteArray body = QByteArray::number(answer.latest) + '\n';
    for (qsizetype index = 0; index < answer.echoes.size(); ++index) {
        const quint32 map = index < answer.maps.size() ? answer.maps[index] : 0;
        body += QByteArray::number(answer.echoes[index]);
        if (map != 0) {
            body += ' ' + QByteArray::number(map);
        }
        body += '\n';
    }
    return body;
}

EchoBoardAnswer PartyEchoBoard::Update(const QByteArray& party, const QByteArray& member,
                                       qint64 since, const QList<qint32>& echoes,
                                       const QList<quint32>& maps) {
    if (party.isEmpty()) {
        return {};
    }
    QMutexLocker lock(&m_mutex);
    Party& board = m_parties[party];
    for (qsizetype index = 0; index < echoes.size(); ++index) {
        board.entries.append(
            Entry{++board.latest, member, echoes[index], index < maps.size() ? maps[index] : 0});
    }
    while (board.entries.size() > MaxEntries) {
        board.entries.removeFirst();
    }
    EchoBoardAnswer answer{board.latest, {}};
    for (const Entry& entry : board.entries) {
        if (entry.sequence > since && entry.member != member) {
            answer.echoes.append(entry.echoes);
            answer.maps.append(entry.map);
        }
    }
    return answer;
}

std::optional<MapStateUpdate> ParseMapStateUpdate(const QByteArray& body) {
    const QList<QByteArray> parts = body.trimmed().split(' ');
    if (parts.size() < 2 || parts.size() > 3 || !IsMemberName(parts[0])) {
        return std::nullopt;
    }
    bool ok = false;
    MapStateUpdate update{parts[0], parts[1].toUInt(&ok), {}};
    if (!ok) {
        return std::nullopt;
    }
    if (parts.size() == 3) {
        const QByteArray& bits = parts[2];
        if (bits.size() % 2 != 0 || !std::all_of(bits.begin(), bits.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            })) {
            return std::nullopt;
        }
        update.bits = bits;
    }
    return update;
}

QByteArray FormatMapStateBoard(const QList<MapState>& states) {
    QByteArray body;
    for (const MapState& state : states) {
        body += QByteArray::number(state.map) + ' ' + state.bits + '\n';
    }
    return body;
}

QList<MapState> PartyMapStateBoard::Update(const QByteArray& party, const QByteArray& member,
                                           quint32 map, const QByteArray& bits, qint64 nowMs) {
    if (party.isEmpty()) {
        return {};
    }
    QMutexLocker lock(&m_mutex);
    auto& members = m_parties[party];
    if (map != 0 && !bits.isEmpty()) {
        members[member] = Entry{MapState{map, bits}, nowMs};
    } else if (const auto own = members.find(member); own != members.end()) {
        own->postedAtMs = nowMs; // still there, only reading
    }
    QList<MapState> others;
    for (auto it = members.begin(); it != members.end();) {
        if (m_freshMs > 0 && nowMs - it->postedAtMs > m_freshMs) {
            it = members.erase(it);
            continue;
        }
        if (it.key() != member) {
            others.append(it->state);
        }
        ++it;
    }
    return others;
}

namespace {

bool IsVital(const QByteArray& vital) {
    return vital == "off" || vital == "up" || vital == "down" || vital == "dead";
}

std::optional<ReviveHits> ParseReviveHits(const QByteArray& text) {
    const QList<QByteArray> parts = text.split(':');
    if (parts.size() != 3 || !IsMemberName(parts[0])) {
        return std::nullopt;
    }
    bool downOk = false;
    bool hitsOk = false;
    const quint32 down = parts[1].toUInt(&downOk);
    const quint32 hits = parts[2].toUInt(&hitsOk);
    if (!downOk || !hitsOk) {
        return std::nullopt;
    }
    return ReviveHits{parts[0], down, hits};
}

} // namespace

std::optional<VitalsUpdate> ParseVitalsUpdate(const QByteArray& body) {
    // A party has three members; a few more revive targets than that is no client of ours.
    constexpr qsizetype MaxReviveTargets = 8;
    const QList<QByteArray> parts = body.trimmed().split(' ');
    if (parts.size() < 11 || parts.size() > 11 + MaxReviveTargets || !IsMemberName(parts[0]) ||
        !IsVital(parts[2])) {
        return std::nullopt;
    }
    VitalsUpdate update;
    MemberVitals& self = update.self;
    self.member = parts[0];
    self.vital = parts[2];
    bool ok[9]{};
    self.map = parts[1].toUInt(&ok[0], 16);
    self.hp = parts[3].toInt(&ok[1]);
    self.max = parts[4].toInt(&ok[2]);
    self.down = parts[5].toUInt(&ok[3]);
    self.landed = parts[6].toUInt(&ok[4]);
    self.needed = parts[7].toUInt(&ok[5]);
    parts[8].toFloat(&ok[6]);
    parts[9].toFloat(&ok[7]);
    parts[10].toFloat(&ok[8]);
    if (!std::all_of(std::begin(ok), std::end(ok), [](bool value) { return value; })) {
        return std::nullopt;
    }
    self.at = parts[8] + ' ' + parts[9] + ' ' + parts[10];
    for (qsizetype index = 11; index < parts.size(); ++index) {
        const auto hits = ParseReviveHits(parts[index]);
        if (!hits) {
            return std::nullopt;
        }
        update.hits.append(*hits);
    }
    return update;
}

QByteArray FormatVitalsBoard(const VitalsBoardAnswer& answer) {
    QByteArray body;
    for (const MemberVitals& member : answer.members) {
        body += member.member + ' ' + QByteArray::number(member.map, 16) + ' ' + member.vital +
                ' ' + QByteArray::number(member.hp) + ' ' + QByteArray::number(member.max) + ' ' +
                QByteArray::number(member.down) + ' ' + QByteArray::number(member.landed) + ' ' +
                QByteArray::number(member.needed) + ' ' + member.at + ' ' +
                QByteArray::number(member.reviveHits) + '\n';
    }
    for (const PartyWipe& wipe : answer.wipes) {
        body += "wipe " + QByteArray::number(wipe.id) + ' ' + QByteArray::number(wipe.map, 16);
        for (const QByteArray& member : wipe.members) {
            body += ' ' + member;
        }
        body += '\n';
    }
    return body;
}

VitalsBoardAnswer PartyVitalsBoard::Update(const QByteArray& party, const VitalsUpdate& update,
                                           qint64 nowMs) {
    if (party.isEmpty()) {
        return {};
    }
    QMutexLocker lock(&m_mutex);
    Party& state = m_parties[party];
    state.members.insert(update.self.member, Entry{update, nowMs});
    for (auto it = state.members.begin(); it != state.members.end();) {
        it = nowMs - it->postedAtMs > FreshMs ? state.members.erase(it) : std::next(it);
    }
    state.wipes.removeIf([&](const Wipe& wipe) { return nowMs - wipe.declaredAtMs > WipeMs; });

    // The poster's map: wiped when nobody there stands and someone there is down that no
    // wipe of the last WipeMs took yet.
    const quint32 map = update.self.map;
    bool standing = false;
    QList<QByteArray> down;
    for (auto it = state.members.cbegin(); it != state.members.cend() && map != 0; ++it) {
        const MemberVitals& member = it->update.self;
        if (member.map != map) {
            continue;
        }
        standing = standing || member.vital == "up";
        const bool wiped =
            std::any_of(state.wipes.cbegin(), state.wipes.cend(), [&](const Wipe& wipe) {
                return wipe.wipe.members.contains(member.member);
            });
        if (member.vital == "down" && !wiped) {
            down.append(member.member);
        }
    }
    if (!standing && !down.isEmpty()) {
        std::sort(down.begin(), down.end());
        state.wipes.append(Wipe{PartyWipe{++state.lastWipe, map, down}, nowMs});
    }

    VitalsBoardAnswer answer;
    for (auto it = state.members.cbegin(); it != state.members.cend(); ++it) {
        MemberVitals member = it->update.self;
        member.reviveHits = 0;
        for (auto other = state.members.cbegin(); other != state.members.cend(); ++other) {
            for (const ReviveHits& hits : other->update.hits) {
                if (hits.target == member.member && hits.down == member.down) {
                    member.reviveHits += hits.hits;
                }
            }
        }
        answer.members.append(member);
    }
    std::sort(answer.members.begin(), answer.members.end(),
              [](const MemberVitals& a, const MemberVitals& b) { return a.member < b.member; });
    for (const Wipe& wipe : state.wipes) {
        answer.wipes.append(wipe.wipe);
    }
    return answer;
}

} // namespace Bloodborne
