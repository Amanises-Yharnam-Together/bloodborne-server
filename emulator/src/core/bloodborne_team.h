// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 5: the team lobby on the game's side (docs/SERVER_COOP_PLAN.md, "The team lobby").
// In the port (always, phase 6) the party is not a setting: the game signs in to the server's
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

class TeamGate {
public:
    enum class Step { Load, Hold };
    // The team screen's buttons (bbport_overlay.cpp): this character ready or not, the leader's
    // Start (a member's is its Ready), or this save plays alone.
    enum class Choice { Ready, Unready, Start, Alone };
    struct Decision {
        Step step = Step::Load;
        std::optional<bbcoop::TeamStart> start; // with Load: the team the save plays in
    };

    void TakeLobby(const bbcoop::LobbyState& state) {
        lobby = state;
    }
    void TakeStart(const bbcoop::TeamStart& start) {
        started = start;
    }
    // Every frame the title wants to load the save in `slot`: held until the team starts or the
    // player plays alone.
    Decision Tick(const std::string& me, u32 slot, s64 now_ms);
    // A button of the screen: the requests to send.
    std::vector<bbcoop::TeamRequest> Choose(Choice choice, const std::string& me);
    // Back at the title: the next load asks again.
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
    u32 slot = 0;
    bool alone = false;
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

// Phase 6: the team join without signs or a bell (docs/native-netcode/session.md, "Team join").
// The team's host opens its game session itself (NetSync CreateSession) once it stands in its
// world, and sends the session's join data (the game's own: what a summon sends its guest) to the
// server (bbcoop::TeamSession); every guest's game joins that session (NetSync JoinSession) when it
// stands alone in its world. TeamJoin says each frame what this game does.
class TeamJoin {
public:
    enum class Action { None, CreateSession, Publish, PublishClosed, Join };
    struct Frame {
        bool host = false;
        s32 matching_state = 0; // the session manager's +0x124: 0 alone, 3 hosting, 6 a guest
        bool ready = false;     // the party controller's go (in the world, settled, retry spaced)
        bool session_known = false; // a guest: the server sent its host's open session
    };
    Action Tick(const Frame& frame);

private:
    bool published = false; // the host said its open session
};

} // namespace Core::Bloodborne
