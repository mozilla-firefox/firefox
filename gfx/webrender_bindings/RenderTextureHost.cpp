/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderTextureHost.h"

#include "GLContext.h"
#include "RenderThread.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/webrender/RenderThread.h"

namespace mozilla {
namespace wr {

void ActivateBindAndTexParameteri(gl::GLContext* aGL, GLenum aActiveTexture,
                                  GLenum aBindTarget, GLuint aBindTexture) {
  aGL->fActiveTexture(aActiveTexture);
  aGL->fBindTexture(aBindTarget, aBindTexture);
  // Initialize the mip filters to linear by default.
  aGL->fTexParameteri(aBindTarget, LOCAL_GL_TEXTURE_MIN_FILTER,
                      LOCAL_GL_LINEAR);
  aGL->fTexParameteri(aBindTarget, LOCAL_GL_TEXTURE_MAG_FILTER,
                      LOCAL_GL_LINEAR);
}

void RenderTextureHostUsageInfo::OnVideoPresent(int aFrameId,
                                                uint32_t aDurationMs) {
  MOZ_RELEASE_ASSERT(RenderThread::IsInRenderThread());

  const auto maxPresentWaitDurationMs = 2;
  const auto maxSlowPresentCount = 5;

  mVideoPresentFrameId = aFrameId;

  if (aDurationMs < maxPresentWaitDurationMs) {
    mSlowPresentCount = 0;
    return;
  }

  mSlowPresentCount++;

  if (mSlowPresentCount <= maxSlowPresentCount) {
    return;
  }

  DisableVideoOverlay();

  gfxCriticalNoteOnce << "Video swapchain present is slow";

  nsPrintfCString marker("Video swapchain present is slow");
  PROFILER_MARKER_TEXT("DisableOverlay", GRAPHICS, {}, marker);
}

void RenderTextureHostUsageInfo::OnCompositorEndFrame(int aFrameId,
                                                      uint32_t aDurationMs) {
  MOZ_RELEASE_ASSERT(RenderThread::IsInRenderThread());

  const auto maxCommitWaitDurationMs = 20;
  const auto maxSlowCommitCount = 5;

  // Check if video was presented in current frame.
  if (mVideoPresentFrameId != aFrameId) {
    return;
  }

  if (aDurationMs < maxCommitWaitDurationMs) {
    mSlowCommitCount = 0;
    return;
  }

  mSlowCommitCount++;

  if (mSlowCommitCount <= maxSlowCommitCount) {
    return;
  }

  gfxCriticalNoteOnce << "Video swapchain is slow";

  nsPrintfCString marker("Video swapchain is slow");
  PROFILER_MARKER_TEXT("DisableOverlay", GRAPHICS, {}, marker);
}

RenderTextureHost::RenderTextureHost() : mIsFromDRMSource(false) {
  MOZ_COUNT_CTOR(RenderTextureHost);
}

RenderTextureHost::~RenderTextureHost() {
  MOZ_RELEASE_ASSERT(RenderThread::IsInRenderThread());
  MOZ_COUNT_DTOR(RenderTextureHost);
  if (mDestroyedCallback) {
    mDestroyedCallback();
  }
}

wr::WrExternalImage RenderTextureHost::Lock(uint8_t aChannelIndex,
                                            gl::GLContext* aGL) {
  return InvalidToWrExternalImage();
}

wr::WrExternalImage RenderTextureHost::LockSWGL(uint8_t aChannelIndex,
                                                void* aContext,
                                                RenderCompositor* aCompositor) {
  return InvalidToWrExternalImage();
}

RefPtr<layers::TextureSource> RenderTextureHost::CreateTextureSource(
    layers::TextureSourceProvider* aProvider) {
  return nullptr;
}

void RenderTextureHost::Destroy() {
  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
}

RefPtr<RenderTextureHostUsageInfo> RenderTextureHost::GetOrMergeUsageInfo(
    const MutexAutoLock& aProofOfMapLock,
    RefPtr<RenderTextureHostUsageInfo> aUsageInfo) {
  MOZ_RELEASE_ASSERT(layers::CompositorThreadHolder::IsInCompositorThread());

  if (mRenderTextureHostUsageInfo && aUsageInfo) {
    if (mRenderTextureHostUsageInfo == aUsageInfo) {
      return mRenderTextureHostUsageInfo;
    }

    // Merge 2 RenderTextureHostUsageInfos to one RenderTextureHostUsageInfo.

    const bool overlayDisabled =
        mRenderTextureHostUsageInfo->VideoOverlayDisabled() ||
        aUsageInfo->VideoOverlayDisabled();

    // If mRenderTextureHostUsageInfo and aUsageInfo are different objects, keep
    // the older one.
    RefPtr<RenderTextureHostUsageInfo> usageInfo = [&]() {
      if (aUsageInfo->mCreationTimeStamp <
          mRenderTextureHostUsageInfo->mCreationTimeStamp) {
        return aUsageInfo;
      }
      return mRenderTextureHostUsageInfo;
    }();

    // Merge info.
    if (overlayDisabled) {
      usageInfo->DisableVideoOverlay();
    }
    mRenderTextureHostUsageInfo = usageInfo;
  } else if (aUsageInfo && !mRenderTextureHostUsageInfo) {
    mRenderTextureHostUsageInfo = aUsageInfo;
  }

  if (!mRenderTextureHostUsageInfo) {
    MOZ_ASSERT(!aUsageInfo);
    mRenderTextureHostUsageInfo = new RenderTextureHostUsageInfo;
  }

  MOZ_ASSERT(mRenderTextureHostUsageInfo);
  MOZ_ASSERT_IF(aUsageInfo && aUsageInfo->VideoOverlayDisabled(),
                mRenderTextureHostUsageInfo->VideoOverlayDisabled());

  return mRenderTextureHostUsageInfo;
}

RefPtr<RenderTextureHostUsageInfo> RenderTextureHost::GetTextureHostUsageInfo(
    const MutexAutoLock& aProofOfMapLock) {
  MOZ_RELEASE_ASSERT(RenderThread::IsInRenderThread());

  return mRenderTextureHostUsageInfo;
}

}  // namespace wr
}  // namespace mozilla
