/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_DOMSVGPOINTLIST_H_
#define DOM_SVG_DOMSVGPOINTLIST_H_

#include "mozAutoDocUpdate.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDebug.h"
#include "nsTArray.h"
#include "SVGPointList.h"  // IWYU pragma: keep
#include "mozilla/Attributes.h"
#include "mozilla/RefPtr.h"

// {61812ad1-c078-4cd1-87e6-bc1c1b8d7284}
#define MOZILLA_DOMSVGPOINTLIST_IID \
  {0x61812ad1, 0xc078, 0x4cd1, {0x87, 0xe6, 0xbc, 0x1c, 0x1b, 0x8d, 0x72, 0x84}}

namespace mozilla {

class ErrorResult;
class SVGAnimatedPointList;

namespace dom {

class DOMSVGPoint;
class SVGElement;
class SVGPolyElement;

//----------------------------------------------------------------------
// Helper class: AutoChangePointListNotifier
// Stack-based helper class to pair calls to WillChangePointList and
// DidChangePointList. Used by DOMSVGPoint and DOMSVGPointList.
template <class T>
class MOZ_RAII AutoChangePointListNotifier {
 public:
  explicit AutoChangePointListNotifier(T* aValue) : mValue(aValue) {
    MOZ_ASSERT(mValue, "Expecting non-null value");
    if (mValue->IsInList()) {
      mUpdateBatch.emplace(mValue->Element()->GetComposedDoc(), true);
      mEmptyOrOldValue =
          mValue->Element()->WillChangePointList(mUpdateBatch.ref());
    }
  }

  ~AutoChangePointListNotifier() {
    if (mValue->IsInList()) {
      mValue->Element()->DidChangePointList(mEmptyOrOldValue,
                                            mUpdateBatch.ref());
      if (mValue->AttrIsAnimating()) {
        mValue->Element()->AnimationNeedsResample();
      }
    }
  }

 private:
  Maybe<mozAutoDocUpdate> mUpdateBatch;
  T* const mValue;
  nsAttrValue mEmptyOrOldValue;
};

/**
 * Class DOMSVGPointList
 *
 * This class is used to create the DOM tearoff objects that wrap internal
 * SVGPointList objects.
 *
 * See the architecture comment in DOMSVGAnimatedLengthList.h first (that's
 * LENGTH list), then continue reading the remainder of this comment.
 *
 * The architecture of this class is very similar to that of DOMSVGLengthList
 * except that, since there is no nsIDOMSVGAnimatedPointList interface
 * in SVG, we have no parent DOMSVGAnimatedPointList (unlike DOMSVGLengthList
 * which has a parent DOMSVGAnimatedLengthList class). (There is an
 * SVGAnimatedPoints interface, but that is quite different to
 * DOMSVGAnimatedLengthList, since it is inherited by elements rather than
 * elements having members of that type.) As a consequence, much of the logic
 * that would otherwise be in DOMSVGAnimatedPointList (and is in
 * DOMSVGAnimatedLengthList) is contained in this class.
 *
 * This class is strongly intertwined with DOMSVGPoint. Our DOMSVGPoint
 * items are friends of us and responsible for nulling out our pointers to
 * them when they die.
 *
 * Our DOM items are created lazily on demand as and when script requests them.
 */
class DOMSVGPointList final : public nsISupports, public nsWrapperCache {
  template <class T>
  friend class AutoChangePointListNotifier;
  friend class DOMSVGPoint;

 public:
  NS_DECLARE_STATIC_IID_ACCESSOR(MOZILLA_DOMSVGPOINTLIST_IID)
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(DOMSVGPointList)

  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override;

  nsISupports* GetParentObject() { return static_cast<nsIContent*>(mElement); }

  /**
   * Factory method to create and return a DOMSVGPointList wrapper
   * for a given internal SVGPointList object. The factory takes care
   * of caching the object that it returns so that the same object can be
   * returned for the given SVGPointList each time it is requested.
   * The cached object is only removed from the cache when it is destroyed due
   * to there being no more references to it or to any of its descendant
   * objects. If that happens, any subsequent call requesting the DOM wrapper
   * for the SVGPointList will naturally result in a new
   * DOMSVGPointList being returned.
   *
   * It's unfortunate that aList is a void* instead of a typed argument. This
   * is because the mBaseVal and mAnimVal members of SVGAnimatedPointList are
   * of different types - a plain SVGPointList, and a SVGPointList*. We
   * use the addresses of these members as the key for the hash table, and
   * clearly SVGPointList* and a SVGPointList** are not the same type.
   */
  static already_AddRefed<DOMSVGPointList> GetDOMWrapper(
      void* aList, dom::SVGPolyElement* aElement);

  /**
   * This method returns the DOMSVGPointList wrapper for an internal
   * SVGPointList object if it currently has a wrapper. If it does
   * not, then nullptr is returned.
   */
  static DOMSVGPointList* GetDOMWrapperIfExists(void* aList);

  /**
   * This will normally be the same as InternalList().Length(), except if
   * we've hit OOM, in which case our length will be zero.
   */
  uint32_t LengthNoFlush() const {
    MOZ_ASSERT(
        mItems.Length() == 0 || mItems.Length() == InternalList().Length(),
        "DOM wrapper's list length is out of sync");
    return mItems.Length();
  }

  /**
   * WATCH OUT! If you add code to call this on a baseVal wrapper, then you
   * must also call it on the animVal wrapper too if necessary!! See other
   * callers!
   *
   * Called by internal code to notify us when we need to sync the length of
   * this DOM list with its internal list. This is called immediately prior to
   * the length of the internal list being changed so that any DOM list items
   * that need to be removed from the DOM list can first copy their values from
   * their internal counterpart.
   *
   * The only time this method could fail is on OOM when trying to increase the
   * length of the DOM list. If that happens then this method simply clears the
   * list and returns. Callers just proceed as normal, and we simply accept
   * that the DOM list will be empty (until successfully set to a new value).
   */
  void InternalListWillChangeTo(const SVGPointList& aNewValue);

  /*
   * We need this so that templates that work on lists and elements can check
   * ownership where elements may be not be in a list.
   */
  bool IsInList() const { return true; }

  /**
   * Returns true if our attribute is animating (in which case our animVal is
   * not simply a mirror of our baseVal).
   */
  bool AttrIsAnimating() const;

  /**
   * Returns true if there is an animated list mirroring the base list.
   */
  bool AnimListMirrorsBaseList() const;

  uint32_t NumberOfItems() const {
    if (IsAnimValList()) {
      Element()->FlushAnimations();
    }
    return LengthNoFlush();
  }
  void Clear(ErrorResult& aRv);
  already_AddRefed<DOMSVGPoint> Initialize(DOMSVGPoint& aNewItem,
                                           ErrorResult& aRv);
  already_AddRefed<DOMSVGPoint> GetItem(uint32_t index, ErrorResult& error);
  already_AddRefed<DOMSVGPoint> IndexedGetter(uint32_t index, bool& found,
                                              ErrorResult& error);
  already_AddRefed<DOMSVGPoint> InsertItemBefore(DOMSVGPoint& aNewItem,
                                                 uint32_t aIndex,
                                                 ErrorResult& aRv);
  already_AddRefed<DOMSVGPoint> ReplaceItem(DOMSVGPoint& aNewItem,
                                            uint32_t aIndex, ErrorResult& aRv);
  already_AddRefed<DOMSVGPoint> RemoveItem(uint32_t aIndex, ErrorResult& aRv);
  already_AddRefed<DOMSVGPoint> AppendItem(DOMSVGPoint& aNewItem,
                                           ErrorResult& aRv) {
    return InsertItemBefore(aNewItem, LengthNoFlush(), aRv);
  }
  uint32_t Length() const { return NumberOfItems(); }

 private:
  /**
   * Only our static GetDOMWrapper() factory method may create objects of our
   * type.
   */
  DOMSVGPointList(dom::SVGElement* aElement, bool aIsAnimValList)
      : mElement(aElement), mIsAnimValList(aIsAnimValList) {
    InternalListWillChangeTo(InternalList());  // Sync mItems
  }

  ~DOMSVGPointList();

  dom::SVGElement* Element() const { return mElement.get(); }

  /// Used to determine if this list is the baseVal or animVal list.
  bool IsAnimValList() const { return mIsAnimValList; }

  /**
   * Get a reference to this object's corresponding internal SVGPointList.
   *
   * To simplify the code we just have this one method for obtaining both
   * base val and anim val internal lists. This means that anim val lists don't
   * get const protection, but our setter methods guard against changing
   * anim val lists.
   */
  SVGPointList& InternalList() const;

  SVGAnimatedPointList& InternalAList() const;

  /// Returns the DOMSVGPoint at aIndex, creating it if necessary.
  already_AddRefed<DOMSVGPoint> GetItemAt(uint32_t aIndex);

  void MaybeInsertNullInAnimValListAt(uint32_t aIndex);
  void MaybeRemoveItemFromAnimValListAt(uint32_t aIndex);

  void RemoveFromTearoffTable();

  // Weak refs to our DOMSVGPoint items. The items are friends and take care
  // of clearing our pointer to them when they die.
  FallibleTArray<DOMSVGPoint*> mItems;

  // Strong ref to our element to keep it alive. We hold this not only for
  // ourself, but also for our DOMSVGPoint items too.
  RefPtr<dom::SVGElement> mElement;

  bool mIsAnimValList;

  // Tracks whether we're in the tearoff table. Initialized to true, since all
  // new instances are added to the table right after construction. Updated to
  // false when we're removed from the table (at which point we're being
  // destructed or soon-to-be destructed).
  bool mIsInTearoffTable = true;
};

NS_DEFINE_STATIC_IID_ACCESSOR(DOMSVGPointList, MOZILLA_DOMSVGPOINTLIST_IID)

}  // namespace dom
}  // namespace mozilla

#endif  // DOM_SVG_DOMSVGPOINTLIST_H_
