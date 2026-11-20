//
// Copyright 2025 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// vk_api_perf_counters:
//    Functionality for storing and collecting Vulkan API Performance Counters
//

#ifndef LIBANGLE_RENDERER_VULKAN_API_PERF_COUNTERS_H_
#define LIBANGLE_RENDERER_VULKAN_API_PERF_COUNTERS_H_

#include "common/PackedEnums.h"
#include "common/system_utils.h"

namespace rx
{
namespace vk
{
using VulkanApiGroupPerfCounters = angle::PackedEnumMap<angle::VulkanApiPerfCounterType, uint64_t>;
using VulkanApiPerfCounters =
    angle::PackedEnumMap<angle::VulkanApiPerfCounterGroup, VulkanApiGroupPerfCounters>;

// Returns counters for the current thread.
VulkanApiPerfCounters &GetCurrentThreadVulkanApiPerfCounters();

// Enumeration of all Vulkan API functions that can be wrapped by VK_CALL* macros.
// This provides a centralized, type-safe reference for determining which performance counter
// group each Vulkan API function belongs to.
enum class VulkanApiFunction : uint8_t
{
    vkAcquireNextImageKHR,
    vkAllocateCommandBuffers,
    vkAllocateDescriptorSets,
    vkAllocateMemory,
    vkBeginCommandBuffer,
    vkBindBufferMemory,
    vkBindImageMemory,
    vkBindImageMemory2,
    vkCmdBeginRenderPass,
    vkCmdBeginRenderingKHR,
    vkCmdClearColorImage,
    vkCmdClearDepthStencilImage,
    vkCmdCopyBuffer,
    vkCmdCopyBufferToImage,
    vkCmdCopyImageToBuffer,
    vkCmdEndRenderPass,
    vkCmdEndRenderingKHR,
    vkCmdInsertDebugUtilsLabelEXT,
    vkCmdNextSubpass,
    vkCmdPipelineBarrier,
    vkCmdPipelineBarrier2KHR,
    vkCmdPushConstants,
    vkCmdResetEvent,
    vkCmdResetQueryPool,
    vkCmdSetEvent,
    vkCmdSetRenderingAttachmentLocationsKHR,
    vkCmdSetRenderingInputAttachmentIndicesKHR,
    vkCmdWaitEvents,
    vkCmdWriteTimestamp,
    vkCopyMemoryToImageEXT,
    vkCreateAndroidSurfaceKHR,
    vkCreateBuffer,
    vkCreateBufferView,
    vkCreateCommandPool,
    vkCreateComputePipelines,
    vkCreateDebugUtilsMessengerEXT,
    vkCreateDescriptorPool,
    vkCreateDescriptorSetLayout,
    vkCreateDevice,
    vkCreateDisplayPlaneSurfaceKHR,
    vkCreateEvent,
    vkCreateFence,
    vkCreateFramebuffer,
    vkCreateGraphicsPipelines,
    vkCreateHeadlessSurfaceEXT,
    vkCreateImage,
    vkCreateImagePipeSurfaceFUCHSIA,
    vkCreateImageView,
    vkCreateInstance,
    vkCreateMetalSurfaceEXT,
    vkCreatePipelineCache,
    vkCreatePipelineLayout,
    vkCreateQueryPool,
    vkCreateRenderPass,
    vkCreateRenderPass2KHR,
    vkCreateSampler,
    vkCreateSamplerYcbcrConversion,
    vkCreateSemaphore,
    vkCreateShaderModule,
    vkCreateSwapchainKHR,
    vkCreateWaylandSurfaceKHR,
    vkCreateWin32SurfaceKHR,
    vkCreateXcbSurfaceKHR,
    vkDestroyBuffer,
    vkDestroyBufferView,
    vkDestroyCommandPool,
    vkDestroyDebugUtilsMessengerEXT,
    vkDestroyDescriptorPool,
    vkDestroyDescriptorSetLayout,
    vkDestroyDevice,
    vkDestroyEvent,
    vkDestroyFence,
    vkDestroyFramebuffer,
    vkDestroyImage,
    vkDestroyImageView,
    vkDestroyInstance,
    vkDestroyPipeline,
    vkDestroyPipelineCache,
    vkDestroyPipelineLayout,
    vkDestroyQueryPool,
    vkDestroyRenderPass,
    vkDestroySampler,
    vkDestroySamplerYcbcrConversion,
    vkDestroySemaphore,
    vkDestroyShaderModule,
    vkDestroySurfaceKHR,
    vkDestroySwapchainKHR,
    vkEndCommandBuffer,
    vkEnumerateDeviceExtensionProperties,
    vkEnumerateDeviceLayerProperties,
    vkEnumerateInstanceExtensionProperties,
    vkEnumerateInstanceLayerProperties,
    vkEnumerateInstanceVersion,
    vkEnumeratePhysicalDevices,
    vkFlushMappedMemoryRanges,
    vkFreeCommandBuffers,
    vkFreeDescriptorSets,
    vkFreeMemory,
    vkGetAndroidHardwareBufferPropertiesANDROID,
    vkGetBufferDeviceAddressKHR,
    vkGetBufferMemoryRequirements,
    vkGetDeviceFaultInfoEXT,
    vkGetDeviceProcAddr,
    vkGetDeviceQueue,
    vkGetDeviceQueue2,
    vkGetDisplayModePropertiesKHR,
    vkGetEventStatus,
    vkGetFenceFdKHR,
    vkGetFenceStatus,
    vkGetImageMemoryRequirements,
    vkGetImageMemoryRequirements2,
    vkGetImageSubresourceLayout,
    vkGetImageSubresourceLayout2EXT,
    vkGetInstanceProcAddr,
    vkGetMemoryFdPropertiesKHR,
    vkGetMemoryHostPointerPropertiesEXT,
    vkGetPastPresentationTimingGOOGLE,
    vkGetPhysicalDeviceDisplayPropertiesKHR,
    vkGetPhysicalDeviceExternalFenceProperties,
    vkGetPhysicalDeviceExternalSemaphoreProperties,
    vkGetPhysicalDeviceFeatures,
    vkGetPhysicalDeviceFeatures2,
    vkGetPhysicalDeviceFormatProperties,
    vkGetPhysicalDeviceFormatProperties2,
    vkGetPhysicalDeviceFragmentShadingRatesKHR,
    vkGetPhysicalDeviceImageFormatProperties2,
    vkGetPhysicalDeviceMemoryProperties,
    vkGetPhysicalDeviceMemoryProperties2,
    vkGetPhysicalDeviceProperties2,
    vkGetPhysicalDeviceQueueFamilyProperties,
    vkGetPhysicalDeviceQueueFamilyProperties2,
    vkGetPhysicalDeviceSurfaceCapabilities2KHR,
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR,
    vkGetPhysicalDeviceSurfaceFormats2KHR,
    vkGetPhysicalDeviceSurfaceFormatsKHR,
    vkGetPhysicalDeviceSurfacePresentModesKHR,
    vkGetPhysicalDeviceSurfaceSupportKHR,
    vkGetPhysicalDeviceWaylandPresentationSupportKHR,
    vkGetPipelineCacheData,
    vkGetSwapchainImagesKHR,
    vkImportFenceFdKHR,
    vkImportSemaphoreFdKHR,
    vkImportSemaphoreZirconHandleFUCHSIA,
    vkInvalidateMappedMemoryRanges,
    vkMapMemory,
    vkMergePipelineCaches,
    vkQueuePresentKHR,
    vkQueueSubmit,
    vkQueueWaitIdle,
    vkResetCommandBuffer,
    vkResetCommandPool,
    vkResetEvent,
    vkResetFences,
    vkResetQueryPoolEXT,
    vkSetDebugUtilsObjectNameEXT,
    vkSetEvent,
    vkTransitionImageLayoutEXT,
    vkUnmapMemory,
    vkUpdateDescriptorSets,
    vkWaitForFences,
    // Consider volk as Vulkan API
    volkGetInstanceVersion,
    volkInitializeCustom,
    volkLoadDevice,
    volkLoadInstance,
};

// Returns the performance counter group for a given Vulkan API function.
// This function is constexpr and will be fully evaluated at compile time with optimizations
// enabled, resulting in zero runtime overhead.
constexpr angle::VulkanApiPerfCounterGroup GetPerfCounterGroup(VulkanApiFunction api)
{
    switch (api)
    {
        // VulkanApiPerfCounterGroup::Command
        case VulkanApiFunction::vkAllocateCommandBuffers:
        case VulkanApiFunction::vkBeginCommandBuffer:
        case VulkanApiFunction::vkCmdBeginRenderPass:
        case VulkanApiFunction::vkCmdBeginRenderingKHR:
        case VulkanApiFunction::vkCmdClearColorImage:
        case VulkanApiFunction::vkCmdClearDepthStencilImage:
        case VulkanApiFunction::vkCmdCopyBuffer:
        case VulkanApiFunction::vkCmdCopyBufferToImage:
        case VulkanApiFunction::vkCmdCopyImageToBuffer:
        case VulkanApiFunction::vkCmdEndRenderPass:
        case VulkanApiFunction::vkCmdEndRenderingKHR:
        case VulkanApiFunction::vkCmdInsertDebugUtilsLabelEXT:
        case VulkanApiFunction::vkCmdNextSubpass:
        case VulkanApiFunction::vkCmdPipelineBarrier:
        case VulkanApiFunction::vkCmdPipelineBarrier2KHR:
        case VulkanApiFunction::vkCmdPushConstants:
        case VulkanApiFunction::vkCmdResetEvent:
        case VulkanApiFunction::vkCmdResetQueryPool:
        case VulkanApiFunction::vkCmdSetEvent:
        case VulkanApiFunction::vkCmdSetRenderingAttachmentLocationsKHR:
        case VulkanApiFunction::vkCmdSetRenderingInputAttachmentIndicesKHR:
        case VulkanApiFunction::vkCmdWaitEvents:
        case VulkanApiFunction::vkCmdWriteTimestamp:
        case VulkanApiFunction::vkCreateCommandPool:
        case VulkanApiFunction::vkDestroyCommandPool:
        case VulkanApiFunction::vkEndCommandBuffer:
        case VulkanApiFunction::vkFreeCommandBuffers:
        case VulkanApiFunction::vkResetCommandBuffer:
        case VulkanApiFunction::vkResetCommandPool:
            return angle::VulkanApiPerfCounterGroup::Command;
        // VulkanApiPerfCounterGroup::Submit
        case VulkanApiFunction::vkQueueSubmit:
            return angle::VulkanApiPerfCounterGroup::Submit;
        // VulkanApiPerfCounterGroup::Surface
        case VulkanApiFunction::vkAcquireNextImageKHR:
        case VulkanApiFunction::vkCreateAndroidSurfaceKHR:
        case VulkanApiFunction::vkCreateDisplayPlaneSurfaceKHR:
        case VulkanApiFunction::vkCreateHeadlessSurfaceEXT:
        case VulkanApiFunction::vkCreateImagePipeSurfaceFUCHSIA:
        case VulkanApiFunction::vkCreateMetalSurfaceEXT:
        case VulkanApiFunction::vkCreateSwapchainKHR:
        case VulkanApiFunction::vkCreateWaylandSurfaceKHR:
        case VulkanApiFunction::vkCreateWin32SurfaceKHR:
        case VulkanApiFunction::vkCreateXcbSurfaceKHR:
        case VulkanApiFunction::vkDestroySurfaceKHR:
        case VulkanApiFunction::vkDestroySwapchainKHR:
        case VulkanApiFunction::vkGetDisplayModePropertiesKHR:
        case VulkanApiFunction::vkGetPastPresentationTimingGOOGLE:
        case VulkanApiFunction::vkGetPhysicalDeviceDisplayPropertiesKHR:
        case VulkanApiFunction::vkGetPhysicalDeviceSurfaceCapabilities2KHR:
        case VulkanApiFunction::vkGetPhysicalDeviceSurfaceCapabilitiesKHR:
        case VulkanApiFunction::vkGetPhysicalDeviceSurfaceFormats2KHR:
        case VulkanApiFunction::vkGetPhysicalDeviceSurfaceFormatsKHR:
        case VulkanApiFunction::vkGetPhysicalDeviceSurfacePresentModesKHR:
        case VulkanApiFunction::vkGetPhysicalDeviceSurfaceSupportKHR:
        case VulkanApiFunction::vkGetPhysicalDeviceWaylandPresentationSupportKHR:
        case VulkanApiFunction::vkGetSwapchainImagesKHR:
        case VulkanApiFunction::vkQueuePresentKHR:
            return angle::VulkanApiPerfCounterGroup::Surface;
        // VulkanApiPerfCounterGroup::Wait
        case VulkanApiFunction::vkQueueWaitIdle:
        case VulkanApiFunction::vkWaitForFences:
            return angle::VulkanApiPerfCounterGroup::Wait;
        // VulkanApiPerfCounterGroup::Other
        case VulkanApiFunction::vkAllocateDescriptorSets:
        case VulkanApiFunction::vkAllocateMemory:
        case VulkanApiFunction::vkBindBufferMemory:
        case VulkanApiFunction::vkBindImageMemory:
        case VulkanApiFunction::vkBindImageMemory2:
        case VulkanApiFunction::vkCopyMemoryToImageEXT:
        case VulkanApiFunction::vkCreateBuffer:
        case VulkanApiFunction::vkCreateBufferView:
        case VulkanApiFunction::vkCreateComputePipelines:
        case VulkanApiFunction::vkCreateDebugUtilsMessengerEXT:
        case VulkanApiFunction::vkCreateDescriptorPool:
        case VulkanApiFunction::vkCreateDescriptorSetLayout:
        case VulkanApiFunction::vkCreateDevice:
        case VulkanApiFunction::vkCreateEvent:
        case VulkanApiFunction::vkCreateFence:
        case VulkanApiFunction::vkCreateFramebuffer:
        case VulkanApiFunction::vkCreateGraphicsPipelines:
        case VulkanApiFunction::vkCreateImage:
        case VulkanApiFunction::vkCreateImageView:
        case VulkanApiFunction::vkCreateInstance:
        case VulkanApiFunction::vkCreatePipelineCache:
        case VulkanApiFunction::vkCreatePipelineLayout:
        case VulkanApiFunction::vkCreateQueryPool:
        case VulkanApiFunction::vkCreateRenderPass:
        case VulkanApiFunction::vkCreateRenderPass2KHR:
        case VulkanApiFunction::vkCreateSampler:
        case VulkanApiFunction::vkCreateSamplerYcbcrConversion:
        case VulkanApiFunction::vkCreateSemaphore:
        case VulkanApiFunction::vkCreateShaderModule:
        case VulkanApiFunction::vkDestroyBuffer:
        case VulkanApiFunction::vkDestroyBufferView:
        case VulkanApiFunction::vkDestroyDebugUtilsMessengerEXT:
        case VulkanApiFunction::vkDestroyDescriptorPool:
        case VulkanApiFunction::vkDestroyDescriptorSetLayout:
        case VulkanApiFunction::vkDestroyDevice:
        case VulkanApiFunction::vkDestroyEvent:
        case VulkanApiFunction::vkDestroyFence:
        case VulkanApiFunction::vkDestroyFramebuffer:
        case VulkanApiFunction::vkDestroyImage:
        case VulkanApiFunction::vkDestroyImageView:
        case VulkanApiFunction::vkDestroyInstance:
        case VulkanApiFunction::vkDestroyPipeline:
        case VulkanApiFunction::vkDestroyPipelineCache:
        case VulkanApiFunction::vkDestroyPipelineLayout:
        case VulkanApiFunction::vkDestroyQueryPool:
        case VulkanApiFunction::vkDestroyRenderPass:
        case VulkanApiFunction::vkDestroySampler:
        case VulkanApiFunction::vkDestroySamplerYcbcrConversion:
        case VulkanApiFunction::vkDestroySemaphore:
        case VulkanApiFunction::vkDestroyShaderModule:
        case VulkanApiFunction::vkEnumerateDeviceExtensionProperties:
        case VulkanApiFunction::vkEnumerateDeviceLayerProperties:
        case VulkanApiFunction::vkEnumerateInstanceExtensionProperties:
        case VulkanApiFunction::vkEnumerateInstanceLayerProperties:
        case VulkanApiFunction::vkEnumerateInstanceVersion:
        case VulkanApiFunction::vkEnumeratePhysicalDevices:
        case VulkanApiFunction::vkFlushMappedMemoryRanges:
        case VulkanApiFunction::vkFreeDescriptorSets:
        case VulkanApiFunction::vkFreeMemory:
        case VulkanApiFunction::vkGetAndroidHardwareBufferPropertiesANDROID:
        case VulkanApiFunction::vkGetBufferDeviceAddressKHR:
        case VulkanApiFunction::vkGetBufferMemoryRequirements:
        case VulkanApiFunction::vkGetDeviceFaultInfoEXT:
        case VulkanApiFunction::vkGetDeviceProcAddr:
        case VulkanApiFunction::vkGetDeviceQueue:
        case VulkanApiFunction::vkGetDeviceQueue2:
        case VulkanApiFunction::vkGetEventStatus:
        case VulkanApiFunction::vkGetFenceFdKHR:
        case VulkanApiFunction::vkGetFenceStatus:
        case VulkanApiFunction::vkGetImageMemoryRequirements:
        case VulkanApiFunction::vkGetImageMemoryRequirements2:
        case VulkanApiFunction::vkGetImageSubresourceLayout:
        case VulkanApiFunction::vkGetImageSubresourceLayout2EXT:
        case VulkanApiFunction::vkGetInstanceProcAddr:
        case VulkanApiFunction::vkGetMemoryFdPropertiesKHR:
        case VulkanApiFunction::vkGetMemoryHostPointerPropertiesEXT:
        case VulkanApiFunction::vkGetPhysicalDeviceExternalFenceProperties:
        case VulkanApiFunction::vkGetPhysicalDeviceExternalSemaphoreProperties:
        case VulkanApiFunction::vkGetPhysicalDeviceFeatures:
        case VulkanApiFunction::vkGetPhysicalDeviceFeatures2:
        case VulkanApiFunction::vkGetPhysicalDeviceFormatProperties:
        case VulkanApiFunction::vkGetPhysicalDeviceFormatProperties2:
        case VulkanApiFunction::vkGetPhysicalDeviceFragmentShadingRatesKHR:
        case VulkanApiFunction::vkGetPhysicalDeviceImageFormatProperties2:
        case VulkanApiFunction::vkGetPhysicalDeviceMemoryProperties:
        case VulkanApiFunction::vkGetPhysicalDeviceMemoryProperties2:
        case VulkanApiFunction::vkGetPhysicalDeviceProperties2:
        case VulkanApiFunction::vkGetPhysicalDeviceQueueFamilyProperties:
        case VulkanApiFunction::vkGetPhysicalDeviceQueueFamilyProperties2:
        case VulkanApiFunction::vkGetPipelineCacheData:
        case VulkanApiFunction::vkImportFenceFdKHR:
        case VulkanApiFunction::vkImportSemaphoreFdKHR:
        case VulkanApiFunction::vkImportSemaphoreZirconHandleFUCHSIA:
        case VulkanApiFunction::vkInvalidateMappedMemoryRanges:
        case VulkanApiFunction::vkMapMemory:
        case VulkanApiFunction::vkMergePipelineCaches:
        case VulkanApiFunction::vkResetEvent:
        case VulkanApiFunction::vkResetFences:
        case VulkanApiFunction::vkResetQueryPoolEXT:
        case VulkanApiFunction::vkSetDebugUtilsObjectNameEXT:
        case VulkanApiFunction::vkSetEvent:
        case VulkanApiFunction::vkTransitionImageLayoutEXT:
        case VulkanApiFunction::vkUnmapMemory:
        case VulkanApiFunction::vkUpdateDescriptorSets:
        case VulkanApiFunction::volkGetInstanceVersion:
        case VulkanApiFunction::volkInitializeCustom:
        case VulkanApiFunction::volkLoadDevice:
        case VulkanApiFunction::volkLoadInstance:
            return angle::VulkanApiPerfCounterGroup::Other;
            // No default to trigger "-Wswitch" if not all enumerations are handled.
    }
    // No return to trigger "-Wreturn-type".
}

namespace priv
{
enum class VulkanApiPerfTimerState
{
    Disabled,
    CompilerEnabled,
    RuntimeEnabled,
};

template <VulkanApiPerfTimerState State>
class ScopedVulkanApiPerfTimerImpl;

template <>
class [[nodiscard]] ScopedVulkanApiPerfTimerImpl<VulkanApiPerfTimerState::Disabled> final
    : angle::NonCopyable
{
  public:
    static void TryEnable() {}
    static bool IsEnabled() { return false; }

    ANGLE_INLINE explicit ScopedVulkanApiPerfTimerImpl(angle::VulkanApiPerfCounterGroup group) {}
    ANGLE_INLINE explicit ScopedVulkanApiPerfTimerImpl(VulkanApiFunction api) {}
};

class ScopedVulkanApiPerfTimerBase : angle::NonCopyable
{
  protected:
    ScopedVulkanApiPerfTimerBase()  = default;
    ~ScopedVulkanApiPerfTimerBase() = default;

    ANGLE_INLINE void beginScope(angle::VulkanApiPerfCounterGroup group)
    {
        mGroup     = group;
        mBeginTime = angle::GetCurrentSystemTimeNs();
    }

    ANGLE_INLINE void endScope() const
    {
        const uint64_t diff                       = (angle::GetCurrentSystemTimeNs() - mBeginTime);
        VulkanApiGroupPerfCounters &groupCounters = GetCurrentThreadVulkanApiPerfCounters()[mGroup];
        groupCounters[angle::VulkanApiPerfCounterType::WallTimeNs] += diff;
        ++groupCounters[angle::VulkanApiPerfCounterType::Samples];
    }

  private:
    angle::VulkanApiPerfCounterGroup mGroup;
    uint64_t mBeginTime;
};

template <>
class [[nodiscard]] ScopedVulkanApiPerfTimerImpl<VulkanApiPerfTimerState::CompilerEnabled> final
    : protected ScopedVulkanApiPerfTimerBase
{
  public:
    static void TryEnable() {}
    static bool IsEnabled() { return true; }

    ANGLE_INLINE explicit ScopedVulkanApiPerfTimerImpl(angle::VulkanApiPerfCounterGroup group)
    {
        beginScope(group);
    }
    ANGLE_INLINE explicit ScopedVulkanApiPerfTimerImpl(VulkanApiFunction api)
        : ScopedVulkanApiPerfTimerImpl(GetPerfCounterGroup(api))
    {}
    ANGLE_INLINE ~ScopedVulkanApiPerfTimerImpl() { endScope(); }
};

template <>
class [[nodiscard]] ScopedVulkanApiPerfTimerImpl<VulkanApiPerfTimerState::RuntimeEnabled> final
    : protected ScopedVulkanApiPerfTimerBase
{
  public:
    static void TryEnable() { sIsEnabled = true; }
    static bool IsEnabled() { return sIsEnabled; }

    ANGLE_INLINE explicit ScopedVulkanApiPerfTimerImpl(angle::VulkanApiPerfCounterGroup group)
    {
        if (sIsEnabled)
        {
            beginScope(group);
        }
    }
    ANGLE_INLINE explicit ScopedVulkanApiPerfTimerImpl(VulkanApiFunction api)
        : ScopedVulkanApiPerfTimerImpl(GetPerfCounterGroup(api))
    {}
    ANGLE_INLINE ~ScopedVulkanApiPerfTimerImpl()
    {
        if (sIsEnabled)
        {
            endScope();
        }
    }

  private:
    static bool sIsEnabled;
};
}  // namespace priv

#if ANGLE_VULKAN_API_PERF_COUNTERS_MODE != 0
#    if ANGLE_VULKAN_API_PERF_COUNTERS_MODE == 1
using ScopedVulkanApiPerfTimer =
    priv::ScopedVulkanApiPerfTimerImpl<priv::VulkanApiPerfTimerState::CompilerEnabled>;
#    elif ANGLE_VULKAN_API_PERF_COUNTERS_MODE == 2
using ScopedVulkanApiPerfTimer =
    priv::ScopedVulkanApiPerfTimerImpl<priv::VulkanApiPerfTimerState::RuntimeEnabled>;
#    else
#        error "Unknown angle_vulkan_api_perf_counters_mode"
#    endif
#else
using ScopedVulkanApiPerfTimer =
    priv::ScopedVulkanApiPerfTimerImpl<priv::VulkanApiPerfTimerState::Disabled>;
#endif

}  // namespace vk
}  // namespace rx

#if ANGLE_VULKAN_API_PERF_COUNTERS_MODE != 0
#    if defined(ANGLE_PLATFORM_APPLE)
#        error "angle_vulkan_api_perf_counters_mode currently does not have Apple platforms support"
#    endif
// Tracking individual secondary |vkCmd*| calls has too much CPU overhead and is not supported.
#    if !ANGLE_USE_CUSTOM_VULKAN_OUTSIDE_RENDER_PASS_CMD_BUFFERS
#        error \
            "using angle_vulkan_api_perf_counters_mode requires \
angle_enable_custom_vulkan_outside_render_pass_cmd_buffers"
#    endif
#    if !ANGLE_USE_CUSTOM_VULKAN_RENDER_PASS_CMD_BUFFERS
#        error \
            "using angle_vulkan_api_perf_counters_mode requires \
angle_enable_custom_vulkan_render_pass_cmd_buffers"
#    endif
// For wrapping all Vulkan API calls (except secondary command buffer commands, which are handled
// separately).
#    define VK_CALL_WITH_GROUP(vk_api_perf_counter_group, vk_call)                               \
        [&]() {                                                                                  \
            rx::vk::ScopedVulkanApiPerfTimer ANGLE_VK_API_PERF_TIMER(vk_api_perf_counter_group); \
            return vk_call;                                                                      \
        }()
#    define VK_CALL_WITH_API(vk_api_function, vk_call)                                 \
        [&]() {                                                                        \
            rx::vk::ScopedVulkanApiPerfTimer ANGLE_VK_API_PERF_TIMER(vk_api_function); \
            return vk_call;                                                            \
        }()
#    define VK_CALL(vk_api_function, ...) \
        VK_CALL_WITH_API(rx::vk::VulkanApiFunction::vk_api_function, vk_api_function(__VA_ARGS__))
// For Vulkan commands that may only be recorded into Vulkan secondary command buffers (which are
// not expected to be called when using ANGLE custom secondary command buffers).
// - Prefer using this macro for all |vkCmd*| calls when possible instead of |VK_CALL|.
// - Only use |VK_CALL| for commands that must also be recorded directly into the primary command
//   buffer (or try to use secondary command buffers when possible).
// - If below |UNREACHABLE| is reached, this means that particular Vulkan command was recorded into
//   the primary command buffer, breaking the promise.
// - When above happens, need to either:
//   - use |VK_CALL| macro (also extend |VulkanApiFunction| enum);
//   - or try to avoid recording the command into primary command buffer if possible;
// - Which of the above options to choose depend on how often this command will be recorded during
//   each frame. To get the call count check the "*_api_samples" metric in "angle_trace_tests".
//   You may temporarily extend |ANGLE_VK_API_PERF_COUNTER_GROUPS_X| macro to move new API into
//   separate group ("Debug" for example). Or as quick fix use |VK_CALL| plus add a bug with TODO to
//   make the decision later.
#    define VK_SECONDARY_CMD_CALL(secondary_only_cmd_vk_call) \
        [&]() {                                               \
            UNREACHABLE();                                    \
            secondary_only_cmd_vk_call;                       \
        }()
#else
#    define VK_CALL_WITH_GROUP(vk_api_perf_counter_group, vk_call) vk_call
#    define VK_CALL_WITH_API(vk_api_function, vk_call) vk_call
#    define VK_CALL(vk_api_function, ...) vk_api_function(__VA_ARGS__)
#    define VK_SECONDARY_CMD_CALL(secondary_only_cmd_vk_call) secondary_only_cmd_vk_call
#endif

#endif  // LIBANGLE_RENDERER_VULKAN_API_PERF_COUNTERS_H_
