/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PersistentBufferProvider.h"

#include "gfxPlatform.h"
#include "mozilla/Maybe.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/gfx/CanvasManagerChild.h"
#include "mozilla/gfx/DrawTargetWebgl.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/KnowsCompositor.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/layers/TextureRecorded.h"
#include "pratom.h"

namespace mozilla {

using namespace gfx;

namespace layers {

PersistentBufferProviderBasic::PersistentBufferProviderBasic(DrawTarget* aDt)
    : mDrawTarget(aDt) {
  MOZ_COUNT_CTOR(PersistentBufferProviderBasic);
}

PersistentBufferProviderBasic::~PersistentBufferProviderBasic() {
  MOZ_COUNT_DTOR(PersistentBufferProviderBasic);
  Destroy();
}

already_AddRefed<gfx::DrawTarget>
PersistentBufferProviderBasic::BorrowDrawTarget(
    const gfx::IntRect& aPersistedRect) {
  MOZ_ASSERT(!mSnapshot);
  RefPtr<gfx::DrawTarget> dt(mDrawTarget);
  return dt.forget();
}

bool PersistentBufferProviderBasic::ReturnDrawTarget(
    already_AddRefed<gfx::DrawTarget> aDT) {
  RefPtr<gfx::DrawTarget> dt(aDT);
  MOZ_ASSERT(mDrawTarget == dt);
  if (dt) {
    // Since SkiaGL default to storing drawing command until flush
    // we have to flush it before present.
    dt->Flush();
  }
  return true;
}

already_AddRefed<gfx::SourceSurface>
PersistentBufferProviderBasic::BorrowSnapshot(gfx::DrawTarget* aTarget) {
  mSnapshot = mDrawTarget->Snapshot();
  RefPtr<SourceSurface> snapshot = mSnapshot;
  return snapshot.forget();
}

void PersistentBufferProviderBasic::ReturnSnapshot(
    already_AddRefed<gfx::SourceSurface> aSnapshot) {
  RefPtr<SourceSurface> snapshot = aSnapshot;
  MOZ_ASSERT(!snapshot || snapshot == mSnapshot);
  mSnapshot = nullptr;
}

void PersistentBufferProviderBasic::Destroy() {
  mSnapshot = nullptr;
  mDrawTarget = nullptr;
}

// static
already_AddRefed<PersistentBufferProviderBasic>
PersistentBufferProviderBasic::Create(gfx::IntSize aSize,
                                      gfx::SurfaceFormat aFormat,
                                      gfx::BackendType aBackend) {
  RefPtr<DrawTarget> dt =
      gfxPlatform::GetPlatform()->CreateDrawTargetForBackend(aBackend, aSize,
                                                             aFormat);

  if (dt) {
    // This is simply to ensure the DrawTarget gets initialized, and will detect
    // a device reset, even if we're on the main thread.
    dt->ClearRect(Rect(0, 0, 0, 0));
  }

  if (!dt || !dt->IsValid()) {
    return nullptr;
  }

  RefPtr provider = MakeRefPtr<PersistentBufferProviderBasic>(dt);

  return provider.forget();
}

static already_AddRefed<TextureClient> CreateTexture(
    KnowsCompositor* aKnowsCompositor, gfx::SurfaceFormat aFormat,
    gfx::IntSize aSize, bool aWillReadFrequently = false,
    bool aUseRemoteTexture = false) {
  TextureAllocationFlags flags = ALLOC_DEFAULT;
  if (aWillReadFrequently) {
    flags = TextureAllocationFlags(flags | ALLOC_DO_NOT_ACCELERATE);
  }
  if (aUseRemoteTexture) {
    flags = TextureAllocationFlags(flags | ALLOC_FORCE_REMOTE);
  }
  RefPtr<TextureClient> tc = TextureClient::CreateForDrawing(
      aKnowsCompositor, aFormat, aSize, BackendSelector::Canvas,
      TextureFlags::DEFAULT | TextureFlags::NON_BLOCKING_READ_LOCK, flags);
  return tc.forget();
}

// static
already_AddRefed<PersistentBufferProviderAccelerated>
PersistentBufferProviderAccelerated::Create(gfx::IntSize aSize,
                                            gfx::SurfaceFormat aFormat,
                                            KnowsCompositor* aKnowsCompositor) {
  if (!aKnowsCompositor || !aKnowsCompositor->GetTextureForwarder() ||
      !aKnowsCompositor->GetTextureForwarder()->IPCOpen()) {
    return nullptr;
  }

  if (!DrawTargetWebgl::CanCreate(aSize, aFormat)) {
#ifdef XP_WIN
    // Direct2D acceleration does not require DrawTargetWebgl, but still
    // requires PersistentBufferProviderAccelerated.
    if (!TextureData::IsRemote(aKnowsCompositor, BackendSelector::Canvas,
                               aFormat, aSize)) {
      return nullptr;
    }
#else
    return nullptr;
#endif
  }

  RefPtr<TextureClient> texture = CreateTexture(
      aKnowsCompositor, aFormat, aSize, false, /* aUseRemoteTexture */ true);
  if (!texture) {
    return nullptr;
  }

  auto* recordedTextureData =
      texture->GetInternalData()->AsRecordedTextureData();
  if (!recordedTextureData) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    gfxCriticalNoteOnce << "Expected RecordedTextureData";
    return nullptr;
  }

  RefPtr<PersistentBufferProviderAccelerated> provider =
      new PersistentBufferProviderAccelerated(
          recordedTextureData->mRemoteTextureOwnerId, texture);
  return provider.forget();
}

PersistentBufferProviderAccelerated::PersistentBufferProviderAccelerated(
    RemoteTextureOwnerId aRemoteTextureOwnerId,
    const RefPtr<TextureClient>& aTexture)
    : mRemoteTextureOwnerId(aRemoteTextureOwnerId), mTexture(aTexture) {
  MOZ_COUNT_CTOR(PersistentBufferProviderAccelerated);
}

PersistentBufferProviderAccelerated::~PersistentBufferProviderAccelerated() {
  MOZ_COUNT_DTOR(PersistentBufferProviderAccelerated);
  Destroy();
}

void PersistentBufferProviderAccelerated::Destroy() {
  mSnapshot = nullptr;
  mDrawTarget = nullptr;

  if (mTexture) {
    if (mTexture->IsLocked()) {
      MOZ_ASSERT(false);
      mTexture->Unlock();
    }
    mTexture = nullptr;
  }
}

already_AddRefed<gfx::DrawTarget>
PersistentBufferProviderAccelerated::BorrowDrawTarget(
    const gfx::IntRect& aPersistedRect) {
  if (!mDrawTarget) {
    if (aPersistedRect.IsEmpty()) {
      mTexture->GetInternalData()->InvalidateContents();
    }
    if (!mTexture->Lock(OpenMode::OPEN_READ_WRITE)) {
      return nullptr;
    }
    mDrawTarget = mTexture->BorrowDrawTarget();
    if (!mDrawTarget || !mDrawTarget->IsValid()) {
      mDrawTarget = nullptr;
      mTexture->Unlock();
      return nullptr;
    }
  }
  return do_AddRef(mDrawTarget);
}

bool PersistentBufferProviderAccelerated::ReturnDrawTarget(
    already_AddRefed<gfx::DrawTarget> aDT) {
  {
    RefPtr<gfx::DrawTarget> dt(aDT);
    MOZ_ASSERT(mDrawTarget == dt);
    if (!mDrawTarget) {
      return false;
    }
    mDrawTarget = nullptr;
  }
  mTexture->Unlock();
  return true;
}

already_AddRefed<gfx::SourceSurface>
PersistentBufferProviderAccelerated::BorrowSnapshot(gfx::DrawTarget* aTarget) {
  if (mDrawTarget) {
    MOZ_ASSERT(mTexture->IsLocked());
  } else {
    if (mTexture->IsLocked()) {
      MOZ_ASSERT(false);
      return nullptr;
    }
    if (!mTexture->Lock(OpenMode::OPEN_READ)) {
      return nullptr;
    }
  }
  mSnapshot = mTexture->BorrowSnapshot();
  return do_AddRef(mSnapshot);
}

void PersistentBufferProviderAccelerated::ReturnSnapshot(
    already_AddRefed<gfx::SourceSurface> aSnapshot) {
  RefPtr<SourceSurface> snapshot = aSnapshot;
  MOZ_ASSERT(!snapshot || snapshot == mSnapshot);
  snapshot = nullptr;
  mTexture->ReturnSnapshot(mSnapshot.forget());
  if (!mDrawTarget) {
    mTexture->Unlock();
  }
}

Maybe<SurfaceDescriptor> PersistentBufferProviderAccelerated::GetFrontBuffer() {
  SurfaceDescriptor desc;
  if (mTexture->GetInternalData()->Serialize(desc)) {
    return Some(desc);
  }
  return Nothing();
}

bool PersistentBufferProviderAccelerated::RequiresRefresh() const {
  return mTexture->GetInternalData()->RequiresRefresh();
}

already_AddRefed<FwdTransactionTracker>
PersistentBufferProviderAccelerated::UseCompositableForwarder(
    CompositableForwarder* aForwarder) {
  return mTexture->GetInternalData()->UseCompositableForwarder(aForwarder);
}

// static
already_AddRefed<PersistentBufferProviderShared>
PersistentBufferProviderShared::Create(gfx::IntSize aSize,
                                       gfx::SurfaceFormat aFormat,
                                       KnowsCompositor* aKnowsCompositor,
                                       bool aWillReadFrequently,
                                       const Maybe<uint64_t>& aWindowID) {
  if (!aKnowsCompositor || !aKnowsCompositor->GetTextureForwarder() ||
      !aKnowsCompositor->GetTextureForwarder()->IPCOpen()) {
    return nullptr;
  }

  if (!StaticPrefs::layers_shared_buffer_provider_enabled()) {
    return nullptr;
  }

#ifdef XP_WIN
  // Bug 1285271 - Disable shared buffer provider on Windows with D2D due to
  // instability.
  aWillReadFrequently = true;
#endif

  RefPtr<TextureClient> texture =
      CreateTexture(aKnowsCompositor, aFormat, aSize, aWillReadFrequently);
  if (!texture) {
    return nullptr;
  }

  RefPtr<PersistentBufferProviderShared> provider =
      new PersistentBufferProviderShared(aSize, aFormat, aKnowsCompositor,
                                         texture, aWillReadFrequently,
                                         aWindowID);
  return provider.forget();
}

PersistentBufferProviderShared::PersistentBufferProviderShared(
    gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
    KnowsCompositor* aKnowsCompositor, RefPtr<TextureClient>& aTexture,
    bool aWillReadFrequently, const Maybe<uint64_t>& aWindowID)
    : mSize(aSize),
      mFormat(aFormat),
      mKnowsCompositor(aKnowsCompositor),
      mFront(Nothing()),
      mWillReadFrequently(aWillReadFrequently),
      mWindowID(aWindowID) {
  MOZ_ASSERT(aKnowsCompositor);
  if (mTextures.append(aTexture)) {
    mBack = Some<uint32_t>(0);
  }

  // XXX KnowsCompositor could be used for mMaxAllowedTextures
  if (gfxVars::UseWebRenderTripleBufferingWin()) {
    ++mMaxAllowedTextures;
  }

  MOZ_COUNT_CTOR(PersistentBufferProviderShared);
}

PersistentBufferProviderShared::~PersistentBufferProviderShared() {
  MOZ_COUNT_DTOR(PersistentBufferProviderShared);

  if (IsActivityTracked()) {
    if (auto* cm = CanvasManagerChild::Get()) {
      cm->GetActiveResourceTracker()->RemoveObject(this);
    } else {
      MOZ_ASSERT_UNREACHABLE("Tracked but no CanvasManagerChild!");
    }
  }

  Destroy();
}

bool PersistentBufferProviderShared::SetKnowsCompositor(
    KnowsCompositor* aKnowsCompositor, bool& aOutLostFrontTexture) {
  MOZ_ASSERT(aKnowsCompositor);
  MOZ_ASSERT(!aOutLostFrontTexture);
  if (!aKnowsCompositor) {
    return false;
  }

  if (mKnowsCompositor == aKnowsCompositor) {
    // The forwarder should not change most of the time.
    return true;
  }

  if (IsActivityTracked()) {
    if (auto* cm = CanvasManagerChild::Get()) {
      cm->GetActiveResourceTracker()->RemoveObject(this);
    } else {
      MOZ_ASSERT_UNREACHABLE("Tracked but no CanvasManagerChild!");
    }
  }

  if (mKnowsCompositor->GetTextureForwarder() !=
          aKnowsCompositor->GetTextureForwarder() ||
      mKnowsCompositor->GetCompositorBackendType() !=
          aKnowsCompositor->GetCompositorBackendType()) {
    // We are going to be used with an different and/or incompatible forwarder.
    // This should be extremely rare. We have to copy the front buffer into a
    // texture that is compatible with the new forwarder.

    // Grab the current front buffer.
    RefPtr<TextureClient> prevTexture = GetTexture(mFront);

    // Get rid of everything else
    Destroy();

    if (prevTexture && !prevTexture->IsValid()) {
      aOutLostFrontTexture = true;
    } else if (prevTexture && prevTexture->IsValid()) {
      RefPtr<TextureClient> newTexture =
          CreateTexture(aKnowsCompositor, mFormat, mSize, mWillReadFrequently);

      MOZ_ASSERT(newTexture);
      if (!newTexture) {
        return false;
      }

      // If we early-return in one of the following branches, we will
      // leave the buffer provider in an empty state, since we called
      // Destroy. Not ideal but at least we won't try to use it with a
      // an incompatible ipc channel.

      if (!newTexture->Lock(OpenMode::OPEN_WRITE)) {
        return false;
      }

      if (!prevTexture->Lock(OpenMode::OPEN_READ)) {
        newTexture->Unlock();
        return false;
      }

      bool success =
          prevTexture->CopyToTextureClient(newTexture, nullptr, nullptr);

      prevTexture->Unlock();
      newTexture->Unlock();

      if (!success) {
        return false;
      }

      if (!mTextures.append(newTexture)) {
        return false;
      }
      mFront = Some<uint32_t>(mTextures.length() - 1);
      mBack = mFront;
    }
  }

  mKnowsCompositor = aKnowsCompositor;

  return true;
}

TextureClient* PersistentBufferProviderShared::GetTexture(
    const Maybe<uint32_t>& aIndex) {
  if (aIndex.isNothing() || !CheckIndex(aIndex.value())) {
    return nullptr;
  }
  return mTextures[aIndex.value()];
}

already_AddRefed<gfx::DrawTarget>
PersistentBufferProviderShared::BorrowDrawTarget(
    const gfx::IntRect& aPersistedRect) {
  if (!mKnowsCompositor->GetTextureForwarder() ||
      !mKnowsCompositor->GetTextureForwarder()->IPCOpen()) {
    return nullptr;
  }

  auto* cm = CanvasManagerChild::Get();
  if (NS_WARN_IF(!cm)) {
    return nullptr;
  }

  MOZ_ASSERT(!mSnapshot);

  if (IsActivityTracked()) {
    cm->GetActiveResourceTracker()->MarkUsed(this);
  } else {
    cm->GetActiveResourceTracker()->AddObject(this);
  }

  if (mDrawTarget) {
    RefPtr<gfx::DrawTarget> dt(mDrawTarget);
    return dt.forget();
  }

  auto previousBackBuffer = mBack;

  TextureClient* tex = GetTexture(mBack);

  // First try to reuse the current back buffer. If we can do that it means
  // we can skip copying its content to the new back buffer.
  if (tex && tex->IsReadLocked()) {
    // The back buffer is currently used by the compositor, we can't draw
    // into it.
    tex = nullptr;
  }

  if (!tex) {
    // Try to grab an already allocated texture if any is available.
    for (uint32_t i = 0; i < mTextures.length(); ++i) {
      if (!mTextures[i]->IsReadLocked()) {
        mBack = Some(i);
        tex = mTextures[i];
        break;
      }
    }
  }

  if (!tex) {
    // We have to allocate a new texture.
    if (mTextures.length() >= mMaxAllowedTextures) {
      // We should never need to buffer that many textures, something's wrong.
      // In theory we throttle the main thread when the compositor can't keep
      // up, so we shoud never get in a situation where we sent 4 textures to
      // the compositor and the latter has not released any of them. In
      // practice, though, the throttling mechanism appears to have some issues,
      // especially when switching between layer managers (during tab-switch).
      // To make sure we don't get too far ahead of the compositor, we send a
      // sync ping to the compositor thread...
      mKnowsCompositor->SyncWithCompositor(mWindowID);
      // ...and try again.
      for (uint32_t i = 0; i < mTextures.length(); ++i) {
        if (!mTextures[i]->IsReadLocked()) {
          gfxCriticalNote << "Managed to allocate after flush.";
          mBack = Some(i);
          tex = mTextures[i];
          break;
        }
      }

      if (!tex) {
        gfxCriticalNote << "Unexpected BufferProvider over-production.";
        // It would be pretty bad to keep piling textures up at this point so we
        // call NotifyInactive to remove some of our textures.
        NotifyInactive();
        // Give up now. The caller can fall-back to a non-shared buffer
        // provider.
        return nullptr;
      }
    }

    RefPtr<TextureClient> newTexture =
        CreateTexture(mKnowsCompositor, mFormat, mSize, mWillReadFrequently);

    MOZ_ASSERT(newTexture);
    if (newTexture) {
      if (mTextures.append(newTexture)) {
        tex = newTexture;
        mBack = Some<uint32_t>(mTextures.length() - 1);
      }
    }
  }

  if (!tex) {
    return nullptr;
  }

  if (mPermanentBackBuffer) {
    // If we have a permanent back buffer lock the selected one and switch to
    // the permanent one before borrowing the DrawTarget. We will copy back into
    // the selected one when ReturnDrawTarget is called, before we make it the
    // new front buffer.
    if (!tex->Lock(OpenMode::OPEN_WRITE)) {
      return nullptr;
    }
    tex = mPermanentBackBuffer;
  } else {
    // Copy from the previous back buffer if required.
    Maybe<TextureClientAutoLock> autoReadLock;
    TextureClient* previous = nullptr;
    if (mBack != previousBackBuffer && !aPersistedRect.IsEmpty()) {
      if (tex->HasSynchronization()) {
        // We are about to read lock a texture that is in use by the compositor
        // and has synchronization. To prevent possible future contention we
        // switch to using a permanent back buffer.
        mPermanentBackBuffer = CreateTexture(mKnowsCompositor, mFormat, mSize,
                                             mWillReadFrequently);
        if (!mPermanentBackBuffer) {
          return nullptr;
        }
        if (!tex->Lock(OpenMode::OPEN_WRITE)) {
          return nullptr;
        }
        tex = mPermanentBackBuffer;
      }

      previous = GetTexture(previousBackBuffer);
      if (previous) {
        autoReadLock.emplace(previous, OpenMode::OPEN_READ);
      }
    }

    if (!tex->Lock(OpenMode::OPEN_READ_WRITE)) {
      return nullptr;
    }

    if (autoReadLock.isSome() && autoReadLock->Succeeded() && previous) {
      DebugOnly<bool> success =
          previous->CopyToTextureClient(tex, &aPersistedRect, nullptr);
      MOZ_ASSERT(success);
    }
  }

  mDrawTarget = tex->BorrowDrawTarget();
  if (mDrawTarget) {
    // This is simply to ensure the DrawTarget gets initialized, and will detect
    // a device reset, even if we're on the main thread.
    mDrawTarget->ClearRect(Rect(0, 0, 0, 0));

    if (!mDrawTarget->IsValid()) {
      mDrawTarget = nullptr;
    }
  }

  RefPtr<gfx::DrawTarget> dt(mDrawTarget);
  return dt.forget();
}

bool PersistentBufferProviderShared::ReturnDrawTarget(
    already_AddRefed<gfx::DrawTarget> aDT) {
  RefPtr<gfx::DrawTarget> dt(aDT);
  MOZ_ASSERT(mDrawTarget == dt);
  // Can't change the current front buffer while its snapshot is borrowed!
  MOZ_ASSERT(!mSnapshot);

  TextureClient* back = GetTexture(mBack);
  MOZ_ASSERT(back);

  mDrawTarget = nullptr;
  dt = nullptr;

  // If we have a permanent back buffer we have actually been drawing to that,
  // so now we must copy to the shared one.
  if (mPermanentBackBuffer && back) {
    DebugOnly<bool> success =
        mPermanentBackBuffer->CopyToTextureClient(back, nullptr, nullptr);
    MOZ_ASSERT(success);

    // Let our permanent back buffer know that we have finished drawing.
    mPermanentBackBuffer->EndDraw();
  }

  if (back) {
    back->Unlock();
    mFront = mBack;
  }

  return !!back;
}

TextureClient* PersistentBufferProviderShared::GetTextureClient() {
  // Can't access the front buffer while drawing.
  MOZ_ASSERT(!mDrawTarget);
  TextureClient* texture = GetTexture(mFront);
  if (!texture) {
    gfxCriticalNote
        << "PersistentBufferProviderShared: front buffer unavailable";
    return nullptr;
  }

  // Sometimes, for example on tab switch, we re-forward our texture. So if we
  // are and it is still read locked, then borrow and return our DrawTarget to
  // force it to be copied to a texture that we will safely read lock.
  if (texture->IsReadLocked()) {
    RefPtr<DrawTarget> dt =
        BorrowDrawTarget(IntRect(0, 0, mSize.width, mSize.height));

    // If we failed to borrow a DrawTarget then all our textures must be read
    // locked or we failed to create one, so we'll just return the current front
    // buffer even though that might lead to contention.
    if (dt) {
      ReturnDrawTarget(dt.forget());
      texture = GetTexture(mFront);
      if (!texture) {
        gfxCriticalNote
            << "PersistentBufferProviderShared: front buffer unavailable";
        return nullptr;
      }
    }
  } else {
    // If it isn't read locked then make sure it is set as updated, so that we
    // will always read lock even if we've forwarded the texture before.
    texture->SetUpdated();
  }

  return texture;
}

already_AddRefed<gfx::SourceSurface>
PersistentBufferProviderShared::BorrowSnapshot(gfx::DrawTarget* aTarget) {
  // If we have a permanent back buffer we can always use that to snapshot.
  if (mPermanentBackBuffer) {
    mSnapshot = mPermanentBackBuffer->BorrowSnapshot();
    return do_AddRef(mSnapshot);
  }

  if (mDrawTarget) {
    auto back = GetTexture(mBack);
    if (NS_WARN_IF(!back) || NS_WARN_IF(!back->IsLocked())) {
      return nullptr;
    }
    mSnapshot = back->BorrowSnapshot();
    return do_AddRef(mSnapshot);
  }

  auto front = GetTexture(mFront);
  if (NS_WARN_IF(!front) || NS_WARN_IF(front->IsLocked())) {
    return nullptr;
  }

  if (front->IsReadLocked() && front->HasSynchronization()) {
    // We are about to read lock a texture that is in use by the compositor and
    // has synchronization. To prevent possible future contention we switch to
    // using a permanent back buffer.
    mPermanentBackBuffer =
        CreateTexture(mKnowsCompositor, mFormat, mSize, mWillReadFrequently);
    if (!mPermanentBackBuffer ||
        !mPermanentBackBuffer->Lock(OpenMode::OPEN_READ_WRITE)) {
      return nullptr;
    }

    if (!front->Lock(OpenMode::OPEN_READ)) {
      return nullptr;
    }

    DebugOnly<bool> success =
        front->CopyToTextureClient(mPermanentBackBuffer, nullptr, nullptr);
    MOZ_ASSERT(success);
    front->Unlock();
    mSnapshot = mPermanentBackBuffer->BorrowSnapshot();
    return do_AddRef(mSnapshot);
  }

  if (!front->Lock(OpenMode::OPEN_READ)) {
    return nullptr;
  }

  mSnapshot = front->BorrowSnapshot();

  return do_AddRef(mSnapshot);
}

void PersistentBufferProviderShared::ReturnSnapshot(
    already_AddRefed<gfx::SourceSurface> aSnapshot) {
  RefPtr<SourceSurface> snapshot = aSnapshot;
  MOZ_ASSERT(!snapshot || snapshot == mSnapshot);

  mSnapshot = nullptr;
  snapshot = nullptr;

  if (mDrawTarget || mPermanentBackBuffer) {
    return;
  }

  auto front = GetTexture(mFront);
  if (front) {
    front->Unlock();
  }
}

void PersistentBufferProviderShared::NotifyInactive() {
  ClearCachedResources();
}

void PersistentBufferProviderShared::ClearCachedResources() {
  RefPtr<TextureClient> front = GetTexture(mFront);
  RefPtr<TextureClient> back = GetTexture(mBack);

  // Clear all textures (except the front and back ones that we just kept).
  mTextures.clear();
  mPermanentBackBuffer = nullptr;

  if (back) {
    if (mTextures.append(back)) {
      mBack = Some<uint32_t>(0);
    }
    if (front == back) {
      mFront = mBack;
    }
  }

  if (front && front != back) {
    if (mTextures.append(front)) {
      mFront = Some<uint32_t>(mTextures.length() - 1);
    }
  }
}

void PersistentBufferProviderShared::Destroy() {
  mSnapshot = nullptr;
  mDrawTarget = nullptr;

  if (mPermanentBackBuffer) {
    mPermanentBackBuffer->Unlock();
    mPermanentBackBuffer = nullptr;
  }

  for (auto& texture : mTextures) {
    if (texture && texture->IsLocked()) {
      MOZ_ASSERT(false);
      texture->Unlock();
    }
  }

  mTextures.clear();
}

}  // namespace layers
}  // namespace mozilla
