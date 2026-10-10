// SPDX-License-Identifier: GPL-2.0-or-later
// Kaniel's log of 2026-10-11 (RTX 4070 Ti, 4K output): at the first memory pressure report the
// collector evicted 5339 images, none written back, and magenta and white blocks covered the
// scene. A GPU-drawn image whose page the CPU also wrote (MaybeCpuDirty) looked like a plain
// texture to the collector: its contents, which exist only on the GPU (RefreshImage keeps the
// mips whose guest bytes did not change), were dropped and later rebuilt from stale memory.
#include <cassert>
#include "gpu/shadps4/video_core/texture_cache/gc_policy.h"

using VideoCore::GcAction;
using VideoCore::GcActionFor;
using VideoCore::GcImageState;

int main() {
    // Uploaded from guest memory only: rebuilt the same from it.
    assert(GcActionFor({}, false) == GcAction::Evict);
    assert(GcActionFor({}, true) == GcAction::Evict);
    assert(GcActionFor({.cpu_dirty = true, .tiled = true}, true) == GcAction::Evict);

    // GPU-drawn, tiled: cannot be written back, so it stays.
    assert(GcActionFor({.gpu_modified = true, .tiled = true}, true) == GcAction::Keep);
    // The case from the log: also touched by the CPU on its pages. Before the fix it went.
    assert(GcActionFor({.gpu_modified = true, .cpu_dirty = true, .tiled = true}, true) ==
           GcAction::Keep);
    assert(GcActionFor({.gpu_modified = true, .cpu_dirty = true, .tiled = true}, false) ==
           GcAction::Keep);
    // Linear and CPU-touched: a write-back would put old bytes over the CPU's new ones.
    assert(GcActionFor({.gpu_modified = true, .cpu_dirty = true}, true) == GcAction::Keep);

    // Linear, clean: written back under pressure, kept otherwise.
    assert(GcActionFor({.gpu_modified = true}, true) == GcAction::WriteBackAndEvict);
    assert(GcActionFor({.gpu_modified = true}, false) == GcAction::Keep);

    // The GPU wrote the memory through buffers since: the buffer cache holds the data the image
    // is rebuilt from, as RefreshImage does on its next use.
    assert(GcActionFor({.gpu_modified = true, .gpu_dirty = true, .tiled = true}, true) ==
           GcAction::Evict);
    assert(GcActionFor({.gpu_modified = true, .gpu_dirty = true}, true) == GcAction::Evict);
    return 0;
}
