// SPDX-License-Identifier: GPL-2.0-or-later
// Depth target sizes for a stencil-only depth buffer (video_core/amdgpu/regs_depth.h), from
// shadPS4 #5384: a depth buffer with no Z format but a stencil format is a stencil-only target.
// Before it, GetDepthSliceSize stopped the game on the missing Z format and the stencil size came
// from the depth size.
#include <cstdio>
#include "video_core/amdgpu/regs_depth.h"

static int failures;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

int main() {
    using AmdGpu::DepthBuffer;
    DepthBuffer buffer{};
    buffer.depth_slice.tile_max = 9; // 10 tiles of 64 pixels
    buffer.z_info.num_samples = 1;   // 2 samples

    // Stencil only: no depth bytes, one stencil byte per sample.
    buffer.z_info.format = DepthBuffer::ZFormat::Invalid;
    buffer.stencil_info.format = DepthBuffer::StencilFormat::Stencil8;
    CHECK(buffer.NumBits() == 0);
    CHECK(buffer.GetDepthSliceSize() == 0);
    CHECK(buffer.GetStencilSliceSize() == 10 * 64 * 2);

    // The stencil size no longer follows the depth format.
    buffer.z_info.format = DepthBuffer::ZFormat::Z32Float;
    CHECK(buffer.NumBits() == 32);
    CHECK(buffer.GetDepthSliceSize() == 10 * 64 * 4 * 2);
    CHECK(buffer.GetStencilSliceSize() == 10 * 64 * 2);
    buffer.z_info.format = DepthBuffer::ZFormat::Z16;
    CHECK(buffer.NumBits() == 16);
    CHECK(buffer.GetDepthSliceSize() == 10 * 64 * 2 * 2);

    // Depth only: no stencil bytes.
    buffer.stencil_info.format = DepthBuffer::StencilFormat::Invalid;
    CHECK(buffer.GetStencilSliceSize() == 0);

    if (failures != 0) {
        std::fprintf(stderr, "depth stencil: %d check(s) failed\n", failures);
        return 1;
    }
    std::puts("depth stencil: ok");
    return 0;
}
