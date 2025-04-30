/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_SVGPATHSEGMENT_H_
#define DOM_SVG_SVGPATHSEGMENT_H_

#include "nsWrapperCache.h"
#include "SVGPathSegUtils.h"
#include "mozilla/dom/SVGPathElement.h"

namespace mozilla::dom {

class SVGPathElement;

class SVGPathSegment final : public nsWrapperCache {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(SVGPathSegment)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(SVGPathSegment)

  SVGPathSegment(SVGPathElement* aSVGPathElement,
                 const StylePathCommand& aValue);

 protected:
  virtual ~SVGPathSegment() = default;

 public:
  SVGPathElement* GetParentObject() const { return mSVGPathElement; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  void GetType(DOMString& aType);
  void SetType(const nsAString& aType);

  void GetValues(nsTArray<float>& aValues);
  void SetValues(const nsTArray<float>& aValues);

 private:
  RefPtr<SVGPathElement> mSVGPathElement;
  nsString mCommand;
  nsTArray<float> mValues;
};

}  // namespace mozilla::dom

#endif  // DOM_SVG_SVGPATHSEGMENT_H_
