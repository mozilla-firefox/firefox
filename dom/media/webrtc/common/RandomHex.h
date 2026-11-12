/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_WEBRTC_COMMON_RANDOMHEX_H_
#define DOM_MEDIA_WEBRTC_COMMON_RANDOMHEX_H_

#include <stddef.h>

#include <string>

namespace mozilla {

// Returns aWords random 32-bit words from NSS, as 8 hex characters each.
// Crashes if NSS cannot generate random bytes.
std::string GetRandomHex(size_t aWords);

}  // namespace mozilla

#endif  // DOM_MEDIA_WEBRTC_COMMON_RANDOMHEX_H_
