/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_TextFormat_h_
#define mozilla_dom_TextFormat_h_

#include "mozilla/dom/TextFormatBinding.h"
#include "nsWrapperCache.h"

class nsIGlobalObject;

namespace mozilla::dom {

class TextFormat final : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(TextFormat)

  nsIGlobalObject* GetParentObject() const { return mGlobal; }

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  static already_AddRefed<TextFormat> Constructor(
      const GlobalObject& aGlobal, const TextFormatInit& aOptions);

  uint32_t RangeStart() const { return mRangeStart; }

  uint32_t RangeEnd() const { return mRangeEnd; }

  enum UnderlineStyle UnderlineStyle() const { return mUnderlineStyle; }

  enum UnderlineThickness UnderlineThickness() const {
    return mUnderlineThickness;
  }

 private:
  TextFormat(nsIGlobalObject* aGlobal, const TextFormatInit& aOptions);
  ~TextFormat() = default;

  nsCOMPtr<nsIGlobalObject> mGlobal;
  uint32_t mRangeStart;
  uint32_t mRangeEnd;
  enum UnderlineStyle mUnderlineStyle;
  enum UnderlineThickness mUnderlineThickness;
};

}  // namespace mozilla::dom

#endif  // DOM_TEXTFORMAT_H_
