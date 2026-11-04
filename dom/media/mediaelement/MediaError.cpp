/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MediaError.h"

#include "js/Warnings.h"  // JS::WarnUTF8
#include "jsapi.h"
#include "mozilla/Utf8.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/MediaErrorBinding.h"
#include "nsContentUtils.h"
#include "nsIScriptError.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(MediaError, mParent)
NS_IMPL_CYCLE_COLLECTING_ADDREF(MediaError)
NS_IMPL_CYCLE_COLLECTING_RELEASE(MediaError)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(MediaError)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

MediaError::MediaError(HTMLMediaElement* aParent, uint16_t aCode,
                       const nsACString& aMessage)
    : mParent(aParent), mCode(aCode), mMessage(aMessage) {}

nsISupports* MediaError::GetParentObject() const {
  return static_cast<EventTarget*>(mParent);
}

void MediaError::GetMessage(nsAString& aResult) const {
  // When fingerprinting resistance is enabled, only messages in this list
  // can be returned to content script.
  static constexpr nsLiteralCString whitelist[] = {
      "404: Not Found"_ns
      // TODO
  };

  const bool shouldBlank = std::find(std::begin(whitelist), std::end(whitelist),
                                     mMessage) == std::end(whitelist);

  if (shouldBlank) {
    // Print a warning message to JavaScript console to alert developers of
    // a non-whitelisted error message.
    nsAutoCString message =
        nsLiteralCString(
            "This error message will be blank when "
            "privacy.resistFingerprinting = true."
            "  If it is really necessary, please add it to the whitelist in"
            " MediaError::GetMessage: ") +
        mMessage;
    Document* ownerDoc = mParent->OwnerDoc();
    AutoJSAPI api;
    // mMessage is an nsCString with no encoding guarantee (it may contain
    // raw bytes from HTTP status text). Ensure valid UTF-8 for WarnUTF8
    // below by converting non-UTF-8 bytes through Latin-1 (a superset of
    // ASCII that maps every byte 0x00-0xFF to a Unicode codepoint).
    if (!IsUtf8(message)) {
      nsAutoCString utf8;
      CopyLatin1toUTF8(message, utf8);
      message = std::move(utf8);
    }
    if (api.Init(ownerDoc->GetScopeObject())) {
      // We prefer this API because it can also print to our debug log and
      // try server's log viewer.
      JS::WarnUTF8(api.cx(), "%s", message.get());
    } else {
      // If failed to use JS::WarnUTF8, fall back to
      // nsContentUtils::ReportToConsoleNonLocalized, which can only print to
      // JavaScript console.
      nsContentUtils::ReportToConsoleNonLocalized(
          NS_ConvertUTF8toUTF16(message), nsIScriptError::warningFlag,
          "MediaError"_ns, ownerDoc);
    }

    if (!nsContentUtils::IsCallerChrome() &&
        ownerDoc->ShouldResistFingerprinting(RFPTarget::MediaError)) {
      aResult.Truncate();
      return;
    }
  }

  CopyUTF8toUTF16(mMessage, aResult);
}

JSObject* MediaError::WrapObject(JSContext* aCx,
                                 JS::Handle<JSObject*> aGivenProto) {
  return MediaError_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
