/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_TouchSwipeInfo_h
#define mozilla_TouchSwipeInfo_h

#include "InputData.h"
#include "mozilla/layers/APZBridge.h"
#include "nsTArray.h"

namespace mozilla {

struct TouchSwipeInfo {
  MultiTouchInput input;
  layers::APZEventResult result;
};

struct TouchSwipeInfoQueue {
  explicit TouchSwipeInfoQueue(uint64_t aInputBlockId)
      : inputBlockId(aInputBlockId) {}

  nsTArray<TouchSwipeInfo> queuedInfo;
  uint64_t inputBlockId;
};

}  // namespace mozilla

#endif  // mozilla_TouchSwipeInfo_h
