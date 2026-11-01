/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLAreaElement_h
#define mozilla_dom_HTMLAreaElement_h

#include "mozilla/Attributes.h"
#include "mozilla/dom/Link.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"

class nsIFrame;

namespace mozilla {
class EventChainPostVisitor;
class EventChainPreVisitor;
namespace dom {

class HTMLAreaElement final : public nsGenericHTMLElement, public Link {
 public:
  explicit HTMLAreaElement(already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo);

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // CC
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLAreaElement,
                                           nsGenericHTMLElement)

  NS_DECL_ADDSIZEOFEXCLUDINGTHIS

  NS_IMPL_FROMNODE_HTML_WITH_TAG(HTMLAreaElement, area)

  virtual int32_t TabIndexDefault() override;

  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;
  MOZ_CAN_RUN_SCRIPT
  nsresult PostHandleEvent(EventChainPostVisitor& aVisitor) override;

  void GetLinkTargetImpl(nsAString& aTarget) override;
  already_AddRefed<nsIURI> GetHrefURI() const override;

  virtual nsresult BindToTree(BindContext&, nsINode& aParent) override;
  virtual void UnbindFromTree(UnbindContext&) override;

  virtual nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;

  // WebIDL
  void GetAlt(DOMString& aValue) { GetHTMLAttr(nsGkAtoms::alt, aValue); }
  void SetAlt(const nsAString& aAlt, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::alt, aAlt, aError);
  }

  void GetCoords(DOMString& aValue) { GetHTMLAttr(nsGkAtoms::coords, aValue); }
  void SetCoords(const nsAString& aCoords, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::coords, aCoords, aError);
  }

  // argument type nsAString for HTMLImageMapAccessible
  void GetShape(nsAString& aValue) { GetHTMLAttr(nsGkAtoms::shape, aValue); }
  void SetShape(const nsAString& aShape, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::shape, aShape, aError);
  }

  void GetHref(nsACString& aValue) {
    GetURIAttr(nsGkAtoms::href, nullptr, aValue);
  }
  void SetHref(const nsACString& aHref, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::href, NS_ConvertUTF8toUTF16(aHref), aError);
  }

  void GetTarget(DOMString& aValue);
  void SetTarget(const nsAString& aTarget, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::target, aTarget, aError);
  }

  void GetDownload(DOMString& aValue) {
    GetHTMLAttr(nsGkAtoms::download, aValue);
  }
  void SetDownload(const nsAString& aDownload, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::download, aDownload, aError);
  }

  void GetHreflang(DOMString& aValue) const {
    GetHTMLAttr(nsGkAtoms::hreflang, aValue);
  }
  void SetHreflang(const nsAString& aValue, mozilla::ErrorResult& rv) {
    SetHTMLAttr(nsGkAtoms::hreflang, aValue, rv);
  }
  void GetType(DOMString& aValue) const {
    GetHTMLAttr(nsGkAtoms::type, aValue);
  }
  void SetType(const nsAString& aValue, mozilla::ErrorResult& rv) {
    SetHTMLAttr(nsGkAtoms::type, aValue, rv);
  }

  void GetPing(DOMString& aValue) { GetHTMLAttr(nsGkAtoms::ping, aValue); }
  void SetPing(const nsAString& aPing, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::ping, aPing, aError);
  }

  void GetRel(DOMString& aValue) { GetHTMLAttr(nsGkAtoms::rel, aValue); }
  void SetRel(const nsAString& aRel, ErrorResult& aError) {
    SetHTMLAttr(nsGkAtoms::rel, aRel, aError);
  }
  nsDOMTokenList* RelList();

  void SetReferrerPolicy(const nsAString& aValue, mozilla::ErrorResult& rv) {
    SetHTMLAttr(nsGkAtoms::referrerpolicy, aValue, rv);
  }
  void GetReferrerPolicy(nsAString& aReferrer) {
    GetEnumAttr(nsGkAtoms::referrerpolicy, "", aReferrer);
  }

  // The Link::GetOrigin is OK for us

  // Link::Link::GetProtocol is OK for us
  // Link::Link::SetProtocol is OK for us

  // The Link::GetUsername is OK for us
  // The Link::SetUsername is OK for us

  // The Link::GetPassword is OK for us
  // The Link::SetPassword is OK for us

  // Link::Link::GetHost is OK for us
  // Link::Link::SetHost is OK for us

  // Link::Link::GetHostname is OK for us
  // Link::Link::SetHostname is OK for us

  // Link::Link::GetPort is OK for us
  // Link::Link::SetPort is OK for us

  // Link::Link::GetPathname is OK for us
  // Link::Link::SetPathname is OK for us

  // Link::Link::GetSearch is OK for us
  // Link::Link::SetSearch is OK for us

  // Link::Link::GetHash is OK for us
  // Link::Link::SetHash is OK for us

  // The Link::GetSearchParams is OK for us

  bool NoHref() const { return GetBoolAttr(nsGkAtoms::nohref); }

  void SetNoHref(bool aValue, ErrorResult& aError) {
    SetHTMLBoolAttr(nsGkAtoms::nohref, aValue, aError);
  }

  void NodeInfoChanged(Document* aOldDoc) final {
    ClearHasPendingLinkUpdate();
    nsGenericHTMLElement::NodeInfoChanged(aOldDoc);
  }

  // The frame of the image using the map this <area> is in.  If multiple
  // images share a map, this is the one that last set up the map.
  nsIFrame* GetImageFrame() const { return mImageFrame; }
  void SetImageFrame(nsIFrame* aFrame) { mImageFrame = aFrame; }

 protected:
  virtual ~HTMLAreaElement();

  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) override;

  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;

  virtual void AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                            const nsAttrValue* aValue,
                            const nsAttrValue* aOldValue,
                            nsIPrincipal* aSubjectPrincipal,
                            bool aNotify) override;

  RefPtr<nsDOMTokenList> mRelList;
  nsIFrame* mImageFrame = nullptr;
};

}  // namespace dom
}  // namespace mozilla

#endif /* mozilla_dom_HTMLAreaElement_h */
