/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http:mozilla.org/MPL/2.0/. */

#ifndef nsHttpInfo__
#define nsHttpInfo__

#include "nsTArrayForwardDeclare.h"

namespace mozilla {
namespace net {

struct HttpRetParams;

class HttpInfo {
 public:
  /* Calls getConnectionData method in nsHttpConnectionMgr. */
  static void GetHttpConnectionData(nsTArray<HttpRetParams>*);
  static void GetHttp3ConnectionStatsData(
      nsTArray<Http3ConnectionStatsParams>*);
};

}  // namespace net
}  // namespace mozilla

#endif  // nsHttpInfo__
