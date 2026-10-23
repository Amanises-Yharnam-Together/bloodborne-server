// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace VideoCore {

/// bbport: where VRAM pressure goes on discrete GPUs.
///  - Reserve (default, Supermedo 5f65e2d): textures stay in VRAM; the buffer cache keeps a
///    reserve of VRAM (ArenaReserveBytes) for the game's memory, which only fits in VRAM on
///    NVIDIA. Moving textures to system memory near the limit (System) cost FPS after every
///    death or area load on 4-8 GB cards (10-25 FPS until a restart): they never came back.
///  - System (#187, krolebord's 6 GB device lost 2026-10-17): new textures go to system memory
///    when VRAM is within its reserve of the driver's budget, and VMA counts the driver's usage.
enum class VramPlacement { Reserve, System };

/// BB_VRAM_PLACEMENT (when set and not empty) over bbport.ini's vram_placement: "system" picks
/// System, anything else Reserve.
inline VramPlacement VramPlacementFrom(const char* env, const std::optional<std::string>& ini) {
    const std::string_view value = env && *env ? std::string_view{env}
                                   : ini       ? std::string_view{*ini}
                                               : std::string_view{};
    return value == "system" ? VramPlacement::System : VramPlacement::Reserve;
}

/// This run's placement (BB_VRAM_PLACEMENT, bbport.ini vram_placement), decided once (image.cpp).
VramPlacement ActiveVramPlacement();

/// The VRAM the buffer cache keeps for the game's memory at start (VramPlacement::Reserve):
/// 512 MiB, or an eighth of a smaller heap, in whole blocks; none on integrated GPUs.
constexpr std::uint64_t ArenaReserveBytes(bool discrete, std::uint64_t heap_size,
                                          std::uint64_t block_size) {
    if (!discrete || block_size == 0) {
        return 0;
    }
    const std::uint64_t size = std::min<std::uint64_t>(512ull << 20, heap_size / 8);
    return size / block_size * block_size;
}

/// bbport: one memory heap as the driver reports it (VK_EXT_memory_budget).
struct HeapUse {
    std::uint64_t size = 0;
    bool device_local = false;
    std::uint64_t usage = 0;  ///< all of the process's memory in the heap, as the driver counts it
    std::uint64_t budget = 0; ///< the driver's budget for the process
};

/// What is kept free on a VRAM heap before new images go elsewhere: 512 MiB, or an eighth of the
/// heap on smaller cards.
constexpr std::uint64_t VramReserve(std::uint64_t heap_size) {
    return std::min<std::uint64_t>(512ull << 20, heap_size / 8);
}

/// Discrete GPUs: the memory types outside VRAM when a VRAM heap is within its reserve of the
/// driver's budget, 0 otherwise. krolebord's play test 2026-10-17 (RTX 4050 Laptop, 6 GB): the
/// texture collector evicted hundreds of images every 5 s and the process still held 5.8 GB of a
/// 5.1 GB budget (the buffer cache's backing of the game's memory grows per area and only fits in
/// VRAM on NVIDIA); new images kept going to VRAM, the driver paged, and the GPU stopped ("device
/// lost") after half an hour. Images made near the limit go to system memory instead: slower, but
/// VRAM stays for what can only be there (Supermedo's bbport acfe3a8). Integrated GPUs keep their
/// own rules.
inline std::uint32_t SystemMemoryTypesNearVramLimit(bool discrete, std::span<const HeapUse> heaps,
                                                    std::span<const std::uint32_t> type_heaps) {
    if (!discrete) {
        return 0;
    }
    bool near_limit = false;
    for (const HeapUse& heap : heaps) {
        if (heap.device_local) {
            near_limit |= heap.usage + VramReserve(heap.size) > heap.budget;
        }
    }
    if (!near_limit) {
        return 0;
    }
    std::uint32_t types = 0;
    for (std::uint32_t type = 0; type < type_heaps.size() && type < 32; ++type) {
        const std::uint32_t heap = type_heaps[type];
        if (heap < heaps.size() && !heaps[heap].device_local) {
            types |= 1u << type;
        }
    }
    return types;
}

/// bbport (Supermedo a1235ee): integrated GPUs whose only device-local heap is small. AMD APUs on
/// Windows report the BIOS's frame buffer (UMA, 256-512 MiB) as that heap and give the GPU the
/// rest from shared system memory, the same RAM. Every image preferred the small heap, textures
/// filled it, and a render target that needed it failed later in play (ErrorOutOfDeviceMemory
/// after some minutes, 256 MiB, Ryzen 5600H). With a device-local heap under 1 GiB and a shared
/// one beside it, textures (images never rendered to) prefer the shared heap and the small one
/// stays for render targets. BB_IMAGES_PREFER_HOST=1 or 0 (when set and not empty) forces it.
inline bool TexturesPreferHost(const char* env, std::span<const HeapUse> heaps) {
    if (env && *env) {
        return env[0] == '1';
    }
    std::uint64_t device_local = 0;
    bool shared_heap = false;
    for (const HeapUse& heap : heaps) {
        if (heap.device_local) {
            device_local += heap.size;
        } else {
            shared_heap = true;
        }
    }
    return shared_heap && device_local < (1ull << 30);
}

} // namespace VideoCore
