//
// Copyright 2025 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#ifndef LIBANGLE_RENDERER_VULKAN_VK_BARRIER_DATA_H_
#define LIBANGLE_RENDERER_VULKAN_VK_BARRIER_DATA_H_

#include <atomic>
#include <limits>
#include <queue>

#include "libANGLE/renderer/vulkan/vk_cache_utils.h"
#include "libANGLE/renderer/vulkan/vk_ref_counted_event.h"

namespace angle
{
struct FeaturesVk;
}

namespace rx
{
namespace vk
{
class Renderer;

// Information useful for buffer-related barriers
struct BufferMemoryBarrierData
{
    VkPipelineStageFlags pipelineStageFlags;
    // EventStage::InvalidEnum indicates don't use VkEvent for barrier(i.e., use pipelineBarrier
    // instead)
    EventStage eventStage;
};

const BufferMemoryBarrierData &GetBufferMemoryBarrierData(PipelineStage stage);

// Imagine an image going through a few layout transitions:
//
//           srcStage 1    dstStage 2          srcStage 2     dstStage 3
//  Layout 1 ------Transition 1-----> Layout 2 ------Transition 2------> Layout 3
//           srcAccess 1  dstAccess 2          srcAccess 2   dstAccess 3
//   \_________________  ___________________/
//                     \/
//               A transition
//
// Every transition requires 6 pieces of information: from/to layouts, src/dst stage masks and
// src/dst access masks.  At the moment we decide to transition the image to Layout 2 (i.e.
// Transition 1), we need to have Layout 1, srcStage 1 and srcAccess 1 stored as history of the
// image.  To perform the transition, we need to know Layout 2, dstStage 2 and dstAccess 2.
// Additionally, we need to know srcStage 2 and srcAccess 2 to retain them for the next transition.
//
// That is, with the history kept, on every new transition we need 5 pieces of new information:
// layout/dstStage/dstAccess to transition into the layout, and srcStage/srcAccess for the future
// transition out from it.  Given the small number of possible combinations of these values, an
// enum is used were each value encapsulates these 5 pieces of information:
//
//                       +--------------------------------+
//           srcStage 1  | dstStage 2          srcStage 2 |   dstStage 3
//  Layout 1 ------Transition 1-----> Layout 2 ------Transition 2------> Layout 3
//           srcAccess 1 |dstAccess 2          srcAccess 2|  dstAccess 3
//                       +---------------  ---------------+
//                                       \/
//                                 One enum value
//
// Note that, while generally dstStage for the to-transition and srcStage for the from-transition
// are the same, they may occasionally be BOTTOM_OF_PIPE and TOP_OF_PIPE respectively.
enum class ImageAccess
{
    Undefined = 0,
    // Framebuffer attachment accesses are placed first, so they can fit in fewer bits in
    // PackedAttachmentOpsDesc.

    // Color (Write):
    ColorWrite,
    // Used only with dynamic rendering, because it needs a different VkImageLayout
    ColorWriteAndInput,
    MSRTTEmulationColorUnresolveAndResolve,

    // Depth (Write), Stencil (Write)
    DepthWriteStencilWrite,
    // Used only with dynamic rendering, because it needs a different VkImageLayout.  For
    // simplicity, depth/stencil attachments when used as input attachments don't attempt to
    // distinguish read-only aspects.  That's only useful for supporting feedback loops, but if an
    // application is reading depth or stencil through an input attachment, it's safe to assume they
    // wouldn't be accessing the other aspect through a sampler!
    DepthStencilWriteAndInput,

    // Depth (Write), Stencil (Read)
    DepthWriteStencilRead,
    DepthWriteStencilReadFragmentShaderStencilRead,
    DepthWriteStencilReadAllShadersStencilRead,

    // Depth (Read), Stencil (Write)
    DepthReadStencilWrite,
    DepthReadStencilWriteFragmentShaderDepthRead,
    DepthReadStencilWriteAllShadersDepthRead,

    // Depth (Read), Stencil (Read)
    DepthReadStencilRead,
    DepthReadStencilReadFragmentShaderRead,
    DepthReadStencilReadAllShadersRead,

    // The GENERAL layout is used when there's a feedback loop.  For depth/stencil it doesn't matter
    // which aspect is participating in feedback and whether the other aspect is read-only.
    ColorWriteFragmentShaderFeedback,
    ColorWriteAllShadersFeedback,
    DepthStencilFragmentShaderFeedback,
    DepthStencilAllShadersFeedback,

    // Depth/stencil resolve is special because it uses the _color_ output stage and mask
    DepthStencilResolve,
    MSRTTEmulationDepthStencilUnresolveAndResolve,

    Present,
    SharedPresent,
    // The rest of the accesses.
    ExternalPreInitialized,
    ExternalShadersReadOnly,
    ExternalShadersWrite,
    ForeignAccess,
    TransferSrc,
    TransferDst,
    TransferSrcDst,
    // Used when the image is transitioned on the host for use by host image copy
    HostCopy,
    VertexShaderReadOnly,
    VertexShaderWrite,
    // PreFragment == Vertex, Tessellation and Geometry stages
    PreFragmentShadersReadOnly,
    PreFragmentShadersWrite,
    FragmentShadingRateAttachmentReadOnly,
    FragmentShaderReadOnly,
    FragmentShaderWrite,
    ComputeShaderReadOnly,
    ComputeShaderWrite,
    AllGraphicsShadersReadOnly,
    AllGraphicsShadersWrite,
    TransferDstAndComputeWrite,

    InvalidEnum,
    EnumCount = InvalidEnum,
};

ImageAccess GetImageAccessFromGLImageLayout(ErrorContext *context, GLenum layout);

// Information useful for image-related barriers
struct ImageMemoryBarrierData
{
    // The Vk layout corresponding to the ImageAccess key.
    VkImageLayout layout;

    // The stage in which the image is used (or Bottom/Top if not using any specific stage).  Unless
    // Bottom/Top (Bottom used for transition to and Top used for transition from), the two values
    // should match.
    VkPipelineStageFlags dstStageMask;
    VkPipelineStageFlags srcStageMask;
    // Access mask when transitioning into this layout.
    VkAccessFlags dstAccessMask;
    // Access mask when transitioning out from this layout.  Note that source access mask never
    // needs a READ bit, as WAR hazards don't need memory barriers (just execution barriers).
    VkAccessFlags srcAccessMask;
    // Read or write.
    ResourceAccess type;
    // *CommandBufferHelper track an array of PipelineBarriers. This indicates which array element
    // this should be merged into. Right now we track individual barrier for every PipelineStage. If
    // layout has a single stage mask bit, we use that stage as index. If layout has multiple stage
    // mask bits, we pick the lowest stage as the index since it is the first stage that needs
    // barrier.
    PipelineStage barrierIndex;
    EventStage eventStage;
    // The pipeline stage flags group that used for heuristic.
    PipelineStageGroup pipelineStageGroup;
};
using ImageAccessToMemoryBarrierDataMap = angle::PackedEnumMap<ImageAccess, ImageMemoryBarrierData>;

// Initialize EventStage to VkPipelineStageFlags mapping table.
void InitializeEventStageToVkPipelineStageFlagsMap(
    EventStageToVkPipelineStageFlagsMap *map,
    VkPipelineStageFlags supportedVulkanPipelineStageMask);

// Initialize ImageAccess to ImageMemoryBarrierData mapping table.
void InitializeImageLayoutAndMemoryBarrierDataMap(
    const angle::FeaturesVk &features,
    ImageAccessToMemoryBarrierDataMap *mapping,
    VkPipelineStageFlags supportedVulkanPipelineStageMask);

}  // namespace vk
}  // namespace rx
#endif  // LIBANGLE_RENDERER_VULKAN_VK_BARRIER_DATA_H_
