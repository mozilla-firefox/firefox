/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ChaosMode.h"

namespace mozilla {

namespace detail {

Atomic<uint32_t, Relaxed> gChaosModeCounter(0);
Atomic<uint32_t, Relaxed> gChaosFeatures(
    static_cast<uint32_t>(ChaosFeature::Any));

} /* namespace detail */
} /* namespace mozilla */
