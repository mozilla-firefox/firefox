/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerRootRemoteMacParent_h
#define mozilla_layers_NativeLayerRootRemoteMacParent_h

#include "mozilla/layers/NativeLayerCA.h"
#include "mozilla/layers/NativeLayerRemoteParent.h"

namespace mozilla {
namespace layers {

// NativeLayerRootRemoteMacParent is a macOS-specific receiver class for
// NativeLayerRootRemoteMacChild. It interprets the incoming messages and
// sends them on to the "real" NativeLayerRoot object in the parent
// process.
class NativeLayerRootRemoteMacParent final : public NativeLayerRemoteParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NativeLayerRootRemoteMacParent,
                                        override)

  explicit NativeLayerRootRemoteMacParent(
      RefPtr<NativeLayerRootCA> aRealNativeLayerRoot);

  mozilla::ipc::IPCResult RecvCommitNativeLayerCommands(
      nsTArray<NativeLayerCommand>&& aCommands) override;

  mozilla::ipc::IPCResult RecvRequestReadback(IntSize aSize,
                                              Shmem* const aPixels) override;

  mozilla::ipc::IPCResult RecvFlush() override;

 protected:
  ~NativeLayerRootRemoteMacParent() = default;

  void HandleCreateLayer(uint64_t aID, IntSize aSize, bool aOpaque);
  void HandleCreateLayerForExternalTexture(uint64_t aID, bool aOpaque);
  void HandleCreateLayerForColor(uint64_t aID, DeviceColor aColor);
  void HandleLayerDestroyed(uint64_t aID);
  void HandleSetLayers(const nsTArray<uint64_t>& aIDs);
  void HandleLayerInfo(uint64_t aID, IntPoint aPosition, IntRect aDisplayRect,
                       Maybe<IntRect> aClipRect,
                       Maybe<RoundedRect> aRoundedClipRect,
                       Matrix4x4 aTransform, SamplingFilter aSamplingFilter,
                       bool aSurfaceIsFlipped);
  void HandleChangedSurface(uint64_t aID, IOSurfacePort aSurfacePort,
                            bool aIsDRM, bool aIsHDR, IntSize aSize);

  RefPtr<NativeLayerRootCA> mRealNativeLayerRoot;
  UniquePtr<NativeLayerRootSnapshotter> mSnapshotter;
  nsTHashMap<uint64_t, RefPtr<NativeLayer>> mKnownLayers;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_NativeLayerRootRemoteMacParent_h
