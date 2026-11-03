/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of DOM Core's Attr node.
 */

#ifndef mozilla_dom_Attr_h
#define mozilla_dom_Attr_h

#include "mozilla/Attributes.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDOMAttributeMap.h"
#include "nsINode.h"
#include "nsString.h"
#include "nsStubMutationObserver.h"

namespace mozilla {
class EventChainPreVisitor;
namespace dom {

class Document;

// Attribute helper class used to wrap up an attribute with a dom
// object that implements the DOM Attr interface.
class Attr final : public nsINode {
  virtual ~Attr() = default;

 public:
  static constexpr bool kHasWeakReferenceTearoff = true;

  Attr(nsDOMAttributeMap* aAttrMap, already_AddRefed<dom::NodeInfo> aNodeInfo,
       const nsAString& aValue);

  NS_DECL_ISUPPORTS_INHERITED
  NS_IMETHOD_(void) DeleteCycleCollectable(void) final;

  NS_DECL_DOMARENA_DESTROY

  NS_IMPL_FROMNODE_HELPER(Attr, IsAttr())

  // nsINode interface
  MOZ_CAN_RUN_SCRIPT void SetTextContent(const nsAString& aTextContent,
                                         nsIPrincipal* aSubjectPrincipal,
                                         mozilla::ErrorResult& aError) override;
  virtual void GetTextContentInternal(nsAString& aTextContent,
                                      OOMReporter& aError) override;
  virtual void SetTextContentInternal(const nsAString& aTextContent,
                                      nsIPrincipal* aSubjectPrincipal,
                                      ErrorResult& aError,
                                      MutationEffectOnScript) override;
  MOZ_CAN_RUN_SCRIPT void SetNodeValue(const nsAString& aNodeValue,
                                       mozilla::ErrorResult& aError) override;
  virtual void GetNodeValueInternal(nsAString& aNodeValue) override;
  virtual void SetNodeValueInternal(
      const nsAString& aNodeValue, ErrorResult& aError,
      MutationEffectOnScript aMutationEffectOnScript =
          MutationEffectOnScript::DropTrustWorthiness) override;

  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override;

  void ConstructUbiNode(void* storage) override;

  nsDOMAttributeMap* GetMap() { return mAttrMap; }

  // When detaching, aValue can avoid looking up the current attribute value.
  void SetMap(nsDOMAttributeMap* aMap, const nsAttrValue* aValue = nullptr);

  Element* GetElement() const;

  // nsINode interface
  nsresult Clone(dom::NodeInfo*, nsINode** aResult) const override;
  nsIURI* GetBaseURI(bool aTryUseXHRDocBaseURI = false) const override;

  static void Initialize();
  static void Shutdown();

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_WRAPPERCACHE_CLASS(Attr)

  // WebIDL
  virtual JSObject* WrapNode(JSContext* aCx,
                             JS::Handle<JSObject*> aGivenProto) override;

  void GetName(nsAString& aName);
  void GetValue(nsAString& aValue);

  MOZ_CAN_RUN_SCRIPT void SetValue(const nsAString& aValue,
                                   nsIPrincipal* aTriggeringPrincipal,
                                   ErrorResult& aRv);
  void SetValueInternal(const nsAString& aValue, ErrorResult& aRv);

  bool Specified() const;

  // XPCOM GetNamespaceURI() is OK
  // XPCOM GetPrefix() is OK
  // XPCOM GetLocalName() is OK

  Element* GetOwnerElement();

 protected:
  virtual Element* GetNameSpaceElement() override { return GetElement(); }

  static bool sInitialized;

 private:
  RefPtr<nsDOMAttributeMap> mAttrMap;
  nsString mValue;
};

}  // namespace dom
}  // namespace mozilla

#endif /* mozilla_dom_Attr_h */
