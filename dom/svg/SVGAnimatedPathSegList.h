/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_SVGANIMATEDPATHSEGLIST_H_
#define DOM_SVG_SVGANIMATEDPATHSEGLIST_H_

#include "mozilla/Attributes.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/SMILAttr.h"
#include "mozilla/UniquePtr.h"
#include "SVGPathData.h"

namespace mozilla {

class SMILValue;

namespace dom {
class SVGAnimationElement;
class SVGElement;
class SVGPathSegment;
struct SVGPathSegmentInit;
}  // namespace dom

/**
 * Class SVGAnimatedPathSegList
 *
 * Despite the fact that no SVGAnimatedPathSegList interface or objects exist
 * in the SVG specification (unlike e.g. SVGAnimated*Length*List), we
 * nevertheless have this internal class. (Note that there is an
 * SVGAnimatedPathData interface, but that's quite different to
 * SVGAnimatedLengthList since it is inherited by elements, as opposed to
 * elements having members of that type.) The reason that we have this class is
 * to provide a single locked down point of entry to the SVGPathData objects,
 * which helps ensure that the DOM wrappers for SVGPathData objects' are always
 * kept in sync. This is vitally important (see the comment in
 * DOMSVGPathSegList::InternalListWillChangeTo) and frees consumers from having
 * to know or worry about wrappers (or forget about them!) for the most part.
 */
class SVGAnimatedPathSegList final {
 public:
  SVGAnimatedPathSegList() = default;

  SVGAnimatedPathSegList& operator=(const SVGAnimatedPathSegList& aOther) {
    mBaseVal = aOther.mBaseVal;
    if (aOther.mAnimVal) {
      mAnimVal = MakeUnique<SVGPathData>(*aOther.mAnimVal);
    }
    return *this;
  }

  /**
   * Because it's so important that mBaseVal and its DOMSVGPathSegList wrapper
   * (if any) be kept in sync (see the comment in
   * DOMSVGPathSegList::InternalListWillChangeTo), this method returns a const
   * reference. Only our friend classes may get mutable references to mBaseVal.
   */
  const SVGPathData& GetBaseValue() const { return mBaseVal; }

  nsresult SetBaseValueString(const nsAString& aValue);

  void SetBaseValueFromPathSegments(
      const dom::Sequence<dom::SVGPathSegmentInit>& aValues);

  void ClearBaseValue();

  /**
   * const! See comment for GetBaseValue!
   */
  const SVGPathData& GetAnimValue() const {
    return mAnimVal ? *mAnimVal : mBaseVal;
  }

  nsresult SetAnimValue(const SVGPathData& aNewAnimValue,
                        dom::SVGElement* aElement);

  void ClearAnimValue(dom::SVGElement* aElement);

  /**
   * Empty paths are not rendered.
   */
  bool IsRendered() const;

  /**
   * Needed for correct DOM wrapper construction since GetAnimValue may
   * actually return the baseVal!
   */
  void* GetBaseValKey() const { return (void*)&mBaseVal; }
  void* GetAnimValKey() const { return (void*)&mAnimVal; }

  bool IsAnimating() const { return !!mAnimVal; }

  UniquePtr<SMILAttr> ToSMILAttr(dom::SVGElement* aElement);

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const;

 private:
  // mAnimVal is a pointer to allow us to determine if we're being animated or
  // not. Making it a non-pointer member and using mAnimVal.IsEmpty() to check
  // if we're animating is not an option, since that would break animation *to*
  // the empty string (<set to="">).

  SVGPathData mBaseVal;
  UniquePtr<SVGPathData> mAnimVal;

  struct SMILAnimatedPathSegList : public SMILAttr {
   public:
    SMILAnimatedPathSegList(SVGAnimatedPathSegList* aVal,
                            dom::SVGElement* aElement)
        : mVal(aVal), mElement(aElement) {}

    // These will stay alive because a SMILAttr only lives as long
    // as the Compositing step, and DOM elements don't get a chance to
    // die during that.
    SVGAnimatedPathSegList* mVal;
    dom::SVGElement* mElement;

    // SMILAttr methods
    nsresult ValueFromString(const nsAString& aStr,
                             const dom::SVGAnimationElement* aSrcElement,
                             SMILValue& aValue,
                             bool& aPreventCachingOfSandwich) const override;
    SMILValue GetBaseValue() const override;
    void ClearAnimValue() override;
    nsresult SetAnimValue(const SMILValue& aValue) override;
  };
};

}  // namespace mozilla

#endif  // DOM_SVG_SVGANIMATEDPATHSEGLIST_H_
