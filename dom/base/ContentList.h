/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * BaseContentList is a basic list of content nodes; ContentList
 * is a commonly used HTMLCollection implementation (used for
 * getElementsByTagName, some properties on HTMLDocument/Document, etc).
 */

#ifndef mozilla_dom_ContentList_h_
#define mozilla_dom_ContentList_h_

#include "mozilla/Attributes.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/NodeList.h"
#include "nsAtomHashKeys.h"
#include "nsContentListDeclarations.h"
#include "nsCycleCollectionParticipant.h"
#include "nsHashKeys.h"
#include "nsISupports.h"
#include "nsNameSpaceManager.h"
#include "nsString.h"
#include "nsStubMutationObserver.h"
#include "nsTArray.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

class BaseContentList : public NodeList {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  // NodeList
  int32_t IndexOf(nsIContent* aContent) override;
  nsIContent* Item(uint32_t aIndex) override;

  uint32_t Length() override { return mElements.Length(); }

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_WRAPPERCACHE_CLASS(BaseContentList)

  void AppendElement(nsIContent* aContent) {
    MOZ_ASSERT(aContent);
    mElements.AppendElement(aContent);
  }
  void MaybeAppendElement(nsIContent* aContent) {
    if (aContent) {
      AppendElement(aContent);
    }
  }

  /**
   * Insert the element at a given index, shifting the objects at
   * the given index and later to make space.
   * @param aContent Element to insert, must not be null
   * @param aIndex Index to insert the element at.
   */
  void InsertElementAt(nsIContent* aContent, int32_t aIndex) {
    NS_ASSERTION(aContent, "Element to insert must not be null");
    mElements.InsertElementAt(aIndex, aContent);
  }

  void RemoveElement(nsIContent* aContent) {
    mElements.RemoveElement(aContent);
  }

  void Reset() { mElements.Clear(); }

  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override = 0;

  void SetCapacity(uint32_t aCapacity) { mElements.SetCapacity(aCapacity); }

  virtual void LastRelease() {}

  // Memory reporting.  For now, subclasses of BaseContentList don't really
  // need to report any members that are not part of the object itself, so we
  // don't need to make this virtual.
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

 protected:
  virtual ~BaseContentList();

  /**
   * To be called from non-destructor locations (e.g. unlink) that want to
   * remove from caches.  Cacheable subclasses should override.
   */
  virtual void RemoveFromCaches() {}

  AutoTArray<nsCOMPtr<nsIContent>, 10> mElements;
};

class SimpleContentList : public BaseContentList {
 public:
  explicit SimpleContentList(nsINode* aRoot) : mRoot(aRoot) {}

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(SimpleContentList, BaseContentList)

  nsINode* GetParentObject() override { return mRoot; }
  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override;

 protected:
  virtual ~SimpleContentList() = default;

 private:
  // This has to be a strong reference, the root might go away before the list.
  nsCOMPtr<nsINode> mRoot;
};

/**
 * An internal interface for HTMLCollection: a content list that exposes its
 * entries as Elements and supports named lookup.
 */
class HTMLCollection : public BaseContentList {
 public:
  Element* Item(uint32_t aIndex) override = 0;
  // Non-virtual getter for populated mElements, otherwise falls back to Item()
  Element* FastItem(uint32_t aIndex) {
    if (aIndex < mElements.Length()) {
      nsIContent* content = mElements[aIndex];
      MOZ_ASSERT(content->IsElement(),
                 "HTMLCollection can only contain elements");
      MOZ_ASSERT(Item(aIndex) == content,
                 "Subclasses should keep mElements consistent");
      return content->AsElement();
    } else {
      return Item(aIndex);
    }
  }
  Element* IndexedGetter(uint32_t aIndex, bool& aFound) {
    Element* item = FastItem(aIndex);
    aFound = !!item;
    return item;
  }
  Element* NamedItem(const nsAString& aName) {
    bool dummy;
    return NamedGetter(aName, dummy);
  }
  Element* NamedGetter(const nsAString& aName, bool& aFound) {
    return GetFirstNamedElement(aName, aFound);
  }
  virtual Element* GetFirstNamedElement(const nsAString& aName,
                                        bool& aFound) = 0;
  virtual void GetSupportedNames(nsTArray<nsString>& aNames) = 0;

 protected:
  // A reasonable implementation of GetFirstNamedElement, if we know the
  // collection is up-to-date.
  Element* DefaultGetFirstNamedElement(const nsAString& aName, bool& aFound);

  // Used by HTMLAllCollection to limit the elements whose name attribute is
  // considered. The filter MUST NOT cause any flushes.
  using FilterElementWithName = bool (*)(nsIContent*);
  void GetSupportedNames(nsTArray<nsString>& aNames,
                         FilterElementWithName aFilter);
};

// Used for returning lists that will always be empty, such as the applets list
// in HTML Documents
class EmptyContentList final : public HTMLCollection {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(EmptyContentList, HTMLCollection)

  explicit EmptyContentList(nsINode* aRoot) : mRoot(aRoot) {}

  nsINode* GetParentObject() override { return mRoot; }

  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override;

  uint32_t Length() final { return 0; }
  Element* Item(uint32_t aIndex) override { return nullptr; }
  Element* GetFirstNamedElement(const nsAString& aName, bool& aFound) override {
    aFound = false;
    return nullptr;
  }
  void GetSupportedNames(nsTArray<nsString>& aNames) override {}

 protected:
  virtual ~EmptyContentList() = default;

 private:
  // This has to be a strong reference, the root might go away before the list.
  nsCOMPtr<nsINode> mRoot;
};

/**
 * Class that's used as the key to hash ContentList implementations
 * for fast retrieval
 */
struct ContentListKey {
  // We have to take an aIsHTMLDocument arg for two reasons:
  // 1) We don't want to include Document.h in this header.
  // 2) We need to do that to make ContentList::RemoveFromHashtable
  //    work, because by the time it's called the document of the
  //    list's root node might have changed.
  ContentListKey(nsINode* aRootNode, int32_t aMatchNameSpaceId,
                 const nsAString& aTagname, bool aIsHTMLDocument)
      : mRootNode(aRootNode),
        mMatchNameSpaceId(aMatchNameSpaceId),
        mTagname(aTagname),
        mIsHTMLDocument(aIsHTMLDocument),
        mHash(mozilla::AddToHash(mozilla::HashString(aTagname), mRootNode,
                                 mMatchNameSpaceId, mIsHTMLDocument)) {}

  ContentListKey(const ContentListKey& aContentListKey) = default;

  inline uint32_t GetHash(void) const { return mHash; }

  nsINode* const mRootNode;  // Weak ref
  const int32_t mMatchNameSpaceId;
  const nsAString& mTagname;
  bool mIsHTMLDocument;
  const uint32_t mHash;
};

/**
 * Class that implements a possibly live NodeList that matches Elements
 * in the tree based on some criterion.
 */
class ContentList : public HTMLCollection, public nsStubMultiMutationObserver {
 protected:
  enum class State : uint8_t {
    // The list is up to date and need not do any walking to be able to answer
    // any questions anyone may have.
    UpToDate = 0,
    // The list contains no useful information and if anyone asks it anything it
    // will have to populate itself before answering.
    Dirty,
    // The list has populated itself to a certain extent and that that part of
    // the list is still valid.  Requests for things outside that part of the
    // list will require walking the tree some more.  When a list is in this
    // state, the last thing in mElements is the last node in the tree that the
    // list looked at.
    Lazy,
  };

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ContentList, HTMLCollection)

  /**
   * @param aRootNode The node under which to limit our search.
   * @param aMatchAtom An atom whose meaning depends on aMatchNameSpaceId.
   *                   The special value "*" always matches whatever aMatchAtom
   *                   is matched against.
   * @param aMatchNameSpaceId If kNameSpaceID_Unknown, then aMatchAtom is the
   *                          tagName to match.
   *                          If kNameSpaceID_Wildcard, then aMatchAtom is the
   *                          localName to match.
   *                          Otherwise we match nodes whose namespace is
   *                          aMatchNameSpaceId and localName matches
   *                          aMatchAtom.
   * @param aDeep If false, then look only at children of the root, nothing
   *              deeper.  If true, then look at the whole subtree rooted at
   *              our root.
   * @param aLiveList Whether the created list should be a live list observing
   *                  mutations to the DOM tree.
   */
  ContentList(nsINode* aRootNode, int32_t aMatchNameSpaceId,
              nsAtom* aHTMLMatchAtom, nsAtom* aXMLMatchAtom, bool aDeep = true,
              bool aLiveList = true);

  /**
   * @param aRootNode The node under which to limit our search.
   * @param aFunc the function to be called to determine whether we match.
   *              This function MUST NOT ever cause mutation of the DOM.
   *              The ContentList implementation guarantees that everything
   *              passed to the function will be IsElement().
   * @param aDestroyFunc the function that will be called to destroy aData
   * @param aData closure data that will need to be passed back to aFunc
   * @param aDeep If false, then look only at children of the root, nothing
   *              deeper.  If true, then look at the whole subtree rooted at
   *              our root.
   * @param aMatchAtom an atom to be passed back to aFunc
   * @param aMatchNameSpaceId a namespace id to be passed back to aFunc
   * @param aFuncMayDependOnAttr a boolean that indicates whether this list is
   *                             sensitive to attribute changes.
   * @param aLiveList Whether the created list should be a live list observing
   *                  mutations to the DOM tree.
   */
  ContentList(nsINode* aRootNode, nsContentListMatchFunc aFunc,
              nsContentListDestroyFunc aDestroyFunc, void* aData,
              bool aDeep = true, nsAtom* aMatchAtom = nullptr,
              int32_t aMatchNameSpaceId = kNameSpaceID_None,
              bool aFuncMayDependOnAttr = true, bool aLiveList = true);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

 protected:
  virtual ~ContentList();

 public:
  // BaseContentList overrides
  int32_t IndexOf(nsIContent* aContent) override;
  nsINode* GetParentObject() override { return mRootNode; }

  uint32_t Length() final;
  Element* Item(uint32_t aIndex) final;
  Element* GetFirstNamedElement(const nsAString& aName, bool& aFound) override;
  void GetSupportedNames(nsTArray<nsString>& aNames) override {
    GetSupportedNames(aNames, nullptr);
  }

  void GetSupportedNames(nsTArray<nsString>& aNames,
                         FilterElementWithName aFilter) {
    BringSelfUpToDate();
    HTMLCollection::GetSupportedNames(aNames, aFilter);
  }

  // nsIMutationObserver
  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED

  bool MatchesKey(const ContentListKey& aKey) const {
    // The root node is most commonly the same: the document.  And the
    // most common namespace id is kNameSpaceID_Unknown.  So check the
    // string first.  Cases in which whether our root's ownerDocument
    // is HTML changes are extremely rare, so check those last.
    MOZ_ASSERT(mXMLMatchAtom,
               "How did we get here with a null match atom on our list?");
    return mXMLMatchAtom->Equals(aKey.mTagname) &&
           mRootNode == aKey.mRootNode &&
           mMatchNameSpaceId == aKey.mMatchNameSpaceId &&
           mIsHTMLDocument == aKey.mIsHTMLDocument;
  }

  /**
   * Sets the state to LIST_DIRTY and clears mElements array.
   * @note This is the only acceptable way to set state to LIST_DIRTY.
   */
  void SetDirty() {
    mState = State::Dirty;
    InvalidateNamedItemsCache();
    Reset();
    SetEnabledCallbacks(nsIMutationObserver::kNodeWillBeDestroyed);
  }

  void LastRelease() override;

  class HashEntry;

 protected:
  /**
   * Stop observing the root nodes and release mRootNode.
   */
  virtual void DisconnectFromRoot();

  // A cache from name to the first named item in mElements. Only possibly
  // non-null when mState is State::UpToDate. Elements are kept alive by our
  // mElements array.
  using NamedItemsCache = nsTHashMap<nsAtomHashKey, Element*>;

  void InvalidateNamedItemsCache() {
    mNamedItemsCache = nullptr;
    mNamedItemsCacheValid = false;
  }

  inline void InsertElementInNamedItemsCache(nsIContent&);
  inline void InvalidateNamedItemsCacheForAttributeChange(int32_t aNameSpaceID,
                                                          nsAtom* aAttribute);
  inline void InvalidateNamedItemsCacheForInsertion(Element&);
  inline void InvalidateNamedItemsCacheForDeletion(Element&);

  void EnsureNamedItemsCacheValid();

  /**
   * Returns whether the element matches our criterion
   *
   * @param  aElement the element to attempt to match
   * @return whether we match
   */
  bool Match(Element* aElement);

  enum class MatchSelfMode : uint8_t {
    Insertion,
    // Also accept SVG elements that may have a class. SMIL can change their
    // class without notification, so they can be in the list without
    // matching now. Needed when aContent leaves the subtree.
    Removal,
  };

  /**
   * See if anything in the subtree rooted at aContent, including
   * aContent itself, matches our criterion.
   *
   * @param  aContent the root of the subtree to match against
   * @return whether we match something in the tree rooted at aContent
   */
  template <MatchSelfMode Mode>
  bool MatchSelf(nsIContent* aContent);

  virtual nsINode* GetNextNode(nsINode* aCurrent);

  /**
   * Populate our list.  Stop once we have at least aNeededLength
   * elements.  At the end of PopulateSelf running, either the last
   * node we examined is the last node in our array or we have
   * traversed the whole document (or both).
   *
   * @param aNeededLength the length the list should have when we are
   *        done (unless it exhausts the document)
   */
  virtual void PopulateSelf(uint32_t aNeededLength);

  /**
   * @param  aContainer a content node which must be a descendant of
   *         mRootNode
   * @return true if children or descendants of aContainer could match our
   *                 criterion.
   *         false otherwise.
   */
  bool MayContainRelevantNodes(nsINode* aContainer) {
    return mDeep || aContainer == mRootNode;
  }

  /**
   * Remove ourselves from the hashtable that caches commonly accessed
   * content lists.  Generally done on destruction.
   */
  void RemoveFromHashtable();
  /**
   * If state is not LIST_UP_TO_DATE, fully populate ourselves with
   * all the nodes we can find.
   */
  void BringSelfUpToDate();

  /**
   * To be called from non-destructor locations that want to remove from caches.
   * Needed because if subclasses want to have cache behavior they can't just
   * override RemoveFromHashtable(), since we call that in our destructor.
   */
  void RemoveFromCaches() override { RemoveFromHashtable(); }

  void MaybeMarkDirty() {
    if (mState != State::Dirty && ++mMissedUpdates > 128) {
      mMissedUpdates = 0;
      SetDirty();
    }
  }

  RefPtr<nsINode> mRootNode;
  int32_t mMatchNameSpaceId;
  RefPtr<nsAtom> mHTMLMatchAtom;
  RefPtr<nsAtom> mXMLMatchAtom;

  /**
   * Function to use to determine whether a piece of content matches
   * our criterion
   */
  nsContentListMatchFunc mFunc = nullptr;
  /**
   * Cleanup closure data with this.
   */
  nsContentListDestroyFunc mDestroyFunc = nullptr;
  /**
   * Closure data to pass to mFunc when we call it
   */
  void* mData = nullptr;

  mozilla::UniquePtr<NamedItemsCache> mNamedItemsCache;

  uint8_t mMissedUpdates = 0;

  // The current state of the list.
  State mState;

  /**
   * True if we are looking for elements named "*"
   */
  bool mMatchAll : 1;
  /**
   * Whether to actually descend the tree.  If this is false, we won't
   * consider grandkids of mRootNode.
   */
  bool mDeep : 1;
  /**
   * Whether the return value of mFunc could depend on the values of
   * attributes.
   */
  bool mFuncMayDependOnAttr : 1;
  /**
   * Whether the ownerDocument of our root node at list creation time was an
   * HTML document.  Only needed when we're doing a namespace/atom match, not
   * when doing function matching, always false otherwise.
   */
  bool mIsHTMLDocument : 1;
  /**
   * True mNamedItemsCache is valid. Note mNamedItemsCache might still be null
   * if there's no named items at all.
   */
  bool mNamedItemsCacheValid : 1;
  /**
   * Whether the list observes mutations to the DOM tree.
   */
  const bool mIsLiveList : 1;
  /*
   * True if this content list is cached in a hash table.
   * For ContentList (but not its subclasses), the hash table is
   * gContentListHashTable.
   * For CacheableFuncStringContentList, the hash table is
   * gFuncStringContentListHashTable.
   * Other subclasses of ContentList can't be in hash tables.
   */
  bool mInHashtable : 1;

#ifdef DEBUG_CONTENT_LIST
  void AssertInSync();
#endif
};

/**
 * A class of cacheable content list; cached on the combination of aRootNode +
 * aFunc + aDataString
 */
class CacheableFuncStringContentList;

class MOZ_STACK_CLASS FuncStringCacheKey {
 public:
  FuncStringCacheKey(nsINode* aRootNode, nsContentListMatchFunc aFunc,
                     const nsAString& aString)
      : mRootNode(aRootNode), mFunc(aFunc), mString(aString) {}

  uint32_t GetHash(void) const {
    uint32_t hash = mozilla::HashString(mString);
    return mozilla::AddToHash(hash, mRootNode, mFunc);
  }

 private:
  friend class CacheableFuncStringContentList;

  nsINode* const mRootNode;
  const nsContentListMatchFunc mFunc;
  const nsAString& mString;
};

// aDestroyFunc is allowed to be null
// aDataAllocator must always return a non-null pointer
class CacheableFuncStringContentList : public ContentList {
 public:
  virtual ~CacheableFuncStringContentList();

  bool Equals(const FuncStringCacheKey* aKey) {
    return mRootNode == aKey->mRootNode && mFunc == aKey->mFunc &&
           mString == aKey->mString;
  }

  enum ContentListType { eNodeList, eHTMLCollection };
#ifdef DEBUG
  ContentListType mType;
#endif

  class HashEntry;

 protected:
  CacheableFuncStringContentList(
      nsINode* aRootNode, nsContentListMatchFunc aFunc,
      nsContentListDestroyFunc aDestroyFunc,
      nsFuncStringContentListDataAllocator aDataAllocator,
      const nsAString& aString, mozilla::DebugOnly<ContentListType> aType)
      : ContentList(aRootNode, aFunc, aDestroyFunc, nullptr),
#ifdef DEBUG
        mType(aType),
#endif
        mString(aString) {
    mData = (*aDataAllocator)(aRootNode, &mString);
    MOZ_ASSERT(mData);
  }

  void RemoveFromCaches() override { RemoveFromFuncStringHashtable(); }
  void RemoveFromFuncStringHashtable();

  nsString mString;
};

class CachableElementsByNameNodeList : public CacheableFuncStringContentList {
 public:
  CachableElementsByNameNodeList(
      nsINode* aRootNode, nsContentListMatchFunc aFunc,
      nsContentListDestroyFunc aDestroyFunc,
      nsFuncStringContentListDataAllocator aDataAllocator,
      const nsAString& aString)
      : CacheableFuncStringContentList(aRootNode, aFunc, aDestroyFunc,
                                       aDataAllocator, aString, eNodeList) {}

  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED

  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override;

#ifdef DEBUG
  static const ContentListType sType;
#endif
};

class CacheableFuncStringHTMLCollection
    : public CacheableFuncStringContentList {
 public:
  CacheableFuncStringHTMLCollection(
      nsINode* aRootNode, nsContentListMatchFunc aFunc,
      nsContentListDestroyFunc aDestroyFunc,
      nsFuncStringContentListDataAllocator aDataAllocator,
      const nsAString& aString)
      : CacheableFuncStringContentList(aRootNode, aFunc, aDestroyFunc,
                                       aDataAllocator, aString,
                                       eHTMLCollection) {}

  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override;

#ifdef DEBUG
  static const ContentListType sType;
#endif
};

class LabelsNodeList final : public ContentList {
 public:
  LabelsNodeList(nsGenericHTMLElement* aLabeledElement, nsINode* aSubtreeRoot,
                 nsContentListMatchFunc aMatchFunc,
                 nsContentListDestroyFunc aDestroyFunc);

  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED
  NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED

  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override;

  /**
   * Reset roots, mutation observers and reference target observers, and clear
   * content list if the roots have changed.
   */
  void ResetRoots();

 protected:
  virtual ~LabelsNodeList();

  void DisconnectFromRoot() override;

  nsINode* GetNextNode(nsINode* aCurrent) override;

 private:
  /**
   * Start searching at the last one if we already have nodes, otherwise
   * start searching at the root.
   *
   * @param aNeededLength The list of length should have when we are
   *                      done (unless it exhausts the document).
   */
  void PopulateSelf(uint32_t aNeededLength) override;

  bool NodeIsInScope(nsINode* aNode);

  static bool ResetRootsCallback(void* aData);

  void WatchLabeledDescendantsOfNearestAncestorLabel(Element* labeledHost);

  /**
   * An array of all relevant subtree roots for the labeled element.
   *
   * A labeled element's labels may include nodes from multiple roots, since
   * each shadow root may have a reference target allowing labels to refer to an
   * element within the shadow root, potentially recusively.
   *
   * This structure is populated by walking up from the labeled element,
   * adding each subtree root in turn and walking out to the next one if the
   * labeled element or the host of the previous root is the reference target of
   * its subtree root.
   *
   * The last element in this array must always be the same as mRootNode.
   */
  nsTArray<nsINode*> mRoots;
};

}  // namespace mozilla::dom
#endif  // mozilla_dom_ContentList_h_
