// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_breadcrumbs.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <vk_mem_alloc.h>
#include "video_core/renderer_vulkan/vk_instance.h"

namespace Vulkan::Breadcrumbs {
namespace {

// Per site: [0] last started, [1] last finished (sequence numbers, 0 = never).
volatile u32* slots = nullptr;
vk::Buffer buffer{};
std::array<std::atomic<u32>, SiteCount> sequence{};
std::atomic<bool> reported{false};
// DCport: VK_EXT_device_fault, for the faulting address after a loss.
VkDevice fault_device = VK_NULL_HANDLE;
PFN_vkGetDeviceFaultInfoEXT get_fault_info = nullptr;

const char* FaultKind(VkDeviceFaultAddressTypeEXT type) {
    switch (type) {
    case VK_DEVICE_FAULT_ADDRESS_TYPE_NONE_EXT: return "none";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT: return "invalid read";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT: return "invalid write";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT: return "invalid execute";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT: return "shader at (unknown)";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT: return "shader at (invalid)";
    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT: return "shader at (fault)";
    default: return "other";
    }
}

void ReportFault() {
    if (!get_fault_info) {
        return;
    }
    VkDeviceFaultCountsEXT counts{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
    if (get_fault_info(fault_device, &counts, nullptr) != VK_SUCCESS) {
        std::printf("GPU fault: the driver gave no details\n");
        return;
    }
    std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
    std::vector<VkDeviceFaultVendorInfoEXT> vendor(counts.vendorInfoCount);
    counts.vendorBinarySize = 0;
    VkDeviceFaultInfoEXT info{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
                              .pAddressInfos = addresses.empty() ? nullptr : addresses.data(),
                              .pVendorInfos = vendor.empty() ? nullptr : vendor.data()};
    const VkResult result = get_fault_info(fault_device, &counts, &info);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        std::printf("GPU fault: the driver gave no details (%d)\n", int(result));
        return;
    }
    std::printf("GPU fault: %s\n", info.description[0] ? info.description : "(no description)");
    for (u32 i = 0; i < counts.addressInfoCount; ++i) {
        const auto& a = addresses[i];
        std::printf("  %s at GPU address %#llx (+/- %#llx)\n", FaultKind(a.addressType),
                    (unsigned long long)a.reportedAddress,
                    (unsigned long long)a.addressPrecision);
    }
    for (u32 i = 0; i < counts.vendorInfoCount; ++i) {
        std::printf("  vendor: %s (code %#llx, data %#llx)\n", vendor[i].description,
                    (unsigned long long)vendor[i].vendorFaultCode,
                    (unsigned long long)vendor[i].vendorFaultData);
    }
}

const char* SiteName(u32 site) {
    switch (site) {
    case Submit0 + 0: return "submission (scheduler 0)";
    case Submit0 + 1: return "submission (scheduler 1)";
    case Submit0 + 2: return "submission (scheduler 2)";
    case Submit0 + 3: return "submission (scheduler 3)";
    case DcUpscale: return "Driveclub upscaler (motion + FSR + merge)";
    case FrameGen: return "frame generation";
    case Present: return "present blit";
    case HudlessCopy: return "HUD-less copy";
    default: return "other";
    }
}

} // namespace

void Init(const Instance& instance) {
    if (instance.IsDeviceFaultSupported() && !get_fault_info) {
        fault_device = instance.GetDevice();
        get_fault_info = reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>(
            instance.GetDevice().getProcAddr("vkGetDeviceFaultInfoEXT"));
        if (get_fault_info) {
            std::printf("GPU faults: on (VK_EXT_device_fault); a device loss names the address\n");
        }
    }
    const char* env = std::getenv("BB_GPU_MARKERS");
    if ((env && env[0] == '0') || !instance.IsAmdBufferMarkerSupported() || slots) {
        return;
    }
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = SiteCount * 2 * sizeof(u32),
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    // Host memory: still readable after the device is gone.
    const VmaAllocationCreateInfo alloc_ci{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
    VkBuffer raw{};
    VmaAllocation allocation{};
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &alloc_ci, &raw, &allocation,
                        &info) != VK_SUCCESS ||
        !info.pMappedData) {
        std::printf("GPU markers: no host buffer, off\n");
        return;
    }
    buffer = raw;
    slots = static_cast<volatile u32*>(info.pMappedData);
    for (u32 i = 0; i < SiteCount * 2; ++i) {
        slots[i] = 0;
    }
    std::printf("GPU markers: on (VK_AMD_buffer_marker); a device loss reports where the GPU was\n");
}

void Begin(vk::CommandBuffer cmdbuf, u32 site) {
    if (!slots || site >= SiteCount) {
        return;
    }
    const u32 seq = sequence[site].fetch_add(1, std::memory_order_relaxed) + 1;
    cmdbuf.writeBufferMarkerAMD(vk::PipelineStageFlagBits::eTopOfPipe, buffer,
                                site * 2 * sizeof(u32), seq);
}

void End(vk::CommandBuffer cmdbuf, u32 site) {
    if (!slots || site >= SiteCount) {
        return;
    }
    // The sequence Begin used: the same thread records both for a site.
    const u32 seq = sequence[site].load(std::memory_order_relaxed);
    cmdbuf.writeBufferMarkerAMD(vk::PipelineStageFlagBits::eBottomOfPipe, buffer,
                                (site * 2 + 1) * sizeof(u32), seq);
}

void Report() {
    if (reported.exchange(true)) {
        return;
    }
    if (slots) {
        std::printf("GPU markers at the device loss (started / finished / recorded):\n");
        for (u32 site = 0; site < SiteCount; ++site) {
            const u32 recorded = sequence[site].load();
            if (!recorded) {
                continue;
            }
            const u32 started = slots[site * 2], finished = slots[site * 2 + 1];
            std::printf("  %-42s %u / %u / %u%s\n", SiteName(site), started, finished,
                        recorded, started != finished ? "  <- running on the GPU" : "");
        }
    }
    ReportFault();
    std::fflush(stdout);
}

} // namespace Vulkan::Breadcrumbs
