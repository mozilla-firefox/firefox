/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_TextureRecorded_h
#define mozilla_layers_TextureRecorded_h

#include "TextureClient.h"
#include "mozilla/layers/CanvasChild.h"
#include "mozilla/layers/LayersTypes.h"

namespace mozilla {
namespace layers {

class RecordedTextureData final : public TextureData {
 public:
  RecordedTextureData(already_AddRefed<CanvasChild> aCanvasChild,
                      gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
                      TextureType aTextureType,
                      TextureType aWebglTextureType = TextureType::Unknown);

  void FillInfo(TextureData::Info& aInfo) const final;

  // Must be called before the first Lock, which creates the remote DrawTarget.
  void SetPreferAcceleration(bool aPreferAcceleration) {
    MOZ_ASSERT(!mInited);
    mPreferAcceleration = aPreferAcceleration;
  }

  void InvalidateContents() final;

  bool Lock(OpenMode aMode) final;

  void Unlock() final;

  already_AddRefed<gfx::DrawTarget> BorrowDrawTarget() final;

  void EndDraw() final;

  void ReturnDrawTarget(already_AddRefed<gfx::DrawTarget> aDT) final;

  already_AddRefed<gfx::SourceSurface> BorrowSnapshot() final;

  void ReturnSnapshot(already_AddRefed<gfx::SourceSurface> aSnapshot) final;

  void Deallocate(LayersIPCChannel* aAllocator) final;

  bool Serialize(SurfaceDescriptor& aDescriptor) final;

  void OnForwardedToHost() final;

  TextureFlags GetTextureFlags() const final;

  bool RequiresRefresh() const final;

  already_AddRefed<FwdTransactionTracker> UseCompositableForwarder(
      CompositableForwarder* aForwarder) final;

  RecordedTextureData* AsRecordedTextureData() final { return this; }

  const RemoteTextureOwnerId mRemoteTextureOwnerId;

 protected:
  friend class gfx::DrawTargetRecording;

  void DrawTargetWillChange();

 private:
  DISALLOW_COPY_AND_ASSIGN(RecordedTextureData);

  ~RecordedTextureData() override;

  void DetachSnapshotWrapper(bool aInvalidate = false, bool aRelease = true);

  RefPtr<CanvasChild> mCanvasChild;
  gfx::IntSize mSize;
  gfx::SurfaceFormat mFormat;
  RefPtr<gfx::DrawTargetRecording> mDT;
  RefPtr<gfx::SourceSurface> mSnapshot;
  RefPtr<gfx::SourceSurface> mSnapshotWrapper;
  OpenMode mLockedMode = OpenMode::OPEN_NONE;
  RemoteTextureId mLastRemoteTextureId;
  RefPtr<layers::FwdTransactionTracker> mFwdTransactionTracker;
  bool mUsedRemoteTexture = false;
  bool mInvalidContents = true;
  bool mInited = false;
  bool mPreferAcceleration = false;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_TextureRecorded_h
