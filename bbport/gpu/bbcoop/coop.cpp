// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the emulator's seamless co-op code (../emulator/src/core/bloodborne_*.cpp) in the
// native port, through the C interface in gpu/bbcoop.h, and the shadPS4 services it uses that
// this port has no emulator for (the eboot's place, param.sfo, shared trophies).

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
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

extern "C" void bbcoop_install(unsigned char* image, uint64_t eboot_size, void* code,
                               uint64_t code_size, const char* app0) {
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
    return 1;
}

extern "C" void bbcoop_file_opened(const char* guest_path) {
    Core::Bloodborne::NoteSaveFileOpened(guest_path);
}

extern "C" void bbcoop_trophy_unlocked(int32_t trophy_id) {
    Libraries::Np::NpTrophy::NotifyTrophyUnlocked(trophy_id);
}
