/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DocumentInlines.h"
#include "MediaDocument.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/LoadURIOptionsBinding.h"  // For ForceMediaDocument
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsILoadInfo.h"
#include "nsNodeInfoManager.h"

namespace mozilla::dom {

class VideoDocument final : public MediaDocument {
 public:
  enum MediaDocumentKind MediaDocumentKind() const override {
    return MediaDocumentKind::Video;
  }

  virtual nsresult StartDocumentLoad(const char* aCommand, nsIChannel* aChannel,
                                     nsILoadGroup* aLoadGroup,
                                     nsISupports* aContainer,
                                     nsIStreamListener** aDocListener,
                                     bool aReset = true) override;
  virtual void SetScriptGlobalObject(
      nsIScriptGlobalObject* aScriptGlobalObject) override;

  virtual void Destroy() override {
    if (mStreamListener) {
      mStreamListener->DropDocumentRef();
    }
    MediaDocument::Destroy();
  }

  nsresult StartLayout() override;

 protected:
  nsresult CreateVideoElement();
  // Sets document <title> to reflect the file name and description.
  void UpdateTitle(nsIChannel* aChannel);

  RefPtr<MediaDocumentStreamListener> mStreamListener;
};

nsresult VideoDocument::StartDocumentLoad(
    const char* aCommand, nsIChannel* aChannel, nsILoadGroup* aLoadGroup,
    nsISupports* aContainer, nsIStreamListener** aDocListener, bool aReset) {
  nsresult rv = MediaDocument::StartDocumentLoad(
      aCommand, aChannel, aLoadGroup, aContainer, aDocListener, aReset);
  NS_ENSURE_SUCCESS(rv, rv);

  mStreamListener = new MediaDocumentStreamListener(this);
  NS_ADDREF(*aDocListener = mStreamListener);
  return rv;
}

nsresult VideoDocument::StartLayout() {
  // Create video element, and begin loading the media resource. Note we
  // delay creating the video element until now (we're called from
  // MediaDocumentStreamListener::OnStartRequest) as the PresShell is likely
  // to have been created by now, so the MediaDecoder will be able to tell
  // what kind of compositor we have, so the video element knows whether
  // it can create a hardware accelerated video decoder or not.
  nsresult rv = CreateVideoElement();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = MediaDocument::StartLayout();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void VideoDocument::SetScriptGlobalObject(
    nsIScriptGlobalObject* aScriptGlobalObject) {
  // Set the script global object on the superclass before doing
  // anything that might require it....
  MediaDocument::SetScriptGlobalObject(aScriptGlobalObject);

  if (aScriptGlobalObject && !InitialSetupHasBeenDone()) {
    DebugOnly<nsresult> rv = CreateSyntheticDocument();
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to create synthetic video document");

    InitialSetupDone();
  }
}

nsresult VideoDocument::CreateVideoElement() {
  RefPtr<Element> body = GetBodyElement();
  if (!body) {
    NS_WARNING("no body on video document!");
    return NS_ERROR_FAILURE;
  }

  // make content
  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  nodeInfo = mNodeInfoManager->GetNodeInfo(
      nsGkAtoms::video, nullptr, kNameSpaceID_XHTML, nsINode::ELEMENT_NODE);

  RefPtr<HTMLMediaElement> element = static_cast<HTMLMediaElement*>(
      NS_NewHTMLVideoElement(nodeInfo.forget(), NOT_FROM_PARSER));

  // Do not autoplay when forcing this as a media document, e.g., for page info.
  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
  element->SetAutoplay(
      loadInfo->GetForceMediaDocument() == ForceMediaDocument::None,
      IgnoreErrors());

  element->SetControls(true, IgnoreErrors());
  element->LoadWithChannel(mChannel,
                           getter_AddRefs(mStreamListener->mNextStream));
  UpdateTitle(mChannel);

  if (nsContentUtils::IsChildOfSameType(this)) {
    // Video documents that aren't toplevel should fill their frames and
    // not have margins
    element->SetAttr(
        kNameSpaceID_None, nsGkAtoms::style,
        nsLiteralString(
            u"position:absolute; top:0; left:0; width:100%; height:100%"),
        true, IsKnownNewAttr::Yes);
  } else {
    LinkStylesheet(nsLiteralString(
        u"resource://content-accessible/TopLevelVideoDocument.css"));
    LinkScript(u"resource://content-accessible/TopLevelVideoDocument.js"_ns);
  }

  ErrorResult rv;
  body->AppendChildTo(element, false, rv);
  return rv.StealNSResult();
}

void VideoDocument::UpdateTitle(nsIChannel* aChannel) {
  if (!aChannel) return;

  nsAutoString fileName;
  GetFileName(fileName, aChannel);
  IgnoredErrorResult ignored;
  SetTitle(fileName, ignored);
}

}  // namespace mozilla::dom

nsresult NS_NewVideoDocument(mozilla::dom::Document** aResult,
                             nsIPrincipal* aPrincipal,
                             nsIPrincipal* aPartitionedPrincipal) {
  auto* doc = new mozilla::dom::VideoDocument();

  NS_ADDREF(doc);
  nsresult rv = doc->Init(aPrincipal, aPartitionedPrincipal);

  if (NS_FAILED(rv)) {
    NS_RELEASE(doc);
  }

  *aResult = doc;

  return rv;
}
