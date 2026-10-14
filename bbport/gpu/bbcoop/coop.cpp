// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the emulator's seamless co-op code (../emulator/src/core/bloodborne_*.cpp) in the
// native port, through the C interface in gpu/bbcoop.h, and the shadPS4 services it uses that
// this port has no emulator for (the eboot's place, param.sfo, shared trophies).

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
#include "bbcoop.h"
#include "bbcoop/bbcoop_internal.h"
#include "bbcoop_revision.h"
#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/singleton.h"
#include "core/bloodborne_crash_dump.h"
#include "core/bloodborne_party.h"
#include "core/bloodborne_re.h"
#include "core/bloodborne_re_internal.h"
#include "core/cpu_patches.h"
#include "core/file_format/psf.h"
#include "core/libraries/np/np_trophy.h"

extern "C" int32_t runtime_trophy_unlock_from_party(int32_t trophy_id);

namespace MemoryPatcher {
EXPORT uintptr_t g_eboot_address = 0;
uint64_t g_eboot_image_size = 0;
std::string g_game_serial;
} // namespace MemoryPatcher

namespace Libraries::Np::NpTrophy {
namespace {
std::atomic<TrophyUnlockObserver> unlock_observer{};
} // namespace

void SetTrophyUnlockObserver(TrophyUnlockObserver observer) {
    unlock_observer.store(observer);
}

int UnlockTrophyFromParty(OrbisNpTrophyId trophy_id) {
    return runtime_trophy_unlock_from_party(trophy_id);
}

void NotifyTrophyUnlocked(OrbisNpTrophyId trophy_id) {
    if (const auto observer = unlock_observer.load(); observer != nullptr) {
        observer(trophy_id);
    }
}
} // namespace Libraries::Np::NpTrophy

extern "C" void bbcoop_note_segment(uint64_t address, uint64_t size, unsigned flags) {
    Core::NoteImageSegment(static_cast<uintptr_t>(address), size, flags);
}

namespace {
std::string game_folder; // app0, as bbcoop_install got it
} // namespace

extern "C" int bbcoop_game_file(const char* relative, char* out, size_t size) {
    if (game_folder.empty()) {
        return 0;
    }
    const auto path = (std::filesystem::path{game_folder} / relative).string();
    if (path.size() + 1 > size) {
        return 0;
    }
    std::memcpy(out, path.c_str(), path.size() + 1);
    return 1;
}

extern "C" void bbcoop_install(unsigned char* image, uint64_t eboot_size, void* code,
                               uint64_t code_size, const char* app0) {
    game_folder = app0 != nullptr ? app0 : "";
    auto* param_sfo = Common::Singleton<PSF>::Instance();
    const auto sfo = std::filesystem::path{app0 != nullptr ? app0 : "."} / "sce_sys" / "param.sfo";
    if (!param_sfo->Open(sfo)) {
        LOG_WARNING(Loader, "Co-op: no param.sfo at {}", sfo.string());
    }
    MemoryPatcher::g_game_serial = std::string{param_sfo->GetString("TITLE_ID").value_or("")};
    MemoryPatcher::g_eboot_address = reinterpret_cast<uintptr_t>(image);
    MemoryPatcher::g_eboot_image_size = eboot_size;
    // The build, as the emulator logs it: the rig checks that every game runs the same one.
    LOG_INFO(Loader, "Run: Revision {}", BB_SCM_REV);
    // What a log needs to compare two players' games, in one line: the build, the launcher's frame
    // rate preset and extra patches (patches.py's names; the patches themselves are compiled
    // into patches.bin), and the map event files the game reads (the kit's co-op ones over the
    // game's). The build and the event files also go to the room's peers (PeerHello).
    const u64 event_files = Core::Bloodborne::EventFilesHash(
        std::filesystem::path{game_folder.empty() ? "." : game_folder} / "dvdroot_ps4" / "event");
    Core::Bloodborne::SetBuildIdentity(BB_SCM_REV, event_files);
    const auto environment = [](const char* name, const char* absent) {
        const char* value = std::getenv(name);
        return std::string{value != nullptr && *value != '\0' ? value : absent};
    };
    char started[32] = "?";
    const std::time_t now = std::time(nullptr);
    if (const std::tm* utc = std::gmtime(&now)) {
        std::strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", utc);
    }
    LOG_INFO(
        Loader, "Co-op: started {} UTC, build {}, FPS preset {}, extra patches {}, event files {}",
        started, BB_SCM_REV, environment("BB_FPS_PRESET", "?"), environment("BB_PATCHES", "none"),
        event_files != 0 ? fmt::format("{:016x}", event_files) : std::string{"unread"});
    if (code != nullptr) {
        Core::RegisterPatchModule(image, eboot_size, code, code_size);
    }
#ifdef _WIN32
    // The loader's own last-chance handler stays: it writes the co-op crash report itself
    // (bbcoop_crash_report) before it ends the process.
    const auto loader_filter = SetUnhandledExceptionFilter(nullptr);
    Core::Bloodborne::InstallCrashCatcher(reinterpret_cast<u64>(image), eboot_size);
    SetUnhandledExceptionFilter(loader_filter);
#else
    Core::Bloodborne::InstallCrashCatcher(reinterpret_cast<u64>(image), eboot_size);
#endif
    Core::Bloodborne::InstallSeamlessCoopPatches();
    Core::Bloodborne::InstallReverseEngineeringTrace();
}

extern "C" void bbcoop_crash_report(void* exception_pointers) {
    Core::Bloodborne::WriteCrashReport(exception_pointers);
}

extern "C" void bbcoop_window_closing(void) {
    Core::Bloodborne::ReportFreezeOnClose();
}

extern "C" int bbcoop_autoplay_pad(uint32_t* buttons, uint8_t sticks[4]) {
    std::uint32_t held = 0;
    std::array<std::uint8_t, 4> moved{128, 128, 128, 128};
    if (!Core::Bloodborne::AutoplayPad(held, moved)) {
        return 0;
    }
    *buttons = held;
    std::memcpy(sticks, moved.data(), moved.size());
    return 1;
}

extern "C" void bbcoop_party_death_pad(uint32_t* buttons, uint8_t sticks[4], uint8_t* l2,
                                       uint8_t* r2) {
    std::array<std::uint8_t, 4> moved{};
    std::memcpy(moved.data(), sticks, moved.size());
    Core::Bloodborne::FilterPartyDeathPad(*buttons, moved, *l2, *r2);
    std::memcpy(sticks, moved.data(), moved.size());
}

static_assert(BBCOOP_VITAL_OFF == static_cast<int>(Core::Bloodborne::Vital::Off) &&
              BBCOOP_VITAL_UP == static_cast<int>(Core::Bloodborne::Vital::Up) &&
              BBCOOP_VITAL_DOWN == static_cast<int>(Core::Bloodborne::Vital::Down) &&
              BBCOOP_VITAL_DEAD == static_cast<int>(Core::Bloodborne::Vital::Dead));

extern "C" int bbcoop_party_panel(bbcoop_panel* panel) {
    *panel = bbcoop_panel{.revive_target = -1};
    Core::Bloodborne::PartyPanel party;
    if (!Core::Bloodborne::ReadPartyPanel(party)) {
        return 0;
    }
    const auto count = std::min<std::size_t>(party.members.size(), BBCOOP_PANEL_MEMBERS);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& member = party.members[index];
        auto& out = panel->members[index];
        std::snprintf(out.name, sizeof(out.name), "%s", member.name.c_str());
        out.vital = static_cast<int>(member.vital);
        out.here = member.here ? 1 : 0;
        out.hp = member.hp;
        out.hp_max = member.max;
        out.landed = member.landed;
        out.needed = member.needed;
    }
    panel->member_count = static_cast<int>(count);
    panel->revive_target = party.revive_target < static_cast<s32>(count) ? party.revive_target : -1;
    panel->vital = static_cast<int>(party.vital);
    panel->landed = party.landed;
    panel->needed = party.needed;
    panel->give_up = party.give_up;
    panel->native_list = Core::Bloodborne::NativePartyListShown() ? 1 : 0;
    panel->native_prompt = Core::Bloodborne::NativePartyPromptShown() ? 1 : 0;
    return 1;
}

extern "C" float bbcoop_revive_share(uint32_t landed, uint32_t needed) {
    return Core::Bloodborne::ReviveShare(landed, needed);
}

extern "C" void bbcoop_portrait_model(const uint8_t* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return;
    }
    Core::Bloodborne::NoteStatusModel(
        std::span<const uint8_t>{rgba, std::size_t(width) * std::size_t(height) * 4}, width, height);
}

extern "C" int bbcoop_portrait_offscreen_wanted(void) {
    return Core::Bloodborne::OffscreenPortraitWanted() ? 1 : 0;
}

extern "C" uint32_t bbcoop_portrait_version(int slot) {
    return Core::Bloodborne::PortraitSlotVersion(slot);
}

extern "C" int bbcoop_portrait_slot(int slot, uint8_t* rgba, size_t size, uint32_t* version) {
    uint32_t seen = 0;
    const bool shown = Core::Bloodborne::ReadPortraitSlot(slot, std::span<uint8_t>{rgba, size}, seen);
    if (version != nullptr) {
        *version = seen;
    }
    return shown ? 1 : 0;
}

namespace {

void CopyName(char* out, size_t size, const std::string& text) {
    const size_t n = std::min(size - 1, text.size());
    std::memcpy(out, text.data(), n);
    out[n] = '\0';
}

int LobbyStateOf(bbcoop::LobbyPlayer::State state) {
    return state == bbcoop::LobbyPlayer::PLAYING ? BBCOOP_LOBBY_PLAYING
           : state == bbcoop::LobbyPlayer::TITLE ? BBCOOP_LOBBY_TITLE
                                                 : BBCOOP_LOBBY_OFFLINE;
}

} // namespace

extern "C" int bbcoop_lobby_view(bbcoop_lobby* lobby) {
    namespace Coop = Core::Bloodborne::Coop;
    *lobby = {};
    if (!Coop::TeamModeOn()) {
        return 0;
    }
    const auto state = Coop::TeamLobbyState();
    if (!state) {
        return 1;
    }
    const std::string me = Coop::TeamLobbyMember();
    CopyName(lobby->me, sizeof(lobby->me), me);
    lobby->signed_in = 1;
    CopyName(lobby->notice, sizeof(lobby->notice), state->notice());
    if (state->has_team()) {
        const bbcoop::Team& team = state->team();
        lobby->in_team = 1;
        lobby->leader = team.leader() == me;
        lobby->started = team.started();
        // The leader first.
        std::vector<const bbcoop::TeamMember*> members;
        for (const bbcoop::TeamMember& m : team.members()) {
            if (m.member() == team.leader()) {
                members.insert(members.begin(), &m);
            } else {
                members.push_back(&m);
            }
        }
        for (const bbcoop::TeamMember* m : members) {
            if (lobby->member_count == BBCOOP_LOBBY_TEAM) {
                break;
            }
            bbcoop_lobby_entry& out = lobby->members[lobby->member_count++];
            CopyName(out.name, sizeof(out.name), m->name());
            CopyName(out.id, sizeof(out.id), m->member());
            out.state = LobbyStateOf(m->state());
            out.ready = m->ready();
        }
    }
    for (const bbcoop::LobbyPlayer& player : state->players()) {
        const bool mine = state->has_team() && player.team() == state->team().id();
        if (mine || lobby->player_count == BBCOOP_LOBBY_PLAYERS) {
            continue;
        }
        bbcoop_lobby_entry& out = lobby->players[lobby->player_count++];
        CopyName(out.name, sizeof(out.name), player.name());
        CopyName(out.id, sizeof(out.id), player.member());
        out.state = LobbyStateOf(player.state());
    }
    for (const bbcoop::TeamInvite& invite : state->invites()) {
        if (lobby->invite_count == BBCOOP_LOBBY_INVITES) {
            break;
        }
        bbcoop_lobby_entry& out = lobby->invites[lobby->invite_count++];
        CopyName(out.name, sizeof(out.name), invite.from_name());
        CopyName(out.id, sizeof(out.id), invite.team());
    }
    return 1;
}

static_assert(BBCOOP_TEAM_GO_READY == int(Core::Bloodborne::TeamGate::Choice::Ready) &&
              BBCOOP_TEAM_GO_UNREADY == int(Core::Bloodborne::TeamGate::Choice::Unready) &&
              BBCOOP_TEAM_GO_START == int(Core::Bloodborne::TeamGate::Choice::Start) &&
              BBCOOP_TEAM_GO_ALONE == int(Core::Bloodborne::TeamGate::Choice::Alone));

extern "C" int bbcoop_team_screen(void) {
    return Core::Bloodborne::Coop::TeamScreenUp() ? 1 : 0;
}

extern "C" void bbcoop_team_choose(int what) {
    Core::Bloodborne::Coop::TeamChoose(static_cast<Core::Bloodborne::TeamGate::Choice>(what));
}

extern "C" void bbcoop_lobby_request(int action, const char* id) {
    Core::Bloodborne::Coop::TeamLobbyRequest(static_cast<bbcoop::TeamRequest::Action>(action),
                                             id ? id : "");
}

extern "C" void bbcoop_file_opened(const char* guest_path) {
    Core::Bloodborne::NoteSaveFileOpened(guest_path);
}

extern "C" void bbcoop_trophy_unlocked(int32_t trophy_id) {
    Libraries::Np::NpTrophy::NotifyTrophyUnlocked(trophy_id);
}
