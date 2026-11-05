/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LinkServiceCommon.h"

#include "mozilla/Base64.h"
#include "mozilla/Maybe.h"
#include "mozilla/SHA1.h"
#include "mozilla/TimeStamp.h"
#include "nsID.h"
#include "nsString.h"

using namespace mozilla;

static const Maybe<nsID>& NetworkIdSeed() {
  static Maybe<nsID> seed = ([]() {
    Maybe<nsID> uuid(std::in_place);
    if (NS_FAILED(nsID::GenerateUUIDInPlace(*uuid))) {
      uuid.reset();
    }
    return uuid;
  })();
  return seed;
}

void SeedNetworkId(SHA1Sum& aSha1) {
  const Maybe<nsID>& seed = NetworkIdSeed();
  if (seed) {
    aSha1.update(seed.ptr(), sizeof(*seed));
  } else {
    TimeStamp timestamp = TimeStamp::ProcessCreation();
    aSha1.update(&timestamp, sizeof(timestamp));
  }
}

nsCString GetNetworkIdSeed() {
  const Maybe<nsID>& seed = NetworkIdSeed();
  if (!seed) {
    return nsCString();
  }
  return nsCString(seed->ToString().get());
}

bool MatchNetworkId(const nsACString& aNetworkId, const nsACString& aSeed,
                    const Maybe<SHA1Sum>& aCurrentUnseededNetworkId,
                    const nsACString& aCurrentNetworkId,
                    nsACString& aMatchingNetworkId) {
  aMatchingNetworkId.Truncate();

  MOZ_ASSERT(aCurrentUnseededNetworkId.isSome() ==
             !aCurrentNetworkId.IsEmpty());
  if (!aCurrentUnseededNetworkId) {
    return false;
  }

  nsID seed;
  if (aNetworkId.IsEmpty() || !seed.Parse(aSeed)) {
    return true;
  }

  SHA1Sum sha1 = *aCurrentUnseededNetworkId;
  sha1.update(&seed, sizeof(seed));
  SHA1Sum::Hash digest;
  sha1.finish(digest);
  nsAutoCString output;
  if (NS_SUCCEEDED(Base64Encode(reinterpret_cast<const char*>(digest),
                                SHA1Sum::kHashSize, output)) &&
      output == aNetworkId) {
    aMatchingNetworkId = aCurrentNetworkId;
  }
  return true;
}
