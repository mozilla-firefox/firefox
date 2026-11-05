/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WebBrowserPersistDocumentParent_h_
#define WebBrowserPersistDocumentParent_h_

#include "mozilla/PWebBrowserPersistDocumentParent.h"
#include "nsCOMPtr.h"
#include "nsIWebBrowserPersistDocument.h"

// This class is the IPC half of the glue between the
// nsIWebBrowserPersistDocument interface and a remote document.  When
// (and if) it receives the Attributes message it constructs an
// WebBrowserPersistRemoteDocument and releases it into the XPCOM
// universe; otherwise, it invokes the document receiver's error
// callback.
//
// This object's lifetime is the normal IPC lifetime; on destruction,
// it calls its XPCOM reflection (if it exists yet) to remove that
// reference.  Normal deletion occurs when the XPCOM object is being
// destroyed or after an InitFailure is received and handled.
//
// See also: BrowserParent::StartPersistence.

namespace mozilla {

class WebBrowserPersistRemoteDocument;

class WebBrowserPersistDocumentParent final
    : public PWebBrowserPersistDocumentParent {
 public:
  NS_INLINE_DECL_REFCOUNTING(WebBrowserPersistDocumentParent, override)

  WebBrowserPersistDocumentParent();

  // Set a callback to be invoked when the actor leaves the START
  // state.  This method must be called exactly once while the actor
  // is still in the START state (or is unconstructed).
  void SetOnReady(nsIWebBrowserPersistDocumentReceiver* aOnReady);

  // True while the actor is in the START state and no callback has
  // been attached to it yet.  Used to validate the sub-document actors
  // that the child sends over PWebBrowserPersistResources, which are
  // required to be freshly constructed.
  bool IsUnclaimedStartState() const { return !mOnReady && !mReflection; }

  using Attrs = WebBrowserPersistDocumentAttrs;

  // IPDL methods:
  mozilla::ipc::IPCResult RecvAttributes(
      Attrs&& aAttrs, Tainted<NotNull<RefPtr<nsIPrincipal>>>&& aPrincipal,
      nsIInputStream* aPostStream);
  mozilla::ipc::IPCResult RecvInitFailure(const nsresult& aFailure);

  PWebBrowserPersistResourcesParent* AllocPWebBrowserPersistResourcesParent();
  bool DeallocPWebBrowserPersistResourcesParent(
      PWebBrowserPersistResourcesParent* aActor);

  PWebBrowserPersistSerializeParent* AllocPWebBrowserPersistSerializeParent(
      const WebBrowserPersistURIMap& aMap,
      const nsACString& aRequestedContentType, const uint32_t& aEncoderFlags,
      const uint32_t& aWrapColumn);
  bool DeallocPWebBrowserPersistSerializeParent(
      PWebBrowserPersistSerializeParent* aActor);

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  virtual ~WebBrowserPersistDocumentParent();

  // This is reset to nullptr when the callback is invoked.
  nsCOMPtr<nsIWebBrowserPersistDocumentReceiver> mOnReady;
  WebBrowserPersistRemoteDocument* mReflection;
};

}  // namespace mozilla

#endif  // WebBrowserPersistDocumentParent_h_
