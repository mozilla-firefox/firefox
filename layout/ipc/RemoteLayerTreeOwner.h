/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layout_RemoteLayerTreeOwner_h
#define mozilla_layout_RemoteLayerTreeOwner_h

#include "mozilla/ProcessType.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/layers/CompositorOptions.h"
#include "mozilla/layers/LayersTypes.h"
#include "nsDisplayList.h"

class nsFrameLoader;
class nsSubDocumentFrame;

namespace mozilla {

namespace dom {
class BrowserParent;
}  // namespace dom

namespace layers {
struct TextureFactoryIdentifier;
}  // namespace layers

namespace layout {

/**
 * RemoteLayerTreeOwner connects and manages layer trees for remote frames. It
 * is directly owned by a BrowserParent and always lives in the parent process.
 */
class RemoteLayerTreeOwner final {
  typedef mozilla::layers::CompositorOptions CompositorOptions;
  typedef mozilla::layers::LayerManager LayerManager;
  typedef mozilla::layers::LayersId LayersId;
  typedef mozilla::layers::TextureFactoryIdentifier TextureFactoryIdentifier;

 public:
  RemoteLayerTreeOwner();
  virtual ~RemoteLayerTreeOwner();

  bool Initialize(dom::BrowserParent* aBrowserParent);
  void Destroy();

  void EnsureLayersConnected(Maybe<CompositorOptions>& aCompositorOptions);
  bool AttachWindowRenderer();
  void OwnerContentChanged();

  LayersId GetLayersId() const { return mLayersId; }
  CompositorOptions GetCompositorOptions() const { return mCompositorOptions; }

  void GetTextureFactoryIdentifier(
      TextureFactoryIdentifier* aTextureFactoryIdentifier) const;

  bool IsInitialized() const { return mInitialized; }
  bool IsLayersConnected() const { return mLayersConnected; }

 private:
  // The GeckoChildID of the remote frame. This is used by the compositor to
  // do security checks on incoming layer transactions.
  GeckoChildID mTabChildID = kInvalidGeckoChildID;
  // The layers id of the remote frame.
  LayersId mLayersId;
  // The compositor options for this layers id. This is only meaningful if
  // the compositor actually knows about this layers id (i.e. when
  // mLayersConnected is true).
  CompositorOptions mCompositorOptions;

  dom::BrowserParent* mBrowserParent;
  RefPtr<WindowRenderer> mWindowRenderer;

  bool mInitialized;
  // A flag that indicates whether or not the compositor knows about the
  // layers id. In some cases this RemoteLayerTreeOwner is not connected to the
  // compositor and so this flag is false.
  bool mLayersConnected;
};

}  // namespace layout
}  // namespace mozilla

#endif  // mozilla_layout_RemoteLayerTreeOwner_h
