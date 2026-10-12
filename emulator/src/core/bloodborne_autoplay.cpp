// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The rig's autoplay: its script verbs, census and pad. Split from bloodborne_re.cpp; the
// shared declarations are in bloodborne_re_internal.h.

#include "core/bloodborne_crash_dump.h"
#include "core/bloodborne_party.h"
#include "core/bloodborne_re.h"
#include "core/bloodborne_re_internal.h"
#include "core/bloodborne_write_watch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include <fmt/ranges.h>
#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/cpu_patches.h"
#include "core/debugger.h"
#include "core/emulator_settings.h"
#include "core/file_format/psf.h"
#include "core/libraries/network/sockets.h"
#include "core/libraries/np/np_handler.h"
#include "core/libraries/np/np_matching2/np_matching2_signaling.h"
#include "core/libraries/np/np_trophy.h"
#include "core/libraries/system/userservice.h"
#include "core/memory.h"

namespace Core::Bloodborne {
namespace {

// Autoplay (SHADPS4_BLOODBORNE_AUTOPLAY=<script>, see ParseAutoplayScript): tests with no one at
// the pad. The pad reader (AutoplayPad) presses Cross on the title screen until the player is in
// the world; the party tick then runs the script's commands one after the other, logging each
// ("Bloodborne autoplay: step 3/9 travel 2102962 ok"), and "Bloodborne autoplay: done (ok)" or
// "done (failed)" at the end, which the rig's test driver waits for.
struct AutoplayRun {
    enum class Phase { Off, Title, World, Done };
    Phase phase = Phase::Off;
    std::vector<AutoplayCommand> script;
    size_t next = 0;
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point step_started{};
    bool step_begun = false;
    std::optional<std::chrono::steady_clock::time_point> hold_until;
    u32 travel_map = 0;
    std::optional<double> travel_fixed_at; // seconds into the step it was moved to the lamp
    u32 attack_handle = 0;
    std::optional<u32> attack_placed; // the attack cycle the player was last put in place for
    s32 noted_max_hp = 0;             // note-max-hp: the player's max HP then
    u32 pickup_handle = 0;            // pickup: the map item it went for
    s32 hit_from_hp = 0;              // hit: the target's HP when it began
};
std::mutex autoplay_mutex;
AutoplayRun autoplay_run;
std::atomic<u32> autoplay_buttons{0};
std::atomic<bool> autoplay_godmode{false};
std::atomic<u32> autoplay_sticks{0x80808080}; // lx, ly, rx, ry
std::once_flag autoplay_loaded;

void LoadAutoplay() {
    const char* path = std::getenv("SHADPS4_BLOODBORNE_AUTOPLAY");
    if (path == nullptr || *path == '\0') {
        return;
    }
    std::ifstream file(path);
    const std::string text{std::istreambuf_iterator<char>{file}, {}};
    std::scoped_lock lock{autoplay_mutex};
    autoplay_run.script = ParseAutoplayScript(text);
    autoplay_run.phase = AutoplayRun::Phase::Title;
    autoplay_run.started = std::chrono::steady_clock::now();
    LOG_INFO(Debug, "Bloodborne autoplay: {} steps from {}", autoplay_run.script.size(), path);
}

// The rig's checks and reports read the game's own map (GetCurrentPackedMap), not the place
// tracker: they test the game, the tracker included; only comparisons with a board's maps use
// the tracker's, as the boards do.
bool AutoplayInWorld() {
    const u64 global = GetGlobalState();
    return GetLocalPlayer() >= 0x10000 && IsUsablePackedMap(GetCurrentPackedMap()) &&
           global >= 0x10000 && HasMemoryAccess(global, 0x09, MemoryProt::CpuRead) &&
           ReadValue<u8>(global, 0x08) == 0 && LocalPlayerPosition().has_value();
}

size_t AutoplaySessionMembers() {
    const u64 session = ReadValue<u64>(image_base + SessionManagerPointerOffset, 0);
    if (session < 0x10000 || !HasMemoryAccess(session, 0x128, MemoryProt::CpuRead)) {
        return 1;
    }
    const u64 begin = ReadValue<u64>(session, 0xF8);
    const u64 end = ReadValue<u64>(session, 0x100);
    return begin >= 0x10000 && end > begin && end - begin <= 0x20 * 8 ? (end - begin) / 0x20 : 1;
}

size_t AutoplayMembersOnMyMap() {
    const u32 map = PlayingMap();
    std::scoped_lock lock{map_board_mutex};
    if (std::chrono::steady_clock::now() - map_board_time > std::chrono::seconds{3}) {
        return 0;
    }
    return static_cast<size_t>(
        std::ranges::count_if(map_board, [&](const MemberMap& entry) { return entry.map == map; }));
}

struct AutoplayCharacter {
    u64 character = 0;
    u64 data = 0;
    float distance = 0.0f;
};

// The character a command names: by handle, or the nearest living one within 30 m (0).
std::atomic<u32> autoplay_marked{0};

std::optional<AutoplayCharacter> FindAutoplayCharacter(u32 handle) {
    if (handle == AutoplayMarked) {
        handle = autoplay_marked.load();
        if (handle == 0) {
            return std::nullopt;
        }
    }
    if (handle == AutoplaySelf) {
        const u64 player = GetLocalPlayer();
        const u64 modules = player >= 0x10000 ? ReadValue<u64>(player, 0x3B0) : 0;
        const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
        return data >= 0x10000 ? std::optional{AutoplayCharacter{player, data, 0.0f}}
                               : std::nullopt;
    }
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    const auto me = LocalPlayerPosition();
    if (world < 0x10000 || !me) {
        return std::nullopt;
    }
    std::optional<AutoplayCharacter> found;
    ForEachWorldCharacter(world, [&](u64 character) {
        if (character == GetLocalPlayer()) {
            return;
        }
        const u64 modules = ReadValue<u64>(character, 0x3B0);
        const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (data < 0x10000 || physics < 0x10000) {
            return;
        }
        const float dx = ReadValue<float>(physics, 0x1E0) - (*me)[0];
        const float dy = ReadValue<float>(physics, 0x1E4) - (*me)[1];
        const float dz = ReadValue<float>(physics, 0x1E8) - (*me)[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (handle != 0) {
            if (ReadValue<u32>(character, 0x08) == handle) {
                found = AutoplayCharacter{character, data, distance};
            }
            return;
        }
        if (ReadValue<s32>(data, 0xF8) > 0 && distance < 30.0f &&
            (!found || distance < found->distance)) {
            found = AutoplayCharacter{character, data, distance};
        }
    });
    return found;
}

void LogAutoplaySnapshot() {
    const auto at = LocalPlayerPosition();
    std::string enemies;
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    std::vector<std::pair<float, std::string>> close_by;
    if (world >= 0x10000 && at) {
        ForEachWorldCharacter(world, [&](u64 character) {
            if (character == GetLocalPlayer()) {
                return;
            }
            const u64 modules = ReadValue<u64>(character, 0x3B0);
            const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
            const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
            if (data < 0x10000 || physics < 0x10000) {
                return;
            }
            const float dx = ReadValue<float>(physics, 0x1E0) - (*at)[0];
            const float dy = ReadValue<float>(physics, 0x1E4) - (*at)[1];
            const float dz = ReadValue<float>(physics, 0x1E8) - (*at)[2];
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance < 30.0f) {
                close_by.emplace_back(
                    distance,
                    fmt::format("{:#x} hp {}/{} dead {:#x} at {:.1f} m ({:.1f}, {:.1f}, {:.1f})",
                                ReadValue<u32>(character, 0x08), ReadValue<s32>(data, 0xF8),
                                ReadValue<s32>(data, 0xFC), ReadValue<u8>(character, 0x1E3),
                                distance, ReadValue<float>(physics, 0x1E0),
                                ReadValue<float>(physics, 0x1E4),
                                ReadValue<float>(physics, 0x1E8)));
            }
        });
    }
    std::ranges::sort(close_by);
    for (size_t index = 0; index < close_by.size() && index < 6; ++index) {
        enemies += " [" + close_by[index].second + "]";
    }
    const auto self = FindAutoplayCharacter(AutoplaySelf);
    // Raw, as all of the rig's checks (see AutoplayInWorld).
    LOG_INFO(Debug,
             "Bloodborne autoplay snapshot: map {:#x} at ({:.1f}, {:.1f}, {:.1f}) heading {:.2f} "
             "hp {}/{} session {} members on map {} in party session {} nearby:{}",
             GetCurrentPackedMap(), at ? (*at)[0] : 0.0f, at ? (*at)[1] : 0.0f,
             at ? (*at)[2] : 0.0f, LocalPlayerHeading().value_or(0.0f),
             self ? ReadValue<s32>(self->data, 0xF8) : 0,
             self ? ReadValue<s32>(self->data, 0xFC) : 0, AutoplaySessionMembers(),
             AutoplayMembersOnMyMap(), InPartySession(), enemies.empty() ? " none" : enemies);
}

// census [<meters>]: every character that close (default 60 m), nearest first, with its HP, death
// flags (+0x1E3), story hide (+0x28) and explicit disable ([+0x18]+0x20 bit 0): which world
// shows or killed what (the clinic's beast, Iosefka).
// A character's backread level ([+0x18] +0x10 now, +0x14 wanted; 4 full, 0 none: the state
// machine 0x018BD320) and its draw flags ([+0x58] +0x114, bit 0x40 cleared by the enable switch),
// for the census: which copy of a character is drawn in one world and not the other.
s32 BackreadLevel(u64 character, u64 offset) {
    const u64 control = ReadValue<u64>(character, 0x18);
    return control >= 0x10000 ? ReadValue<s32>(control, offset) : -1;
}

u32 DrawFlags(u64 character) {
    const u64 model = ReadValue<u64>(character, 0x58);
    return model >= 0x10000 && HasMemoryAccess(model, 0x116, MemoryProt::CpuRead)
               ? ReadValue<u16>(model, 0x114)
               : 0xFFFFFFFF;
}

void LogAutoplayCensus(float meters) {
    const auto at = LocalPlayerPosition();
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    if (world < 0x10000 || !at) {
        LOG_INFO(Debug, "Bloodborne autoplay census: no world");
        return;
    }
    std::vector<std::pair<float, std::string>> close_by;
    ForEachWorldCharacter(world, [&](u64 character) {
        if (character == GetLocalPlayer()) {
            return;
        }
        const u64 modules = ReadValue<u64>(character, 0x3B0);
        const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (data < 0x10000 || physics < 0x10000) {
            return;
        }
        const float x = ReadValue<float>(physics, 0x1E0);
        const float y = ReadValue<float>(physics, 0x1E4);
        const float z = ReadValue<float>(physics, 0x1E8);
        const float dx = x - (*at)[0];
        const float dy = y - (*at)[1];
        const float dz = z - (*at)[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance < meters) {
            close_by.emplace_back(
                distance,
                fmt::format("{:#x} hp {}/{} dead {:#x} hidden {} disabled {} at {:.1f} m "
                            "({:.1f}, {:.1f}, {:.1f}) backread {}/{} draw {:#x}",
                            ReadValue<u32>(character, 0x08), ReadValue<s32>(data, 0xF8),
                            ReadValue<s32>(data, 0xFC), ReadValue<u8>(character, 0x1E3),
                            ReadValue<u8>(character, 0x28), IsCharacterDisabled(character) ? 1 : 0,
                            distance, x, y, z, BackreadLevel(character, 0x10),
                            BackreadLevel(character, 0x14), DrawFlags(character)));
        }
    });
    std::ranges::sort(close_by);
    // Raw, as all of the rig's checks (see AutoplayInWorld).
    LOG_INFO(Debug,
             "Bloodborne autoplay census: map {:#x}, {} characters within {:.0f} m of "
             "({:.1f}, {:.1f}, {:.1f})",
             GetCurrentPackedMap(), close_by.size(), meters, (*at)[0], (*at)[1], (*at)[2]);
    for (const auto& [distance, line] : close_by) {
        LOG_INFO(Debug, "Bloodborne autoplay census: {}", line);
    }
}

std::string AutoplayText(const AutoplayCommand& command) {
    std::string text = command.verb;
    for (const std::string& arg : command.args) {
        text += ' ' + arg;
    }
    return text;
}

// Runs one script command for this tick: true once it is done, `failure` set if it failed.
bool RunAutoplayStep(const AutoplayCommand& command, bool begun, double elapsed,
                     std::string& failure) {
    const auto arg = [&](size_t index) -> std::string_view {
        return index < command.args.size() ? std::string_view{command.args[index]}
                                           : std::string_view{};
    };
    const auto number = [&](size_t index, double fallback) {
        const std::string_view text = arg(index);
        double value = fallback;
        if (!text.empty()) {
            std::from_chars(text.data(), text.data() + text.size(), value);
        }
        return value;
    };
    const auto waited_out = [&](double limit) {
        if (elapsed > limit) {
            failure = fmt::format("timed out after {:.0f} s", elapsed);
        }
        return false;
    };
    const std::string& verb = command.verb;
    if (verb == "wait") {
        return elapsed >= number(0, 1.0);
    }
    if (verb == "wait-session") {
        return AutoplaySessionMembers() >= static_cast<size_t>(number(0, 2)) ||
               waited_out(number(1, 180));
    }
    // wait-solo [<timeout>]: until the session is gone (a member quit or crashed).
    if (verb == "wait-solo") {
        return AutoplaySessionMembers() <= 1 || waited_out(number(0, 180));
    }
    if (verb == "wait-members") {
        return AutoplayMembersOnMyMap() >= static_cast<size_t>(number(0, 2)) ||
               waited_out(number(1, 180));
    }
    if (verb == "wait-map") {
        const auto map = ParseAutoplayTarget(arg(0));
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        return (map && AutoplayInWorld() && GetCurrentPackedMap() == *map) ||
               waited_out(number(1, 180));
    }
    // wait-vital <up|down|dead> [<timeout>]: until this player's party death state is that.
    // wait-member-vital <vital> [<timeout>]: until another member's is, on the vitals board.
    if (verb == "wait-vital" || verb == "wait-member-vital") {
        const auto vital = ParseVital(arg(0));
        if (!vital) {
            failure = "no such vital";
            return false;
        }
        if (verb == "wait-vital") {
            return party_death_run.downed.Current() == *vital || waited_out(number(1, 60));
        }
        const VitalsView view = ReadVitalsBoard();
        return (view.Fresh() && std::ranges::any_of(view.board.members,
                                                    [&](const MemberVitals& member) {
                                                        return member.member != view.member &&
                                                               member.vital == *vital;
                                                    })) ||
               waited_out(number(1, 60));
    }
    // ensure-map <map> <warp point>: travel there unless already on that map (a test's start).
    if (verb == "ensure-map" && !begun) {
        const auto map = ParseAutoplayTarget(arg(0));
        if (!map) {
            failure = "no such map";
            return false;
        }
        // Already there, unless falling through the void (a save made after a bad warp).
        const auto at = LocalPlayerPosition();
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        if (GetCurrentPackedMap() == *map && at && (*at)[1] > -500.0f) {
            return true;
        }
    }
    if (verb == "travel" || verb == "ensure-map") {
        const bool ensure = verb == "ensure-map";
        const s32 point = static_cast<s32>(number(ensure ? 1 : 0, HuntersDreamWarpPoint));
        if (!begun) {
            const u64 global = GetGlobalState();
            if (global < 0x10000) {
                failure = "no game state";
                return false;
            }
            WriteValue(global, 0x1528, point);
            WriteValue(global, 0x1520, u8{0});
            using WarpNextStageBonfire = void PS4_SYSV_ABI (*)(u64 unused, s32 warp_point);
            reinterpret_cast<WarpNextStageBonfire>(image_base + WarpNextStageBonfireOffset)(0,
                                                                                            point);
            // As a guest's own lamp trip in a session (TickGuestLampTrip) does; out of one it is
            // the game's own travel, as a player's at a lamp.
            if (IsPartyGuestRole() && InPartySession()) {
                guest_trip_in_flight = true;
            }
            std::scoped_lock lock{autoplay_mutex};
            autoplay_run.travel_map = PackedMapOfWarpPoint(point);
            autoplay_run.travel_fixed_at.reset();
            return false;
        }
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        const bool arrived = elapsed > 5.0 && AutoplayInWorld() && !PartyTripActive() &&
                             GetCurrentPackedMap() == autoplay_run.travel_map;
        if (arrived && !autoplay_run.travel_fixed_at) {
            // The warp alone leaves the player at (0, 0, 0), above the void: to the lamp, as a
            // guest's trip does (guest_trip_fixup_at). The load put the save's last lamp back as
            // the respawn point, so the lamp travelled to goes in again first.
            if (const u64 global = GetGlobalState(); global >= 0x10000) {
                WriteValue(global, 0x1528, point);
            }
            MoveToRespawnPoint(true);
            std::scoped_lock lock{autoplay_mutex};
            autoplay_run.travel_fixed_at = elapsed;
            return false;
        }
        if (arrived && elapsed > *autoplay_run.travel_fixed_at + 2.0) {
            const auto at = LocalPlayerPosition();
            if (!at || (*at)[1] < -500.0f) {
                failure = "arrived in the void";
                return false;
            }
            return true;
        }
        return waited_out(number(ensure ? 2 : 1, 180));
    }
    if (verb == "hurt" || verb == "kill") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found || !HasMemoryAccess(found->data, 0xFC, MemoryProt::CpuWrite)) {
            failure = "no such character";
            return false;
        }
        const s32 hp = ReadValue<s32>(found->data, 0xF8);
        const s32 target_hp = verb == "kill" ? 0 : std::min(hp, static_cast<s32>(number(1, 1)));
        // A kill goes through the game's own (writing 0 HP leaves a standing corpse); checked
        // against its prologue first.
        static constexpr std::array<u8, 6> KillPrologue{0x55, 0x48, 0x89, 0xE5, 0x41, 0x57};
        if (verb == "kill" && IsCharacter(found->character, "autoplay kill") &&
            std::ranges::equal(
                std::span<const u8>{reinterpret_cast<const u8*>(image_base + KillCharacterOffset),
                                    KillPrologue.size()},
                KillPrologue)) {
            using Kill = void PS4_SYSV_ABI (*)(u64 character);
            reinterpret_cast<Kill>(image_base + KillCharacterOffset)(found->character);
        } else {
            WriteValue(found->data, 0xF8, target_hp);
            // It stands for this player's hits (see own_damage), which the HP board shares.
            std::scoped_lock lock{own_hits_mutex};
            own_damage.Hit(ReadValue<u32>(found->character, 0x08), true, hp - target_hp);
        }
        LOG_INFO(Debug, "Bloodborne autoplay: {} {:#x} hp {} -> {} at {:.1f} m", verb,
                 ReadValue<u32>(found->character, 0x08), hp, target_hp, found->distance);
        return true;
    }
    // mark <target>: the character later commands call "marked" (the nearest one now, say).
    if (verb == "mark") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        autoplay_marked.store(ReadValue<u32>(found->character, 0x08));
        LOG_INFO(Debug, "Bloodborne autoplay: marked {:#x} hp {} at {:.1f} m",
                 autoplay_marked.load(), ReadValue<s32>(found->data, 0xF8), found->distance);
        return true;
    }
    // approach <target> [<dx> [<dz> [<heading>]]]: stand there, beside it, turned to <heading>.
    if (verb == "approach") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        const u64 modules = found ? ReadValue<u64>(found->character, 0x3B0) : 0;
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (physics < 0x10000) {
            failure = "no such character";
            return false;
        }
        TeleportLocalPlayer({ReadValue<float>(physics, 0x1E0) + static_cast<float>(number(1, 1.5)),
                             ReadValue<float>(physics, 0x1E4) + 0.2f,
                             ReadValue<float>(physics, 0x1E8) + static_cast<float>(number(2, 0))},
                            static_cast<float>(number(3, 0)));
        return true;
    }
    // attack <target> [<timeout>]: one HP left, then light attacks from each side in turn until
    // it dies (AttackStanceFor): a real hit, so the kill reward and the death run as in play.
    if (verb == "attack") {
        if (!begun) {
            const auto target = ParseAutoplayTarget(arg(0));
            const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
            if (!found || !HasMemoryAccess(found->data, 0xFC, MemoryProt::CpuWrite)) {
                failure = "no such character";
                return false;
            }
            const s32 hp = ReadValue<s32>(found->data, 0xF8);
            WriteValue(found->data, 0xF8, std::min(hp, s32{1}));
            autoplay_run.attack_handle = ReadValue<u32>(found->character, 0x08);
            autoplay_run.attack_placed.reset();
            LOG_INFO(Debug, "Bloodborne autoplay: attacking {:#x} (hp {} -> 1) at {:.1f} m",
                     autoplay_run.attack_handle, hp, found->distance);
        }
        // By its handle from here on: "nearest" would be the next enemy once this one is dead.
        const auto found = FindAutoplayCharacter(autoplay_run.attack_handle);
        const s32 hp = found ? ReadValue<s32>(found->data, 0xF8) : 0;
        if (hp <= 0) {
            autoplay_buttons.store(0);
            LOG_INFO(Debug, "Bloodborne autoplay: {:#x} {} in attack cycle {}",
                     autoplay_run.attack_handle, found ? "died" : "is gone",
                     autoplay_run.attack_placed.value_or(0));
            return true;
        }
        const u32 cycle = AttackCycleAt(elapsed);
        const u64 modules = ReadValue<u64>(found->character, 0x3B0);
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (autoplay_run.attack_placed != cycle && physics >= 0x10000) {
            if (autoplay_run.attack_placed) {
                LOG_INFO(Debug, "Bloodborne autoplay: attack cycle {} missed (heading {:.2f})",
                         *autoplay_run.attack_placed, LocalPlayerHeading().value_or(0.0f));
            }
            const float target_x = ReadValue<float>(physics, 0x1E0);
            const float target_z = ReadValue<float>(physics, 0x1E8);
            const AttackStance stance = AttackStanceFor(target_x, target_z, cycle);
            TeleportLocalPlayer({stance.x, ReadValue<float>(physics, 0x1E4) + 0.2f, stance.z},
                                HeadingTowards(stance.x, stance.z, target_x, target_z));
            autoplay_run.attack_placed = cycle;
        }
        autoplay_buttons.store(AttackHoldsR1(elapsed) ? AutoplayButton("r1") : 0);
        if (elapsed > number(1, 40)) {
            failure = fmt::format("still alive after {:.0f} s: hp {}", elapsed, hp);
        }
        return false;
    }
    // hit <target> [<timeout>]: light attacks from each side in turn (as attack, without taking
    // its HP down first) until one lands: a real hit that the game sends to the other players.
    if (verb == "hit") {
        if (!begun) {
            const auto target = ParseAutoplayTarget(arg(0));
            const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
            if (!found) {
                failure = "no such character";
                return false;
            }
            autoplay_run.attack_handle = ReadValue<u32>(found->character, 0x08);
            autoplay_run.attack_placed.reset();
            autoplay_run.hit_from_hp = ReadValue<s32>(found->data, 0xF8);
            LOG_INFO(Debug, "Bloodborne autoplay: hitting {:#x} (hp {}) at {:.1f} m",
                     autoplay_run.attack_handle, autoplay_run.hit_from_hp, found->distance);
        }
        const auto found = FindAutoplayCharacter(autoplay_run.attack_handle);
        if (!found) {
            autoplay_buttons.store(0);
            failure = "the target is gone";
            return false;
        }
        const s32 hp = ReadValue<s32>(found->data, 0xF8);
        if (hp < autoplay_run.hit_from_hp) {
            autoplay_buttons.store(0);
            LOG_INFO(Debug, "Bloodborne autoplay: hit {:#x}: hp {} -> {} in attack cycle {}",
                     autoplay_run.attack_handle, autoplay_run.hit_from_hp, hp,
                     autoplay_run.attack_placed.value_or(0));
            return true;
        }
        const u32 cycle = AttackCycleAt(elapsed);
        const u64 modules = ReadValue<u64>(found->character, 0x3B0);
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        if (autoplay_run.attack_placed != cycle && physics >= 0x10000) {
            const float target_x = ReadValue<float>(physics, 0x1E0);
            const float target_z = ReadValue<float>(physics, 0x1E8);
            const AttackStance stance = AttackStanceFor(target_x, target_z, cycle);
            TeleportLocalPlayer({stance.x, ReadValue<float>(physics, 0x1E4) + 0.2f, stance.z},
                                HeadingTowards(stance.x, stance.z, target_x, target_z));
            autoplay_run.attack_placed = cycle;
        }
        autoplay_buttons.store(AttackHoldsR1(elapsed) ? AutoplayButton("r1") : 0);
        if (elapsed > number(1, 30)) {
            autoplay_buttons.store(0);
            failure = fmt::format("no hit landed in {:.0f} s: hp {}", elapsed, hp);
        }
        return false;
    }
    if (verb == "teleport") {
        TeleportLocalPlayer({static_cast<float>(number(0, 0)), static_cast<float>(number(1, 0)),
                             static_cast<float>(number(2, 0))},
                            static_cast<float>(number(3, 0)));
        return true;
    }
    // pickup [<meters>] [<timeout>]: onto the nearest usable map item within reach (30 m), then
    // X for 0.3 s of every 1.5 s until the item leaves the usable list: a real pickup through the
    // item button (the host could not pick up items in the Forbidden Woods, 2026-10-09).
    if (verb == "pickup") {
        const auto position = MapItemSearchPlayerPosition();
        const auto items =
            position ? UsableMapItemsNear(*position) : std::vector<std::pair<float, MapItemSpot>>{};
        if (!begun) {
            if (items.empty() || items.front().first > number(0, 30)) {
                failure = "no usable map item near";
                return false;
            }
            const MapItemSpot& spot = items.front().second;
            autoplay_run.pickup_handle = spot.handle;
            TeleportLocalPlayer({spot.at[0], spot.at[1] + 0.2f, spot.at[2]});
            LOG_INFO(Debug, "Bloodborne autoplay: picking up map item {:#x} {:.1f} m away",
                     spot.handle, items.front().first);
        }
        const bool there = std::ranges::any_of(items, [](const auto& item) {
            return item.second.handle == autoplay_run.pickup_handle;
        });
        if (!there) {
            autoplay_buttons.store(0);
            LOG_INFO(Debug, "Bloodborne autoplay: map item {:#x} picked up after {:.1f} s",
                     autoplay_run.pickup_handle, elapsed);
            return true;
        }
        autoplay_buttons.store(
            elapsed > 1.0 && std::fmod(elapsed, 1.5) < 0.3 ? AutoplayButton("cross") : 0);
        if (elapsed > number(1, 20)) {
            autoplay_buttons.store(0);
            failure = fmt::format(
                "map item {:#x} still there after {:.0f} s (item button found {} not busy {} "
                "pressed {})",
                autoplay_run.pickup_handle, elapsed, item_button_counts[1].load(),
                item_button_counts[2].load(), item_button_counts[3].load());
        }
        return false;
    }
    // rise <metres>: straight up from where the player stands, to fall back down.
    if (verb == "rise") {
        const auto at = LocalPlayerPosition();
        if (!at) {
            failure = "not in the world";
            return false;
        }
        TeleportLocalPlayer({(*at)[0], (*at)[1] + static_cast<float>(number(0, 8)), (*at)[2]},
                            LocalPlayerHeading().value_or(0.0f));
        return true;
    }
    // approach-downed: a metre from the nearest downed party member on this map, where the
    // vitals board has it.
    if (verb == "approach-downed") {
        const VitalsView view = ReadVitalsBoard();
        const auto me = LocalPlayerPosition();
        const auto distance = [&](const LampPosition& at) {
            return std::hypot(at[0] - (*me)[0], at[1] - (*me)[1], at[2] - (*me)[2]);
        };
        std::optional<LampPosition> nearest;
        for (const MemberVitals& member : view.board.members) {
            if (me && view.Fresh() && member.member != view.member && member.vital == Vital::Down &&
                member.map == PlayingMap() &&
                (!nearest || distance(member.at) < distance(*nearest))) {
                nearest = member.at;
            }
        }
        if (!nearest) {
            failure = "nobody down on this map";
            return false;
        }
        TeleportLocalPlayer({(*nearest)[0] + 1.0f, (*nearest)[1] + 0.2f, (*nearest)[2]});
        return true;
    }
    // revive-downed [<timeout>]: strikes the nearest downed party member on this map until nobody
    // is down (how many hits depends on the weapon): every 1.5 s a metre from it, then R1.
    if (verb == "revive-downed") {
        const VitalsView view = ReadVitalsBoard();
        const auto me = LocalPlayerPosition();
        std::optional<LampPosition> downed;
        for (const MemberVitals& member : view.board.members) {
            if (view.Fresh() && member.member != view.member && member.vital == Vital::Down &&
                member.map == PlayingMap()) {
                downed = member.at;
            }
        }
        if (!downed || !me) {
            autoplay_buttons.store(0);
            if (!begun) {
                failure = "nobody down on this map";
                return false;
            }
            LOG_INFO(Debug, "Bloodborne autoplay: nobody down after {:.1f} s of revive hits",
                     elapsed);
            return true;
        }
        const double phase = std::fmod(elapsed, 1.5);
        if (phase < 0.1) {
            TeleportLocalPlayer({(*downed)[0] + 1.0f, (*downed)[1] + 0.2f, (*downed)[2]});
        }
        autoplay_buttons.store(phase >= 0.6 && phase < 0.75 ? AutoplayButton("r1") : 0);
        if (elapsed > number(0, 60)) {
            autoplay_buttons.store(0);
            failure = fmt::format("still down after {:.0f} s of revive hits", elapsed);
        }
        return false;
    }
    if (verb == "press" || verb == "stick") {
        u32 buttons = 0;
        u32 sticks = 0x80808080;
        if (verb == "press") {
            buttons = AutoplayButton(arg(0));
            if (buttons == 0) {
                failure = "no such button";
                return false;
            }
        } else {
            const u32 x = static_cast<u32>(std::clamp(number(0, 128), 0.0, 255.0));
            const u32 y = static_cast<u32>(std::clamp(number(1, 128), 0.0, 255.0));
            sticks = x | (y << 8) | 0x80800000;
        }
        autoplay_buttons.store(buttons);
        autoplay_sticks.store(sticks);
        std::scoped_lock lock{autoplay_mutex};
        autoplay_run.hold_until =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds{static_cast<s64>(number(verb == "press" ? 1 : 2, 150))};
        return false;
    }
    if (verb == "set-flag") {
        using SetFlag = void PS4_SYSV_ABI (*)(u64, s32, bool);
        reinterpret_cast<SetFlag>(image_base + PartyGhostSetEventFlagOffset)(
            0, static_cast<s32>(number(0, 0)), number(1, 1) != 0);
        return true;
    }
    if (verb == "expect-flag" || verb == "wait-flag") {
        const bool on = EventFlagOn(static_cast<s32>(number(0, 0)));
        if (on == (number(1, 1) != 0)) {
            return true;
        }
        if (verb == "expect-flag") {
            failure = fmt::format("flag is {}", on ? 1 : 0);
            return false;
        }
        return waited_out(number(2, 180));
    }
    // godmode [off]: the player's HP back to its max on every party tick (tests that stand
    // around near enemies), until switched off.
    if (verb == "godmode") {
        autoplay_godmode.store(arg(0) != "off");
        return true;
    }
    if (verb == "snapshot") {
        LogAutoplaySnapshot();
        return true;
    }
    if (verb == "census") {
        LogAutoplayCensus(static_cast<float>(number(0, 60)));
        return true;
    }
    // shot <name> [<timeout>]: a picture of the port's next frame (AutoplayShotPath), once its
    // presenter took the request. The emulator has no request file: skipped. A picture not taken
    // in time (10 s) is noted and the script goes on: it shows the HUD, it tests nothing.
    if (verb == "shot") {
        static const char* request = std::getenv("BB_SHOT_TRIGGER");
        if (request == nullptr) {
            LOG_INFO(Debug, "Bloodborne autoplay: shot {} skipped (nothing takes pictures)",
                     arg(0));
            return true;
        }
        if (!begun) {
            const auto path = AutoplayShotPath(request, arg(0));
            if (!path) {
                failure = "no such shot name";
                return false;
            }
            std::error_code error;
            std::filesystem::create_directories(std::filesystem::path{*path}.parent_path(), error);
            // Written whole, then moved into place: the presenter never reads half a path.
            const std::string partial = std::string{request} + ".part";
            std::ofstream{partial, std::ios::binary | std::ios::trunc} << *path;
            std::filesystem::rename(partial, request, error);
            if (error) {
                failure = fmt::format("no shot request: {}", error.message());
                return false;
            }
            LOG_INFO(Debug, "Bloodborne autoplay: shot {} asked for", *path);
        }
        std::error_code error;
        if (!std::filesystem::exists(request, error)) {
            return true;
        }
        if (elapsed > number(1, 10)) {
            LOG_WARNING(Debug, "Bloodborne autoplay: shot {} not taken in {:.0f} s", arg(0),
                        elapsed);
            return true;
        }
        return false;
    }
    if (verb == "log") {
        std::string text;
        for (const std::string& word : command.args) {
            text += (text.empty() ? "" : " ") + word;
        }
        LOG_INFO(Debug, "Bloodborne autoplay log: {}", text);
        return true;
    }
    if (verb == "expect-hp") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        const s32 hp = ReadValue<s32>(found->data, 0xF8);
        const auto holds = CompareAutoplay(hp, arg(1), static_cast<s64>(number(2, 0)));
        if (!holds || !*holds) {
            failure = fmt::format("hp is {}", hp);
            return false;
        }
        return true;
    }
    // dump <target>: a character's first 0x800 bytes, its control block (+0x18) and its module
    // pointers (+0x3B0) with each module's first 0x100 bytes, logged in hex, to diff one character
    // between the host's and a guest's world (Iosefka shown on a guest with the event hide byte
    // set, 2026-10-10).
    if (verb == "dump") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found || !HasMemoryAccess(found->character, 0x400, MemoryProt::CpuRead)) {
            failure = "no such character";
            return false;
        }
        const u32 handle = ReadValue<u32>(found->character, 0x08);
        const auto dump = [&](const char* what, u64 base, u64 size) {
            if (base < 0x10000 || !HasMemoryAccess(base, size, MemoryProt::CpuRead)) {
                return;
            }
            for (u64 offset = 0; offset < size; offset += 0x20) {
                std::string row;
                for (u64 byte = 0; byte < 0x20; ++byte) {
                    row += fmt::format("{:02x}", ReadValue<u8>(base, offset + byte));
                }
                LOG_INFO(Debug, "Bloodborne autoplay dump {:#x} {} +{:03x}: {}", handle, what,
                         offset, row);
            }
        };
        dump("chr", found->character, 0x800);
        dump("ctrl", ReadValue<u64>(found->character, 0x18), 0x100);
        // The model (draw flags at +0x114): how a phantom is drawn.
        dump("model", ReadValue<u64>(found->character, 0x58), 0x400);
        const u64 modules = ReadValue<u64>(found->character, 0x3B0);
        dump("modules", modules, 0x100);
        // Each module's first bytes: what draws the character is in one of them.
        for (u64 index = 0; modules >= 0x10000 && index < 0x20; ++index) {
            dump(fmt::format("module{:02x}", index).c_str(), ReadValue<u64>(modules, index * 8),
                 0x100);
        }
        return true;
    }
    // raise-stat <index> <amount>: one of the player's stats (PlayerGameData +0x40 + 8 * index,
    // what channel 8 sends) raised, as a level up does (see OwnDataWatch).
    if (verb == "raise-stat") {
        const u64 mine = OwnGameData();
        const u64 at = 0x40 + 8 * static_cast<u64>(number(0, 0));
        if (mine < 0x10000 || at > 0x90 || !HasMemoryAccess(mine + at, 4, MemoryProt::CpuWrite)) {
            failure = "no player data";
            return false;
        }
        const s32 before = ReadValue<s32>(mine, at);
        WriteValue(mine, at, static_cast<s32>(before + number(1, 1)));
        LOG_INFO(Debug, "Bloodborne autoplay raise-stat: +{:#x} {} -> {}", at, before,
                 ReadValue<s32>(mine, at));
        return true;
    }
    // peek <target> <offset>, poke <target> <offset> <value> [1] (hex): a character's u32 (or byte)
    // at an offset, logged or written (reverse engineering on the rig: a field's meaning tried in
    // play).
    if (verb == "peek" || verb == "poke") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        u32 offset = 0;
        u32 value = 0;
        const std::string_view offset_text = arg(1);
        const std::string_view value_text = arg(2);
        std::from_chars(offset_text.data(), offset_text.data() + offset_text.size(), offset, 16);
        std::from_chars(value_text.data(), value_text.data() + value_text.size(), value, 16);
        if (!found || offset > 0x2000 ||
            !HasMemoryAccess(found->character + offset, 4, MemoryProt::CpuRead)) {
            failure = "no such character";
            return false;
        }
        const u32 before = ReadValue<u32>(found->character, offset);
        if (verb == "poke" && arg(3) == "1") {
            WriteValue(found->character, offset, static_cast<u8>(value)); // one byte
        } else if (verb == "poke") {
            WriteValue(found->character, offset, value);
        }
        LOG_INFO(Debug, "Bloodborne autoplay {}: {:#x} +{:#x} = {:#x}{}", verb,
                 ReadValue<u32>(found->character, 0x08), offset, before,
                 verb == "poke" ? fmt::format(" -> {:#x}", value) : "");
        return true;
    }
    // expect-hidden <handle> <0|1> [<timeout>]: the character hidden by the story (its event
    // disable byte, +0x28) or not, within the time given (default: now).
    if (verb == "expect-hidden") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        const bool hidden = ReadValue<u8>(found->character, 0x28) != 0;
        if (hidden == (number(1, 1) != 0)) {
            return true;
        }
        if (elapsed >= number(2, 0)) {
            failure = fmt::format("hidden is {}", hidden ? 1 : 0);
        }
        return false;
    }
    // expect-disabled <handle> <0|1> [<timeout>]: the character's enable switch off (2004:5,
    // IsCharacterDisabled) or on, within the time given (default: now).
    if (verb == "expect-disabled") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        const bool disabled = IsCharacterDisabled(found->character);
        if (disabled == (number(1, 1) != 0)) {
            return true;
        }
        if (elapsed >= number(2, 0)) {
            failure = fmt::format("disabled is {}", disabled ? 1 : 0);
        }
        return false;
    }
    // note-max-hp: the player's max HP now, for a later expect-max-hp self == noted.
    if (verb == "note-max-hp") {
        const auto self = FindAutoplayCharacter(AutoplaySelf);
        if (!self) {
            failure = "no player";
            return false;
        }
        autoplay_run.noted_max_hp = ReadValue<s32>(self->data, 0xFC);
        LOG_INFO(Debug, "Bloodborne autoplay: max hp {}", autoplay_run.noted_max_hp);
        return true;
    }
    // expect-max-hp <target> <op> <value|noted>: its max HP (a party guest's is not cut to 70%).
    if (verb == "expect-max-hp") {
        const auto target = ParseAutoplayTarget(arg(0));
        const auto found = target ? FindAutoplayCharacter(*target) : std::nullopt;
        if (!found) {
            failure = "no such character";
            return false;
        }
        const s32 max = ReadValue<s32>(found->data, 0xFC);
        const s64 limit =
            arg(2) == "noted" ? autoplay_run.noted_max_hp : static_cast<s64>(number(2, 0));
        const auto holds = CompareAutoplay(max, arg(1), limit);
        if (!holds || !*holds) {
            failure = fmt::format("max hp is {}", max);
            return false;
        }
        return true;
    }
    // expect-effect <id> <0|1>: whether the player has that SpEffect (9200: the level sync).
    if (verb == "expect-effect") {
        const auto effects = LocalEffects();
        const bool has =
            std::ranges::find(effects, static_cast<s32>(number(0, 0))) != effects.end();
        if (has != (number(1, 1) != 0)) {
            failure = fmt::format("effect {} is {}", arg(0), has ? "on" : "off");
            return false;
        }
        return true;
    }
    if (verb == "expect-session") {
        const size_t members = AutoplaySessionMembers();
        if (members != static_cast<size_t>(number(0, 1))) {
            failure = fmt::format("{} in the session", members);
            return false;
        }
        return true;
    }
    // expect-others-near <n> <meters>: at least n other players stand alive within <meters> of
    // the player (a guest that got up invisible to the host: its copy dead, or left where it died).
    if (verb == "expect-others-near") {
        const auto here = LocalPlayerPosition();
        const double limit = number(1, 10);
        size_t close_by = 0;
        std::string seen;
        for (const OtherPlayer& other : OtherPlayers()) {
            const double distance = here && other.at ? std::hypot((*other.at)[0] - (*here)[0],
                                                                  (*other.at)[1] - (*here)[1],
                                                                  (*other.at)[2] - (*here)[2])
                                                     : -1.0;
            seen += fmt::format(" [type={} hp={} dead={:#x} at {:.1f} m]", other.type, other.hp,
                                other.dead_flags, distance);
            close_by += other.hp > 0 && (other.dead_flags & 0x05) == 0 && distance >= 0.0 &&
                        distance <= limit;
        }
        if (close_by < static_cast<size_t>(number(0, 1))) {
            failure = fmt::format("{} alive within {} m:{}", close_by, limit,
                                  seen.empty() ? " none" : seen);
            return false;
        }
        return true;
    }
    if (verb == "expect-map") {
        const auto map = ParseAutoplayTarget(arg(0));
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        if (!map || GetCurrentPackedMap() != *map) {
            failure = fmt::format("on map {:#x}", GetCurrentPackedMap());
            return false;
        }
        return true;
    }
    if (verb == "done") {
        return true;
    }
    failure = "unknown command";
    return false;
}

} // namespace

namespace Coop {

// From the party tick (the game thread), once the player is in the world.
void TickAutoplay() {
    if (autoplay_godmode.load()) {
        if (const auto self = FindAutoplayCharacter(AutoplaySelf);
            self && ReadValue<s32>(self->data, 0xF8) > 0 &&
            HasMemoryAccess(self->data, 0x100, MemoryProt::CpuWrite)) {
            WriteValue(self->data, 0xF8, ReadValue<s32>(self->data, 0xFC));
        }
    }
    AutoplayCommand command;
    size_t index = 0;
    size_t total = 0;
    bool begun = false;
    double elapsed = 0.0;
    const auto now = std::chrono::steady_clock::now();
    {
        std::scoped_lock lock{autoplay_mutex};
        if (autoplay_run.phase != AutoplayRun::Phase::World) {
            return;
        }
        if (autoplay_run.hold_until) {
            if (now < *autoplay_run.hold_until) {
                return;
            }
            autoplay_run.hold_until.reset();
            autoplay_buttons.store(0);
            autoplay_sticks.store(0x80808080);
            LOG_INFO(Debug, "Bloodborne autoplay: step {}/{} {} ok", autoplay_run.next + 1,
                     autoplay_run.script.size(),
                     AutoplayText(autoplay_run.script[autoplay_run.next]));
            ++autoplay_run.next;
            autoplay_run.step_started = now;
            autoplay_run.step_begun = false;
            return;
        }
        if (autoplay_run.next >= autoplay_run.script.size() ||
            autoplay_run.script[autoplay_run.next].verb == "done") {
            autoplay_run.phase = AutoplayRun::Phase::Done;
            LOG_INFO(Debug, "Bloodborne autoplay: done (ok)");
            return;
        }
        command = autoplay_run.script[autoplay_run.next];
        index = autoplay_run.next;
        total = autoplay_run.script.size();
        begun = autoplay_run.step_begun;
        autoplay_run.step_begun = true;
        elapsed = std::chrono::duration<double>(now - autoplay_run.step_started).count();
    }
    std::string failure;
    const bool finished = RunAutoplayStep(command, begun, elapsed, failure);
    std::scoped_lock lock{autoplay_mutex};
    if (!failure.empty()) {
        LOG_ERROR(Debug, "Bloodborne autoplay: step {}/{} {} FAILED: {}", index + 1, total,
                  AutoplayText(command), failure);
        LOG_INFO(Debug, "Bloodborne autoplay: done (failed)");
        autoplay_run.phase = AutoplayRun::Phase::Done;
        autoplay_buttons.store(0);
        autoplay_sticks.store(0x80808080);
        return;
    }
    if (finished && !autoplay_run.hold_until) {
        LOG_INFO(Debug, "Bloodborne autoplay: step {}/{} {} ok (t={:.1f})", index + 1, total,
                 AutoplayText(command), SteadyMilliseconds() / 1000.0);
        ++autoplay_run.next;
        autoplay_run.step_started = std::chrono::steady_clock::now();
        autoplay_run.step_begun = false;
    }
}

} // namespace Coop

namespace {

// From the pad reader: on the title screen, Cross now and then until the player is in the world.
void TickAutoplayTitle() {
    std::scoped_lock lock{autoplay_mutex};
    if (autoplay_run.phase != AutoplayRun::Phase::Title) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (AutoplayInWorld()) {
        autoplay_run.phase = AutoplayRun::Phase::World;
        autoplay_run.step_started = now;
        autoplay_run.step_begun = false;
        autoplay_buttons.store(0);
        // Raw, as all of the rig's checks (see AutoplayInWorld).
        LOG_INFO(Debug, "Bloodborne autoplay: in the world on map {:#x}", GetCurrentPackedMap());
        return;
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - autoplay_run.started);
    autoplay_buttons.store(TitlePressesCross(static_cast<u64>(ms.count())) ? AutoplayButton("cross")
                                                                           : 0);
}

} // namespace

bool AutoplayPad(std::uint32_t& buttons, std::array<std::uint8_t, 4>& sticks) {
    MakeEnemiesHarder();
    std::call_once(autoplay_loaded, LoadAutoplay);
    if (autoplay_run.phase == AutoplayRun::Phase::Off) {
        return false;
    }
    TickAutoplayTitle();
    buttons = autoplay_buttons.load();
    const u32 packed = autoplay_sticks.load();
    sticks = {static_cast<u8>(packed), static_cast<u8>(packed >> 8), static_cast<u8>(packed >> 16),
              static_cast<u8>(packed >> 24)};
    return true;
}

} // namespace Core::Bloodborne
