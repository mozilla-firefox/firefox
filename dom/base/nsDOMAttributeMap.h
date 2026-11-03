/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of the |attributes| property of DOM Core's Element object.
 */

#ifndef nsDOMAttributeMap_h
#define nsDOMAttributeMap_h

#include "mozilla/MemoryReporting.h"
#include "nsCycleCollectionParticipant.h"
#include "nsRefPtrHashtable.h"
#include "nsString.h"
#include "nsWrapperCache.h"

class nsAtom;
class nsAttrValue;
class nsINode;
class nsIPrincipal;
class nsNodeInfoManager;

namespace mozilla {
class ErrorResult;

namespace dom {
class Attr;
class DocGroup;
class Document;
class Element;
class NodeInfo;
}  // namespace dom
}  // namespace mozilla

/**
 * Structure used as a key for caching Attrs in nsDOMAttributeMap's
 * mAttributeCache.
 */
class nsAttrKey {
 public:
  /**
   * The namespace of the attribute
   */
  int32_t mNamespaceID;

  /**
   * The atom for attribute, stored as void*, to make sure that we only use it
   * for the hashcode, and we can never dereference it.
   */
  void* mLocalName;

  nsAttrKey(int32_t aNs, nsAtom* aName)
      : mNamespaceID(aNs), mLocalName(aName) {}

  nsAttrKey(const nsAttrKey& aAttr) = default;
};

/**
 * PLDHashEntryHdr implementation for nsAttrKey.
 */
class nsAttrHashKey : public PLDHashEntryHdr {
 public:
  using KeyType = const nsAttrKey&;
  using KeyTypePointer = const nsAttrKey*;

  explicit nsAttrHashKey(KeyTypePointer aKey) : mKey(*aKey) {}
  nsAttrHashKey(const nsAttrHashKey& aCopy)
      : PLDHashEntryHdr{}, mKey(aCopy.mKey) {}
  ~nsAttrHashKey() = default;

  KeyType GetKey() const { return mKey; }
  bool KeyEquals(KeyTypePointer aKey) const {
    return mKey.mLocalName == aKey->mLocalName &&
           mKey.mNamespaceID == aKey->mNamespaceID;
  }

  static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }
  static PLDHashNumber HashKey(KeyTypePointer aKey) {
    if (!aKey) return 0;

    return mozilla::HashGeneric(aKey->mNamespaceID, aKey->mLocalName);
  }
  enum { ALLOW_MEMMOVE = true };

 private:
  nsAttrKey mKey;
};

class nsDOMAttributeMap final : public nsISupports, public nsWrapperCache {
 public:
  using Attr = mozilla::dom::Attr;
  using DocGroup = mozilla::dom::DocGroup;
  using Document = mozilla::dom::Document;
  using Element = mozilla::dom::Element;
  using ErrorResult = mozilla::ErrorResult;

  explicit nsDOMAttributeMap(Element* aContent);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_WRAPPERCACHE_CLASS(nsDOMAttributeMap)

  void DropReference();

  Element* GetContent() { return mContent; }

  /**
   * Called when mContent is adopted into a new document.
   * Adopts all cached Attr nodes into aManager's document.
   */
  void AdoptCachedAttributes(nsNodeInfoManager* aManager);

  /**
   * Drop an attribute from the map's cache (does not remove the attribute
   * from the node!)
   * aValue is optional and avoids looking up the attribute value when supplied.
   */
  void DropAttribute(int32_t aNamespaceID, nsAtom* aLocalName,
                     const nsAttrValue* aValue = nullptr);

  /**
   * Returns the number of attribute nodes currently in the map.
   * Note: this is just the number of cached attribute nodes, not the number of
   * attributes in mContent.
   *
   * @return The number of attribute nodes in the map.
   */
  uint32_t Count() const;

  using AttrCache = nsRefPtrHashtable<nsAttrHashKey, Attr>;

  static void BlastSubtreeToPieces(nsINode* aNode);

  Element* GetParentObject() const { return mContent; }
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;
  DocGroup* GetDocGroup() const;

  // WebIDL
  Attr* GetNamedItem(const nsAString& aAttrName);
  Attr* NamedGetter(const nsAString& aAttrName, bool& aFound);
  already_AddRefed<Attr> RemoveNamedItem(mozilla::dom::NodeInfo* aNodeInfo,
                                         ErrorResult& aError);
  already_AddRefed<Attr> RemoveNamedItem(const nsAString& aName,
                                         ErrorResult& aError);

  Attr* Item(uint32_t aIndex);
  Attr* IndexedGetter(uint32_t aIndex, bool& aFound);
  uint32_t Length() const;

  Attr* GetNamedItemNS(const nsAString& aNamespaceURI,
                       const nsAString& aLocalName);
  MOZ_CAN_RUN_SCRIPT already_AddRefed<Attr> SetNamedItemNS(
      Attr& aNode, nsIPrincipal* aSubjectPrincipal, ErrorResult& aError);
  already_AddRefed<Attr> RemoveNamedItemNS(const nsAString& aNamespaceURI,
                                           const nsAString& aLocalName,
                                           ErrorResult& aError);

  void GetSupportedNames(nsTArray<nsString>& aNames);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

 protected:
  ~nsDOMAttributeMap();

 private:
  nsCOMPtr<Element> mContent;

  /**
   * Cache of Attrs.
   */
  AttrCache mAttributeCache;

  already_AddRefed<mozilla::dom::NodeInfo> GetAttrNodeInfo(
      const nsAString& aNamespaceURI, const nsAString& aLocalName);

  Attr* GetAttribute(mozilla::dom::NodeInfo* aNodeInfo);
};

#endif /* nsDOMAttributeMap_h */
