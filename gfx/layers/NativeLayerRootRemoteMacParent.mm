/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/CheckedInt.h"
#include "mozilla/layers/NativeLayerRootRemoteMacParent.h"
#include "xpcpublic.h"

namespace mozilla {
namespace layers {

NativeLayerRootRemoteMacParent::NativeLayerRootRemoteMacParent(
    RefPtr<NativeLayerRootCA> aRealNativeLayerRoot)
    : mRealNativeLayerRoot(aRealNativeLayerRoot) {
  MOZ_ASSERT(mRealNativeLayerRoot);
}

mozilla::ipc::IPCResult
NativeLayerRootRemoteMacParent::RecvCommitNativeLayerCommands(
    nsTArray<NativeLayerCommand>&& aCommands) {
  for (auto& command : aCommands) {
    switch (command.type()) {
      case NativeLayerCommand::TCommandCreateLayer: {
        auto& createLayer = command.get_CommandCreateLayer();
        HandleCreateLayer(createLayer.ID(), createLayer.Size(),
                          createLayer.Opaque());
        break;
      }

      case NativeLayerCommand::TCommandCreateLayerForExternalTexture: {
        auto& createLayerForExternalTexture =
            command.get_CommandCreateLayerForExternalTexture();
        HandleCreateLayerForExternalTexture(
            createLayerForExternalTexture.ID(),
            createLayerForExternalTexture.Opaque());
        break;
      }

      case NativeLayerCommand::TCommandCreateLayerForColor: {
        auto& createLayerForColor = command.get_CommandCreateLayerForColor();
        HandleCreateLayerForColor(createLayerForColor.ID(),
                                  createLayerForColor.Color());
        break;
      }

      case NativeLayerCommand::TCommandLayerDestroyed: {
        auto& layerDestroyed = command.get_CommandLayerDestroyed();
        HandleLayerDestroyed(layerDestroyed.ID());
        break;
      }

      case NativeLayerCommand::TCommandSetLayers: {
        auto& setLayers = command.get_CommandSetLayers();
        HandleSetLayers(setLayers.IDs());
        break;
      }

      case NativeLayerCommand::TCommandLayerInfo: {
        auto& layerInfo = command.get_CommandLayerInfo();
        HandleLayerInfo(layerInfo.ID(), layerInfo.Position(),
                        layerInfo.DisplayRect(), layerInfo.ClipRect(),
                        layerInfo.RoundedClipRect(), layerInfo.Transform(),
                        layerInfo.samplingFilter(),
                        layerInfo.SurfaceIsFlipped());
        break;
      }

      case NativeLayerCommand::TCommandChangedSurface: {
        auto& changedSurface = command.get_CommandChangedSurface();
        HandleChangedSurface(changedSurface.ID(),
                             std::move(changedSurface.Surface()),
                             changedSurface.IsDRM(), changedSurface.IsHDR(),
                             changedSurface.Size());
        break;
      }

      default: {
        gfxWarning() << "Unknown NativeLayerCommand.";
        break;
      }
    }
  }

  mRealNativeLayerRoot->CommitToScreen();

  return IPC_OK();
}

mozilla::ipc::IPCResult NativeLayerRootRemoteMacParent::RecvRequestReadback(
    IntSize aSize, Shmem* const aPixels) {
  if (!xpc::IsInAutomation()) {
    return IPC_FAIL(this, "Should only be called from automation.");
  }

  // Actually do a snapshot on mRealNativeLayerRoot.
  // TODO: we'll probably have to handle higher bit depth formats at some point
  // with the upcoming HDR work, but for now assume B8G8R8A8.
  auto readbackFormat = gfx::SurfaceFormat::B8G8R8A8;
  auto readbackSize = (CheckedUint32(aSize.width) * aSize.height *
                       gfx::BytesPerPixel(readbackFormat));
  if (!readbackSize.isValid()) {
    return IPC_FAIL(this, "Invalid readback size.");
  }

  Shmem buffer;
  if (!AllocShmem(readbackSize.value(), &buffer)) {
    return IPC_FAIL(this, "Can't allocate shmem.");
  }

  if (!mSnapshotter) {
    mSnapshotter = mRealNativeLayerRoot->CreateSnapshotter();
    if (!mSnapshotter) {
      return IPC_FAIL(this, "Can't create parent-side snapshotter.");
    }
  }

  if (!mSnapshotter->ReadbackPixels(aSize, readbackFormat,
                                    buffer.Range<uint8_t>())) {
    return IPC_FAIL(this, "Failed readback.");
  }

  *aPixels = buffer;
  return IPC_OK();
}

mozilla::ipc::IPCResult NativeLayerRootRemoteMacParent::RecvFlush() {
  // No-op message; when this returns, the other side knows that any
  // preceding messages have finished processing.
  return IPC_OK();
}

void NativeLayerRootRemoteMacParent::HandleCreateLayer(uint64_t aID,
                                                       IntSize aSize,
                                                       bool aOpaque) {
  RefPtr<NativeLayerCA> layer =
      mRealNativeLayerRoot->CreateLayerForSurfacePresentation(aSize, aOpaque);
  mKnownLayers.InsertOrUpdate(aID, layer);
}

void NativeLayerRootRemoteMacParent::HandleCreateLayerForExternalTexture(
    uint64_t aID, bool aOpaque) {
  RefPtr<NativeLayer> layer =
      mRealNativeLayerRoot->CreateLayerForExternalTexture(aOpaque);
  mKnownLayers.InsertOrUpdate(aID, layer);
}

void NativeLayerRootRemoteMacParent::HandleCreateLayerForColor(
    uint64_t aID, DeviceColor aColor) {
  RefPtr<NativeLayer> layer = mRealNativeLayerRoot->CreateLayerForColor(aColor);
  mKnownLayers.InsertOrUpdate(aID, layer);
}

void NativeLayerRootRemoteMacParent::HandleLayerDestroyed(uint64_t aID) {
  mKnownLayers.Remove(aID);
}

void NativeLayerRootRemoteMacParent::HandleSetLayers(
    const nsTArray<uint64_t>& aIDs) {
  nsTArray<RefPtr<NativeLayer>> layers;
  for (auto ID : aIDs) {
    auto entry = mKnownLayers.MaybeGet(ID);
    if (!entry) {
      gfxWarning() << "Got a SetLayers for an unknown layer.";
      continue;
    }

    RefPtr<NativeLayer> layer = *entry;
    layers.AppendElement(layer);
  }
  mRealNativeLayerRoot->SetLayers(layers);
}

void NativeLayerRootRemoteMacParent::HandleLayerInfo(
    uint64_t aID, IntPoint aPosition, IntRect aDisplayRect,
    Maybe<IntRect> aClipRect, Maybe<RoundedRect> aRoundedClipRect,
    Matrix4x4 aTransform, SamplingFilter aSamplingFilter,
    bool aSurfaceIsFlipped) {
  auto entry = mKnownLayers.MaybeGet(aID);
  if (!entry) {
    gfxWarning() << "Got a LayerInfo for an unknown layer.";
    return;
  }

  RefPtr<NativeLayerCA> layer = (*entry)->AsNativeLayerCA();
  MOZ_ASSERT(layer, "All of our known layers should be NativeLayerCA.");

  // Set the other properties of layer.
  layer->SetPosition(aPosition);
  layer->SetDisplayRect(aDisplayRect);
  layer->SetClipRect(aClipRect);
  layer->SetRoundedClipRect(aRoundedClipRect);
  layer->SetTransform(aTransform);
  layer->SetSamplingFilter(aSamplingFilter);
  layer->SetSurfaceIsFlipped(aSurfaceIsFlipped);
}

void NativeLayerRootRemoteMacParent::HandleChangedSurface(
    uint64_t aID, IOSurfacePort aSurfacePort, bool aIsDRM, bool aIsHDR,
    IntSize aSize) {
  auto entry = mKnownLayers.MaybeGet(aID);
  if (!entry) {
    gfxWarning() << "Got a ChangedSurface for an unknown layer.";
    return;
  }

  RefPtr<NativeLayerCA> layer = (*entry)->AsNativeLayerCA();
  MOZ_ASSERT(layer, "All of our known layers should be NativeLayerCA.");

  if (auto surface = aSurfacePort.GetSurface()) {
    layer->SetSurfaceToPresent(surface, aSize, aIsDRM, aIsHDR);
  }
}

}  // namespace layers
}  // namespace mozilla
