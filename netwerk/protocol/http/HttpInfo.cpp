/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http:mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "nsHttpHandler.h"
#include "HttpInfo.h"

void mozilla::net::HttpInfo::GetHttpConnectionData(
    nsTArray<HttpRetParams>* args) {
  if (gHttpHandler && gHttpHandler->ConnMgr()) {
    gHttpHandler->ConnMgr()->GetConnectionData(args);
  }
}

void mozilla::net::HttpInfo::GetHttp3ConnectionStatsData(
    nsTArray<Http3ConnectionStatsParams>* args) {
  if (gHttpHandler && gHttpHandler->ConnMgr()) {
    gHttpHandler->ConnMgr()->GetHttp3ConnectionStatsData(args);
  }
}
