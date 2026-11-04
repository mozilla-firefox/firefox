/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_SurfacePoolWayland_h
#define mozilla_layers_SurfacePoolWayland_h

#include <wayland-egl.h>

#include <unordered_map>

#include "GLContext.h"
#include "MozFramebuffer.h"
#include "mozilla/layers/SurfacePool.h"
#include "mozilla/widget/WaylandBuffer.h"

namespace mozilla::layers {

class SurfacePoolWayland final : public SurfacePool {
 public:
  // Get a handle for a new window. aGL can be nullptr.
  RefPtr<SurfacePoolHandle> GetHandleForGL(gl::GLContext* aGL) override;

  // Destroy all GL resources associated with aGL managed by this pool.
  void DestroyGLResourcesForContext(gl::GLContext* aGL) override;

 private:
  friend class SurfacePoolHandleWayland;
  friend RefPtr<SurfacePool> SurfacePool::Create(size_t aPoolSizeLimit);

  explicit SurfacePoolWayland(size_t aPoolSizeLimit);

  RefPtr<widget::WaylandBuffer> ObtainBufferFromPool(
      const widget::WaylandSurfaceLock& aWaylandSurfaceLock,
      const gfx::IntSize& aSize, gl::GLContext* aGL,
      RefPtr<widget::DRMFormat> aFormat);
  void ReturnBufferToPool(const widget::WaylandSurfaceLock& aWaylandSurfaceLock,
                          const RefPtr<widget::WaylandBuffer>& aBuffer);
  void EnforcePoolSizeLimit();
  void CollectPendingSurfaces();
  Maybe<GLuint> GetFramebufferForBuffer(
      const RefPtr<widget::WaylandBuffer>& aBuffer, gl::GLContext* aGL,
      bool aNeedsDepthBuffer);

  struct GLResourcesForBuffer final {
    RefPtr<gl::GLContext> mGL;                   // non-null
    UniquePtr<gl::MozFramebuffer> mFramebuffer;  // non-null
  };

  // Keep mWaylandSurface / mWaylandBuffer in pair as we don't share
  // mWaylandBuffer among WaylandSurfaces.
  struct SurfacePoolEntry final {
    const gfx::IntSize mSize;
    const RefPtr<widget::WaylandSurface> mWaylandSurface;  // non-null
    const RefPtr<widget::WaylandBuffer> mWaylandBuffer;    // non-null
    Maybe<GLResourcesForBuffer> mGLResources;
  };

  bool CanRecycleSurfaceForRequest(
      const MutexAutoLock& aProofOfLock, const SurfacePoolEntry& aEntry,
      const widget::WaylandSurfaceLock& aWaylandSurfaceLock,
      const gfx::IntSize& aSize, gl::GLContext* aGL);

  RefPtr<gl::DepthAndStencilBuffer> GetDepthBufferForSharing(
      const MutexAutoLock& aProofOfLock, gl::GLContext* aGL,
      const gfx::IntSize& aSize);
  UniquePtr<gl::MozFramebuffer> CreateFramebufferForTexture(
      const MutexAutoLock& aProofOfLock, gl::GLContext* aGL,
      const gfx::IntSize& aSize, GLuint aTexture, bool aNeedsDepthBuffer);

  Mutex mMutex MOZ_UNANNOTATED;

  // Stores the entries for surfaces that are in use by NativeLayerWayland, i.e.
  // an entry is inside mInUseEntries between calls to ObtainSurfaceFromPool()
  // and ReturnSurfaceToPool().
  std::unordered_map<widget::WaylandBuffer*, SurfacePoolEntry> mInUseEntries;

  // Stores entries which are no longer in use by NativeLayerWayland but are
  // still in use by the window server, i.e. for which
  // WaylandBuffer::IsAttached() still returns true.
  // These entries are checked once per frame inside
  // CollectPendingSurfaces(), and returned to mAvailableEntries once the
  // window server is done.
  nsTArray<SurfacePoolEntry> mPendingEntries;

  // Stores entries which are available for recycling. These entries are not
  // in use by a NativeLayerWayland or by the window server.
  nsTArray<SurfacePoolEntry> mAvailableEntries;
  size_t mPoolSizeLimit;

  template <typename F>
  void ForEachEntry(F aFn);

  struct DepthBufferEntry final {
    RefPtr<gl::GLContext> mGL;
    gfx::IntSize mSize;
    WeakPtr<gl::DepthAndStencilBuffer> mBuffer;
  };

  nsTArray<DepthBufferEntry> mDepthBuffers;
};

// A surface pool handle that is stored on NativeLayerWayland and keeps the
// SurfacePool alive.
class SurfacePoolHandleWayland final : public SurfacePoolHandle {
 public:
  SurfacePoolHandleWayland* AsSurfacePoolHandleWayland() override {
    return this;
  }

  RefPtr<widget::WaylandBuffer> ObtainBufferFromPool(
      const widget::WaylandSurfaceLock& aWaylandSurfaceLock,
      const gfx::IntSize& aSize, RefPtr<widget::DRMFormat> aFormat);
  void ReturnBufferToPool(const widget::WaylandSurfaceLock& aWaylandSurfaceLock,
                          const RefPtr<widget::WaylandBuffer>& aBuffer);
  Maybe<GLuint> GetFramebufferForBuffer(
      const RefPtr<widget::WaylandBuffer>& aBuffer, bool aNeedsDepthBuffer);
  const auto& gl() { return mGL; }

  RefPtr<SurfacePool> Pool() override { return mPool; }
  void OnBeginFrame() override;
  void OnEndFrame() override;

 private:
  friend class SurfacePoolWayland;
  SurfacePoolHandleWayland(RefPtr<SurfacePoolWayland> aPool,
                           gl::GLContext* aGL);

  const RefPtr<SurfacePoolWayland> mPool;
  const RefPtr<gl::GLContext> mGL;
};

}  // namespace mozilla::layers

#endif
