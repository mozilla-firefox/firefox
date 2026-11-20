//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PersistentCommandPool.h:
//    Defines the class interface for PersistentCommandBuffer
//

#ifndef LIBANGLE_RENDERER_VULKAN_PERSISTENTCOMMANDPOOL_H_
#define LIBANGLE_RENDERER_VULKAN_PERSISTENTCOMMANDPOOL_H_

#include <vector>

#include "libANGLE/renderer/vulkan/vk_utils.h"
#include "libANGLE/renderer/vulkan/vk_wrapper.h"

namespace rx
{

namespace vk
{

// When command buffer gets collected, we have the option to call reset right away, or we can defer
// the reset call until the command buffer is going to be recycled for use again by allocate call.
enum class WhenToResetCommandBuffer
{
    Now,
    Defer,
};

class PersistentCommandPool final
{
  public:
    PersistentCommandPool();
    ~PersistentCommandPool();

    void destroy(VkDevice device);
    angle::Result init(ErrorContext *context,
                       ProtectionType protectionType,
                       uint32_t queueFamilyIndex);

    angle::Result allocate(ErrorContext *context, PrimaryCommandBuffer *commandBufferOut);
    angle::Result collect(ErrorContext *context,
                          PrimaryCommandBuffer &&buffer,
                          WhenToResetCommandBuffer whenToReset);

    bool valid() const { return mCommandPool.valid(); }

  private:
    angle::Result allocateCommandBuffer(ErrorContext *context);

    // command buffers that are free and ready to use
    std::deque<PrimaryCommandBuffer> mFreeBuffers;
    // command buffers that are free but needs reset before use
    std::deque<PrimaryCommandBuffer> mFreeBuffersNeedReset;

    CommandPool mCommandPool;

    static const int kInitBufferNum = 2;
};

}  // namespace vk

}  // namespace rx

#endif
