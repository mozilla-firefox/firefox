/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACE_DMABUF_H_
#define SHARED_SURFACE_DMABUF_H_

#include "SharedSurface.h"
#include "mozilla/widget/DMABufSurface.h"

namespace mozilla {
namespace gl {

class GLContext;
class GLLibraryEGL;

class SharedSurface_DMABUF final : public SharedSurface {
 public:
  const RefPtr<DMABufSurface> mSurface;

  static UniquePtr<SharedSurface_DMABUF> Create(const SharedSurfaceDesc&);

 private:
  SharedSurface_DMABUF(const SharedSurfaceDesc&, UniquePtr<MozFramebuffer>,
                       RefPtr<DMABufSurface>);

  void UpdateProdTexture(const MutexAutoLock& curAutoLock);

 public:
  virtual ~SharedSurface_DMABUF();

  // Exclusive Content/WebGL lock/unlock of surface for write
  virtual void LockProdImpl() override {}
  virtual void UnlockProdImpl() override {}

  // Non-exclusive Content/WebGL lock/unlock of surface for write
  virtual void ProducerAcquireImpl() override {}
  virtual void ProducerReleaseImpl() override;

  // Non-exclusive Content/WebGL lock/unlock for read from surface
  virtual void ProducerReadAcquireImpl() override {}
  virtual void ProducerReadReleaseImpl() override {}

  Maybe<layers::SurfaceDescriptor> ToSurfaceDescriptor() override;

  void WaitForBufferOwnership() override;
};

class SurfaceFactory_DMABUF : public SurfaceFactory {
 public:
  static UniquePtr<SurfaceFactory_DMABUF> Create(GLContext& gl);

  explicit SurfaceFactory_DMABUF(GLContext&);

  bool SupportsCspaces() const override { return true; }

 public:
  virtual UniquePtr<SharedSurface> CreateSharedImpl(
      const SharedSurfaceDesc& desc) override {
    return SharedSurface_DMABUF::Create(desc);
  }

  bool CanCreateSurface(GLContext& gl);
};

}  // namespace gl

}  // namespace mozilla

#endif /* SHARED_SURFACE_DMABUF_H_ */
