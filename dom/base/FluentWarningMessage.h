/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FluentWarningMessage_h
#define mozilla_dom_FluentWarningMessage_h

#include <algorithm>
#include <iterator>

#include "mozilla/Assertions.h"
#include "nsContentUtils.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla::dom {

/**
 * Describes the Fluent message for a warning in one of the warning lists
 * (deprecated operations, document warnings) that has been migrated from
 * dom.properties. Tables of these are looked up with GetFluentWarningMessage.
 */
template <typename Warning>
struct FluentWarningMessage {
  Warning mWarning;
  const char* mL10nId;
  // Fixed URL passed to the message as $url, or nullptr.
  const char* mUrl;
  // Names of the positional parameters passed by callers, in order.
  const char* mArgNames[3];
};

/**
 * Looks up aWarning in aMessages. If found, sets its message identifier and
 * arguments, converting the positional aParams to named arguments, and returns
 * true.
 */
template <typename Warning, size_t N>
bool GetFluentWarningMessage(
    const FluentWarningMessage<Warning> (&aMessages)[N], Warning aWarning,
    const nsTArray<nsString>& aParams, nsACString& aL10nId,
    nsTArray<intl::FluentArg>& aArgs) {
  for (const FluentWarningMessage<Warning>& message : aMessages) {
    if (message.mWarning != aWarning) {
      continue;
    }
    aL10nId.AssignASCII(message.mL10nId);
    if (message.mUrl) {
      aArgs.EmplaceBack("url"_ns, nsDependentCString(message.mUrl));
    }
    size_t argCount = 0;
    while (argCount < std::size(message.mArgNames) &&
           message.mArgNames[argCount]) {
      ++argCount;
    }
    MOZ_ASSERT(argCount == aParams.Length(), "Unexpected number of parameters");
    for (size_t i = 0; i < std::min(argCount, size_t(aParams.Length())); ++i) {
      aArgs.EmplaceBack(nsDependentCString(message.mArgNames[i]), aParams[i]);
    }
    return true;
  }
  return false;
}

}  // namespace mozilla::dom

#endif  // mozilla_dom_FluentWarningMessage_h
