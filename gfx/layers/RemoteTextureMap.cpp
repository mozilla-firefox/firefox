/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/RemoteTextureMap.h"

#include <algorithm>
#include <vector>

#include "CompositableHost.h"
#include "ImageDataSerializer.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/AsyncImagePipelineManager.h"
#include "mozilla/layers/BufferTexture.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/RemoteTextureHostWrapper.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/WebRenderTextureHost.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/webgpu/SharedTexture.h"
#include "mozilla/webrender/RenderThread.h"
#include "SharedSurface.h"

namespace mozilla::layers {

RemoteTextureRecycleBin::RemoteTextureRecycleBin(bool aIsShared)
    : mIsShared(aIsShared) {}

RemoteTextureRecycleBin::~RemoteTextureRecycleBin() = default;

RemoteTextureOwnerClient::RemoteTextureOwnerClient(
    const base::ProcessId aForPid)
    : mForPid(aForPid) {}

RemoteTextureOwnerClient::~RemoteTextureOwnerClient() = default;

bool RemoteTextureOwnerClient::IsRegistered(
    const RemoteTextureOwnerId aOwnerId) {
  auto it = mOwnerIds.find(aOwnerId);
  if (it == mOwnerIds.end()) {
    return false;
  }
  return true;
}

void RemoteTextureOwnerClient::RegisterTextureOwner(
    const RemoteTextureOwnerId aOwnerId, bool aSharedRecycling) {
  MOZ_ASSERT(mOwnerIds.find(aOwnerId) == mOwnerIds.end());
  mOwnerIds.emplace(aOwnerId);
  RefPtr<RemoteTextureRecycleBin> recycleBin;
  if (aSharedRecycling) {
    if (!mSharedRecycleBin) {
      mSharedRecycleBin = MakeRefPtr<RemoteTextureRecycleBin>(true);
    }
    recycleBin = mSharedRecycleBin;
  }
  RemoteTextureMap::Get()->RegisterTextureOwner(aOwnerId, mForPid, recycleBin);
}

void RemoteTextureOwnerClient::UnregisterTextureOwner(
    const RemoteTextureOwnerId aOwnerId) {
  auto it = mOwnerIds.find(aOwnerId);
  if (it == mOwnerIds.end()) {
    return;
  }
  mOwnerIds.erase(it);
  RemoteTextureMap::Get()->UnregisterTextureOwner(aOwnerId, mForPid);
}

void RemoteTextureOwnerClient::UnregisterAllTextureOwners() {
  if (mOwnerIds.empty()) {
    return;
  }
  RemoteTextureMap::Get()->UnregisterTextureOwners(mOwnerIds, mForPid);
  mOwnerIds.clear();
  mSharedRecycleBin = nullptr;
}

bool RemoteTextureOwnerClient::WaitForTxn(const RemoteTextureOwnerId aOwnerId,
                                          RemoteTextureTxnType aTxnType,
                                          RemoteTextureTxnId aTxnId) {
  auto it = mOwnerIds.find(aOwnerId);
  if (it == mOwnerIds.end() || !aTxnType || !aTxnId) {
    return false;
  }
  return RemoteTextureMap::Get()->WaitForTxn(aOwnerId, mForPid, aTxnType,
                                             aTxnId);
}

void RemoteTextureOwnerClient::ClearRecycledTextures() {
  RemoteTextureMap::Get()->ClearRecycledTextures(mOwnerIds, mForPid,
                                                 mSharedRecycleBin);
}

void RemoteTextureOwnerClient::NotifyContextLost(
    const RemoteTextureOwnerIdSet* aOwnerIds) {
  if (aOwnerIds) {
    for (const auto& id : *aOwnerIds) {
      if (mOwnerIds.find(id) == mOwnerIds.end()) {
        MOZ_ASSERT_UNREACHABLE("owner id not registered by client");
        return;
      }
    }
  } else {
    aOwnerIds = &mOwnerIds;
  }
  if (aOwnerIds->empty()) {
    return;
  }
  RemoteTextureMap::Get()->NotifyContextLost(*aOwnerIds, mForPid);
}

void RemoteTextureOwnerClient::NotifyContextRestored(
    const RemoteTextureOwnerIdSet* aOwnerIds) {
  if (aOwnerIds) {
    for (const auto& id : *aOwnerIds) {
      if (mOwnerIds.find(id) == mOwnerIds.end()) {
        MOZ_ASSERT_UNREACHABLE("owner id not registered by client");
        return;
      }
    }
  } else {
    aOwnerIds = &mOwnerIds;
  }
  if (aOwnerIds->empty()) {
    return;
  }
  RemoteTextureMap::Get()->NotifyContextRestored(*aOwnerIds, mForPid);
}

void RemoteTextureOwnerClient::PushTexture(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId,
    UniquePtr<TextureData>&& aTextureData) {
  MOZ_ASSERT(IsRegistered(aOwnerId));

  RefPtr<TextureHost> textureHost = RemoteTextureMap::CreateRemoteTexture(
      aTextureData.get(), TextureFlags::DEFAULT);
  if (!textureHost) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  RemoteTextureMap::Get()->PushTexture(aTextureId, aOwnerId, mForPid,
                                       std::move(aTextureData), textureHost,
                                       /* aResourceWrapper */ nullptr);
}

void RemoteTextureOwnerClient::PushTexture(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId,
    const std::shared_ptr<gl::SharedSurface>& aSharedSurface,
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat,
    const SurfaceDescriptor& aDesc) {
  MOZ_ASSERT(IsRegistered(aOwnerId));

  UniquePtr<TextureData> textureData =
      MakeUnique<SharedSurfaceTextureData>(aDesc, aFormat, aSize);
  RefPtr<TextureHost> textureHost = RemoteTextureMap::CreateRemoteTexture(
      textureData.get(), TextureFlags::DEFAULT);
  if (!textureHost) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  RemoteTextureMap::Get()->PushTexture(
      aTextureId, aOwnerId, mForPid, std::move(textureData), textureHost,
      SharedResourceWrapper::SharedSurface(aSharedSurface));
}

void RemoteTextureOwnerClient::PushTexture(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId,
    const std::shared_ptr<webgpu::SharedTexture>& aSharedTexture,
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat,
    const SurfaceDescriptor& aDesc) {
  MOZ_ASSERT(IsRegistered(aOwnerId));

  UniquePtr<TextureData> textureData =
      MakeUnique<SharedSurfaceTextureData>(aDesc, aFormat, aSize);
  RefPtr<TextureHost> textureHost = RemoteTextureMap::CreateRemoteTexture(
      textureData.get(), TextureFlags::DEFAULT);
  if (!textureHost) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  RemoteTextureMap::Get()->PushTexture(
      aTextureId, aOwnerId, mForPid, std::move(textureData), textureHost,
      SharedResourceWrapper::SharedTexture(aSharedTexture));
}

void RemoteTextureOwnerClient::PushDummyTexture(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId) {
  MOZ_ASSERT(IsRegistered(aOwnerId));

  auto flags = TextureFlags::DEALLOCATE_CLIENT | TextureFlags::REMOTE_TEXTURE |
               TextureFlags::DUMMY_TEXTURE;
  auto* rawData = BufferTextureData::Create(
      gfx::IntSize(1, 1), gfx::SurfaceFormat::B8G8R8A8, gfx::ColorSpace2::SRGB,
      gfx::TransferFunction::SRGB, gfx::BackendType::SKIA,
      LayersBackend::LAYERS_WR, flags, ALLOC_DEFAULT, nullptr);
  if (!rawData) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  auto textureData = UniquePtr<TextureData>(rawData);

  RefPtr<TextureHost> textureHost = RemoteTextureMap::CreateRemoteTexture(
      textureData.get(), TextureFlags::DUMMY_TEXTURE);
  if (!textureHost) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  RemoteTextureMap::Get()->PushTexture(aTextureId, aOwnerId, mForPid,
                                       std::move(textureData), textureHost,
                                       /* aResourceWrapper */ nullptr);
}

void RemoteTextureOwnerClient::GetLatestBufferSnapshot(
    const RemoteTextureOwnerId aOwnerId, const mozilla::ipc::Shmem& aDestShmem,
    const gfx::IntSize& aDestSize, size_t aDestStride) {
  MOZ_ASSERT(IsRegistered(aOwnerId));
  RemoteTextureMap::Get()->GetLatestBufferSnapshot(
      aOwnerId, mForPid, aDestShmem, aDestSize, aDestStride);
}

UniquePtr<TextureData> RemoteTextureOwnerClient::GetRecycledTextureData(
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat,
    TextureType aTextureType, RemoteTextureOwnerId aOwnerId) {
  return RemoteTextureMap::Get()->GetRecycledTextureData(
      aOwnerId, mForPid, mSharedRecycleBin, aSize, aFormat, aTextureType);
}

UniquePtr<TextureData>
RemoteTextureOwnerClient::CreateOrRecycleBufferTextureData(
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat,
    RemoteTextureOwnerId aOwnerId) {
  auto texture =
      GetRecycledTextureData(aSize, aFormat, TextureType::Unknown, aOwnerId);
  if (texture) {
    return texture;
  }

  auto flags = TextureFlags::DEALLOCATE_CLIENT | TextureFlags::REMOTE_TEXTURE;
  auto* data = BufferTextureData::Create(
      aSize, aFormat, gfx::ColorSpace2::SRGB, gfx::TransferFunction::SRGB,
      gfx::BackendType::SKIA, LayersBackend::LAYERS_WR, flags, ALLOC_DEFAULT,
      nullptr);
  return UniquePtr<TextureData>(data);
}

std::shared_ptr<gl::SharedSurface>
RemoteTextureOwnerClient::GetRecycledSharedSurface(
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat,
    SurfaceDescriptor::Type aType, RemoteTextureOwnerId aOwnerId) {
  UniquePtr<SharedResourceWrapper> wrapper =
      RemoteTextureMap::Get()->RemoteTextureMap::GetRecycledSharedTexture(
          aOwnerId, mForPid, mSharedRecycleBin, aSize, aFormat, aType);
  if (!wrapper) {
    return nullptr;
  }
  MOZ_ASSERT(wrapper->mSharedSurface);
  return wrapper->mSharedSurface;
}

std::shared_ptr<webgpu::SharedTexture>
RemoteTextureOwnerClient::GetRecycledSharedTexture(
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat,
    SurfaceDescriptor::Type aType, RemoteTextureOwnerId aOwnerId) {
  UniquePtr<SharedResourceWrapper> wrapper =
      RemoteTextureMap::Get()->RemoteTextureMap::GetRecycledSharedTexture(
          aOwnerId, mForPid, mSharedRecycleBin, aSize, aFormat, aType);
  if (!wrapper) {
    return nullptr;
  }
  MOZ_ASSERT(wrapper->mSharedTexture);
  return wrapper->mSharedTexture;
}

StaticAutoPtr<RemoteTextureMap> RemoteTextureMap::sInstance;

/* static */
void RemoteTextureMap::Init() {
  MOZ_ASSERT(!sInstance);
  sInstance = new RemoteTextureMap();
}

/* static */
void RemoteTextureMap::Shutdown() {
  if (sInstance) {
    sInstance = nullptr;
  }
}

RemoteTextureMap::RemoteTextureMap() : mMonitor("RemoteTextureMap::mMonitor") {}

RemoteTextureMap::~RemoteTextureMap() = default;

bool RemoteTextureMap::RecycleTexture(
    const RefPtr<RemoteTextureRecycleBin>& aRecycleBin,
    TextureDataHolder& aHolder, bool aExpireOldTextures) {
  if (!aHolder.mTextureData ||
      (aHolder.mTextureHost &&
       aHolder.mTextureHost->GetFlags() & TextureFlags::DUMMY_TEXTURE)) {
    return false;
  }

  // Expire old textures so they don't sit in the list forever if unused.
  constexpr size_t kSharedTextureLimit = 21;
  constexpr size_t kTextureLimit = 7;
  while (aRecycleBin->mRecycledTextures.size() >=
         (aRecycleBin->mIsShared ? kSharedTextureLimit : kTextureLimit)) {
    if (!aExpireOldTextures) {
      // There are too many textures, and we can't expire any to make room.
      return false;
    }
    aRecycleBin->mRecycledTextures.pop_front();
  }

  TextureData::Info info;
  aHolder.mTextureData->FillInfo(info);
  RemoteTextureRecycleBin::RecycledTextureHolder recycled{info.size,
                                                          info.format};
  if (aHolder.mResourceWrapper) {
    // Don't attempt to recycle non-recyclable shared surfaces
    if (aHolder.mResourceWrapper->mSharedSurface &&
        !aHolder.mResourceWrapper->mSharedSurface->mDesc.canRecycle) {
      return false;
    }

    // Recycle shared texture
    SurfaceDescriptor desc;
    if (!aHolder.mTextureData->Serialize(desc)) {
      return false;
    }
    recycled.mType = desc.type();
    recycled.mResourceWrapper = std::move(aHolder.mResourceWrapper);
  } else {
    // Recycle texture data
    recycled.mTextureData = std::move(aHolder.mTextureData);
  }
  if (!StaticPrefs::gfx_remote_texture_recycle_disabled()) {
    aRecycleBin->mRecycledTextures.push_back(std::move(recycled));
  }

  return true;
}

void RemoteTextureMap::PushTexture(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId,
    const base::ProcessId aForPid, UniquePtr<TextureData>&& aTextureData,
    RefPtr<TextureHost>& aTextureHost,
    UniquePtr<SharedResourceWrapper>&& aResourceWrapper) {
  MOZ_RELEASE_ASSERT(aTextureHost);

  std::vector<RefPtr<TextureHost>>
      releasingTextures;  // Release outside the monitor
  std::vector<std::function<void(const RemoteTextureInfo&)>>
      renderingReadyCallbacks;  // Call outside the monitor
  {
    MonitorAutoLock lock(mMonitor);

    auto* owner = GetTextureOwner(lock, aOwnerId, aForPid);
    if (!owner) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return;
    }

    if (owner->mIsContextLost &&
        !(aTextureHost->GetFlags() & TextureFlags::DUMMY_TEXTURE)) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      gfxCriticalNoteOnce << "Texture pushed during context lost";
    }

    auto textureData = MakeUnique<TextureDataHolder>(
        aTextureId, aTextureHost, std::move(aTextureData),
        std::move(aResourceWrapper));

    MOZ_ASSERT(owner->mLatestPushedTextureId < aTextureId);
    if (owner->mLatestPushedTextureId < aTextureId) {
      owner->mLatestPushedTextureId = aTextureId;
    }
    MOZ_ASSERT(owner->mLatestUsingTextureId < aTextureId);

    owner->mWaitingTextureDataHolders.push_back(std::move(textureData));

    {
      GetRenderingReadyCallbacks(lock, owner, aTextureId,
                                 renderingReadyCallbacks);
      // Update mRemoteTextureHost.
      // This happens when PushTexture() with RemoteTextureId is called after
      // GetRemoteTexture() with the RemoteTextureId.
      const auto key = std::pair(aForPid, aTextureId);
      auto it = mRemoteTextureHostWrapperHolders.find(key);
      if (it != mRemoteTextureHostWrapperHolders.end()) {
        MOZ_ASSERT(!it->second->mRemoteTextureHost);
        it->second->mRemoteTextureHost = aTextureHost;
      }
    }

    mMonitor.Notify();

    // Release owner->mReleasingRenderedTextureHosts before checking
    // NumCompositableRefs()
    if (!owner->mReleasingRenderedTextureHosts.empty()) {
      std::transform(
          owner->mReleasingRenderedTextureHosts.begin(),
          owner->mReleasingRenderedTextureHosts.end(),
          std::back_inserter(releasingTextures),
          [](CompositableTextureHostRef& aRef) { return aRef.get(); });
      owner->mReleasingRenderedTextureHosts.clear();
    }

    // Drop obsoleted remote textures.
    while (!owner->mUsingTextureDataHolders.empty()) {
      auto& front = owner->mUsingTextureDataHolders.front();
      // If mLatestRenderedTextureHost is last compositable ref of remote
      // texture's TextureHost, its RemoteTextureHostWrapper is already
      // unregistered. It happens when pushed remote textures that follow are
      // not rendered since last mLatestRenderedTextureHost update. In this
      // case, remove the TextureHost from mUsingTextureDataHolders. It is for
      // unblocking remote texture recyclieng.
      if (front->mTextureHost &&
          front->mTextureHost->NumCompositableRefs() == 1 &&
          front->mTextureHost == owner->mLatestRenderedTextureHost) {
        owner->mUsingTextureDataHolders.pop_front();
        continue;
      }
      // When compositable ref of TextureHost becomes 0, the TextureHost is not
      // used by WebRender anymore.
      if (front->mTextureHost &&
          front->mTextureHost->NumCompositableRefs() == 0) {
        owner->mReleasingTextureDataHolders.push_back(std::move(front));
        owner->mUsingTextureDataHolders.pop_front();
      } else if (front->mTextureHost &&
                 front->mTextureHost->NumCompositableRefs() >= 0) {
        // Remote texture is still in use by WebRender.
        break;
      } else {
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        owner->mUsingTextureDataHolders.pop_front();
      }
    }
    while (!owner->mReleasingTextureDataHolders.empty()) {
      RecycleTexture(owner->mRecycleBin,
                     *owner->mReleasingTextureDataHolders.front(), true);
      owner->mReleasingTextureDataHolders.pop_front();
    }
  }

  const auto info = RemoteTextureInfo(aTextureId, aOwnerId, aForPid);
  for (auto& callback : renderingReadyCallbacks) {
    callback(info);
  }
}

bool RemoteTextureMap::RemoveTexture(const RemoteTextureId aTextureId,
                                     const RemoteTextureOwnerId aOwnerId,
                                     const base::ProcessId aForPid) {
  MonitorAutoLock lock(mMonitor);

  auto* owner = GetTextureOwner(lock, aOwnerId, aForPid);
  if (!owner) {
    return false;
  }

  for (auto it = owner->mWaitingTextureDataHolders.begin();
       it != owner->mWaitingTextureDataHolders.end(); it++) {
    auto& data = *it;
    if (data->mTextureId == aTextureId) {
      if (mRemoteTextureHostWrapperHolders.find(std::pair(
              aForPid, aTextureId)) != mRemoteTextureHostWrapperHolders.end()) {
        return false;
      }
      if (!RecycleTexture(owner->mRecycleBin, *data, false)) {
        owner->mReleasingTextureDataHolders.push_back(std::move(data));
      }
      owner->mWaitingTextureDataHolders.erase(it);
      return true;
    }
  }

  return false;
}

void RemoteTextureMap::GetLatestBufferSnapshot(
    const RemoteTextureOwnerId aOwnerId, const base::ProcessId aForPid,
    const mozilla::ipc::Shmem& aDestShmem, const gfx::IntSize& aDestSize,
    size_t aDestStride) {
  // The compositable ref of remote texture should be updated in mMonitor lock.
  CompositableTextureHostRef textureHostRef;
  RefPtr<TextureHost> releasingTexture;  // Release outside the monitor
  std::shared_ptr<webgpu::SharedTexture> sharedTexture;
  {
    MonitorAutoLock lock(mMonitor);

    auto* owner = GetTextureOwner(lock, aOwnerId, aForPid);
    if (!owner) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return;
    }

    // Get latest TextureHost of remote Texture.
    if (owner->mWaitingTextureDataHolders.empty() &&
        owner->mUsingTextureDataHolders.empty()) {
      return;
    }
    const auto* holder = !owner->mWaitingTextureDataHolders.empty()
                             ? owner->mWaitingTextureDataHolders.back().get()
                             : owner->mUsingTextureDataHolders.back().get();
    TextureHost* textureHost = holder->mTextureHost;

    if (textureHost->GetSize() != aDestSize) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return;
    }
    if (textureHost->GetFormat() != gfx::SurfaceFormat::R8G8B8A8 &&
        textureHost->GetFormat() != gfx::SurfaceFormat::B8G8R8A8) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return;
    }
    if (holder->mResourceWrapper && holder->mResourceWrapper->mSharedTexture) {
      // Increment compositable ref to prevent that TextureDataHolder is removed
      // during memcpy.
      textureHostRef = textureHost;
      sharedTexture = holder->mResourceWrapper->mSharedTexture;
    } else if (textureHost->AsBufferTextureHost()) {
      // Increment compositable ref to prevent that TextureDataHolder is removed
      // during memcpy.
      textureHostRef = textureHost;
    } else {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return;
    }
  }

  if (!textureHostRef) {
    return;
  }

  if (sharedTexture) {
    const auto src_size = sharedTexture->GetSize();
    // The size of the shared texture should match `textureHost->GetSize()`.
    // We already checked above that `textureHost->GetSize()` matches
    // `aDestSize`. Just assert here to make sure they match.
    MOZ_RELEASE_ASSERT(src_size == aDestSize);

    sharedTexture->GetSnapshot(aDestShmem, aDestStride);
  } else if (auto* bufferTextureHost = textureHostRef->AsBufferTextureHost()) {
    const auto src_size = bufferTextureHost->GetSize();
    // We already checked above that `textureHost->GetSize()` matches
    // `aDestSize`.

    uint8_t* src = bufferTextureHost->GetBuffer();
    uint8_t* dst = aDestShmem.get<uint8_t>();

    const Maybe<int32_t> maybe_src_stride = ImageDataSerializer::GetRGBStride(
        bufferTextureHost->GetBufferDescriptor());
    MOZ_RELEASE_ASSERT(maybe_src_stride.isSome());
    const size_t src_stride = static_cast<size_t>(maybe_src_stride.value());
    const size_t bytesPerRow = static_cast<size_t>(src_size.width) * 4;
    MOZ_RELEASE_ASSERT(src_stride >= bytesPerRow);
    MOZ_RELEASE_ASSERT(aDestStride >= bytesPerRow);

    for (int y = 0; y < src_size.height; y++) {
      memcpy(dst, src, bytesPerRow);
      if (bytesPerRow < aDestStride) {
        memset(dst + bytesPerRow, 0, aDestStride - bytesPerRow);
      }
      src += src_stride;
      dst += aDestStride;
    }
  }

  {
    MonitorAutoLock lock(mMonitor);
    // Release compositable ref in mMonitor lock, but release RefPtr outside the
    // monitor
    releasingTexture = textureHostRef;
    textureHostRef = nullptr;
  }
}

void RemoteTextureMap::RegisterTextureOwner(
    const RemoteTextureOwnerId aOwnerId, const base::ProcessId aForPid,
    const RefPtr<RemoteTextureRecycleBin>& aRecycleBin) {
  MonitorAutoLock lock(mMonitor);

  const auto key = std::pair(aForPid, aOwnerId);
  auto it = mTextureOwners.find(key);
  if (it != mTextureOwners.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }
  auto owner = MakeUnique<TextureOwner>();
  if (aRecycleBin) {
    owner->mRecycleBin = aRecycleBin;
  } else {
    owner->mRecycleBin = MakeRefPtr<RemoteTextureRecycleBin>(false);
  }

  auto itWaiting = mWaitingTextureOwners.find(key);
  if (itWaiting != mWaitingTextureOwners.end()) {
    owner->mRenderingReadyCallbackHolders.swap(
        itWaiting->second->mRenderingReadyCallbackHolders);
    mWaitingTextureOwners.erase(itWaiting);
  }

  mTextureOwners.emplace(key, std::move(owner));
}

void RemoteTextureMap::KeepTextureDataAliveForTextureHostIfNecessary(
    const MonitorAutoLock& aProofOfLock, RemoteTextureMap::TextureOwner* aOwner,
    std::deque<UniquePtr<TextureDataHolder>>& aHolders) {
  for (auto& holder : aHolders) {
    // If remote texture of TextureHost still exist, keep
    // SharedResourceWrapper/TextureData alive while the TextureHost is alive.
    if (holder->mTextureHost &&
        holder->mTextureHost->NumCompositableRefs() > 0) {
      RefPtr<nsISerialEventTarget> eventTarget = GetCurrentSerialEventTarget();
      RefPtr<Runnable> runnable = NS_NewRunnableFunction(
          "RemoteTextureMap::UnregisterTextureOwner::Runnable",
          [data = std::move(holder->mTextureData),
           wrapper = std::move(holder->mResourceWrapper)]() {});

      auto destroyedCallback = [eventTarget = std::move(eventTarget),
                                runnable = std::move(runnable)]() mutable {
        eventTarget->Dispatch(runnable.forget());
      };

      holder->mTextureHost->SetDestroyedCallback(destroyedCallback);
    } else {
      RecycleTexture(aOwner->mRecycleBin, *holder, true);
    }
  }
}

UniquePtr<RemoteTextureMap::TextureOwner>
RemoteTextureMap::UnregisterTextureOwner(
    MonitorAutoLock& aProofOfLock, const RemoteTextureOwnerId aOwnerId,
    const base::ProcessId aForPid,
    std::vector<RefPtr<TextureHost>>& aReleasingTextures,
    std::vector<std::function<void(const RemoteTextureInfo&)>>&
        aRenderingReadyCallbacks) {
  const auto key = std::pair(aForPid, aOwnerId);
  auto it = mTextureOwners.find(key);
  if (it == mTextureOwners.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  auto* owner = it->second.get();
  // If waiting for a last use, and it hasn't arrived yet, then defer
  // unregistering.
  if (owner->mWaitForTxn) {
    owner->mDeferUnregister = GetCurrentSerialEventTarget();
    // If another thread is waiting on this owner to produce textures,
    // it must be notified that owner is going away.
    if (!owner->mLatestTextureHost &&
        owner->mWaitingTextureDataHolders.empty()) {
      aProofOfLock.Notify();
    }
    return nullptr;
  }

  if (owner->mLatestTextureHost) {
    // Release CompositableRef in mMonitor
    aReleasingTextures.emplace_back(owner->mLatestTextureHost);
    owner->mLatestTextureHost = nullptr;
  }

  // mReleasingRenderedTextureHosts and mLatestRenderedTextureHost could
  // simply be cleared. Since NumCompositableRefs() > 0 keeps TextureHosts in
  // mUsingTextureDataHolders alive. They need to be cleared before
  // KeepTextureDataAliveForTextureHostIfNecessary() call. The function uses
  // NumCompositableRefs().
  if (!owner->mReleasingRenderedTextureHosts.empty()) {
    std::transform(owner->mReleasingRenderedTextureHosts.begin(),
                   owner->mReleasingRenderedTextureHosts.end(),
                   std::back_inserter(aReleasingTextures),
                   [](CompositableTextureHostRef& aRef) { return aRef.get(); });
    owner->mReleasingRenderedTextureHosts.clear();
  }
  if (owner->mLatestRenderedTextureHost) {
    owner->mLatestRenderedTextureHost = nullptr;
  }

  GetAllRenderingReadyCallbacks(aProofOfLock, owner, aRenderingReadyCallbacks);

  KeepTextureDataAliveForTextureHostIfNecessary(
      aProofOfLock, owner, owner->mWaitingTextureDataHolders);

  KeepTextureDataAliveForTextureHostIfNecessary(
      aProofOfLock, owner, owner->mUsingTextureDataHolders);

  KeepTextureDataAliveForTextureHostIfNecessary(
      aProofOfLock, owner, owner->mReleasingTextureDataHolders);

  UniquePtr<TextureOwner> releasingOwner = std::move(it->second);
  mTextureOwners.erase(it);
  return releasingOwner;
}

void RemoteTextureMap::UnregisterTextureOwner(
    const RemoteTextureOwnerId aOwnerId, const base::ProcessId aForPid) {
  UniquePtr<TextureOwner> releasingOwner;  // Release outside the monitor
  std::vector<RefPtr<TextureHost>>
      releasingTextures;  // Release outside the monitor
  std::vector<std::function<void(const RemoteTextureInfo&)>>
      renderingReadyCallbacks;  // Call outside the monitor
  {
    MonitorAutoLock lock(mMonitor);

    releasingOwner = UnregisterTextureOwner(
        lock, aOwnerId, aForPid, releasingTextures, renderingReadyCallbacks);
    if (!releasingOwner) {
      return;
    }

    mMonitor.Notify();
  }

  const auto info =
      RemoteTextureInfo(RemoteTextureId{0}, RemoteTextureOwnerId{0}, 0);
  for (auto& callback : renderingReadyCallbacks) {
    callback(info);
  }
}

void RemoteTextureMap::UnregisterTextureOwners(
    const RemoteTextureOwnerIdSet& aOwnerIds, const base::ProcessId aForPid) {
  std::vector<UniquePtr<TextureOwner>>
      releasingOwners;  // Release outside the monitor
  std::vector<RefPtr<TextureHost>>
      releasingTextures;  // Release outside the monitor
  std::vector<std::function<void(const RemoteTextureInfo&)>>
      renderingReadyCallbacks;  // Call outside the monitor
  {
    MonitorAutoLock lock(mMonitor);

    for (const auto& id : aOwnerIds) {
      if (auto releasingOwner = UnregisterTextureOwner(
              lock, id, aForPid, releasingTextures, renderingReadyCallbacks)) {
        releasingOwners.push_back(std::move(releasingOwner));
      }
    }

    if (releasingOwners.empty()) {
      return;
    }

    mMonitor.Notify();
  }

  const auto info =
      RemoteTextureInfo(RemoteTextureId{0}, RemoteTextureOwnerId{0}, 0);
  for (auto& callback : renderingReadyCallbacks) {
    callback(info);
  }
}

already_AddRefed<RemoteTextureTxnScheduler>
RemoteTextureMap::RegisterTxnScheduler(base::ProcessId aForPid,
                                       RemoteTextureTxnType aType) {
  MonitorAutoLock lock(mMonitor);

  const auto key = std::pair(aForPid, aType);
  auto it = mTxnSchedulers.find(key);
  if (it != mTxnSchedulers.end()) {
    RefPtr<RemoteTextureTxnScheduler> scheduler(it->second);
    if (scheduler) {
      return scheduler.forget();
    }
    mTxnSchedulers.erase(it);
  }

  RefPtr<RemoteTextureTxnScheduler> scheduler(
      new RemoteTextureTxnScheduler(aForPid, aType));
  mTxnSchedulers.emplace(
      key, ThreadSafeWeakPtr<RemoteTextureTxnScheduler>(scheduler));
  return scheduler.forget();
}

void RemoteTextureMap::UnregisterTxnScheduler(base::ProcessId aForPid,
                                              RemoteTextureTxnType aType) {
  MonitorAutoLock lock(mMonitor);

  const auto key = std::pair(aForPid, aType);
  auto it = mTxnSchedulers.find(key);
  if (it == mTxnSchedulers.end()) {
    MOZ_ASSERT_UNREACHABLE("Remote texture txn scheduler does not exist.");
    return;
  }
  mTxnSchedulers.erase(it);
}

already_AddRefed<RemoteTextureTxnScheduler> RemoteTextureTxnScheduler::Create(
    mozilla::ipc::IProtocol* aProtocol) {
  if (auto* instance = RemoteTextureMap::Get()) {
    if (auto* toplevel = aProtocol->ToplevelProtocol()) {
      auto pid = toplevel->OtherPidMaybeInvalid();
      if (pid != base::kInvalidProcessId) {
        return instance->RegisterTxnScheduler(pid, toplevel->GetProtocolId());
      }
    }
  }
  return nullptr;
}

RemoteTextureTxnScheduler::~RemoteTextureTxnScheduler() {
  NotifyTxn(std::numeric_limits<RemoteTextureTxnId>::max());
  RemoteTextureMap::Get()->UnregisterTxnScheduler(mForPid, mType);
}

void RemoteTextureTxnScheduler::NotifyTxn(RemoteTextureTxnId aTxnId) {
  MonitorAutoLock lock(RemoteTextureMap::Get()->mMonitor);

  mLastTxnId = aTxnId;

  for (; !mWaits.empty(); mWaits.pop_front()) {
    auto& wait = mWaits.front();
    if (wait.mTxnId > aTxnId) {
      break;
    }
    RemoteTextureMap::Get()->NotifyTxn(lock, wait.mOwnerId, mForPid);
  }
}

bool RemoteTextureTxnScheduler::WaitForTxn(const MonitorAutoLock& aProofOfLock,
                                           RemoteTextureOwnerId aOwnerId,
                                           RemoteTextureTxnId aTxnId) {
  if (aTxnId <= mLastTxnId) {
    return false;
  }
  mWaits.insert(std::upper_bound(mWaits.begin(), mWaits.end(), aTxnId),
                Wait{aOwnerId, aTxnId});
  return true;
}

void RemoteTextureMap::ClearRecycledTextures(
    const RemoteTextureOwnerIdSet& aOwnerIds, const base::ProcessId aForPid,
    const RefPtr<RemoteTextureRecycleBin>& aRecycleBin) {
  std::list<RemoteTextureRecycleBin::RecycledTextureHolder>
      releasingTextures;  // Release outside the monitor
  {
    MonitorAutoLock lock(mMonitor);

    if (aRecycleBin) {
      releasingTextures.splice(releasingTextures.end(),
                               aRecycleBin->mRecycledTextures);
    }

    for (const auto& id : aOwnerIds) {
      const auto key = std::pair(aForPid, id);
      auto it = mTextureOwners.find(key);
      if (it == mTextureOwners.end()) {
        MOZ_ASSERT_UNREACHABLE("unexpected to be called");
        continue;
      }
      auto& owner = it->second;

      releasingTextures.splice(releasingTextures.end(),
                               owner->mRecycleBin->mRecycledTextures);
    }
  }
}

void RemoteTextureMap::NotifyContextLost(
    const RemoteTextureOwnerIdSet& aOwnerIds, const base::ProcessId aForPid) {
  MonitorAutoLock lock(mMonitor);

  bool changed = false;
  for (const auto& id : aOwnerIds) {
    const auto key = std::pair(aForPid, id);
    auto it = mTextureOwners.find(key);
    if (it == mTextureOwners.end()) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      continue;
    }
    auto& owner = it->second;
    if (!owner->mIsContextLost) {
      owner->mIsContextLost = true;
      changed = true;
    }
  }

  if (changed) {
    mMonitor.Notify();
  }
}

void RemoteTextureMap::NotifyContextRestored(
    const RemoteTextureOwnerIdSet& aOwnerIds, const base::ProcessId aForPid) {
  MonitorAutoLock lock(mMonitor);

  bool changed = false;
  for (const auto& id : aOwnerIds) {
    const auto key = std::pair(aForPid, id);
    auto it = mTextureOwners.find(key);
    if (it == mTextureOwners.end()) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      continue;
    }
    auto& owner = it->second;
    if (owner->mIsContextLost) {
      owner->mIsContextLost = false;
      changed = true;
    }
  }

  if (changed) {
    mMonitor.Notify();
  }
}

/* static */
RefPtr<TextureHost> RemoteTextureMap::CreateRemoteTexture(
    TextureData* aTextureData, TextureFlags aTextureFlags) {
  SurfaceDescriptor desc;
  DebugOnly<bool> ret = aTextureData->Serialize(desc);
  MOZ_ASSERT(ret);
  TextureFlags flags = aTextureFlags | TextureFlags::REMOTE_TEXTURE |
                       TextureFlags::DEALLOCATE_CLIENT;

  Maybe<wr::ExternalImageId> externalImageId = Nothing();
  RefPtr<TextureHost> textureHost =
      TextureHost::Create(desc, null_t(), nullptr, LayersBackend::LAYERS_WR,
                          flags, externalImageId);
  MOZ_ASSERT(textureHost);
  if (!textureHost) {
    gfxCriticalNoteOnce << "Failed to create remote texture";
    return nullptr;
  }

  textureHost->EnsureRenderTexture(Nothing());

  return textureHost;
}

void RemoteTextureMap::UpdateTexture(const MonitorAutoLock& aProofOfLock,
                                     RemoteTextureMap::TextureOwner* aOwner,
                                     const RemoteTextureId aTextureId) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(aOwner);
  MOZ_ASSERT(aTextureId >= aOwner->mLatestUsingTextureId);

  if (aTextureId == aOwner->mLatestUsingTextureId) {
    // No need to update texture.
    return;
  }

  // Move remote textures to mUsingTextureDataHolders.
  while (!aOwner->mWaitingTextureDataHolders.empty()) {
    auto& front = aOwner->mWaitingTextureDataHolders.front();
    if (aTextureId < front->mTextureId) {
      break;
    }
    MOZ_RELEASE_ASSERT(front->mTextureHost);
    aOwner->mLatestTextureHost = front->mTextureHost;
    aOwner->mLatestUsingTextureId = front->mTextureId;

    UniquePtr<TextureDataHolder> holder = std::move(front);
    aOwner->mWaitingTextureDataHolders.pop_front();
    // If there are textures not being used by the compositor that will be
    // obsoleted by this new texture, then queue them for removal later on
    // the creating thread.
    while (!aOwner->mUsingTextureDataHolders.empty()) {
      auto& back = aOwner->mUsingTextureDataHolders.back();
      if (back->mTextureHost &&
          back->mTextureHost->NumCompositableRefs() == 0) {
        if (!RecycleTexture(aOwner->mRecycleBin, *back, false)) {
          aOwner->mReleasingTextureDataHolders.push_back(std::move(back));
        }
        aOwner->mUsingTextureDataHolders.pop_back();
        continue;
      }
      break;
    }
    aOwner->mUsingTextureDataHolders.push_back(std::move(holder));
  }
}

void RemoteTextureMap::GetRenderingReadyCallbacks(
    const MonitorAutoLock& aProofOfLock, RemoteTextureMap::TextureOwner* aOwner,
    const RemoteTextureId aTextureId,
    std::vector<std::function<void(const RemoteTextureInfo&)>>& aFunctions) {
  MOZ_ASSERT(aOwner);

  while (!aOwner->mRenderingReadyCallbackHolders.empty()) {
    auto& front = aOwner->mRenderingReadyCallbackHolders.front();
    if (aTextureId < front->mTextureId) {
      break;
    }
    if (front->mCallback) {
      aFunctions.push_back(std::move(front->mCallback));
    }
    aOwner->mRenderingReadyCallbackHolders.pop_front();
  }
}

void RemoteTextureMap::GetAllRenderingReadyCallbacks(
    const MonitorAutoLock& aProofOfLock, RemoteTextureMap::TextureOwner* aOwner,
    std::vector<std::function<void(const RemoteTextureInfo&)>>& aFunctions) {
  GetRenderingReadyCallbacks(aProofOfLock, aOwner, RemoteTextureId::Max(),
                             aFunctions);
  MOZ_ASSERT(aOwner->mRenderingReadyCallbackHolders.empty());
}

bool RemoteTextureMap::WaitForRemoteTextureOwner(
    RemoteTextureHostWrapper* aTextureHostWrapper) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(aTextureHostWrapper);

  const auto& ownerId = aTextureHostWrapper->mOwnerId;
  const auto& forPid = aTextureHostWrapper->mForPid;

  MonitorAutoLock lock(mMonitor);

  auto* owner = GetTextureOwner(lock, ownerId, forPid);
  // If there is no texture owner yet, then we might need to wait for one to
  // be created, if allowed. If so, we must also wait for an initial texture
  // host to be created so we can use it.
  if (!owner || (!owner->mLatestTextureHost &&
                 owner->mWaitingTextureDataHolders.empty())) {
    const TimeDuration timeout = TimeDuration::FromMilliseconds(10000);
    while (!owner || (!owner->mLatestTextureHost &&
                      owner->mWaitingTextureDataHolders.empty())) {
      if (owner && (owner->mIsContextLost || owner->mDeferUnregister)) {
        // If the context was lost, no further updates are expected.
        return false;
      }
      CVStatus status = mMonitor.Wait(timeout);
      if (status == CVStatus::Timeout) {
        return false;
      }
      owner = GetTextureOwner(lock, ownerId, forPid);
    }
  }
  return true;
}

void RemoteTextureMap::GetRemoteTexture(
    RemoteTextureHostWrapper* aTextureHostWrapper) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(aTextureHostWrapper);

  if (aTextureHostWrapper->IsReadyForRendering()) {
    return;
  }

  const auto& textureId = aTextureHostWrapper->mTextureId;
  const auto& ownerId = aTextureHostWrapper->mOwnerId;
  const auto& forPid = aTextureHostWrapper->mForPid;
  const auto& size = aTextureHostWrapper->mSize;

  RefPtr<TextureHost> textureHost;
  {
    MonitorAutoLock lock(mMonitor);

    auto* owner = GetTextureOwner(lock, ownerId, forPid);
    if (!owner) {
      return;
    }

    UpdateTexture(lock, owner, textureId);

    if (owner->mLatestTextureHost &&
        (owner->mLatestTextureHost->GetFlags() & TextureFlags::DUMMY_TEXTURE)) {
      // Remote texture allocation was failed.
      return;
    }

    if (textureId == owner->mLatestUsingTextureId) {
      MOZ_ASSERT(owner->mLatestTextureHost);
      MOZ_ASSERT(owner->mLatestTextureHost->GetSize() == size);
      if (owner->mLatestTextureHost->GetSize() != size) {
        gfxCriticalNoteOnce << "unexpected remote texture size: "
                            << owner->mLatestTextureHost->GetSize()
                            << " expected: " << size;
      }
      textureHost = owner->mLatestTextureHost;
    }

    // Update mRemoteTextureHost
    if (textureId == owner->mLatestUsingTextureId) {
      const auto key = std::pair(forPid, textureId);
      auto it = mRemoteTextureHostWrapperHolders.find(key);
      if (it != mRemoteTextureHostWrapperHolders.end() &&
          !it->second->mRemoteTextureHost) {
        it->second->mRemoteTextureHost = owner->mLatestTextureHost;
      } else {
        MOZ_ASSERT(it->second->mRemoteTextureHost == owner->mLatestTextureHost);
      }
    }

    if (textureHost) {
      aTextureHostWrapper->SetRemoteTextureHost(lock, textureHost);
      aTextureHostWrapper->ApplyTextureFlagsToRemoteTexture();
    }
  }
}

void RemoteTextureMap::NotifyTxn(const MonitorAutoLock& aProofOfLock,
                                 const RemoteTextureOwnerId aOwnerId,
                                 const base::ProcessId aForPid) {
  if (auto* owner = GetTextureOwner(aProofOfLock, aOwnerId, aForPid)) {
    if (!owner->mWaitForTxn) {
      MOZ_ASSERT_UNREACHABLE("Expected texture owner to wait for txn.");
      return;
    }
    owner->mWaitForTxn = false;
    if (!owner->mDeferUnregister) {
      // If unregistering was not deferred, then don't try to force
      // unregistering yet.
      return;
    }
    owner->mDeferUnregister->Dispatch(NS_NewRunnableFunction(
        "RemoteTextureMap::SetLastRemoteTextureUse::Runnable",
        [aOwnerId, aForPid]() {
          RemoteTextureMap::Get()->UnregisterTextureOwner(aOwnerId, aForPid);
        }));
  }
}

bool RemoteTextureMap::WaitForTxn(const RemoteTextureOwnerId aOwnerId,
                                  const base::ProcessId aForPid,
                                  RemoteTextureTxnType aTxnType,
                                  RemoteTextureTxnId aTxnId) {
  MonitorAutoLock lock(mMonitor);
  auto* owner = GetTextureOwner(lock, aOwnerId, aForPid);
  if (!owner) {
    return false;
  }
  if (owner->mDeferUnregister) {
    MOZ_ASSERT_UNREACHABLE(
        "Texture owner must wait for txn before unregistering.");
    return false;
  }
  if (owner->mWaitForTxn) {
    MOZ_ASSERT_UNREACHABLE("Texture owner already waiting for txn.");
    return false;
  }
  const auto key = std::pair(aForPid, aTxnType);
  auto it = mTxnSchedulers.find(key);
  if (it != mTxnSchedulers.end()) {
    RefPtr<RemoteTextureTxnScheduler> scheduler(it->second);
    if (scheduler) {
      if (scheduler->WaitForTxn(lock, aOwnerId, aTxnId)) {
        owner->mWaitForTxn = true;
      }
      return true;
    }
  }
  // During shutdown, different toplevel protocols may go away in
  // disadvantageous orders, causing us to sometimes be processing
  // waits even though the source of transactions upon which the
  // wait depends shut down. This is generally harmless to ignore,
  // as it means no further transactions will be generated of that
  // type and all such transactions have been processed before it
  // unregistered.
  NS_WARNING("Could not find scheduler for txn type.");
  return false;
}

void RemoteTextureMap::ReleaseRemoteTextureHost(
    RemoteTextureHostWrapper* aTextureHostWrapper) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(aTextureHostWrapper);

  RefPtr<TextureHost> releasingTexture;  // Release outside the mutex
  {
    MonitorAutoLock lock(mMonitor);
    releasingTexture = aTextureHostWrapper->GetRemoteTextureHost(lock);
    aTextureHostWrapper->ClearRemoteTextureHost(lock);
  }
}

RefPtr<TextureHost> RemoteTextureMap::GetOrCreateRemoteTextureHostWrapper(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId,
    const base::ProcessId aForPid, const gfx::IntSize& aSize,
    const TextureFlags aFlags) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MonitorAutoLock lock(mMonitor);

  const auto key = std::pair(aForPid, aTextureId);
  auto it = mRemoteTextureHostWrapperHolders.find(key);
  if (it != mRemoteTextureHostWrapperHolders.end()) {
    return it->second->mRemoteTextureHostWrapper;
  }

  auto wrapper = RemoteTextureHostWrapper::Create(aTextureId, aOwnerId, aForPid,
                                                  aSize, aFlags);
  auto wrapperHolder = MakeUnique<RemoteTextureHostWrapperHolder>(wrapper);

  mRemoteTextureHostWrapperHolders.emplace(key, std::move(wrapperHolder));

  return wrapper;
}

void RemoteTextureMap::UnregisterRemoteTextureHostWrapper(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId,
    const base::ProcessId aForPid) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  std::vector<RefPtr<TextureHost>>
      releasingTextures;  // Release outside the monitor
  {
    MonitorAutoLock lock(mMonitor);

    const auto key = std::pair(aForPid, aTextureId);
    auto it = mRemoteTextureHostWrapperHolders.find(key);
    if (it == mRemoteTextureHostWrapperHolders.end()) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return;
    }
    releasingTextures.emplace_back(it->second->mRemoteTextureHostWrapper);
    if (it->second->mRemoteTextureHost) {
      releasingTextures.emplace_back(it->second->mRemoteTextureHost);
    }

    mRemoteTextureHostWrapperHolders.erase(it);
    mMonitor.Notify();
  }
}

bool RemoteTextureMap::CheckRemoteTextureReady(
    const RemoteTextureInfo& aInfo,
    std::function<void(const RemoteTextureInfo&)>&& aCallback) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  MonitorAutoLock lock(mMonitor);

  auto* owner = GetTextureOwner(lock, aInfo.mOwnerId, aInfo.mForPid);
  if (aInfo.mWaitForRemoteTextureOwner && !owner) {
    // Remote texture owner is not registered yet. Waiting for remote texture
    // owner

    const auto key = std::pair(aInfo.mForPid, aInfo.mOwnerId);
    if (!mWaitingTextureOwners[key]) {
      auto waitingOwner = MakeUnique<WaitingTextureOwner>();
      mWaitingTextureOwners[key] = std::move(waitingOwner);
    }

    MOZ_ASSERT(mWaitingTextureOwners[key]);

    WaitingTextureOwner* waitingOwner = mWaitingTextureOwners[key].get();

    auto callbackHolder = MakeUnique<RenderingReadyCallbackHolder>(
        aInfo.mTextureId, std::move(aCallback));
    waitingOwner->mRenderingReadyCallbackHolders.push_back(
        std::move(callbackHolder));

    return false;
  }

  if (!owner || owner->mIsContextLost) {
    // Owner is already removed or context lost. No need to wait texture ready.
    return true;
  }

  const auto key = std::pair(aInfo.mForPid, aInfo.mTextureId);
  auto it = mRemoteTextureHostWrapperHolders.find(key);
  if (it == mRemoteTextureHostWrapperHolders.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    gfxCriticalNoteOnce << "Remote texture does not exist id:"
                        << uint64_t(aInfo.mTextureId);
    return true;
  }

  if (owner->mLatestPushedTextureId >= aInfo.mTextureId) {
    return true;
  }

  auto callbackHolder = MakeUnique<RenderingReadyCallbackHolder>(
      aInfo.mTextureId, std::move(aCallback));
  owner->mRenderingReadyCallbackHolders.push_back(std::move(callbackHolder));

  return false;
}

bool RemoteTextureMap::WaitRemoteTextureReady(const RemoteTextureInfo& aInfo) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  MonitorAutoLock lock(mMonitor);

  auto* owner = GetTextureOwner(lock, aInfo.mOwnerId, aInfo.mForPid);
  if (aInfo.mWaitForRemoteTextureOwner &&
      (!owner || (!owner->mLatestTextureHost &&
                  owner->mWaitingTextureDataHolders.empty()))) {
    const TimeDuration timeout = TimeDuration::FromMilliseconds(10000);
    while (!owner || (!owner->mLatestTextureHost &&
                      owner->mWaitingTextureDataHolders.empty())) {
      if (owner && (owner->mIsContextLost || owner->mDeferUnregister)) {
        // If the context was lost, no further updates are expected.
        return false;
      }
      CVStatus status = mMonitor.Wait(timeout);
      if (status == CVStatus::Timeout) {
        return false;
      }
      owner = GetTextureOwner(lock, aInfo.mOwnerId, aInfo.mForPid);
    }
  }

  if (!owner || owner->mIsContextLost) {
    // Owner is already removed or context lost.
    return false;
  }

  const auto key = std::pair(aInfo.mForPid, aInfo.mTextureId);
  auto it = mRemoteTextureHostWrapperHolders.find(key);
  if (it == mRemoteTextureHostWrapperHolders.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    gfxCriticalNoteOnce << "Remote texture does not exist id:"
                        << uint64_t(aInfo.mTextureId);
    return false;
  }

  const TimeDuration timeout = TimeDuration::FromMilliseconds(1000);

  while (owner->mLatestPushedTextureId < aInfo.mTextureId) {
    CVStatus status = mMonitor.Wait(timeout);
    if (status == CVStatus::Timeout) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      gfxCriticalNoteOnce << "Remote texture wait time out id:"
                          << uint64_t(aInfo.mTextureId);
      return false;
    }

    auto it = mRemoteTextureHostWrapperHolders.find(key);
    if (it == mRemoteTextureHostWrapperHolders.end()) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return false;
    }

    owner = GetTextureOwner(lock, aInfo.mOwnerId, aInfo.mForPid);
    // When owner is alreay unregistered, remote texture will not be pushed.
    if (!owner || owner->mIsContextLost) {
      // This could happen with IPC abnormal shutdown
      return false;
    }
  }

  return true;
}

void RemoteTextureMap::SuppressRemoteTextureReadyCheck(
    const RemoteTextureInfo& aInfo) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MonitorAutoLock lock(mMonitor);

  // Clear if WaitingTextureOwner exists.
  auto itWaiting =
      mWaitingTextureOwners.find(std::pair(aInfo.mForPid, aInfo.mOwnerId));
  if (itWaiting != mWaitingTextureOwners.end()) {
    mWaitingTextureOwners.erase(itWaiting);
  }

  const auto key = std::pair(aInfo.mForPid, aInfo.mTextureId);
  auto it = mRemoteTextureHostWrapperHolders.find(key);
  if (it == mRemoteTextureHostWrapperHolders.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }
  it->second->mReadyCheckSuppressed = true;
}

UniquePtr<TextureData> RemoteTextureMap::GetRecycledTextureData(
    const RemoteTextureOwnerId aOwnerId, const base::ProcessId aForPid,
    const RefPtr<RemoteTextureRecycleBin>& aRecycleBin,
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat,
    TextureType aTextureType) {
  MonitorAutoLock lock(mMonitor);

  RefPtr<RemoteTextureRecycleBin> bin;
  if (aOwnerId.IsValid()) {
    if (auto* owner = GetTextureOwner(lock, aOwnerId, aForPid)) {
      bin = owner->mRecycleBin;
    }
  } else {
    bin = aRecycleBin;
  }
  if (!bin) {
    return nullptr;
  }

  for (auto it = bin->mRecycledTextures.begin();
       it != bin->mRecycledTextures.end(); it++) {
    auto& holder = *it;
    if (holder.mTextureData &&
        holder.mTextureData->GetTextureType() == aTextureType &&
        holder.mSize == aSize && holder.mFormat == aFormat) {
      UniquePtr<TextureData> texture = std::move(holder.mTextureData);
      bin->mRecycledTextures.erase(it);
      return texture;
    }
  }

  return nullptr;
}

UniquePtr<SharedResourceWrapper> RemoteTextureMap::GetRecycledSharedTexture(
    const RemoteTextureOwnerId aOwnerId, const base::ProcessId aForPid,
    const RefPtr<RemoteTextureRecycleBin>& aRecycleBin,
    const gfx::IntSize& aSize, const gfx::SurfaceFormat aFormat,
    SurfaceDescriptor::Type aType) {
  MonitorAutoLock lock(mMonitor);

  RefPtr<RemoteTextureRecycleBin> bin;
  if (aOwnerId.IsValid()) {
    if (auto* owner = GetTextureOwner(lock, aOwnerId, aForPid)) {
      bin = owner->mRecycleBin;
    }
  } else {
    bin = aRecycleBin;
  }
  if (!bin) {
    return nullptr;
  }

  for (auto it = bin->mRecycledTextures.begin();
       it != bin->mRecycledTextures.end(); it++) {
    auto& holder = *it;
    if (holder.mType == aType && holder.mSize == aSize &&
        holder.mFormat == aFormat) {
      UniquePtr<SharedResourceWrapper> wrapper =
          std::move(holder.mResourceWrapper);
      bin->mRecycledTextures.erase(it);
      return wrapper;
    }
  }

  return nullptr;
}

RemoteTextureMap::TextureOwner* RemoteTextureMap::GetTextureOwner(
    const MonitorAutoLock& aProofOfLock, const RemoteTextureOwnerId aOwnerId,
    const base::ProcessId aForPid) {
  const auto key = std::pair(aForPid, aOwnerId);
  auto it = mTextureOwners.find(key);
  if (it == mTextureOwners.end()) {
    return nullptr;
  }
  return it->second.get();
}

RemoteTextureMap::TextureDataHolder::TextureDataHolder(
    const RemoteTextureId aTextureId, RefPtr<TextureHost> aTextureHost,
    UniquePtr<TextureData>&& aTextureData,
    UniquePtr<SharedResourceWrapper>&& aResourceWrapper)
    : mTextureId(aTextureId),
      mTextureHost(aTextureHost),
      mTextureData(std::move(aTextureData)),
      mResourceWrapper(std::move(aResourceWrapper)) {}

RemoteTextureMap::RenderingReadyCallbackHolder::RenderingReadyCallbackHolder(
    const RemoteTextureId aTextureId,
    std::function<void(const RemoteTextureInfo&)>&& aCallback)
    : mTextureId(aTextureId), mCallback(std::move(aCallback)) {}

RemoteTextureMap::RemoteTextureHostWrapperHolder::
    RemoteTextureHostWrapperHolder(
        RefPtr<TextureHost> aRemoteTextureHostWrapper)
    : mRemoteTextureHostWrapper(aRemoteTextureHostWrapper) {}

}  // namespace mozilla::layers
