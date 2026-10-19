// SPDX-License-Identifier: GPL-2.0-or-later
// bbport co-op: the map screen's live 3D view (bbport_map_3d.h).
#include "bbport_map_3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#include <vk_mem_alloc.h>

#include "imgui_impl_vulkan.h"
#include "video_core/host_shaders/map_3d_frag.h"
#include "video_core/host_shaders/map_3d_vert.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace BbMap3d {

namespace {

constexpr vk::Format ColorFormat = vk::Format::eR8G8B8A8Unorm;
constexpr vk::Format DepthFormat = vk::Format::eD32Sfloat;

struct PushConstants {
    float middle[4];
    float turn[4];
    float frame[4];
};

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
};

struct Mesh {
    Buffer positions, colors, indices;
    std::uint32_t index_count = 0;
    float dx = 0, dz = 0;
};

struct Target {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    vk::UniqueImageView view;
};

const Vulkan::Instance* instance = nullptr;
vk::Device device;
VmaAllocator allocator = nullptr;
vk::UniquePipeline pipeline;
vk::UniquePipelineLayout layout;
vk::UniqueSampler sampler;
Target color, depth;
std::uint32_t target_width = 0, target_height = 0;
bool color_was_drawn = false; // the colour image has been in the shader-read layout before
VkDescriptorSet texture = VK_NULL_HANDLE;
std::map<std::uint32_t, Mesh> meshes;
bool asked = false;
Camera camera{};
std::uint32_t width = 0, height = 0;

bool MakeBuffer(Buffer& out, const void* data, std::size_t size, VkBufferUsageFlags usage) {
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    // In the GPU's own memory when the CPU can write it there, else the CPU's.
    const VmaAllocationCreateInfo alloc_ci{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(allocator, &buffer_ci, &alloc_ci, &out.buffer, &out.allocation, &info) != VK_SUCCESS ||
        info.pMappedData == nullptr) {
        out = {};
        return false;
    }
    std::memcpy(info.pMappedData, data, size);
    vmaFlushAllocation(allocator, out.allocation, 0, VK_WHOLE_SIZE);
    return true;
}

void FreeBuffer(Buffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

void FreeTarget(Target& target) {
    target.view.reset();
    if (target.image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, target.image, target.allocation);
    }
    target.image = VK_NULL_HANDLE;
    target.allocation = nullptr;
}

bool MakeTarget(Target& target, vk::Format format, VkImageUsageFlags usage, vk::ImageAspectFlags aspect,
                std::uint32_t w, std::uint32_t h) {
    const VkImageCreateInfo image_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = static_cast<VkFormat>(format),
        .extent = {w, h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo alloc_ci{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(allocator, &image_ci, &alloc_ci, &target.image, &target.allocation, nullptr) != VK_SUCCESS) {
        target.image = VK_NULL_HANDLE;
        return false;
    }
    auto view = device.createImageViewUnique(vk::ImageViewCreateInfo{
        .image = target.image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {aspect, 0, 1, 0, 1},
    });
    if (view.result != vk::Result::eSuccess) {
        FreeTarget(target);
        return false;
    }
    target.view = std::move(view.value);
    return true;
}

/// The pictures big enough for `w` x `h` (made again, waiting for the GPU, only when they grow).
bool EnsureTargets(std::uint32_t w, std::uint32_t h) {
    if (w <= target_width && h <= target_height && color.image != VK_NULL_HANDLE) {
        return true;
    }
    (void)device.waitIdle();
    if (texture != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(texture);
        texture = VK_NULL_HANDLE;
    }
    FreeTarget(color);
    FreeTarget(depth);
    target_width = std::max(w, target_width);
    target_height = std::max(h, target_height);
    color_was_drawn = false;
    if (!MakeTarget(color, ColorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    vk::ImageAspectFlagBits::eColor, target_width, target_height) ||
        !MakeTarget(depth, DepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, vk::ImageAspectFlagBits::eDepth,
                    target_width, target_height)) {
        FreeTarget(color);
        FreeTarget(depth);
        target_width = target_height = 0;
        std::printf("Map 3D: no room for the %ux%u picture\n", w, h);
        return false;
    }
    texture = ImGui_ImplVulkan_AddTexture(*color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return texture != VK_NULL_HANDLE;
}

} // namespace

namespace {
bool made_once = false; // the pipeline was tried (only once the live view is first used)
} // namespace

/// The pass's pipeline and sampler, made the first time the live view is used.
static bool MakePipeline() {
    if (made_once) {
        return pipeline.get() != VK_NULL_HANDLE;
    }
    made_once = true;

    const vk::PushConstantRange push{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    auto made_layout = device.createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo{
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    });
    if (made_layout.result != vk::Result::eSuccess) {
        return false;
    }
    layout = std::move(made_layout.value);
    const auto vs = Vulkan::CompileSPV(MAP_3D_VERT, device);
    const auto fs = Vulkan::CompileSPV(MAP_3D_FRAG, device);
    if (!vs || !fs) {
        std::printf("Map 3D: shaders did not compile\n");
        return false;
    }
    const std::array stages{
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex, .module = vs, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment, .module = fs, .pName = "main"},
    };
    const std::array bindings{
        vk::VertexInputBindingDescription{.binding = 0, .stride = 12, .inputRate = vk::VertexInputRate::eVertex},
        vk::VertexInputBindingDescription{.binding = 1, .stride = 4, .inputRate = vk::VertexInputRate::eVertex},
    };
    const std::array attributes{
        vk::VertexInputAttributeDescription{.location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat},
        vk::VertexInputAttributeDescription{.location = 1, .binding = 1, .format = vk::Format::eR8G8B8A8Unorm},
    };
    const vk::PipelineVertexInputStateCreateInfo vertex_input{
        .vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size()),
        .pVertexBindingDescriptions = bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    const vk::PipelineInputAssemblyStateCreateInfo assembly{.topology = vk::PrimitiveTopology::eTriangleList};
    const vk::Viewport viewport{.width = 1.0f, .height = 1.0f, .maxDepth = 1.0f};
    const vk::Rect2D scissor{.extent = {1, 1}};
    const vk::PipelineViewportStateCreateInfo viewport_state{
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    const vk::PipelineRasterizationStateCreateInfo raster{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone, // the game's models are seen from both sides
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples = vk::SampleCountFlagBits::e1};
    const vk::PipelineDepthStencilStateCreateInfo depth_state{
        .depthTestEnable = true,
        .depthWriteEnable = true,
        .depthCompareOp = vk::CompareOp::eLess,
    };
    const vk::PipelineColorBlendAttachmentState blend{
        .blendEnable = false,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo blend_state{.attachmentCount = 1, .pAttachments = &blend};
    const std::array dynamic{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic_state{
        .dynamicStateCount = static_cast<std::uint32_t>(dynamic.size()), .pDynamicStates = dynamic.data()};
    const vk::PipelineRenderingCreateInfo rendering{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &ColorFormat,
        .depthAttachmentFormat = DepthFormat,
    };
    auto made = device.createGraphicsPipelineUnique({}, vk::GraphicsPipelineCreateInfo{
                                                            .pNext = &rendering,
                                                            .stageCount = static_cast<std::uint32_t>(stages.size()),
                                                            .pStages = stages.data(),
                                                            .pVertexInputState = &vertex_input,
                                                            .pInputAssemblyState = &assembly,
                                                            .pViewportState = &viewport_state,
                                                            .pRasterizationState = &raster,
                                                            .pMultisampleState = &multisample,
                                                            .pDepthStencilState = &depth_state,
                                                            .pColorBlendState = &blend_state,
                                                            .pDynamicState = &dynamic_state,
                                                            .layout = *layout,
                                                        });
    device.destroyShaderModule(vs);
    device.destroyShaderModule(fs);
    if (made.result != vk::Result::eSuccess) {
        std::printf("Map 3D: no pipeline\n");
        return false;
    }
    pipeline = std::move(made.value);
    auto made_sampler = device.createSamplerUnique(vk::SamplerCreateInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
    });
    if (made_sampler.result != vk::Result::eSuccess) {
        pipeline.reset();
        return false;
    }
    sampler = std::move(made_sampler.value);
    std::printf("Map 3D: live view ready\n");
    return true;
}

bool Init(const Vulkan::Instance& vulkan) {
    instance = &vulkan;
    device = vulkan.GetDevice();
    allocator = vulkan.GetAllocator();
    return true;
}

void Upload(std::uint32_t map, const std::shared_ptr<const BbMapArt::AreaMesh>& mesh, float dx, float dz) {
    if (instance == nullptr || !MakePipeline() || !mesh || mesh->indices.empty() || meshes.contains(map)) {
        return;
    }
    Mesh made;
    made.dx = dx;
    made.dz = dz;
    made.index_count = static_cast<std::uint32_t>(mesh->indices.size());
    if (!MakeBuffer(made.positions, mesh->positions.data(), mesh->positions.size() * 4,
                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !MakeBuffer(made.colors, mesh->colors.data(), mesh->colors.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !MakeBuffer(made.indices, mesh->indices.data(), mesh->indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        FreeBuffer(made.positions);
        FreeBuffer(made.colors);
        FreeBuffer(made.indices);
        std::printf("Map 3D: no room for %s's mesh\n", BbMapArt::MapNameOf(map).c_str());
        return;
    }
    meshes[map] = made;
    std::printf("Map 3D: %s on the GPU (%u triangles)\n", BbMapArt::MapNameOf(map).c_str(), made.index_count / 3);
}

bool Has(std::uint32_t map) {
    return meshes.contains(map);
}

void Clear() {
    if (meshes.empty()) {
        return;
    }
    (void)device.waitIdle();
    for (auto& [map, mesh] : meshes) {
        FreeBuffer(mesh.positions);
        FreeBuffer(mesh.colors);
        FreeBuffer(mesh.indices);
    }
    meshes.clear();
}

ImTextureRef Request(const Camera& wanted, ImVec2& uv1) {
    camera = wanted;
    width = static_cast<std::uint32_t>(std::max(1.0f, std::round(wanted.x1 - wanted.x0)));
    height = static_cast<std::uint32_t>(std::max(1.0f, std::round(wanted.y1 - wanted.y0)));
    if (!pipeline || meshes.empty() || !EnsureTargets(width, height)) {
        asked = false;
        return ImTextureRef();
    }
    asked = true;
    uv1 = ImVec2(float(width) / float(target_width), float(height) / float(target_height));
    return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture)));
}

void Draw(vk::CommandBuffer cmdbuf) {
    if (!asked || !pipeline || color.image == VK_NULL_HANDLE) {
        return;
    }
    asked = false;
    const vk::ImageSubresourceRange color_range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    const vk::ImageSubresourceRange depth_range{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
    const std::array before{
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .oldLayout = color_was_drawn ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .image = color.image,
            .subresourceRange = color_range,
        },
        vk::ImageMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eLateFragmentTests,
            .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests,
            .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
                             vk::AccessFlagBits2::eDepthStencilAttachmentRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .image = depth.image,
            .subresourceRange = depth_range,
        },
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = static_cast<std::uint32_t>(before.size()),
        .pImageMemoryBarriers = before.data(),
    });
    const vk::RenderingAttachmentInfo color_attachment{
        .imageView = *color.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{.color = vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 0.0f}}},
    };
    const vk::RenderingAttachmentInfo depth_attachment{
        .imageView = *depth.view,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = vk::ClearValue{.depthStencil = vk::ClearDepthStencilValue{1.0f, 0}},
    };
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, {width, height}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
        .pDepthAttachment = &depth_attachment,
    });
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
    cmdbuf.setViewport(0, vk::Viewport{.width = float(width), .height = float(height), .maxDepth = 1.0f});
    cmdbuf.setScissor(0, vk::Rect2D{{0, 0}, {width, height}});
    const float c = std::cos(camera.yaw), s = std::sin(camera.yaw);
    for (const auto& [map, mesh] : meshes) {
        const PushConstants push{
            {camera.cx, camera.cy, camera.cz, camera.zoom},
            {c, s, std::sin(camera.pitch), std::cos(camera.pitch)},
            {float(width) * 0.5f, float(height) * 0.5f, mesh.dx, mesh.dz},
        };
        cmdbuf.pushConstants(*layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                             sizeof(push), &push);
        const std::array<vk::Buffer, 2> vertex_buffers{vk::Buffer(mesh.positions.buffer), vk::Buffer(mesh.colors.buffer)};
        const std::array<vk::DeviceSize, 2> offsets{0, 0};
        cmdbuf.bindVertexBuffers(0, vertex_buffers, offsets);
        cmdbuf.bindIndexBuffer(vk::Buffer(mesh.indices.buffer), 0, vk::IndexType::eUint32);
        cmdbuf.drawIndexed(mesh.index_count, 1, 0, 0, 0);
    }
    cmdbuf.endRendering();
    const vk::ImageMemoryBarrier2 after{
        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .image = color.image,
        .subresourceRange = color_range,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after});
    color_was_drawn = true;
}

} // namespace BbMap3d
