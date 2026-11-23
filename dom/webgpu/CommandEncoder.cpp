/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CommandEncoder.h"

#include "Buffer.h"
#include "CommandBuffer.h"
#include "ComputePassEncoder.h"
#include "Device.h"
#include "ExternalTexture.h"
#include "RenderPassEncoder.h"
#include "TextureView.h"
#include "Utility.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/webgpu/CanvasContext.h"
#include "mozilla/webgpu/ffi/wgpu.h"

namespace mozilla::webgpu {

GPU_IMPL_CYCLE_COLLECTION(CommandEncoder, mParent, mExternalTextures)
GPU_IMPL_JS_WRAP(CommandEncoder)

void CommandEncoder::ConvertTextureDataLayoutToFFI(
    const dom::GPUTexelCopyBufferLayout& aLayout,
    ffi::WGPUFfiTexelCopyBufferLayout* aLayoutFFI) {
  *aLayoutFFI = {};
  aLayoutFFI->offset = aLayout.mOffset;

  if (aLayout.mBytesPerRow.WasPassed()) {
    aLayoutFFI->bytes_per_row = &aLayout.mBytesPerRow.Value();
  } else {
    aLayoutFFI->bytes_per_row = nullptr;
  }

  if (aLayout.mRowsPerImage.WasPassed()) {
    aLayoutFFI->rows_per_image = &aLayout.mRowsPerImage.Value();
  } else {
    aLayoutFFI->rows_per_image = nullptr;
  }
}

void CommandEncoder::ConvertTextureCopyViewToFFI(
    const dom::GPUTexelCopyTextureInfo& aCopy,
    ffi::WGPUTexelCopyTextureInfo* aViewFFI) {
  *aViewFFI = {};
  aViewFFI->texture = aCopy.mTexture->GetId();
  aViewFFI->mip_level = aCopy.mMipLevel;
  const auto& origin = aCopy.mOrigin;
  if (origin.IsRangeEnforcedUnsignedLongSequence()) {
    const auto& seq = origin.GetAsRangeEnforcedUnsignedLongSequence();
    aViewFFI->origin.x = seq.Length() > 0 ? seq[0] : 0;
    aViewFFI->origin.y = seq.Length() > 1 ? seq[1] : 0;
    aViewFFI->origin.z = seq.Length() > 2 ? seq[2] : 0;
  } else if (origin.IsGPUOrigin3DDict()) {
    const auto& dict = origin.GetAsGPUOrigin3DDict();
    aViewFFI->origin.x = dict.mX;
    aViewFFI->origin.y = dict.mY;
    aViewFFI->origin.z = dict.mZ;
  } else {
    MOZ_CRASH("Unexpected origin type");
  }
  aViewFFI->aspect = ConvertTextureAspect(aCopy.mAspect);
}

static ffi::WGPUTexelCopyTextureInfo ConvertTextureCopyView(
    const dom::GPUTexelCopyTextureInfo& aCopy) {
  ffi::WGPUTexelCopyTextureInfo view = {};
  CommandEncoder::ConvertTextureCopyViewToFFI(aCopy, &view);
  return view;
}

CommandEncoder::CommandEncoder(Device* const aParent, RawId aId)
    : ObjectBase(aParent->GetChild(), aId,
                 ffi::wgpu_client_drop_command_encoder),
      ChildOf(aParent) {}

CommandEncoder::~CommandEncoder() = default;

void CommandEncoder::TrackPresentationContext(
    WeakPtr<CanvasContext> aTargetContext) {
  if (aTargetContext) {
    mPresentationContexts.AppendElement(aTargetContext);
  }
}

void CommandEncoder::CopyBufferToBuffer(
    const Buffer& aSource, BufferAddress aSourceOffset,
    const Buffer& aDestination, BufferAddress aDestinationOffset,
    const dom::Optional<BufferAddress>& aSize) {
  // In Javascript, `size === undefined` means "copy from source offset to end
  // of buffer". wgpu_client_command_encoder_copy_buffer_to_buffer uses a value
  // of UINT64_MAX to encode this. If the requested copy size was UINT64_MAX,
  // fudge it to a different value that will still be rejected for misalignment
  // on the device timeline.
  BufferAddress size;
  if (aSize.WasPassed()) {
    if (aSize.Value() == std::numeric_limits<uint64_t>::max()) {
      size = std::numeric_limits<uint64_t>::max() - 4;
    } else {
      size = aSize.Value();
    }
  } else {
    size = std::numeric_limits<uint64_t>::max();
  }

  ffi::wgpu_client_command_encoder_copy_buffer_to_buffer(
      GetClient(), GetId(), aSource.GetId(), aSourceOffset,
      aDestination.GetId(), aDestinationOffset, size);
}

void CommandEncoder::CopyBufferToTexture(
    const dom::GPUTexelCopyBufferInfo& aSource,
    const dom::GPUTexelCopyTextureInfo& aDestination,
    const dom::GPUExtent3D& aCopySize) {
  ffi::WGPUFfiTexelCopyBufferLayout src_layout = {};
  CommandEncoder::ConvertTextureDataLayoutToFFI(aSource, &src_layout);
  ffi::wgpu_client_command_encoder_copy_buffer_to_texture(
      GetClient(), GetId(), aSource.mBuffer->GetId(), &src_layout,
      ConvertTextureCopyView(aDestination), ConvertExtent(aCopySize));

  TrackPresentationContext(aDestination.mTexture->mTargetContext);
}
void CommandEncoder::CopyTextureToBuffer(
    const dom::GPUTexelCopyTextureInfo& aSource,
    const dom::GPUTexelCopyBufferInfo& aDestination,
    const dom::GPUExtent3D& aCopySize) {
  ffi::WGPUFfiTexelCopyBufferLayout dstLayout = {};
  CommandEncoder::ConvertTextureDataLayoutToFFI(aDestination, &dstLayout);
  ffi::wgpu_client_command_encoder_copy_texture_to_buffer(
      GetClient(), GetId(), ConvertTextureCopyView(aSource),
      aDestination.mBuffer->GetId(), &dstLayout, ConvertExtent(aCopySize));
}
void CommandEncoder::CopyTextureToTexture(
    const dom::GPUTexelCopyTextureInfo& aSource,
    const dom::GPUTexelCopyTextureInfo& aDestination,
    const dom::GPUExtent3D& aCopySize) {
  ffi::wgpu_client_command_encoder_copy_texture_to_texture(
      GetClient(), GetId(), ConvertTextureCopyView(aSource),
      ConvertTextureCopyView(aDestination), ConvertExtent(aCopySize));

  TrackPresentationContext(aDestination.mTexture->mTargetContext);
}

void CommandEncoder::ClearBuffer(const Buffer& aBuffer, const uint64_t aOffset,
                                 const dom::Optional<uint64_t>& aSize) {
  uint64_t sizeVal = 0xdeaddead;
  uint64_t* size = nullptr;
  if (aSize.WasPassed()) {
    sizeVal = aSize.Value();
    size = &sizeVal;
  }

  ffi::wgpu_client_command_encoder_clear_buffer(GetClient(), GetId(),
                                                aBuffer.GetId(), aOffset, size);
}

void CommandEncoder::PushDebugGroup(const nsAString& aString) {
  NS_ConvertUTF16toUTF8 marker(aString);
  ffi::wgpu_client_command_encoder_push_debug_group(GetClient(), GetId(),
                                                    &marker);
}
void CommandEncoder::PopDebugGroup() {
  ffi::wgpu_client_command_encoder_pop_debug_group(GetClient(), GetId());
}
void CommandEncoder::InsertDebugMarker(const nsAString& aString) {
  NS_ConvertUTF16toUTF8 marker(aString);
  ffi::wgpu_client_command_encoder_insert_debug_marker(GetClient(), GetId(),
                                                       &marker);
}

already_AddRefed<ComputePassEncoder> CommandEncoder::BeginComputePass(
    const dom::GPUComputePassDescriptor& aDesc) {
  ffi::WGPUFfiComputePassDescriptor desc = {};

  webgpu::StringHelper label(aDesc.mLabel);
  desc.label = label.Get();

  ffi::WGPUPassTimestampWrites passTimestampWrites = {};
  if (aDesc.mTimestampWrites.WasPassed()) {
    AssignPassTimestampWrites(aDesc.mTimestampWrites.Value(),
                              passTimestampWrites);
    desc.timestamp_writes = &passTimestampWrites;
  }

  RawId id = ffi::wgpu_client_command_encoder_begin_compute_pass(
      GetClient(), GetId(), &desc);
  RefPtr<ComputePassEncoder> pass = new ComputePassEncoder(this, id);
  pass->SetLabel(aDesc.mLabel);
  return pass.forget();
}

already_AddRefed<RenderPassEncoder> CommandEncoder::BeginRenderPass(
    const dom::GPURenderPassDescriptor& aDesc) {
  dom::GPURenderPassDescriptor desc{aDesc};

  auto coerceToViewInPlace =
      [](dom::OwningGPUTextureOrGPUTextureView& texOrView)
      -> RefPtr<TextureView> {
    RefPtr<TextureView> view;
    switch (texOrView.GetType()) {
      case dom::OwningGPUTextureOrGPUTextureView::Type::eGPUTexture: {
        dom::GPUTextureViewDescriptor defaultDesc{};
        RefPtr<Texture> tex = texOrView.GetAsGPUTexture();
        texOrView.SetAsGPUTextureView() =
            tex->CreateView(defaultDesc, IgnoreErrors());
        break;
      }

      case dom::OwningGPUTextureOrGPUTextureView::Type::eGPUTextureView:
        // Nothing to do, great!
        break;
    }
    view = texOrView.GetAsGPUTextureView();
    return view;
  };

  for (auto& atOrNull : desc.mColorAttachments) {
    if (atOrNull.IsNull()) {
      continue;
    }
    auto& at = atOrNull.Value();
    TrackPresentationContext(coerceToViewInPlace(at.mView)->GetTargetContext());
    if (at.mResolveTarget.WasPassed()) {
      TrackPresentationContext(
          coerceToViewInPlace(at.mResolveTarget.Value())->GetTargetContext());
    }
  }
  if (desc.mDepthStencilAttachment.WasPassed()) {
    coerceToViewInPlace(desc.mDepthStencilAttachment.Value().mView);
  }

  auto id = BeginFfiRenderPass(GetClient(), GetId(), desc);
  RefPtr<RenderPassEncoder> pass = new RenderPassEncoder(this, id);
  pass->SetLabel(desc.mLabel);
  return pass.forget();
}

void CommandEncoder::ResolveQuerySet(QuerySet& aQuerySet, uint32_t aFirstQuery,
                                     uint32_t aQueryCount,
                                     webgpu::Buffer& aDestination,
                                     uint64_t aDestinationOffset) {
  ffi::wgpu_client_command_encoder_resolve_query_set(
      GetClient(), GetId(), aQuerySet.GetId(), aFirstQuery, aQueryCount,
      aDestination.GetId(), aDestinationOffset);
}

void CommandEncoder::EndComputePass(
    RawId aComputePassEncoderId, CanvasContextArray& aCanvasContexts,
    Span<RefPtr<ExternalTexture>> aExternalTextures) {
  for (const auto& context : aCanvasContexts) {
    TrackPresentationContext(context);
  }
  mExternalTextures.AppendElements(aExternalTextures);

  ffi::wgpu_client_compute_pass_encoder_end(GetClient(), aComputePassEncoderId);
}

void CommandEncoder::EndRenderPass(
    RawId aRenderPassEncoderId, CanvasContextArray& aCanvasContexts,
    Span<RefPtr<ExternalTexture>> aExternalTextures) {
  for (const auto& context : aCanvasContexts) {
    TrackPresentationContext(context);
  }
  mExternalTextures.AppendElements(aExternalTextures);

  ffi::wgpu_client_render_pass_encoder_end(GetClient(), aRenderPassEncoderId);
}

already_AddRefed<CommandBuffer> CommandEncoder::Finish(
    const dom::GPUCommandBufferDescriptor& aDesc) {
  ffi::WGPUFfiCommandBufferDescriptor desc = {};

  webgpu::StringHelper label(aDesc.mLabel);
  desc.label = label.Get();

  RawId command_buffer_id =
      ffi::wgpu_client_command_encoder_finish(GetClient(), GetId(), &desc);

  RefPtr<CommandBuffer> comb = new CommandBuffer(
      mParent, command_buffer_id, std::move(mPresentationContexts),
      std::move(mExternalTextures));
  comb->SetLabel(aDesc.mLabel);
  return comb.forget();
}

}  // namespace mozilla::webgpu
