/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderImageHost.h"

#include <utility>

#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/AsyncImagePipelineManager.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/CompositorVsyncScheduler.h"  // for CompositorVsyncScheduler
#include "mozilla/layers/KnowsCompositor.h"
#include "mozilla/layers/RemoteTextureHostWrapper.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/WebRenderBridgeParent.h"
#include "mozilla/layers/WebRenderTextureHost.h"
#include "nsAString.h"
#include "nsDebug.h"          // for NS_WARNING, NS_ASSERTION
#include "nsPrintfCString.h"  // for nsPrintfCString
#include "nsString.h"         // for nsAutoCString

#if XP_WIN
#  include "mozilla/layers/GpuProcessD3D11TextureMap.h"
#  include "mozilla/layers/TextureHostWrapperD3D11.h"
#endif

namespace mozilla {

using namespace gfx;

namespace layers {

class ISurfaceAllocator;

WebRenderImageHost::WebRenderImageHost(const TextureInfo& aTextureInfo)
    : CompositableHost(aTextureInfo), mCurrentAsyncImageManager(nullptr) {}

WebRenderImageHost::~WebRenderImageHost() {
  MOZ_ASSERT(mPendingRemoteTextureWrappers.empty());
  MOZ_ASSERT(mWrBridges.empty());
}

void WebRenderImageHost::OnReleased() {
  ImageComposite::ClearImages();
  if (!mPendingRemoteTextureWrappers.empty()) {
    mPendingRemoteTextureWrappers.clear();
  }
  SetCurrentTextureHost(nullptr);
}

void WebRenderImageHost::UseTextureHost(
    const nsTArray<TimedTexture>& aTextures) {
  CompositableHost::UseTextureHost(aTextures);
  MOZ_ASSERT(aTextures.Length() >= 1);

  if (!mPendingRemoteTextureWrappers.empty()) {
    mPendingRemoteTextureWrappers.clear();
  }

  if (mCurrentTextureHost &&
      mCurrentTextureHost->AsRemoteTextureHostWrapper()) {
    mCurrentTextureHost = nullptr;
  }

  nsTArray<TimedImage> newImages;

  for (uint32_t i = 0; i < aTextures.Length(); ++i) {
    const TimedTexture& t = aTextures[i];
    MOZ_ASSERT(t.mTexture);
    if (i + 1 < aTextures.Length() && t.mProducerID == mLastProducerID &&
        t.mFrameID < mLastFrameID) {
      // Ignore frames before a frame that we already composited. We don't
      // ever want to display these frames. This could be important if
      // the frame producer adjusts timestamps (e.g. to track the audio clock)
      // and the new frame times are earlier.
      continue;
    }
    TimedImage& img = *newImages.AppendElement();
    img.mTextureHost = t.mTexture;
    img.mTimeStamp = t.mTimeStamp;
    img.mPictureRect = t.mPictureRect;
    img.mFrameID = t.mFrameID;
    img.mProducerID = t.mProducerID;
    img.mTextureHost->SetCropRect(img.mPictureRect);
  }

  SetImages(std::move(newImages));

  if (GetAsyncRef()) {
    for (const auto& it : mWrBridges) {
      RefPtr<WebRenderBridgeParent> wrBridge = it.second->WrBridge();
      if (wrBridge && wrBridge->CompositorScheduler()) {
        wrBridge->CompositorScheduler()->ScheduleComposition(
            wr::RenderReasons::ASYNC_IMAGE);
      }
    }
  }

  // Video producers generally send replacement images with the same frameID but
  // slightly different timestamps in order to sync with the audio clock. This
  // means that any CompositeUntil() call we made in Composite() may no longer
  // guarantee that we'll composite until the next frame is ready. Fix that
  // here.
  if (mLastFrameID >= 0 && !mWrBridges.empty()) {
    for (const auto& img : Images()) {
      bool frameComesAfter =
          img.mFrameID > mLastFrameID || img.mProducerID != mLastProducerID;
      if (frameComesAfter && !img.mTimeStamp.IsNull()) {
        for (const auto& it : mWrBridges) {
          RefPtr<WebRenderBridgeParent> wrBridge = it.second->WrBridge();
          if (wrBridge) {
            wrBridge->AsyncImageManager()->CompositeUntil(
                img.mTimeStamp + TimeDuration::FromMilliseconds(BIAS_TIME_MS));
          }
        }
        break;
      }
    }
  }
}

void WebRenderImageHost::PushPendingRemoteTexture(
    const RemoteTextureId aTextureId, const RemoteTextureOwnerId aOwnerId,
    const base::ProcessId aForPid, const gfx::IntSize aSize,
    const TextureFlags aFlags) {
  // Ensure aOwnerId is the same as RemoteTextureOwnerId of pending
  // RemoteTextures.
  if (!mPendingRemoteTextureWrappers.empty()) {
    auto* wrapper =
        mPendingRemoteTextureWrappers.front()->AsRemoteTextureHostWrapper();
    MOZ_ASSERT(wrapper);
    if (wrapper->mOwnerId != aOwnerId || wrapper->mForPid != aForPid) {
      // Clear when RemoteTextureOwner is different.
      mPendingRemoteTextureWrappers.clear();
      mWaitingReadyCallback = false;
      mWaitForRemoteTextureOwner = true;
    }
  }

  // Check if waiting for remote texture owner is allowed.
  if (!(aFlags & TextureFlags::WAIT_FOR_REMOTE_TEXTURE_OWNER)) {
    mWaitForRemoteTextureOwner = false;
  }

  RefPtr<TextureHost> texture =
      RemoteTextureMap::Get()->GetOrCreateRemoteTextureHostWrapper(
          aTextureId, aOwnerId, aForPid, aSize, aFlags);
  MOZ_ASSERT(texture);
  mPendingRemoteTextureWrappers.push_back(
      CompositableTextureHostRef(texture.get()));
}

void WebRenderImageHost::UseRemoteTexture(bool aCalledInCallback) {
  if (mPendingRemoteTextureWrappers.empty()) {
    return;
  }

  const bool useReadyCallback = bool(GetAsyncRef());
  CompositableTextureHostRef texture;

  if (useReadyCallback) {
    if (mWaitingReadyCallback) {
      return;
    }
    MOZ_ASSERT(!mWaitingReadyCallback);

    auto readyCallback = [self = RefPtr<WebRenderImageHost>(this)](
                             const RemoteTextureInfo aInfo) {
      RefPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
          "WebRenderImageHost::UseRemoteTexture",
          [self = std::move(self), aInfo]() {
            MOZ_RELEASE_ASSERT(CompositorThreadHolder::IsInCompositorThread());

            if (self->mPendingRemoteTextureWrappers.empty()) {
              return;
            }

            auto* wrapper = self->mPendingRemoteTextureWrappers.front()
                                ->AsRemoteTextureHostWrapper();
            MOZ_ASSERT(wrapper);
            if (wrapper->mOwnerId != aInfo.mOwnerId ||
                wrapper->mForPid != aInfo.mForPid) {
              // obsoleted callback
              return;
            }

            self->mWaitingReadyCallback = false;
            self->UseRemoteTexture(/* aCalledInCallback */ true);
          });

      CompositorThread()->Dispatch(runnable.forget());
    };

    // Check which of the pending remote textures is the most recent and ready.
    while (!mPendingRemoteTextureWrappers.empty()) {
      auto* wrapper =
          mPendingRemoteTextureWrappers.front()->AsRemoteTextureHostWrapper();

      // When UseRemoteTexture() is called in callback, it does not need to wait
      // RemoteTextureOwner. In this case, RemoteTextureOwner should be alive or
      // obsoleted.
      if (mWaitForRemoteTextureOwner && !aCalledInCallback) {
        // XXX remove sync wait
        RemoteTextureMap::Get()->WaitForRemoteTextureOwner(wrapper);
      }
      mWaitingReadyCallback = !RemoteTextureMap::Get()->CheckRemoteTextureReady(
          wrapper->GetRemoteTextureInfo(), readyCallback);
      if (mWaitingReadyCallback) {
        break;
      }
      RemoteTextureMap::Get()->GetRemoteTexture(wrapper);
      texture = mPendingRemoteTextureWrappers.front();
      mPendingRemoteTextureWrappers.pop_front();
    }
  } else {
    texture = mPendingRemoteTextureWrappers.front();
    auto* wrapper = texture->AsRemoteTextureHostWrapper();
    mPendingRemoteTextureWrappers.pop_front();
    MOZ_ASSERT(mPendingRemoteTextureWrappers.empty());

    if (mWaitForRemoteTextureOwner) {
      if (StaticPrefs::gfx_remote_texture_wait_owner_at_image_host()) {
        RemoteTextureMap::Get()->WaitForRemoteTextureOwner(wrapper);
      } else {
        wrapper->EnableWaitForRemoteTextureOwner(true);
      }
    }
    mWaitForRemoteTextureOwner = false;
  }

  if (!texture ||
      (GetAsyncRef() &&
       !texture->AsRemoteTextureHostWrapper()->IsReadyForRendering())) {
    return;
  }

  SetCurrentTextureHost(texture);

  if (GetAsyncRef()) {
    for (const auto& it : mWrBridges) {
      RefPtr<WebRenderBridgeParent> wrBridge = it.second->WrBridge();
      if (wrBridge && wrBridge->CompositorScheduler()) {
        wrBridge->CompositorScheduler()->ScheduleComposition(
            wr::RenderReasons::ASYNC_IMAGE);
      }
    }
  }
}

void WebRenderImageHost::CleanupResources() {
  ImageComposite::ClearImages();
  SetCurrentTextureHost(nullptr);
}

void WebRenderImageHost::RemoveTextureHost(TextureHost* aTexture) {
  RemoveImagesWithTextureHost(aTexture);
}

void WebRenderImageHost::ClearImages(ClearImagesType aType) {
  ImageComposite::ClearImages();
  if (aType == ClearImagesType::All) {
    if (!mPendingRemoteTextureWrappers.empty()) {
      mPendingRemoteTextureWrappers.clear();
    }
    SetCurrentTextureHost(nullptr);

    if (GetAsyncRef()) {
      for (const auto& it : mWrBridges) {
        RefPtr<WebRenderBridgeParent> wrBridge = it.second->WrBridge();
        if (wrBridge && wrBridge->CompositorScheduler()) {
          wrBridge->CompositorScheduler()->ScheduleComposition(
              wr::RenderReasons::ASYNC_IMAGE);
        }
      }
    }
  }
}

TimeStamp WebRenderImageHost::GetCompositionTime() const {
  TimeStamp time;

  MOZ_ASSERT(mCurrentAsyncImageManager);
  if (mCurrentAsyncImageManager) {
    time = mCurrentAsyncImageManager->GetCompositionTime();
  }
  return time;
}

CompositionOpportunityId WebRenderImageHost::GetCompositionOpportunityId()
    const {
  CompositionOpportunityId id;

  MOZ_ASSERT(mCurrentAsyncImageManager);
  if (mCurrentAsyncImageManager) {
    id = mCurrentAsyncImageManager->GetCompositionOpportunityId();
  }
  return id;
}

void WebRenderImageHost::AppendImageCompositeNotification(
    const ImageCompositeNotificationInfo& aInfo) const {
  if (mCurrentAsyncImageManager) {
    mCurrentAsyncImageManager->AppendImageCompositeNotification(aInfo);
  }
}

TextureHost* WebRenderImageHost::GetAsTextureHostForComposite(
    AsyncImagePipelineManager* aAsyncImageManager) {
  MOZ_ASSERT(aAsyncImageManager);

  if (mCurrentTextureHost &&
      mCurrentTextureHost->AsRemoteTextureHostWrapper()) {
    return mCurrentTextureHost;
  }

  mCurrentAsyncImageManager = aAsyncImageManager;
  const auto onExit =
      mozilla::MakeScopeExit([&]() { mCurrentAsyncImageManager = nullptr; });

  int imageIndex = ChooseImageIndex();
  if (imageIndex < 0) {
    SetCurrentTextureHost(nullptr);
    return nullptr;
  }

  if (uint32_t(imageIndex) + 1 < ImagesCount()) {
    mCurrentAsyncImageManager->CompositeUntil(
        GetImage(imageIndex + 1)->mTimeStamp +
        TimeDuration::FromMilliseconds(BIAS_TIME_MS));
  }

  const TimedImage* img = GetImage(imageIndex);

  RefPtr<TextureHost> texture = img->mTextureHost.get();
#if XP_WIN
  // Convert YUV BufferTextureHost to TextureHostWrapperD3D11 if possible
  if (const auto* bufferHost = texture->AsBufferTextureHost()) {
    auto identifier = aAsyncImageManager->GetTextureFactoryIdentifier();
    const bool tryConvertToNV12 =
        StaticPrefs::gfx_video_convert_yuv_to_nv12_image_host_win() &&
        identifier.mSupportsD3D11NV12 &&
        KnowsCompositor::SupportsD3D11(identifier) && (bufferHost->IsYCbCr());
    if (tryConvertToNV12) {
      PROFILER_MARKER_TEXT("WebRenderImageHost", GRAPHICS, {},
                           "Try ConvertToNV12"_ns);

      if (!mTextureAllocator) {
        mTextureAllocator = new TextureWrapperD3D11Allocator();
      }
      RefPtr<TextureHost> textureWrapper =
          TextureHostWrapperD3D11::CreateFromBufferTexture(mTextureAllocator,
                                                           texture);
      if (textureWrapper) {
        texture = textureWrapper;
      }
    } else if (profiler_thread_is_being_profiled_for_markers() &&
               StaticPrefs::gfx_video_convert_yuv_to_nv12_image_host_win() &&
               texture->GetFormat() == gfx::SurfaceFormat::YUV420) {
      nsPrintfCString str("No ConvertToNV12 D3D11 %d NV12 %d",
                          KnowsCompositor::SupportsD3D11(identifier),
                          identifier.mSupportsD3D11NV12);
      PROFILER_MARKER_TEXT("WebRenderImageHost", GRAPHICS, {}, str);
    }
  }
#endif
  SetCurrentTextureHost(texture);

  if (mCurrentAsyncImageManager->GetCompositionTime()) {
    // We are in a composition. Send ImageCompositeNotifications.
    OnFinishRendering(imageIndex, img, mAsyncRef.mProcessId, mAsyncRef.mHandle);
  }

  return mCurrentTextureHost;
}

void WebRenderImageHost::SetCurrentTextureHost(TextureHost* aTexture) {
  if (aTexture == mCurrentTextureHost.get()) {
    return;
  }
  mCurrentTextureHost = aTexture;
}

void WebRenderImageHost::Dump(std::stringstream& aStream, const char* aPrefix,
                              bool aDumpHtml) {
  for (const auto& img : Images()) {
    aStream << aPrefix;
    aStream << (aDumpHtml ? "<ul><li>TextureHost: " : "TextureHost: ");
    DumpTextureHost(aStream, img.mTextureHost);
    aStream << (aDumpHtml ? " </li></ul> " : " ");
  }
}

void WebRenderImageHost::SetWrBridge(const wr::PipelineId& aPipelineId,
                                     WebRenderBridgeParent* aWrBridge) {
  MOZ_ASSERT(aWrBridge);
  MOZ_ASSERT(!mCurrentAsyncImageManager);
#ifdef DEBUG
  const auto it = mWrBridges.find(wr::AsUint64(aPipelineId));
  MOZ_ASSERT(it == mWrBridges.end());
#endif
  RefPtr<WebRenderBridgeParentRef> ref =
      aWrBridge->GetWebRenderBridgeParentRef();
  mWrBridges.emplace(wr::AsUint64(aPipelineId), ref);
}

void WebRenderImageHost::ClearWrBridge(const wr::PipelineId& aPipelineId,
                                       WebRenderBridgeParent* aWrBridge) {
  MOZ_ASSERT(aWrBridge);
  MOZ_ASSERT(!mCurrentAsyncImageManager);

  const auto it = mWrBridges.find(wr::AsUint64(aPipelineId));
  MOZ_ASSERT(it != mWrBridges.end());
  if (it == mWrBridges.end()) {
    gfxCriticalNote << "WrBridge mismatch happened";
    return;
  }
  mWrBridges.erase(it);
  SetCurrentTextureHost(nullptr);
}

}  // namespace layers
}  // namespace mozilla
