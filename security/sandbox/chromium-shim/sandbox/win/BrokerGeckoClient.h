/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SANDBOX_WIN_BROKERGECKOCLIENT_H_
#define SANDBOX_WIN_BROKERGECKOCLIENT_H_

#include <string_view>

#include "BrokerGeckoServices.h"

namespace mozilla::sandboxing {

// Returns true if Gecko allows read-only access to the font file at aNtPath.
// Returns false if Gecko has not provided a validator.
bool GeckoValidateFontFile(std::wstring_view aNtPath);

}  // namespace mozilla::sandboxing

#endif  // SANDBOX_WIN_BROKERGECKOCLIENT_H_
