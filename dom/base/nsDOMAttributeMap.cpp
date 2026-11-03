/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of the |attributes| property of DOM Core's Element object.
 */

#include "nsDOMAttributeMap.h"

#include "mozilla/MemoryReporting.h"
#include "mozilla/dom/Attr.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/NamedNodeMapBinding.h"
#include "mozilla/dom/NodeInfoInlines.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "nsAttrName.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsIContentInlines.h"
#include "nsNameSpaceManager.h"
#include "nsNodeInfoManager.h"
#include "nsUnicharUtils.h"
#include "nsWrapperCacheInlines.h"

using namespace mozilla;
using namespace mozilla::dom;

//----------------------------------------------------------------------

nsDOMAttributeMap::nsDOMAttributeMap(Element* aContent) : mContent(aContent) {
  // We don't add a reference to our content. If it goes away,
  // we'll be told to drop our reference
}

nsDOMAttributeMap::~nsDOMAttributeMap() { DropReference(); }

void nsDOMAttributeMap::DropReference() {
  for (auto iter = mAttributeCache.Iter(); !iter.Done(); iter.Next()) {
    iter.Data()->SetMap(nullptr);
    iter.Remove();
  }
  mContent = nullptr;
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(nsDOMAttributeMap)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsDOMAttributeMap)
  tmp->DropReference();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mContent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsDOMAttributeMap)
  for (const auto& entry : tmp->mAttributeCache) {
    cb.NoteXPCOMChild(static_cast<nsINode*>(entry.GetWeak()));
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mContent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(nsDOMAttributeMap)
  if (tmp->HasKnownLiveWrapper()) {
    if (tmp->mContent) {
      // The map owns the element so we can mark it when the
      // map itself is certainly alive.
      mozilla::dom::FragmentOrElement::MarkNodeChildren(tmp->mContent);
    }
    return true;
  }
  if (tmp->mContent &&
      mozilla::dom::FragmentOrElement::CanSkip(tmp->mContent, true)) {
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(nsDOMAttributeMap)
  return tmp->HasKnownLiveWrapperAndDoesNotNeedTracing(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(nsDOMAttributeMap)
  return tmp->HasKnownLiveWrapper();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

// QueryInterface implementation for nsDOMAttributeMap

NS_INTERFACE_MAP_BEGIN(nsDOMAttributeMap)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(nsDOMAttributeMap)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMAttributeMap)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsDOMAttributeMap)

void nsDOMAttributeMap::AdoptCachedAttributes(nsNodeInfoManager* aManager) {
  for (const auto& entry : mAttributeCache.Values()) {
    IgnoredErrorResult rv;
    entry->Adopt(aManager, rv);
  }
}

void nsDOMAttributeMap::DropAttribute(int32_t aNamespaceID, nsAtom* aLocalName,
                                      const nsAttrValue* aValue) {
  nsAttrKey attr(aNamespaceID, aLocalName);
  if (auto entry = mAttributeCache.Lookup(attr)) {
    entry.Data()->SetMap(nullptr, aValue);  // break link to map
    entry.Remove();
  }
}

Attr* nsDOMAttributeMap::GetAttribute(mozilla::dom::NodeInfo* aNodeInfo) {
  NS_ASSERTION(aNodeInfo, "GetAttribute() called with aNodeInfo == nullptr!");

  nsAttrKey attr(aNodeInfo->NamespaceID(), aNodeInfo->NameAtom());

  return mAttributeCache.LookupOrInsertWith(attr, [&] {
    // Newly inserted entry!
    RefPtr<mozilla::dom::NodeInfo> ni = aNodeInfo;
    auto* nim = ni->NodeInfoManager();
    return new (nim) Attr(this, ni.forget(), u""_ns);
  });
}

Attr* nsDOMAttributeMap::NamedGetter(const nsAString& aAttrName, bool& aFound) {
  aFound = false;
  NS_ENSURE_TRUE(mContent, nullptr);

  RefPtr<mozilla::dom::NodeInfo> ni =
      mContent->GetExistingAttrNameFromQName(aAttrName);
  if (!ni) {
    return nullptr;
  }

  aFound = true;
  return GetAttribute(ni);
}

void nsDOMAttributeMap::GetSupportedNames(nsTArray<nsString>& aNames) {
  // For HTML elements in HTML documents, only include names that are still the
  // same after ASCII-lowercasing, since our named getter will end up
  // ASCII-lowercasing the given string.
  bool lowercaseNamesOnly =
      mContent->IsHTMLElement() && mContent->IsInHTMLDocument();

  const uint32_t count = mContent->GetAttrCount();
  bool seenNonAtomName = false;
  for (uint32_t i = 0; i < count; i++) {
    const nsAttrName* name = mContent->GetAttrNameAt(i);
    seenNonAtomName = seenNonAtomName || !name->IsAtom();
    nsString qualifiedName;
    name->GetQualifiedName(qualifiedName);

    if (lowercaseNamesOnly &&
        nsContentUtils::StringContainsASCIIUpper(qualifiedName)) {
      continue;
    }

    // Omit duplicates.  We only need to do this check if we've seen a non-atom
    // name, because that's the only way we can have two identical qualified
    // names.
    if (seenNonAtomName && aNames.Contains(qualifiedName)) {
      continue;
    }

    aNames.AppendElement(qualifiedName);
  }
}

Attr* nsDOMAttributeMap::GetNamedItem(const nsAString& aAttrName) {
  bool dummy;
  return NamedGetter(aAttrName, dummy);
}

already_AddRefed<Attr> nsDOMAttributeMap::SetNamedItemNS(
    Attr& aAttr, nsIPrincipal* aSubjectPrincipal, ErrorResult& aError) {
  NS_ENSURE_TRUE(mContent, nullptr);

  nsAutoString value;
  aAttr.GetValue(value);

  RefPtr<NodeInfo> ni = aAttr.NodeInfo();

  Maybe<nsAutoString> compliantStringHolder;
  RefPtr<nsAtom> nameAtom = ni->NameAtom();
  nsCOMPtr<Element> element = mContent;
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantAttributeValue(
          *element, nameAtom, ni->NamespaceID(), value, aSubjectPrincipal,
          compliantStringHolder, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  // Check that attribute is not owned by somebody else
  nsDOMAttributeMap* owner = aAttr.GetMap();
  if (owner) {
    if (owner != this) {
      aError.Throw(NS_ERROR_DOM_INUSE_ATTRIBUTE_ERR);
      return nullptr;
    }

    // setting a preexisting attribute is a no-op, just return the same
    // node.
    RefPtr<Attr> attribute = &aAttr;
    return attribute.forget();
  }

  nsresult rv;
  if (mContent->OwnerDoc() != aAttr.OwnerDoc()) {
    DebugOnly<void*> adoptedNode =
        mContent->OwnerDoc()->AdoptNode(aAttr, aError);
    if (aError.Failed()) {
      return nullptr;
    }

    NS_ASSERTION(adoptedNode == &aAttr, "Uh, adopt node changed nodes?");
  }

  // Get nodeinfo and preexisting attribute (if it exists)
  RefPtr<NodeInfo> oldNi;

  uint32_t i, count = mContent->GetAttrCount();
  for (i = 0; i < count; ++i) {
    const nsAttrName* name = mContent->GetAttrNameAt(i);
    int32_t attrNS = name->NamespaceID();
    nsAtom* nameAtom = name->LocalName();

    // we're purposefully ignoring the prefix.
    if (aAttr.NodeInfo()->Equals(nameAtom, attrNS)) {
      oldNi = mContent->NodeInfo()->NodeInfoManager()->GetNodeInfo(
          nameAtom, name->GetPrefix(), aAttr.NodeInfo()->NamespaceID(),
          nsINode::ATTRIBUTE_NODE);
      break;
    }
  }

  RefPtr<Attr> oldAttr;

  if (oldNi) {
    oldAttr = GetAttribute(oldNi);

    if (oldAttr == &aAttr) {
      return oldAttr.forget();
    }

    if (oldAttr) {
      // Just remove it from our hashtable.  This has no side-effects, so we
      // don't have to recheck anything after we do it.  Then we'll add our new
      // Attr to the hashtable and do the actual attr set on the element.  This
      // will make the whole thing look like a single attribute mutation (with
      // the new attr node in place) as opposed to a removal and addition.
      BorrowedAttrInfo info = mContent->GetAttrInfoAt(i);
      MOZ_ASSERT(info.mName &&
                 info.mName->Equals(oldNi->NameAtom(), oldNi->NamespaceID()));
      DropAttribute(oldNi->NamespaceID(), oldNi->NameAtom(), info.mValue);
    }
  }

  // Add the new attribute to the attribute map before updating
  // its value in the element. @see bug 364413.
  nsAttrKey attrkey(ni->NamespaceID(), ni->NameAtom());
  mAttributeCache.InsertOrUpdate(attrkey, RefPtr{&aAttr});
  aAttr.SetMap(this);

  rv = mContent->SetAttr(ni->NamespaceID(), ni->NameAtom(), ni->GetPrefixAtom(),
                         *compliantString, nullptr, true,
                         oldNi ? IsKnownNewAttr::No : IsKnownNewAttr::Yes);
  if (NS_FAILED(rv)) {
    DropAttribute(ni->NamespaceID(), ni->NameAtom());
    aError.Throw(rv);
    return nullptr;
  }

  return oldAttr.forget();
}

already_AddRefed<Attr> nsDOMAttributeMap::RemoveNamedItem(NodeInfo* aNodeInfo,
                                                          ErrorResult& aError) {
  RefPtr<Attr> attribute = GetAttribute(aNodeInfo);
  // This removes the attribute node from the attribute map.
  aError = mContent->UnsetAttr(aNodeInfo->NamespaceID(), aNodeInfo->NameAtom(),
                               true);
  return attribute.forget();
}

already_AddRefed<Attr> nsDOMAttributeMap::RemoveNamedItem(
    const nsAString& aName, ErrorResult& aError) {
  if (!mContent) {
    aError.Throw(NS_ERROR_DOM_NOT_FOUND_ERR);
    return nullptr;
  }

  RefPtr<mozilla::dom::NodeInfo> ni =
      mContent->GetExistingAttrNameFromQName(aName);
  if (!ni) {
    aError.Throw(NS_ERROR_DOM_NOT_FOUND_ERR);
    return nullptr;
  }

  return RemoveNamedItem(ni, aError);
}

Attr* nsDOMAttributeMap::IndexedGetter(uint32_t aIndex, bool& aFound) {
  aFound = false;
  NS_ENSURE_TRUE(mContent, nullptr);

  const nsAttrName* name;
  if (!mContent->GetAttrNameAt(aIndex, &name)) {
    return nullptr;
  }

  aFound = true;
  // Don't use the nodeinfo even if one exists since it can have the wrong
  // owner document.
  RefPtr<mozilla::dom::NodeInfo> ni =
      mContent->NodeInfo()->NodeInfoManager()->GetNodeInfo(
          name->LocalName(), name->GetPrefix(), name->NamespaceID(),
          nsINode::ATTRIBUTE_NODE);
  return GetAttribute(ni);
}

Attr* nsDOMAttributeMap::Item(uint32_t aIndex) {
  bool dummy;
  return IndexedGetter(aIndex, dummy);
}

uint32_t nsDOMAttributeMap::Length() const {
  NS_ENSURE_TRUE(mContent, 0);

  return mContent->GetAttrCount();
}

Attr* nsDOMAttributeMap::GetNamedItemNS(const nsAString& aNamespaceURI,
                                        const nsAString& aLocalName) {
  RefPtr<mozilla::dom::NodeInfo> ni =
      GetAttrNodeInfo(aNamespaceURI, aLocalName);
  if (!ni) {
    return nullptr;
  }

  return GetAttribute(ni);
}

already_AddRefed<mozilla::dom::NodeInfo> nsDOMAttributeMap::GetAttrNodeInfo(
    const nsAString& aNamespaceURI, const nsAString& aLocalName) {
  if (!mContent) {
    return nullptr;
  }

  int32_t nameSpaceID = kNameSpaceID_None;

  if (!aNamespaceURI.IsEmpty()) {
    nameSpaceID = nsNameSpaceManager::GetInstance()->GetNameSpaceID(
        aNamespaceURI, nsContentUtils::IsChromeDoc(mContent->OwnerDoc()));

    if (nameSpaceID == kNameSpaceID_Unknown) {
      return nullptr;
    }
  }

  uint32_t i, count = mContent->GetAttrCount();
  for (i = 0; i < count; ++i) {
    const nsAttrName* name = mContent->GetAttrNameAt(i);
    int32_t attrNS = name->NamespaceID();
    nsAtom* nameAtom = name->LocalName();

    // we're purposefully ignoring the prefix.
    if (nameSpaceID == attrNS && nameAtom->Equals(aLocalName)) {
      RefPtr<mozilla::dom::NodeInfo> ni;
      ni = mContent->NodeInfo()->NodeInfoManager()->GetNodeInfo(
          nameAtom, name->GetPrefix(), nameSpaceID, nsINode::ATTRIBUTE_NODE);

      return ni.forget();
    }
  }

  return nullptr;
}

already_AddRefed<Attr> nsDOMAttributeMap::RemoveNamedItemNS(
    const nsAString& aNamespaceURI, const nsAString& aLocalName,
    ErrorResult& aError) {
  RefPtr<mozilla::dom::NodeInfo> ni =
      GetAttrNodeInfo(aNamespaceURI, aLocalName);
  if (!ni) {
    aError.Throw(NS_ERROR_DOM_NOT_FOUND_ERR);
    return nullptr;
  }

  return RemoveNamedItem(ni, aError);
}

uint32_t nsDOMAttributeMap::Count() const { return mAttributeCache.Count(); }

size_t nsDOMAttributeMap::SizeOfIncludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);

  n += mAttributeCache.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mAttributeCache) {
    n += aMallocSizeOf(entry.GetWeak());
  }

  // NB: mContent is non-owning and thus not counted.
  return n;
}

/* virtual */
JSObject* nsDOMAttributeMap::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return NamedNodeMap_Binding::Wrap(aCx, this, aGivenProto);
}

DocGroup* nsDOMAttributeMap::GetDocGroup() const {
  return mContent ? mContent->OwnerDoc()->GetDocGroup() : nullptr;
}
