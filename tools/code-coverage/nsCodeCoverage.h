/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef tools_codecoverage_nscodecoverage_h
#define tools_codecoverage_nscodecoverage_h

#include "nsICodeCoverage.h"

class nsCodeCoverage final : nsICodeCoverage {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICODECOVERAGE

 private:
  ~nsCodeCoverage() = default;
};

#endif  // tools_codecoverage_nscodecoverage_h
