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

// Kaniel 2026-10-14: whether the team's leader (`me`) may begin the hunt: every other member online
// and ready, or the team started already.
bool TeamCanBegin(const bbcoop::LobbyState& lobby, const std::string& me);

// Kaniel 2026-10-15: the player slot a guest joins the host's session with (GameMan +0x16F4), its
// own in the team: 7 for the first guest (the summon sign's), 6 for the second, 5 for the third.
// Phase 7: a member with no slot of its own (the leader) joining another member's session (that
// member hosts it now, host migration) takes the slot the session's host left.
u32 TeamGuestSign(const bbcoop::LobbyState& lobby, const std::string& me,
                  const std::string& session_host = {});

// Phase 7: what the team screen says when the server refused this game's link (another build),
// from the server's reason; empty when it did not.
std::string LinkRefusalNotice(std::string_view refusal);

// The members the team loading screen shows (member, name), the leader first: this game and those
// coming (ready, not offline); not one gone or staying at the title (Kaniel 2026-10-15).
std::vector<std::pair<std::string, std::string>> TeamLoadMembers(const bbcoop::LobbyState& lobby,
                                                                 const std::string& me);

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
    // The team's Start as it came (a save playing alone takes it in game: TeamPlayStepOf), and
    // forgotten once the save is out of that team.
    const std::optional<bbcoop::TeamStart>& Start() const {
        return started;
    }
    void DropStart() {
        started.reset();
    }

private:
    std::optional<bbcoop::LobbyState> lobby;
    std::optional<bbcoop::TeamStart> started;
    std::optional<s64> holding_since;
    u32 slot = 0;
    bool alone = false;
};

// Kaniel 2026-10-16: the team menu in game (the team screen over the game, from the System menu:
// invite, remove a member, leave, join the hunt). What a game playing a save does about its team
// each frame: `party` is the team's party it plays in ("" alone), `start` the team's last Start.
//  - Leave: the lobby says it is out of that team (its leader removed it, or it left): it leaves
//    the team's session where it stands and plays on alone, in its own world (no reload).
//  - Adopt: playing alone, the Start of the team it is ready in came (it joined the hunt from the
//    menu): it takes the team's party, and joins the session in place (TeamJoin).
enum class TeamPlayStep { None, Adopt, Leave };
TeamPlayStep TeamPlayStepOf(const bbcoop::LobbyState* lobby, std::string_view party,
                            const bbcoop::TeamStart* start, const std::string& me = {});
// The menu's "Join the hunt" in game: the invitation to `invite_team` accepted first (when in no
// team), then this save ready with its slot and character; the leader's starts the team too.
std::vector<bbcoop::TeamRequest> TeamJoinHuntRequests(const bbcoop::LobbyState& lobby,
                                                      const std::string& me,
                                                      const std::string& invite_team, u32 slot,
                                                      const std::string& character);

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
    // TakeHost (phase 7): the server passed the room to this guest; it hosts the session now.
    // LeaveAlone (phase 7): a guest alone in its session for AloneLeaveMs leaves it.
    enum class Action { None, CreateSession, Publish, PublishClosed, Join, TakeHost, LeaveAlone };
    static constexpr s64 AloneLeaveMs = 5'000;
    struct Frame {
        bool host = false;
        s32 matching_state = 0; // the session manager's +0x124: 0 alone, 3 hosting, 6 a guest
        bool ready = false;     // the party controller's go (in the world, settled, retry spaced)
        bool session_known = false;   // a guest: the server sent its host's open session
        bool room_owner = false;      // the server passed the session's room to this game
        bool room_has_others = false; // another member is in that room (not the one who left)
        size_t session_members = 2;   // the game's session, this game counted
        bool standing = true;         // the player is up (not down or dead)
        s64 now_ms = 0;
    };
    Action Tick(const Frame& frame);

private:
    bool published = false;         // the host said its open session
    std::optional<s64> alone_since; // a guest alone in its session since
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

// Kaniel 2026-10-15: a cutscene that plays under the team's loading screen (a new character's
// opening) is skipped by itself: Options, the game's own skip, pressed for the player in short
// pulses while the cutscene player (SprjRemoMan) is in its playing step. `step` is that player's
// step (-1 none), entered at `step_ms`; true while Options is to be held down. The pad asks it
// as the game reads the pad (the game's own tick may stand still under a cutscene).
class CutsceneSkip {
public:
    // SprjRemoMan::STEP_PrologueWait and STEP_Update: the rig's opening spent ~100 s in the
    // first, the second only its last moment.
    static constexpr int PrologueStep = 3;
    static constexpr int PlayStep = 4;
    static constexpr s64 HoldMs = 120;        // a press
    static constexpr s64 EveryMs = 700;       // between presses
    static constexpr s64 LongestMs = 300'000; // in the step this long: stuck, left alone
    static constexpr int MostPresses = 200;   // about two minutes of presses: then left alone
    bool Press(bool covered, int step, s64 step_ms, s64 now_ms);
    int Presses() const {
        return presses;
    }

private:
    std::optional<s64> pressed_ms;
    int presses = 0;
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

    // This game's stage: loading its world, then joining, and ready only once it is in the
    // session and `others_seen` of the `others_needed` other hunters' characters stand in its
    // world (Kaniel 2026-10-15: the screen stays until every guest's character has spawned).
    static Stage OwnStage(bool loading, bool in_session, size_t others_seen, size_t others_needed);
    // The other members this game waits to see: `members` but this one (`me`) and those the map
    // board (`maps`: member, map) has on another map than `map` (0: loading, still waited for).
    static size_t OthersHere(std::span<const std::pair<std::string, std::string>> members,
                             std::string_view me, std::span<const std::pair<std::string, u32>> maps,
                             u32 map);
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
