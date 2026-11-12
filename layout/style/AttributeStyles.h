/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * style sheet and style rule processor representing data from presentational
 * HTML attributes
 */

#ifndef mozilla_AttributeStyles_h_
#define mozilla_AttributeStyles_h_

#include "mozilla/MemoryReporting.h"
#include "nsColor.h"
#include "nsString.h"
#include "nsTHashMap.h"

struct MiscContainer;
namespace mozilla {
struct StyleLockedDeclarationBlock;
namespace dom {
class Document;
}  // namespace dom

class AttributeStyles final {
 public:
  explicit AttributeStyles(dom::Document* aDocument);
  void SetOwningDocument(dom::Document* aDocument);

  NS_INLINE_DECL_REFCOUNTING(AttributeStyles)

  size_t DOMSizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

  void Reset();

  const Maybe<nscolor>& GetLinkColor() const { return mLinkColor; }
  const Maybe<nscolor>& GetActiveLinkColor() const { return mActiveLinkColor; }
  const Maybe<nscolor>& GetVisitedLinkColor() const {
    return mVisitedLinkColor;
  }

  void SetLinkColor(nscolor aColor);
  void SetActiveLinkColor(nscolor aColor);
  void SetVisitedLinkColor(nscolor aColor);

  void CacheStyleAttr(const nsAString& aSerialized, MiscContainer* aValue) {
    mCachedStyleAttrs.InsertOrUpdate(aSerialized, aValue);
  }
  void EvictStyleAttr(const nsAString& aSerialized, MiscContainer* aValue) {
    NS_ASSERTION(LookupStyleAttr(aSerialized) == aValue,
                 "Cached value doesn't match?");
    mCachedStyleAttrs.Remove(aSerialized);
  }
  MiscContainer* LookupStyleAttr(const nsAString& aSerialized) {
    return mCachedStyleAttrs.Get(aSerialized);
  }

  AttributeStyles(const AttributeStyles& aCopy) = delete;
  AttributeStyles& operator=(const AttributeStyles& aCopy) = delete;

 private:
  ~AttributeStyles();

  // Implementation of SetLink/VisitedLink/ActiveLinkColor
  void ImplLinkColorSetter(Maybe<nscolor>& aSlot, nscolor);

  dom::Document* mDocument;
  Maybe<nscolor> mLinkColor;
  Maybe<nscolor> mActiveLinkColor;
  Maybe<nscolor> mVisitedLinkColor;
  nsTHashMap<nsStringHashKey, MiscContainer*> mCachedStyleAttrs;
};

}  // namespace mozilla

#endif
