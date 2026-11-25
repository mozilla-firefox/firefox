/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ThreadDelay.h"

#include "mozilla/Assertions.h"
#include "mozilla/ChaosMode.h"

#if defined(XP_WIN) || defined(XP_MACOSX)
#  include <thread>

#  include "mozilla/TimeStamp.h"
#else
#  include <unistd.h>
#endif

namespace mozilla {

void DelayForChaosMode(ChaosFeature aFeature,
                       const uint32_t aMicrosecondLimit) {
  if (!ChaosMode::isActive(aFeature)) {
    return;
  }

  MOZ_ASSERT(aMicrosecondLimit <= 1000);
  const uint32_t duration = ChaosMode::randomUint32LessThan(aMicrosecondLimit);
#if defined(XP_WIN) || defined(XP_MACOSX)
  // Windows and macOS don't support sleeping at low resolutions.
  // If we use normal sleep commands, it can cause heavy delays, so we spin
  // with yields. This should be OK, as this is Chaos Mode-only and each wait
  // is required to be <= 1ms.
  const TimeStamp deadline =
      TimeStamp::Now() + TimeDuration::FromMicroseconds(duration);
  while (TimeStamp::Now() < deadline) {
    std::this_thread::yield();
  }
#else
  ::usleep(duration);
#endif
}

}  // namespace mozilla
