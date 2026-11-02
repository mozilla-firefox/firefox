/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HappyEyeballs.h"

namespace mozilla {
namespace net {

// static
nsresult HappyEyeballs::Init(HappyEyeballs** aHappyEyeballs,
                             const nsACString& aOrigin, uint16_t aPort,
                             const nsTArray<happy_eyeballs::AltSvc>* aAltSvc,
                             happy_eyeballs::IpPreference aPref,
                             happy_eyeballs::HttpVersions aHttpVersions) {
  return happy_eyeballs::happy_eyeballs_create(
      (const HappyEyeballs**)aHappyEyeballs, &aOrigin, aPort, aAltSvc, aPref,
      aHttpVersions);
}

}  // namespace net
}  // namespace mozilla
