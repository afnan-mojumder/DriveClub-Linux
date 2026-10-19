// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_frame_generation.h"

#include <cstdio>
#include <cstdlib>

#include "bbport_settings.h"
#include "ffx_vk_portable.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_presenter.h"

namespace Vulkan {
namespace {

constexpr vk::Format SourceFormat = vk::Format::eR8G8B8A8Unorm;

FfxVkPortableImage Describe(vk::Image image, vk::Format format, u32 width, u32 height,
                            vk::ImageUsageFlags usage, FfxVkPortableResourceState state) {
    FfxVkPortableImage out{};
    out.structSize = sizeof(out);
    out.image = image;
    out.format = static_cast<VkFormat>(format);
    out.extent = {width, height};
    out.mipCount = 1;
    out.arrayLayers = 1;
    out.usage = static_cast<VkImageUsageFlags>(usage);
    out.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    out.state = state;
    return out;
}

FfxVkPortableImage None() {
    FfxVkPortableImage out{};
    out.structSize = sizeof(out);
    return out;
}

void PrintIssues(const char* what, u64 issues) {
    std::printf("Frame generation: %s invalid:", what);
    for (u32 bit = 0; bit < 64; ++bit) {
        if (issues & (1ull << bit)) {
            std::printf(" %s", ffxVkPortableValidationIssueName(1ull << bit));
        }
    }
    std::printf("\n");
}

vk::Image CreateImage(const Instance& instance, u32 w, u32 h, vk::ImageUsageFlags usage,
                      VmaAllocation& allocation, vk::Format format = SourceFormat) {
    const VkImageCreateInfo info = static_cast<VkImageCreateInfo>(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {w, h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = usage,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    const VmaAllocationCreateInfo alloc{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VkImage image{};
    if (vmaCreateImage(instance.GetAllocator(), &info, &alloc, &image, &allocation, nullptr) !=
        VK_SUCCESS) {
        allocation = {};
        return {};
    }
    return vk::Image{image};
}

bool settings_on() {
    const auto& s = BbSettings::Get();
    return s.frame_gen && s.upscaler != BbSettings::UpscalerOff;
}

void Barrier(vk::CommandBuffer cmdbuf, vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
             vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
             vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access) {
    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = from,
        .newLayout = to,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
}

} // namespace

FrameGenerator::FrameGenerator(const Instance& instance_) : instance{instance_} {}

FrameGenerator::~FrameGenerator() {
    DestroyContext();
}

void FrameGenerator::DestroyContext() {
    if (context || color_image) {
        (void)instance.GetDevice().waitIdle();
    }
    if (context) {
        ffxVkPortableFrameGenerationContextDestroy(context);
        context = nullptr;
    }
    if (color_image) {
        vmaDestroyImage(instance.GetAllocator(), VkImage(color_image), color_allocation);
        color_image = vk::Image{};
        color_allocation = {};
    }
    if (hudless_image) {
        vmaDestroyImage(instance.GetAllocator(), VkImage(hudless_image), hudless_allocation);
        hudless_image = vk::Image{};
        hudless_allocation = {};
    }
    if (small_depth) {
        vmaDestroyImage(instance.GetAllocator(), VkImage(small_depth), small_depth_allocation);
        small_depth = vk::Image{};
        small_depth_allocation = {};
    }
    if (small_motion) {
        vmaDestroyImage(instance.GetAllocator(), VkImage(small_motion), small_motion_allocation);
        small_motion = vk::Image{};
        small_motion_allocation = {};
    }
    render_w = render_h = display_w = display_h = 0;
    have_previous = false;
}

void FrameGenerator::DestroyGenerated(const Instance& instance, Frame& frame) {
    if (frame.gen_image) {
        vmaDestroyImage(instance.GetAllocator(), VkImage(frame.gen_image), frame.gen_allocation);
        frame.gen_image = vk::Image{};
        frame.gen_allocation = {};
    }
    frame.gen_width = frame.gen_height = 0;
    frame.has_generated = false;
}

bool FrameGenerator::EnsureContext(u32 rw, u32 rh, u32 dw, u32 dh) {
    if (context && rw == render_w && rh == render_h && dw == display_w && dh == display_h) {
        return true;
    }
    DestroyContext();

    FfxVkPortableDeviceInfo device_info{};
    device_info.structSize = sizeof(device_info);
    device_info.instance = instance.GetInstance();
    device_info.physicalDevice = instance.GetPhysicalDevice();
    device_info.device = instance.GetDevice();
    device_info.getDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
    device_info.queue = instance.GetGraphicsQueue();
    device_info.queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex();
    device_info.shaderFloat16Enabled = instance.IsShaderFloat16Enabled();
    device_info.subgroupSizeControlEnabled = instance.IsSubgroupSizeControlEnabled();
    device_info.synchronization2Enabled = VK_TRUE;
    device_info.shaderStorageImageWriteWithoutFormatEnabled = VK_TRUE;

    FfxVkPortableFrameGenerationCreateInfo create{};
    create.structSize = sizeof(create);
    // Driveclub's depth is reversed and infinite; its motion vectors are unjittered.
    create.flags = FFX_VK_PORTABLE_CONTEXT_DEPTH_INVERTED | FFX_VK_PORTABLE_CONTEXT_DEPTH_INFINITE;
    create.maxRenderSize = {rw, rh};
    create.displaySize = {dw, dh};
    create.interpolationSourceFormat = static_cast<VkFormat>(SourceFormat);
    create.outputFormat = static_cast<VkFormat>(SourceFormat);
    if (const u64 issues = ffxVkPortableValidateFrameGenerationCreateInfo(&create)) {
        PrintIssues("create info", issues);
        return false;
    }
    if (ffxVkPortableFrameGenerationContextCreate(&device_info, &create, &context) !=
        FFX_VK_PORTABLE_OK) {
        std::printf("Frame generation: context creation failed\n");
        context = nullptr;
        return false;
    }
    color_image = CreateImage(instance, dw, dh,
                              vk::ImageUsageFlagBits::eTransferDst |
                                  vk::ImageUsageFlagBits::eTransferSrc |
                                  vk::ImageUsageFlagBits::eSampled |
                                  vk::ImageUsageFlagBits::eStorage,
                              color_allocation);
    hudless_image = CreateImage(instance, dw, dh,
                                vk::ImageUsageFlagBits::eTransferDst |
                                    vk::ImageUsageFlagBits::eTransferSrc |
                                    vk::ImageUsageFlagBits::eSampled |
                                    vk::ImageUsageFlagBits::eStorage,
                                hudless_allocation);
    const vk::ImageUsageFlags small_usage = vk::ImageUsageFlagBits::eTransferDst |
                                            vk::ImageUsageFlagBits::eSampled |
                                            vk::ImageUsageFlagBits::eStorage;
    small_depth = CreateImage(instance, dw, dh, small_usage, small_depth_allocation,
                              vk::Format::eR32Sfloat);
    small_motion = CreateImage(instance, dw, dh, small_usage, small_motion_allocation,
                               vk::Format::eR16G16Sfloat);
    if (!color_image || !hudless_image || !small_depth || !small_motion) {
        std::printf("Frame generation: no memory for its source image\n");
        DestroyContext();
        return false;
    }
    render_w = rw;
    render_h = rh;
    display_w = dw;
    display_h = dh;
    std::printf("Frame generation: FSR 3 frame interpolation, render %ux%u, display %ux%u\n", rw,
                rh, dw, dh);
    return true;
}

bool FrameGenerator::EnsureGenerated(Frame& frame) {
    if (frame.gen_image && frame.gen_width == frame.width && frame.gen_height == frame.height) {
        return true;
    }
    DestroyGenerated(instance, frame);
    frame.gen_image = CreateImage(instance, frame.width, frame.height,
                                  vk::ImageUsageFlagBits::eStorage |
                                      vk::ImageUsageFlagBits::eSampled |
                                      vk::ImageUsageFlagBits::eTransferSrc,
                                  frame.gen_allocation);
    if (!frame.gen_image) {
        return false;
    }
    frame.gen_width = frame.width;
    frame.gen_height = frame.height;
    return true;
}

bool FrameGenerator::Record(vk::CommandBuffer cmdbuf, Frame& frame,
                            const TemporalUpscaler::FrameGenInputs* inputs) {
    frame.has_generated = false;
    // BB_FG_STATS=1: every 300 flips, what frame generation did with them.
    static const bool stats = std::getenv("BB_FG_STATS") != nullptr;
    static u32 flips, no_scene, resets, made;
    if (stats && ++flips == 300) {
        std::printf("Frame generation: of 300 flips %u generated, %u without a scene, %u resets\n",
                    made, no_scene, resets);
        flips = no_scene = resets = made = 0;
    }
    if (stats && settings_on()) {
        no_scene += inputs == nullptr;
        resets += inputs && inputs->reset;
    }
    const auto& settings = BbSettings::Get();
    if (!settings.frame_gen || settings.upscaler == BbSettings::UpscalerOff || failed ||
        frame.is_hdr) {
        have_previous = false;
        return false;
    }
    // Menus and loading screens: no scene, nothing to interpolate (the next scene starts anew).
    if (!inputs) {
        have_previous = false;
        return false;
    }
    // A render size above the window's (supersampling) is fine: the interpolation runs at the
    // window's size with the scene's depth and motion at render size. BB_FG_NO_SUPERSAMPLE=1
    // turns that off.
    static const bool no_supersample = std::getenv("BB_FG_NO_SUPERSAMPLE") != nullptr;
    if (no_supersample && (inputs->width > frame.width || inputs->height > frame.height)) {
        static bool printed;
        if (!printed) {
            printed = true;
            std::printf("Frame generation: off while the render size (%ux%u) is above the "
                        "window's (%ux%u)\n",
                        inputs->width, inputs->height, frame.width, frame.height);
        }
        have_previous = false;
        return false;
    }
    // Image only: flat depth and zero motion at the window's size, so
    // the interpolation finds motion from the pictures alone; nothing else from the game.
    const bool image_only = settings.frame_gen_image_only;
    // Rendering above the window's size: depth and motion are scaled down to it first.
    const bool shrink =
        !image_only && (inputs->width > frame.width || inputs->height > frame.height);
    const u32 rw = shrink || image_only ? frame.width : inputs->width;
    const u32 rh = shrink || image_only ? frame.height : inputs->height;
    if (!EnsureContext(rw, rh, frame.width, frame.height) || !EnsureGenerated(frame)) {
        failed = true;
        return false;
    }
    const u32 w = frame.width, h = frame.height;
    const auto all = vk::PipelineStageFlagBits2::eAllCommands;
    vk::Image depth = inputs->depth, motion = inputs->motion;
    if (shrink) {
        const vk::ImageBlit scale{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .srcOffsets = std::array{vk::Offset3D{0, 0, 0},
                                     vk::Offset3D{s32(inputs->width), s32(inputs->height), 1}},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{s32(rw), s32(rh), 1}},
        };
        for (const auto& [src, dst] : {std::pair{inputs->depth, small_depth},
                                       std::pair{inputs->motion, small_motion}}) {
            Barrier(cmdbuf, src, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, all,
                    vk::AccessFlagBits2::eMemoryWrite, vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferRead);
            Barrier(cmdbuf, dst, vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eTransferDstOptimal, all, vk::AccessFlagBits2::eNone,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            // Nearest: depth and motion are not blended across edges.
            cmdbuf.blitImage(src, vk::ImageLayout::eGeneral, dst,
                             vk::ImageLayout::eTransferDstOptimal, scale, vk::Filter::eNearest);
            Barrier(cmdbuf, dst, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead);
        }
        depth = small_depth;
        motion = small_motion;
    }
    if (image_only) {
        // Reversed infinite depth: 0 is far away everywhere.
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        for (const vk::Image dst : {small_depth, small_motion}) {
            Barrier(cmdbuf, dst, vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eTransferDstOptimal, all, vk::AccessFlagBits2::eNone,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            cmdbuf.clearColorImage(dst, vk::ImageLayout::eTransferDstOptimal,
                                   vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 0.0f}},
                                   range);
            Barrier(cmdbuf, dst, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead);
        }
        depth = small_depth;
        motion = small_motion;
    }
    const auto transfer = vk::PipelineStageFlagBits2::eTransfer;
    const auto compute = vk::PipelineStageFlagBits2::eComputeShader;

    // The finished frame (the swapchain's format) as the interpolation's RGBA8 source.
    Barrier(cmdbuf, frame.image, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal,
            all, vk::AccessFlagBits2::eMemoryWrite, transfer, vk::AccessFlagBits2::eTransferRead);
    Barrier(cmdbuf, color_image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, all, vk::AccessFlagBits2::eNone, transfer,
            vk::AccessFlagBits2::eTransferWrite);
    const vk::ImageBlit blit{
        .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .srcOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{s32(w), s32(h), 1}},
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{s32(w), s32(h), 1}},
    };
    cmdbuf.blitImage(frame.image, vk::ImageLayout::eTransferSrcOptimal, color_image,
                     vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eNearest);
    Barrier(cmdbuf, frame.image, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral,
            transfer, vk::AccessFlagBits2::eTransferRead, all, vk::AccessFlagBits2::eMemoryRead);
    Barrier(cmdbuf, color_image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
            transfer, vk::AccessFlagBits2::eTransferWrite, compute,
            vk::AccessFlagBits2::eShaderRead);
    Barrier(cmdbuf, frame.gen_image, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, all,
            vk::AccessFlagBits2::eNone, compute,
            vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite);
    // The frame before its HUD (the display buffer, kept by the upscaler, Transfer source):
    // the interpolation then leaves the HUD where it is.
    const bool hudless = !image_only && inputs->hudless != vk::Image{};
    if (hudless) {
        Barrier(cmdbuf, hudless_image, vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal, all, vk::AccessFlagBits2::eNone, transfer,
                vk::AccessFlagBits2::eTransferWrite);
        const vk::ImageBlit scale{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .srcOffsets = std::array{vk::Offset3D{0, 0, 0},
                                     vk::Offset3D{s32(inputs->hudless_width),
                                                  s32(inputs->hudless_height), 1}},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstOffsets = std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{s32(w), s32(h), 1}},
        };
        cmdbuf.blitImage(inputs->hudless, vk::ImageLayout::eTransferSrcOptimal, hudless_image,
                         vk::ImageLayout::eTransferDstOptimal, scale, vk::Filter::eLinear);
        Barrier(cmdbuf, hudless_image, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eGeneral, transfer, vk::AccessFlagBits2::eTransferWrite, compute,
                vk::AccessFlagBits2::eShaderRead);
    }

    // The upscaler's reset flag is raised on most frames (its frame start runs again on every
    // HUD draw); the history here restarts only after frames without a scene.
    const bool reset = !have_previous;
    // The interpolation copies its source into its history (transfer source).
    const vk::ImageUsageFlags source_usage = vk::ImageUsageFlagBits::eTransferDst |
                                             vk::ImageUsageFlagBits::eTransferSrc |
                                             vk::ImageUsageFlagBits::eSampled |
                                             vk::ImageUsageFlagBits::eStorage;
    const FfxVkPortableImage source =
        Describe(color_image, SourceFormat, w, h, source_usage,
                 FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    const FfxVkPortableImage hudless_source =
        hudless ? Describe(hudless_image, SourceFormat, w, h, source_usage,
                           FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ)
                : None();
    // Optical flow and the interpolation see the HUD-less frame when there is one.
    const FfxVkPortableImage& interpolation_source = hudless ? hudless_source : source;

    FfxVkPortableFrameGenerationPrepareInfo prepare{};
    prepare.structSize = sizeof(prepare);
    prepare.commandBuffer = cmdbuf;
    prepare.depth = Describe(depth, vk::Format::eR32Sfloat, rw, rh,
                             vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                             FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    prepare.motionVectors =
        Describe(motion, vk::Format::eR16G16Sfloat, rw, rh,
                 vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                 FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    prepare.renderSize = {rw, rh};
    prepare.jitterOffset = {0.0f, 0.0f};
    // Pixels at render size, as the upscaler takes them; scaled with the images.
    prepare.motionVectorScale =
        image_only ? FfxVkPortableFloat2{1.0f, 1.0f}
                   : FfxVkPortableFloat2{float(rw) / float(inputs->width),
                                         float(rh) / float(inputs->height)};
    prepare.frameTimeMilliseconds = inputs->frame_ms;
    prepare.cameraNear = inputs->near_plane;
    prepare.cameraFar = 1.0e6f;
    prepare.cameraVerticalFovRadians = inputs->vertical_fov;
    prepare.viewSpaceToMeters = 1.0f;
    prepare.minLuminance = 0.0f;
    prepare.maxLuminance = 1.0f;
    prepare.transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB;
    prepare.cameraUp = {0.0f, 1.0f, 0.0f};
    prepare.cameraRight = {1.0f, 0.0f, 0.0f};
    prepare.cameraForward = {0.0f, 0.0f, 1.0f};
    prepare.reset = reset ? VK_TRUE : VK_FALSE;
    prepare.frameId = frame_id;

    FfxVkPortableFrameGenerationDispatchInfo dispatch{};
    dispatch.structSize = sizeof(dispatch);
    dispatch.commandBuffer = cmdbuf;
    dispatch.currentColor = source;
    dispatch.hudlessColor = hudless_source;
    dispatch.distortionField = None();
    dispatch.output = Describe(frame.gen_image, SourceFormat, w, h,
                               vk::ImageUsageFlagBits::eStorage |
                                   vk::ImageUsageFlagBits::eSampled |
                                   vk::ImageUsageFlagBits::eTransferSrc,
                               FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.displaySize = {w, h};
    dispatch.interpolationRect = {0, 0, w, h};
    dispatch.frameTimeMilliseconds = inputs->frame_ms;
    dispatch.cameraNear = inputs->near_plane;
    dispatch.cameraFar = 1.0e6f;
    dispatch.cameraVerticalFovRadians = inputs->vertical_fov;
    dispatch.viewSpaceToMeters = 1.0f;
    dispatch.minLuminance = 0.0f;
    dispatch.maxLuminance = 1.0f;
    dispatch.transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB;
    dispatch.reset = reset ? VK_TRUE : VK_FALSE;
    dispatch.frameId = frame_id;

    FfxVkPortableFrameGenerationCreateInfo create{};
    create.structSize = sizeof(create);
    create.flags = FFX_VK_PORTABLE_CONTEXT_DEPTH_INVERTED | FFX_VK_PORTABLE_CONTEXT_DEPTH_INFINITE;
    create.maxRenderSize = {render_w, render_h};
    create.displaySize = {display_w, display_h};
    create.interpolationSourceFormat = static_cast<VkFormat>(SourceFormat);
    create.outputFormat = static_cast<VkFormat>(SourceFormat);
    if (const u64 issues = ffxVkPortableValidateFrameGenerationPrepareInfo(&create, &prepare)) {
        PrintIssues("prepare", issues);
        failed = true;
        return false;
    }
    if (const u64 issues = ffxVkPortableValidateFrameGenerationDispatchInfo(&create, &dispatch)) {
        PrintIssues("dispatch", issues);
        failed = true;
        return false;
    }
    // Switching between frames with and without a HUD-less image changes what the history
    // holds: start it again.
    if (hudless != had_hudless) {
        prepare.reset = dispatch.reset = VK_TRUE;
        had_hudless = hudless;
    }
    if (ffxVkPortableFrameGenerationContextPrepare(context, &prepare, &interpolation_source) !=
            FFX_VK_PORTABLE_OK ||
        ffxVkPortableFrameGenerationContextRecordDispatch(context, &dispatch) !=
            FFX_VK_PORTABLE_OK) {
        std::printf("Frame generation: recording failed; off for this session\n");
        failed = true;
        return false;
    }
    ++frame_id;
    // The presenter blits the generated frame (General) on its own queue submission.
    Barrier(cmdbuf, frame.gen_image, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, all,
            vk::AccessFlagBits2::eMemoryWrite, all, vk::AccessFlagBits2::eMemoryRead);
    // The first frame of a scene only starts the history: nothing between it and the last.
    const bool generated = have_previous;
    have_previous = true;
    frame.has_generated = generated;
    made += generated;
    return generated;
}

} // namespace Vulkan
