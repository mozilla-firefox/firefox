/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_PersistentBUFFERPROVIDER_H
#define MOZILLA_GFX_PersistentBUFFERPROVIDER_H

#include "mozilla/Assertions.h"  // for MOZ_ASSERT, etc
#include "mozilla/RefCounted.h"
#include "mozilla/RefPtr.h"  // for RefPtr, already_AddRefed, etc
#include "mozilla/Vector.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/ActiveResource.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/LayersTypes.h"

namespace mozilla {

class ClientWebGLContext;

namespace gfx {
class SourceSurface;
class DrawTarget;
}  // namespace gfx

namespace layers {

class CompositableForwarder;
class FwdTransactionTracker;
class KnowsCompositor;
class TextureClient;

/**
 * A PersistentBufferProvider is for users which require the temporary use of
 * a DrawTarget to draw into. When they're done drawing they return the
 * DrawTarget, when they later need to continue drawing they get a DrawTarget
 * from the provider again, the provider will guarantee the contents of the
 * previously returned DrawTarget is persisted into the one newly returned.
 */
class PersistentBufferProvider : public RefCounted<PersistentBufferProvider>,
                                 public SupportsWeakPtr {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(PersistentBufferProvider)

  virtual ~PersistentBufferProvider() = default;

  virtual bool IsShared() const { return false; }
  virtual bool IsAccelerated() const { return false; }

  /**
   * Get a DrawTarget from the PersistentBufferProvider.
   *
   * \param aPersistedRect This indicates the area of the DrawTarget that needs
   *                       to have remained the same since the call to
   *                       ReturnDrawTarget.
   */
  virtual already_AddRefed<gfx::DrawTarget> BorrowDrawTarget(
      const gfx::IntRect& aPersistedRect) = 0;

  /**
   * Return a DrawTarget to the PersistentBufferProvider and indicate the
   * contents of this DrawTarget is to be considered current by the
   * BufferProvider. The caller should forget any references to the DrawTarget.
   */
  virtual bool ReturnDrawTarget(already_AddRefed<gfx::DrawTarget> aDT) = 0;

  /**
   * Temporarily borrow a snapshot of the provider. If a target is supplied,
   * the snapshot will be optimized for it, if applicable.
   */
  virtual already_AddRefed<gfx::SourceSurface> BorrowSnapshot(
      gfx::DrawTarget* aTarget = nullptr) = 0;

  virtual void ReturnSnapshot(
      already_AddRefed<gfx::SourceSurface> aSnapshot) = 0;

  virtual TextureClient* GetTextureClient() { return nullptr; }

  virtual Maybe<RemoteTextureOwnerId> GetRemoteTextureOwnerId() const {
    return Nothing();
  }

  virtual void OnMemoryPressure() {}

  virtual void OnShutdown() {}

  virtual bool SetKnowsCompositor(KnowsCompositor* aKnowsCompositor,
                                  bool& aOutLostFrontTexture) {
    return true;
  }

  virtual void ClearCachedResources() {}

  /**
   * Return true if this provider preserves the drawing state (clips,
   * transforms, etc.) across frames. In practice this means users of the
   * provider can skip popping all of the clips at the end of the frames and
   * pushing them back at the beginning of the following frames, which can be
   * costly (cf. bug 1294351).
   */
  virtual bool PreservesDrawingState() const = 0;

  /**
   * Whether or not the provider should be recreated, such as when profiling
   * heuristics determine this type of provider is no longer advantageous to
   * use.
   */
  virtual bool RequiresRefresh() const { return false; }

  virtual Maybe<SurfaceDescriptor> GetFrontBuffer() { return Nothing(); }

  virtual already_AddRefed<FwdTransactionTracker> UseCompositableForwarder(
      CompositableForwarder* aForwarder) {
    return nullptr;
  }
};

class PersistentBufferProviderBasic : public PersistentBufferProvider {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(PersistentBufferProviderBasic,
                                          override)

  static already_AddRefed<PersistentBufferProviderBasic> Create(
      gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
      gfx::BackendType aBackend);

  explicit PersistentBufferProviderBasic(gfx::DrawTarget* aTarget);

  already_AddRefed<gfx::DrawTarget> BorrowDrawTarget(
      const gfx::IntRect& aPersistedRect) override;

  bool ReturnDrawTarget(already_AddRefed<gfx::DrawTarget> aDT) override;

  already_AddRefed<gfx::SourceSurface> BorrowSnapshot(
      gfx::DrawTarget* aTarget) override;

  void ReturnSnapshot(already_AddRefed<gfx::SourceSurface> aSnapshot) override;

  bool PreservesDrawingState() const override { return true; }

  void OnShutdown() override { Destroy(); }

 protected:
  void Destroy();

  ~PersistentBufferProviderBasic() override;

  RefPtr<gfx::DrawTarget> mDrawTarget;
  RefPtr<gfx::SourceSurface> mSnapshot;
};

class PersistentBufferProviderAccelerated : public PersistentBufferProvider {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(PersistentBufferProviderAccelerated,
                                          override)

  static already_AddRefed<PersistentBufferProviderAccelerated> Create(
      gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
      KnowsCompositor* aKnowsCompositor);

  bool IsAccelerated() const override { return true; }

  already_AddRefed<gfx::DrawTarget> BorrowDrawTarget(
      const gfx::IntRect& aPersistedRect) override;

  bool ReturnDrawTarget(already_AddRefed<gfx::DrawTarget> aDT) override;

  already_AddRefed<gfx::SourceSurface> BorrowSnapshot(
      gfx::DrawTarget* aTarget) override;

  void ReturnSnapshot(already_AddRefed<gfx::SourceSurface> aSnapshot) override;

  Maybe<RemoteTextureOwnerId> GetRemoteTextureOwnerId() const override {
    return Some(mRemoteTextureOwnerId);
  }

  bool PreservesDrawingState() const override { return true; }

  void OnShutdown() override { Destroy(); }

  Maybe<SurfaceDescriptor> GetFrontBuffer() override;

  bool RequiresRefresh() const override;

  already_AddRefed<FwdTransactionTracker> UseCompositableForwarder(
      CompositableForwarder* aForwarder) override;

 protected:
  explicit PersistentBufferProviderAccelerated(
      RemoteTextureOwnerId aRemoteTextureOwnerId,
      const RefPtr<TextureClient>& aTexture);
  ~PersistentBufferProviderAccelerated() override;

  void Destroy();

  RemoteTextureOwnerId mRemoteTextureOwnerId;
  RefPtr<TextureClient> mTexture;

  RefPtr<gfx::DrawTarget> mDrawTarget;
  RefPtr<gfx::SourceSurface> mSnapshot;
};

/**
 * Provides access to a buffer which can be sent to the compositor without
 * requiring a copy.
 */
class PersistentBufferProviderShared : public PersistentBufferProvider,
                                       public ActiveResource {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(PersistentBufferProviderShared,
                                          override)

  static already_AddRefed<PersistentBufferProviderShared> Create(
      gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
      KnowsCompositor* aKnowsCompositor, bool aWillReadFrequently = false,
      const Maybe<uint64_t>& aWindowID = Nothing());

  bool IsShared() const override { return true; }

  already_AddRefed<gfx::DrawTarget> BorrowDrawTarget(
      const gfx::IntRect& aPersistedRect) override;

  bool ReturnDrawTarget(already_AddRefed<gfx::DrawTarget> aDT) override;

  already_AddRefed<gfx::SourceSurface> BorrowSnapshot(
      gfx::DrawTarget* aTarget) override;

  void ReturnSnapshot(already_AddRefed<gfx::SourceSurface> aSnapshot) override;

  TextureClient* GetTextureClient() override;

  void NotifyInactive() override;

  void OnShutdown() override { Destroy(); }

  bool SetKnowsCompositor(KnowsCompositor* aKnowsCompositor,
                          bool& aOutLostFrontTexture) override;

  void ClearCachedResources() override;

  bool PreservesDrawingState() const override { return false; }

 protected:
  PersistentBufferProviderShared(gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
                                 KnowsCompositor* aKnowsCompositor,
                                 RefPtr<TextureClient>& aTexture,
                                 bool aWillReadFrequently,
                                 const Maybe<uint64_t>& aWindowID);

  ~PersistentBufferProviderShared();

  TextureClient* GetTexture(const Maybe<uint32_t>& aIndex);
  bool CheckIndex(uint32_t aIndex) { return aIndex < mTextures.length(); }

  void Destroy();

  gfx::IntSize mSize;
  gfx::SurfaceFormat mFormat;
  RefPtr<KnowsCompositor> mKnowsCompositor;
  // If the texture has its own synchronization then copying back from the
  // previous texture can cause contention issues and even deadlocks. So we use
  // a separate permanent back buffer and copy into the shared back buffer when
  // the DrawTarget is returned, before making it the new front buffer.
  RefPtr<TextureClient> mPermanentBackBuffer;
  static const size_t kMaxTexturesAllowed = 5;
  Vector<RefPtr<TextureClient>, kMaxTexturesAllowed + 2> mTextures;
  // Offset of the texture in mTextures that the canvas uses.
  Maybe<uint32_t> mBack;
  // Offset of the texture in mTextures that is presented to the compositor.
  Maybe<uint32_t> mFront;
  // Whether to avoid acceleration.
  bool mWillReadFrequently = false;
  // Owning window ID of the buffer provider.
  Maybe<uint64_t> mWindowID;

  RefPtr<gfx::DrawTarget> mDrawTarget;
  RefPtr<gfx::SourceSurface> mSnapshot;
  size_t mMaxAllowedTextures = kMaxTexturesAllowed;
};

struct AutoReturnSnapshot final {
  PersistentBufferProvider* mBufferProvider;
  RefPtr<gfx::SourceSurface>* mSnapshot;

  explicit AutoReturnSnapshot(PersistentBufferProvider* aProvider = nullptr)
      : mBufferProvider(aProvider), mSnapshot(nullptr) {}

  ~AutoReturnSnapshot() {
    if (mBufferProvider) {
      mBufferProvider->ReturnSnapshot(mSnapshot ? mSnapshot->forget()
                                                : nullptr);
    }
  }
};

}  // namespace layers
}  // namespace mozilla

#endif
