// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;

/// bbport: GPU breadcrumbs for device losses. Each site writes a sequence number when the GPU
/// starts it (top of pipe) and when it finishes it (bottom of pipe) into host memory with
/// VK_AMD_buffer_marker; after a device loss Report() prints the sites started but not
/// finished, which is where the GPU was. Off without the extension or with BB_GPU_MARKERS=0.
namespace Breadcrumbs {

enum Site : u32 {
    Submit0 = 0, ///< one per scheduler (Submit0 + its index), the whole command buffer
    DcUpscale = 8,
    FrameGen = 9,
    Present = 10,
    HudlessCopy = 11,
    SiteCount = 16,
};

void Init(const Instance& instance);
void Begin(vk::CommandBuffer cmdbuf, u32 site);
void End(vk::CommandBuffer cmdbuf, u32 site);
/// Prints where the GPU was (once); call before reporting a device loss.
void Report();

} // namespace Breadcrumbs
} // namespace Vulkan
