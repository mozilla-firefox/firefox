/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HttpWinUtils_h_
#define HttpWinUtils_h_

namespace mozilla {
namespace net {

class nsHttpChannel;

void AddWindowsSSO(nsHttpChannel* channel, bool aDeviceOnly);

}  // namespace net
}  // namespace mozilla

#endif  // HttpWinUtils_h_
