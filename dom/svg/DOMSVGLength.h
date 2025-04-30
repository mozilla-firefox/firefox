/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_DOMSVGLENGTH_H_
#define DOM_SVG_DOMSVGLENGTH_H_

#include "DOMSVGLengthList.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDebug.h"
#include "nsTArray.h"
#include "SVGLength.h"
#include "mozilla/Attributes.h"
#include "nsWrapperCache.h"

#define MOZ_SVG_LIST_INDEX_BIT_COUNT 21  // supports > 2 million list items

namespace mozilla {

class ErrorResult;

namespace dom {
class SVGElement;

/**
 * Class DOMSVGLength
 *
 * This class creates the DOM objects that wrap internal SVGLength objects that
 * are in an SVGLengthList. It is also used to create the objects returned by
 * SVGSVGElement.createSVGLength().
 *
 * For the DOM wrapper classes for non-list SVGLength, see SVGAnimatedLength.h.
 *
 * See the architecture comment in DOMSVGAnimatedLengthList.h.
 *
 * This class is strongly intertwined with DOMSVGAnimatedLengthList and
 * DOMSVGLengthList. We are a friend of DOMSVGLengthList, and are responsible
 * for nulling out our DOMSVGLengthList's pointer to us when we die, making it
 * a real weak pointer.
 *
 * When objects of this type are in a DOMSVGLengthList they belong to an
 * attribute. While they belong to an attribute, the objects' values come from
 * their corresponding internal SVGLength objects in the internal SVGLengthList
 * objects for the attribute. Getting and setting values of a DOMSVGLength
 * requires reading and writing to its internal SVGLength. However, if the
 * DOMSVGLength is detached from its DOMSVGLengthList then it first makes a
 * copy of its internal SVGLength's value and unit so that it doesn't appear to
 * "lose" its value from script's perspective on being removed from the list.
 * This means that these DOM tearoffs have space to store these values, even
 * though they're not used in the common case.
 *
 * Objects of this type are also used to reflect the baseVal and animVal of
 * a single, non-list SVGLength attribute. Getting and settings values of the
 * DOMSVGLength in this case requires reading and writing to the corresponding
 * SVGAnimatedLength object.
 *
 * This class also stores its current list index, attribute enum, and whether
 * it belongs to a baseVal or animVal list. This is so that objects of this
 * type can find their corresponding internal SVGLength.
 *
 * To use these classes for <length> attributes as well as <list-of-length>
 * attributes, we would need to take a bit from mListIndex and use that to
 * indicate whether the object belongs to a list or non-list attribute, then
 * if-else as appropriate. The bug for doing that work is:
 * https://bugzilla.mozilla.org/show_bug.cgi?id=571734
 */
class DOMSVGLength final : public nsWrapperCache {
  template <class T>
  friend class AutoChangeLengthListNotifier;

  /**
   * Ctor for creating the object returned by
   * SVGAnimatedLength::ToDOMBaseVal/ToDOMAnimVal
   */
  DOMSVGLength(SVGAnimatedLength* aVal, dom::SVGElement* aSVGElement,
               bool aAnimVal);

  ~DOMSVGLength() { CleanupWeakRefs(); }

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(DOMSVGLength)
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_NATIVE_CLASS(DOMSVGLength)

  /**
   * Generic ctor for DOMSVGLength objects that are created for an attribute.
   */
  DOMSVGLength(DOMSVGLengthList* aList, uint8_t aAttrEnum, uint32_t aListIndex,
               bool aIsAnimValItem);

  /**
   * Ctor for creating the objects returned by SVGSVGElement.createSVGLength(),
   * which do not initially belong to an attribute.
   */
  DOMSVGLength();

  static already_AddRefed<DOMSVGLength> GetTearOff(SVGAnimatedLength* aVal,
                                                   dom::SVGElement* aSVGElement,
                                                   bool aAnimVal);

  /**
   * Create an unowned copy of a length that is owned or is reflecting a single
   * attribute. The caller is responsible for the first AddRef().
   */
  DOMSVGLength* Copy();

  /**
   * Returns true if our attribute is animating.
   */
  bool IsAnimating() const;

  /**
   * In future, if this class is used for non-list lengths, this will be
   * different to IsInList().
   */
  bool HasOwner() const { return !!mOwner; }

  /**
   * This method is called to notify this DOM object that it is being inserted
   * into a list, and give it the information it needs as a result.
   *
   * This object MUST NOT already belong to a list when this method is called.
   * That's not to say that script can't move these DOM objects between
   * lists - it can - it's just that the logic to handle that (and send out
   * the necessary notifications) is located elsewhere (in DOMSVGLengthList).)
   */
  void InsertingIntoList(DOMSVGLengthList* aList, uint8_t aAttrEnum,
                         uint32_t aListIndex, bool aIsAnimValItem);

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

  SVGLength ToSVGLength();

  // WebIDL
  uint16_t UnitType();
  float GetValue(ErrorResult& aRv);
  void SetValue(float aUserUnitValue, ErrorResult& aRv);
  float ValueInSpecifiedUnits();
  void SetValueInSpecifiedUnits(float aValue, ErrorResult& aRv);
  void GetValueAsString(nsAString& aValue);
  void SetValueAsString(const nsAString& aValue, ErrorResult& aRv);
  void NewValueSpecifiedUnits(uint16_t aUnit, float aValue, ErrorResult& aRv);
  void ConvertToSpecifiedUnits(uint16_t aUnit, ErrorResult& aRv);

  nsISupports* GetParentObject() { return mOwner; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

 private:
  dom::SVGElement* Element();

  uint8_t AttrEnum() const { return mAttrEnum; }

  /**
   * Get a reference to the internal SVGLength list item that this DOM wrapper
   * object currently wraps.
   *
   * To simplify the code we just have this one method for obtaining both
   * baseVal and animVal internal items. This means that animVal items don't
   * get const protection, but then our setter methods guard against changing
   * animVal items.
   */
  SVGLength& InternalItem();

  /**
   * Some values have units that depend on style or layout so we may need to
   * flush to ensure we get or set the right value in pixels.
   */
  void FlushIfNeeded();

#ifdef DEBUG
  bool IndexIsValid();
#endif

  /**
   * Clears soon-to-be-invalid weak references in external objects that were
   * set up during the creation of this object. This should be called during
   * destruction and during cycle collection.
   */
  void CleanupWeakRefs();

  RefPtr<nsISupports> mOwner;  // Either a DOMSVGLengthList if we're in a list,
                               // an SVGElement if we're an attribute or null

  // Bounds for the following are checked in the ctor, so be sure to update
  // that if you change the capacity of any of the following.

  uint32_t mListIndex : MOZ_SVG_LIST_INDEX_BIT_COUNT;
  uint32_t mAttrEnum : 4;  // supports up to 16 attributes
  uint32_t mIsAnimValItem : 1;

  // Tracks whether we're in the tearoff table. Initialized to false in the
  // ctor, but then immediately set to true after we're added to the table
  // (unless we're an instance created via 'Copy()'; those never get added to
  // the table).  Updated to false when we're removed from the table (at which
  // point we're being destructed or soon-to-be destructed).
  uint32_t mIsInTearoffTable : 1;

  // The following members are only used when we're not in a list:
  uint32_t mUnit : 5;  // can handle 31 units (the 10 SVG 1.1 units + rem, vw,
                       // vh, wm, calc + future additions)
  float mValue = 0.0f;
};

}  // namespace dom
}  // namespace mozilla

#undef MOZ_SVG_LIST_INDEX_BIT_COUNT

#endif  // DOM_SVG_DOMSVGLENGTH_H_
