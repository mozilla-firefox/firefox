/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_WIDGET_GTK_WAYLAND_BUFFER_H
#define MOZILLA_WIDGET_GTK_WAYLAND_BUFFER_H

#include "DMABufSurface.h"
#include "GLContext.h"
#include "MozFramebuffer.h"
#include "WaylandSurface.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "nsTArray.h"
#include "nsWaylandDisplay.h"

namespace mozilla::widget {

class WaylandBufferDMABUF;
class BufferTransaction;

class WaylandBuffer {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WaylandBuffer);

  static already_AddRefed<WaylandBuffer> Create(RefPtr<BufferSurface> aSurface);
  static already_AddRefed<WaylandBuffer> CreateDMABuf(
      const LayoutDeviceIntSize& aSize, gl::GLContext* aGL,
      RefPtr<DRMFormat> aFormat);
  static RefPtr<WaylandBuffer> CreateSHM(
      const LayoutDeviceIntSize& aSize,
      RefPtr<widget::DRMFormat> aFormat = nullptr);

  already_AddRefed<gfx::DrawTarget> Lock();
  void* GetImageData();
  GLuint GetTexture() { return mBufferSurface->GetTexture(); }
  void DestroyGLResources() { mBufferSurface->ReleaseTextures(); }
  gfx::SurfaceFormat GetSurfaceFormat() { return mBufferSurface->GetFormat(); }

  DMABufSurface* GetDMABufSurface() {
    return mBufferSurface->GetAsDMABufSurface();
  }

  LayoutDeviceIntSize GetSize() const { return mSize; }
  bool IsMatchingSize(const LayoutDeviceIntSize& aSize) const {
    return aSize == mSize;
  }

  bool IsAttached(const WaylandSurfaceLock& aSurfaceLock) const;
  BufferTransaction* GetTransaction(const WaylandSurfaceLock& aSurfaceLock);
  void RemoveTransaction(const WaylandSurfaceLock& aSurfaceLock,
                         RefPtr<BufferTransaction> aTransaction);

  size_t GetBufferAge() const { return mBufferAge; }
  void IncrementBufferAge() { mBufferAge++; }
  void ResetBufferAge() { mBufferAge = 0; }

#ifdef MOZ_LOGGING
  void DumpToFile(const char* aHint);
#endif

  // Create and return wl_buffer for underlying memory buffer if it's missing.
  wl_buffer* CreateWlBuffer();

  // Set wl_buffer from external source (WaylandBufferDMABUFHolder).
  void SetExternalWLBuffer(wl_buffer* aWLBuffer);

 protected:
  explicit WaylandBuffer(const LayoutDeviceIntSize& aSize);
  virtual ~WaylandBuffer();

  // if set the wl_buffer is managed by someone else
  // (for instance WaylandBufferHolder)
  // and WaylandBuffer can't destroy it.
  wl_buffer* mExternalWlBuffer = nullptr;
  AutoTArray<RefPtr<BufferTransaction>, 3> mBufferTransactions;
  LayoutDeviceIntSize mSize;
  static gfx::SurfaceFormat sFormat;
  RefPtr<BufferSurface> mBufferSurface;
  size_t mBufferAge = 0;

#ifdef MOZ_LOGGING
  static int mDumpSerial;
  static char* mDumpDir;
#endif
};

class WaylandBufferHolder final {
 public:
  bool Matches(BufferSurface* aSurface) const;

  wl_buffer* GetWLBuffer() { return mWLBuffer; }

  WaylandBufferHolder(BufferSurface* aSurface, wl_buffer* aWLBuffer);
  ~WaylandBufferHolder();

 private:
  // we own the wl_buffer and are responsible for its destruction
  wl_buffer* mWLBuffer = nullptr;
  uint32_t mUID = 0;
  uint32_t mPID = 0;
};

// BufferTransaction class holds wl_buffer callbacks after
// wl_surface_commit and manages wl_buffer. One WaylandBuffer and WaylandSurface
// can have active transactions over the same underlying memory buffer which
// allows to map/unmap wl_surfaces for instance during layered page scrolling.
// This helps slower Wayland compositors (like KDE) which doesn't release
// wl_buffers quickly and holds them for longer time.
class BufferTransaction {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(BufferTransaction);

  BufferTransaction(WaylandBuffer* aBuffer, wl_buffer* aWLBuffer,
                    bool aIsExternalBuffer);

  wl_buffer* BufferBorrowLocked(const WaylandSurfaceLock& aSurfaceLock);

  void BufferDetachCallback();
  void BufferDeleteCallback();

  bool IsAttached() {
    return mBufferState == BufferState::WaitingForDetach ||
           mBufferState == BufferState::WaitingForDelete;
  }
  bool IsDetached() { return mBufferState == BufferState::Detached; }
  bool IsDeleted() { return mBufferState == BufferState::Deleted; }

  void DeleteTransactionLocked(const WaylandSurfaceLock& aSurfaceLock);

  bool MatchesBuffer(uintptr_t aBuffer) {
    return aBuffer == reinterpret_cast<uintptr_t>(mBuffer.get());
  }

  bool CanRecycle(WaylandSurface* aSurface) {
    return IsDetached() && (!mSurface || mSurface == aSurface);
  }

 private:
  ~BufferTransaction();

  void WlBufferDeleteLocked(const WaylandSurfaceLock& aSurfaceLock);
  void DeleteLocked(const WaylandSurfaceLock& aSurfaceLock);

  RefPtr<WaylandSurface> mSurface;
  RefPtr<WaylandBuffer> mBuffer;

  enum class BufferState {
    Detached,
    Deleted,
    WaitingForDetach,
    WaitingForDelete
  };

  BufferState mBufferState{BufferState::Detached};
  wl_buffer* mWLBuffer = nullptr;
  bool mIsExternalBuffer = false;
};

}  // namespace mozilla::widget

#endif  // MOZILLA_WIDGET_GTK_WAYLAND_BUFFER_H
