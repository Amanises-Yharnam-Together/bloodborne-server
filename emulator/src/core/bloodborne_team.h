// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 5: the team lobby on the game's side (docs/SERVER_COOP_PLAN.md, "The team lobby").
// With SHADPS4_BLOODBORNE_TEAMS=1 the party is not a setting: the game signs in to the server's
// lobby, and a team's Start names the party (its key and who hosts). The load of a save is held
// on the title until then (TeamGate): a member's Continue or Load Game is its Ready, the leader's
// is the team's Start.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/types.h"
#include "coop.pb.h"

namespace Core::Bloodborne {

// SHADPS4_BLOODBORNE_TEAMS: "1" or "on" turns the team lobby on.
bool TeamModeEnabled(const char* setting);

class TeamGate {
public:
    enum class Step { Load, Hold };
    struct Decision {
        Step step = Step::Load;
        std::vector<bbcoop::TeamRequest> requests; // to send now
        std::optional<bbcoop::TeamStart> start;    // with Load: the team the save plays in
    };
    // A load held this long with no word from the lobby (no server) goes ahead alone.
    static constexpr s64 NoLobbyLoadsAfterMs = 5000;

    void TakeLobby(const bbcoop::LobbyState& state) {
        lobby = state;
    }
    void TakeStart(const bbcoop::TeamStart& start) {
        started = start;
    }
    // Every frame the title wants to load the save in `slot` (its character `character`).
    Decision Tick(const std::string& me, u32 slot, const std::string& character, s64 now_ms);
    // Back at the title: the next load asks the team again.
    void Reset();

    const std::optional<bbcoop::LobbyState>& Lobby() const {
        return lobby;
    }
    bool Holding() const {
        return holding_since.has_value();
    }

private:
    std::optional<bbcoop::LobbyState> lobby;
    std::optional<bbcoop::TeamStart> started;
    std::optional<s64> holding_since;
    bool asked = false;
};

// Phase 5b: a team's world (bbcoop::TeamWorld). The world's part of the event flags (FlagOwner
// Host and Shared, docs/FLAGS.md; a character's own flags are never in it): the ones on, in the
// flag blocks the store had. The team's host reports its own; every member's save takes the team's
// as it loads, its own kept aside (a file per save slot) until it plays alone again.
struct WorldFlags {
    std::vector<s32> on;     // sorted
    std::vector<u32> blocks; // sorted
    bool operator==(const WorldFlags&) const = default;
};
bool IsWorldFlag(s32 flag);
// One block of the event flag store: flag = id * flags_per_block + byte * 8 + bit (MSB first).
struct FlagBlockBits {
    u32 id = 0;
    std::span<const u8> bytes;
};
WorldFlags CaptureWorldFlags(u32 flags_per_block, std::span<const FlagBlockBits> blocks);
// What to set so the store's world flags are `target`'s, in the blocks both have (a block the
// target does not cover keeps its flags): each flag and its value, in order.
std::vector<std::pair<s32, bool>> WorldFlagChanges(u32 flags_per_block,
                                                   std::span<const FlagBlockBits> blocks,
                                                   const WorldFlags& target);
// The personal world kept aside, as its file has it ("blocks" then "on", a number a line).
std::string FormatWorldFlags(const WorldFlags& world);
std::optional<WorldFlags> ParseWorldFlags(std::string_view text);
WorldFlags WorldFlagsOf(const bbcoop::TeamWorld& world);

} // namespace Core::Bloodborne
