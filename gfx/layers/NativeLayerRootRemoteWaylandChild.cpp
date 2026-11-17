/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/NativeLayerRootRemoteWaylandChild.h"

#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/NativeLayerRemoteWayland.h"
#include "mozilla/layers/SurfacePoolWayland.h"
#include "mozilla/widget/DMABufDevice.h"

namespace mozilla {
namespace layers {

already_AddRefed<NativeLayer> NativeLayerRootRemoteWaylandChild::CreateLayer(
    const gfx::IntSize& aSize, bool aIsOpaque,
    SurfacePoolHandle* aSurfacePoolHandle) {
  RefPtr layer = MakeRefPtr<NativeLayerRemoteWayland>(
      aSize, aIsOpaque, aSurfacePoolHandle->AsSurfacePoolHandleWayland(),
      mDRMFormat);
  mCommandQueue->AppendCommand(mozilla::layers::CommandCreateLayer(
      reinterpret_cast<uint64_t>(layer.get()), aSize, aIsOpaque));
  // Share our command queue.
  layer->mCommandQueue = mCommandQueue;
  return layer.forget();
}

already_AddRefed<NativeLayer>
NativeLayerRootRemoteWaylandChild::CreateLayerForExternalTexture(
    bool aIsOpaque) {
  RefPtr layer = MakeRefPtr<NativeLayerRemoteWayland>(aIsOpaque);
  mCommandQueue->AppendCommand(
      mozilla::layers::CommandCreateLayerForExternalTexture(
          reinterpret_cast<uint64_t>(layer.get()), aIsOpaque));
  // Share our command queue.
  layer->mCommandQueue = mCommandQueue;
  return layer.forget();
}

already_AddRefed<NativeLayer>
NativeLayerRootRemoteWaylandChild::CreateLayerForColor(
    gfx::DeviceColor aColor) {
  RefPtr layer = MakeRefPtr<NativeLayerRemoteWayland>(aColor);
  mCommandQueue->AppendCommand(mozilla::layers::CommandCreateLayerForColor(
      reinterpret_cast<uint64_t>(layer.get()), aColor));
  // Share our command queue.
  layer->mCommandQueue = mCommandQueue;
  return layer.forget();
}

void NativeLayerRootRemoteWaylandChild::AppendLayer(NativeLayer* aLayer) {
  RefPtr<NativeLayerRemoteWayland> layerWayland =
      aLayer->AsNativeLayerRemoteWayland();
  MOZ_ASSERT(layerWayland);
  mNativeLayers.AppendElement(std::move(layerWayland));
  mNativeLayersChanged = true;
}

void NativeLayerRootRemoteWaylandChild::RemoveLayer(NativeLayer* aLayer) {
  if (mNativeLayers.RemoveElement(aLayer)) {
    mNativeLayersChanged = true;
  }
}

void NativeLayerRootRemoteWaylandChild::SetLayers(
    const nsTArray<RefPtr<NativeLayer>>& aLayers) {
  // We don't create a command for this, because we don't care
  // about the layers until CommitToScreen().
  nsTArray<RefPtr<NativeLayerRemoteWayland>> layers(aLayers.Length());
  for (const auto& layer : aLayers) {
    RefPtr layerWayland = layer->AsNativeLayerRemoteWayland();
    MOZ_ASSERT(layerWayland);
    layers.AppendElement(std::move(layerWayland));
  }

  if (mNativeLayers == layers) {
    // A no-op.
    return;
  }

  mNativeLayersChanged = true;
  mNativeLayers.Clear();
  mNativeLayers.AppendElements(layers);
}

UniquePtr<NativeLayerRootSnapshotter>
NativeLayerRootRemoteWaylandChild::CreateSnapshotter() {
  return nullptr;
}

void NativeLayerRootRemoteWaylandChild::PrepareForCommit() {
  // Intentionally ignored.
}

bool NativeLayerRootRemoteWaylandChild::CommitToScreen() {
  // Prepare and send all commands to the parent actor.

  // Our shared command queue has all of our CreateLayer and LayerDestroyed
  // commands. That's good, because the commands that we're adding to the
  // queue rely upon those CreateLayer commands appearing first.

  // Iterate our layers to get the LayerInfo and ChangedSurface commands
  // into our shared command queue.
  for (const auto& layer : mNativeLayers) {
    layer->FlushDirtyLayerInfoToCommandQueue();
  }

  if (mNativeLayersChanged) {
    // If mNativeLayersChanged is set, we will send a SetLayers
    // command of this array filled with the IDs of everything
    // in mNativeLayers.
    nsTArray<uint64_t> setLayerIDs;
    for (const auto& layer : mNativeLayers) {
      auto ID = reinterpret_cast<uint64_t>(layer.get());
      setLayerIDs.AppendElement(ID);
    }
    mCommandQueue->AppendCommand(
        mozilla::layers::CommandSetLayers(setLayerIDs));
    mNativeLayersChanged = false;
  }

  // Now flush the shared command queue to our local command array,
  // which is suitable for sending to the parent process.
  nsTArray<NativeLayerCommand> commands;
  mCommandQueue->FlushToArray(commands);

  if (!commands.IsEmpty()) {
    // Send all the queued commands, including the ones we just added.
    MOZ_ASSERT(mRemoteChild);
    mRemoteChild->SendCommitNativeLayerCommands(std::move(commands));
  }
  return true;
}

void NativeLayerRootRemoteWaylandChild::
    WaitUntilCommitToScreenHasBeenProcessed() {
  mRemoteChild->SendFlush();
}

NativeLayerRootRemoteWaylandChild::NativeLayerRootRemoteWaylandChild()
    : mRemoteChild(MakeRefPtr<NativeLayerRemoteChild>()),
      mCommandQueue(MakeRefPtr<NativeLayerCommandQueue>()) {
  // Get DRM format for surfaces created by GBM. The formats come from gfxVars,
  // because we don't have a connection to the Wayland display.
  if (!gfx::gfxVars::UseDMABufSurfaceExport()) {
    if (auto* formats = widget::GetGlobalDMABufFormats()) {
      mDRMFormat = formats->GetDRMFormat(GBM_FORMAT_ARGB8888);
    }
    if (!mDRMFormat) {
      mDRMFormat = new widget::DRMFormat(GBM_FORMAT_ARGB8888);
    }
  }
}

NativeLayerRootRemoteWaylandChild::~NativeLayerRootRemoteWaylandChild() =
    default;

}  // namespace layers
}  // namespace mozilla
