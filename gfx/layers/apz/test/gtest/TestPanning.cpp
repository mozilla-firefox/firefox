/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "APZCBasicTester.h"
#include "APZCTreeManagerTester.h"
#include "APZTestCommon.h"
#include "FrameMetrics.h"
#include "InputUtils.h"
#include "Units.h"
#include "gtest/gtest.h"
#include "mozilla/ScrollSnapInfo.h"
#include "mozilla/ServoComputedData.h"
#include "mozilla/gfx/CompositorHitTestInfo.h"
#include "mozilla/layers/ScrollableLayerGuid.h"

class APZCPanningTester : public APZCBasicTester {
 protected:
  void DoPanTest(bool aShouldTriggerScroll, bool aShouldBeConsumed,
                 uint32_t aBehavior) {
    apzc->DisableDefaultTouchBehaviors();

    if (aShouldTriggerScroll) {
      // Four repaint request for each pan.
      EXPECT_CALL(*mcc, RequestContentRepaint(_)).Times(8);
    } else {
      EXPECT_CALL(*mcc, RequestContentRepaint(_)).Times(0);
    }

    int touchStart = 50;
    int touchEnd = 10;
    ParentLayerPoint pointOut;
    AsyncTransform viewTransformOut;

    nsTArray<uint32_t> allowedTouchBehaviors;
    allowedTouchBehaviors.AppendElement(aBehavior);

    // Pan down
    PanAndCheckStatus(apzc, touchStart, touchEnd, aShouldBeConsumed,
                      &allowedTouchBehaviors);
    apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);

    if (aShouldTriggerScroll) {
      EXPECT_EQ(ParentLayerPoint(0, -(touchEnd - touchStart)), pointOut);
      EXPECT_NE(AsyncTransform(), viewTransformOut);
    } else {
      EXPECT_EQ(ParentLayerPoint(), pointOut);
      EXPECT_EQ(AsyncTransform(), viewTransformOut);
    }

    // Clear the fling from the previous pan, or stopping it will
    // consume the next touchstart
    apzc->CancelAnimation();

    // Pan back
    PanAndCheckStatus(apzc, touchEnd, touchStart, aShouldBeConsumed,
                      &allowedTouchBehaviors);
    apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);

    EXPECT_EQ(ParentLayerPoint(), pointOut);
    EXPECT_EQ(AsyncTransform(), viewTransformOut);
  }

  void DoPanWithPreventDefaultTest() {
    apzc->DisableDefaultTouchBehaviors();
    MakeApzcWaitForMainThread();

    int touchStart = 50;
    int touchEnd = 10;
    ParentLayerPoint pointOut;
    AsyncTransform viewTransformOut;
    uint64_t blockId = 0;

    // Pan down
    nsTArray<uint32_t> allowedTouchBehaviors;
    allowedTouchBehaviors.AppendElement(
        mozilla::layers::AllowedTouchBehavior::VERTICAL_PAN);
    PanAndCheckStatus(apzc, touchStart, touchEnd, true, &allowedTouchBehaviors,
                      &blockId);

    // Send the signal that content has handled and preventDefaulted the touch
    // events. This flushes the event queue.
    apzc->ContentReceivedInputBlock(blockId, true);

    apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);
    EXPECT_EQ(ParentLayerPoint(), pointOut);
    EXPECT_EQ(AsyncTransform(), viewTransformOut);

    apzc->AssertStateIsReset();
  }

  void PanWithFling() {
    // Send a pan gesture that triggers a fling animation at the end.
    // Note that we need at least two _PAN events to have enough samples
    // in the velocity tracker to compute a fling velocity.
    PanGesture(PanGestureInput::PANGESTURE_START, apzc, ScreenIntPoint(50, 80),
               ScreenPoint(0, 2), mcc->Time());
    mcc->AdvanceByMillis(5);
    apzc->AdvanceAnimations(mcc->GetSampleTime());
    PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
               ScreenPoint(0, 10), mcc->Time());
    mcc->AdvanceByMillis(5);
    apzc->AdvanceAnimations(mcc->GetSampleTime());
    PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
               ScreenPoint(0, 10), mcc->Time());
    mcc->AdvanceByMillis(5);
    apzc->AdvanceAnimations(mcc->GetSampleTime());
    PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 80),
               ScreenPoint(0, 0), mcc->Time(), MODIFIER_NONE,
               /*aSimulateMomentum=*/true);
  }
};

class APZCDPITester : public APZCBasicTester {
 protected:
  virtual TestAPZCTreeManager* CreateTreeManager() override {
    return new TestAPZCTreeManager(mcc, CSSToLayoutDeviceScale{2.0F});
  }
};

// In the each of the following 4 pan tests we are performing two pan gestures:
// vertical pan from top to bottom and back - from bottom to top. According to
// the pointer-events/touch-action spec AUTO and PAN_Y touch-action values allow
// vertical scrolling while NONE and PAN_X forbid it. The first parameter of
// DoPanTest method specifies this behavior. However, the events will be marked
// as consumed even if the behavior in PAN_X, because the user could move their
// finger horizontally too - APZ has no way of knowing beforehand and so must
// consume the events.
TEST_F(APZCPanningTester, PanWithTouchActionAuto) {
  // Velocity bias can cause extra repaint requests.
  SCOPED_GFX_PREF_FLOAT("apz.velocity_bias", 0.0);
  DoPanTest(true, true,
            mozilla::layers::AllowedTouchBehavior::HORIZONTAL_PAN |
                mozilla::layers::AllowedTouchBehavior::VERTICAL_PAN);
}

TEST_F(APZCPanningTester, PanWithTouchActionNone) {
  // Velocity bias can cause extra repaint requests.
  SCOPED_GFX_PREF_FLOAT("apz.velocity_bias", 0.0);
  DoPanTest(false, false, 0);
}

TEST_F(APZCPanningTester, PanWithTouchActionPanX) {
  // Velocity bias can cause extra repaint requests.
  SCOPED_GFX_PREF_FLOAT("apz.velocity_bias", 0.0);
  DoPanTest(false, false,
            mozilla::layers::AllowedTouchBehavior::HORIZONTAL_PAN);
}

TEST_F(APZCPanningTester, PanWithTouchActionPanY) {
  // Velocity bias can cause extra repaint requests.
  SCOPED_GFX_PREF_FLOAT("apz.velocity_bias", 0.0);
  DoPanTest(true, true, mozilla::layers::AllowedTouchBehavior::VERTICAL_PAN);
}

TEST_F(APZCPanningTester, PanWithPreventDefault) {
  DoPanWithPreventDefaultTest();
}

TEST_F(APZCPanningTester, PanWithHistoricalTouchData) {
  SCOPED_GFX_PREF_FLOAT("apz.fling_min_velocity_threshold", 0.0);

  // Simulate the same pan gesture, in three different ways.
  // We start at y=50, with a 50ms resting period at the start of the pan.
  // Then we accelerate the finger upwards towards y=10, reaching a 10px/10ms
  // velocity towards the end of the panning motion.
  //
  // The first simulation fires touch move events with 10ms gaps.
  // The second simulation skips two of the touch move events, simulating
  // "jank". The third simulation also skips those two events, but reports the
  // missed positions in the following event's historical coordinates.
  //
  // Consequently, the first and third simulation should estimate the same
  // velocities, whereas the second simulation should estimate a different
  // velocity because it is missing data.

  // First simulation: full data

  APZEventResult result = TouchDown(apzc, ScreenIntPoint(0, 50), mcc->Time());

  mcc->AdvanceByMillis(50);
  result = TouchMove(apzc, ScreenIntPoint(0, 45), mcc->Time());
  mcc->AdvanceByMillis(10);
  result = TouchMove(apzc, ScreenIntPoint(0, 40), mcc->Time());
  mcc->AdvanceByMillis(10);
  result = TouchMove(apzc, ScreenIntPoint(0, 30), mcc->Time());
  mcc->AdvanceByMillis(10);
  result = TouchMove(apzc, ScreenIntPoint(0, 20), mcc->Time());
  result = TouchUp(apzc, ScreenIntPoint(0, 20), mcc->Time());
  auto velocityFromFullDataAsSeparateEvents = apzc->GetVelocityVector();
  apzc->CancelAnimation();

  mcc->AdvanceByMillis(100);

  // Second simulation: partial data

  result = TouchDown(apzc, ScreenIntPoint(0, 50), mcc->Time());

  mcc->AdvanceByMillis(50);
  result = TouchMove(apzc, ScreenIntPoint(0, 45), mcc->Time());
  mcc->AdvanceByMillis(30);
  result = TouchMove(apzc, ScreenIntPoint(0, 20), mcc->Time());
  result = TouchUp(apzc, ScreenIntPoint(0, 20), mcc->Time());
  auto velocityFromPartialData = apzc->GetVelocityVector();
  apzc->CancelAnimation();

  mcc->AdvanceByMillis(100);

  // Third simulation: full data via historical data

  result = TouchDown(apzc, ScreenIntPoint(0, 50), mcc->Time());

  mcc->AdvanceByMillis(50);
  result = TouchMove(apzc, ScreenIntPoint(0, 45), mcc->Time());
  mcc->AdvanceByMillis(30);

  MultiTouchInput mti =
      CreateMultiTouchInput(MultiTouchInput::MULTITOUCH_MOVE, mcc->Time());
  auto singleTouchData = CreateSingleTouchData(0, ScreenIntPoint(0, 20));
  singleTouchData.mHistoricalData.AppendElement(
      SingleTouchData::HistoricalTouchData{
          mcc->Time() - TimeDuration::FromMilliseconds(20),
          ScreenIntPoint(0, 40),
          {},
          {},
          0.0f,
          0.0f});
  singleTouchData.mHistoricalData.AppendElement(
      SingleTouchData::HistoricalTouchData{
          mcc->Time() - TimeDuration::FromMilliseconds(10),
          ScreenIntPoint(0, 30),
          {},
          {},
          0.0f,
          0.0f});
  mti.mTouches.AppendElement(singleTouchData);
  result = apzc->ReceiveInputEvent(mti);

  result = TouchUp(apzc, ScreenIntPoint(0, 20), mcc->Time());
  auto velocityFromFullDataViaHistory = apzc->GetVelocityVector();
  apzc->CancelAnimation();

  EXPECT_EQ(velocityFromFullDataAsSeparateEvents,
            velocityFromFullDataViaHistory);
  EXPECT_NE(velocityFromPartialData, velocityFromFullDataViaHistory);
}

TEST_F(APZCPanningTester, DuplicatePanEndEvents_Bug1833950) {
  // Send a pan gesture that triggers a fling animation at the end.
  PanWithFling();

  // Give the fling animation a chance to start.
  SampleAnimationOnce();
  apzc->AssertStateIsFling();

  // Send a duplicate pan-end event.
  // This test is just intended to check that doing this doesn't
  // trigger an assertion failure in debug mode.
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time(), MODIFIER_NONE,
             /*aSimulateMomentum=*/true);
}

#ifndef MOZ_WIDGET_ANDROID  // Only applies to GenericOverscrollEffect
// When content preventDefaults a pan gesture block, the block is marked
// interrupted and the pan-end that follows can no longer be added to it. If the
// gesture had already scrolled the APZC into overscroll, that pan-end is the
// only thing left that can start the snap-back, so it still has to reach
// OnPanEnd. Note that on GTK, where this was reported, the pan-end is derived
// from a scroll-stop event and therefore carries no displacement.
//
// helper_overscroll_stuck_pan_end.html covers the companion case, where an
// overscroll snap-back animation from an earlier gesture is already running and
// must not be cancelled. Here no animation is running yet, so nothing else can
// relieve the overscroll.
TEST_F(APZCPanningTester, StrayPanEndSnapsBackOverscroll_Bug1931090) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  // Hold the block in the input queue until content has responded, as happens
  // for a page with a non-passive wheel listener.
  MakeApzcWaitForMainThread();

  FrameMetrics metrics = apzc->GetFrameMetrics();
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 1000));
  metrics.SetVisualScrollOffset(CSSPoint(0, 0));
  metrics.SetIsRootContent(true);
  apzc->SetFrameMetrics(metrics);

  // Send a pan-start with a displacement. No effect yet: it's waiting in the
  // input queue for a content response.
  APZEventResult result =
      PanGesture(PanGestureInput::PANGESTURE_START, apzc,
                 ScreenIntPoint(50, 50), ScreenPoint(0, -20), mcc->Time());
  EXPECT_FALSE(apzc->IsOverscrolled());

  // Content lets the first event of the gesture through. (The block needs
  // both a confirmed target and a content response before the queue will
  // allow it to be processed).
  apzc->ConfirmTarget(result.mInputBlockId);
  apzc->ContentReceivedInputBlock(result.mInputBlockId, false);

  // So this pans upward at the top of the scroll range and the APZC goes into
  // overscroll, with the fingers still on the touchpad. No snap-back animation
  // runs yet, since the gesture has not ended.
  EXPECT_TRUE(apzc->IsOverscrolled());
  EXPECT_FALSE(apzc->IsOverscrollAnimationRunning());

  // Send a second event in the block, which content now cancels. Every pan
  // event is dispatched to content as a wheel event and reports its own
  // response for the same block, and a preventDefault interrupts the block even
  // after an earlier response allowed it.
  mcc->AdvanceByMillis(10);
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 50),
             ScreenPoint(0, -20), mcc->Time());
  apzc->ContentReceivedInputBlock(result.mInputBlockId, true);
  EXPECT_TRUE(apzc->IsOverscrolled());

  // Send a zero-delta pan-end ending the gesture. This is the last chance to
  // relieve the overscroll.
  mcc->AdvanceByMillis(10);
  APZEventResult endResult =
      PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 50),
                 ScreenPoint(0, 0), mcc->Time(), MODIFIER_NONE,
                 /*aSimulateMomentum=*/true);

  // Content lets the pan-end event through. Note: use endResult's input block
  // id because the pan-end event creates a new input block since the previous
  // block has been interrupted.
  apzc->ConfirmTarget(endResult.mInputBlockId);
  apzc->ContentReceivedInputBlock(endResult.mInputBlockId, false);

  // Now the important assertion in the test: the pan-end should start an
  // overscroll animation that allows overscroll to be relieved.
  EXPECT_TRUE(apzc->IsOverscrollAnimationRunning());
  apzc->AdvanceAnimationsUntilEnd();
  EXPECT_FALSE(apzc->IsOverscrolled());
}
#endif

// A zero-delta pan-end that arrives without an active pan gesture block is
// turned into a synthesized block by InputQueue::ReceivePanGestureInput. Such a
// block stands for a gesture that has already ended, so it must not cancel a
// running scroll animation.
TEST_F(APZCPanningTester, StrayPanEndPreservesSmoothMsdScroll_Bug2033958) {
  FrameMetrics metrics = apzc->GetFrameMetrics();
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 1000));
  metrics.SetVisualScrollOffset(CSSPoint(0, 200));
  metrics.SetIsRootContent(true);
  apzc->SetFrameMetrics(metrics);

  // Request a main-thread driven smooth scroll back to the top, of the kind
  // scroll snapping uses.
  ScrollMetadata metadata = apzc->GetScrollMetadata();
  nsTArray<ScrollPositionUpdate> scrollUpdates;
  scrollUpdates.AppendElement(ScrollPositionUpdate::NewSmoothScroll(
      ScrollMode::SmoothMsd, ScrollOrigin::Other,
      CSSPoint::ToAppUnits(CSSPoint(0, 0)), ScrollTriggeredByScript::Yes,
      nullptr, ViewportType::Visual));
  metadata.SetScrollUpdates(scrollUpdates);
  metadata.GetMetrics().SetScrollGeneration(
      scrollUpdates.LastElement().GetGeneration());
  apzc->NotifyMainThreadTransaction(
      metadata, AsyncPanZoomController::LayersUpdateFlags{
                    .mIsFirstPaint = false, .mThisLayerTreeUpdated = true});

  apzc->AssertInSmoothMsdScroll();

  // Let the animation get partway to its destination.
  SampleAnimationOneFrame();
  float scrollYMidway = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
  EXPECT_LT(scrollYMidway, 200);
  EXPECT_GT(scrollYMidway, 0);

  // Send a pan-end with no preceding pan-start
  mcc->AdvanceByMillis(10);
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 50),
             ScreenPoint(0, 0), mcc->Time(), MODIFIER_NONE,
             /*aSimulateMomentum=*/true);

  // The animation should have survived, and should still reach its
  // destination.
  apzc->AssertInSmoothMsdScroll();
  apzc->AdvanceAnimationsUntilEnd();
  EXPECT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 0);
}

// When content preventDefaults every event of a pan gesture, the zero-delta
// pan-end that ends the gesture arrives after the gesture's block has been
// interrupted, and gets a synthesized block of its own. Content lets that
// block through, since a zero-delta event does not result in a DOM wheel event
// that could be prevented. The momentum events that follow belong to the
// prevented gesture, so they must not scroll.
TEST_F(APZCPanningTester, StrayPanEndBlockDoesNotTakeMomentum_Bug2086786) {
  MakeApzcWaitForMainThread();

  FrameMetrics metrics = apzc->GetFrameMetrics();
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 1000));
  metrics.SetVisualScrollOffset(CSSPoint(0, 200));
  metrics.SetIsRootContent(true);
  apzc->SetFrameMetrics(metrics);

  // Content prevents the pan-start, which interrupts its block.
  APZEventResult result =
      PanGesture(PanGestureInput::PANGESTURE_START, apzc,
                 ScreenIntPoint(50, 50), ScreenPoint(0, 20), mcc->Time());
  apzc->ConfirmTarget(result.mInputBlockId);
  apzc->ContentReceivedInputBlock(result.mInputBlockId, true);

  // The pan-update gets a new block, which content prevents as well.
  mcc->AdvanceByMillis(10);
  result = PanGesture(PanGestureInput::PANGESTURE_PAN, apzc,
                      ScreenIntPoint(50, 50), ScreenPoint(0, 20), mcc->Time());
  apzc->ConfirmTarget(result.mInputBlockId);
  apzc->ContentReceivedInputBlock(result.mInputBlockId, true);
  EXPECT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 200);

  // The zero-delta pan-end gets a synthesized block, which content lets
  // through.
  mcc->AdvanceByMillis(10);
  result = PanGesture(PanGestureInput::PANGESTURE_END, apzc,
                      ScreenIntPoint(50, 50), ScreenPoint(0, 0), mcc->Time());
  apzc->ConfirmTarget(result.mInputBlockId);
  apzc->ContentReceivedInputBlock(result.mInputBlockId, false);
  EXPECT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 200);

  // The momentum which follows should not scroll, even before content has had
  // a chance to prevent it.
  mcc->AdvanceByMillis(10);
  result = PanGesture(PanGestureInput::PANGESTURE_MOMENTUMSTART, apzc,
                      ScreenIntPoint(50, 50), ScreenPoint(0, 20), mcc->Time());
  EXPECT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 200);
  apzc->ContentReceivedInputBlock(result.mInputBlockId, true);

  mcc->AdvanceByMillis(10);
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 50), ScreenPoint(0, 20), mcc->Time());
  mcc->AdvanceByMillis(10);
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMEND, apzc,
             ScreenIntPoint(50, 50), ScreenPoint(0, 0), mcc->Time());
  EXPECT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 200);
}

class APZCPanningTesterMock : public APZCTreeManagerTester {
 public:
  APZCPanningTesterMock() { CreateMockHitTester(); }
};

TEST_F(APZCPanningTester, HoldGesture_HoldAndRelease) {
  // Send a pan gesture that triggers a fling animation at the end.
  PanWithFling();

  // Give the fling animation a chance to start.
  SampleAnimationOnce();
  apzc->AssertStateIsFling();

  // Send a PANGESTURE_MAYSTART event, signifying that the fingers went back
  // down on the touchpad.
  PanGesture(PanGestureInput::PANGESTURE_MAYSTART, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time());

  // This should have had the effect of cancelling the fling animation.
  apzc->AssertStateIsReset();

  // Send a PANGESTURE_CANCELLED event, signifying that the fingers have been
  // lifted without any scrolling. This should have no effect on the gesture
  // state.
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_CANCELLED, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 0), mcc->Time());
  apzc->AssertStateIsReset();
}

TEST_F(APZCPanningTester, HoldGesture_HoldAndScroll) {
  // Send a pan gesture that triggers a fling animation at the end.
  PanWithFling();

  // Give the fling animation a chance to start.
  SampleAnimationOnce();
  apzc->AssertStateIsFling();

  // Record the scroll offset before the fingers go back.
  float scrollYBefore = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
  EXPECT_GT(scrollYBefore, 0);

  // Send a PANGESTURE_MAYSTART event, signifying that the fingers went back
  // down on the touchpad.
  PanGesture(PanGestureInput::PANGESTURE_MAYSTART, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time());

  // This should have had the effect of cancelling the fling animation.
  apzc->AssertStateIsReset();

  // Do actual panning as part of the same gesture.
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_START, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time(), MODIFIER_NONE,
             /*aSimulateMomentum=*/true);

  // Check that we've done additional scrolling.
  float scrollYAfter = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
  EXPECT_GT(scrollYAfter, scrollYBefore);
}

TEST_F(APZCPanningTesterMock, HoldGesture_ActiveWheelListener) {
  // Explicitly set the content response timeout.
  // The value should be greater than the sum of the AdvanceByMillis()
  // intervals from the MAYSTART event until the content response.
  SCOPED_GFX_PREF_INT("apz.content_response_timeout", 100);

  CreateSimpleScrollingLayer();
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);
  UpdateHitTestingTree();

  RefPtr<TestAsyncPanZoomController> apzc = ApzcOf(root);
  ViewID scrollId = START_SCROLL_ID;
  ScreenIntPoint panPoint(50, 80);

  // Simulate an active wheel listener by having the MockHitTester
  // return eApzAwareListeners as the hit-test result for every event.
  gfx::CompositorHitTestInfo dispatchToContent{
      CompositorHitTestFlags::eVisibleToHitTest,
      CompositorHitTestFlags::eApzAwareListeners};

  // Send a MAYSTART. Note that this has zero delta and does not
  // result in an event sent to web content (so it will not itself
  // result in a content response).
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_MAYSTART, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());

#ifdef MOZ_WIDGET_GTK
  // On Linux, send a CANCELLED. This signifies the end of the hold gesture.
  // (On Mac the widget code goes directly to sending START.)
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_CANCELLED, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());
#endif

  // Send a START. This does result in an event sent to web
  // content if there is a nonzero delta.
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(scrollId, dispatchToContent);
  auto startResult = PanGesture(PanGestureInput::PANGESTURE_START, manager,
                                panPoint, ScreenPoint(0, 10), mcc->Time());

  // Send a couple of PAN events.
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, panPoint,
             ScreenPoint(0, 10), mcc->Time());
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, panPoint,
             ScreenPoint(0, 10), mcc->Time());

  // Simulate a content response which allows the scroll.
  manager->SetTargetAPZC(startResult.mInputBlockId, {startResult.mTargetGuid});
  manager->ContentReceivedInputBlock(startResult.mInputBlockId,
                                     /*aPreventDefault=*/false);

  // Check that we did scroll.
  // In the buggy scenario for which this test case is written,
  // the input block for the hold gesture was never confirmed,
  // stalling the input queue so that we don't scroll until that input
  // block is timed out.
  EXPECT_GT(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 0);

  // Clean up by sending an END event.
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_END, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());
  apzc->AssertStateIsReset();
}

TEST_F(APZCPanningTesterMock, HoldGesture_PreventDefaultAfterLongHold) {
  // Explicitly set a content response timeout.
  SCOPED_GFX_PREF_INT("apz.content_response_timeout", 20);

  CreateSimpleScrollingLayer();
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);
  UpdateHitTestingTree();

  RefPtr<TestAsyncPanZoomController> apzc = ApzcOf(root);
  ViewID scrollId = START_SCROLL_ID;
  ScreenIntPoint panPoint(50, 80);

  // Simulate an active wheel listener by having the MockHitTester
  // return eApzAwareListeners as the hit-test result for every event.
  gfx::CompositorHitTestInfo dispatchToContent{
      CompositorHitTestFlags::eVisibleToHitTest,
      CompositorHitTestFlags::eApzAwareListeners};

  // Send a MAYSTART. Note that this has zero delta and does not
  // result in an event sent to web content (so it will not itself
  // result in a content response).
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_MAYSTART, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());

  // Allow enough time to pass for the content response timeout to be reached.
  mcc->AdvanceByMillis(30);

#ifdef MOZ_WIDGET_GTK
  // On Linux, send a CANCELLED. This signifies the end of the hold gesture.
  // (On Mac the widget code goes directly to sending START.)
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_CANCELLED, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());
#endif

  // Send a START. This does result in an event sent to web
  // content if there is a nonzero delta.
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(scrollId, dispatchToContent);
  auto startResult = PanGesture(PanGestureInput::PANGESTURE_START, manager,
                                panPoint, ScreenPoint(0, 10), mcc->Time());

  // Send a couple of PAN events.
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, panPoint,
             ScreenPoint(0, 10), mcc->Time());
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, panPoint,
             ScreenPoint(0, 10), mcc->Time());

  // Simulate a content response which does NOT allow the scroll.
  manager->SetTargetAPZC(startResult.mInputBlockId, {startResult.mTargetGuid});
  manager->ContentReceivedInputBlock(startResult.mInputBlockId,
                                     /*aPreventDefault=*/true);

  // Check that we did NOT scroll.
  // In the buggy scenario for which this test case is written,
  // the hold gesture and the scroll go into the same input block,
  // for which the content response times out during the hold gesture,
  // and we don't wait for the content response for the scroll.
  EXPECT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 0);

  // Clean up by sending an END event.
  QueueMockHitResult(scrollId, dispatchToContent);
  PanGesture(PanGestureInput::PANGESTURE_END, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());
  apzc->AssertStateIsReset();
}

TEST_F(APZCPanningTesterMock, HoldGesture_SubframeTargeting) {
  // Set up a layer tree with a scrollable subframe handing off to a root frame.
  const char* treeShape = "x(x)";
  LayerIntRect layerVisibleRect[] = {
      LayerIntRect(0, 0, 100, 100),
      LayerIntRect(0, 0, 100, 100),
  };
  CreateScrollData(treeShape, layerVisibleRect);
  SetScrollableFrameMetrics(root, START_SCROLL_ID, CSSRect(0, 0, 100, 100));
  SetScrollableFrameMetrics(layers[1], START_SCROLL_ID + 1,
                            CSSRect(0, 0, 100, 200));
  SetScrollHandoff(layers[1], root);
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);
  UpdateHitTestingTree();

  auto* rootApzc = ApzcOf(root);
  auto* subframeApzc = ApzcOf(layers[1]);
  rootApzc->GetFrameMetrics().SetIsRootContent(true);

  // Mark the subframe as overscroll-behavior:none. This is important to
  // trigger the codepath in FindScrollTarget() that exposes the bug.
  subframeApzc->GetScrollMetadata().SetOverscrollBehavior(
      OverscrollBehaviorInfo::FromStyleConstants(
          StyleOverscrollBehavior::None, StyleOverscrollBehavior::None));

  ViewID subframeScrollId = START_SCROLL_ID + 1;
  ScreenIntPoint panPoint(50, 50);

  // Send a MAYSTART. Note that this has zero delta, and causes its input
  // block to be marked as having empty mAllowedScrollDirections because the
  // subframe fails the "can this APZC be scrolled by this event" check
  // and is overscroll-behavior:none.
  QueueMockHitResult(subframeScrollId,
                     CompositorHitTestFlags::eVisibleToHitTest);
  PanGesture(PanGestureInput::PANGESTURE_MAYSTART, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());

#ifdef MOZ_WIDGET_GTK
  // On Linux, send a CANCELLED. This signifies the end of the hold gesture.
  // (On Mac the widget code goes directly to sending START.)
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(subframeScrollId,
                     CompositorHitTestFlags::eVisibleToHitTest);
  PanGesture(PanGestureInput::PANGESTURE_CANCELLED, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());
#endif

  // Send a START. In the buggy scenario, this gets added to the same input
  // block as the MAYSTART, which has been marked as having empty
  // mAllowedScrollDirections, and thus fails to scroll anything.
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(subframeScrollId,
                     CompositorHitTestFlags::eVisibleToHitTest);
  PanGesture(PanGestureInput::PANGESTURE_START, manager, panPoint,
             ScreenPoint(0, 10), mcc->Time());

  // Check that the subframe scrolled.
  EXPECT_GT(subframeApzc->GetFrameMetrics().GetVisualScrollOffset().y, 0);

  // Clean up by sending an END event.
  QueueMockHitResult(subframeScrollId,
                     CompositorHitTestFlags::eVisibleToHitTest);
  PanGesture(PanGestureInput::PANGESTURE_END, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());
}

TEST_F(APZCPanningTester, HoldGesture_DuringAutoscrollAnimation) {
  // Tell APZ about the current mouse position. This is needed for
  // autoscroll to work correctly.
  tm->SetCurrentMousePosition(ScreenPoint(5, 5));

  // Start an autoscroll animation.
  apzc->StartAutoscroll(ScreenPoint(5, 5));
  apzc->AssertStateIsAutoscroll();

  // Send a PANGESTURE_MAYSTART event. With a touchpad, this can happen
  // when you start moving the cursor after a three-finger gesture to
  // start autoscroll.
  PanGesture(PanGestureInput::PANGESTURE_MAYSTART, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time());

  // Check that this did NOT cancel the autoscroll animation.
  apzc->AssertStateIsAutoscroll();
}

TEST_F(APZCPanningTester, Autoscroll_ScrollWheelCooldown) {
  auto cooldownMS = StaticPrefs::apz_autoscroll_scroll_wheel_cooldown();
  // Tell APZ about the current mouse position. This is needed for
  // autoscroll to work correctly.
  tm->SetCurrentMousePosition(ScreenPoint(5, 5));

  // Start an autoscroll animation.
  apzc->StartAutoscroll(ScreenPoint(5, 5));
  apzc->AssertStateIsAutoscroll();

  // Send a scroll wheel and check that this did NOT cancel the autoscroll
  // animation.
  Wheel(apzc, ScreenIntPoint(10, 10), ScreenPoint(0, 10), mcc->Time());
  apzc->AssertStateIsAutoscroll();

  // Advance to right before cooldown ends
  if (cooldownMS - 1 > 0) {
    mcc->AdvanceByMillis(cooldownMS - 1);
    // Send a scroll wheel and check that this did NOT cancel the autoscroll
    // animation.
    Wheel(apzc, ScreenIntPoint(10, 10), ScreenPoint(0, 10), mcc->Time());
    apzc->AssertStateIsAutoscroll();
  }
  // Advance the time to right after cooldown
  mcc->AdvanceByMillis(2);

  // Send a scroll wheel and check that this DID cancel the autoscroll
  // animation.
  Wheel(apzc, ScreenIntPoint(10, 10), ScreenPoint(0, 10), mcc->Time());
  apzc->AssertStateIsReset();
}

TEST_F(APZCDPITester, AutoScroll_DPITest) {
  // Start an autoscroll and assert it is active
  apzc->StartAutoscroll(ScreenPoint(5, 5));
  apzc->AssertStateIsAutoscroll();

  // Move the mouse to a position that would trigger the autoscroll at
  // a widget scale of 1.0F but should not at 2.0F (Calculated empirically)
  tm->SetCurrentMousePosition(ScreenPoint(29, 5));

  // Start the animation
  mcc->AdvanceByMillis(100);
  apzc->AdvanceAnimations(mcc->GetSampleTime());

  // Get the offset due to the autoscroll
  EXPECT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset(), CSSPoint(0, 0));

  // Move the mouse to a position that should autoscroll
  tm->SetCurrentMousePosition(ScreenPoint(30, 5));

  // Start the animation
  mcc->AdvanceByMillis(100);
  apzc->AdvanceAnimations(mcc->GetSampleTime());

  // Get the offset due to the autoscroll
  EXPECT_NE(apzc->GetFrameMetrics().GetVisualScrollOffset(), CSSPoint(0, 0));
}
