// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_temporal_upscaler.h"

#include <vk_mem_alloc.h>

struct FfxVkPortableFrameGenerationContext;

namespace Vulkan {

class Instance;
struct Frame;

/// bbport (Driveclub): FSR 3 frame generation (FidelityFX frame interpolation and optical flow,
/// FSR-Vulkan). At each flip the finished frame and the scene's depth and motion vectors give a
/// frame halfway between the previous flip and this one, which the presenter shows first.
/// Without a HUD-less image the interpolation sees the HUD as part of the scene.
class FrameGenerator {
public:
    explicit FrameGenerator(const Instance& instance);
    ~FrameGenerator();
    FrameGenerator(const FrameGenerator&) = delete;
    FrameGenerator& operator=(const FrameGenerator&) = delete;

    /// Draw thread, after the frame image is final (General layout): records the interpolation
    /// into cmdbuf and leaves it in frame's generated image. False when nothing was generated
    /// (setting off, no scene this frame, the first frame after a pause, or an error).
    bool Record(vk::CommandBuffer cmdbuf, Frame& frame,
                const TemporalUpscaler::FrameGenInputs* inputs);

    /// The frame's generated image, made at its size (draw thread; frames are recreated there).
    static void DestroyGenerated(const Instance& instance, Frame& frame);

private:
    bool EnsureContext(u32 render_w, u32 render_h, u32 display_w, u32 display_h);
    bool EnsureGenerated(Frame& frame);
    void DestroyContext();

    const Instance& instance;
    FfxVkPortableFrameGenerationContext* context = nullptr;
    u32 render_w = 0, render_h = 0, display_w = 0, display_h = 0;
    VmaAllocation color_allocation{};
    vk::Image color_image{}; ///< the frame as RGBA8, the interpolation's source
    VmaAllocation hudless_allocation{};
    vk::Image hudless_image{}; ///< the frame before its HUD, RGBA8 at the frame's size
    // Rendering above the window's size: the depth and motion scaled to it (the interpolation
    // takes no render size above its output).
    VmaAllocation small_depth_allocation{}, small_motion_allocation{};
    vk::Image small_depth{}, small_motion{};
    bool failed = false;
    bool have_previous = false;
    bool had_hudless = false;
    u64 frame_id = 0;
};

} // namespace Vulkan
