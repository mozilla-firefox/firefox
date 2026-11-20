//
// Copyright 2025 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "libANGLE/renderer/vulkan/vk_barrier_data.h"
#include "libANGLE/renderer/vulkan/vk_renderer.h"

namespace rx
{
namespace vk
{
namespace
{
// clang-format off
constexpr angle::PackedEnumMap<PipelineStage, BufferMemoryBarrierData> kBufferMemoryBarrierData = {
    {PipelineStage::TopOfPipe, {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, EventStage::InvalidEnum}},
    {PipelineStage::DrawIndirect, {VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, EventStage::VertexInput}},
    {PipelineStage::VertexInput, {VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, EventStage::VertexInput}},
    {PipelineStage::VertexShader, {VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, EventStage::VertexShader}},
    {PipelineStage::TessellationControl, {VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT, EventStage::InvalidEnum}},
    {PipelineStage::TessellationEvaluation, {VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT, EventStage::InvalidEnum}},
    {PipelineStage::GeometryShader, {VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, EventStage::InvalidEnum}},
    {PipelineStage::TransformFeedback, {VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT, EventStage::TransformFeedbackWrite}},
    {PipelineStage::FragmentShadingRate, {0, EventStage::InvalidEnum}},
    {PipelineStage::EarlyFragmentTest, {0, EventStage::InvalidEnum}},
    {PipelineStage::FragmentShader, {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, EventStage::FragmentShader}},
    {PipelineStage::LateFragmentTest, {0, EventStage::InvalidEnum}},
    {PipelineStage::ColorAttachmentOutput, {0, EventStage::InvalidEnum}},
    {PipelineStage::ComputeShader, {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, EventStage::ComputeShader}},
    {PipelineStage::Transfer, {VK_PIPELINE_STAGE_TRANSFER_BIT, EventStage::InvalidEnum}},
    {PipelineStage::BottomOfPipe, BufferMemoryBarrierData{VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, EventStage::InvalidEnum}},
    {PipelineStage::Host, {VK_PIPELINE_STAGE_HOST_BIT, EventStage::InvalidEnum}},
};

constexpr ImageAccessToMemoryBarrierDataMap kImageMemoryBarrierData = {
    {
        ImageAccess::Undefined,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            // Transition to: we don't expect to transition into Undefined.
            0,
            // Transition from: there's no data in the image to care about.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::InvalidEnum,
            // We do not directly use this layout in SetEvent. We transit to other layout before using
            EventStage::InvalidEnum,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::ColorWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::ColorAttachmentOutput,
            EventStage::Attachment,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::ColorWriteAndInput,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::ColorAttachmentOutput,
            EventStage::Attachment,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::MSRTTEmulationColorUnresolveAndResolve,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::FragmentShader,
            EventStage::AttachmentAndFragmentShader,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::DepthWriteStencilWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            kAllDepthStencilPipelineStageFlags,
            kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::EarlyFragmentTest,
            EventStage::Attachment,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::DepthStencilWriteAndInput,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ,
            kAllDepthStencilPipelineStageFlags,
            kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::EarlyFragmentTest,
            EventStage::Attachment,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::DepthWriteStencilRead,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
            kAllDepthStencilPipelineStageFlags,
            kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::EarlyFragmentTest,
            EventStage::Attachment,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::DepthWriteStencilReadFragmentShaderStencilRead,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | kAllDepthStencilPipelineStageFlags,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::EarlyFragmentTest,
            EventStage::AttachmentAndFragmentShader,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::DepthWriteStencilReadAllShadersStencilRead,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,
            kAllShadersPipelineStageFlags | kAllDepthStencilPipelineStageFlags,
            kAllShadersPipelineStageFlags | kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::VertexShader,
            EventStage::AttachmentAndAllShaders,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::DepthReadStencilWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
            kAllDepthStencilPipelineStageFlags,
            kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::EarlyFragmentTest,
            EventStage::Attachment,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::DepthReadStencilWriteFragmentShaderDepthRead,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | kAllDepthStencilPipelineStageFlags,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::EarlyFragmentTest,
            EventStage::AttachmentAndFragmentShader,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::DepthReadStencilWriteAllShadersDepthRead,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
            kAllShadersPipelineStageFlags | kAllDepthStencilPipelineStageFlags,
            kAllShadersPipelineStageFlags | kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::VertexShader,
            EventStage::AttachmentAndAllShaders,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::DepthReadStencilRead,
            ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            kAllDepthStencilPipelineStageFlags,
            kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::EarlyFragmentTest,
            EventStage::Attachment,
            PipelineStageGroup::FragmentOnly,
        },
    },

    {
        ImageAccess::DepthReadStencilReadFragmentShaderRead,
            ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | kAllDepthStencilPipelineStageFlags,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::EarlyFragmentTest,
            EventStage::AttachmentAndFragmentShader,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::DepthReadStencilReadAllShadersRead,
            ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            kAllShadersPipelineStageFlags | kAllDepthStencilPipelineStageFlags,
            kAllShadersPipelineStageFlags | kAllDepthStencilPipelineStageFlags,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::VertexShader,
            EventStage::AttachmentAndAllShaders,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::ColorWriteFragmentShaderFeedback,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::FragmentShader,
            EventStage::AttachmentAndFragmentShader,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::ColorWriteAllShadersFeedback,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | kAllShadersPipelineStageFlags,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | kAllShadersPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::VertexShader,
            EventStage::AttachmentAndAllShaders,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::DepthStencilFragmentShaderFeedback,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            kAllDepthStencilPipelineStageFlags | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            kAllDepthStencilPipelineStageFlags | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::FragmentShader,
            EventStage::AttachmentAndFragmentShader,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::DepthStencilAllShadersFeedback,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            kAllDepthStencilPipelineStageFlags | kAllShadersPipelineStageFlags,
            kAllDepthStencilPipelineStageFlags | kAllShadersPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::VertexShader,
            EventStage::AttachmentAndAllShaders,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::DepthStencilResolve,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            // Note: depth/stencil resolve uses color output stage and mask!
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::ColorAttachmentOutput,
            EventStage::Attachment,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::MSRTTEmulationDepthStencilUnresolveAndResolve,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            // Note: depth/stencil resolve uses color output stage and mask!
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::FragmentShader,
            EventStage::AttachmentAndFragmentShader,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::Present,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            // Transition to: do not delay execution of commands in the second synchronization
            // scope. Allow layout transition to be delayed until present semaphore is signaled.
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            // Transition from: use same stages as in Acquire Image Semaphore stage mask in order to
            // build a dependency chain from the Acquire Image Semaphore to the layout transition's
            // first synchronization scope.
            kSwapchainAcquireImageWaitStageFlags,
            // Transition to: vkQueuePresentKHR automatically performs the appropriate memory barriers:
            //
            // > Any writes to memory backing the images referenced by the pImageIndices and
            // > pSwapchains members of pPresentInfo, that are available before vkQueuePresentKHR
            // > is executed, are automatically made visible to the read access performed by the
            // > presentation engine.
            0,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::BottomOfPipe,
            // We do not directly use this layout in SetEvent.
            EventStage::InvalidEnum,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::SharedPresent,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR,
            // All currently possible stages for SharedPresent
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_MEMORY_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::BottomOfPipe,
            EventStage::AttachmentAndFragmentShaderAndTransfer,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::ExternalPreInitialized,
        ImageMemoryBarrierData{
            // Binding a VkImage with an initial layout of VK_IMAGE_LAYOUT_UNDEFINED to external
            // memory whose content has already been defined does not make the content undefined
            // (see 12.8.1.  External Resource Sharing).
            //
            // Note that for external memory objects, if the content is already defined, the
            // ownership rules imply that the first operation on the texture must be a call to
            // glWaitSemaphoreEXT that grants ownership of the image and informs us of the true
            // layout.  If the content is not already defined, the first operation may not be a
            // glWaitSemaphore, but in this case undefined layout is appropriate.
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            // Transition to: we don't expect to transition into PreInitialized.
            0,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_MEMORY_WRITE_BIT,
            ResourceAccess::ReadOnly,
            PipelineStage::InvalidEnum,
            // We do not directly use this layout in SetEvent. We transit to internal layout before using
            EventStage::InvalidEnum,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::ExternalShadersReadOnly,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::TopOfPipe,
            // We do not directly use this layout in SetEvent. We transit to internal layout before using
            EventStage::InvalidEnum,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::ExternalShadersWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_SHADER_WRITE_BIT,
            ResourceAccess::ReadWrite,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::TopOfPipe,
            // We do not directly use this layout in SetEvent. We transit to internal layout before using
            EventStage::InvalidEnum,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::ForeignAccess,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            // Transition to: we don't expect to transition into ForeignAccess, that's done at
            // submission time by the CommandQueue; the following value doesn't matter.
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            // Transition to: see dstStageMask
            0,
            // Transition from: all writes must finish before barrier; it is unknown how the foreign
            // entity has access the memory.
            VK_ACCESS_MEMORY_WRITE_BIT,
            ResourceAccess::ReadWrite,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::TopOfPipe,
            // We do not directly use this layout in SetEvent. We transit to internal layout before using
            EventStage::InvalidEnum,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::TransferSrc,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_TRANSFER_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::Transfer,
            EventStage::Transfer,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::TransferDst,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            // Transition to: all writes must happen after barrier.
            VK_ACCESS_TRANSFER_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_TRANSFER_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::Transfer,
            EventStage::Transfer,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::TransferSrcDst,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_TRANSFER_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::Transfer,
            EventStage::Transfer,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::HostCopy,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            // Transition to: we don't expect to transition into HostCopy on the GPU.
            0,
            // Transition from: the data was initialized in the image by the host.  Note that we
            // only transition to this layout if the image was previously in UNDEFINED, in which
            // case it didn't contain any data prior to the host copy either.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::InvalidEnum,
            // We do not directly use this layout in SetEvent.
            EventStage::InvalidEnum,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::VertexShaderReadOnly,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::VertexShader,
            EventStage::VertexShader,
            PipelineStageGroup::PreFragmentOnly,
        },
    },
    {
        ImageAccess::VertexShaderWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_SHADER_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::VertexShader,
            EventStage::VertexShader,
            PipelineStageGroup::PreFragmentOnly,
        },
    },
    {
        ImageAccess::PreFragmentShadersReadOnly,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            kPreFragmentStageFlags,
            kPreFragmentStageFlags,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::VertexShader,
            EventStage::PreFragmentShaders,
            PipelineStageGroup::PreFragmentOnly,
        },
    },
    {
        ImageAccess::PreFragmentShadersWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            kPreFragmentStageFlags,
            kPreFragmentStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_SHADER_WRITE_BIT,
            ResourceAccess::ReadWrite,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::VertexShader,
            EventStage::PreFragmentShaders,
            PipelineStageGroup::PreFragmentOnly,
        },
    },
    {
        ImageAccess::FragmentShadingRateAttachmentReadOnly,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR,
            VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR,
            VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::FragmentShadingRate,
            EventStage::FragmentShadingRate,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::FragmentShaderReadOnly,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::FragmentShader,
            EventStage::FragmentShader,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::FragmentShaderWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_SHADER_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::FragmentShader,
            EventStage::FragmentShader,
            PipelineStageGroup::FragmentOnly,
        },
    },
    {
        ImageAccess::ComputeShaderReadOnly,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            PipelineStage::ComputeShader,
            EventStage::ComputeShader,
            PipelineStageGroup::ComputeOnly,
        },
    },
    {
        ImageAccess::ComputeShaderWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_SHADER_WRITE_BIT,
            ResourceAccess::ReadWrite,
            PipelineStage::ComputeShader,
            EventStage::ComputeShader,
            PipelineStageGroup::ComputeOnly,
        },
    },
    {
        ImageAccess::AllGraphicsShadersReadOnly,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            kAllShadersPipelineStageFlags,
            kAllShadersPipelineStageFlags,
            // Transition to: all reads must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT,
            // Transition from: RAR and WAR don't need memory barrier.
            0,
            ResourceAccess::ReadOnly,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::VertexShader,
            EventStage::AllShaders,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::AllGraphicsShadersWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            kAllShadersPipelineStageFlags,
            kAllShadersPipelineStageFlags,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_SHADER_WRITE_BIT,
            ResourceAccess::ReadWrite,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::VertexShader,
            EventStage::AllShaders,
            PipelineStageGroup::Other,
        },
    },
    {
        ImageAccess::TransferDstAndComputeWrite,
        ImageMemoryBarrierData{
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            // Transition to: all reads and writes must happen after barrier.
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
            // Transition from: all writes must finish before barrier.
            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            ResourceAccess::ReadWrite,
            // In case of multiple destination stages, We barrier the earliest stage
            PipelineStage::ComputeShader,
            EventStage::TransferAndComputeShader,
            PipelineStageGroup::Other,
        },
    },
};
// clang-format on

PipelineStageGroup GetPipelineStageGroupFromStageFlags(VkPipelineStageFlags dstStageMask)
{
    if ((dstStageMask & ~kFragmentAndAttachmentPipelineStageFlags) == 0)
    {
        return PipelineStageGroup::FragmentOnly;
    }
    else if (dstStageMask == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
    {
        return PipelineStageGroup::ComputeOnly;
    }
    else if ((dstStageMask & ~kPreFragmentStageFlags) == 0)
    {
        return PipelineStageGroup::PreFragmentOnly;
    }
    return PipelineStageGroup::Other;
}
}  // namespace

const BufferMemoryBarrierData &GetBufferMemoryBarrierData(PipelineStage stage)
{
    return kBufferMemoryBarrierData[stage];
}

ImageAccess GetImageAccessFromGLImageLayout(ErrorContext *context, GLenum layout)
{
    switch (layout)
    {
        case GL_NONE:
            return ImageAccess::Undefined;
        case GL_LAYOUT_GENERAL_EXT:
            return ImageAccess::ExternalShadersWrite;
        case GL_LAYOUT_COLOR_ATTACHMENT_EXT:
            return ImageAccess::ColorWrite;
        case GL_LAYOUT_DEPTH_STENCIL_ATTACHMENT_EXT:
            return ImageAccess::DepthWriteStencilWrite;
        case GL_LAYOUT_DEPTH_STENCIL_READ_ONLY_EXT:
            return ImageAccess::DepthReadStencilRead;
        case GL_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_EXT:
            return ImageAccess::DepthReadStencilWrite;
        case GL_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_EXT:
            return ImageAccess::DepthWriteStencilRead;
        case GL_LAYOUT_SHADER_READ_ONLY_EXT:
            return ImageAccess::ExternalShadersReadOnly;
        case GL_LAYOUT_TRANSFER_SRC_EXT:
            return ImageAccess::TransferSrc;
        case GL_LAYOUT_TRANSFER_DST_EXT:
            return ImageAccess::TransferDst;
        default:
            UNREACHABLE();
            return vk::ImageAccess::Undefined;
    }
}

void InitializeEventStageToVkPipelineStageFlagsMap(
    EventStageToVkPipelineStageFlagsMap *map,
    VkPipelineStageFlags supportedVulkanPipelineStageMask)
{
    map->fill(0);

    for (const BufferMemoryBarrierData &bufferBarrierData : kBufferMemoryBarrierData)
    {
        const EventStage eventStage = bufferBarrierData.eventStage;
        if (eventStage != EventStage::InvalidEnum)
        {
            (*map)[eventStage] |=
                bufferBarrierData.pipelineStageFlags & supportedVulkanPipelineStageMask;
        }
    }

    for (const ImageMemoryBarrierData &imageBarrierData : kImageMemoryBarrierData)
    {
        const EventStage eventStage = imageBarrierData.eventStage;
        if (eventStage != EventStage::InvalidEnum)
        {
            (*map)[eventStage] |= imageBarrierData.dstStageMask & supportedVulkanPipelineStageMask;
        }
    }
}

void InitializeImageLayoutAndMemoryBarrierDataMap(
    const angle::FeaturesVk &features,
    ImageAccessToMemoryBarrierDataMap *map,
    VkPipelineStageFlags supportedVulkanPipelineStageMask)
{
    *map = kImageMemoryBarrierData;
    for (ImageMemoryBarrierData &barrierData : *map)
    {
        barrierData.srcStageMask &= supportedVulkanPipelineStageMask;
        barrierData.dstStageMask &= supportedVulkanPipelineStageMask;
        ASSERT(barrierData.pipelineStageGroup ==
               GetPipelineStageGroupFromStageFlags(barrierData.dstStageMask));
    }

    // Use the GENERAL layout if possible and efficient.  By removing image layout transitions,
    // we're able to issue more efficient synchronization.
    if (features.supportsUnifiedImageLayouts.enabled)
    {
        for (ImageMemoryBarrierData &barrierData : *map)
        {
            if (barrierData.layout != VK_IMAGE_LAYOUT_UNDEFINED &&
                barrierData.layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
                barrierData.layout != VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR)
            {
                ASSERT(barrierData.layout != VK_IMAGE_LAYOUT_PREINITIALIZED);
                barrierData.layout = VK_IMAGE_LAYOUT_GENERAL;
            }
        }
    }

    // When dynamic rendering is not enabled, input attachments should use the GENERAL layout.
    if (!features.preferDynamicRendering.enabled)
    {
        (*map)[ImageAccess::ColorWriteAndInput].layout        = VK_IMAGE_LAYOUT_GENERAL;
        (*map)[ImageAccess::DepthStencilWriteAndInput].layout = VK_IMAGE_LAYOUT_GENERAL;
    }
}

}  // namespace vk
}  // namespace rx
