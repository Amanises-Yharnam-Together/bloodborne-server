// SPDX-License-Identifier: GPL-2.0-or-later
// Vertex stream formats the renderer must take without stopping the game
// (video_core/renderer_vulkan/liverpool_to_vk.h). A player's RX 580 (AMD's Windows driver, no
// VK_EXT_vertex_input_dynamic_state, 2026-10-23) stopped at "Begin the hunt" on
// "Unknown data_format=15 and num_format=7": without dynamic vertex input the pipeline key takes
// the formats of the V#s as they are in guest memory, and 15 is a value the hardware reserves.
#include <cstdio>
#include "video_core/renderer_vulkan/liverpool_to_vk.h"

static int failures;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

int main() {
    using namespace AmdGpu;
    using namespace Vulkan::LiverpoolToVK;
    const auto reserved = static_cast<DataFormat>(15);

    // The pair from the player's log names no format: the draw is skipped, not the game stopped.
    CHECK(!IsKnownSurfaceFormat(reserved, NumberFormat::Float));
    CHECK(!IsKnownSurfaceFormat(static_cast<DataFormat>(63), NumberFormat::Uint));

    // Known stream formats are unchanged.
    CHECK(IsKnownSurfaceFormat(DataFormat::Format32_32_32, NumberFormat::Float));
    CHECK(IsKnownSurfaceFormat(DataFormat::Format8_8_8_8, NumberFormat::Unorm));
    // An invalid (unused) V# keeps meaning "no format", as SurfaceFormat always allowed.
    CHECK(IsKnownSurfaceFormat(DataFormat::FormatInvalid, NumberFormat::Float));

    // A stream the pipeline is built with must have a format the driver takes: an unknown one
    // falls back to a format every driver supports for vertex buffers.
    CHECK(VertexInputFormat(reserved, NumberFormat::Float) == vk::Format::eR32G32B32A32Sfloat);
    CHECK(VertexInputFormat(DataFormat::Format32_32_32, NumberFormat::Float) ==
          vk::Format::eR32G32B32Sfloat);
    CHECK(VertexInputFormat(DataFormat::Format16_16, NumberFormat::Float) ==
          vk::Format::eR16G16Sfloat);
    CHECK(VertexInputFormat(DataFormat::FormatInvalid, NumberFormat::Float) ==
          vk::Format::eUndefined);

    if (failures != 0) {
        std::fprintf(stderr, "vertex formats: %d check(s) failed\n", failures);
        return 1;
    }
    std::puts("vertex formats: ok");
    return 0;
}
