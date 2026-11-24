/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteLayerTreeOwner.h"

#include "base/basictypes.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/EffectsInfo.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/LayerTreeLog.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/layers/WebRenderScrollData.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsContentUtils.h"
#include "nsFrameLoader.h"
#include "nsIWidget.h"
#include "nsStyleStructInlines.h"
#include "nsSubDocumentFrame.h"

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::layers;

namespace mozilla {
namespace layout {

static already_AddRefed<WindowRenderer> GetWindowRenderer(
    BrowserParent* aBrowserParent) {
  RefPtr<WindowRenderer> renderer;
  if (Element* element = aBrowserParent->GetOwnerElement()) {
    renderer = nsContentUtils::WindowRendererForContent(element);
    if (renderer) {
      return renderer.forget();
    }
    renderer = nsContentUtils::WindowRendererForDocument(element->OwnerDoc());
    if (renderer) {
      return renderer.forget();
    }
  }
  return nullptr;
}

RemoteLayerTreeOwner::RemoteLayerTreeOwner()
    : mLayersId{0},
      mBrowserParent(nullptr),
      mInitialized(false),
      mLayersConnected(false) {}

RemoteLayerTreeOwner::~RemoteLayerTreeOwner() = default;

// The LayersId of the pipeline that embeds aBrowserParent: the parent
// document's LayersId if aBrowserParent is a nested remote frame, or the
// LayersId of the chrome window hosting it otherwise. The latter only exists
// once that window's widget has a CompositorSession, so callers must have
// obtained its WindowRenderer first.
static LayersId GetEmbedderLayersId(BrowserParent* aBrowserParent) {
  if (WindowGlobalParent* embedderWindow =
          aBrowserParent->GetBrowsingContext()->GetParentWindowContext()) {
    if (BrowserParent* embedder = embedderWindow->GetBrowserParent()) {
      return embedder->GetLayersId();
    }
  }
  if (RefPtr<nsIWidget> widget = aBrowserParent->GetWidget()) {
    return widget->GetLayersId();
  }
  return LayersId{};
}

bool RemoteLayerTreeOwner::Initialize(BrowserParent* aBrowserParent) {
  if (mInitialized || !aBrowserParent) {
    return false;
  }

  mBrowserParent = aBrowserParent;
  // Note that this may be what creates the embedding widget's
  // CompositorSession, which GetEmbedderLayersId() below relies on.
  RefPtr<WindowRenderer> renderer = GetWindowRenderer(mBrowserParent);
  PCompositorBridgeChild* compositor =
      renderer ? renderer->GetCompositorBridgeChild() : nullptr;
  mTabChildID = mBrowserParent->Manager()->ChildID();

  // Our remote frame will push layers updates to the compositor,
  // and we'll keep an indirect reference to that tree.
  GPUProcessManager* gpm = GPUProcessManager::Get();
  mLayersConnected = gpm->AllocateAndConnectLayerTreeId(
      compositor, mTabChildID, GetEmbedderLayersId(mBrowserParent), &mLayersId,
      &mCompositorOptions);
  LAYERTREE_LOG("Layer tree {:x} for content {} initialized, connected {}",
                mLayersId, mBrowserParent->Manager()->ChildID(),
                mLayersConnected);

  mInitialized = true;
  return true;
}

void RemoteLayerTreeOwner::Destroy() {
  if (mLayersId.IsValid()) {
    LAYERTREE_LOG("Layer tree {:x} for content {} destroyed", mLayersId,
                  mBrowserParent ? mBrowserParent->Manager()->ChildID()
                                 : dom::ContentParentId());
    GPUProcessManager::Get()->UnmapLayerTreeId(mLayersId, mTabChildID);
  }

  mBrowserParent = nullptr;
  mWindowRenderer = nullptr;
}

void RemoteLayerTreeOwner::EnsureLayersConnected(
    Maybe<CompositorOptions>& aCompositorOptions) {
  RefPtr<WindowRenderer> renderer = GetWindowRenderer(mBrowserParent);
  if (!renderer || !renderer->GetCompositorBridgeChild()) {
    aCompositorOptions = Nothing();
    return;
  }

  mLayersConnected =
      renderer->GetCompositorBridgeChild()->SendNotifyChildRecreated(
          mLayersId, GetEmbedderLayersId(mBrowserParent), &mCompositorOptions);
  LAYERTREE_LOG("Layer tree {:x} reconnected {}", mLayersId, mLayersConnected);
  aCompositorOptions = Some(mCompositorOptions);
}

bool RemoteLayerTreeOwner::AttachWindowRenderer() {
  RefPtr<WindowRenderer> renderer;
  if (mBrowserParent) {
    renderer = GetWindowRenderer(mBrowserParent);
  }

  // Perhaps the document containing this frame currently has no presentation?
  if (renderer && renderer->GetCompositorBridgeChild() &&
      renderer != mWindowRenderer) {
    LAYERTREE_LOG("Layer tree {:x} moving to another window", mLayersId);
    mLayersConnected = renderer->GetCompositorBridgeChild()->SendAdoptChild(
        mLayersId, GetEmbedderLayersId(mBrowserParent));
  }

  mWindowRenderer = std::move(renderer);
  return !!mWindowRenderer;
}

void RemoteLayerTreeOwner::OwnerContentChanged() {
  (void)AttachWindowRenderer();
}

void RemoteLayerTreeOwner::GetTextureFactoryIdentifier(
    TextureFactoryIdentifier* aTextureFactoryIdentifier) const {
  RefPtr<WindowRenderer> renderer =
      mBrowserParent ? GetWindowRenderer(mBrowserParent) : nullptr;
  // Perhaps the document containing this frame currently has no presentation?
  if (renderer && renderer->AsWebRender()) {
    *aTextureFactoryIdentifier =
        renderer->AsWebRender()->GetTextureFactoryIdentifier();
  } else {
    *aTextureFactoryIdentifier = TextureFactoryIdentifier();
  }
}

}  // namespace layout
}  // namespace mozilla
