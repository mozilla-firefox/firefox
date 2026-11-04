/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LINK_SERVICE_COMMON_H_
#define LINK_SERVICE_COMMON_H_

#include "mozilla/Maybe.h"
#include "nsStringFwd.h"

namespace mozilla {
class SHA1Sum;
}

// Add a seed to the computed network ID to prevent user linkability.
void SeedNetworkId(mozilla::SHA1Sum& aSha1);

// The seed SeedNetworkId() adds, as a string, or empty if there is none.
nsCString GetNetworkIdSeed();

// Whether aNetworkId computed with seed aSeed identifies the current network.
// Returns false while aCurrentNetworkId is empty, meaning there is no current
// network to compare against.  Otherwise returns true and sets
// aMatchingNetworkId to aCurrentNetworkId if the IDs identify the same
// network, or to empty if they do not.
bool MatchNetworkId(
    const nsACString& aNetworkId, const nsACString& aSeed,
    const mozilla::Maybe<mozilla::SHA1Sum>& aCurrentUnseededNetworkId,
    const nsACString& aCurrentNetworkId, nsACString& aMatchingNetworkId);

#endif  // LINK_SERVICE_COMMON_H_
