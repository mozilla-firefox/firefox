/* License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/SurfacePoolWayland.h"

#include "GLBlitHelper.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/webrender/RenderThread.h"

#ifdef MOZ_LOGGING
#  undef LOG
#  undef LOGVERBOSE
#  include "Units.h"
#  include "mozilla/Logging.h"
#  include "nsTArray.h"
extern mozilla::LazyLogModule gWidgetCompositorLog;
#  define LOG(str, ...)                                     \
    MOZ_LOG(gWidgetCompositorLog, mozilla::LogLevel::Debug, \
            (str, ##__VA_ARGS__))
#  define LOGVERBOSE(str, ...)                                \
    MOZ_LOG(gWidgetCompositorLog, mozilla::LogLevel::Verbose, \
            (str, ##__VA_ARGS__))
#else
#  define LOG(args)
#  define LOGVERBOSE(args)
#endif /* MOZ_LOGGING */

namespace mozilla::layers {

using gfx::IntSize;
using gl::DepthAndStencilBuffer;
using gl::GLContext;
using gl::MozFramebuffer;
using widget::WaylandBuffer;

/* static */ RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit) {
  return new SurfacePoolWayland(aPoolSizeLimit);
}

SurfacePoolWayland::SurfacePoolWayland(size_t aPoolSizeLimit)
    : mMutex("SurfacePoolWayland"), mPoolSizeLimit(aPoolSizeLimit) {}

RefPtr<SurfacePoolHandle> SurfacePoolWayland::GetHandleForGL(GLContext* aGL) {
  return new SurfacePoolHandleWayland(this, aGL);
}

template <typename F>
void SurfacePoolWayland::ForEachEntry(F aFn) {
  for (auto& iter : mInUseEntries) {
    aFn(iter.second);
  }
  for (auto& entry : mPendingEntries) {
    aFn(entry);
  }
  for (auto& entry : mAvailableEntries) {
    aFn(entry);
  }
}

void SurfacePoolWayland::DestroyGLResourcesForContext(GLContext* aGL) {
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());
  MutexAutoLock lock(mMutex);

  ForEachEntry([&](SurfacePoolEntry& entry) {
    if (entry.mGLResources && entry.mGLResources->mGL == aGL) {
      entry.mGLResources = Nothing();
      entry.mWaylandBuffer->DestroyGLResources();
    }
  });
  mDepthBuffers.RemoveElementsBy(
      [&](const DepthBufferEntry& entry) { return entry.mGL == aGL; });
}

bool SurfacePoolWayland::CanRecycleSurfaceForRequest(
    const MutexAutoLock& aProofOfLock, const SurfacePoolEntry& aEntry,
    const widget::WaylandSurfaceLock& aWaylandSurfaceLock, const IntSize& aSize,
    GLContext* aGL) {
  if (aEntry.mWaylandSurface != aWaylandSurfaceLock.GetWaylandSurface()) {
    LOGVERBOSE(
        "SurfacePoolWayland::CanRecycleSurfaceForRequest(): can't recycle due "
        "to different WaylandSurface.");
    return false;
  }
  MOZ_DIAGNOSTIC_ASSERT(
      !aEntry.mWaylandBuffer->IsAttached(aWaylandSurfaceLock));
  if (aEntry.mSize != aSize) {
    LOGVERBOSE(
        "SurfacePoolWayland::CanRecycleSurfaceForRequest(): can't recycle due "
        "to different sizes.");
    return false;
  }
  if (aEntry.mGLResources) {
    LOGVERBOSE(
        "SurfacePoolWayland::CanRecycleSurfaceForRequest(): mGLResources "
        "recycle %d",
        aEntry.mGLResources->mGL == aGL);
    return aEntry.mGLResources->mGL == aGL;
  }
  LOGVERBOSE(
      "SurfacePoolWayland::CanRecycleSurfaceForRequest(): aGL recycle %d",
      aGL == nullptr);
  return aGL == nullptr;
}

RefPtr<WaylandBuffer> SurfacePoolWayland::ObtainBufferFromPool(
    const widget::WaylandSurfaceLock& aWaylandSurfaceLock, const IntSize& aSize,
    GLContext* aGL, RefPtr<widget::DRMFormat> aFormat) {
  MutexAutoLock lock(mMutex);

  auto iterToRecycle =
      std::find_if(mAvailableEntries.begin(), mAvailableEntries.end(),
                   [&](const SurfacePoolEntry& aEntry) {
                     return CanRecycleSurfaceForRequest(
                         lock, aEntry, aWaylandSurfaceLock, aSize, aGL);
                   });
  if (iterToRecycle != mAvailableEntries.end()) {
    RefPtr<WaylandBuffer> buffer = iterToRecycle->mWaylandBuffer;
    mInUseEntries.insert({buffer.get(), std::move(*iterToRecycle)});
    mAvailableEntries.RemoveElementAt(iterToRecycle);
    LOGVERBOSE(
        "SurfacePoolWayland::ObtainBufferFromPool() recycled [%p] U[%zu] "
        "P[%zu] "
        "A[%zu]",
        buffer.get(), mInUseEntries.size(), mPendingEntries.Length(),
        mAvailableEntries.Length());
    return buffer;
  }

  RefPtr<WaylandBuffer> buffer;
  if (aGL) {
    buffer = widget::WaylandBuffer::CreateDMABuf(
        LayoutDeviceIntSize::FromUnknownSize(aSize), aGL, aFormat);
  } else {
    buffer = widget::WaylandBuffer::CreateSHM(
        LayoutDeviceIntSize::FromUnknownSize(aSize), aFormat);
  }
  if (buffer) {
    mInUseEntries.insert(
        {buffer.get(),
         SurfacePoolEntry{
             aSize, aWaylandSurfaceLock.GetWaylandSurface(), buffer, {}}});
  }
  LOGVERBOSE(
      "SurfacePoolWayland::ObtainBufferFromPool() created [%p] U[%d] P[%d] "
      "A[%d]",
      buffer.get(), (int)mInUseEntries.size(), (int)mPendingEntries.Length(),
      (int)mAvailableEntries.Length());
  return buffer;
}

void SurfacePoolWayland::ReturnBufferToPool(
    const widget::WaylandSurfaceLock& aWaylandSurfaceLock,
    const RefPtr<WaylandBuffer>& aBuffer) {
  MutexAutoLock lock(mMutex);

  auto inUseEntryIter = mInUseEntries.find(aBuffer);
  MOZ_RELEASE_ASSERT(inUseEntryIter != mInUseEntries.end());

  if (aBuffer->IsAttached(aWaylandSurfaceLock)) {
    mPendingEntries.AppendElement(std::move(inUseEntryIter->second));
  } else {
    mAvailableEntries.AppendElement(std::move(inUseEntryIter->second));
  }
  mInUseEntries.erase(inUseEntryIter);

  LOGVERBOSE(
      "SurfacePoolWayland::ReturnBufferToPool() buffer [%p] U[%d] P[%d] A[%d]",
      aBuffer.get(), (int)mInUseEntries.size(), (int)mPendingEntries.Length(),
      (int)mAvailableEntries.Length());
}

void SurfacePoolWayland::EnforcePoolSizeLimit() {
  MutexAutoLock lock(mMutex);

  // Enforce the pool size limit, removing least-recently-used entries as
  // necessary.
  while (mAvailableEntries.Length() > mPoolSizeLimit) {
    mAvailableEntries.RemoveElementAt(0);
  }

  if (mPendingEntries.Length() > mPoolSizeLimit * 2) {
    LOG("SurfacePoolWayland() mPendingEntries num %d mPoolSizeLimit %d Are we "
        "leaking pending entries?",
        (int)mPendingEntries.Length(), (int)mPoolSizeLimit);
  }
  if (mInUseEntries.size() > mPoolSizeLimit * 2) {
    LOG("SurfacePoolWayland() mInUseEntries num %d mPoolSizeLimit %d Are we "
        "leaking in-use entries?",
        (int)mInUseEntries.size(), (int)mPoolSizeLimit);
  }
}

void SurfacePoolWayland::CollectPendingSurfaces() {
  // Move pending entries to a local array so we can check each surface's
  // IsAttached() without holding mMutex. This is safe because:
  // - Only this method removes from mPendingEntries, and it runs on a single
  //   thread (the render thread), so no concurrent removals can occur.
  // - ReturnBufferToPool() may append new entries while mMutex is released;
  //   those are merged back in the final lock section below and will be picked
  //   up by the next run.
  // - DestroyGLResourcesForContext() also runs on the render thread, so it
  //   cannot race with this method.
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());
  nsTArray<SurfacePoolEntry> pendingEntries;
  {
    MutexAutoLock lock(mMutex);
    pendingEntries = std::move(mPendingEntries);
  }

  nsTArray<SurfacePoolEntry> stillPending(pendingEntries.Length());
  nsTArray<SurfacePoolEntry> nowAvailable;

  for (auto& entry : pendingEntries) {
    widget::WaylandSurfaceLock lock(entry.mWaylandSurface);
    LOGVERBOSE(
        "SurfacePoolWayland::CollectPendingSurfaces() [%p] attached [%d]",
        entry.mWaylandBuffer.get(), entry.mWaylandBuffer->IsAttached(lock));
    if (!entry.mWaylandBuffer->IsAttached(lock)) {
      nowAvailable.AppendElement(std::move(entry));
    } else {
      stillPending.AppendElement(std::move(entry));
    }
  }

  {
    MutexAutoLock lock(mMutex);
    stillPending.AppendElements(std::move(mPendingEntries));
    mPendingEntries = std::move(stillPending);
    mAvailableEntries.AppendElements(std::move(nowAvailable));
    LOGVERBOSE("SurfacePoolWayland::CollectPendingSurfaces() U[%d] P[%d] A[%d]",
               (int)mInUseEntries.size(), (int)mPendingEntries.Length(),
               (int)mAvailableEntries.Length());
  }
}

Maybe<GLuint> SurfacePoolWayland::GetFramebufferForBuffer(
    const RefPtr<WaylandBuffer>& aBuffer, GLContext* aGL,
    bool aNeedsDepthBuffer) {
  MutexAutoLock lock(mMutex);
  MOZ_RELEASE_ASSERT(aGL);

  auto inUseEntryIter = mInUseEntries.find(aBuffer);
  MOZ_RELEASE_ASSERT(inUseEntryIter != mInUseEntries.end());

  SurfacePoolEntry& entry = inUseEntryIter->second;
  if (entry.mGLResources) {
    // We have an existing framebuffer.
    MOZ_RELEASE_ASSERT(entry.mGLResources->mGL == aGL,
                       "Recycled surface that still had GL resources from a "
                       "different GL context. "
                       "This shouldn't happen.");
    if (!aNeedsDepthBuffer || entry.mGLResources->mFramebuffer->HasDepth()) {
      return Some(entry.mGLResources->mFramebuffer->mFB);
    }
  }

  // No usable existing framebuffer, we need to create one.

  if (!aGL->MakeCurrent()) {
    // Context may have been destroyed.
    return {};
  }

  const GLuint tex = aBuffer->GetTexture();
  auto fb = CreateFramebufferForTexture(lock, aGL, entry.mSize, tex,
                                        aNeedsDepthBuffer);
  if (!fb) {
    // Framebuffer completeness check may have failed.
    return {};
  }

  GLuint fbo = fb->mFB;
  entry.mGLResources = Some(GLResourcesForBuffer{aGL, std::move(fb)});
  return Some(fbo);
}

RefPtr<gl::DepthAndStencilBuffer> SurfacePoolWayland::GetDepthBufferForSharing(
    const MutexAutoLock& aProofOfLock, GLContext* aGL, const IntSize& aSize) {
  // Clean out entries for which the weak pointer has become null.
  mDepthBuffers.RemoveElementsBy(
      [&](const DepthBufferEntry& entry) { return !entry.mBuffer; });

  for (const auto& entry : mDepthBuffers) {
    if (entry.mGL == aGL && entry.mSize == aSize) {
      return entry.mBuffer.get();
    }
  }
  return nullptr;
}

UniquePtr<MozFramebuffer> SurfacePoolWayland::CreateFramebufferForTexture(
    const MutexAutoLock& aProofOfLock, GLContext* aGL, const IntSize& aSize,
    GLuint aTexture, bool aNeedsDepthBuffer) {
  if (aNeedsDepthBuffer) {
    // Try to find an existing depth buffer of aSize in aGL and create a
    // framebuffer that shares it.
    if (auto buffer = GetDepthBufferForSharing(aProofOfLock, aGL, aSize)) {
      return MozFramebuffer::CreateForBackingWithSharedDepthAndStencil(
          aSize, 0, LOCAL_GL_TEXTURE_2D, aTexture, buffer,
          MozFramebuffer::ColorBackingOwnership::Borrowed);
    }
  }

  // No depth buffer needed or we didn't find one. Create a framebuffer with a
  // new depth buffer and store a weak pointer to the new depth buffer in
  // mDepthBuffers.
  UniquePtr<MozFramebuffer> fb = MozFramebuffer::CreateForBacking(
      aGL, aSize, 0, aNeedsDepthBuffer, aNeedsDepthBuffer, LOCAL_GL_TEXTURE_2D,
      aTexture, MozFramebuffer::ColorBackingOwnership::Borrowed);
  if (fb && fb->GetDepthAndStencilBuffer()) {
    mDepthBuffers.AppendElement(
        DepthBufferEntry{aGL, aSize, fb->GetDepthAndStencilBuffer().get()});
  }

  return fb;
}

SurfacePoolHandleWayland::SurfacePoolHandleWayland(
    RefPtr<SurfacePoolWayland> aPool, GLContext* aGL)
    : mPool(std::move(aPool)), mGL(aGL) {}

void SurfacePoolHandleWayland::OnBeginFrame() {
  mPool->CollectPendingSurfaces();
}

void SurfacePoolHandleWayland::OnEndFrame() { mPool->EnforcePoolSizeLimit(); }

RefPtr<WaylandBuffer> SurfacePoolHandleWayland::ObtainBufferFromPool(
    const widget::WaylandSurfaceLock& aWaylandSurfaceLock, const IntSize& aSize,
    RefPtr<widget::DRMFormat> aFormat) {
  return mPool->ObtainBufferFromPool(aWaylandSurfaceLock, aSize, mGL, aFormat);
}

void SurfacePoolHandleWayland::ReturnBufferToPool(
    const widget::WaylandSurfaceLock& aProofOfLock,
    const RefPtr<WaylandBuffer>& aBuffer) {
  mPool->ReturnBufferToPool(aProofOfLock, aBuffer);
}

Maybe<GLuint> SurfacePoolHandleWayland::GetFramebufferForBuffer(
    const RefPtr<WaylandBuffer>& aBuffer, bool aNeedsDepthBuffer) {
  return mPool->GetFramebufferForBuffer(aBuffer, mGL, aNeedsDepthBuffer);
}

}  // namespace mozilla::layers
#undef LOG
#undef LOGVERBOSE
