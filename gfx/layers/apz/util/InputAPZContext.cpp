/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InputAPZContext.h"

namespace mozilla {
namespace layers {

constinit ScrollableLayerGuid InputAPZContext::sGuid;
uint64_t InputAPZContext::sBlockId = 0;
nsEventStatus InputAPZContext::sApzResponse = nsEventStatus_eSentinel;
bool InputAPZContext::sPendingLayerization = false;
bool InputAPZContext::sRoutedToChildProcess = false;
bool InputAPZContext::sDropped = false;

/*static*/
ScrollableLayerGuid InputAPZContext::GetTargetLayerGuid() { return sGuid; }

/*static*/
uint64_t InputAPZContext::GetInputBlockId() { return sBlockId; }

/*static*/
nsEventStatus InputAPZContext::GetApzResponse() { return sApzResponse; }

/*static*/
bool InputAPZContext::HavePendingLayerization() { return sPendingLayerization; }

/*static*/
bool InputAPZContext::WasRoutedToChildProcess() {
  return sRoutedToChildProcess;
}

/*static*/
bool InputAPZContext::WasDropped() { return sDropped; }

InputAPZContext::InputAPZContext(const ScrollableLayerGuid& aGuid,
                                 const uint64_t& aBlockId,
                                 const nsEventStatus& aApzResponse,
                                 bool aPendingLayerization)
    : mOldGuid(sGuid),
      mOldBlockId(sBlockId),
      mOldApzResponse(sApzResponse),
      mOldPendingLayerization(sPendingLayerization),
      mOldRoutedToChildProcess(sRoutedToChildProcess),
      mOldDropped(sDropped) {
  sGuid = aGuid;
  sBlockId = aBlockId;
  sApzResponse = aApzResponse;
  sPendingLayerization = aPendingLayerization;
  sRoutedToChildProcess = false;
  sDropped = false;
}

InputAPZContext::~InputAPZContext() {
  sGuid = mOldGuid;
  sBlockId = mOldBlockId;
  sApzResponse = mOldApzResponse;
  sPendingLayerization = mOldPendingLayerization;
  sRoutedToChildProcess = mOldRoutedToChildProcess;
  sDropped = mOldDropped;
}

/*static*/
void InputAPZContext::SetRoutedToChildProcess() {
  sRoutedToChildProcess = true;
}

/*static*/
void InputAPZContext::SetDropped() { sDropped = true; }

}  // namespace layers
}  // namespace mozilla
