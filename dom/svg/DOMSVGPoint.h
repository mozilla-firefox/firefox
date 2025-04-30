/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_DOMSVGPOINT_H_
#define DOM_SVG_DOMSVGPOINT_H_

#include "DOMSVGPointList.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDebug.h"
#include "SVGPoint.h"
#include "nsWrapperCache.h"
#include "mozilla/Attributes.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/dom/SVGSVGElement.h"
#include "mozilla/gfx/2D.h"

#define MOZ_SVG_LIST_INDEX_BIT_COUNT 29

namespace mozilla::dom {
struct DOMMatrix2DInit;

/**
 * Class DOMSVGPoint
 *
 * This class creates the DOM objects that wrap internal SVGPoint objects that
 * are in an SVGPointList. It is also used to create the objects returned by
 * SVGSVGElement.createSVGPoint() and other functions that return DOM SVGPoint
 * objects.
 *
 * See the architecture comment in DOMSVGPointList.h for an overview of the
 * important points regarding these DOM wrapper structures.
 *
 * See the architecture comment in DOMSVGLength.h (yes, LENGTH) for an overview
 * of the important points regarding how this specific class works.
 */
class DOMSVGPoint final : public nsWrapperCache {
  template <class T>
  friend class AutoChangePointListNotifier;

  using Point = gfx::Point;

 public:
  /**
   * Generic ctor for DOMSVGPoint objects that are created for an attribute.
   */
  DOMSVGPoint(DOMSVGPointList* aList, uint32_t aListIndex, bool aIsAnimValItem)
      : mVal(nullptr),
        mOwner(aList),
        mListIndex(aListIndex),
        mIsAnimValItem(aIsAnimValItem),
        mIsTranslatePoint(false),
        mIsInTearoffTable(false) {
    // These shifts are in sync with the members.
    MOZ_ASSERT(aList && aListIndex <= MaxListIndex(), "bad arg");

    MOZ_ASSERT(IndexIsValid(), "Bad index for DOMSVGPoint!");
  }

  // Constructor for unowned points and SVGSVGElement.createSVGPoint
  explicit DOMSVGPoint(const Point& aPt)
      : mListIndex(0),
        mIsAnimValItem(false),
        mIsTranslatePoint(false),
        mIsInTearoffTable(false) {
    // In this case we own mVal
    mVal = new SVGPoint(aPt.x, aPt.y);
  }

 private:
  // The translate of an SVGSVGElement
  DOMSVGPoint(SVGPoint* aPt, SVGSVGElement* aSVGSVGElement)
      : mVal(aPt),
        mOwner(ToSupports(aSVGSVGElement)),
        mListIndex(0),
        mIsAnimValItem(false),
        mIsTranslatePoint(true),
        mIsInTearoffTable(false) {}

  virtual ~DOMSVGPoint() { CleanupWeakRefs(); }

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(DOMSVGPoint)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(DOMSVGPoint)

  static already_AddRefed<DOMSVGPoint> GetTranslateTearOff(
      SVGPoint* aVal, SVGSVGElement* aSVGSVGElement);

  bool IsInList() const { return HasOwner() && !IsTranslatePoint(); }

  /**
   * "Owner" here means that the instance has an
   * internal counterpart from which it gets its values. (A better name may
   * be HasWrappee().)
   */
  bool HasOwner() const { return !!mOwner; }

  bool IsTranslatePoint() const { return mIsTranslatePoint; }

  void DidChangeTranslate();

  /**
   * This method is called to notify this DOM object that it is being inserted
   * into a list, and give it the information it needs as a result.
   *
   * This object MUST NOT already belong to a list when this method is called.
   * That's not to say that script can't move these DOM objects between
   * lists - it can - it's just that the logic to handle that (and send out
   * the necessary notifications) is located elsewhere (in DOMSVGPointList).)
   */
  void InsertingIntoList(DOMSVGPointList* aList, uint32_t aListIndex,
                         bool aIsAnimValItem);

  static uint32_t MaxListIndex() {
    return (1U << MOZ_SVG_LIST_INDEX_BIT_COUNT) - 1;
  }

  /// This method is called to notify this object that its list index changed.
  void UpdateListIndex(uint32_t aListIndex) { mListIndex = aListIndex; }

  /**
   * This method is called to notify this DOM object that it is about to be
   * removed from its current DOM list so that it can first make a copy of its
   * internal counterpart's values. (If it didn't do this, then it would
   * "lose" its value on being removed.)
   */
  void RemovingFromList();

  SVGPoint ToSVGPoint() { return InternalItem(); }

  // WebIDL
  float X();
  void SetX(float aX, ErrorResult& rv);
  float Y();
  void SetY(float aY, ErrorResult& rv);
  already_AddRefed<DOMSVGPoint> MatrixTransform(const DOMMatrix2DInit& aMatrix,
                                                ErrorResult& aRv);

  nsISupports* GetParentObject() { return Element(); }

  /**
   * Returns true if our attribute is animating (in which case our animVal is
   * not simply a mirror of our baseVal).
   */
  bool AttrIsAnimating() const;

  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override;

  DOMSVGPoint* Copy() { return new DOMSVGPoint(InternalItem()); }

 private:
#ifdef DEBUG
  bool IndexIsValid();
#endif

  SVGElement* Element();

  /**
   * Clears soon-to-be-invalid weak references in external objects that were
   * set up during the creation of this object. This should be called during
   * destruction and during cycle collection.
   */
  void CleanupWeakRefs();

  /**
   * Get a reference to the internal SVGPoint list item that this DOM wrapper
   * object currently wraps.
   */
  SVGPoint& InternalItem();

  SVGPoint* mVal;              // If mIsTranslatePoint is true, the element owns
                               // the value. Otherwise we do.
  RefPtr<nsISupports> mOwner;  // If mIsTranslatePoint is true, this is an
                               // SVGSVGElement, if we're unowned it's null, or
                               // we're in a list and it's a DOMSVGPointList

  // Bounds for the following are checked in the ctor, so be sure to update
  // that if you change the capacity of any of the following.

  uint32_t mListIndex : MOZ_SVG_LIST_INDEX_BIT_COUNT;
  uint32_t mIsAnimValItem : 1;     // True if We're the animated value of a list
  uint32_t mIsTranslatePoint : 1;  // true iff our owner is a SVGSVGElement

  // Tracks whether we're in the tearoff table. Initialized to false in the
  // ctor, but then immediately set to true if/when we're added to the table
  // (not all instances are).  Updated to false when we're removed from the
  // table (at which point we're being destructed or soon-to-be destructed).
  uint32_t mIsInTearoffTable : 1;
};

}  // namespace mozilla::dom

#undef MOZ_SVG_LIST_INDEX_BIT_COUNT

#endif  // DOM_SVG_DOMSVGPOINT_H_
