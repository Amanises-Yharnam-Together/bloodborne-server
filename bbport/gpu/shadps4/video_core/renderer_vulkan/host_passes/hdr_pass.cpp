// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/host_passes/hdr_pass.h"

#include "common/assert.h"
#include "video_core/host_shaders/fs_tri_vert.h"
#include "video_core/host_shaders/hdr_output_frag.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan::HostPasses {

void HdrOutputPass::Create(const Instance& instance_, vk::Format sdr_format_, vk::Format hdr_format) {
    instance = &instance_;
    sdr_format = sdr_format_;
    const vk::Device device = instance->GetDevice();

    const std::array bindings{
        vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
    };
    desc_set_layout = Check<"create HDR output descriptor set layout">(
        device.createDescriptorSetLayoutUnique({
            .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
            .bindingCount = static_cast<u32>(bindings.size()),
            .pBindings = bindings.data(),
        }));
    const vk::PushConstantRange push_constants{
        .stageFlags = vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(Settings),
    };
    pipeline_layout = Check<"create HDR output pipeline layout">(device.createPipelineLayoutUnique({
        .setLayoutCount = 1U,
        .pSetLayouts = &*desc_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constants,
    }));

    const auto vs_module = CompileSPV(FS_TRI_VERT, device);
    ASSERT(vs_module);
    const auto fs_module = CompileSPV(HDR_OUTPUT_FRAG, device);
    ASSERT(fs_module);
    SetObjectName(device, fs_module, "hdr_output.frag");
    const std::array stages{
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex, .module = vs_module, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment, .module = fs_module, .pName = "main"},
    };
    const vk::PipelineRenderingCreateInfo rendering_ci{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &hdr_format,
    };
    const vk::PipelineVertexInputStateCreateInfo vertex_input{};
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    const vk::PipelineViewportStateCreateInfo viewport_info{.viewportCount = 1, .scissorCount = 1};
    const vk::PipelineRasterizationStateCreateInfo raster_state{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eClockwise,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    const vk::PipelineColorBlendAttachmentState attachment{
        .blendEnable = false,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo color_blending{
        .attachmentCount = 1,
        .pAttachments = &attachment,
    };
    const std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic_info{
        .dynamicStateCount = dynamic_states.size(),
        .pDynamicStates = dynamic_states.data(),
    };
    pipeline = Check<"create HDR output pipeline">(device.createGraphicsPipelineUnique(
        {}, vk::GraphicsPipelineCreateInfo{
                .pNext = &rendering_ci,
                .stageCount = stages.size(),
                .pStages = stages.data(),
                .pVertexInputState = &vertex_input,
                .pInputAssemblyState = &input_assembly,
                .pViewportState = &viewport_info,
                .pRasterizationState = &raster_state,
                .pMultisampleState = &multisampling,
                .pColorBlendState = &color_blending,
                .pDynamicState = &dynamic_info,
                .layout = *pipeline_layout,
            }));
    device.destroyShaderModule(vs_module);
    device.destroyShaderModule(fs_module);

    sampler = Check<"create HDR output sampler">(device.createSamplerUnique({
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
    }));
}

void HdrOutputPass::Destroy() {
    if (!instance) {
        return;
    }
    DestroyImage(game);
    DestroyImage(overlay);
    extent = {};
}

HdrOutputPass::Image HdrOutputPass::MakeImage(vk::Extent2D size, vk::ImageUsageFlags usage,
                                              const char* name) {
    Image out;
    const VkImageCreateInfo image_ci = static_cast<VkImageCreateInfo>(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = sdr_format,
        .extent = {size.width, size.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = usage | vk::ImageUsageFlagBits::eSampled,
    });
    const VmaAllocationCreateInfo alloc_ci{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VkImage image{};
    const VkResult result =
        vmaCreateImage(instance->GetAllocator(), &image_ci, &alloc_ci, &image, &out.allocation, nullptr);
    ASSERT_MSG(result == VK_SUCCESS, "Failed to allocate the HDR output's {} image: {}", name,
               vk::to_string(vk::Result{result}));
    out.image = vk::Image{image};
    out.view = Check<"create HDR output image view">(instance->GetDevice().createImageView({
        .image = out.image,
        .viewType = vk::ImageViewType::e2D,
        .format = sdr_format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        },
    }));
    SetObjectName(instance->GetDevice(), out.image, "HDR output {}", name);
    return out;
}

void HdrOutputPass::DestroyImage(Image& image) {
    if (image.view) {
        instance->GetDevice().destroyImageView(image.view);
    }
    if (image.image) {
        vmaDestroyImage(instance->GetAllocator(), image.image, image.allocation);
    }
    image = {};
}

void HdrOutputPass::EnsureImages(vk::Extent2D size) {
    if (size == extent && game.image) {
        return;
    }
    // Rare (window size or mode changes): the previous frames may still read the old images.
    Check<"wait for the GPU before replacing the HDR output images">(instance->GetDevice().waitIdle());
    Destroy();
    using Usage = vk::ImageUsageFlagBits;
    game = MakeImage(size, Usage::eTransferDst | Usage::eTransferSrc | Usage::eColorAttachment, "game");
    overlay = MakeImage(size, Usage::eTransferDst | Usage::eColorAttachment, "overlay");
    extent = size;
}

void HdrOutputPass::Render(vk::CommandBuffer cmdbuf, vk::ImageView target, vk::Extent2D size,
                           const Settings& settings) {
    const std::array image_infos{
        vk::DescriptorImageInfo{
            .sampler = *sampler,
            .imageView = game.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        },
        vk::DescriptorImageInfo{
            .sampler = *sampler,
            .imageView = overlay.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        },
    };
    const std::array set_writes{
        vk::WriteDescriptorSet{
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &image_infos[0],
        },
        vk::WriteDescriptorSet{
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &image_infos[1],
        },
    };
    const vk::RenderingAttachmentInfo attachment{
        .imageView = target,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    cmdbuf.setViewport(0, vk::Viewport{.width = float(size.width),
                                       .height = float(size.height),
                                       .minDepth = 0.0f,
                                       .maxDepth = 1.0f});
    cmdbuf.setScissor(0, vk::Rect2D{.extent = size});
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, set_writes);
    cmdbuf.pushConstants(*pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(Settings),
                         &settings);
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea{.extent = size},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    });
    cmdbuf.draw(3, 1, 0, 0);
    cmdbuf.endRendering();
}

} // namespace Vulkan::HostPasses
