/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* SharedSurface abstracts an actual surface (can be a GL texture, but
 * not necessarily) that handles sharing.
 * Its specializations are:
 *     SharedSurface_Basic (client-side bitmap, does readback)
 *     SharedSurface_GLTexture
 *     SharedSurface_EGLImage
 *     SharedSurface_ANGLEShareHandle
 */

#ifndef SHARED_SURFACE_H_
#define SHARED_SURFACE_H_

#include <stdint.h>

#include "GLContext.h"  // Bug 1635644
#include "GLContextTypes.h"
#include "GLDefs.h"
#include "mozilla/Attributes.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/Mutex.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "SurfaceTypes.h"

class nsIThread;

namespace mozilla {
namespace gfx {
class DataSourceSurface;
class DrawTarget;
}  // namespace gfx

namespace layers {
class KnowsCompositor;
enum class LayersBackend : int8_t;
class LayersIPCChannel;
class SharedSurfaceTextureClient;
class SurfaceDescriptor;
class TextureClient;
enum class TextureFlags : uint32_t;
enum class TextureType : int8_t;
}  // namespace layers

namespace gl {

class MozFramebuffer;
struct ScopedBindFramebuffer;
class SurfaceFactory;

struct PartialSharedSurfaceDesc {
  const WeakPtr<GLContext> gl;
  const SharedSurfaceType type;
  const layers::TextureType consumerType;
  const bool canRecycle;

  bool operator==(const PartialSharedSurfaceDesc& rhs) const {
    return gl == rhs.gl && type == rhs.type &&
           consumerType == rhs.consumerType && canRecycle == rhs.canRecycle;
  }
};
struct SharedSurfaceDesc : public PartialSharedSurfaceDesc {
  gfx::IntSize size = {};
  gfx::ColorSpace2 colorSpace = gfx::ColorSpace2::UNKNOWN;
  gfx::TransferFunction transferFunction = gfx::TransferFunction::SRGB;

  bool operator==(const SharedSurfaceDesc& rhs) const {
    return PartialSharedSurfaceDesc::operator==(rhs) && size == rhs.size &&
           colorSpace == rhs.colorSpace &&
           transferFunction == rhs.transferFunction;
  }
  bool operator!=(const SharedSurfaceDesc& rhs) const {
    return !(*this == rhs);
  }
};

class SharedSurface {
 public:
  const SharedSurfaceDesc mDesc;
  const UniquePtr<MozFramebuffer> mFb;  // null if we should use fb=0.

 protected:
  bool mIsLocked = false;
  bool mIsProducerAcquired = false;

  SharedSurface(const SharedSurfaceDesc&, UniquePtr<MozFramebuffer>);

 public:
  virtual ~SharedSurface();

  bool IsLocked() const { return mIsLocked; }
  bool IsProducerAcquired() const { return mIsProducerAcquired; }

  // This locks the SharedSurface as the production buffer for the context.
  // This is needed by backends which use PBuffers and/or EGLSurfaces.
  void LockProd();

  // Unlocking is harmless if we're already unlocked.
  void UnlockProd();

  // This surface has been moved to the front buffer and will not be locked
  // again until it is recycled. Do any finalization steps here.
  virtual void Commit() {}

 protected:
  virtual void LockProdImpl() {};
  virtual void UnlockProdImpl() {};

  virtual void ProducerAcquireImpl() {};
  virtual void ProducerReleaseImpl() {};

  virtual void ProducerReadAcquireImpl() { ProducerAcquireImpl(); }
  virtual void ProducerReadReleaseImpl() { ProducerReleaseImpl(); }

 public:
  void ProducerAcquire() {
    MOZ_ASSERT(!mIsProducerAcquired);
    ProducerAcquireImpl();
    mIsProducerAcquired = true;
  }
  void ProducerRelease() {
    MOZ_ASSERT(mIsProducerAcquired);
    ProducerReleaseImpl();
    mIsProducerAcquired = false;
  }
  void ProducerReadAcquire() {
    MOZ_ASSERT(!mIsProducerAcquired);
    ProducerReadAcquireImpl();
    mIsProducerAcquired = true;
  }
  void ProducerReadRelease() {
    MOZ_ASSERT(mIsProducerAcquired);
    ProducerReadReleaseImpl();
    mIsProducerAcquired = false;
  }

  // This function waits until the buffer is no longer being used.
  // To optimize the performance, some implementaions recycle SharedSurfaces
  // even when its buffer is still being used.
  virtual void WaitForBufferOwnership() {}

  // Returns true if the buffer is available.
  // You can call WaitForBufferOwnership to wait for availability.
  virtual bool IsBufferAvailable() const { return true; }

  virtual bool NeedsIndirectReads() const { return false; }

  // Returns true if the surface is still valid to use. If false, the underlying
  // resource has been released and we must allocate a new surface instead.
  virtual bool IsValid() const { return bool(mDesc.gl); };

  virtual Maybe<layers::SurfaceDescriptor> ToSurfaceDescriptor() = 0;

  void BeginWrite() {
    WaitForBufferOwnership();
    ProducerAcquire();
    LockProd();
  }

  void EndWrite() {
    UnlockProd();
    ProducerRelease();
    Commit();
  }

  void BeginRead() {
    WaitForBufferOwnership();
    LockProd();
    ProducerReadAcquire();
  }

  void EndRead() {
    ProducerReadRelease();
    UnlockProd();
  }
};

// -

class SurfaceFactory {
 public:
  const PartialSharedSurfaceDesc mDesc;

  layers::TextureType GetConsumerType() const { return mDesc.consumerType; }

 protected:
  Mutex mMutex MOZ_UNANNOTATED;

 public:
  static UniquePtr<SurfaceFactory> Create(GLContext*, layers::TextureType);

 protected:
  explicit SurfaceFactory(const PartialSharedSurfaceDesc&);

 public:
  virtual ~SurfaceFactory();

 protected:
  virtual UniquePtr<SharedSurface> CreateSharedImpl(
      const SharedSurfaceDesc&) = 0;

 public:
  virtual bool SupportsCspaces() const { return false; }

  UniquePtr<SharedSurface> CreateShared(const gfx::IntSize& size,
                                        gfx::ColorSpace2 cs) {
    if (!SupportsCspaces()) {
      cs = gfx::ColorSpace2::Display;
    }
    return CreateSharedImpl({mDesc, size, cs});
  }
};

template <typename T>
inline UniquePtr<T> AsUnique(T* const p) {
  return UniquePtr<T>(p);
}

}  // namespace gl
}  // namespace mozilla

#endif  // SHARED_SURFACE_H_
