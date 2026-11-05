/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/NativeLayerWaylandSurfaceHandler.h"

#include "GLBlitHelper.h"
#include "GLContext.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/webrender/RenderThread.h"
#include "nsRegion.h"

namespace mozilla {
namespace layers {

using gfx::BackendType;
using gfx::DataSourceSurface;
using gfx::DrawTarget;
using gfx::IntPoint;
using gfx::IntRect;
using gfx::IntRegion;
using gfx::IntSize;
using gfx::Matrix4x4;
using gfx::SurfaceFormat;
using gl::GLContext;

NativeLayerWaylandSurfaceHandler::NativeLayerWaylandSurfaceHandler(
    const gfx::IntSize& aSize, SurfacePoolHandleWayland* aSurfacePoolHandle)
    : mSize(aSize), mSurfacePoolHandle(aSurfacePoolHandle) {
  MOZ_RELEASE_ASSERT(mSurfacePoolHandle,
                     "Need a non-null surface pool handle.");
}

RefPtr<DrawTarget> NativeLayerWaylandSurfaceHandler::NextSurfaceAsDrawTarget(
    const widget::WaylandSurfaceLock* aProofOfLock, const IntRect& aDisplayRect,
    const IntRegion& aUpdateRegion, BackendType aBackendType,
    RefPtr<widget::DRMFormat> aDRMFormat) {
  mDisplayRect = aDisplayRect;
  mDirtyRegion = aUpdateRegion;

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer);
  if (mFrontBuffer &&
      !SurfacePoolHandleWayland::IsBufferInUse(mFrontBuffer, aProofOfLock)) {
    // the Wayland compositor released the buffer early, we can reuse it
    mInProgressBuffer = std::move(mFrontBuffer);
  } else {
    mInProgressBuffer = mSurfacePoolHandle->ObtainBufferFromPool(
        aProofOfLock, mSize, aDRMFormat);
    if (mFrontBuffer) {
      ReadBackFrontBuffer(aProofOfLock);
      mSurfacePoolHandle->ReturnBufferToPool(aProofOfLock, mFrontBuffer);
      mFrontBuffer = nullptr;
    }
  }
  MOZ_DIAGNOSTIC_ASSERT(!mFrontBuffer);

  if (!mInProgressBuffer) {
    gfxCriticalError() << "Failed to obtain buffer";
    wr::RenderThread::Get()->HandleWebRenderError(
        wr::WebRenderError::NEW_SURFACE);
    return nullptr;
  }

  MOZ_DIAGNOSTIC_ASSERT(
      !SurfacePoolHandleWayland::IsBufferInUse(mInProgressBuffer, aProofOfLock),
      "Reusing attached buffer!");

  return mInProgressBuffer->Lock();
}

Maybe<GLuint> NativeLayerWaylandSurfaceHandler::NextSurfaceAsFramebuffer(
    const widget::WaylandSurfaceLock* aProofOfLock, const IntRect& aDisplayRect,
    const IntRegion& aUpdateRegion, bool aNeedsDepth,
    RefPtr<widget::DRMFormat> aDRMFormat) {
  mDisplayRect = aDisplayRect;
  mDirtyRegion = IntRegion(aUpdateRegion);

  MOZ_DIAGNOSTIC_ASSERT(!mInProgressBuffer);
  if (mFrontBuffer &&
      !SurfacePoolHandleWayland::IsBufferInUse(mFrontBuffer, aProofOfLock)) {
    // the Wayland compositor released the buffer early, we can reuse it
    mInProgressBuffer = std::move(mFrontBuffer);
  } else {
    mInProgressBuffer = mSurfacePoolHandle->ObtainBufferFromPool(
        aProofOfLock, mSize, aDRMFormat);
  }

  MOZ_DIAGNOSTIC_ASSERT(mInProgressBuffer,
                        "NativeLayerWaylandRender: Failed to obtain buffer");
  if (!mInProgressBuffer) {
    return Nothing();
  }

  MOZ_DIAGNOSTIC_ASSERT(
      !SurfacePoolHandleWayland::IsBufferInUse(mInProgressBuffer, aProofOfLock),
      "Reusing attached buffer!");

  // get the framebuffer before handling partial damage so we don't accidently
  // create one without depth buffer
  Maybe<GLuint> fbo = mSurfacePoolHandle->GetFramebufferForBuffer(
      mInProgressBuffer, aNeedsDepth);
  MOZ_DIAGNOSTIC_ASSERT(
      fbo, "NativeLayerWaylandRender: Failed to create framebuffer!");
  if (!fbo) {
    return Nothing();
  }

  if (mFrontBuffer) {
    ReadBackFrontBuffer(aProofOfLock);
    mSurfacePoolHandle->ReturnBufferToPool(aProofOfLock, mFrontBuffer);
    mFrontBuffer = nullptr;
  }

  return fbo;
}

void NativeLayerWaylandSurfaceHandler::ReadBackFrontBuffer(
    const widget::WaylandSurfaceLock* aProofOfLock) {
  IntRegion copyRegion = IntRegion(mDisplayRect);
  copyRegion.SubOut(mDirtyRegion);

  if (!copyRegion.IsEmpty()) {
    if (mSurfacePoolHandle->gl()) {
      mSurfacePoolHandle->gl()->MakeCurrent();
      Maybe<GLuint> sourceFB =
          mSurfacePoolHandle->GetFramebufferForBuffer(mFrontBuffer, false);
      MOZ_DIAGNOSTIC_ASSERT(sourceFB,
                            "NativeLayerWaylandRender: Failed to get "
                            "mFrontBuffer framebuffer!");
      if (!sourceFB) {
        return;
      }
      Maybe<GLuint> destFB =
          mSurfacePoolHandle->GetFramebufferForBuffer(mInProgressBuffer, false);
      MOZ_DIAGNOSTIC_ASSERT(destFB,
                            "NativeLayerWaylandRender: Failed to get "
                            "mInProgressBuffer framebuffer!");
      if (!destFB) {
        return;
      }

      mSurfacePoolHandle->gl()->fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER,
                                                 sourceFB.value());
      mSurfacePoolHandle->gl()->fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER,
                                                 destFB.value());

      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        gfx::IntRect r = iter.Get();
        mSurfacePoolHandle->gl()->BlitHelper()->BlitFramebuffer(
            r, r, LOCAL_GL_NEAREST);
      }
    } else {
      RefPtr<gfx::DataSourceSurface> dataSourceSurface =
          gfx::CreateDataSourceSurfaceFromData(
              mSize, mFrontBuffer->GetSurfaceFormat(),
              (const uint8_t*)mFrontBuffer->GetImageData(),
              mSize.width * BytesPerPixel(mFrontBuffer->GetSurfaceFormat()));
      RefPtr<DrawTarget> dt = mInProgressBuffer->Lock();

      for (auto iter = copyRegion.RectIter(); !iter.Done(); iter.Next()) {
        IntRect r = iter.Get();
        dt->CopySurface(dataSourceSurface, r, IntPoint(r.x, r.y));
      }
    }
  }
}

bool NativeLayerWaylandSurfaceHandler::CommitFrontBufferToScreenLocked(
    const widget::WaylandSurfaceLock* aProofOfLock) {
  // Don't operate over hidden layers
  mDirtyRegion.SetEmpty();

  if (auto* surface = mFrontBuffer->GetDMABufSurface()) {
    surface->FenceWait();
  }

  return true;
}

void NativeLayerWaylandSurfaceHandler::NotifySurfaceReady(
    const widget::WaylandSurfaceLock* aProofOfLock) {
  // NotifySurfaceReady() may be called even if NextSurfaceAsDrawTarget() fails.
  if (!mInProgressBuffer) {
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(!mFrontBuffer);
  mFrontBuffer = std::move(mInProgressBuffer);
  if (mSurfacePoolHandle->gl()) {
    if (auto* surface = mFrontBuffer->GetDMABufSurface()) {
      surface->FenceSet();
    }
    mSurfacePoolHandle->gl()->FlushIfHeavyGLCallsSinceLastFlush();
  }
}

bool NativeLayerWaylandSurfaceHandler::IsFrontBufferChanged() {
  return !mDirtyRegion.IsEmpty();
}

void NativeLayerWaylandSurfaceHandler::CopyFrontBufferToFrameBuffer(
    const widget::WaylandSurfaceLock* aProofOfLock, GLuint aFB) {
  if (!mFrontBuffer) {
    return;
  }

  mSurfacePoolHandle->gl()->MakeCurrent();

  Maybe<GLuint> sourceFB =
      mSurfacePoolHandle->GetFramebufferForBuffer(mFrontBuffer, true);
  MOZ_DIAGNOSTIC_ASSERT(
      sourceFB,
      "NativeLayerWaylandRender: Failed to get mFrontBuffer framebuffer!");
  if (!sourceFB) {
    return;
  }

  mSurfacePoolHandle->gl()->BlitHelper()->BlitFramebufferToFramebuffer(
      sourceFB.value(), aFB, IntRect(IntPoint(), mSize),
      IntRect(IntPoint(), mSize), LOCAL_GL_NEAREST);
}

gl::GLContext* NativeLayerWaylandSurfaceHandler::gl() {
  return mSurfacePoolHandle->gl();
}

void NativeLayerWaylandSurfaceHandler::DiscardBackbuffersLocked(
    const widget::WaylandSurfaceLock* aProofOfLock, bool aForce) {
  if (mInProgressBuffer && (!SurfacePoolHandleWayland::IsBufferInUse(
                                mInProgressBuffer, aProofOfLock) ||
                            aForce)) {
    mSurfacePoolHandle->ReturnBufferToPool(aProofOfLock, mInProgressBuffer);
    mInProgressBuffer = nullptr;
  }
  if (mFrontBuffer &&
      (!SurfacePoolHandleWayland::IsBufferInUse(mFrontBuffer, aProofOfLock) ||
       aForce)) {
    mSurfacePoolHandle->ReturnBufferToPool(aProofOfLock, mFrontBuffer);
    mFrontBuffer = nullptr;
  }
}

}  // namespace layers
}  // namespace mozilla
