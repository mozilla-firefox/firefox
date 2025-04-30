/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_DOMSVGSTRINGLIST_H_
#define DOM_SVG_DOMSVGSTRINGLIST_H_

#include "nsCycleCollectionParticipant.h"
#include "SVGElement.h"
#include "mozilla/Attributes.h"
#include "mozilla/RefPtr.h"

namespace mozilla {

class ErrorResult;
class SVGStringList;

namespace dom {

/**
 * Class DOMSVGStringList
 *
 * This class is used to create the DOM tearoff objects that wrap internal
 * SVGPathData objects.
 *
 * See the architecture comment in DOMSVGAnimatedLengthList.h first (that's
 * LENGTH list), then continue reading the remainder of this comment.
 *
 * The architecture of this class is similar to that of DOMSVGLengthList
 * except for two important aspects:
 *
 * First, since there is no nsIDOMSVGAnimatedStringList interface in SVG, we
 * have no parent DOMSVGAnimatedStringList (unlike DOMSVGLengthList which has
 * a parent DOMSVGAnimatedLengthList class). As a consequence, much of the
 * logic that would otherwise be in DOMSVGAnimatedStringList (and is in
 * DOMSVGAnimatedLengthList) is contained in this class.
 *
 * Second, since there is no nsIDOMSVGString interface in SVG, we have no
 * DOMSVGString items to maintain. As far as script is concerned, objects
 * of this class contain a list of strings, not a list of mutable objects
 * like the other SVG list types. As a result, unlike the other SVG list
 * types, this class does not create its items lazily on demand and store
 * them so it can return the same objects each time. It simply returns a new
 * string each time any given item is requested.
 */
class DOMSVGStringList final : public nsISupports, public nsWrapperCache {
  friend class AutoChangeStringListNotifier;

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(DOMSVGStringList)

  dom::SVGElement* GetParentObject() const { return mElement; }
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  uint32_t NumberOfItems() const;
  uint32_t Length() const;
  void Clear();
  void Initialize(const nsAString& aNewItem, nsAString& aRetval,
                  ErrorResult& aRv);
  void GetItem(uint32_t aIndex, nsAString& aRetval, ErrorResult& aRv);
  void IndexedGetter(uint32_t aIndex, bool& aFound, nsAString& aRetval);
  void InsertItemBefore(const nsAString& aNewItem, uint32_t aIndex,
                        nsAString& aRetval, ErrorResult& aRv);
  void ReplaceItem(const nsAString& aNewItem, uint32_t aIndex,
                   nsAString& aRetval, ErrorResult& aRv);
  void RemoveItem(uint32_t aIndex, nsAString& aRetval, ErrorResult& aRv);
  void AppendItem(const nsAString& aNewItem, nsAString& aRetval,
                  ErrorResult& aRv);

  /**
   * Factory method to create and return a DOMSVGStringList wrapper
   * for a given internal SVGStringList object. The factory takes care
   * of caching the object that it returns so that the same object can be
   * returned for the given SVGStringList each time it is requested.
   * The cached object is only removed from the cache when it is destroyed due
   * to there being no more references to it. If that happens, any subsequent
   * call requesting the DOM wrapper for the SVGStringList will naturally
   * result in a new DOMSVGStringList being returned.
   */
  static already_AddRefed<DOMSVGStringList> GetDOMWrapper(
      SVGStringList* aList, dom::SVGElement* aElement,
      bool aIsConditionalProcessingAttribute, uint8_t aAttrEnum);

 private:
  /**
   * Only our static GetDOMWrapper() factory method may create objects of our
   * type.
   */
  DOMSVGStringList(dom::SVGElement* aElement,
                   bool aIsConditionalProcessingAttribute, uint8_t aAttrEnum)
      : mElement(aElement),
        mAttrEnum(aAttrEnum),
        mIsConditionalProcessingAttribute(aIsConditionalProcessingAttribute) {}

  ~DOMSVGStringList();

  SVGStringList& InternalList() const;

  void RemoveFromTearoffTable();

  // Strong ref to our element to keep it alive.
  RefPtr<dom::SVGElement> mElement;

  uint8_t mAttrEnum;

  bool mIsConditionalProcessingAttribute;

  // Tracks whether we're in the tearoff table. Initialized to true, since all
  // new instances are added to the table right after construction. Updated to
  // false when we're removed from the table (at which point we're being
  // destructed or soon-to-be destructed).
  bool mIsInTearoffTable = true;
};

}  // namespace dom
}  // namespace mozilla

#endif  // DOM_SVG_DOMSVGSTRINGLIST_H_
