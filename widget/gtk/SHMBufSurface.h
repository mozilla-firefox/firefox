/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHMBufSurface_h_
#define SHMBufSurface_h_

#include "Units.h"
#ifdef MOZ_WAYLAND
#  include "WUniquePtr.h"
#endif
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/widget/BufferSurface.h"

struct wl_shm_pool;
namespace mozilla::layers {
class BufferDescriptor;
}
class DMABufSurface;

class SHMBufSurface : public BufferSurface {
 public:
  SHMBufSurface* GetAsSHMBufSurface() override { return this; }

  void* GetImageData() override;
  SHMBufSurface();

  virtual already_AddRefed<DMABufSurface> UploadToDMABufSurface(
      mozilla::gl::GLContext* aGLContext) {
    return nullptr;
  }

 protected:
  ~SHMBufSurface() override;

  // Creates the plane texture if it's missing and refreshes it from shared
  // memory if SetTextureNeedsUpload() was called.
  bool CreateTexture(mozilla::gl::GLContext* aGLContext, int aPlane) override;

  // Uploads shared memory content of aPlane to mTexture[aPlane].
  // It's called by CreateTexture() only, with mGL made current and
  // mTexture[aPlane] already generated. aNeedInit is set when the texture
  // storage needs to be allocated, i.e. the texture is a fresh one.
  virtual bool UploadTexture(int aPlane, bool aNeedInit) = 0;

  // Surface is backed by external memory
  uint8_t* mBuffer = nullptr;

#ifdef MOZ_WAYLAND
  // SHMBufSurface is backed by shared memory (wl_shm_pool) and provides a
  // CPU-accessible buffer we can draw into. The shared memory pool is created
  // and owned directly by the surface.
  mozilla::WUniquePtr<wl_shm_pool> mShmPool;
  mozilla::ipc::MutableSharedMemoryHandle mShmHandle;
  mozilla::ipc::SharedMemoryMapping mShm;
#endif
};

class SHMBufSurfaceRGBA final : public SHMBufSurface {
 public:
  static RefPtr<SHMBufSurfaceRGBA> Create(const mozilla::gfx::IntSize& aSize,
                                          int32_t aFOURCCFormat);
  static RefPtr<SHMBufSurfaceRGBA> Create(
      uint8_t* aBuffer, const mozilla::layers::BufferDescriptor& aDescriptor);

  already_AddRefed<mozilla::gfx::DataSourceSurface> GetAsSourceSurface()
      override;
  already_AddRefed<mozilla::gfx::DrawTarget> Lock() override;

  already_AddRefed<DMABufSurface> UploadToDMABufSurface(
      mozilla::gl::GLContext* aGLContext) override;

  int GetTextureCount() override { return 1; }

#ifdef MOZ_WAYLAND
  wl_buffer* CreateWlBuffer() override;
#endif

  SHMBufSurfaceRGBA();

 private:
  ~SHMBufSurfaceRGBA() override = default;

  bool UploadTexture(int aPlane, bool aNeedInit) override;

  bool CreateImpl(const mozilla::gfx::IntSize& aSize, int32_t aFOURCCFormat);
  bool CreateImpl(uint8_t* aBuffer,
                  const mozilla::layers::BufferDescriptor& aDescriptor);
};

class SHMBufSurfaceYUV final : public SHMBufSurface {
 public:
  static RefPtr<SHMBufSurfaceYUV> Create(
      uint8_t* aBuffer, const mozilla::layers::BufferDescriptor& aDescriptor);

  already_AddRefed<mozilla::gfx::DataSourceSurface> GetAsSourceSurface()
      override {
    return nullptr;
  };

  already_AddRefed<DMABufSurface> UploadToDMABufSurface(
      mozilla::gl::GLContext* aGLContext) override;

  SHMBufSurfaceYUV();

 private:
  ~SHMBufSurfaceYUV() override = default;

  bool UploadTexture(int aPlane, bool aNeedInit) override;

  bool CreateImpl(uint8_t* aBuffer,
                  const mozilla::layers::BufferDescriptor& aDescriptor);

  mozilla::layers::YCbCrDescriptor mDescriptor;
};

#endif
