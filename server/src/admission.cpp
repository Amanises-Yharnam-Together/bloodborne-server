// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "admission.h"

#include <algorithm>
#include <QHostAddress>
#include <QStringList>

namespace Admission {

bool Guard::Bucket::Take(double amount, double rate, double burst, qint64 now_ms) {
    if (tokens < 0) {
        tokens = burst;
    } else {
        tokens = std::min(burst, tokens + (now_ms - at_ms) * rate / 1000.0);
    }
    at_ms = now_ms;
    if (tokens < amount) {
        return false;
    }
    tokens -= amount;
    return true;
}

bool Guard::AllowHello(quint32 address, qint64 now_ms) {
    if (hellos.size() > 100'000) {
        hellos.clear(); // a spoofed flood of addresses: start over rather than grow
    }
    if (!hellos[address].Take(1.0, limits.hellos_per_second, limits.hello_burst, now_ms)) {
        ++counters.hellos_refused;
        return false;
    }
    return true;
}

bool Guard::MayMakeAccount(quint32 address, qint64 now_ms) {
    QList<qint64>& made = accounts[address];
    made.erase(std::remove_if(made.begin(), made.end(),
                              [&](qint64 at) { return now_ms - at > 3'600'000; }),
               made.end());
    if (made.size() >= limits.new_accounts_per_hour) {
        ++counters.accounts_refused;
        return false;
    }
    return true;
}

void Guard::Made(quint32 address, qint64 now_ms) {
    accounts[address].append(now_ms);
}

void Guard::Unmade(quint32 address) {
    const auto found = accounts.find(address);
    if (found != accounts.end() && !found->isEmpty()) {
        found->removeLast();
    }
}

bool Guard::AllowDatagram(quint64 link, quint32 address, int bytes, qint64 now_ms) {
    LinkState& state = links[link];
    if (state.datagrams.Take(1.0, limits.datagrams_per_second, limits.datagram_burst, now_ms) &&
        state.bytes.Take(bytes, limits.bytes_per_second, limits.byte_burst, now_ms)) {
        return true;
    }
    ++counters.datagrams_dropped;
    // One strike a second at most: a link past its rate for strikes_to_ban seconds within the
    // window is banned.
    if (now_ms - state.last_strike_ms >= 1000) {
        state.last_strike_ms = now_ms;
        if (now_ms - state.strikes_from_ms > limits.strike_window_ms) {
            state.strikes_from_ms = now_ms;
            state.strikes = 0;
        }
        if (++state.strikes >= limits.strikes_to_ban) {
            Ban(address, now_ms + limits.ban_ms);
            ++counters.bans;
            state.strikes = 0;
        }
    }
    return false;
}

bool Guard::Banned(quint32 address, qint64 now_ms) const {
    if (banned_addresses.contains(address)) {
        return true;
    }
    const auto ban = bans.constFind(address);
    return ban != bans.constEnd() && *ban > now_ms;
}

void Guard::SetBanned(const QSet<quint32>& addresses, const QSet<QByteArray>& ids) {
    banned_addresses = addresses;
    banned_ids = ids;
}

void ParseBanList(const QString& text, QSet<quint32>& addresses, QSet<QByteArray>& ids) {
    for (QString line : text.split('\n')) {
        line = line.section('#', 0, 0).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        bool ipv4 = false;
        const quint32 address = QHostAddress(line).toIPv4Address(&ipv4);
        if (ipv4) {
            addresses.insert(address);
        } else {
            ids.insert(line.toUtf8());
        }
    }
}

} // namespace Admission
