/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef XPCOM_THREADS_THREADDELAY_H_
#define XPCOM_THREADS_THREADDELAY_H_

#include "mozilla/ChaosMode.h"

namespace mozilla {

// Sleep for a random number of microseconds less than 100 if aFeature is
// enabled.
void DelayForChaosMode(ChaosFeature aFeature);

}  // namespace mozilla

#endif  // XPCOM_THREADS_THREADDELAY_H_
