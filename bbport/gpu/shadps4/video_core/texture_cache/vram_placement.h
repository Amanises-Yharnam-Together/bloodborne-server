// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>

namespace VideoCore {

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

} // namespace VideoCore
