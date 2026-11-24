/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#include "MockWidget.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/dom/Touch.h"
#include "mozilla/layers/APZEventState.h"

using namespace mozilla;
using namespace mozilla::layers;
using ::testing::_;
using ::testing::InSequence;
using ::testing::MockFunction;

static void ProcessTouch(APZEventState* aState, nsIWidget* aWidget,
                         EventMessage aMessage, uint64_t aInputBlockId,
                         nsEventStatus aContentResponse) {
  WidgetTouchEvent event(true, aMessage, aWidget);
  event.mFlags.mHandledByAPZ = true;
  event.mTouches.AppendElement(new dom::Touch(0, LayoutDeviceIntPoint(10, 10),
                                              LayoutDeviceIntPoint(), 0, 0));
  aState->ProcessTouchEvent(event, ScrollableLayerGuid(), aInputBlockId,
                            nsEventStatus_eConsumeDoDefault, aContentResponse,
                            {});
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY static void ProcessLongTap(APZEventState* aState,
                                                       uint64_t aInputBlockId) {
  RefPtr<APZEventState> state = aState;
  state->ProcessLongTap(nullptr, CSSPoint(10, 10), CSSToLayoutDeviceScale(1), 0,
                        aInputBlockId);
}

// Tests that a long tap doesn't send a content response for the original
// touch block, so that the response for the block comes from the first
// touchmove.
TEST(APZEventState, LongTapDoesNotRespondForOriginalTouchBlock)
{
  const uint64_t touchBlockId = 1;
  const uint64_t longTapBlockId = 2;

  MockFunction<void(uint64_t, bool)> contentResponse;
  MockFunction<void(std::string checkPointName)> check;
  {
    InSequence s;
    EXPECT_CALL(check, Call("pre long-tap"));
    // The second arg is not defined, because the result of the long tap varies
    // depending on the platform.
    EXPECT_CALL(contentResponse, Call(longTapBlockId, _)).Times(1);
    EXPECT_CALL(check, Call("post long-tap"));
    EXPECT_CALL(contentResponse, Call(touchBlockId, true)).Times(1);
  }

  RefPtr<MockWidget> widget = new MockWidget();
  RefPtr<APZEventState> state = new APZEventState(
      widget, [&](uint64_t aInputBlockId, bool aPreventDefault) {
        contentResponse.Call(aInputBlockId, aPreventDefault);
      });

  // Process touchstart event first. This event is not preventDefault-ed
  // (i.e., the status is nsEventStatus_eIgnore).
  ProcessTouch(state, widget, eTouchStart, touchBlockId, nsEventStatus_eIgnore);
  check.Call("pre long-tap");
  ProcessLongTap(state, longTapBlockId);
  check.Call("post long-tap");
  // Process touchmove event then. This event is preventDefault-ed
  // (i.e., the status is nsEventStatus_eConsumeNoDefault).
  ProcessTouch(state, widget, eTouchMove, touchBlockId,
               nsEventStatus_eConsumeNoDefault);

  state->Destroy();
}
