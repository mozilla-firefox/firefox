/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGPUParent.h"

#include <memory>
#include <unordered_set>

#include "ExternalTexture.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/gfx/FileHandleWrapper.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/layers/WebRenderTextureHost.h"
#include "mozilla/webgpu/FrontBufferSnapshot.h"
#include "mozilla/webgpu/SharedTexture.h"
#include "mozilla/webgpu/ffi/wgpu.h"

#if defined(XP_WIN)
#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "mozilla/webgpu/SharedTextureD3D11.h"
#endif

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
#  include "mozilla/webgpu/SharedTextureDMABuf.h"
#endif

#if defined(XP_MACOSX)
#  include "mozilla/webgpu/SharedTextureMacIOSurface.h"
#endif

namespace mozilla::webgpu {

const uint64_t POLL_TIME_MS = 100;

static mozilla::LazyLogModule sLogger("WebGPU");

namespace ffi {

extern bool wgpu_server_use_shared_texture_for_swap_chain(
    WGPUWebGPUParentPtr aParent, WGPUSwapChainId aSwapChainId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  return parent->UseSharedTextureForSwapChain(aSwapChainId);
}

extern void wgpu_server_disable_shared_texture_for_swap_chain(
    WGPUWebGPUParentPtr aParent, WGPUSwapChainId aSwapChainId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  parent->DisableSharedTextureForSwapChain(aSwapChainId);
}

extern bool wgpu_server_ensure_shared_texture_for_swap_chain(
    WGPUWebGPUParentPtr aParent, WGPUSwapChainId aSwapChainId,
    WGPUDeviceId aDeviceId, WGPUTextureId aTextureId, uint32_t aWidth,
    uint32_t aHeight, struct WGPUTextureFormat aFormat,
    WGPUTextureUsages aUsage) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  return parent->EnsureSharedTextureForSwapChain(
      aSwapChainId, aDeviceId, aTextureId, aWidth, aHeight, aFormat, aUsage);
}

extern void wgpu_server_ensure_shared_texture_for_readback(
    WGPUWebGPUParentPtr aParent, WGPUSwapChainId aSwapChainId,
    WGPUDeviceId aDeviceId, WGPUTextureId aTextureId, uint32_t aWidth,
    uint32_t aHeight, struct WGPUTextureFormat aFormat,
    WGPUTextureUsages aUsage) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  parent->EnsureSharedTextureForReadBackPresent(
      aSwapChainId, aDeviceId, aTextureId, aWidth, aHeight, aFormat, aUsage);
}

#ifdef XP_WIN
extern void* wgpu_server_get_shared_texture_handle(WGPUWebGPUParentPtr aParent,
                                                   WGPUTextureId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto texture = parent->GetSharedTexture(aId);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  auto* textureD3D11 = texture->AsSharedTextureD3D11();
  if (!textureD3D11) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }
  void* sharedHandle = textureD3D11->GetSharedTextureHandle();
  if (!sharedHandle) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    gfxCriticalNoteOnce << "Failed to get shared handle";
    return nullptr;
  }

  return sharedHandle;
}
#endif

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
extern int32_t wgpu_server_get_dma_buf_fd(WGPUWebGPUParentPtr aParent,
                                          WGPUTextureId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto texture = parent->GetSharedTexture(aId);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return -1;
  }

  auto* textureDMABuf = texture->AsSharedTextureDMABuf();
  if (!textureDMABuf) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return -1;
  }
  auto fd = textureDMABuf->CloneDmaBufFd();
  // fd should be closed by the caller.
  return fd.release();
}

extern "C" bool wgpu_server_get_linux_dmabuf_modifiers(
    const uint64_t** aModifiers, uint32_t* aModifierCount) {
  if (!aModifiers || !aModifierCount) {
    return false;
  }

  *aModifiers = nullptr;
  *aModifierCount = 0;

  // Vulkan B8G8R8A8_UNORM maps to the exported ARGB dmabuf format on
  // little-endian Linux.
  const auto& modifiers = mozilla::gfx::gfxVars::DMABufModifiersARGB();
  if (modifiers.IsEmpty()) {
    return false;
  }

  *aModifiers = modifiers.Elements();
  *aModifierCount = static_cast<uint32_t>(modifiers.Length());
  return true;
}
#endif

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
extern ffi::WGPUDMABufInfo wgpu_server_get_dma_buf_info(
    WGPUWebGPUParentPtr aParent, WGPUTextureId aId) {
  ffi::WGPUDMABufInfo info = {};

  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto texture = parent->GetSharedTexture(aId);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return info;
  }

  auto* textureDMABuf = texture->AsSharedTextureDMABuf();
  if (!textureDMABuf) {
    return info;
  }
  return textureDMABuf->GetDMABufInfo();
}
#endif

#if defined(XP_MACOSX)
extern uint32_t wgpu_server_get_external_io_surface_id(
    WGPUWebGPUParentPtr aParent, WGPUTextureId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto texture = parent->GetSharedTexture(aId);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return 0;
  }

  auto* textureIOSurface = texture->AsSharedTextureMacIOSurface();
  if (!textureIOSurface) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return 0;
  }
  return textureIOSurface->GetIOSurfaceId();
}
#endif

extern void wgpu_server_remove_shared_texture(WGPUWebGPUParentPtr aParent,
                                              WGPUTextureId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->RemoveSharedTexture(aId);
}

extern bool wgpu_parent_is_external_texture_enabled() {
  return gfx::gfxVars::AllowWebGPUExternalTexture();
}

extern ffi::WGPUExternalTextureDescriptorFromSource
wgpu_parent_external_texture_source_get_external_texture_descriptor(
    void* aParent, WGPUExternalTextureSourceId aId,
    ffi::WGPUPredefinedColorSpace aDestColorSpace) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  const auto& source = parent->GetExternalTextureSource(aId);
  return source.GetExternalTextureDescriptor(aDestColorSpace);
}

extern void wgpu_parent_destroy_external_texture_source(
    WGPUWebGPUParentPtr aParent, WGPUExternalTextureSourceId aId) {
  auto* const parent = static_cast<WebGPUParent*>(aParent);
  parent->DestroyExternalTextureSource(aId);
}

extern void wgpu_parent_drop_external_texture_source(
    WGPUWebGPUParentPtr aParent, WGPUExternalTextureSourceId aId) {
  auto* const parent = static_cast<WebGPUParent*>(aParent);
  parent->DropExternalTextureSource(aId);
}

extern void wgpu_server_dealloc_buffer_shmem(WGPUWebGPUParentPtr aParent,
                                             WGPUBufferId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->DeallocBufferShmem(aId);
}

extern void wgpu_server_pre_device_drop(WGPUWebGPUParentPtr aParent,
                                        WGPUDeviceId aId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->PreDeviceDrop(aId);
}

extern void wgpu_server_set_buffer_map_data(
    WGPUWebGPUParentPtr aParent, WGPUDeviceId aDeviceId, WGPUBufferId aBufferId,
    bool aHasMapFlags, uint64_t aMappedOffset, uint64_t aMappedSize,
    bool aIsMapped, uintptr_t aShmemIndex) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto mapping = parent->mTempMappings.at(aShmemIndex);

  auto data = WebGPUParent::BufferMapData{
      .mShmem = mapping,
      .mHasMapFlags = aHasMapFlags,
      .mMappedOffset = aMappedOffset,
      .mMappedSize = aMappedSize,
      .mIsMapped = aIsMapped,
      .mDeviceId = aDeviceId,
  };

  parent->mSharedMemoryMap.insert({aBufferId, std::move(data)});
}

extern void wgpu_parent_buffer_unmap(WGPUWebGPUParentPtr aParent,
                                     WGPUDeviceId aDeviceId,
                                     WGPUBufferId aBufferId, bool aFlush) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->BufferUnmap(aDeviceId, aBufferId, aFlush);
}

extern void wgpu_parent_queue_submit(
    WGPUWebGPUParentPtr aParent, WGPUQueueId aQueueId,
    const WGPUCommandBufferId* aCommandBufferIds,
    uintptr_t aCommandBufferIdsLength, const WGPUTextureId* aTextureIds,
    uintptr_t aTextureIdsLength,
    const WGPUExternalTextureSourceId* aExternalTextureSourceIds,
    uintptr_t aExternalTextureSourceIdsLength) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto command_buffers = Span(aCommandBufferIds, aCommandBufferIdsLength);
  auto textures = Span(aTextureIds, aTextureIdsLength);
  auto externalTextureSources =
      Span(aExternalTextureSourceIds, aExternalTextureSourceIdsLength);
  parent->QueueSubmit(aQueueId, command_buffers, textures,
                      externalTextureSources);
}

extern void wgpu_parent_create_swap_chain(
    WGPUWebGPUParentPtr aParent, WGPUDeviceId aDeviceId, WGPUQueueId aQueueId,
    uint32_t aWidth, uint32_t aHeight, WGPUSurfaceFormat aFormat,
    const WGPUBufferId* aBufferIds, uintptr_t aBufferIdsLength,
    WGPURemoteTextureOwnerId aRemoteTextureOwnerId,
    bool aUseSharedTextureInSwapChain) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto buffer_ids_span = Span(aBufferIds, aBufferIdsLength);
  auto buffer_ids = nsTArray<RawId>(aBufferIdsLength);
  for (const RawId id : buffer_ids_span) {
    buffer_ids.AppendElement(id);
  }
  auto size = gfx::IntSize(aWidth, aHeight);
  auto format = gfx::SurfaceFormat(aFormat);
  auto desc = layers::RGBDescriptor(size, format, gfx::ColorSpace2::SRGB,
                                    gfx::TransferFunction::SRGB);
  auto owner = layers::RemoteTextureOwnerId{aRemoteTextureOwnerId};
  parent->DeviceCreateSwapChain(aDeviceId, aQueueId, desc, buffer_ids, owner,
                                aUseSharedTextureInSwapChain);
}

extern void wgpu_parent_swap_chain_present(
    WGPUWebGPUParentPtr aParent, WGPUTextureId aTextureId,
    WGPUCommandEncoderId aCommandEncoderId,
    WGPUCommandBufferId aCommandBufferId, WGPURemoteTextureId aRemoteTextureId,
    WGPURemoteTextureOwnerId aRemoteTextureOwnerId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto remote_texture = layers::RemoteTextureId{aRemoteTextureId};
  auto owner = layers::RemoteTextureOwnerId{aRemoteTextureOwnerId};
  parent->SwapChainPresent(aTextureId, aCommandEncoderId, aCommandBufferId,
                           remote_texture, owner);
}

extern void wgpu_parent_swap_chain_drop(
    WGPUWebGPUParentPtr aParent, WGPURemoteTextureOwnerId aRemoteTextureOwnerId,
    WGPURemoteTextureTxnType aTxnType, WGPURemoteTextureTxnId aTxnId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto owner = layers::RemoteTextureOwnerId{aRemoteTextureOwnerId};
  parent->SwapChainDrop(owner, aTxnType, aTxnId);
}

#ifdef XP_WIN
extern bool wgpu_parent_get_compositor_device_luid(
    struct WGPUFfiLUID* aOutLuid) {
  auto luid = WebGPUParent::GetCompositorDeviceLuid();
  if (luid.isSome()) {
    *aOutLuid = luid.extract();
    return true;
  } else {
    return false;
  }
}
#endif

extern void wgpu_parent_post_request_device(WGPUWebGPUParentPtr aParent,
                                            WGPUDeviceId aDeviceId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  parent->PostAdapterRequestDevice(aDeviceId);
}

extern ffi::WGPUBufferMapClosure wgpu_parent_build_buffer_map_closure(
    WGPUWebGPUParentPtr aParent, RawId aDeviceId, RawId aBufferId,
    ffi::WGPUHostMap aMode, uint64_t aOffset, uint64_t aSize) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto request =
      std::make_unique<WebGPUParent::MapRequest>(WebGPUParent::MapRequest{
          parent, aDeviceId, aBufferId, aMode, aOffset, aSize});

  ffi::WGPUBufferMapClosure closure = {
      &WebGPUParent::MapCallback,
      reinterpret_cast<uint8_t*>(request.release())};

  return closure;
}

extern ffi::WGPUSubmittedWorkDoneClosure
wgpu_parent_build_submitted_work_done_closure(WGPUWebGPUParentPtr aParent,
                                              WGPUQueueId aQueueId) {
  auto* parent = static_cast<WebGPUParent*>(aParent);

  auto request = std::make_unique<WebGPUParent::OnSubmittedWorkDoneRequest>(
      WebGPUParent::OnSubmittedWorkDoneRequest{parent, aQueueId});

  ffi::WGPUSubmittedWorkDoneClosure closure = {
      &WebGPUParent::OnSubmittedWorkDoneCallback,
      reinterpret_cast<uint8_t*>(request.release())};

  return closure;
}

extern void wgpu_parent_send_server_message(WGPUWebGPUParentPtr aParent,
                                            struct WGPUByteBuf* aMessage) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  auto* message = FromFFI(aMessage);
  if (!parent->SendServerMessage(std::move(*message))) {
    NS_ERROR("SendServerMessage failed");
  }
}

extern void* wgpu_parent_weak_ptr_new(WGPUWebGPUParentPtr aParent) {
  auto* parent = static_cast<WebGPUParent*>(aParent);
  return new WeakPtr<WebGPUParent>(parent);
}

extern WGPUWebGPUParentPtr wgpu_parent_weak_ptr_get(void* aWeakPtr) {
  auto* weakPtr = static_cast<WeakPtr<WebGPUParent>*>(aWeakPtr);
  return weakPtr->get();
}

extern void wgpu_parent_weak_ptr_delete(void* aWeakPtr) {
  delete static_cast<WeakPtr<WebGPUParent>*>(aWeakPtr);
}

}  // namespace ffi

struct PendingSwapChainDrop {
  layers::RemoteTextureTxnType mTxnType;
  layers::RemoteTextureTxnId mTxnId;
};

class PresentationData {
  NS_INLINE_DECL_REFCOUNTING(PresentationData);

 public:
  WeakPtr<WebGPUParent> mParent;
  bool mUseSharedTextureInSwapChain;
  const RawId mDeviceId;
  const RawId mQueueId;
  Maybe<RawId> mLastSubmittedTextureId;
  // `WebGPUParent::DeviceCreateSwapChain` already validated that:
  //  - the format is either R8G8B8A8 or B8G8R8A8 (bpp = 4)
  //  - width and height are > 0
  //  - width * bpp (= stride) <= std::numeric_limits<uint32_t>::max()
  //  - width * bpp * height (= len) <= std::numeric_limits<size_t>::max()
  const layers::RGBDescriptor mDesc;

  uint64_t mSubmissionIndex = 0;

  std::deque<std::shared_ptr<SharedTexture>> mRecycledSharedTextures;

  std::unordered_set<layers::RemoteTextureId, layers::RemoteTextureId::HashFn>
      mWaitingReadbackTexturesForPresent;
  Maybe<PendingSwapChainDrop> mPendingSwapChainDrop;

  const uint32_t mBufferStride;
  const size_t mBufferSize;
  std::vector<RawId> mUnassignedBufferIds;
  std::vector<RawId> mAvailableBufferIds;
  std::vector<RawId> mQueuedBufferIds;

  bool mReadbackSnapshotCallbackCalled = false;

  PresentationData(WebGPUParent* aParent, bool aUseSharedTextureInSwapChain,
                   RawId aDeviceId, RawId aQueueId,
                   const layers::RGBDescriptor& aDesc, uint32_t aBufferStride,
                   size_t aBufferSize, const nsTArray<RawId>& aBufferIds)
      : mParent(aParent),
        mUseSharedTextureInSwapChain(aUseSharedTextureInSwapChain),
        mDeviceId(aDeviceId),
        mQueueId(aQueueId),
        mDesc(aDesc),
        mBufferStride(aBufferStride),
        mBufferSize(aBufferSize) {
    MOZ_COUNT_CTOR(PresentationData);

    for (const RawId id : aBufferIds) {
      mUnassignedBufferIds.push_back(id);
    }
  }

 private:
  ~PresentationData() { MOZ_COUNT_DTOR(PresentationData); }
};

WebGPUParent::WebGPUParent(const dom::ContentParentId& aContentId)
    : mContentId(aContentId), mContext(ffi::wgpu_server_new(this)) {
  mTimer.Start(base::TimeDelta::FromMilliseconds(POLL_TIME_MS), this,
               &WebGPUParent::MaintainDevices);
}

WebGPUParent::~WebGPUParent() = default;

void WebGPUParent::MaintainDevices() {
  ffi::wgpu_server_poll_all_devices(mContext.get(), false);
}

void WebGPUParent::PostAdapterRequestDevice(RawId aDeviceId) {
#if defined(XP_WIN)
  HANDLE handle =
      wgpu_server_get_device_fence_handle(mContext.get(), aDeviceId);
  if (handle) {
    RefPtr<gfx::FileHandleWrapper> fenceHandle =
        new gfx::FileHandleWrapper(UniqueFileHandle(handle));
    mDeviceFenceHandles.emplace(aDeviceId, std::move(fenceHandle));
  }
#endif
}

void WebGPUParent::PreDeviceDrop(RawId aDeviceId) {
  auto it = mDeviceFenceHandles.find(aDeviceId);
  if (it != mDeviceFenceHandles.end()) {
    mDeviceFenceHandles.erase(it);
  }
}

WebGPUParent::BufferMapData* WebGPUParent::GetBufferMapData(RawId aBufferId) {
  const auto iter = mSharedMemoryMap.find(aBufferId);
  if (iter == mSharedMemoryMap.end()) {
    return nullptr;
  }

  return &iter->second;
}

static const char* MapStatusString(ffi::WGPUBufferMapAsyncStatus status) {
  switch (status) {
    case ffi::WGPUBufferMapAsyncStatus_Success:
      return "Success";
    case ffi::WGPUBufferMapAsyncStatus_AlreadyMapped:
      return "Already mapped";
    case ffi::WGPUBufferMapAsyncStatus_MapAlreadyPending:
      return "Map is already pending";
    case ffi::WGPUBufferMapAsyncStatus_ContextLost:
      return "Context lost";
    case ffi::WGPUBufferMapAsyncStatus_Invalid:
      return "Invalid buffer";
    case ffi::WGPUBufferMapAsyncStatus_InvalidRange:
      return "Invalid range";
    case ffi::WGPUBufferMapAsyncStatus_InvalidAlignment:
      return "Invalid alignment";
    case ffi::WGPUBufferMapAsyncStatus_InvalidUsageFlags:
      return "Invalid usage flags";
    case ffi::WGPUBufferMapAsyncStatus_Error:
      return "Map failed";
  }

  MOZ_CRASH("Bad ffi::WGPUBufferMapAsyncStatus");
}

Maybe<MappedRangeGuard> WebGPUParent::LockMappedRange(
    ffi::WGPUBufferId aBufferId, uint64_t aStart, uint64_t aSize) {
  return MappedRangeGuard::Create(mContext.get(), aBufferId, aStart, aSize);
}

void WebGPUParent::MapCallback(uint8_t* aUserData,
                               ffi::WGPUBufferMapAsyncStatus aStatus) {
  auto req =
      std::unique_ptr<MapRequest>(reinterpret_cast<MapRequest*>(aUserData));

  if (!req->mParent) {
    return;
  }
  if (!req->mParent->CanSend()) {
    return;
  }

  if (aStatus != ffi::WGPUBufferMapAsyncStatus_Success) {
    auto error = nsPrintfCString("Mapping WebGPU buffer failed: %s",
                                 MapStatusString(aStatus));

    ffi::wgpu_server_send_buffer_map_error(req->mParent, req->mBufferId,
                                           &error);
    return;
  }

  auto* mapData = req->mParent->GetBufferMapData(req->mBufferId);

  // The buffer mapping callback can race with buffer.destroy()
  // since the callback is not directly called by wgpu-core; it's
  // called later by a task on the same thread as the WebGPU parent actor.
  if (!mapData) {
    auto error = nsCString("Mapping WebGPU buffer failed: Map aborted");
    ffi::wgpu_server_send_buffer_map_error(req->mParent, req->mBufferId,
                                           &error);
    return;
  }

  auto size = req->mSize;
  auto offset = req->mOffset;

  Maybe<MappedRangeGuard> maybeSrc =
      req->mParent->LockMappedRange(req->mBufferId, offset, size);

  // The buffer mapping callback can race with buffer.destroy() and
  // buffer.unmap() since the callback is not directly called by wgpu-core;
  // it's called later by a task on the same thread as the WebGPU parent
  // actor.
  if (!maybeSrc) {
    auto error = nsCString("Mapping WebGPU buffer failed: Map aborted");
    ffi::wgpu_server_send_buffer_map_error(req->mParent, req->mBufferId,
                                           &error);
    return;
  }

  const std::span<uint8_t> src = maybeSrc->AsSpan();

  if (req->mHostMap == ffi::WGPUHostMap_Read && size > 0) {
    auto shmSize = mapData->mShmem->Size();
    MOZ_RELEASE_ASSERT(offset <= shmSize);
    MOZ_RELEASE_ASSERT(size <= shmSize - offset);

    auto dst = mapData->mShmem->DataAsSpan<uint8_t>().Subspan(offset, size);
    memcpy(dst.data(), src.data(), size);
  }

  // Release the lock on the mapped range before we do anything else
  // interesting with the buffer or device.
  maybeSrc.reset();

  mapData->mMappedOffset = offset;
  mapData->mMappedSize = size;
  mapData->mIsMapped = true;

  bool is_writable = req->mHostMap == ffi::WGPUHostMap_Write;
  ffi::wgpu_server_send_buffer_map_success(req->mParent, req->mBufferId,
                                           is_writable, offset, size);
}

void WebGPUParent::BufferUnmap(RawId aDeviceId, RawId aBufferId, bool aFlush) {
  MOZ_LOG(sLogger, LogLevel::Info,
          ("RecvBufferUnmap %" PRIu64 " flush=%d\n", aBufferId, aFlush));

  auto* mapData = GetBufferMapData(aBufferId);
  if (!mapData) {
    return;
  }

  if (mapData->mIsMapped && aFlush) {
    uint64_t offset = mapData->mMappedOffset;
    uint64_t size = mapData->mMappedSize;

    // To avoid deadlock, this `MappedRangeGuard` must go out of scope
    // before we try to unmap the buffer.
    const Maybe<MappedRangeGuard> mapped =
        LockMappedRange(aBufferId, offset, size);

    // There may be nothing left to flush into: once a device is lost or
    // destroyed, wgpu-core destroys its buffers in `release_gpu_resources`
    // on poll.
    if (mapped) {
      auto shmSize = mapData->mShmem->Size();
      MOZ_RELEASE_ASSERT(offset <= shmSize);
      MOZ_RELEASE_ASSERT(size <= shmSize - offset);

      auto src = mapData->mShmem->DataAsSpan<uint8_t>().Subspan(offset, size);
      memcpy(mapped->AsSpan().data(), src.data(), size);
    }
  }

  ffi::wgpu_server_buffer_unmap(mContext.get(), aBufferId);

  mapData->mMappedOffset = 0;
  mapData->mMappedSize = 0;
  mapData->mIsMapped = false;

  if (!mapData->mHasMapFlags) {
    // We get here if the buffer was mapped at creation without map flags.
    // We don't need the shared memory anymore.
    DeallocBufferShmem(aBufferId);
  }
}

void WebGPUParent::DeallocBufferShmem(RawId aBufferId) {
  const auto iter = mSharedMemoryMap.find(aBufferId);
  if (iter != mSharedMemoryMap.end()) {
    mSharedMemoryMap.erase(iter);
  }
}

void WebGPUParent::RemoveSharedTexture(RawId aTextureId) {
  auto it = mSharedTextures.find(aTextureId);
  if (it != mSharedTextures.end()) {
    mSharedTextures.erase(it);
  }
}

const ExternalTextureSourceHost& WebGPUParent::GetExternalTextureSource(
    ffi::WGPUExternalTextureSourceId aId) const {
  return mExternalTextureSources.at(aId);
}

void WebGPUParent::DestroyExternalTextureSource(RawId aSourceId) {
  auto it = mExternalTextureSources.find(aSourceId);
  if (it != mExternalTextureSources.end()) {
    for (const auto viewId : it->second.ViewIds()) {
      ffi::wgpu_server_texture_view_drop(mContext.get(), viewId);
    }
    for (const auto textureId : it->second.TextureIds()) {
      ffi::wgpu_server_texture_destroy(mContext.get(), textureId);
      ffi::wgpu_server_texture_drop(mContext.get(), textureId);
    }
  }
}

void WebGPUParent::DropExternalTextureSource(RawId aSourceId) {
  auto it = mExternalTextureSources.find(aSourceId);
  if (it != mExternalTextureSources.end()) {
    mExternalTextureSources.erase(it);
  }
}

void WebGPUParent::QueueSubmit(RawId aQueueId,
                               Span<const RawId> aCommandBuffers,
                               Span<const RawId> aTextureIds,
                               Span<const RawId> aExternalTextureSourceIds) {
  for (const auto& sourceId : aExternalTextureSourceIds) {
    auto it = mExternalTextureSources.find(sourceId);
    if (it != mExternalTextureSources.end()) {
      auto& source = it->second;
      if (!source.OnBeforeQueueSubmit(this, aQueueId)) {
        // If the above call failed we cannot submit the command buffers, as
        // it would be invalid to read from the external textures.
        return;
      }
    }
  }

  // Must come after the bail-out above: the semaphores created here are
  // registered as pending signals on the queue, and only
  // wgpu_server_queue_submit() disposes of them.
  nsTArray<ffi::WGPUVkSemaphoreHandle> signalSemaphores;
  for (const auto& textureId : aTextureIds) {
    auto it = mSharedTextures.find(textureId);
    if (it != mSharedTextures.end()) {
      auto& sharedTexture = it->second;
      sharedTexture->onBeforeQueueSubmit(mContext.get(), aQueueId,
                                         signalSemaphores);
    }
  }

  auto index = ffi::wgpu_server_queue_submit(
      mContext.get(), aQueueId,
      {aCommandBuffers.Elements(), aCommandBuffers.Length()},
      {signalSemaphores.Elements(), signalSemaphores.Length()});
  // Check if index is valid. 0 means error.
  if (index != 0) {
    for (const auto& textureId : aTextureIds) {
      auto it = mSharedTextures.find(textureId);
      if (it != mSharedTextures.end()) {
        auto& sharedTexture = it->second;

        sharedTexture->SetSubmissionIndex(index);
        // Update mLastSubmittedTextureId
        auto ownerId = sharedTexture->GetOwnerId();
        const auto& lookup = mPresentationDataMap.find(ownerId);
        if (lookup != mPresentationDataMap.end()) {
          RefPtr<PresentationData> data = lookup->second.get();
          data->mLastSubmittedTextureId = Some(textureId);
        }
      }
    }
  }
}

void WebGPUParent::OnSubmittedWorkDoneCallback(uint8_t* userdata) {
  auto req = std::unique_ptr<OnSubmittedWorkDoneRequest>(
      reinterpret_cast<OnSubmittedWorkDoneRequest*>(userdata));
  if (!req->mParent) {
    return;
  }
  if (!req->mParent->CanSend()) {
    return;
  }

  ipc::ByteBuf bb;
  ffi::wgpu_server_pack_work_done(ToFFI(&bb), req->mQueueId);
  if (!req->mParent->SendServerMessage(std::move(bb))) {
    NS_ERROR("SendServerMessage failed");
  }
}

// TODO: proper destruction

void WebGPUParent::DeviceCreateSwapChain(
    RawId aDeviceId, RawId aQueueId, const layers::RGBDescriptor& aDesc,
    const nsTArray<RawId>& aBufferIds,
    const layers::RemoteTextureOwnerId& aOwnerId,
    bool aUseSharedTextureInSwapChain) {
  switch (aDesc.format()) {
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Invalid surface format!");
      return;
  }

  const auto bufferStrideWithMask =
      Device::BufferStrideWithMask(aDesc.size(), aDesc.format());
  if (!bufferStrideWithMask.isValid()) {
    MOZ_ASSERT_UNREACHABLE("Invalid width / buffer stride!");
    return;
  }

  constexpr uint32_t kBufferAlignmentMask = 0xff;
  const uint32_t bufferStride =
      bufferStrideWithMask.value() & ~kBufferAlignmentMask;

  const auto rows = CheckedInt<uint32_t>(aDesc.size().height);
  if (!rows.isValid()) {
    MOZ_ASSERT_UNREACHABLE("Invalid height!");
    return;
  }

  const auto bufferSize = CheckedInt<size_t>(rows.value()) * bufferStride;
  if (!bufferSize.isValid()) {
    MOZ_ASSERT_UNREACHABLE("Buffer size overflowed!");
    return;
  }

  if (!mRemoteTextureOwner) {
    mRemoteTextureOwner =
        MakeRefPtr<layers::RemoteTextureOwnerClient>(OtherChildID());
  }
  mRemoteTextureOwner->RegisterTextureOwner(aOwnerId);

  auto data = MakeRefPtr<PresentationData>(
      this, aUseSharedTextureInSwapChain, aDeviceId, aQueueId, aDesc,
      bufferStride, bufferSize.value(), aBufferIds);
  if (!mPresentationDataMap.emplace(aOwnerId, data).second) {
    NS_ERROR("External image is already registered as WebGPU canvas!");
  }
}

struct ReadbackPresentRequest {
  ReadbackPresentRequest(
      const ffi::WGPUGlobal* aContext, RefPtr<PresentationData>& aData,
      RefPtr<layers::RemoteTextureOwnerClient>& aRemoteTextureOwner,
      const layers::RemoteTextureId aTextureId,
      const layers::RemoteTextureOwnerId aOwnerId)
      : mContext(aContext),
        mData(aData),
        mRemoteTextureOwner(aRemoteTextureOwner),
        mTextureId(aTextureId),
        mOwnerId(aOwnerId) {}

  const ffi::WGPUGlobal* mContext;
  RefPtr<PresentationData> mData;
  RefPtr<layers::RemoteTextureOwnerClient> mRemoteTextureOwner;
  const layers::RemoteTextureId mTextureId;
  const layers::RemoteTextureOwnerId mOwnerId;
};

static void ReadbackPresentCallback(uint8_t* userdata,
                                    ffi::WGPUBufferMapAsyncStatus status) {
  UniquePtr<ReadbackPresentRequest> req(
      reinterpret_cast<ReadbackPresentRequest*>(userdata));

  const auto onExit = mozilla::MakeScopeExit([&]() {
    auto& waitingTextures = req->mData->mWaitingReadbackTexturesForPresent;
    auto it = waitingTextures.find(req->mTextureId);
    MOZ_ASSERT(it != waitingTextures.end());
    if (it != waitingTextures.end()) {
      waitingTextures.erase(it);
    }
    if (req->mData->mPendingSwapChainDrop.isSome() && waitingTextures.empty()) {
      if (req->mData->mParent) {
        auto& pendingDrop = req->mData->mPendingSwapChainDrop.ref();
        req->mData->mParent->SwapChainDrop(req->mOwnerId, pendingDrop.mTxnType,
                                           pendingDrop.mTxnId);
        req->mData->mPendingSwapChainDrop = Nothing();
      }
    }
  });

  if (!req->mRemoteTextureOwner->IsRegistered(req->mOwnerId)) {
    // SwapChain is already Destroyed
    return;
  }

  RefPtr<PresentationData> data = req->mData;
  // get the buffer ID
  RawId bufferId;
  {
    bufferId = data->mQueuedBufferIds.back();
    data->mQueuedBufferIds.pop_back();
  }

  // Ensure we'll make the bufferId available for reuse
  data->mAvailableBufferIds.push_back(bufferId);

  MOZ_LOG(sLogger, LogLevel::Info,
          ("ReadbackPresentCallback for buffer %" PRIu64 " status=%d\n",
           bufferId, status));
  // copy the data
  if (status == ffi::WGPUBufferMapAsyncStatus_Success) {
    const auto bufferSize = data->mBufferSize;

    Maybe<MappedRangeGuard> mapped =
        MappedRangeGuard::Create(req->mContext, bufferId, 0, bufferSize);

    // There may be nothing to read back: once a device is lost or
    // destroyed, wgpu-core destroys its buffers in `release_gpu_resources`
    // on poll.
    if (!mapped) {
      MOZ_LOG(sLogger, LogLevel::Info,
              ("ReadbackPresentCallback for buffer %" PRIu64
               " skipped: the readback buffer is gone\n",
               bufferId));
      return;
    }

    const auto size = data->mDesc.size();

    auto textureData =
        req->mRemoteTextureOwner->CreateOrRecycleBufferTextureData(
            size, data->mDesc.format(), req->mOwnerId);
    if (!textureData) {
      gfxCriticalNoteOnce << "Failed to allocate BufferTextureData";
      return;
    }
    layers::MappedTextureData mappedData;
    if (textureData && textureData->BorrowMappedData(mappedData)) {
      uint8_t* src = mapped->AsSpan().data();
      uint8_t* dst = mappedData.data;

      const size_t dst_stride = static_cast<size_t>(mappedData.stride);
      // `mappedData.stride` is computed via
      // `ImageDataSerializer::ComputeRGBStride` and returns 0 if it overflows
      MOZ_RELEASE_ASSERT(dst_stride != 0);

      const size_t src_stride = static_cast<size_t>(data->mBufferStride);
      const size_t bytesPerRow =
          static_cast<size_t>(data->mDesc.size().width) * 4;
      MOZ_RELEASE_ASSERT(src_stride >= bytesPerRow);
      MOZ_RELEASE_ASSERT(dst_stride >= bytesPerRow);

      // The height is in bounds for both buffers since we just requested a new
      // destination buffer with the same height of the source.
      for (auto row = 0; row < size.height; ++row) {
        memcpy(dst, src, bytesPerRow);
        if (bytesPerRow < dst_stride) {
          memset(dst + bytesPerRow, 0, dst_stride - bytesPerRow);
        }
        src += src_stride;
        dst += dst_stride;
      }
      req->mRemoteTextureOwner->PushTexture(req->mTextureId, req->mOwnerId,
                                            std::move(textureData));
    } else {
      NS_WARNING("WebGPU present skipped: the swapchain is resized!");
    }

    // Release the lock on the mapped range, so the unmap doesn't deadlock.
    mapped.reset();

    wgpu_server_buffer_unmap(req->mContext, bufferId);
  } else {
    // TODO: better handle errors
    NS_WARNING("WebGPU frame mapping failed!");
  }
}

struct ReadbackSnapshotRequest {
  ReadbackSnapshotRequest(const ffi::WGPUGlobal* aContext,
                          RefPtr<PresentationData>& aData,
                          ffi::WGPUBufferId aBufferId,
                          FrontBufferSnapshot& aSnapshot)
      : mContext(aContext),
        mData(aData),
        mBufferId(aBufferId),
        mSnapshot(aSnapshot) {}

  const ffi::WGPUGlobal* mContext;
  RefPtr<PresentationData> mData;
  const ffi::WGPUBufferId mBufferId;
  FrontBufferSnapshot& mSnapshot;
};

static void ReadbackSnapshotCallback(uint8_t* userdata,
                                     ffi::WGPUBufferMapAsyncStatus status) {
  UniquePtr<ReadbackSnapshotRequest> req(
      reinterpret_cast<ReadbackSnapshotRequest*>(userdata));

  RefPtr<PresentationData> data = req->mData;
  data->mReadbackSnapshotCallbackCalled = true;

  // Ensure we'll make the bufferId available for reuse
  data->mAvailableBufferIds.push_back(req->mBufferId);

  MOZ_LOG(sLogger, LogLevel::Info,
          ("ReadbackSnapshotCallback for buffer %" PRIu64 " status=%d\n",
           req->mBufferId, status));
  if (status != ffi::WGPUBufferMapAsyncStatus_Success) {
    return;
  }
  // copy the data
  const auto bufferSize = data->mBufferSize;

  Maybe<MappedRangeGuard> mapped =
      MappedRangeGuard::Create(req->mContext, req->mBufferId, 0, bufferSize);

  // There may be nothing to read back: once a device is lost or
  // destroyed, wgpu-core destroys its buffers in `release_gpu_resources`
  // on poll.
  if (!mapped) {
    MOZ_LOG(sLogger, LogLevel::Info,
            ("ReadbackSnapshotCallback for buffer %" PRIu64
             " skipped: the readback buffer is gone\n",
             req->mBufferId));
    return;
  }

  const auto& descSize = data->mDesc.size();
  const size_t bytesPerRow = static_cast<size_t>(descSize.width) * 4;

  const size_t src_stride = static_cast<size_t>(data->mBufferStride);
  MOZ_RELEASE_ASSERT(src_stride >= bytesPerRow);

  auto handle =
      ipc::shared_memory::Create(bytesPerRow * size_t(descSize.height));
  auto mapping = handle.Map();
  if (handle && mapping) {
    uint8_t* src = mapped->AsSpan().data();
    uint8_t* dst = mapping.DataAs<uint8_t>();

    for (auto row = 0; row < descSize.height; ++row) {
      memcpy(dst, src, bytesPerRow);
      src += src_stride;
      dst += bytesPerRow;
    }
    req->mSnapshot = {std::move(handle), uint32_t(descSize.width),
                      uint32_t(descSize.height), bytesPerRow};
  }

  // Release the lock on the mapped range, so the unmap doesn't deadlock.
  mapped.reset();

  wgpu_server_buffer_unmap(req->mContext, req->mBufferId);
}

ipc::IPCResult WebGPUParent::GetFrontBufferSnapshot(
    const layers::RemoteTextureOwnerId& aOwnerId,
    const RawId& aCommandEncoderId, const RawId& aCommandBufferId,
    FrontBufferSnapshot& aSnapshot) {
  const auto& lookup = mPresentationDataMap.find(aOwnerId);
  if (lookup == mPresentationDataMap.end()) {
    // This can happen if `GPUCanvasContext.configure()` was called with an
    // invalid configuration.
    NS_WARNING("WebGPU reading back an invalid canvas");
    return IPC_OK();
  }

  RefPtr<PresentationData> data = lookup->second.get();
  data->mReadbackSnapshotCallbackCalled = false;

  const auto& size = data->mDesc.size();

  if (size.width > INT16_MAX || size.height > INT16_MAX) {
    return IPC_OK();
  }

  if (data->mLastSubmittedTextureId.isNothing()) {
    // No commands referencing the canvas have ever been submitted, so it's
    // blank.
    const size_t bytesPerRow = static_cast<size_t>(size.width) * 4;
    auto handle = ipc::shared_memory::Create(bytesPerRow * size_t(size.height));
    auto mapping = handle.Map();
    if (handle && mapping) {
      memset(mapping.DataAs<uint8_t>(), 0, mapping.Size());
    }
    aSnapshot = {std::move(handle), uint32_t(size.width), uint32_t(size.height),
                 bytesPerRow};
    return IPC_OK();
  }

  auto it = mSharedTextures.find(data->mLastSubmittedTextureId.ref());
  // Shared texture is already invalid and posted to RemoteTextureMap
  if (it == mSharedTextures.end()) {
    if (!mRemoteTextureOwner || !mRemoteTextureOwner->IsRegistered(aOwnerId)) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return IPC_OK();
    }
    if (!data->mUseSharedTextureInSwapChain) {
      ffi::wgpu_server_device_poll(mContext.get(), data->mDeviceId, true);
    }
    aSnapshot = mRemoteTextureOwner->GetLatestBufferSnapshot(aOwnerId);
    return IPC_OK();
  }

  // Readback synchronously

  RawId bufferId = 0;
  const auto bufferSize = data->mBufferSize;

  // The swap chain owns exactly MAX_SWAPCHAIN_BUFFER_COUNT staging-buffer IDs.
  // Each ID must always be in exactly one of mUnassignedBufferIds,
  // mAvailableBufferIds, mQueuedBufferIds, or a pending
  // ReadbackSnapshotCallback. In error cases, be sure to release the buffer ID
  // back to the available pool.
  const auto bufferIdGuard = mozilla::MakeScopeExit([&] {
    if (bufferId) {
      data->mAvailableBufferIds.push_back(bufferId);
    }
  });

  // step 1: find an available staging buffer, or create one
  {
    if (!data->mAvailableBufferIds.empty()) {
      bufferId = data->mAvailableBufferIds.back();
      data->mAvailableBufferIds.pop_back();
    } else if (!data->mUnassignedBufferIds.empty()) {
      bufferId = data->mUnassignedBufferIds.back();
      data->mUnassignedBufferIds.pop_back();

      ffi::WGPUBufferUsagesWebGPU usage =
          dom::GPUBufferUsage_Binding::COPY_DST |
          dom::GPUBufferUsage_Binding::MAP_READ;

      bool succeeded = ffi::wgpu_server_device_create_buffer(
          mContext.get(), data->mDeviceId, bufferId, bufferSize, usage);
      if (!succeeded) {
        data->mUnassignedBufferIds.push_back(bufferId);
        bufferId = 0;
      }
    }
  }

  MOZ_LOG(sLogger, LogLevel::Info,
          ("GetFrontBufferSnapshot with buffer %" PRIu64 "\n", bufferId));
  if (!bufferId) {
    // TODO: add a warning - no buffer are available!
    return IPC_OK();
  }

  // step 3: submit a copy command for the frame

  bool succeeded = ffi::wgpu_server_submit_copy_texture_to_buffer(
      mContext.get(), data->mDeviceId, data->mQueueId, aCommandEncoderId,
      aCommandBufferId, data->mLastSubmittedTextureId.ref(),
      static_cast<uint32_t>(size.width), static_cast<uint32_t>(size.height),
      bufferId, data->mBufferStride);
  if (!succeeded) {
    return IPC_OK();
  }

  auto snapshotRequest = std::make_unique<ReadbackSnapshotRequest>(
      mContext.get(), data, bufferId, aSnapshot);

  ffi::WGPUBufferMapAsyncStatus status = ffi::wgpu_server_buffer_map_blocking(
      mContext.get(), data->mDeviceId, bufferId, 0, bufferSize,
      ffi::WGPUHostMap_Read);
  ReadbackSnapshotCallback(
      reinterpret_cast<uint8_t*>(snapshotRequest.release()), status);
  // bufferId was transferred to ReadbackSnapshotCallback.
  bufferId = 0;

  // Check if ReadbackSnapshotCallback is called.
  MOZ_RELEASE_ASSERT(data->mReadbackSnapshotCallbackCalled == true);

  return IPC_OK();
}

void WebGPUParent::PostSharedTexture(
    const std::shared_ptr<SharedTexture>&& aSharedTexture,
    const layers::RemoteTextureId aRemoteTextureId,
    const layers::RemoteTextureOwnerId aOwnerId) {
  const auto& lookup = mPresentationDataMap.find(aOwnerId);
  if (lookup == mPresentationDataMap.end() || !mRemoteTextureOwner ||
      !mRemoteTextureOwner->IsRegistered(aOwnerId)) {
    NS_WARNING("WebGPU presenting on an invalid or destroyed swap chain!");
    return;
  }

  const auto surfaceFormat = gfx::SurfaceFormat::B8G8R8A8;
  const auto size = aSharedTexture->GetSize();

  RefPtr<PresentationData> data = lookup->second.get();

  Maybe<layers::SurfaceDescriptor> desc = aSharedTexture->ToSurfaceDescriptor();
  if (!desc) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  mRemoteTextureOwner->PushTexture(aRemoteTextureId, aOwnerId, aSharedTexture,
                                   size, surfaceFormat, *desc);

  auto recycledTexture = mRemoteTextureOwner->GetRecycledSharedTexture(
      size, surfaceFormat, desc->type(), aOwnerId);
  if (recycledTexture) {
    recycledTexture->CleanForRecycling();
    data->mRecycledSharedTextures.push_back(recycledTexture);
  }
}

RefPtr<gfx::FileHandleWrapper> WebGPUParent::GetDeviceFenceHandle(
    const RawId aDeviceId) {
  auto it = mDeviceFenceHandles.find(aDeviceId);
  if (it == mDeviceFenceHandles.end()) {
    return nullptr;
  }
  return it->second;
}

void WebGPUParent::SwapChainPresent(
    RawId aTextureId, RawId aCommandEncoderId, RawId aCommandBufferId,
    const layers::RemoteTextureId& aRemoteTextureId,
    const layers::RemoteTextureOwnerId& aOwnerId) {
  // step 0: get the data associated with the swapchain
  const auto& lookup = mPresentationDataMap.find(aOwnerId);
  if (lookup == mPresentationDataMap.end() || !mRemoteTextureOwner ||
      !mRemoteTextureOwner->IsRegistered(aOwnerId)) {
    NS_WARNING("WebGPU presenting on an invalid or destroyed swap chain!");
    return;
  }

  RefPtr<PresentationData> data = lookup->second.get();

  if (data->mUseSharedTextureInSwapChain) {
    auto it = mSharedTextures.find(aTextureId);
    if (it == mSharedTextures.end()) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return;
    }
    std::shared_ptr<SharedTexture> sharedTexture = it->second;
    mSharedTextures.erase(it);

    if (!sharedTexture->IsSubmitted()) {
      mRemoteTextureOwner->PushDummyTexture(aRemoteTextureId, aOwnerId);
      gfxCriticalNoteOnce << "Dummy texture is submitted";
      return;
    }

    MOZ_ASSERT(sharedTexture->GetOwnerId() == aOwnerId);

    PostSharedTexture(std::move(sharedTexture), aRemoteTextureId, aOwnerId);
    return;
  }

  RawId bufferId = 0;
  const auto& size = data->mDesc.size();
  const auto bufferSize = data->mBufferSize;

  // The swap chain owns exactly MAX_SWAPCHAIN_BUFFER_COUNT staging-buffer IDs.
  // Each ID must always be in exactly one of mUnassignedBufferIds,
  // mAvailableBufferIds, mQueuedBufferIds, or a pending
  // ReadbackPresentCallback. In error cases, be sure to release the buffer ID
  // back to the available pool.
  const auto bufferIdGuard = mozilla::MakeScopeExit([&] {
    if (bufferId) {
      data->mAvailableBufferIds.push_back(bufferId);
    }
  });

  // step 1: find an available staging buffer, or create one
  {
    if (!data->mAvailableBufferIds.empty()) {
      bufferId = data->mAvailableBufferIds.back();
      data->mAvailableBufferIds.pop_back();
    } else if (!data->mUnassignedBufferIds.empty()) {
      bufferId = data->mUnassignedBufferIds.back();
      data->mUnassignedBufferIds.pop_back();

      ffi::WGPUBufferUsagesWebGPU usage =
          dom::GPUBufferUsage_Binding::COPY_DST |
          dom::GPUBufferUsage_Binding::MAP_READ;

      bool succeeded = ffi::wgpu_server_device_create_buffer(
          mContext.get(), data->mDeviceId, bufferId, bufferSize, usage);
      if (!succeeded) {
        data->mUnassignedBufferIds.push_back(bufferId);
        bufferId = 0;
      }
    }
  }

  MOZ_LOG(sLogger, LogLevel::Info,
          ("RecvSwapChainPresent with buffer %" PRIu64 "\n", bufferId));
  if (!bufferId) {
    // TODO: add a warning - no buffer are available!
    return;
  }

  // step 3: submit a copy command for the frame
  bool succeeded = ffi::wgpu_server_submit_copy_texture_to_buffer(
      mContext.get(), data->mDeviceId, data->mQueueId, aCommandEncoderId,
      aCommandBufferId, aTextureId, static_cast<uint32_t>(size.width),
      static_cast<uint32_t>(size.height), bufferId, data->mBufferStride);
  if (!succeeded) {
    return;
  }

  auto& waitingTextures = data->mWaitingReadbackTexturesForPresent;
  auto it = waitingTextures.find(aRemoteTextureId);
  MOZ_ASSERT(it == waitingTextures.end());
  if (it == waitingTextures.end()) {
    waitingTextures.emplace(aRemoteTextureId);
  }

  // step 4: request the pixels to be copied into the shared texture
  // TODO: this isn't strictly necessary. When WR wants to Lock() the external
  // texture,
  // we can just give it the contents of the last mapped buffer instead of the
  // copy.
  auto presentRequest = std::make_unique<ReadbackPresentRequest>(
      mContext.get(), data, mRemoteTextureOwner, aRemoteTextureId, aOwnerId);

  ffi::WGPUBufferMapClosure closure = {
      &ReadbackPresentCallback,
      reinterpret_cast<uint8_t*>(presentRequest.release())};

  data->mQueuedBufferIds.insert(data->mQueuedBufferIds.begin(), bufferId);

  ffi::wgpu_server_buffer_map(mContext.get(), bufferId, 0, bufferSize,
                              ffi::WGPUHostMap_Read, closure);
  // bufferId was transferred to ReadbackPresentCallback.
  bufferId = 0;
}

void WebGPUParent::SwapChainDrop(const layers::RemoteTextureOwnerId& aOwnerId,
                                 layers::RemoteTextureTxnType aTxnType,
                                 layers::RemoteTextureTxnId aTxnId) {
  const auto& lookup = mPresentationDataMap.find(aOwnerId);
  if (lookup == mPresentationDataMap.end()) {
    NS_WARNING("drop invalid or destroyed swap chain!");
    return;
  }

  RefPtr<PresentationData> data = lookup->second.get();

  auto waitingCount = data->mWaitingReadbackTexturesForPresent.size();
  if (waitingCount > 0) {
    // Defer SwapChainDrop until readback complete
    data->mPendingSwapChainDrop = Some(PendingSwapChainDrop{aTxnType, aTxnId});
    return;
  }

  if (mRemoteTextureOwner) {
    if (aTxnType && aTxnId) {
      mRemoteTextureOwner->WaitForTxn(aOwnerId, aTxnType, aTxnId);
    }
    mRemoteTextureOwner->UnregisterTextureOwner(aOwnerId);
  }

  mPresentationDataMap.erase(lookup);

  for (const auto bid : data->mAvailableBufferIds) {
    ffi::wgpu_server_buffer_drop(mContext.get(), bid);
    data->mUnassignedBufferIds.push_back(bid);
  }
  for (const auto bid : data->mQueuedBufferIds) {
    ffi::wgpu_server_buffer_drop(mContext.get(), bid);
    data->mUnassignedBufferIds.push_back(bid);
  }

  ipc::ByteBuf bb;
  ffi::wgpu_server_pack_free_swap_chain_buffer_ids(
      ToFFI(&bb),
      {data->mUnassignedBufferIds.data(), data->mUnassignedBufferIds.size()});
  if (!SendServerMessage(std::move(bb))) {
    NS_ERROR("SendServerMessage failed");
  }
}

void WebGPUParent::ActorDestroy(ActorDestroyReason aWhy) {
  mTimer.Stop();
  mPresentationDataMap.clear();
  if (mRemoteTextureOwner) {
    mRemoteTextureOwner->UnregisterAllTextureOwners();
    mRemoteTextureOwner = nullptr;
  }
  mContext = nullptr;
}

ipc::IPCResult WebGPUParent::RecvMessages(
    uint32_t nrOfMessages, ipc::ByteBuf&& aSerializedMessages,
    nsTArray<ipc::ByteBuf>&& aDataBuffers,
    nsTArray<MutableSharedMemoryHandle>&& aShmems) {
  MOZ_ASSERT(mTempMappings.empty());

  mTempMappings.reserve(aShmems.Length());

  nsTArray<ffi::WGPUFfiSlice_u8> shmem_mappings(aShmems.Length());

  for (const auto& shmem : aShmems) {
    auto mapping = shmem.Map();

    auto* ptr = mapping.DataAs<uint8_t>();
    auto len = mapping.Size();
    ffi::WGPUFfiSlice_u8 byte_slice{ptr, len};
    shmem_mappings.AppendElement(std::move(byte_slice));

    // `aShmem` may be an invalid handle, however this will simply result in an
    // invalid mapping with 0 size, which we use safely.
    mTempMappings.push_back(
        std::make_shared<ipc::SharedMemoryMapping>(std::move(mapping)));
  }

  ffi::WGPUFfiSlice_ByteBuf data_buffers{ToFFI(aDataBuffers.Elements()),
                                         aDataBuffers.Length()};

  ffi::WGPUFfiSlice_FfiSlice_u8 shmem_mapping_slices{shmem_mappings.Elements(),
                                                     shmem_mappings.Length()};

  ffi::wgpu_server_messages(mContext.get(), nrOfMessages,
                            ToFFI(&aSerializedMessages), data_buffers,
                            shmem_mapping_slices);

  mTempMappings.clear();

  return IPC_OK();
}

ipc::IPCResult WebGPUParent::RecvCreateExternalTextureSource(
    RawId aDeviceId, RawId aQueueId, RawId aExternalTextureSourceId,
    const ExternalTextureSourceDescriptor& aDesc) {
  MOZ_RELEASE_ASSERT(mExternalTextureSources.find(aExternalTextureSourceId) ==
                     mExternalTextureSources.end());
  mExternalTextureSources.emplace(
      aExternalTextureSourceId,
      ExternalTextureSourceHost::Create(this, aDeviceId, aQueueId, aDesc));

  return IPC_OK();
}

bool WebGPUParent::UseSharedTextureForSwapChain(
    ffi::WGPUSwapChainId aSwapChainId) {
  auto ownerId = layers::RemoteTextureOwnerId{aSwapChainId._0};
  const auto& lookup = mPresentationDataMap.find(ownerId);
  if (lookup == mPresentationDataMap.end()) {
    NS_WARNING("WebGPU presenting on an invalid or destroyed swap chain!");
    return false;
  }

  RefPtr<PresentationData> data = lookup->second.get();

  return data->mUseSharedTextureInSwapChain;
}

void WebGPUParent::DisableSharedTextureForSwapChain(
    ffi::WGPUSwapChainId aSwapChainId) {
  auto ownerId = layers::RemoteTextureOwnerId{aSwapChainId._0};
  const auto& lookup = mPresentationDataMap.find(ownerId);
  if (lookup == mPresentationDataMap.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  RefPtr<PresentationData> data = lookup->second.get();

  if (data->mUseSharedTextureInSwapChain) {
    gfxCriticalNote << "Disable SharedTexture for SwapChain:  "
                    << aSwapChainId._0;
  }

  data->mUseSharedTextureInSwapChain = false;
}

static bool SwapChainFormatMatches(
    gfx::SurfaceFormat aSurfaceFormat,
    const ffi::WGPUTextureFormat& aTextureFormat) {
  switch (aSurfaceFormat) {
    case gfx::SurfaceFormat::B8G8R8A8:
      return aTextureFormat.tag == ffi::WGPUTextureFormat_Bgra8Unorm ||
             aTextureFormat.tag == ffi::WGPUTextureFormat_Bgra8UnormSrgb;
    case gfx::SurfaceFormat::R8G8B8A8:
      return aTextureFormat.tag == ffi::WGPUTextureFormat_Rgba8Unorm ||
             aTextureFormat.tag == ffi::WGPUTextureFormat_Rgba8UnormSrgb;
    default:
      return false;
  }
}

bool WebGPUParent::EnsureSharedTextureForSwapChain(
    ffi::WGPUSwapChainId aSwapChainId, ffi::WGPUDeviceId aDeviceId,
    ffi::WGPUTextureId aTextureId, uint32_t aWidth, uint32_t aHeight,
    struct ffi::WGPUTextureFormat aFormat, ffi::WGPUTextureUsages aUsage) {
  auto ownerId = layers::RemoteTextureOwnerId{aSwapChainId._0};
  const auto& lookup = mPresentationDataMap.find(ownerId);
  if (lookup == mPresentationDataMap.end()) {
    gfxWarningOnce() << "invalid swap chain";
    return false;
  }

  RefPtr<PresentationData> data = lookup->second.get();
  if (!data->mUseSharedTextureInSwapChain) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return false;
  }

  MOZ_RELEASE_ASSERT(aWidth == static_cast<uint32_t>(data->mDesc.size().width));
  MOZ_RELEASE_ASSERT(aHeight ==
                     static_cast<uint32_t>(data->mDesc.size().height));
  MOZ_RELEASE_ASSERT(SwapChainFormatMatches(data->mDesc.format(), aFormat));

  // Recycled SharedTexture if it exists.
  if (!data->mRecycledSharedTextures.empty()) {
    std::shared_ptr<SharedTexture> texture =
        data->mRecycledSharedTextures.front();
    // Check if the texture is recyclable.
    if (texture->mWidth == aWidth && texture->mHeight == aHeight &&
        texture->mFormat.tag == aFormat.tag && texture->mUsage == aUsage) {
      texture->SetOwnerId(ownerId);
      data->mRecycledSharedTextures.pop_front();
      mSharedTextures.emplace(aTextureId, texture);
      return true;
    }
    data->mRecycledSharedTextures.clear();
  }

  auto sharedTexture = CreateSharedTexture(ownerId, aDeviceId, aTextureId,
                                           aWidth, aHeight, aFormat, aUsage);
  return static_cast<bool>(sharedTexture);
}

void WebGPUParent::EnsureSharedTextureForReadBackPresent(
    ffi::WGPUSwapChainId aSwapChainId, ffi::WGPUDeviceId aDeviceId,
    ffi::WGPUTextureId aTextureId, uint32_t aWidth, uint32_t aHeight,
    struct ffi::WGPUTextureFormat aFormat, ffi::WGPUTextureUsages aUsage) {
  auto ownerId = layers::RemoteTextureOwnerId{aSwapChainId._0};
  const auto& lookup = mPresentationDataMap.find(ownerId);
  if (lookup == mPresentationDataMap.end()) {
    gfxWarningOnce() << "invalid swap chain";
    return;
  }

  RefPtr<PresentationData> data = lookup->second.get();
  if (data->mUseSharedTextureInSwapChain) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  MOZ_RELEASE_ASSERT(aWidth == static_cast<uint32_t>(data->mDesc.size().width));
  MOZ_RELEASE_ASSERT(aHeight ==
                     static_cast<uint32_t>(data->mDesc.size().height));
  MOZ_RELEASE_ASSERT(SwapChainFormatMatches(data->mDesc.format(), aFormat));

  UniquePtr<SharedTexture> texture =
      SharedTextureReadBackPresent::Create(aWidth, aHeight, aFormat, aUsage);
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  texture->SetOwnerId(ownerId);
  std::shared_ptr<SharedTexture> shared(texture.release());
  mSharedTextures[aTextureId] = std::move(shared);
}

std::shared_ptr<SharedTexture> WebGPUParent::CreateSharedTexture(
    const layers::RemoteTextureOwnerId& aOwnerId, ffi::WGPUDeviceId aDeviceId,
    ffi::WGPUTextureId aTextureId, uint32_t aWidth, uint32_t aHeight,
    const struct ffi::WGPUTextureFormat aFormat,
    ffi::WGPUTextureUsages aUsage) {
  MOZ_RELEASE_ASSERT(mSharedTextures.find(aTextureId) == mSharedTextures.end());

  UniquePtr<SharedTexture> texture =
      SharedTexture::Create(this, aDeviceId, aWidth, aHeight, aFormat, aUsage);
  if (!texture) {
    return nullptr;
  }

  texture->SetOwnerId(aOwnerId);
  std::shared_ptr<SharedTexture> shared(texture.release());
  mSharedTextures.emplace(aTextureId, shared);

  return shared;
}

std::shared_ptr<SharedTexture> WebGPUParent::GetSharedTexture(
    ffi::WGPUTextureId aId) {
  auto it = mSharedTextures.find(aId);
  if (it == mSharedTextures.end()) {
    return nullptr;
  }
  return it->second;
}

#if defined(XP_WIN)
/* static */
Maybe<ffi::WGPUFfiLUID> WebGPUParent::GetCompositorDeviceLuid() {
  const RefPtr<ID3D11Device> d3d11Device =
      gfx::DeviceManagerDx::Get()->GetCompositorDevice();
  if (!d3d11Device) {
    gfxCriticalNoteOnce << "CompositorDevice does not exist";
    return Nothing();
  }

  RefPtr<IDXGIDevice> dxgiDevice;
  d3d11Device->QueryInterface((IDXGIDevice**)getter_AddRefs(dxgiDevice));

  RefPtr<IDXGIAdapter> dxgiAdapter;
  dxgiDevice->GetAdapter(getter_AddRefs(dxgiAdapter));

  DXGI_ADAPTER_DESC desc;
  if (FAILED(dxgiAdapter->GetDesc(&desc))) {
    gfxCriticalNoteOnce << "Failed to get DXGI_ADAPTER_DESC";
    return Nothing();
  }

  return Some(
      ffi::WGPUFfiLUID{desc.AdapterLuid.LowPart, desc.AdapterLuid.HighPart});
}
#endif

}  // namespace mozilla::webgpu
