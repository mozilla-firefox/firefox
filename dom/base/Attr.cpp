/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of DOM Core's Attr node.
 */

#include "mozilla/dom/Attr.h"

#include "NodeUbiReporting.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/AttrBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "nsCOMArray.h"
#include "nsContentCreatorFunctions.h"
#include "nsDOMString.h"
#include "nsError.h"
#include "nsIContentInlines.h"
#include "nsNameSpaceManager.h"
#include "nsNodeSupportsWeakRefTearoff.h"
#include "nsTextNode.h"
#include "nsUnicharUtils.h"
#include "nsWrapperCacheInlines.h"

namespace mozilla::dom {

//----------------------------------------------------------------------
bool Attr::sInitialized;

Attr::Attr(nsDOMAttributeMap* aAttrMap,
           already_AddRefed<dom::NodeInfo> aNodeInfo, const nsAString& aValue)
    : nsINode(std::move(aNodeInfo)), mAttrMap(aAttrMap), mValue(aValue) {
  MOZ_ASSERT(mNodeInfo, "We must get a nodeinfo here!");
  MOZ_ASSERT(mNodeInfo->NodeType() == ATTRIBUTE_NODE, "Wrong nodeType");

  // We don't add a reference to our content. It will tell us
  // to drop our reference when it goes away.
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(Attr)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Attr)
  if (!nsINode::Traverse(tmp, cb)) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAttrMap)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Attr)
  nsINode::Unlink(tmp);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAttrMap)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(Attr)
  Element* ownerElement = tmp->GetElement();
  if (tmp->HasKnownLiveWrapper()) {
    if (ownerElement) {
      // The attribute owns the element via attribute map so we can
      // mark it when the attribute is certainly alive.
      mozilla::dom::FragmentOrElement::MarkNodeChildren(ownerElement);
    }
    return true;
  }
  if (ownerElement &&
      mozilla::dom::FragmentOrElement::CanSkip(ownerElement, true)) {
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(Attr)
  return tmp->HasKnownLiveWrapperAndDoesNotNeedTracing(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(Attr)
  return tmp->HasKnownLiveWrapper();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

// QueryInterface implementation for Attr
NS_INTERFACE_TABLE_HEAD(Attr)
  NS_WRAPPERCACHE_INTERFACE_TABLE_ENTRY
  NS_INTERFACE_TABLE(Attr, nsINode, EventTarget)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(Attr)
  NS_INTERFACE_MAP_ENTRY_TEAROFF(nsISupportsWeakReference,
                                 new nsNodeSupportsWeakRefTearoff(this))
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(Attr)

NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE_AND_DESTROY(Attr,
                                                               LastRelease(),
                                                               Destroy())

NS_IMPL_DOMARENA_DESTROY(Attr)

void Attr::SetMap(nsDOMAttributeMap* aMap, const nsAttrValue* aValue) {
  if (mAttrMap && !aMap && sInitialized) {
    // We're breaking a relationship with content and not getting a new one,
    // need to locally cache value.
    if (aValue) {
#ifdef DEBUG
      nsAutoString expectedValue;
      GetValue(expectedValue);
      MOZ_ASSERT(aValue->Equals(expectedValue, nsCaseTreatment::eCaseMatters));
#endif
      aValue->ToString(mValue);
    } else {
      GetValue(mValue);
    }
  }

  mAttrMap = aMap;
}

Element* Attr::GetElement() const {
  if (!mAttrMap) {
    return nullptr;
  }
  nsIContent* content = mAttrMap->GetContent();
  return content ? content->AsElement() : nullptr;
}

void Attr::GetName(nsAString& aName) { aName = NodeName(); }

void Attr::GetValue(nsAString& aValue) {
  Element* element = GetElement();
  if (element) {
    element->GetAttr(mNodeInfo->NamespaceID(), mNodeInfo->NameAtom(), aValue);
  } else {
    aValue = mValue;
  }
}

void Attr::SetValue(const nsAString& aValue, nsIPrincipal* aTriggeringPrincipal,
                    ErrorResult& aRv) {
  nsCOMPtr<Element> element = GetElement();
  if (!element) {
    mValue = aValue;
    return;
  }

  RefPtr<nsAtom> nameAtom = mNodeInfo->NameAtom();

  Maybe<nsAutoString> compliantStringHolder;
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantAttributeValue(
          *element, nameAtom, mNodeInfo->NamespaceID(), aValue,
          aTriggeringPrincipal, compliantStringHolder, aRv);
  if (aRv.Failed()) {
    return;
  }

  // After the GetTrustedTypesCompliantAttributeValue() call, the attribute may
  // have been detached or moved to another element so perform the check on
  // GetElement() again.
  SetValueInternal(*compliantString, aRv);
}

void Attr::SetValueInternal(const nsAString& aValue, ErrorResult& aRv) {
  Element* element = GetElement();
  if (!element) {
    mValue = aValue;
    return;
  }

  RefPtr<nsAtom> nameAtom = mNodeInfo->NameAtom();
  aRv = element->SetAttr(mNodeInfo->NamespaceID(), nameAtom,
                         mNodeInfo->GetPrefixAtom(), aValue, nullptr, true,
                         IsKnownNewAttr::No);
}

bool Attr::Specified() const { return true; }

Element* Attr::GetOwnerElement() { return GetElement(); }

void Attr::SetNodeValue(const nsAString& aNodeValue, ErrorResult& aError) {
  SetValue(aNodeValue, nullptr, aError);
}

void Attr::GetNodeValueInternal(nsAString& aNodeValue) { GetValue(aNodeValue); }

void Attr::SetNodeValueInternal(const nsAString& aNodeValue,
                                ErrorResult& aError, MutationEffectOnScript) {
  SetValueInternal(aNodeValue, aError);
}

nsresult Attr::Clone(dom::NodeInfo* aNodeInfo, nsINode** aResult) const {
  nsAutoString value;
  const_cast<Attr*>(this)->GetValue(value);
  *aResult = new (aNodeInfo->NodeInfoManager())
      Attr(nullptr, do_AddRef(aNodeInfo), value);

  NS_ADDREF(*aResult);

  return NS_OK;
}

nsIURI* Attr::GetBaseURI(bool aTryUseXHRDocBaseURI) const {
  Element* parent = GetElement();

  return parent ? parent->GetBaseURI(aTryUseXHRDocBaseURI)
                : OwnerDoc()->GetBaseURI(aTryUseXHRDocBaseURI);
}

void Attr::SetTextContent(const nsAString& aTextContent,
                          nsIPrincipal* aSubjectPrincipal,
                          ErrorResult& aError) {
  SetValue(aTextContent, aSubjectPrincipal, aError);
}

void Attr::GetTextContentInternal(nsAString& aTextContent,
                                  OOMReporter& aError) {
  GetValue(aTextContent);
}

void Attr::SetTextContentInternal(const nsAString& aTextContent,
                                  nsIPrincipal* aSubjectPrincipal,
                                  ErrorResult& aError, MutationEffectOnScript) {
  SetValueInternal(aTextContent, aError);
}

void Attr::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mCanHandle = true;
}

void Attr::Initialize() { sInitialized = true; }

void Attr::Shutdown() { sInitialized = false; }

JSObject* Attr::WrapNode(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return Attr_Binding::Wrap(aCx, this, aGivenProto);
}

void Attr::ConstructUbiNode(void* storage) {
  JS::ubi::Concrete<Attr>::construct(storage, this);
}

}  // namespace mozilla::dom
