/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_NativeLayerRootRemoteWaylandChild_h
#define mozilla_layers_NativeLayerRootRemoteWaylandChild_h

#include "mozilla/layers/NativeLayerCommandQueue.h"
#include "mozilla/layers/NativeLayerRemoteChild.h"
#include "mozilla/layers/NativeLayerRemoteWayland.h"

namespace mozilla {

namespace widget {
class DRMFormat;
}  // namespace widget

namespace layers {

// NativeLayerRootRemoteWaylandChild is a Wayland-specific implementation of
// NativeLayerRoot that runs on the GPU process, and sends updates
// to the parent process.
class NativeLayerRootRemoteWaylandChild final : public NativeLayerRoot {
 public:
  NativeLayerRootRemoteWaylandChild();

  already_AddRefed<NativeLayer> CreateLayer(
      const gfx::IntSize& aSize, bool aIsOpaque,
      SurfacePoolHandle* aSurfacePoolHandle) override;
  already_AddRefed<NativeLayer> CreateLayerForExternalTexture(
      bool aIsOpaque) override;
  already_AddRefed<NativeLayer> CreateLayerForColor(
      gfx::DeviceColor aColor) override;

  void AppendLayer(NativeLayer* aLayer) override;
  void RemoveLayer(NativeLayer* aLayer) override;
  void SetLayers(const nsTArray<RefPtr<NativeLayer>>& aLayers) override;
  UniquePtr<NativeLayerRootSnapshotter> CreateSnapshotter() override;

  // Called before any layer content changes.
  void PrepareForCommit() override;

  // Publish the layer changes to the screen. Returns whether the commit was
  // successful.
  bool CommitToScreen() override;

  // Send a sync message to the parent to make sure it has seen our layer
  // commands message.
  void WaitUntilCommitToScreenHasBeenProcessed() override;

  RefPtr<NativeLayerRemoteChild> GetRemoteChild() { return mRemoteChild; }

 protected:
  virtual ~NativeLayerRootRemoteWaylandChild();

  RefPtr<NativeLayerRemoteChild> mRemoteChild;
  RefPtr<NativeLayerCommandQueue> mCommandQueue;
  nsTArray<RefPtr<NativeLayerRemoteWayland>> mNativeLayers;
  // The format of the DMABuf surfaces we render into, or null when the
  // surfaces are created by exporting GL textures.
  RefPtr<widget::DRMFormat> mDRMFormat;

  bool mNativeLayersChanged = false;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_NativeLayerRootRemoteWaylandChild_h
