// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: HDR output. The presenter composes the frame exactly as for SDR, into `Game()` instead
// of the swapchain image, and draws the overlay (menu, FPS counter, party HUD) into `Overlay()`;
// Render() then writes both into the HDR10 swapchain image (shaders/hdr_output.frag). Everything
// before it (upscalers, frame generation, the rig's pictures) keeps its SDR images.

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

#include <vk_mem_alloc.h>

namespace Vulkan {
class Instance;
}

namespace Vulkan::HostPasses {

class HdrOutputPass {
public:
    struct Settings {
        float paper_white = 200.0f;
        float peak = 1000.0f;
        float highlights = 0.5f;
    };

    /// `sdr_format`: the frame's (and overlay's) format; `hdr_format`: the swapchain's.
    void Create(const Instance& instance, vk::Format sdr_format, vk::Format hdr_format);
    void Destroy();

    /// The two images at the swapchain's size (waits for the GPU before replacing them).
    void EnsureImages(vk::Extent2D extent);

    [[nodiscard]] vk::Image Game() const {
        return game.image;
    }
    [[nodiscard]] vk::Image Overlay() const {
        return overlay.image;
    }
    [[nodiscard]] vk::ImageView OverlayView() const {
        return overlay.view;
    }

    /// Game in ShaderReadOnlyOptimal, Overlay in ShaderReadOnlyOptimal, `target` (the swapchain
    /// image's view) in ColorAttachmentOptimal.
    void Render(vk::CommandBuffer cmdbuf, vk::ImageView target, vk::Extent2D extent,
                const Settings& settings);

private:
    struct Image {
        vk::Image image{};
        vk::ImageView view{};
        VmaAllocation allocation{};
    };
    Image MakeImage(vk::Extent2D extent, vk::ImageUsageFlags usage, const char* name);
    void DestroyImage(Image& image);

    const Instance* instance{};
    vk::Format sdr_format{};
    vk::Extent2D extent{};
    Image game, overlay;
    vk::UniquePipeline pipeline{};
    vk::UniquePipelineLayout pipeline_layout{};
    vk::UniqueDescriptorSetLayout desc_set_layout{};
    vk::UniqueSampler sampler{};
};

} // namespace Vulkan::HostPasses
