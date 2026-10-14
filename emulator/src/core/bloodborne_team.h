// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Phase 5: the team lobby on the game's side (docs/SERVER_COOP_PLAN.md, "The team lobby").
// In the port (always, phase 6) the party is not a setting: the game signs in to the server's
// lobby, and a team's Start names the party (its key and who hosts). The load of a save is held
// on the title until then (TeamGate): a member's Continue or Load Game is its Ready, the leader's
// is the team's Start.
#pragma once

#include <array>
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

// Kaniel 2026-10-14: the bell items are out of the port's game (the team joins without them):
// Beckoning Bell 200, Small Resonant Bell 205, Sinister Resonant Bell 225, Old Hunter Bell 4312
// (EquipParamGoods ids, names from item.msgbnd). The Choir Bell (2050) is a hunter tool and stays.
inline constexpr std::array<u32, 4> BellGoods{200, 205, 225, 4312};
constexpr bool IsBellGoods(u32 goods) {
    for (const u32 bell : BellGoods) {
        if (bell == goods) {
            return true;
        }
    }
    return false;
}

// Event flag 4047 ("a player on the way out", docs/FLAGS.md): set when a session ends under a
// player; a summoned guest's reload home cleared it. A team's guest stays in its world (no reload),
// so once it stands out of a session with the flag on for ClearAfterMs, it is cleared: the party
// controller waits while it is on, and the guest would never join its host again.
class LeavingFlag {
public:
    static constexpr s32 Flag = 4047;
    static constexpr s64 ClearAfterMs = 5'000;
    bool Clear(bool flag_on, bool in_session, s64 now_ms) {
        if (!flag_on || in_session) {
            since_ms.reset();
            return false;
        }
        if (!since_ms) {
            since_ms = now_ms;
        }
        if (now_ms - *since_ms < ClearAfterMs) {
            return false;
        }
        since_ms.reset();
        return true;
    }

private:
    std::optional<s64> since_ms;
};

// A team's guest stays in its world (no reload home), so a white phantom whose session is over is
// made a player again, once it has stood alone (no session, matching idle) for ReturnAfterMs: a
// join makes it a phantom first, while the session is not yet the game's (rig 2026-10-14: a guest
// made a player too early could not be revived).
class PhantomHome {
public:
    static constexpr s64 ReturnAfterMs = 5'000;
    bool Due(bool phantom, bool alone, s64 now_ms) {
        if (!phantom || !alone) {
            since_ms.reset();
            return false;
        }
        if (!since_ms) {
            since_ms = now_ms;
        }
        if (now_ms - *since_ms < ReturnAfterMs) {
            return false;
        }
        since_ms.reset();
        return true;
    }

private:
    std::optional<s64> since_ms;
};

// Kaniel 2026-10-14: the team loading screen, as in League of Legends. From the team's Start every
// member's game shows each hunter with a bar (bbcoop::LoadProgress through the server) and holds
// until all of them are in the team's session (or LimitMs), so the hunt opens for everyone at once.
class TeamLoad {
public:
    using Stage = bbcoop::LoadProgress::Stage;
    static constexpr s64 LimitMs = 90'000;
    // A save's load fills its bar to LoadedPercent over about this long (the game says nothing of
    // its own progress); joining and ready take it on to 100.
    static constexpr s64 LoadingGuessMs = 20'000;
    static constexpr u32 LoadedPercent = 70;
    struct Row {
        std::string member;
        std::string name;
        Stage stage = bbcoop::LoadProgress::WAITING;
        u32 percent = 0;
        bool self = false;
    };
    struct View {
        bool up = false;
        std::vector<Row> rows;
    };

    void Start(s64 now_ms);
    void SetStage(Stage stage, s64 now_ms);
    void Take(const bbcoop::LoadProgress& other, s64 now_ms);
    // `members`: the team's (member, name), the leader first.
    View Tick(std::span<const std::pair<std::string, std::string>> members, const std::string& me,
              s64 now_ms);
    // This game's progress to send, when it changed.
    std::optional<bbcoop::LoadProgress> ToSend(s64 now_ms);

private:
    struct Heard {
        bbcoop::LoadProgress load;
        s64 at_ms = 0;
    };
    u32 OwnPercent(s64 now_ms) const;
    std::optional<s64> started_ms;
    Stage own = bbcoop::LoadProgress::WAITING;
    s64 own_since_ms = 0;
    bool open = false;
    std::optional<std::pair<Stage, u32>> sent;
    std::vector<std::pair<std::string, Heard>> others;
};

} // namespace Core::Bloodborne
