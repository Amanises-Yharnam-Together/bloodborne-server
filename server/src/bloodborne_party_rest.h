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
};

// "<member> <lamp>", member 1-64 letters or digits; lamp 0 means not resting.
std::optional<LampRest> ParseRestUpdate(const QByteArray& body);
// One "<member> <lamp>\n" line per rest.
QByteArray FormatRestBoard(const QList<LampRest>& rests);

class PartyRestBoard {
public:
    // Members post about once a second while they rest; older posts are from members that left.
    static constexpr qint64 FreshMs = 5'000;

    // Records the member's rest and returns its party's current rests, sorted by member.
    QList<LampRest> Update(const QByteArray& party, const QByteArray& member, qint32 lamp,
                           qint64 nowMs);

private:
    struct Entry {
        qint32 lamp = 0;
        qint64 updatedAtMs = 0;
    };

    QMutex m_mutex;
    QHash<QByteArray, QHash<QByteArray, Entry>> m_parties;
};

} // namespace Bloodborne
