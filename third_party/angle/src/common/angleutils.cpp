//
// Copyright 2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "common/angleutils.h"
#include "common/SimpleMutex.h"
#include "common/debug.h"
#include "common/unsafe_buffers.h"

#include <stdio.h>

#include <limits>
#include <vector>

namespace angle
{
// dirtyPointer is a special value that will make the comparison with any valid pointer fail and
// force the renderer to re-apply the state.
const uintptr_t DirtyPointer = std::numeric_limits<uintptr_t>::max();

std::string_view GetVulkanApiPerfCounterGroupName(VulkanApiPerfCounterGroup group)
{
#define ANGLE_VK_API_PERF_COUNTER_CASE_GROUP_RETURN_NAME(GROUP) \
    case VulkanApiPerfCounterGroup::GROUP:                      \
        return ANGLE_STRINGIFY(GROUP);

    switch (group)
    {
        ANGLE_VK_API_PERF_COUNTER_GROUPS_X(ANGLE_VK_API_PERF_COUNTER_CASE_GROUP_RETURN_NAME)
        default:
            UNREACHABLE();
            return "INVALID_VulkanApiPerfCounterGroup";
    }

#undef ANGLE_VK_API_PERF_COUNTER_CASE_GROUP_RETURN_NAME
}

std::string_view GetVulkanApiPerfCounterTypeName(VulkanApiPerfCounterType type)
{
#define ANGLE_VK_API_PERF_COUNTER_CASE_TYPE_RETURN_NAME(TYPE) \
    case VulkanApiPerfCounterType::TYPE:                      \
        return ANGLE_STRINGIFY(TYPE);

    switch (type)
    {
        ANGLE_VK_API_PERF_COUNTER_TYPES_X(ANGLE_VK_API_PERF_COUNTER_CASE_TYPE_RETURN_NAME)
        default:
            UNREACHABLE();
            return "INVALID_VulkanApiPerfCounterType";
    }

#undef ANGLE_VK_API_PERF_COUNTER_CASE_TYPE_RETURN_NAME
}

std::string_view GetVulkanApiPerfCounterName(VulkanApiPerfCounterGroup group,
                                             VulkanApiPerfCounterType type)
{
#define ANGLE_VK_API_PERF_COUNTER_CASE_TYPE_RETURN_COUNTER_NAME(TYPE, GROUP) \
    case VulkanApiPerfCounterType::TYPE:                                     \
        return ANGLE_STRINGIFY(vk##GROUP##Api##TYPE);

#define ANGLE_VK_API_PERF_COUNTER_CASE_GROUP_SWITCH_TYPE(GROUP)                 \
    case VulkanApiPerfCounterGroup::GROUP:                                      \
        switch (type)                                                           \
        {                                                                       \
            ANGLE_VK_API_PERF_COUNTER_TYPES_WITH_PARAM_X(                       \
                ANGLE_VK_API_PERF_COUNTER_CASE_TYPE_RETURN_COUNTER_NAME, GROUP) \
            default:                                                            \
                UNREACHABLE();                                                  \
                return "INVALID_VulkanApiPerfCounterType";                      \
        }

    switch (group)
    {
        ANGLE_VK_API_PERF_COUNTER_GROUPS_X(ANGLE_VK_API_PERF_COUNTER_CASE_GROUP_SWITCH_TYPE)
        default:
            UNREACHABLE();
            return "INVALID_VulkanApiPerfCounterGroup";
    }

#undef ANGLE_VK_API_PERF_COUNTER_CASE_TYPE_RETURN_COUNTER_NAME
#undef ANGLE_VK_API_PERF_COUNTER_CASE_GROUP_SWITCH_TYPE
}
}  // namespace angle

std::string ArrayString(unsigned int i)
{
    // We assume that UINT_MAX and GL_INVALID_INDEX are equal.
    ASSERT(i != UINT_MAX);

    std::stringstream strstr;
    strstr << "[";
    strstr << i;
    strstr << "]";
    return strstr.str();
}

std::string ArrayIndexString(const std::vector<unsigned int> &indices)
{
    std::stringstream strstr;

    for (unsigned int index : angle::Reversed(indices))
    {
        // We assume that UINT_MAX and GL_INVALID_INDEX are equal.
        ASSERT(index != UINT_MAX);
        strstr << "[";
        strstr << index;
        strstr << "]";
    }

    return strstr.str();
}

size_t FormatStringIntoVector(const char *fmt, va_list vararg, std::vector<char> &outBuffer)
{
    va_list varargCopy;
    va_copy(varargCopy, vararg);

    int len = ANGLE_UNSAFE_TODO(vsnprintf(nullptr, 0, fmt, vararg));
    ASSERT(len >= 0);

    outBuffer.resize(len + 1, 0);

    len = ANGLE_UNSAFE_TODO(vsnprintf(outBuffer.data(), outBuffer.size(), fmt, varargCopy));
    va_end(varargCopy);
    ASSERT(len >= 0);
    return static_cast<size_t>(len);
}
