/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MicrosoftEntraSSOUtils_h_
#define MicrosoftEntraSSOUtils_h_

#include "ErrorList.h"  // For nsresult
#include "nsHttpChannel.h"

namespace mozilla {
namespace net {

class nsHttpChannel;
class MicrosoftEntraSSOUtils;  // Used in SSORequestDelegate

nsresult AddMicrosoftEntraSSO(nsHttpChannel* aChannel, bool aDeviceOnly,
                              std::function<void()>&& aResultCallback);

}  // namespace net
}  // namespace mozilla

#endif  // MicrosoftEntraSSOUtils_h_
