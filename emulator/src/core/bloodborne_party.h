// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "common/types.h"

namespace Core::Bloodborne {

// Validates a "<name>:<secret>" party setting and returns the header value to send.
std::optional<std::string> ParsePartySetting(std::string_view setting);

enum class PartyRole { Host, Guest };
enum class PartyAction { Wait, StartSearch };

// The special effects a rung bell leaves on the player. While one is active the game's SOS
// status (0x0186FE40) reports the player as searching, so adding the effect is the whole bell.
// The Beckoning Bell's is 9000 (special state 189); the game adds 9003 next to it by itself.
constexpr s32 BeckoningBellEffect = 9000;
constexpr s32 SmallResonantBellEffect = 9005;

// 01.09's add-special-effect virtual (character vtable slot 0x3F0, 0x018C6DB0 for players).
// Only the effect id, the source and the 1.0 scales are understood; the rest are passed exactly
// as the game passes them.
using AddSpEffectFn = void PS4_SYSV_ABI (*)(u64 character, s32 effect_id, u64 source, s32 arg3,
                                            s32 arg4, s32 arg5, float scale0, float scale1,
                                            float scale2, float scale3, float scale4,
                                            s32 stack_arg);

// Starts the summon search without the bell item: adds the search effect to the player the same
// way 01.09 adds 9005 itself (0x01C0D3B0). No use-item path, so no animation, item or Insight.
bool StartSummonSearch(u64 player, s32 effect_id, AddSpEffectFn add_sp_effect);

// What the game looks like this tick, read from guest memory by bloodborne_re.cpp.
struct PartyObservation {
    bool player_valid = false;
    bool map_usable = false;
    bool stage_transition = false;
    bool matching_idle = false; // no multiplayer session, not even a pending one
    bool search_effect_active = false;
    bool white_phantom = false; // still in a host's world (the host may have left)
    // Hosting, with a party member online that is not in the session yet (a party of three:
    // the Beckoning Bell's effect ends when the first cooperator arrives).
    bool room_for_more = false;
    // A boss fight or its clear sequence runs (event flags 4000 / 4047): a player joining then is
    // kicked by the game, which ends the session.
    bool boss_sequence = false;
};

// CSMultiPlayMan session state (+0x124) as seen in the rig, and the summon-reload marker the
// game sets (+0x84 of the session rules) while it carries a guest into the host's world.
constexpr s32 MatchingStateHosting = 3;
constexpr s32 MatchingStateSummonedGuest = 6;
constexpr s32 SummonReloadInProgress = 3;

// Whether a world teardown must end the session even though the seamless patch keeps it: a
// summoned guest or a host leaving its world (quit to title), but never during a
// summon reload (see the tests for why). `reload_pending` is our own deferred summon reload.
bool ShouldEndSessionOnWorldTeardown(s32 matching_state, s32 reload_state, bool reload_pending);

// Deaths that keep the party. 01.09 runs the event scripts' death flow natively (the Lua source,
// global_event.lua in script/eventcommon.luabnd.dcx, is never loaded), so the change is a few
// byte patches in PartyGhostDeath / PartyGhostDeath_1 plus a revive where the menu used to be.
constexpr u64 PartyGhostSetEventFlagOffset = 0x0132E5A0;       // SetEventFlag(_, flag, on)
constexpr u64 PartyGhostCustomLuaCallStartOffset = 0x0132ED40; // CustomLuaCallStart(proxy, id, arg)
constexpr u64 PartyGhostDeathMenuBranchOffset = 0x01382359;    // jne past the menu (ClearBoss)

struct CodePatch {
    u64 offset;
    std::vector<u8> original;
    std::vector<u8> patched;
};
const std::vector<CodePatch>& GhostDeathKeepSessionPatches();

// Target of a `call rel32` at `offset`, or 0 if the bytes are not one.
u64 DecodeNearCallTarget(u64 offset, std::span<const u8> bytes);

// The event script bindings the revive uses (PS4 SysV; the unused first argument is the script
// state the native code passes along).
struct GhostDeathCalls {
    bool PS4_SYSV_ABI (*is_white_ghost)() = nullptr;
    bool PS4_SYSV_ABI (*set_event_flag)(u64 unused, s32 flag, bool on) = nullptr;
    void (*clear_return_state)() = nullptr; // ours: [SprjEventMan+0x10]+8, set by OnEvent_4000
    bool PS4_SYSV_ABI (*request_full_recover)() = nullptr;
    bool PS4_SYSV_ABI (*update_blood_mark)() = nullptr;
    bool PS4_SYSV_ABI (*revive_player)() = nullptr;
    void PS4_SYSV_ABI (*revive_local_character)() = nullptr;
    void (*restore_alive_state)() = nullptr; // ours: see AliveStateSnapshot
    void PS4_SYSV_ABI (*set_chr_type)(u64 unused, s32 entity, s32 type) = nullptr;
    void PS4_SYSV_ABI (*chr_reset_animation)(u64 unused, s32 entity) = nullptr;
    void (*warp_to_last_lamp)() = nullptr; // ours: GlobalState +0x1528, see the emulator
};
constexpr s32 DeathEventFlag = 4000;
constexpr s32 WhiteGhostCharacterType = 1;
constexpr s32 LocalPlayerEntity = 10000;

// What a character's death overwrites besides HP: the state flags at +0x1E0 (the kill sets the
// dead bit, +0x1E3 bit 0) and the action state the death processing writes ([+0x3B0]+8: +0x1B4
// and +0x1B8). Saved from the local player while it is alive, put back by the revive.
struct AliveState {
    u64 flags = 0;
    s32 action = 0;
    s32 action_sub = 0;
};
class AliveStateSnapshot {
public:
    void Observe(bool alive, const AliveState& state) {
        if (alive) {
            saved = state;
        }
    }
    const std::optional<AliveState>& Saved() const {
        return saved;
    }

private:
    std::optional<AliveState> saved;
};

// OnSelfBloodMark(4032, "SelfBloodMark", attributes) in the native g_Initialize: `mov ecx, 0x30`.
const std::vector<CodePatch>& GuestBloodstainPatches();

// HostDead's white phantom branch and the epilogue the seamless patch jumps to.
constexpr u64 HostDeadEpilogueOffset = 0x01381A50;
const std::vector<CodePatch>& HostDeathKeepGuestsPatches();
// A boss kill that keeps the party: BlockClear2_2 no longer sends the guest's leave broadcast
// (4063) or schedules BlockClear2_3Leave, and BlockClear2_3 clears a white phantom's leaving
// flags instead of warping it home. BlockClear2 no longer makes a white phantom wait for that
// load (SetLoadWait turned its action buttons off).
constexpr u64 OnKeyTime2Offset = 0x0132B1C0; // OnKeyTime2(proxy, event, state, delay, ...)
const std::vector<CodePatch>& BossClearKeepSessionPatches();
// Co-op areas (0x0191A750): a block's co-op is off while its flag in a table per area/block/part
// is set (Cathedral Ward: 2405/2406, set at load until the player's own world opened the gate,
// 12400160). A party joins anywhere: the flag check (0x01399BF0) reads as off.
constexpr u64 EventFlagIsSetOffset = 0x01399BF0;
const std::vector<CodePatch>& JoinAnywherePatches();
// A world load that is not a trip (0x0193C5xx: a cutscene that changes the time of day and
// warps the player, e.g. Gascoigne's 24000020) calls StopRequest (returning to 0x0193C65C) and
// LeaveSession for a member of a session. In a party it is a trip like a lamp's.
bool IsWorldReloadStopRequest(u64 caller_offset);
// Whether that StopRequest starts a party trip: from the world load, in a party session, with no
// trip under way.
bool StartsTripOnWorldReload(u64 caller_offset, bool trip_active, bool in_party_session);

// Target of a `jmp rel32` at `offset`, or 0 if the bytes are not one.
u64 DecodeNearJumpTarget(u64 offset, std::span<const u8> bytes);

// Revives the local player in place if it is a white phantom (a party guest). Returns whether it
// did; anyone else (and a missing binding) is left to the game.
bool ReviveDeadPartyGuest(const GhostDeathCalls& calls);

// The live player's version: SoloPlayDeath_2 replaced while in a session (no world reload): the
// host, and a guest that travelled (it is a live player afterwards).
bool ShouldReviveHostInPlace(s32 matching_state);
void ReviveDeadPartyHost(const GhostDeathCalls& calls);

// The sign poster's timers (seconds, a float at +0x28 of the sign object): 01.09 waits 5 s
// before a new sign's first post and 30 s between re-posts, ignoring ss.info's interval.
constexpr float FirstSignPostDelaySeconds = 0.0f;
constexpr float SignRepostSeconds = 1.0f;

// `mov dword ptr [<reg> + 0x28], <seconds>` with the ModRM byte of the game's own store.
std::array<u8, 7> EncodeSignTimerStore(u8 modrm, float seconds);
// The game's sign timer stores.
struct SignTimerSite {
    u64 offset;
    u8 modrm;
    float original_seconds;
    float patched_seconds;
};
constexpr std::array<SignTimerSite, 4> SignTimerSites{{
    {0x018A9264, 0x47, 5.0f, FirstSignPostDelaySeconds},
    {0x018A9324, 0x47, 5.0f, FirstSignPostDelaySeconds},
    {0x018A9757, 0x43, 30.0f, SignRepostSeconds},
    {0x018A988F, 0x43, 30.0f, SignRepostSeconds},
}};

// Lamps as campfires (a party session only; solo and co-op with strangers stay vanilla). A lamp
// is used through its talk script (t<map><nnn>.esd), which 01.09 hides while the game is in
// multiplayer (ESD function 52) and which asks "travel to the Hunter's Dream?" by setting
// 70000100 + map * 10000 + lamp for common event 7200 to warp. In a party session that flag
// means "rest here" instead, and the world refreshes once the whole party rests at one lamp.
constexpr s32 NotResting = 0;
bool IsLampTravelFlag(s32 flag);
// Party rests are work in progress and off by default: lamps travel as usual. The value of
// SHADPS4_BLOODBORNE_LAMP_REST; "1" turns them on.
bool LampRestsEnabled(const char* setting);
bool IsPartySession(bool party_configured, s32 matching_state);
// Whether the talk scripts should see this player as the host (ESD function 14 = 0).
bool TalksAsHost(bool in_party_session, s32 character_type);
// The Hunter's Dream lamp the lamps' trip warps to (common event 7200: WarpPlayerToRespawnPoint).
constexpr s32 HuntersDreamWarpPoint = 2102962;
// The packed map WarpNextStage_Bonfire (0x0132E050) derives from a warp point: area AA and block B
// of AABxxxx.
u32 PackedMapOfWarpPoint(s32 warp_point);
// The Dream's headstones: "travel to <lamp>" sets 7210MMML for event 12107000.
bool IsHeadstoneTravelFlag(s32 flag);
s32 HeadstoneWarpPoint(s32 flag);
// A party guest's lamp trip, which the game's event would end for a client.
bool GuestMakesItsOwnLampTrip(bool in_party_session, PartyRole role);
// Whether an environment setting is "1".
bool SettingIsOn(const char* setting);
// The guest-as-host experiment: turn a white phantom in a party session into a live player.
bool ShouldPlayGuestAsHost(bool switched_on, bool in_party_session, s32 character_type);

using LampPosition = std::array<float, 3>;
struct LampRest;

enum class LampUse { Rest, Travel };

// Where this player rests: from using a lamp until walking away from it (or the session ends).
class LampRestState {
public:
    static constexpr float LeaveDistance = 4.0f;
    static constexpr float JoinDistance = 2.5f;

    void Rest(s32 lamp, const LampPosition& at);
    // The lamp's dialog was confirmed: a rest, or the trip if this player already rests there.
    LampUse Use(s32 lamp, const LampPosition& at);
    // Joins a party member's rest when standing near where it rests; true if it did.
    bool JoinNearby(std::span<const LampRest> board, const std::optional<LampPosition>& player);
    std::optional<s32> Tick(const std::optional<LampPosition>& player, bool in_party_session);
    std::optional<LampPosition> RestedAt() const {
        return lamp ? std::optional{rested_at} : std::nullopt;
    }
    // Where others can join this rest: only a rest started at the lamp itself.
    std::optional<LampPosition> JoinablePosition() const;

private:
    std::optional<s32> lamp;
    LampPosition rested_at{};
    bool joined = false;
};

// The party's rest board on shadNet (POST /bloodborne/party/rest): each member posts
// "<member> <lamp>" and gets back one "<member> <lamp>" line per member resting right now.
struct LampRest {
    std::string member;
    s32 lamp = NotResting;
    std::optional<LampPosition> at; // where the member rests: "<member> <lamp> <x> <y> <z>"
};
std::string EncodeLampRestUpdate(std::string_view member, s32 lamp,
                                 const std::optional<LampPosition>& at = std::nullopt);
std::optional<std::vector<LampRest>> ParseLampRestBoard(std::string_view body);

// Fires once each time every member of the session comes to rest at the lamp this player rests
// at. Each member decides for itself; they all see the same board, so they refresh together.
class LampRestCoordinator {
public:
    bool Tick(std::optional<s32> resting_lamp, s32 session_members,
              std::span<const LampRest> board);

private:
    bool everyone_was_resting = false;
};

// What a lamp reload would do, in place: heal, refill vials and bullets from storage the way the
// respawn load does (0x01939470 calls 0x014DBB70 then 0x014DACE0), and respawn the world's
// enemies (WorldChrMan +0xA6C, see the emulator). The session is untouched.
struct LampRestCalls {
    void (*request_full_recover)() = nullptr;
    void (*clamp_restock_item)() = nullptr;
    void (*restock_from_storage)() = nullptr;
    void (*request_world_respawn)() = nullptr;
};
bool RefreshAtLamp(const LampRestCalls& calls);

// The bonfire respawn re-enables every character of the loaded blocks; the event scripts that
// hide some again do not all run in a session. A character to switch off again afterwards.
bool KeepDisabledAfterRespawn(bool disabled_before, s32 hp_before);
// A party rest respawns only what was killed during this visit to the map: living characters,
// and characters the scripts keep dead from the start, are left out of the bonfire respawn.
bool RespawnsAtPartyRest(s32 hp, bool alive_this_visit);
// The same for the character state flags (+0x1E0) the scripts set and the respawn clears.
constexpr u64 ScriptCharacterFlags = 0x4000 | 0x20000 | 0x40000000;
u64 RestoreScriptFlagsAfterRespawn(u64 flags_before, u64 flags_after, s32 hp_before);

// A party member's trip to another stage keeps the session: 01.09 would end it twice, once when
// the old world is torn down and once when the new stage has loaded (stage uid change). Active
// from the stage transition until that second point, or until it has clearly gone wrong.
class PartyTrip {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr Clock::duration Timeout = std::chrono::seconds{90};

    void Start(Clock::time_point now, u32 from_stage, u64 from_player) {
        started = now;
        from = from_stage;
        player = from_player;
    }
    void Finish() {
        started.reset();
    }
    // The current stage (packed map), whether the game is loading and the local player's
    // character, from the party tick. Over on a new stage, or after a reload of the same one (a
    // new character).
    void ObserveStage(u32 stage, bool loading, u64 current_player) {
        if (started && !loading && current_player != 0 &&
            (stage != from || current_player != player)) {
            Finish();
        }
    }
    bool Active(Clock::time_point now) const {
        return started && now - *started < Timeout;
    }

private:
    std::optional<Clock::time_point> started;
    u32 from = 0;
    u64 player = 0;
};

// Join tasks (CSMultiPlayerIns, one per session player, in CSMultiPlayMan's list): the kind at
// +0xE8 (0 the remote host, 1 a remote guest; 2 and 3 are the local player as guest or host)
// and the step at +0x50. Step 4 creates the player's character from its synced data (once +0xED
// bit 0 says the data is there), step 5 plays its arrival, step 6 is the steady in-session step.
constexpr s32 RemoteHostTask = 0;
constexpr s32 RemoteGuestTask = 1;
constexpr s32 CreateCharacterStep = 4;
// A remote player's join state from before a trip: kind, the member id's identity pointer, slot
// (+0xEC) and flags (+0xED, bit 0 = the member's data is synced).
struct SavedJoinTask {
    s32 kind = 0;
    u64 identity = 0;
    u8 slot = 0;
    u8 flags = 0;
    bool operator==(const SavedJoinTask&) const = default;
};
// The saved task a rebuilt one of `kind` takes: its member's own, else the only one of that kind.
std::optional<size_t> PickSavedJoinTask(std::span<const SavedJoinTask> saved, s32 kind,
                                        u64 identity);
// What a trip's start keeps: the tasks found now, and the saved ones of members not on this map
// (their slots are still theirs when they are met again).
std::vector<SavedJoinTask> MergeSavedJoinTasks(std::span<const SavedJoinTask> before,
                                               std::span<const SavedJoinTask> found);
constexpr s32 InSessionStep = 6;
bool ShouldRecreateAfterTrip(s32 kind, s32 step);
s32 RemoteTaskKind(bool hosting);
// SessionManager +0x118: set by the game's kick-all (0x0178D7F8, the ForceSummonSuccess handler);
// while it is set the host kicks every joiner on arrival (0x01ED121E, reason 0xff000019) until the
// room is made again. A party host's session is reopened instead.
constexpr u64 SessionClosingOffset = 0x118;
bool ReopensClosedSession(bool party, s32 matching_state, u8 closing);
// The kind of a join task rebuilt after a trip for the member with `identity`: on the host every
// other member is a remote guest; on a guest the member's own saved kind (the host 0, another
// guest 1), else a guest when the saved tasks name another member as the host, else the host.
s32 RebuiltTaskKind(bool hosting, std::span<const SavedJoinTask> saved, u64 identity);
s32 LocalTaskKind(bool hosting);
bool ShouldRestoreSearchAfterTrip(bool in_party_session, bool search_effect_active);
// The bell effects (9000/9003, 9005/9006) in `before` that are missing from `after`, in order.
std::vector<s32> EffectsToRestoreAfterTrip(std::span<const s32> before, std::span<const s32> after);

// The party's map board on shadNet (POST /bloodborne/party/map): each member posts
// "<member> <map>" (the packed map in hex) and reads back one such line per member.
struct MemberMap {
    std::string member;
    u32 map = 0;
};
std::string EncodeMapUpdate(std::string_view member, u32 map);
std::optional<std::vector<MemberMap>> ParseMapBoard(std::string_view body);
// Whether there is someone else in the party and all of them are on `map`.
bool EveryoneElseOnMap(std::span<const MemberMap> board, std::string_view self, u32 map);
// Whether no other member is on `map` (a usable map: 0 while loading counts as not alone).
bool NoOneElseOnMap(std::span<const MemberMap> board, std::string_view self, u32 map);

// A party of three: the boards know a member by its online id, hex-encoded (board member names
// are letters and digits only), and a session member's identity object holds its online id.
std::string BoardMemberOfOnlineId(std::string_view online_id);
std::string OnlineIdOfBoardMember(std::string_view member); // "" if it is not one
// The one candidate found in `bytes` as a whole NUL-terminated name, or nothing.
std::optional<std::string> FindOnlineIdIn(std::span<const u8> bytes,
                                          std::span<const std::string> candidates);
// After this member's trip: which session members (their board names, nullopt if unknown) to
// re-create now, the ones on this map not handled yet, and whether every one is then handled.
// With a member that cannot be told apart, all of them once everyone else is here.
struct RecreatePlan {
    std::vector<size_t> now;
    bool done = false;
};
RecreatePlan PlanRecreate(std::span<const std::optional<std::string>> members,
                          std::span<const MemberMap> board, std::string_view self, u32 map,
                          const std::set<std::string>& handled);
// Whether the host (its map from its map state post) is on `map` and still on the map board.
bool HostIsHere(std::optional<u32> host_map, std::span<const MemberMap> board,
                std::string_view self, u32 map);
// A guest runs its map's enemies while this holds: the host is not on its map (two guests there
// both do). Without the host's post yet, no other member on its map.
bool HostAway(std::optional<u32> host_map, std::span<const MemberMap> board, std::string_view self,
              u32 map);
// Another member is on another map (kill echoes go to the board then).
bool SomeoneElsewhere(std::span<const MemberMap> board, std::string_view self, u32 map);
// The host rings again: hosting, fewer than three in the session, a party member online outside.
bool RoomForMore(bool hosting, size_t in_session, size_t party_online);

// Shared trophies on shadNet (POST /bloodborne/party/trophy): "<member> [<id>...]" with the
// trophies this member unlocked in a party session; the answer is every trophy the party
// unlocked, one id per line. A member unlocks the ones it has not handled yet.
std::string EncodeTrophyUpdate(std::string_view member, std::span<const s32> trophies);
std::optional<std::vector<s32>> ParseTrophyBoard(std::string_view body);
std::vector<s32> TrophiesToUnlock(std::span<const s32> board, std::span<const s32> handled);

// A guest's progress in the host's world never reaches its own save. Picked-up map items
// (ItemLotParam getItemFlagId 5[0-3]MMxxxx, chalice dungeons 529xxxxx left out) set in a party
// session are kept in a file, one flag per line, and set again in the guest's own world.
bool IsMapPickupFlag(s32 flag);
// The kept progress is per character: the save slot of /savedata0/userdata000N (N 0-9).
std::optional<int> CharacterSlotOfSavePath(std::string_view path);
// Pickups once confirmed set in the character's own world never clear again, so a load of a
// slot where none of them is set holds a new character, and the slot's kept lists are dropped.
bool IsNewCharacterInSlot(std::span<const s32> confirmed, size_t confirmed_set_now);
std::string FormatFlagList(std::span<const s32> flags);
std::vector<s32> ParseFlagList(std::string_view text);
// The event flag store (EventFlagMan [0x0553B100]): blocks of +0x1C flags each, kept in a map at
// +0x38 by block id; a flag's bit is at byte index/8, most significant bit first.
struct FlagChanges {
    std::vector<s32> set;
    std::vector<s32> cleared;
};
FlagChanges ChangedFlags(u32 block, u32 flags_per_block, std::span<const u8> before,
                         std::span<const u8> after);
// Shared progression: the guest's copy of the host's world follows the host's flag changes, so
// the changes it sees in a session (lamps, shortcuts, bosses, quest steps) are kept, latest
// state per flag, in a file of "<flag> <0|1>" lines and set in the guest's own world once it is
// home. A join reload's bulk change, the death/multiplayer (4xxx) flags and
// the pickups (their own list) are left out.
bool CarriesToGuestWorld(s32 flag);
// Whether a load of the guest's world gets the kept world changes before its map items and
// events: at home, and on a party trip (its own world loads while the session stays up).
bool SetsGuestWorldChangesOnLoad(bool in_session, bool on_trip);
// True once per party trip: the first time it is asked while the trip is on.
class OncePerTrip {
public:
    bool Take(bool on_trip) {
        if (!on_trip) {
            taken = false;
            return false;
        }
        const bool first = !taken;
        taken = true;
        return first;
    }

private:
    bool taken = false;
};
// A lamp's "active" flag (10007810 + map * 10000 + lamp * 20, what the lamp talk scripts check)
// to its lamp entity (map * 10000 + 1950 + lamp); the game clears the flag again for a character
// whose lamp list ([0x0553B120]+0xC70) lacks the lamp.
std::optional<s32> LampOfActiveFlag(s32 flag);
// The other player's "set this event flag" (0x017C0570) and "event ended" (0x017C0901) messages
// leave these flags and events alone in a party: what one player received is not given to the
// other (the Hunter's Dream messengers' gift events and flags, 12101020-12101029, and the
// pick in their talk, 72101000-72101099).
bool KeepsLocalFlag(bool party_configured, s32 flag);
// SHADPS4_BLOODBORNE_TRACE_FLAG: a flag the flag tracer watches in a session (diagnostics).
std::optional<s32> TracedFlagSetting(const char* setting);
struct FlagBit {
    u32 block = 0;
    u32 byte = 0;
    u8 mask = 0;
};
// Where a flag lives in the event flag store (blocks of flags_per_block flags, MSB first).
FlagBit FlagBitLocation(s32 flag, u32 flags_per_block);
// A lamp's first registration for a character (0x0146DA20) writes its flags base..base+11 off
// (+10/+11: lit). Those on before it (bit i of on_before: base + i, a lamp another party member
// lit) are set again in a party.
std::vector<s32> LampFlagsKeptAtRegistration(bool party_configured, s32 base, u16 on_before);
// Where a party death wakes the player (point: GlobalState +0x1528, which the events set on a
// death: the first one to the Hunter's Dream, a kidnapping to the Gaol): moved there while the
// point is in a loaded block, a trip there otherwise, except right after such a trip.
enum class DeathRespawn { StayPut, MoveToPoint, Travel };
DeathRespawn ChooseDeathRespawn(s32 point, bool in_loaded_block, bool arriving_from_trip);
// A flag every party member gets when another one sees it set (see PartyFlagBoard): a lamp's
// active flags (LampOfActiveFlag and the one after), which the game does not sync itself, and
// a boss fog's "entered" flag, which a guest's (unsynced) fog event sets to start the boss.
bool SharedWithParty(s32 flag);
// A boss fog's "host entered" flag (12414700 for Father Gascoigne): the boss start event waits
// on it. Party guests set it too (party_boss_events.py); it holds for one map visit.
bool IsBossFogEntryFlag(s32 flag);
// The crash catcher's own log line (see bloodborne_crash_dump.cpp): the exception code, the
// address, the eboot offset when it is inside the game, and the thread's name.
std::string FormatCrashLine(u32 code, u64 address, u64 image_base, u64 image_size,
                            std::string_view thread);
// The words of a stack that point into the eboot, as " eboot+0x..." (at most `max` of them).
std::string FormatGameAddresses(std::span<const u64> words, u64 image_base, u64 image_size,
                                size_t max);
// The co-op HP effect a boss start gives the boss (SpEffect 7500/7501/7502, max HP x1.5/x2/x2.5)
// sometimes misses a boss on one side (hidden by its entrance there), so that side keeps the
// unscaled max HP. Given the max HP seen before any co-op effect, the new (max HP, HP) for a
// living boss that has the effect but not the scaled max HP.
std::optional<std::pair<s32, s32>> CoopHpScale(s32 unscaled_max, s32 max, s32 hp,
                                               std::span<const s32> effects);
// What a member posts to the party's flag board from a change it saw: the SharedWithParty flags
// that turned on.
std::vector<std::pair<s32, bool>> FlagsToShare(const FlagChanges& changes);
bool IsBulkFlagChange(size_t changed_flags);
void RecordWorldChanges(std::map<s32, bool>& world, const FlagChanges& changes);
std::string FormatWorldChanges(const std::map<s32, bool>& world);
std::map<s32, bool> ParseWorldChanges(std::string_view text);
// Lamps shared by the party (POST /bloodborne/party/flags): every member posts the lamps it saw
// lit (FlagsToShare; "<member> <since> [<flag>:<0|1>...]") and sets the other members' changes
// after <since> (the answer: "<latest>", then "<flag> <0|1>" lines).
struct FlagBoardAnswer {
    s64 latest = 0;
    std::vector<std::pair<s32, bool>> changes;
    bool operator==(const FlagBoardAnswer&) const = default;
};
std::string EncodeFlagUpdate(std::string_view member, s64 since,
                             std::span<const std::pair<s32, bool>> changes);
std::optional<FlagBoardAnswer> ParseFlagBoard(std::string_view body);

// Kill echoes shared across maps (POST /bloodborne/party/echoes): the echoes a member got from a
// kill (AddSouls called from the enemy kill 0x018FC99B or the boss kill 0x01385263) while no
// other member was on its map go to the board ("<member> <since> [<echoes>...]"), and the other
// members add the ones after <since> (the answer: "<latest>", then one "<echoes>" line each).
struct KillEchoes {
    s32 amount = 0;
    u32 map = 0; // where the kill was made; 0 from an older post
    bool operator==(const KillEchoes&) const = default;
};
struct EchoBoardAnswer {
    s64 latest = 0;
    std::vector<KillEchoes> echoes;
    bool operator==(const EchoBoardAnswer&) const = default;
};
// `someone_elsewhere`: a party member in the session is on another map (the game shares the
// echoes with the ones on this map itself).
bool SharesKillEchoes(u64 caller_offset, bool someone_elsewhere);
// Whether a member on `my_map` adds the echoes of a kill made on `kill_map`.
bool GrantsKillEchoes(u32 kill_map, u32 my_map);
// Whether the party boards (lamps and boss fog flags, trophies) are read, posted and applied:
// whenever a party is configured and the player is loaded, in a session or not.
bool SharesWithParty(bool party_configured, bool player_loaded);

// Enemy deaths kept in step across loads (POST /bloodborne/party/kills, the flag board's format
// with character handles): the kills this member made during its current visit to a map, posted
// as (handle, true), and taken back as (handle, false) when the visit ends. A visit is one load
// of a map (the map and the player's character).
// Where a party guest resumes (user/bloodborne_party_resume_slotN.txt, "<map> <x> <y> <z>"):
// its last map and position, so the next start goes back there instead of to its save's
// position (where it was first summoned from).
struct ResumePoint {
    u32 map = 0;
    LampPosition position{};
    s32 progress = 0; // event flags set when it was kept (a new character has far fewer)
    bool operator==(const ResumePoint&) const = default;
};
std::string EncodeResumePoint(const ResumePoint& point);
std::optional<ResumePoint> ParseResumePoint(std::string_view text);
// The warp point a trip to a map uses: its first lamp (<area><block>2950), or the Hunter's Dream
// headstones' point.
s32 MapWarpPoint(u32 packed_map);
struct ResumeStep {
    enum class Kind { Stay, Move, Travel, Forget };
    Kind kind = Kind::Stay;
    s32 warp_point = 0;
};
// From where the guest loaded: stay (nothing remembered, or within 5 m), move on the same map,
// travel to the remembered map first, or forget the point (another character in the slot). A
// guest no longer on the map it loaded on (`loaded_map`, 0 if not known) travelled on: it stays.
ResumeStep ChooseResumeStep(const ResumePoint& point, u32 map, const LampPosition& at, s32 progress,
                            u32 loaded_map = 0);

// World state per map (POST /bloodborne/party/mapstate): the host posts the bits of its map's
// 10000 flags from MapFlagBase (MSB first, as the flag store keeps them) as "<member> <map>
// <hex>", and a guest loading that map sets the TakesHostMapFlag ones to the host's.
s32 MapFlagBase(u32 packed_map);
bool TakesHostMapFlag(s32 flag);
constexpr s32 MapFlagCount = 10000;
struct MemberMapState {
    u32 map = 0;
    std::vector<u8> bits;
    bool operator==(const MemberMapState&) const = default;
};
std::string EncodeMapStateUpdate(std::string_view member, u32 map, std::span<const u8> bits);
std::optional<std::vector<MemberMapState>> ParseMapStateBoard(std::string_view body);

// A member's visit to a map (see VisitKills): it ends when the enemies respawn for that member.
// trips counts the party trips started (lamps, the Dream, a death sent home).
class VisitTracker {
public:
    u64 Update(u32 map, u64 player, bool in_session, u64 trips);

private:
    u32 map = 0;
    u64 player = 0;
    u64 trips = 0;
    u32 generation = 0;
};

// Enemy HP kept in step (POST /bloodborne/party/hp, the map state board's format): each member
// posts the enemies it has hurt during its visit, as (handle, HP, max HP), and every member
// shows the lowest share of HP left. Since seamless travel each member runs its own copy of a
// map, so the host's hits never reached the guest's Gascoigne (friend run 2026-10-09).
struct EnemyHp {
    u32 handle = 0;
    s32 hp = 0;
    s32 max = 0;
    bool operator==(const EnemyHp&) const = default;
};
struct EnemyHpReport {
    u64 visit = 0;
    std::vector<EnemyHp> enemies;
    bool operator==(const EnemyHpReport&) const = default;
};
std::vector<u8> EncodeEnemyHp(u64 visit, std::span<const EnemyHp> enemies);
std::optional<EnemyHpReport> DecodeEnemyHp(std::span<const u8> bytes);
// The HP to write on the local copy, or nothing: only ever down, never to 0 (a death comes
// from the game itself), scaled by each copy's max HP (the co-op HP bonus may differ).
std::optional<s32> SharedEnemyHp(s32 hp, s32 max, s32 other_hp, s32 other_max);
// The HP to write on this member's copy of `handle`: the lowest share any other member in `map`'s
// area reports (their posts, in the map state board's format), or nothing.
std::optional<s32> LowestSharedHp(u32 handle, s32 hp, s32 max, u32 map,
                                  std::span<const MemberMapState> others);

// The kill board's answer names who posted each change ("<handle> <0|1> <member>").
struct KillChange {
    s32 handle = 0;
    bool killed = false;
    std::string member;
    bool operator==(const KillChange&) const = default;
};
struct KillBoardAnswer {
    s64 latest = 0;
    std::vector<KillChange> changes;
    bool operator==(const KillBoardAnswer&) const = default;
};
std::optional<KillBoardAnswer> ParseKillBoard(std::string_view body);
// The other members' standing kills: an enemy stays killed while any member's kill of it stands.
class PartyKills {
public:
    void Apply(std::span<const KillChange> changes);
    bool Contains(u32 handle) const;
    std::unordered_set<u32> All() const;

private:
    std::map<std::string, std::set<u32>> by_member;
};

// Whether enemy deaths are watched now: sharing, and the world standing (no trip, no load).
bool WatchesKills(bool sharing, bool trip_active, bool loading);
// Whether a character's death flags (+0x1E3) mark a real death: bit 0 from the game's kill (an
// event's ForceCharacterDeath, 0x018BFAD3), bit 2 from a death by damage (0x018C52D1, after the
// kill reward). Both are cleared when it comes back (0x018CC550). Not with bit 5: an enemy its
// world puts down, dead in that save (0x26; cleared at 0x013320DF), is no kill made now.
bool IsRealDeath(u8 death_flags);

// A character as the kill board sees it: its handle, its HP (0 once really dead, see
// IsRealDeath) and the object itself, which a world that loads makes anew.
struct SeenCharacter {
    u32 handle = 0;
    s32 hp = 0;
    u64 instance = 0;
    bool settling = false; // just taken over or handed back (see EnemyOwnerChangeSettles)
};

// How long a character's death is only seen, not posted, after this member took it over or
// handed it back (ClaimEnemiesWhenAlone): its own world's state settles on it then (a boss it
// killed for good goes down).
constexpr auto EnemyOwnerChangeSettles = std::chrono::seconds{5};
// Whether a character still settles at `now_ms`: within EnemyOwnerChangeSettles of the last time
// the map's world changed hands (`world_changed_ms`: a load that kept the map's objects, a summon
// into the host's world, or a guest starting or ending its run of the map's enemies) or of its
// own takeover or hand-back (`owner_changed_ms`).
bool CharacterSettles(s64 now_ms, s64 world_changed_ms, std::optional<s64> owner_changed_ms);

class VisitKills {
public:
    std::vector<std::pair<s32, bool>> Observe(u64 visit, std::span<const SeenCharacter> characters);
    const std::vector<u32>& Killed() const {
        return killed;
    }

private:
    struct Seen {
        s32 hp = 0;
        u64 instance = 0;
    };
    u64 current = 0;
    std::map<u32, Seen> seen;
    std::vector<u32> killed;
};
std::string EncodeEchoUpdate(std::string_view member, s64 since,
                             std::span<const KillEchoes> echoes);
std::optional<EchoBoardAnswer> ParseEchoBoard(std::string_view body);

// The multiplayer walls (common event 7600, from the maps' event files): the wall object and the
// fog SFX the event shows while the game is in multiplayer.
struct MultiplayerWall {
    s32 object = 0;
    s32 sfx = 0;
};
std::vector<MultiplayerWall> MultiplayerWallsOn(u32 packed_map);

// The summoned guest's special effect that cuts its max HP (SpEffectParam row 9006: max HP rate
// 0.7 at +0x10). A party guest keeps its full HP.
constexpr s32 PhantomHpEffect = 9006;
constexpr u64 SpEffectMaxHpRateOffset = 0x10;
// Level sync: a player whose level is above the host's band (host level * (1 + rate) + add,
// from the online settings) gets special effect 9200 (0x01507DDC), and every character whose
// effects include it has its max HP, stamina and attack scaled to the host's level through the
// CalcCorrectGraph rows 120-124 (0x01582940). A party guest keeps its own level.
constexpr s32 LevelSyncEffect = 9200;
bool LevelSyncsGuest(bool in_party_session, bool above_host_band);
// A party guest's lost echoes: joining the host clears GameDataMan +0x28 ("has a bloodstain")
// but keeps the record (echoes +0x38, block +0x3C), so the enemy holding them gives nothing
// back. The mark is set again while the record still holds echoes.
bool RestoresBloodstainMark(bool party_guest, bool mark_before, bool mark_now, s32 echoes,
                            u32 block);
// Friendly NPCs on a summoned guest's side: character creation (0x018E4FB6, 0x018D8934) turns an
// NPC whose NpcParam has isChangeWanderGhost into a wandering ghost (type 4, the ghost model at
// +0x10C), drawn see-through. A party guest sees them as the host does.
constexpr u64 NpcWanderGhostFlagOffset = 0x145;
constexpr u32 NpcWanderGhostFlag = 0x80;
bool NpcTurnsIntoWanderGhost(bool in_party_session, bool param_says_wander_ghost);
// Map items (MapItemMan, 0x017D-0x017E): a summoned guest (matching state at CSMultiPlayMan
// +0x124 is 6, or the session's joined id at +0x98 is not -1) creates no map items
// (0x017DC3CF) and may pick one up only while GlobalState +0x1592 is set (0x017D6831,
// 0x017D6AEF). A party guest reads those checks from a stand-in that answers like a solo
// player, so each player sees and picks up its own copy. It does so whenever a party is
// configured: the guest's map reload for a join runs before the matching state reads as a
// session, and a configured party only ever joins its own members.
constexpr u64 MatchingStateOffset = 0x124;
constexpr u64 JoinedSessionOffset = 0x98;
constexpr u64 GuestItemPickupAllowedOffset = 0x1592;
const std::array<u8, GuestItemPickupAllowedOffset + 8>& SoloMapItemStandIn();
bool GuestHandlesMapItemsAlone(bool party_configured);
// The boss item: 2003:36 (AwardItemLot even for a client, 0x0132E4A0) calls the award
// 0x017DDC50(item man, lot, 0), returning to 0x0132E4B5. For a party guest it lands inside the
// boss-clear sequence and its dialog leaves the guest unable to confirm anything, so it is held
// and given DeferredBossItemDelay later.
constexpr u64 ClientItemLotAwardReturn = 0x0132E4B5;
constexpr auto DeferredBossItemDelay = std::chrono::seconds{8};
bool DefersItemLotAward(bool party_configured, bool summoned_guest, u64 return_offset);
// The menu scripts' "am I a summoned client" condition (0x01A13FD1, matching state 6 or a
// joined session): with a party configured the answer is "no", like the item dialog's host.
bool MenuSeesAClient(bool party_configured, bool summoned_client);
// Map item packets between players (channel 0x2C, read at 0x017D33D8): header word 1, then the
// message kind. The host's item sync clears the guest's map items (0x017D3498-0x017D34A8), so
// in a configured party every such packet reads as empty and each player's items stay its own.
constexpr u32 MapItemPacketChannel = 0x2C;
u32 MapItemPacketHeaderSeen(u32 header, bool party_configured);
// An 8-byte `vmovss xmm, [rip+disp32]` at `address` changed to load from `target` instead.
std::array<u8, 8> RetargetRipLoad(std::span<const u8, 8> original, u64 address, u64 target);
// A loaded PARAM file: the type name at +0x0C, the row count (u16) at +0x0A and sorted 24-byte
// row entries (id, padding, data offset) at +0x40.
bool IsParamOfType(std::span<const u8> param, std::string_view type);
std::optional<u64> FindParamRow(std::span<const u8> param, s32 id);

// Auto-rejoin for a configured party: whenever this player is back in its own world with no
// session and no active search, start the role's search again (host: Beckoning, guest: Small
// Resonant) once the world has been stable for a while.
// OnLeave_Limit (event 4046, 0x0138A4A0) resets a player into its own world and warps it there,
// meant for a white phantom whose host is gone. Whether a party member skips it (see
// StayWhenTheHostLeaves): a phantom only while the session has `other_members` besides it (else
// it goes home and resumes where it was); a live player (the host, or a guest after its own
// trip) always, as it is in its own world already.
bool SkipsLeaveLimit(s32 character_type, size_t other_members);

// The placement a guest's sign gets with its claim (for a party guest, its own spot: it joins in
// place) is for that one join. The pre-match warp and the summon handoff move the guest to it, so
// kept after the session it brought, it warped a guest that had travelled on and then lost its
// host back to where it had joined. Spent once that session is over: `session_over` is matching
// idle with no reload or stage transition pending.
class JoinPlacementLife {
public:
    void Received() {
        joined = false;
    }
    // True once, on the tick the placement is spent (forget it then).
    bool Spent(s32 matching_state, bool session_over) {
        if (matching_state == MatchingStateSummonedGuest) {
            joined = true;
            return false;
        }
        if (!joined || !session_over) {
            return false;
        }
        joined = false;
        return true;
    }

private:
    bool joined = false;
};

class PartyController {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr u32 StableTicksBeforeSearch = 20;
    static constexpr Clock::duration RetryDelay = std::chrono::seconds{15};

    explicit PartyController(PartyRole role) : role{role} {}

    s32 SearchEffectId() const {
        return role == PartyRole::Host ? BeckoningBellEffect : SmallResonantBellEffect;
    }
    PartyAction Tick(const PartyObservation& observation, Clock::time_point now);

private:
    PartyRole role;
    u32 stable_ticks = 0;
    std::optional<Clock::time_point> last_search;
    /// Since when a hosting bell with room for more has been on (see Tick).
    std::optional<Clock::time_point> ringing_since;
};

// Autoplay (SHADPS4_BLOODBORNE_AUTOPLAY=<script file>), for tests with no one at the pad: the
// emulator presses Cross on the title screen until the player is in the world, then runs the
// script, one command per line ("<verb> <args...>", # starts a comment).
struct AutoplayCommand {
    std::string verb;
    std::vector<std::string> args;
    bool operator==(const AutoplayCommand&) const = default;
};
std::vector<AutoplayCommand> ParseAutoplayScript(std::string_view text);
// The pad button a script names (cross, circle, square, triangle, options, up, down, left, right,
// l1, r1, l2, r2, l3, r3), 0 if none.
u32 AutoplayButton(std::string_view name);
// Whether Cross is down `ms` after the title screen's autoplay began.
bool TitlePressesCross(u64 ms);
// The game's own kill (what an event's ForceCharacterDeath does): HP to 0, the dead flag
// (+0x1E3 bit 0), then the death step (0x018BD9D0). `void (character)`.
constexpr u64 KillCharacterOffset = 0x018BF9A0;
// A target: "nearest" (0), "self" (AutoplaySelf, the player), "marked" (AutoplayMarked, the one
// the last mark command picked) or a character handle in hex.
constexpr u32 AutoplaySelf = 0xFFFFFFFF;
constexpr u32 AutoplayMarked = 0xFFFFFFFE;
std::optional<u32> ParseAutoplayTarget(std::string_view text);
// `value <op> limit` for op <, <=, ==, !=, >=, >; nothing for another op.
std::optional<bool> CompareAutoplay(s64 value, std::string_view op, s64 limit);
// attack <target>: a real hit, so the kill reward and the death run as in play (the game's own
// kill leaves an ordinary enemy standing in a party). Cycle k (AttackCycleSeconds each) stands
// AttackDistance from the target on side k % 4 (+z, +x, -z, -x) and holds R1 from 0.3 s to 0.5 s
// into the cycle, turned towards the target (HeadingTowards).
struct AttackStance {
    float x = 0.0f;
    float z = 0.0f;
};
constexpr double AttackCycleSeconds = 1.5;
constexpr float AttackDistance = 1.2f;
AttackStance AttackStanceFor(float target_x, float target_z, u32 cycle);
// The teleport heading (radians) that turns a player at (x, z) to face (to_x, to_z): heading h
// faces (-sin h, cos h).
float HeadingTowards(float x, float z, float to_x, float to_z);
u32 AttackCycleAt(double elapsed);
bool AttackHoldsR1(double elapsed);

} // namespace Core::Bloodborne
