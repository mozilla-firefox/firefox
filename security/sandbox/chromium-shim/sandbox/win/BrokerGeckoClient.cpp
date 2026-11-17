/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BrokerGeckoClient.h"

#include <string.h>

#include <algorithm>
#include <type_traits>

namespace mozilla::sandboxing {

static BrokerGeckoServices sBrokerGeckoServices;

extern "C" __declspec(dllexport) void SetBrokerGeckoServices(
    const BrokerGeckoServices* aServices) {
  sBrokerGeckoServices = BrokerGeckoServices();
  if (!aServices) {
    return;
  }

  memcpy(&sBrokerGeckoServices, aServices,
         std::min(aServices->size, sizeof(BrokerGeckoServices)));
  sBrokerGeckoServices.size = sizeof(BrokerGeckoServices);
}

static_assert(std::is_same_v<decltype(&SetBrokerGeckoServices),
                             SetBrokerGeckoServicesFn>);

bool GeckoValidateFontFile(std::wstring_view aNtPath) {
  return sBrokerGeckoServices.validateFontFile &&
         sBrokerGeckoServices.validateFontFile(aNtPath);
}

}  // namespace mozilla::sandboxing
