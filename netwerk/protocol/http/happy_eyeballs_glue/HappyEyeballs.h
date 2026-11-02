/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HappyEyeballs_h_
#define HappyEyeballs_h_

#include <cstdint>

#include "mozilla/net/happy_eyeballs_glue.h"
#include "nsError.h"
#include "nsTArray.h"

namespace mozilla {
namespace net {

class HappyEyeballs final {
 public:
  HappyEyeballs() = delete;
  ~HappyEyeballs() = delete;

  static nsresult Init(HappyEyeballs** aHappyEyeballs,
                       const nsACString& aOrigin, uint16_t aPort,
                       const nsTArray<happy_eyeballs::AltSvc>* aAltSvc,
                       happy_eyeballs::IpPreference aPref,
                       happy_eyeballs::HttpVersions aHttpVersions);

  void AddRef() { happy_eyeballs::happy_eyeballs_addref(this); }
  void Release() { happy_eyeballs::happy_eyeballs_release(this); }
};

}  // namespace net
}  // namespace mozilla

#endif
