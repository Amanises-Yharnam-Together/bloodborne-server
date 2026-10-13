// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/bloodborne_party.h"

namespace Core::Bloodborne {

// Replays a co-op trace (see CoopTraceVersion) through the pure sync classes: the recorded inputs
// of the kill board (VisitKills), enemy HP (SharedDamage, LowerToHostHp), the shown board
// (ShownLikeTheHost) and member kills (AppliesMemberKill), the kill frames and kill board
// answers the game took, and the world log's inputs (WorldLogHost, WorldLogMember) and path.
// Each record gives the decisions it led to, one line each. Game memory is not replayed: what the
// game did with a decision is not in the trace.
class CoopTraceReplay {
public:
    std::vector<std::string> Feed(std::string_view line);
    // Every line's decisions, in order; with `stamped`, each one after its record's UTC ms.
    std::vector<std::string> FeedAll(std::string_view text, bool stamped = false);

private:
    // An "-in" kind's values and lists as its records left them.
    struct Inputs {
        std::map<std::string, std::string> values;
        std::map<std::string, std::vector<std::string>> lists;
        bool Take(const TraceRecord& record, const std::set<std::string>& list_keys);
    };
    void Kills(const TraceRecord& record, std::vector<std::string>& out);
    void Hp(const TraceRecord& record, std::vector<std::string>& out);
    void Shown(const TraceRecord& record, std::vector<std::string>& out);
    void KillApply(const TraceRecord& record, std::vector<std::string>& out);
    void FrameIn(const TraceRecord& record, std::vector<std::string>& out);
    void BoardIn(const TraceRecord& record, std::vector<std::string>& out);
    void WorldLogIn(const TraceRecord& record, std::vector<std::string>& out);
    void WorldLogHostIn(std::string_view op, std::span<const u8> payload, s64 at,
                        std::vector<std::string>& out);
    void WorldLogMemberIn(std::string_view op, std::span<const u8> payload, s64 at,
                          std::vector<std::string>& out);

    Inputs kills_in;
    Inputs hp_in;
    Inputs shown_in;
    VisitKills visit_kills;
    std::set<u32> visit_maps;
    std::map<u32, std::vector<u32>> held;
    SharedDamage damage;
    PartyRole hp_role = PartyRole::Host; // the last one-world mode's
    std::vector<EnemyHp> posted;
    PartyKills party_kills;
    std::set<u32> board_kills;
    std::map<std::string, std::vector<u32>> peer_kills;
    WorldLogHost wlog_host;
    WorldLogMember wlog_member;
};

} // namespace Core::Bloodborne
