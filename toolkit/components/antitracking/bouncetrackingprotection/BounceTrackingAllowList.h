/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_BounceTrackingAllowList_h
#define mozilla_BounceTrackingAllowList_h

#include "mozilla/ContentBlockingAllowList.h"

namespace mozilla {

// Extends ContentBlockingAllowListCache to also check for "cookie" and
// "bounce-tracking-protection-exception" allow permissions.
// "bounce-tracking-protection-exception" is set via remote permissions.
class BounceTrackingAllowList final : public ContentBlockingAllowListCache {
 private:
  nsTArray<nsCString> GetAllowListPermissionTypes() override;
  nsresult IsAllowListPermission(nsIPermission* aPermission,
                                 bool* aResult) override;
};

}  // namespace mozilla

#endif
