// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_scene_hdr.h"

#include <algorithm>
#include <cstdio>

#include "common/assert.h"
#include "video_core/host_shaders/hdr_exposure_comp.h"
#include "video_core/host_shaders/hdr_gain_comp.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

namespace {

constexpr vk::Format GainFormat = vk::Format::eR16G16Sfloat;
constexpr vk::ImageSubresourceRange ColorRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

struct ExposureParams {
    u32 decode_srgb;
};

struct GainParams {
    float headroom;
    float highlights;
    u32 decode_srgb;
    u32 has_hudless;
};

bool IsSrgb(vk::Format format) {
    switch (format) {
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Srgb:
    case vk::Format::eA8B8G8R8SrgbPack32:
        return true;
    default:
        return false;
    }
}

} // namespace

SceneHdr::SceneHdr(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_} {
    using Feature = vk::FormatFeatureFlagBits;
    const auto features =
        instance.GetPhysicalDevice().getFormatProperties(GainFormat).optimalTilingFeatures;
    const auto needed = Feature::eStorageImage | Feature::eBlitSrc | Feature::eBlitDst |
                        Feature::eSampledImageFilterLinear | Feature::eTransferDst;
    supported = (features & needed) == needed;
    if (!supported) {
        std::printf("SceneHdr: no RG16F storage images; HDR highlights stretch the SDR frame\n");
        return;
    }
    const VkBufferCreateInfo buffer_ci = static_cast<VkBufferCreateInfo>(vk::BufferCreateInfo{
        .size = 16,
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    const VmaAllocationCreateInfo alloc_ci{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    const VkResult result = vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &alloc_ci,
                                            &exposure, &exposure_allocation, nullptr);
    ASSERT_MSG(result == VK_SUCCESS, "Failed to allocate the scene HDR exposure buffer");
    CreatePipelines();
}

SceneHdr::~SceneHdr() {
    const vk::Device device = instance.GetDevice();
    for (auto* views : {&scene_views, &hudless_views}) {
        for (const auto& entry : *views) {
            device.destroyImageView(entry.view);
        }
    }
    for (auto& slot : gains) {
        if (slot.view) {
            device.destroyImageView(slot.view);
        }
    }
    if (exposure) {
        vmaDestroyBuffer(instance.GetAllocator(), exposure, exposure_allocation);
    }
}

void SceneHdr::CreatePipelines() {
    const vk::Device device = instance.GetDevice();
    sampler = Check(device.createSamplerUnique({
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
    }));
    const auto make = [&](std::span<const vk::DescriptorType> types, u32 push_size,
                          vk::UniqueDescriptorSetLayout& layout,
                          vk::UniquePipelineLayout& pipeline_layout, const auto& code,
                          const char* name) {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        for (u32 i = 0; i < types.size(); ++i) {
            bindings.push_back({.binding = i,
                                .descriptorType = types[i],
                                .descriptorCount = 1,
                                .stageFlags = vk::ShaderStageFlagBits::eCompute});
        }
        layout = Check(device.createDescriptorSetLayoutUnique({
            .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
            .bindingCount = u32(bindings.size()),
            .pBindings = bindings.data(),
        }));
        const vk::PushConstantRange push{.stageFlags = vk::ShaderStageFlagBits::eCompute,
                                         .offset = 0,
                                         .size = push_size};
        pipeline_layout = Check(device.createPipelineLayoutUnique({
            .setLayoutCount = 1,
            .pSetLayouts = &*layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push,
        }));
        const auto module = CompileSPV(code, device);
        ASSERT(module);
        SetObjectName(device, module, name);
        auto pipeline = Check(device.createComputePipelineUnique(
            {}, vk::ComputePipelineCreateInfo{
                    .stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                              .module = module,
                              .pName = "main"},
                    .layout = *pipeline_layout,
                }));
        device.destroyShaderModule(module);
        return pipeline;
    };
    using Type = vk::DescriptorType;
    const std::array exposure_types{Type::eCombinedImageSampler, Type::eCombinedImageSampler,
                                    Type::eStorageBuffer};
    exposure_pipeline = make(exposure_types, sizeof(ExposureParams), exposure_layout,
                             exposure_pipeline_layout, HDR_EXPOSURE_COMP, "hdr_exposure.comp");
    const std::array gain_types{Type::eCombinedImageSampler, Type::eCombinedImageSampler,
                                Type::eCombinedImageSampler, Type::eStorageBuffer,
                                Type::eStorageImage};
    gain_pipeline = make(gain_types, sizeof(GainParams), gain_layout, gain_pipeline_layout,
                         HDR_GAIN_COMP, "hdr_gain.comp");
}

vk::ImageView SceneHdr::ViewOf(vk::Image image, std::vector<View>& views, vk::Format format) {
    for (const auto& entry : views) {
        if (entry.format == format) {
            return entry.view;
        }
    }
    const vk::ImageView view = Check(instance.GetDevice().createImageView({
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = ColorRange,
    }));
    views.push_back({format, view});
    return view;
}

void SceneHdr::Ensure(VideoCore::UniqueImage& image, std::vector<View>& views,
                      vk::Format& have_format, u32& have_width, u32& have_height,
                      vk::Format format, u32 width, u32 height, const char* name) {
    if (image && have_format == format && have_width == width && have_height == height) {
        return;
    }
    const auto device = instance.GetDevice();
    for (const auto& entry : views) {
        scheduler.DeferOperation([device, view = entry.view] { device.destroyImageView(view); });
    }
    views.clear();
    scheduler.DeferOperation([old = std::move(image)] {});
    image = VideoCore::UniqueImage(device, instance.GetAllocator());
    image.Create(vk::ImageCreateInfo{
        // Read through the display buffer's view format (sRGB or not).
        .flags = vk::ImageCreateFlagBits::eMutableFormat,
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        // Transfer source: BB_FRAME_DUMP_TRIGGER saves the copies (Captures).
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                 vk::ImageUsageFlagBits::eTransferSrc,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    SetObjectName(device, vk::Image(image), "Scene HDR {}", name);
    have_format = format;
    have_width = width;
    have_height = height;
}

void SceneHdr::Copy(vk::Image src, vk::ImageLayout layout, vk::Image dst, u32 width, u32 height) {
    scheduler.Record([src, layout, dst, width, height](vk::CommandBuffer cmd) {
        const vk::MemoryBarrier2 source_ready{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        };
        // The previous frame's gain pass may still read it.
        vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = dst,
            .subresourceRange = ColorRange,
        };
        cmd.pipelineBarrier2({.memoryBarrierCount = 1,
                              .pMemoryBarriers = &source_ready,
                              .imageMemoryBarrierCount = 1,
                              .pImageMemoryBarriers = &barrier});
        const vk::ImageCopy region{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .extent = {width, height, 1},
        };
        cmd.copyImage(src, layout, dst, vk::ImageLayout::eTransferDstOptimal, region);
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    });
}

void SceneHdr::CaptureScene(vk::Image image, vk::ImageLayout layout, vk::Format format, u32 width,
                            u32 height) {
    if (!WantsScene()) {
        return;
    }
    Ensure(scene_copy, scene_views, scene_format, scene_width, scene_height, format, width, height,
           "scene");
    Copy(image, layout, vk::Image(scene_copy), width, height);
    pending.scene = true;
}

void SceneHdr::CaptureHudless(vk::Image image, vk::ImageLayout layout, vk::Format format,
                              u32 width, u32 height) {
    if (!WantsHudless()) {
        return;
    }
    Ensure(hudless_copy, hudless_views, hudless_format, hudless_width, hudless_height, format,
           width, height, "hudless");
    Copy(image, layout, vk::Image(hudless_copy), width, height);
    pending.hudless = true;
}

SceneHdr::Job SceneHdr::Prepare(u32 slot, vk::Format view_format, vk::Extent2D size) {
    const Pending current = std::exchange(pending, Pending{});
    Job job{};
    if (!current.scene || !Active() || size.width == 0 || size.height == 0) {
        return job;
    }
    if (slot >= gains.size()) {
        return job;
    }
    auto& gain = gains[slot];
    const auto device = instance.GetDevice();
    if (!gain.image || gain.width != size.width || gain.height != size.height) {
        if (gain.view) {
            scheduler.DeferOperation([device, view = gain.view] { device.destroyImageView(view); });
        }
        scheduler.DeferOperation([old = std::move(gain.image)] {});
        gain.image = VideoCore::UniqueImage(device, instance.GetAllocator());
        gain.image.Create(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = GainFormat,
            .extent = {size.width, size.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        SetObjectName(device, vk::Image(gain.image), "Scene HDR gain #{}", slot);
        gain.view = Check(device.createImageView({
            .image = vk::Image(gain.image),
            .viewType = vk::ImageViewType::e2D,
            .format = GainFormat,
            .subresourceRange = ColorRange,
        }));
        gain.width = size.width;
        gain.height = size.height;
    }
    job.valid = true;
    job.slot = slot;
    // The scene is read as stored; the SDR images as the display buffer is.
    job.scene = ViewOf(vk::Image(scene_copy), scene_views, scene_format);
    job.hudless = current.hudless ? ViewOf(vk::Image(hudless_copy), hudless_views, view_format)
                                  : vk::ImageView{};
    job.gain = gain.view;
    job.gain_image = vk::Image(gain.image);
    job.size = size;
    job.decode_srgb = !IsSrgb(view_format);
    job.clear_exposure = !std::exchange(exposure_cleared, true);
    return job;
}

void SceneHdr::Record(vk::CommandBuffer cmdbuf, const Job& job, vk::ImageView display,
                      float headroom, float highlights) {
    if (!job.valid) {
        return;
    }
    const auto all = vk::PipelineStageFlagBits2::eAllCommands;
    const auto rw = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    if (job.clear_exposure) {
        cmdbuf.fillBuffer(exposure, 0, VK_WHOLE_SIZE, 0);
    }
    const vk::MemoryBarrier2 before{
        .srcStageMask = all, .srcAccessMask = rw, .dstStageMask = all, .dstAccessMask = rw};
    const vk::ImageMemoryBarrier2 to_general{
        .srcStageMask = all,
        .srcAccessMask = rw,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = job.gain_image,
        .subresourceRange = ColorRange,
    };
    cmdbuf.pipelineBarrier2({.memoryBarrierCount = 1,
                             .pMemoryBarriers = &before,
                             .imageMemoryBarrierCount = 1,
                             .pImageMemoryBarriers = &to_general});

    const vk::ImageView clean = job.hudless ? job.hudless : display;
    const auto sampled = [&](vk::ImageView view) {
        return vk::DescriptorImageInfo{.sampler = *sampler,
                                       .imageView = view,
                                       .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    };
    const std::array images{sampled(job.scene), sampled(clean), sampled(display)};
    const vk::DescriptorBufferInfo buffer{.buffer = exposure, .offset = 0, .range = 16};
    const vk::DescriptorImageInfo storage{.imageView = job.gain,
                                          .imageLayout = vk::ImageLayout::eGeneral};
    const auto write = [](u32 binding, vk::DescriptorType type) {
        return vk::WriteDescriptorSet{.dstBinding = binding, .descriptorCount = 1,
                                      .descriptorType = type};
    };
    using Type = vk::DescriptorType;

    std::array exposure_writes{write(0, Type::eCombinedImageSampler),
                               write(1, Type::eCombinedImageSampler),
                               write(2, Type::eStorageBuffer)};
    exposure_writes[0].pImageInfo = &images[0];
    exposure_writes[1].pImageInfo = &images[1];
    exposure_writes[2].pBufferInfo = &buffer;
    const ExposureParams exposure_params{.decode_srgb = job.decode_srgb ? 1u : 0u};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *exposure_pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *exposure_pipeline_layout, 0,
                                exposure_writes);
    cmdbuf.pushConstants(*exposure_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                         sizeof(exposure_params), &exposure_params);
    cmdbuf.dispatch(1, 1, 1);

    const vk::BufferMemoryBarrier2 exposure_done{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
        .buffer = exposure,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    cmdbuf.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &exposure_done});

    std::array gain_writes{write(0, Type::eCombinedImageSampler),
                           write(1, Type::eCombinedImageSampler),
                           write(2, Type::eCombinedImageSampler), write(3, Type::eStorageBuffer),
                           write(4, Type::eStorageImage)};
    gain_writes[0].pImageInfo = &images[0];
    gain_writes[1].pImageInfo = &images[1];
    gain_writes[2].pImageInfo = &images[2];
    gain_writes[3].pBufferInfo = &buffer;
    gain_writes[4].pImageInfo = &storage;
    const GainParams gain_params{.headroom = std::max(headroom, 1.0f),
                                 .highlights = std::clamp(highlights, 0.0f, 1.0f),
                                 .decode_srgb = job.decode_srgb ? 1u : 0u,
                                 .has_hudless = job.hudless ? 1u : 0u};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *gain_pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *gain_pipeline_layout, 0,
                                gain_writes);
    cmdbuf.pushConstants(*gain_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                         sizeof(gain_params), &gain_params);
    cmdbuf.dispatch((job.size.width + 7) / 8, (job.size.height + 7) / 8, 1);

    const vk::ImageMemoryBarrier2 to_present{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = all,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .image = job.gain_image,
        .subresourceRange = ColorRange,
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_present});
}

SceneHdr::Captures SceneHdr::CapturedImages() const {
    const auto bytes = [](vk::Format format) { return format == vk::Format::eR16G16B16A16Sfloat ? 8u : 4u; };
    return {.scene = {vk::Image(scene_copy), scene_width, scene_height, bytes(scene_format)},
            .hudless = {vk::Image(hudless_copy), hudless_width, hudless_height, bytes(hudless_format)}};
}

SceneHdr::Gain SceneHdr::GainImage(u32 slot) const {
    if (slot >= gains.size() || !gains[slot].image) {
        return {};
    }
    return {vk::Image(gains[slot].image), gains[slot].width, gains[slot].height};
}

} // namespace Vulkan
