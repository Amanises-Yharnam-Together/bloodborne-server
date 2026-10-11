// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <optional>
#include <vector>

#include "common/types.h"

namespace Libraries::Net {

// A peer of the P2P transport (its address and port) going quiet or talking again.
struct SilenceChange {
    u64 peer{};
    bool silent{};
    s64 gap_ms{};  // how long nothing came from it
    u64 packets{}; // what came from it before
};

// Which peers stopped sending (Kaniel's friend 2026-10-11: the other players froze on her screen
// before her game left the session, and nothing said whether their packets still arrived). Each
// change is reported once; a peer quiet for a minute is forgotten (it left).
class PeerSilence {
public:
    explicit PeerSilence(s64 quiet_ms) : quiet_ms{quiet_ms} {}

    // A packet from `peer`: the peer talking again after it was reported quiet, or nothing.
    std::optional<SilenceChange> Heard(u64 peer, s64 now_ms) {
        Peer& seen = peers[peer];
        std::optional<SilenceChange> change;
        if (seen.reported) {
            change = SilenceChange{peer, false, now_ms - seen.last_ms, seen.packets};
            seen.reported = false;
        }
        seen.last_ms = now_ms;
        ++seen.packets;
        return change;
    }

    // Peers newly quiet for longer than quiet_ms.
    std::vector<SilenceChange> Check(s64 now_ms) {
        std::vector<SilenceChange> changes;
        for (auto it = peers.begin(); it != peers.end();) {
            Peer& seen = it->second;
            const s64 gap = now_ms - seen.last_ms;
            if (gap > ForgetMs) {
                it = peers.erase(it);
                continue;
            }
            if (!seen.reported && gap > quiet_ms) {
                seen.reported = true;
                changes.push_back({it->first, true, gap, seen.packets});
            }
            ++it;
        }
        return changes;
    }

private:
    static constexpr s64 ForgetMs = 60000;
    struct Peer {
        s64 last_ms{};
        u64 packets{};
        bool reported{};
    };
    s64 quiet_ms;
    std::map<u64, Peer> peers;
};

} // namespace Libraries::Net
