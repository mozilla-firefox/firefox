/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * BaseContentList is a basic list of content nodes; ContentList
 * is a commonly used NodeList implementation (used for
 * getElementsByTagName, some properties on HTMLDocument/Document, etc).
 */

#include "mozilla/dom/ContentList.h"

#include <algorithm>

#include "PLDHashTable.h"
#include "jsfriendapi.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/MruCache.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLCollectionBinding.h"
#include "mozilla/dom/HTMLLabelElement.h"
#include "mozilla/dom/NodeInfoInlines.h"
#include "mozilla/dom/NodeListBinding.h"
#include "nsCCUncollectableMarker.h"
#include "nsContentUtils.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsTHashtable.h"
#include "nsWrapperCacheInlines.h"

#ifdef DEBUG_CONTENT_LIST
#  define ASSERT_IN_SYNC AssertInSync()
#else
#  define ASSERT_IN_SYNC PR_BEGIN_MACRO PR_END_MACRO
#endif

using namespace mozilla::dom;

namespace mozilla::dom {

BaseContentList::~BaseContentList() = default;

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(BaseContentList)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(BaseContentList)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mElements)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
  tmp->RemoveFromCaches();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(BaseContentList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mElements)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(BaseContentList)
  if (nsCCUncollectableMarker::sGeneration && tmp->HasKnownLiveWrapper()) {
    for (uint32_t i = 0; i < tmp->mElements.Length(); ++i) {
      nsIContent* c = tmp->mElements[i];
      if (c->IsPurple()) {
        c->RemovePurple();
      }
      Element::MarkNodeChildren(c);
    }
    if (nsINode* root = tmp->GetParentObject()) {
      if (root->IsContent() && root->AsContent()->IsPurple()) {
        root->AsContent()->RemovePurple();
      }
      Element::MarkNodeChildren(root);
    }
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(BaseContentList)
  return nsCCUncollectableMarker::sGeneration && tmp->HasKnownLiveWrapper();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(BaseContentList)
  return nsCCUncollectableMarker::sGeneration && tmp->HasKnownLiveWrapper();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

// QueryInterface implementation for BaseContentList
NS_INTERFACE_TABLE_HEAD(BaseContentList)
  NS_WRAPPERCACHE_INTERFACE_TABLE_ENTRY
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(BaseContentList)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(BaseContentList)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE(BaseContentList,
                                                   LastRelease())

nsIContent* BaseContentList::Item(uint32_t aIndex) {
  return mElements.SafeElementAt(aIndex);
}

int32_t BaseContentList::IndexOf(nsIContent* aContent) {
  return mElements.IndexOf(aContent);
}

size_t BaseContentList::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);
  n += mElements.ShallowSizeOfExcludingThis(aMallocSizeOf);
  return n;
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(SimpleContentList, BaseContentList, mRoot)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SimpleContentList)
NS_INTERFACE_MAP_END_INHERITING(BaseContentList)

NS_IMPL_ADDREF_INHERITED(SimpleContentList, BaseContentList)
NS_IMPL_RELEASE_INHERITED(SimpleContentList, BaseContentList)

JSObject* SimpleContentList::WrapObject(JSContext* cx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return NodeList_Binding::Wrap(cx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(EmptyContentList, HTMLCollection, mRoot)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(EmptyContentList)
NS_INTERFACE_MAP_END_INHERITING(HTMLCollection)

NS_IMPL_ADDREF_INHERITED(EmptyContentList, HTMLCollection)
NS_IMPL_RELEASE_INHERITED(EmptyContentList, HTMLCollection)

JSObject* EmptyContentList::WrapObject(JSContext* cx,
                                       JS::Handle<JSObject*> aGivenProto) {
  return HTMLCollection_Binding::Wrap(cx, this, aGivenProto);
}

struct ContentListCache
    : public MruCache<ContentListKey, ContentList*, ContentListCache> {
  static HashNumber Hash(const ContentListKey& aKey) { return aKey.GetHash(); }
  static bool Match(const ContentListKey& aKey, const ContentList* aVal) {
    return aVal->MatchesKey(aKey);
  }
};

static ContentListCache sRecentlyUsedContentLists;

class ContentList::HashEntry : public PLDHashEntryHdr {
 public:
  using KeyType = const ContentListKey*;
  using KeyTypePointer = KeyType;

  // Note that this is creating a blank entry, so you'll have to manually
  // initialize it after it has been inserted into the hash table.
  explicit HashEntry(KeyTypePointer aKey) : mContentList(nullptr) {}

  HashEntry(HashEntry&& aEnt) : mContentList(std::move(aEnt.mContentList)) {}

  ~HashEntry() {
    if (mContentList) {
      MOZ_RELEASE_ASSERT(mContentList->mInHashtable);
      mContentList->mInHashtable = false;
    }
  }

  bool KeyEquals(KeyTypePointer aKey) const {
    return mContentList->MatchesKey(*aKey);
  }

  static KeyTypePointer KeyToPointer(KeyType aKey) { return aKey; }

  static PLDHashNumber HashKey(KeyTypePointer aKey) { return aKey->GetHash(); }

  ContentList* GetContentList() const { return mContentList; }
  void SetContentList(ContentList* aContentList) {
    MOZ_RELEASE_ASSERT(!mContentList);
    MOZ_ASSERT(aContentList);
    MOZ_RELEASE_ASSERT(!aContentList->mInHashtable);
    mContentList = aContentList;
    mContentList->mInHashtable = true;
  }

  enum { ALLOW_MEMMOVE = true };

 private:
  ContentList* MOZ_UNSAFE_REF(
      "This entry will be removed in ContentList::RemoveFromHashtable "
      "before mContentList is destroyed") mContentList;
};

// Hashtable for storing ContentLists
static StaticAutoPtr<nsTHashtable<ContentList::HashEntry>>
    gContentListHashTable;

#ifdef DEBUG
const CacheableFuncStringContentList::ContentListType
    CachableElementsByNameNodeList::sType =
        CacheableFuncStringContentList::eNodeList;
const CacheableFuncStringContentList::ContentListType
    CacheableFuncStringHTMLCollection::sType =
        CacheableFuncStringContentList::eHTMLCollection;
#endif

class CacheableFuncStringContentList::HashEntry : public PLDHashEntryHdr {
 public:
  using KeyType = const FuncStringCacheKey*;
  using KeyTypePointer = KeyType;

  // Note that this is creating a blank entry, so you'll have to manually
  // initialize it after it has been inserted into the hash table.
  explicit HashEntry(KeyTypePointer aKey) : mContentList(nullptr) {}

  HashEntry(HashEntry&& aEnt) : mContentList(std::move(aEnt.mContentList)) {}

  ~HashEntry() {
    if (mContentList) {
      MOZ_RELEASE_ASSERT(mContentList->mInHashtable);
      mContentList->mInHashtable = false;
    }
  }

  bool KeyEquals(KeyTypePointer aKey) const {
    return mContentList->Equals(aKey);
  }

  static KeyTypePointer KeyToPointer(KeyType aKey) { return aKey; }

  static PLDHashNumber HashKey(KeyTypePointer aKey) { return aKey->GetHash(); }

  CacheableFuncStringContentList* GetContentList() const {
    return mContentList;
  }
  void SetContentList(CacheableFuncStringContentList* aContentList) {
    MOZ_RELEASE_ASSERT(!mContentList);
    MOZ_ASSERT(aContentList);
    MOZ_RELEASE_ASSERT(!aContentList->mInHashtable);
    mContentList = aContentList;
    mContentList->mInHashtable = true;
  }

  enum { ALLOW_MEMMOVE = true };

 private:
  CacheableFuncStringContentList* MOZ_UNSAFE_REF(
      "This entry will be removed in "
      "CacheableFuncStringContentList::RemoveFromFuncStringHashtable "
      "before mContentList is destroyed") mContentList;
};

// Hashtable for storing CacheableFuncStringContentList
static StaticAutoPtr<nsTHashtable<CacheableFuncStringContentList::HashEntry>>
    gFuncStringContentListHashTable;

//-----------------------------------------------------
// ContentList implementation

ContentList::ContentList(nsINode* aRootNode, int32_t aMatchNameSpaceId,
                         nsAtom* aHTMLMatchAtom, nsAtom* aXMLMatchAtom,
                         bool aDeep, bool aLiveList)
    : mRootNode(aRootNode),
      mMatchNameSpaceId(aMatchNameSpaceId),
      mHTMLMatchAtom(aHTMLMatchAtom),
      mXMLMatchAtom(aXMLMatchAtom),
      mState(State::Dirty),
      mDeep(aDeep),
      mFuncMayDependOnAttr(false),
      mIsHTMLDocument(aRootNode->OwnerDoc()->IsHTMLDocument()),
      mNamedItemsCacheValid(false),
      mIsLiveList(aLiveList),
      mInHashtable(false) {
  NS_ASSERTION(mRootNode, "Must have root");
  if (nsGkAtoms::_asterisk == mHTMLMatchAtom) {
    NS_ASSERTION(mXMLMatchAtom == nsGkAtoms::_asterisk,
                 "HTML atom and XML atom are not both asterisk?");
    mMatchAll = true;
  } else {
    mMatchAll = false;
  }
  // This is aLiveList instead of mIsLiveList to avoid Valgrind errors.
  if (aLiveList) {
    SetEnabledCallbacks(nsIMutationObserver::kNodeWillBeDestroyed);
    mRootNode->AddMutationObserver(this);
  }
}

ContentList::ContentList(nsINode* aRootNode, nsContentListMatchFunc aFunc,
                         nsContentListDestroyFunc aDestroyFunc, void* aData,
                         bool aDeep, nsAtom* aMatchAtom,
                         int32_t aMatchNameSpaceId, bool aFuncMayDependOnAttr,
                         bool aLiveList)
    : mRootNode(aRootNode),
      mMatchNameSpaceId(aMatchNameSpaceId),
      mHTMLMatchAtom(aMatchAtom),
      mXMLMatchAtom(aMatchAtom),
      mFunc(aFunc),
      mDestroyFunc(aDestroyFunc),
      mData(aData),
      mState(State::Dirty),
      mMatchAll(false),
      mDeep(aDeep),
      mFuncMayDependOnAttr(aFuncMayDependOnAttr),
      mIsHTMLDocument(false),
      mNamedItemsCacheValid(false),
      mIsLiveList(aLiveList),
      mInHashtable(false) {
  NS_ASSERTION(mRootNode, "Must have root");
  // This is aLiveList instead of mIsLiveList to avoid Valgrind errors.
  if (aLiveList) {
    SetEnabledCallbacks(nsIMutationObserver::kNodeWillBeDestroyed);
    mRootNode->AddMutationObserver(this);
  }
}

ContentList::~ContentList() {
  RemoveFromHashtable();
  if (mIsLiveList && mRootNode) {
    mRootNode->RemoveMutationObserver(this);
  }

  if (mDestroyFunc) {
    // Clean up mData
    (*mDestroyFunc)(mData);
  }
}

JSObject* ContentList::WrapObject(JSContext* cx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return HTMLCollection_Binding::Wrap(cx, this, aGivenProto);
}

NS_IMPL_CYCLE_COLLECTION_CLASS(ContentList)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(ContentList, HTMLCollection)
  tmp->DisconnectFromRoot();
  tmp->SetDirty();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(ContentList, HTMLCollection)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRootNode)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ContentList)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
NS_INTERFACE_MAP_END_INHERITING(HTMLCollection)

NS_IMPL_ADDREF_INHERITED(ContentList, HTMLCollection)
NS_IMPL_RELEASE_INHERITED(ContentList, HTMLCollection)

uint32_t ContentList::Length() {
  BringSelfUpToDate();

  return mElements.Length();
}

Element* ContentList::Item(uint32_t aIndex) {
  uint32_t count = mElements.Length();
  MOZ_ASSERT_IF(mState == State::Dirty, !count);
  if (aIndex >= count && mState != State::UpToDate) {
    // Prefetch progressively more elements if access is sequential
    uint32_t prefetch = aIndex == count ? std::min(count, 64u) : 0;
    PopulateSelf(std::min(aIndex, UINT32_MAX - 1 - prefetch) + prefetch + 1);
  }

  ASSERT_IN_SYNC;
  NS_ASSERTION(!mRootNode || mState != State::Dirty,
               "PopulateSelf left the list in a dirty (useless) state!");

  nsIContent* content = mElements.SafeElementAt(aIndex);
  MOZ_ASSERT(!content || content->IsElement(),
             "ContentList only contains elements");
  return static_cast<Element*>(content);
}

inline void ContentList::InsertElementInNamedItemsCache(nsIContent& aContent) {
  const bool hasName = aContent.HasName();
  const bool hasId = aContent.HasID();
  if (!hasName && !hasId) {
    return;
  }

  Element* el = aContent.AsElement();
  MOZ_ASSERT_IF(hasName, el->IsHTMLElement());

  uint32_t i = 0;
  while (BorrowedAttrInfo info = el->GetAttrInfoAt(i++)) {
    const bool valid = (info.mName->Equals(nsGkAtoms::name) && hasName) ||
                       (info.mName->Equals(nsGkAtoms::id) && hasId);
    if (!valid) {
      continue;
    }

    if (!mNamedItemsCache) {
      mNamedItemsCache = std::make_unique<NamedItemsCache>();
    }

    nsAtom* name = info.mValue->GetAtomValue();
    // NOTE: LookupOrInsert makes sure we keep the first element we find for a
    // given name.
    mNamedItemsCache->LookupOrInsert(name, el);
  }
}

inline void ContentList::InvalidateNamedItemsCacheForAttributeChange(
    int32_t aNamespaceID, nsAtom* aAttribute) {
  if (!mNamedItemsCacheValid) {
    return;
  }
  if ((aAttribute == nsGkAtoms::id || aAttribute == nsGkAtoms::name) &&
      aNamespaceID == kNameSpaceID_None) {
    InvalidateNamedItemsCache();
  }
}

inline void ContentList::InvalidateNamedItemsCacheForInsertion(
    Element& aElement) {
  if (!mNamedItemsCacheValid) {
    return;
  }

  InsertElementInNamedItemsCache(aElement);
}

inline void ContentList::InvalidateNamedItemsCacheForDeletion(
    Element& aElement) {
  if (!mNamedItemsCacheValid) {
    return;
  }
  if (aElement.HasName() || aElement.HasID()) {
    InvalidateNamedItemsCache();
  }
}

void ContentList::EnsureNamedItemsCacheValid() {
  BringSelfUpToDate();

  if (mNamedItemsCacheValid) {
    return;
  }

  MOZ_ASSERT(!mNamedItemsCache);

  // https://dom.spec.whatwg.org/#dom-htmlcollection-nameditem-key
  // XXX: Blink/WebKit don't follow the spec here, and searches first-by-id,
  // then by name.
  for (const nsCOMPtr<nsIContent>& content : mElements) {
    InsertElementInNamedItemsCache(*content);
  }

  mNamedItemsCacheValid = true;
}

Element* ContentList::GetFirstNamedElement(const nsAString& aName,
                                           bool& aFound) {
  aFound = false;
  if (aName.IsEmpty()) {
    return nullptr;
  }

  EnsureNamedItemsCacheValid();

  if (!mNamedItemsCache) {
    return nullptr;
  }

  // Typically IDs and names are atomized
  RefPtr<nsAtom> name = NS_Atomize(aName);
  NS_ENSURE_TRUE(name, nullptr);

  Element* item = mNamedItemsCache->Get(name);
  aFound = !!item;
  return item;
}

Element* HTMLCollection::DefaultGetFirstNamedElement(const nsAString& aName,
                                                     bool& aFound) {
  aFound = false;
  RefPtr<nsAtom> name = NS_Atomize(aName);
  for (nsIContent* content : mElements) {
    MOZ_DIAGNOSTIC_ASSERT(content);
    Element* element = content->AsElement();
    if (element->GetID() == name ||
        (element->HasName() &&
         element->GetParsedAttr(nsGkAtoms::name)->GetAtomValue() == name)) {
      aFound = true;
      return element;
    }
  }
  return nullptr;
}

void HTMLCollection::GetSupportedNames(nsTArray<nsString>& aNames,
                                       FilterElementWithName aFilter) {
  AutoTArray<nsAtom*, 8> atoms;
  for (nsIContent* content : mElements) {
    if (content->HasID()) {
      nsAtom* id = content->GetID();
      MOZ_ASSERT(id != nsGkAtoms::_empty, "Empty ids don't get atomized");
      if (!atoms.Contains(id)) {
        atoms.AppendElement(id);
      }
    }

    if (auto* el = nsGenericHTMLElement::FromNode(content)) {
      // XXXbz should we be checking for particular tags here?  How
      // stable is this part of the spec?
      // Note: nsINode::HasName means the name is exposed on the document,
      // which is false for options, so we don't check it here.
      const nsAttrValue* val = el->GetParsedAttr(nsGkAtoms::name);
      if (val && val->Type() == nsAttrValue::eAtom &&
          (!aFilter || aFilter(el))) {
        nsAtom* name = val->GetAtomValue();
        MOZ_ASSERT(name != nsGkAtoms::_empty, "Empty names don't get atomized");
        if (!atoms.Contains(name)) {
          atoms.AppendElement(name);
        }
      }
    }
  }

  uint32_t atomsLen = atoms.Length();
  nsString* names = aNames.AppendElements(atomsLen);
  for (uint32_t i = 0; i < atomsLen; ++i) {
    atoms[i]->ToString(names[i]);
  }
}

int32_t ContentList::IndexOf(nsIContent* aContent) {
  BringSelfUpToDate();

  return mElements.IndexOf(aContent);
}

void ContentList::DisconnectFromRoot() {
  if (mIsLiveList && mRootNode) {
    mRootNode->RemoveMutationObserver(this);
  }
  mRootNode = nullptr;
}

void ContentList::LastRelease() {
  RemoveFromCaches();
  DisconnectFromRoot();
  SetDirty();
}

void ContentList::AttributeChanged(Element* aElement, int32_t aNameSpaceID,
                                   nsAtom* aAttribute, AttrModType,
                                   const nsAttrValue* aOldValue) {
  MOZ_ASSERT(aElement, "Must have a content node to work with");

  if (mState == State::Dirty ||
      !MayContainRelevantNodes(aElement->GetParentNode()) ||
      !nsContentUtils::IsInSameAnonymousTree(mRootNode, aElement)) {
    // Either we're already dirty or aElement will never match us.
    return;
  }

  InvalidateNamedItemsCacheForAttributeChange(aNameSpaceID, aAttribute);

  if (!mFunc || !mFuncMayDependOnAttr) {
    // aElement might be relevant but the attribute change doesn't affect
    // whether we match it.
    return;
  }

  if (Match(aElement)) {
    if (mElements.IndexOf(aElement) == mElements.NoIndex) {
      // We match aElement now, and it's not in our list already.  Just dirty
      // ourselves; this is simpler than trying to figure out where to insert
      // aElement.
      SetDirty();
    }
  } else {
    // We no longer match aElement.  Remove it from our list.  If it's
    // already not there, this is a no-op (though a potentially
    // expensive one).  Either way, no change of mState is required
    // here.
    if (mElements.RemoveElement(aElement)) {
      InvalidateNamedItemsCacheForDeletion(*aElement);
    }
  }
}

void ContentList::ContentAppended(nsIContent* aFirstNewContent,
                                  const ContentAppendInfo&) {
  nsIContent* container = aFirstNewContent->GetParent();
  MOZ_ASSERT(container, "Can't get at the new content if no container!");

  /*
   * If the state is State::Dirty then we have no useful information in our list
   * and we want to put off doing work as much as possible.
   *
   * Also, if container is anonymous from our point of view, we know that we
   * can't possibly be matching any of the kids.
   *
   * Optimize out also the common case when just one new node is appended and
   * it doesn't match us.
   */
  if (mState == State::Dirty ||
      !nsContentUtils::IsInSameAnonymousTree(mRootNode, container) ||
      !MayContainRelevantNodes(container) ||
      (!aFirstNewContent->HasChildren() &&
       !aFirstNewContent->GetNextSibling() &&
       !MatchSelf<MatchSelfMode::Insertion>(aFirstNewContent))) {
    MaybeMarkDirty();
    return;
  }

  /*
   * We want to handle the case of ContentAppended by sometimes
   * appending the content to our list, not just setting state to
   * State::Dirty, since most of our ContentAppended notifications
   * should come during pageload and be at the end of the document.
   * Do a bit of work to see whether we could just append to what we
   * already have.
   */

  uint32_t ourCount = mElements.Length();
  const bool appendingToList = [&] {
    if (ourCount == 0) {
      return true;
    }
    if (mRootNode == container) {
      return true;
    }
    return nsContentUtils::PositionIsBefore(mElements.LastElement(),
                                            aFirstNewContent);
  }();

  if (!appendingToList) {
    // The new stuff is somewhere in the middle of our list; check
    // whether we need to invalidate
    for (nsIContent* cur = aFirstNewContent; cur; cur = cur->GetNextSibling()) {
      if (MatchSelf<MatchSelfMode::Insertion>(cur)) {
        // Uh-oh.  We're gonna have to add elements into the middle
        // of our list. That's not worth the effort.
        SetDirty();
        break;
      }
    }

    ASSERT_IN_SYNC;
    return;
  }

  /*
   * At this point we know we could append.  If we're not up to
   * date, however, that would be a bad idea -- it could miss some
   * content that we never picked up due to being lazy.  Further, we
   * may never get asked for this content... so don't grab it yet.
   */
  if (mState == State::Lazy) {
    return;
  }

  /*
   * We're up to date.  That means someone's actively using us; we
   * may as well grab this content....
   */
  if (mDeep) {
    for (nsIContent* cur = aFirstNewContent; cur;
         cur = cur->GetNextNode(container)) {
      if (cur->IsElement() && Match(cur->AsElement())) {
        mElements.AppendElement(cur);
        InvalidateNamedItemsCacheForInsertion(*cur->AsElement());
      }
    }
  } else {
    for (nsIContent* cur = aFirstNewContent; cur; cur = cur->GetNextSibling()) {
      if (cur->IsElement() && Match(cur->AsElement())) {
        mElements.AppendElement(cur);
        InvalidateNamedItemsCacheForInsertion(*cur->AsElement());
      }
    }
  }

  ASSERT_IN_SYNC;
}

void ContentList::ContentInserted(nsIContent* aChild,
                                  const ContentInsertInfo&) {
  // Note that aChild->GetParentNode() can be null here if we are inserting into
  // the document itself; any attempted optimizations to this method should deal
  // with that.
  if (mState != State::Dirty &&
      MayContainRelevantNodes(aChild->GetParentNode()) &&
      nsContentUtils::IsInSameAnonymousTree(mRootNode, aChild) &&
      MatchSelf<MatchSelfMode::Insertion>(aChild)) {
    SetDirty();
  }

  ASSERT_IN_SYNC;
}

void ContentList::ContentWillBeRemoved(nsIContent* aChild,
                                       const ContentRemoveInfo&) {
  if (mState != State::Dirty &&
      MayContainRelevantNodes(aChild->GetParentNode()) &&
      nsContentUtils::IsInSameAnonymousTree(mRootNode, aChild) &&
      MatchSelf<MatchSelfMode::Removal>(aChild)) {
    SetDirty();
  }

  ASSERT_IN_SYNC;
}

bool ContentList::Match(Element* aElement) {
  if (mFunc) {
    return (*mFunc)(aElement, mMatchNameSpaceId, mXMLMatchAtom, mData);
  }

  if (!mXMLMatchAtom) return false;

  NodeInfo* ni = aElement->NodeInfo();

  bool unknown = mMatchNameSpaceId == kNameSpaceID_Unknown;
  bool wildcard = mMatchNameSpaceId == kNameSpaceID_Wildcard;
  bool toReturn = mMatchAll;
  if (!unknown && !wildcard) toReturn &= ni->NamespaceEquals(mMatchNameSpaceId);

  if (toReturn) return toReturn;

  bool matchHTML =
      mIsHTMLDocument && aElement->GetNameSpaceID() == kNameSpaceID_XHTML;

  if (unknown) {
    return matchHTML ? ni->QualifiedNameEquals(mHTMLMatchAtom)
                     : ni->QualifiedNameEquals(mXMLMatchAtom);
  }

  if (wildcard) {
    return matchHTML ? ni->Equals(mHTMLMatchAtom) : ni->Equals(mXMLMatchAtom);
  }

  return matchHTML ? ni->Equals(mHTMLMatchAtom, mMatchNameSpaceId)
                   : ni->Equals(mXMLMatchAtom, mMatchNameSpaceId);
}

template <ContentList::MatchSelfMode Mode>
bool ContentList::MatchSelf(nsIContent* aContent) {
  MOZ_ASSERT(aContent, "Can't match null stuff, you know");
  MOZ_ASSERT(mDeep || aContent->GetParentNode() == mRootNode,
             "MatchSelf called on a node that we can't possibly match");

  if (!aContent->IsElement()) {
    return false;
  }

  auto matches = [&](Element* aElement) {
    if (Match(aElement)) {
      return true;
    }
    if constexpr (Mode == MatchSelfMode::Removal) {
      return mFunc == nsContentUtils::MatchClassNames &&
             aElement->IsSVGElement() && aElement->MayHaveClass();
    }
    return false;
  };

  if (matches(aContent->AsElement())) {
    return true;
  }

  if (!mDeep) return false;

  for (nsIContent* cur = aContent->GetFirstChild(); cur;
       cur = cur->GetNextNode(aContent)) {
    if (cur->IsElement() && matches(cur->AsElement())) {
      return true;
    }
  }

  return false;
}

nsINode* ContentList::GetNextNode(nsINode* aCurrent) {
  return aCurrent->GetNextNode(mRootNode);
}

void ContentList::PopulateSelf(uint32_t aNeededLength) {
  if (!mRootNode) {
    return;
  }

  ASSERT_IN_SYNC;

  uint32_t count = mElements.Length();
  NS_ASSERTION(mState != State::Dirty || count == 0,
               "Reset() not called when setting state to State::Dirty?");

  if (count >= aNeededLength) {
    // An override may already have appended elements.
    return;
  }

  uint32_t elementsToAppend = aNeededLength - count;
#ifdef DEBUG
  uint32_t invariant = elementsToAppend + mElements.Length();
#endif

  if (mDeep) {
    // If we already have nodes start searching at the last one, otherwise
    // start searching at the root.
    nsINode* cur = count ? mElements[count - 1].get() : mRootNode.get();
    do {
      cur = GetNextNode(cur);
      if (!cur) {
        break;
      }
      if (cur->IsElement() && Match(cur->AsElement())) {
        // Append AsElement() to get nsIContent instead of nsINode
        mElements.AppendElement(cur->AsElement());
        --elementsToAppend;
      }
    } while (elementsToAppend);
  } else {
    nsIContent* cur = count ? mElements[count - 1]->GetNextSibling()
                            : mRootNode->GetFirstChild();
    for (; cur && elementsToAppend; cur = cur->GetNextSibling()) {
      if (cur->IsElement() && Match(cur->AsElement())) {
        mElements.AppendElement(cur);
        --elementsToAppend;
      }
    }
  }

  NS_ASSERTION(elementsToAppend + mElements.Length() == invariant,
               "Something is awry!");

  if (elementsToAppend != 0) {
    mState = State::UpToDate;
  } else {
    mState = State::Lazy;
  }

  SetEnabledCallbacks(nsIMutationObserver::kAll);

  ASSERT_IN_SYNC;
}

void ContentList::RemoveFromHashtable() {
  if (mFunc) {
    // CacheableFuncStringContentList can be in a hash table without being
    // in gContentListHashTable, but it will have been removed from the hash
    // table in its dtor before it runs the ContentList dtor.
    MOZ_RELEASE_ASSERT(!mInHashtable);

    // This can't be in gContentListHashTable.
    return;
  }

  nsDependentAtomString str(mXMLMatchAtom);
  ContentListKey key(mRootNode, mMatchNameSpaceId, str, mIsHTMLDocument);
  sRecentlyUsedContentLists.Remove(key);

  if (gContentListHashTable) {
    gContentListHashTable->RemoveEntry(&key);

    if (gContentListHashTable->Count() == 0) {
      gContentListHashTable = nullptr;
    }
  }

  MOZ_RELEASE_ASSERT(!mInHashtable);
}

void ContentList::BringSelfUpToDate() {
  if (mState != State::UpToDate) {
    PopulateSelf(uint32_t(-1));
  }

  mMissedUpdates = 0;

  ASSERT_IN_SYNC;
  NS_ASSERTION(!mRootNode || mState == State::UpToDate,
               "PopulateSelf dod not bring content list up to date!");
}

CacheableFuncStringContentList::~CacheableFuncStringContentList() {
  RemoveFromFuncStringHashtable();
}

void CacheableFuncStringContentList::RemoveFromFuncStringHashtable() {
  if (!gFuncStringContentListHashTable) {
    MOZ_RELEASE_ASSERT(!mInHashtable);
    return;
  }

  FuncStringCacheKey key(mRootNode, mFunc, mString);
  gFuncStringContentListHashTable->RemoveEntry(&key);

  if (gFuncStringContentListHashTable->Count() == 0) {
    gFuncStringContentListHashTable = nullptr;
  }

  MOZ_RELEASE_ASSERT(!mInHashtable);
}

#ifdef DEBUG_CONTENT_LIST
void ContentList::AssertInSync() {
  if (mState == State::Dirty) {
    return;
  }

  if (!mRootNode) {
    NS_ASSERTION(mElements.Length() == 0 && mState == State::Dirty,
                 "Empty iterator isn't quite empty?");
    return;
  }

  // XXX This code will need to change if ContentLists can ever match
  // elements that are outside of the document element.
  nsIContent* root = mRootNode->IsDocument()
                         ? mRootNode->AsDocument()->GetRootElement()
                         : mRootNode->AsContent();

  PreContentIterator preOrderIter;
  if (mDeep) {
    preOrderIter.Init(root);
    preOrderIter.First();
  }

  uint32_t cnt = 0, index = 0;
  while (true) {
    if (cnt == mElements.Length() && mState == State::Lazy) {
      break;
    }

    nsIContent* cur =
        mDeep ? preOrderIter.GetCurrentNode() : mRootNode->GetChildAt(index++);
    if (!cur) {
      break;
    }

    if (cur->IsElement() && Match(cur->AsElement())) {
      NS_ASSERTION(cnt < mElements.Length() && mElements[cnt] == cur,
                   "Elements is out of sync");
      ++cnt;
    }

    if (mDeep) {
      preOrderIter.Next();
    }
  }

  NS_ASSERTION(cnt == mElements.Length(), "Too few elements");
}
#endif

//-----------------------------------------------------
// CachableElementsByNameNodeList

JSObject* CachableElementsByNameNodeList::WrapObject(
    JSContext* cx, JS::Handle<JSObject*> aGivenProto) {
  return NodeList_Binding::Wrap(cx, this, aGivenProto);
}

void CachableElementsByNameNodeList::AttributeChanged(
    Element* aElement, int32_t aNameSpaceID, nsAtom* aAttribute,
    AttrModType aModType, const nsAttrValue* aOldValue) {
  // No need to rebuild the list if the changed attribute is not the name
  // attribute.
  if (aAttribute != nsGkAtoms::name) {
    InvalidateNamedItemsCacheForAttributeChange(aNameSpaceID, aAttribute);
    return;
  }

  CacheableFuncStringContentList::AttributeChanged(
      aElement, aNameSpaceID, aAttribute, aModType, aOldValue);
}

//-----------------------------------------------------
// CacheableFuncStringHTMLCollection

JSObject* CacheableFuncStringHTMLCollection::WrapObject(
    JSContext* cx, JS::Handle<JSObject*> aGivenProto) {
  return HTMLCollection_Binding::Wrap(cx, this, aGivenProto);
}

//-----------------------------------------------------
// LabelsNodeList

LabelsNodeList::LabelsNodeList(nsGenericHTMLElement* aLabeledElement,
                               nsINode* aSubtreeRoot,
                               nsContentListMatchFunc aMatchFunc,
                               nsContentListDestroyFunc aDestroyFunc)
    : ContentList(aSubtreeRoot, aMatchFunc, aDestroyFunc, aLabeledElement) {
  if (auto* shadow = ShadowRoot::FromNodeOrNull(aSubtreeRoot)) {
    shadow->Host()->AddReferenceTargetChangeObserver(ResetRootsCallback, this);
  }
  mRoots.AppendElement(aSubtreeRoot);
  ResetRoots();
}

LabelsNodeList::~LabelsNodeList() {
  for (nsINode* root : mRoots) {
    root->RemoveMutationObserver(this);
    if (auto* shadow = ShadowRoot::FromNodeOrNull(root)) {
      if (Element* host = shadow->GetHost()) {
        host->RemoveReferenceTargetChangeObserver(ResetRootsCallback, this);
      }
    }
  }
}

JSObject* LabelsNodeList::WrapObject(JSContext* cx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return NodeList_Binding::Wrap(cx, this, aGivenProto);
}

bool LabelsNodeList::NodeIsInScope(nsINode* aNode) {
  for (nsINode* root : mRoots) {
    if (nsContentUtils::IsInSameAnonymousTree(root, aNode)) {
      return true;
    }
  }
  return false;
}

void LabelsNodeList::AttributeChanged(Element* aElement, int32_t aNameSpaceID,
                                      nsAtom* aAttribute, AttrModType,
                                      const nsAttrValue* aOldValue) {
  MOZ_ASSERT(aElement, "Must have a content node to work with");
  if (mState == State::Dirty || !NodeIsInScope(aElement)) {
    return;
  }

  InvalidateNamedItemsCacheForAttributeChange(aNameSpaceID, aAttribute);

  // We need to handle input type changes to or from "hidden".
  if (aElement->IsHTMLElement(nsGkAtoms::input) &&
      aAttribute == nsGkAtoms::type && aNameSpaceID == kNameSpaceID_None) {
    SetDirty();
    return;
  }

  // We need to handle changes to `<label for>`.
  if (aElement->IsHTMLElement(nsGkAtoms::label) &&
      aAttribute == nsGkAtoms::_for && aNameSpaceID == kNameSpaceID_None) {
    SetDirty();
    return;
  }

  // We need to handle changes to the `id` attribute.
  if (aAttribute == nsGkAtoms::id && aNameSpaceID == kNameSpaceID_None) {
    SetDirty();
    return;
  }
}

void LabelsNodeList::ContentAppended(nsIContent* aFirstNewContent,
                                     const ContentAppendInfo&) {
  nsIContent* container = aFirstNewContent->GetParent();
  // If a labelable element is moved to outside or inside of nested associated
  // labels, we're gonna have to modify the content list.
  if (mState != State::Dirty && NodeIsInScope(container)) {
    SetDirty();
    return;
  }
}

void LabelsNodeList::ContentInserted(nsIContent* aChild,
                                     const ContentInsertInfo&) {
  // If a labelable element is moved to outside or inside of nested associated
  // labels, we're gonna have to modify the content list.
  if (mState != State::Dirty && NodeIsInScope(aChild)) {
    SetDirty();
    return;
  }
}

void LabelsNodeList::ContentWillBeRemoved(nsIContent* aChild,
                                          const ContentRemoveInfo&) {
  // If a labelable element is removed, we're gonna have to clean the content
  // list.
  if (mState != State::Dirty && NodeIsInScope(aChild)) {
    SetDirty();
    return;
  }
}

void LabelsNodeList::NodeWillBeDestroyed(nsINode* aNode) {
  RemoveFromCaches();
  DisconnectFromRoot();
  SetDirty();
  mData = nullptr;
}

// static
bool LabelsNodeList::ResetRootsCallback(void* aData) {
  auto* list = (LabelsNodeList*)aData;
  list->ResetRoots();
  return true;
}

void LabelsNodeList::ResetRoots() {
  MOZ_ASSERT(mIsLiveList, "LabelsNodeList is always a live list");

  auto* labeledElement = static_cast<nsGenericHTMLElement*>(mData);
  MOZ_ASSERT(labeledElement, "Must have labeled element");

  nsTArray<nsINode*> newRoots;

  Element* labeledElementOrHost = labeledElement;
  bool labeledElementOrHostIsInShadowTree = false;
  ShadowRoot* shadowRoot = labeledElement->GetContainingShadow();
  while (shadowRoot) {
    newRoots.AppendElement(shadowRoot);
    // If reference target is not enabled, GetReferenceTargetElement() will
    // always return nullptr.
    if (shadowRoot->GetReferenceTargetElement() != labeledElementOrHost) {
      labeledElementOrHostIsInShadowTree = true;
      break;
    }
    labeledElementOrHost = shadowRoot->Host();
    shadowRoot = labeledElementOrHost->GetContainingShadow();
  }

  // If the outermost labeled element or host is in a shadow tree, its
  // containing shadow root is already in newRoots.
  if (!labeledElementOrHostIsInShadowTree) {
    // `labeledHost` is either `labeledElement`, or the shadow host which has
    // `labeledElement` as its resolved reference target.
    if (Document* doc = labeledElementOrHost->GetUncomposedDoc()) {
      newRoots.AppendElement(doc);
    } else if (newRoots.IsEmpty()) {
      newRoots.AppendElement(labeledElementOrHost->SubtreeRoot());
    }
  }

  if (newRoots == mRoots) {
    return;
  }
  MOZ_ASSERT(!newRoots.IsEmpty(), "Must have at least one root");

  for (nsINode* root : mRoots) {
    if (!newRoots.Contains(root)) {
      root->RemoveMutationObserver(this);
    }

    // Only the outermost shadow root should have this as a
    // ReferenceTargetChangedObserver, to avoid duplicated notifications.
    if (auto* shadow = ShadowRoot::FromNodeOrNull(root)) {
      if (Element* host = shadow->GetHost()) {
        host->RemoveReferenceTargetChangeObserver(ResetRootsCallback, this);
      }
    }
  }
  for (nsINode* root : newRoots) {
    if (!mRoots.Contains(root)) {
      root->AddMutationObserver(this);
    }
  }

  mRoots = std::move(newRoots);
  mRootNode = mRoots.LastElement();

  if (labeledElementOrHostIsInShadowTree) {
    auto* shadow = ShadowRoot::FromNodeOrNull(mRootNode);
    MOZ_ASSERT(shadow);
    shadow->Host()->AddReferenceTargetChangeObserver(ResetRootsCallback, this);
  }
  labeledElementOrHost->AddReferenceTargetChangeObserver(ResetRootsCallback,
                                                         this);

  SetDirty();
}

nsINode* LabelsNodeList::GetNextNode(nsINode* aCurrent) {
  auto* labeledElement = (nsGenericHTMLElement*)mData;
  MOZ_ASSERT(labeledElement, "Must have labeled element");
  MOZ_ASSERT(mRootNode, "Must have root node");

  nsINode* next = nullptr;

  // If aCurrent's resolved reference target is the labeled element, descend
  // into aCurrent's shadow root, if it has one. (Otherwise, ignore shadow
  // roots.)
  if (aCurrent->IsElement()) {
    Element* curElement = aCurrent->AsElement();
    ShadowRoot* curShadow = curElement->GetShadowRoot();
    if (curShadow && curElement->ResolveReferenceTarget() == labeledElement) {
      next = curShadow->GetFirstChild();
    }
  }
  if (next) {
    return next;
  }

  // Default case: just get the next node in the current tree.
  next = aCurrent->GetNextNode();
  if (next) {
    return next;
  }

  // If we descended into a shadow tree, back out of it until we find an
  // adjacent node, or hit a shadow root which doesn't have the current element
  // as its reference target.
  nsINode* cur = aCurrent;
  while (!next) {
    ShadowRoot* shadow = cur->GetContainingShadow();
    if (!shadow || shadow->GetReferenceTargetElement() == cur) {
      break;
    }
    cur = shadow->Host();
    next = cur->GetNextNode();
  }
  return next;
}

void LabelsNodeList::PopulateSelf(uint32_t aNeededLength) {
  if (!mRootNode) {
    return;
  }

  // Start searching at the root.
  nsINode* cur = mRootNode;
  if (mElements.IsEmpty() && cur->IsElement() && Match(cur->AsElement())) {
    mElements.AppendElement(cur->AsElement());
    mState = State::Lazy;
    SetEnabledCallbacks(nsIMutationObserver::kAll);
  }

  ContentList::PopulateSelf(aNeededLength);
}

void LabelsNodeList::DisconnectFromRoot() {
  for (nsINode* root : mRoots) {
    root->RemoveMutationObserver(this);
    if (auto* shadow = ShadowRoot::FromNodeOrNull(root)) {
      if (Element* host = shadow->GetHost()) {
        host->RemoveReferenceTargetChangeObserver(ResetRootsCallback, this);
      }
    }
  }
  mRoots.Clear();

  ContentList::DisconnectFromRoot();
}

}  // namespace mozilla::dom

already_AddRefed<ContentList> NS_GetContentList(nsINode* aRootNode,
                                                int32_t aMatchNameSpaceId,
                                                const nsAString& aTagname) {
  NS_ASSERTION(aRootNode, "content list has to have a root");

  RefPtr<ContentList> list;
  ContentListKey hashKey(aRootNode, aMatchNameSpaceId, aTagname,
                         aRootNode->OwnerDoc()->IsHTMLDocument());
  auto p = sRecentlyUsedContentLists.Lookup(hashKey);
  if (p) {
    list = p.Data();
    return list.forget();
  }

  // Initialize the hashtable if needed.
  if (!gContentListHashTable) {
    gContentListHashTable = new nsTHashtable<ContentList::HashEntry>();
  }

  // First we look in our hashtable.  Then we create a content list if needed
  auto entry = gContentListHashTable->PutEntry(&hashKey, mozilla::fallible);
  if (entry) {
    list = entry->GetContentList();
  }

  if (!list) {
    // We need to create a ContentList and add it to our new entry, if
    // we have an entry
    RefPtr<nsAtom> xmlAtom = NS_Atomize(aTagname);
    RefPtr<nsAtom> htmlAtom;
    if (aMatchNameSpaceId == kNameSpaceID_Unknown) {
      nsAutoString lowercaseName;
      nsContentUtils::ASCIIToLower(aTagname, lowercaseName);
      htmlAtom = NS_Atomize(lowercaseName);
    } else {
      htmlAtom = xmlAtom;
    }
    list = new ContentList(aRootNode, aMatchNameSpaceId, htmlAtom, xmlAtom);
    if (entry) {
      entry->SetContentList(list);
    }
  }

  p.Set(list);
  return list.forget();
}

template <class ListType>
already_AddRefed<ContentList> GetFuncStringContentList(
    nsINode* aRootNode, nsContentListMatchFunc aFunc,
    nsContentListDestroyFunc aDestroyFunc,
    nsFuncStringContentListDataAllocator aDataAllocator,
    const nsAString& aString) {
  NS_ASSERTION(aRootNode, "content list has to have a root");

  RefPtr<CacheableFuncStringContentList> list;

  // Initialize the hashtable if needed.
  if (!gFuncStringContentListHashTable) {
    gFuncStringContentListHashTable =
        new nsTHashtable<CacheableFuncStringContentList::HashEntry>();
  }

  CacheableFuncStringContentList::HashEntry* entry = nullptr;
  // First we look in our hashtable.  Then we create a content list if needed
  if (gFuncStringContentListHashTable) {
    FuncStringCacheKey hashKey(aRootNode, aFunc, aString);

    entry =
        gFuncStringContentListHashTable->PutEntry(&hashKey, mozilla::fallible);
    if (entry) {
      list = entry->GetContentList();
#ifdef DEBUG
      MOZ_ASSERT_IF(list, list->mType == ListType::sType);
#endif
    }
  }

  if (!list) {
    // We need to create a ContentList and add it to our new entry, if
    // we have an entry
    list =
        new ListType(aRootNode, aFunc, aDestroyFunc, aDataAllocator, aString);
    if (entry) {
      entry->SetContentList(list);
    }
  }

  // Don't cache these lists globally

  return list.forget();
}

// Explicit instantiations to avoid link errors
template already_AddRefed<ContentList>
GetFuncStringContentList<CachableElementsByNameNodeList>(
    nsINode* aRootNode, nsContentListMatchFunc aFunc,
    nsContentListDestroyFunc aDestroyFunc,
    nsFuncStringContentListDataAllocator aDataAllocator,
    const nsAString& aString);
template already_AddRefed<ContentList>
GetFuncStringContentList<CacheableFuncStringHTMLCollection>(
    nsINode* aRootNode, nsContentListMatchFunc aFunc,
    nsContentListDestroyFunc aDestroyFunc,
    nsFuncStringContentListDataAllocator aDataAllocator,
    const nsAString& aString);
