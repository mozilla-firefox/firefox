/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "TextFormat.h"

#include "nsIGlobalObject.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTING_ADDREF(TextFormat)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TextFormat)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(TextFormat, mGlobal)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TextFormat)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

TextFormat::TextFormat(nsIGlobalObject* aGlobal, const TextFormatInit& aOptions)
    : mGlobal(aGlobal),
      mRangeStart(aOptions.mRangeStart),
      mRangeEnd(aOptions.mRangeEnd),
      mUnderlineStyle(aOptions.mUnderlineStyle),
      mUnderlineThickness(aOptions.mUnderlineThickness) {}

JSObject* TextFormat::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return TextFormat_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<TextFormat> TextFormat::Constructor(
    const GlobalObject& aGlobal, const TextFormatInit& aOptions) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<TextFormat> fmt = new TextFormat(global, aOptions);
  return fmt.forget();
}

}  // namespace mozilla::dom
