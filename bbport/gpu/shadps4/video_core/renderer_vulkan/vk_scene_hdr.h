// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: HDR highlights from the game's 16-bit scene (bbport.ini hdr_scene). With HDR output
// the presenter encodes the finished SDR frame (host_passes/hdr_pass.h); this class gives it,
// per frame, how much brighter than the SDR picture each pixel is in the scene the game rendered
// before its tonemap clipped it. The game's look (exposure, grading, UI) stays the SDR one.
//
//   * at the post-processing combine (TemporalUpscaler::OnDispatch, GPU command thread): a copy
//     of the RGBA16F scene color (CaptureScene);
//   * at the first UI draw: a copy of the finished scene without the UI (CaptureHudless);
//   * in the presenter (PrepareFrame, same thread, in command order): Prepare() takes this
//     frame's captures, Record() runs shaders/hdr_exposure.comp (the tonemap's mid-tone ratio)
//     and shaders/hdr_gain.comp into the frame's gain image (RG16F, the display buffer's size);
//   * at present: HdrOutputPass multiplies the frame's linear color by the gain.
// The gain is the highlight expansion of the UI-free scene (host_shaders/hdr_highlight.glsl) or,
// where larger, the 16-bit scene's own brightness over the SDR picture.
//
// Frames without captures (menus, movies, the title screen) have no gain; the presenter then
// stretches the SDR highlights as without this class.

#pragma once

#include <atomic>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

#include <vk_mem_alloc.h>

namespace Vulkan {

class Instance;
class Scheduler;

class SceneHdr {
public:
    SceneHdr(const Instance& instance, Scheduler& scheduler);
    ~SceneHdr();

    /// Presenter, at start: one gain image per presented frame (Frame::id).
    void SetSlots(u32 count) {
        gains.resize(count);
    }

    /// Presenter (swap thread): HDR output with scene highlights is on. Thread safe.
    void SetActive(bool value) noexcept {
        active.store(value && supported, std::memory_order_relaxed);
    }
    [[nodiscard]] bool Active() const noexcept {
        return active.load(std::memory_order_relaxed);
    }

    // ---- Upscaler side: GPU command thread, in command order ---------------------------------

    /// No scene copy for this frame yet.
    [[nodiscard]] bool WantsScene() const noexcept {
        return Active() && !pending.scene;
    }
    /// The RGBA16F scene color right before the game's post processing (`layout`: its layout
    /// when the copy runs).
    void CaptureScene(vk::Image image, vk::ImageLayout layout, vk::Format format, u32 width,
                      u32 height);
    /// A scene copy exists and the UI-free copy is not taken yet.
    [[nodiscard]] bool WantsHudless() const noexcept {
        return pending.scene && !pending.hudless;
    }
    /// The finished SDR scene before any UI pixel (R8G8B8A8, unorm or sRGB).
    void CaptureHudless(vk::Image image, vk::ImageLayout layout, vk::Format format, u32 width,
                        u32 height);

    // ---- Presenter side ----------------------------------------------------------------------

    struct Job {
        bool valid = false;
        u32 slot = 0;
        vk::ImageView scene, hudless; ///< hudless null: no UI was drawn
        vk::ImageView gain;           ///< storage view of the slot's image
        vk::Image gain_image;
        vk::Extent2D size;            ///< the gain's size (the display buffer's)
        bool decode_srgb = false;
        bool clear_exposure = false; ///< the first job: no exposure from earlier frames
    };
    /// Command thread, PrepareFrame: takes this frame's captures. `slot`: the frame's index
    /// (one gain image each), `view_format`: the display buffer's view format, `size`: its size.
    /// Not valid when nothing was captured since the last frame or the feature is off.
    Job Prepare(u32 slot, vk::Format view_format, vk::Extent2D size);
    /// Records the two passes (any recording thread, after the frame's display buffer is in
    /// ShaderReadOnlyOptimal). The gain image ends in TransferSrcOptimal.
    /// `highlights`: bbport.ini hdr_highlights (0..1).
    void Record(vk::CommandBuffer cmdbuf, const Job& job, vk::ImageView display, float headroom,
                float highlights);

    /// Present: slot's gain image (TransferSrcOptimal), when a valid Job wrote it.
    struct Gain {
        vk::Image image;
        u32 width = 0, height = 0;
    };
    [[nodiscard]] Gain GainImage(u32 slot) const;

    /// BB_FRAME_DUMP_TRIGGER: the last copies of the scene (RGBA16F) and the UI-free SDR scene,
    /// in ShaderReadOnlyOptimal once a valid Job's Record ran.
    struct Capture {
        vk::Image image;
        u32 width = 0, height = 0, bytes_per_pixel = 4;
    };
    struct Captures {
        Capture scene, hudless;
    };
    [[nodiscard]] Captures CapturedImages() const;

private:
    struct Pending {
        bool scene = false;
        bool hudless = false;
    };
    struct View {
        vk::Format format;
        vk::ImageView view;
    };
    struct GainSlot {
        VideoCore::UniqueImage image;
        vk::ImageView view;
        u32 width = 0, height = 0;
    };

    void CreatePipelines();
    void Copy(vk::Image src, vk::ImageLayout layout, vk::Image dst, u32 width, u32 height);
    void Ensure(VideoCore::UniqueImage& image, std::vector<View>& views, vk::Format& have_format,
                u32& have_width, u32& have_height, vk::Format format, u32 width, u32 height,
                const char* name);
    vk::ImageView ViewOf(vk::Image image, std::vector<View>& views, vk::Format format);

    const Instance& instance;
    Scheduler& scheduler;
    bool supported = false; ///< RG16F storage images with linear filtering
    std::atomic<bool> active{false};

    Pending pending;
    VideoCore::UniqueImage scene_copy, hudless_copy;
    std::vector<View> scene_views, hudless_views;
    vk::Format scene_format{}, hudless_format{};
    u32 scene_width = 0, scene_height = 0, hudless_width = 0, hudless_height = 0;
    std::vector<GainSlot> gains;

    VkBuffer exposure{};
    VmaAllocation exposure_allocation{};
    bool exposure_cleared = false;

    vk::UniqueSampler sampler;
    vk::UniqueDescriptorSetLayout exposure_layout, gain_layout;
    vk::UniquePipelineLayout exposure_pipeline_layout, gain_pipeline_layout;
    vk::UniquePipeline exposure_pipeline, gain_pipeline;
};

} // namespace Vulkan
