//
// Copyright 2025 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// vk_api_perf_counters:
//    Functionality for storing and collecting Vulkan API Performance Counters
//

#include "libANGLE/renderer/vulkan/vk_api_perf_counters.h"

namespace rx
{
namespace vk
{
namespace
{
#if defined(ANGLE_PLATFORM_APPLE)
static VulkanApiPerfCounters sVulkanApiPerfCounters;
#else
thread_local VulkanApiPerfCounters sVulkanApiPerfCounters;
#endif
}  // anonymous namespace

VulkanApiPerfCounters &GetCurrentThreadVulkanApiPerfCounters()
{
#if defined(ANGLE_PLATFORM_APPLE)
    UNIMPLEMENTED();
#endif
    return sVulkanApiPerfCounters;
}

namespace priv
{
bool ScopedVulkanApiPerfTimerImpl<VulkanApiPerfTimerState::RuntimeEnabled>::sIsEnabled = false;
}  // namespace priv
}  // namespace vk
}  // namespace rx
