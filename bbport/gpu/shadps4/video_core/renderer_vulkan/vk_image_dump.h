// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: raw image dumps for offline checks (BB_DUMP_DIR, default out/dump): the upscaler's
// (BB_DUMP_TRIGGER) and the screen lighting's (BB_LIGHTING_DUMP) images.
#pragma once

#include <initializer_list>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class Scheduler;

struct DumpImage {
    vk::Image image; ///< in layout General
    u32 width, height, bytes_per_pixel;
    const char* name;
    const char* format;
    vk::ImageAspectFlagBits aspect = vk::ImageAspectFlagBits::eColor;
};

/// Copies `images` into host buffers after the commands recorded so far in `cmdbuf` and writes
/// them to <dir>/fNNN_<name>_<w>x<h>_<format>.raw once the GPU is done.
void DumpImages(const Instance& instance, Scheduler& scheduler, vk::CommandBuffer cmdbuf,
                int frame, std::initializer_list<DumpImage> images);

} // namespace Vulkan
