/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextureRecorded.h"

#include "RecordedCanvasEventImpl.h"
#include "mozilla/gfx/DrawTargetRecording.h"
#include "mozilla/layers/CompositableForwarder.h"

namespace mozilla {
namespace layers {

RecordedTextureData::RecordedTextureData(
    already_AddRefed<CanvasChild> aCanvasChild, gfx::IntSize aSize,
    gfx::SurfaceFormat aFormat, TextureType aTextureType,
    TextureType aWebglTextureType)
    : mRemoteTextureOwnerId(RemoteTextureOwnerId::GetNext()),
      mCanvasChild(aCanvasChild),
      mSize(aSize),
      mFormat(aFormat) {}

RecordedTextureData::~RecordedTextureData() {
  // We need the translator to drop its reference for the DrawTarget first,
  // because the TextureData might need to destroy its DrawTarget within a lock.
  mSnapshot = nullptr;
  DetachAndReleaseSnapshotWrapper();
  if (mDT) {
    mDT->DetachTextureData(this);
    mDT = nullptr;
  }
  mCanvasChild->RecordEvent(RecordedTextureDestruction(
      mRemoteTextureOwnerId, ToRemoteTextureTxnType(mFwdTransactionTracker),
      ToRemoteTextureTxnId(mFwdTransactionTracker)));
}

void RecordedTextureData::FillInfo(TextureData::Info& aInfo) const {
  aInfo.size = mSize;
  aInfo.format = mFormat;
  aInfo.supportsMoz2D = true;
  aInfo.hasSynchronization = true;
}

void RecordedTextureData::InvalidateContents() { mInvalidContents = true; }

bool RecordedTextureData::Lock(OpenMode aMode) {
  if (!mCanvasChild->EnsureBeginTransaction()) {
    return false;
  }

  if (!mDT && mDrawTargetCreationAttempted) {
    // A failed creation attempt is final for this texture.
    return false;
  }

  // A write requires a new remote texture ID the next time we serialize.
  if (aMode & OpenMode::OPEN_WRITE) {
    mUsedRemoteTexture = false;
  }

  const bool wasInvalidContents = mInvalidContents;
  mInvalidContents = false;

  if (!mDT) {
    mDrawTargetCreationAttempted = true;
    // The canvas draw target creation event locks the remote texture on replay.
    mDT = mCanvasChild->CreateDrawTarget(mRemoteTextureOwnerId, mSize, mFormat,
                                         mPreferAcceleration);
    if (!mDT) {
      return false;
    }

    mDT->AttachTextureData(this);
  } else {
    mCanvasChild->RecordEvent(
        RecordedTextureLock(mRemoteTextureOwnerId, aMode, wasInvalidContents));
  }

  mLockedMode = aMode;
  return true;
}

void RecordedTextureData::DetachAndReleaseSnapshotWrapper(bool aInvalidate) {
  if (mSnapshotWrapper) {
    // If the snapshot only has one ref, then we don't need to worry about
    // copying before invalidation since it is about to be deleted. Otherwise,
    // we need to ensure any internal data is appropriately copied before
    // shmems are potentially overwritten if there are still existing users.
    mCanvasChild->DetachSurface(mSnapshotWrapper,
                                aInvalidate && !mSnapshotWrapper->hasOneRef());
    mSnapshotWrapper = nullptr;
  }
}

void RecordedTextureData::Unlock() {
  if ((mLockedMode == OpenMode::OPEN_READ_WRITE) &&
      mCanvasChild->ShouldCacheDataSurface()) {
    DetachAndReleaseSnapshotWrapper();
    mSnapshot = mDT->Snapshot();
    mDT->DetachAllSnapshots();
    mCanvasChild->RecordEvent(RecordedCacheDataSurface(mSnapshot.get()));
  }

  mCanvasChild->RecordEvent(RecordedTextureUnlock(mRemoteTextureOwnerId));

  mLockedMode = OpenMode::OPEN_NONE;
}

already_AddRefed<gfx::DrawTarget> RecordedTextureData::BorrowDrawTarget() {
  if (mLockedMode & OpenMode::OPEN_WRITE) {
    // The snapshot will be invalidated.
    mSnapshot = nullptr;
    DetachAndReleaseSnapshotWrapper(true);
  }
  return do_AddRef(mDT);
}

void RecordedTextureData::EndDraw() {
  MOZ_ASSERT(mDT->hasOneRef());
  MOZ_ASSERT(mLockedMode == OpenMode::OPEN_READ_WRITE);

  if (mCanvasChild->ShouldCacheDataSurface()) {
    DetachAndReleaseSnapshotWrapper();
    mSnapshot = mDT->Snapshot();
    mCanvasChild->RecordEvent(RecordedCacheDataSurface(mSnapshot.get()));
  }
}

void RecordedTextureData::DrawTargetWillChange() {
  // The DrawTargetRecording will be modified, so ensure that possibly the last
  // reference to a snapshot is discarded so that it does not inadvertently
  // force a copy.
  mSnapshot = nullptr;
  DetachAndReleaseSnapshotWrapper(true);
}

already_AddRefed<gfx::SourceSurface> RecordedTextureData::BorrowSnapshot() {
  if (mSnapshotWrapper) {
    // The DT is unmodified since the last time snapshot was borrowed, so it
    // is safe to reattach the snapshot for shmem readbacks.
    mCanvasChild->AttachSurface(mSnapshotWrapper);
    return do_AddRef(mSnapshotWrapper);
  }

  // There are some failure scenarios where we have no DrawTarget and
  // BorrowSnapshot is called in an attempt to copy to a new texture.
  if (!mDT) {
    return nullptr;
  }

  RefPtr<gfx::SourceSurface> wrapper = mCanvasChild->WrapSurface(
      mSnapshot ? mSnapshot : mDT->Snapshot(), mRemoteTextureOwnerId);
  mSnapshotWrapper = wrapper;
  return wrapper.forget();
}

void RecordedTextureData::ReturnDrawTarget(
    already_AddRefed<gfx::DrawTarget> aDT) {
  RefPtr<gfx::DrawTarget> dt(aDT);
}

void RecordedTextureData::ReturnSnapshot(
    already_AddRefed<gfx::SourceSurface> aSnapshot) {
  RefPtr<gfx::SourceSurface> snapshot = aSnapshot;
  // Keep the wrapper so BorrowSnapshot can reattach it if the draw target
  // has not changed.
  mCanvasChild->DetachSurface(mSnapshotWrapper);
}

void RecordedTextureData::Deallocate(LayersIPCChannel* aAllocator) {}

bool RecordedTextureData::Serialize(SurfaceDescriptor& aDescriptor) {
  // If something is querying the id, assume it is going to be composited.
  if (!mUsedRemoteTexture) {
    mLastRemoteTextureId = RemoteTextureId::GetNext();
    mCanvasChild->RecordEvent(
        RecordedPresentTexture(mRemoteTextureOwnerId, mLastRemoteTextureId));
    mUsedRemoteTexture = true;
  }

  aDescriptor = SurfaceDescriptorRemoteTexture(mLastRemoteTextureId,
                                               mRemoteTextureOwnerId);
  return true;
}

already_AddRefed<FwdTransactionTracker>
RecordedTextureData::UseCompositableForwarder(
    CompositableForwarder* aForwarder) {
  return FwdTransactionTracker::GetOrCreate(mFwdTransactionTracker);
}

void RecordedTextureData::OnForwardedToHost() {
  // Compositing with RecordedTextureData requires RemoteTextureMap.
  MOZ_CRASH("OnForwardedToHost not supported!");
}

TextureFlags RecordedTextureData::GetTextureFlags() const {
  // With WebRender, resource open happens asynchronously on RenderThread.
  // Use WAIT_HOST_USAGE_END to keep TextureClient alive during host side usage.
  return TextureFlags::WAIT_HOST_USAGE_END;
}

bool RecordedTextureData::RequiresRefresh() const {
  return mCanvasChild->RequiresRefresh(mRemoteTextureOwnerId);
}

}  // namespace layers
}  // namespace mozilla
