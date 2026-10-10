/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "APZCBasicTester.h"
#include "APZTestCommon.h"
#include "InputUtils.h"
#include "mozilla/StaticPrefs_apz.h"

class APZCPinchTester : public APZCBasicTester {
 private:
  // This (multiplied by apz.touch_start_tolerance) needs to be the hypotenuse
  // in a Pythagorean triple, along with overcomeTouchToleranceX and
  // overcomeTouchToleranceY from APZCTesterBase::Pan().
  // This is because APZCTesterBase::Pan(), when run without the
  // PanOptions::ExactCoordinates option, will need to first overcome the
  // touch start tolerance by performing a move of exactly
  // (apz.touch_start_tolerance * DPI) length.
  // When moving on both axes at once, we need to use integers for both legs
  // (overcomeTouchToleranceX and overcomeTouchToleranceY) while making sure
  // that the hypotenuse is also a round integer number (hence Pythagorean
  // triples). (The hypotenuse is the length of the movement in this case.)
  static const int mDPI = 100;

 public:
  explicit APZCPinchTester(
      AsyncPanZoomController::GestureBehavior aGestureBehavior =
          AsyncPanZoomController::DEFAULT_GESTURES)
      : APZCBasicTester(aGestureBehavior) {}

  void SetUp() override {
    APZCBasicTester::SetUp();
    tm->SetDPI(mDPI);
  }

 protected:
  FrameMetrics GetPinchableFrameMetrics() {
    FrameMetrics fm;
    fm.SetCompositionBounds(ParentLayerRect(0, 0, 100, 200));
    fm.SetScrollableRect(CSSRect(0, 0, 980, 1000));
    fm.SetVisualScrollOffset(CSSPoint(300, 300));
    fm.SetLayoutViewport(CSSRect(300, 300, 100, 200));
    fm.SetZoom(CSSToParentLayerScale(2.0));
    // APZC only allows zooming on the root scrollable frame.
    fm.SetIsRootContent(true);
    // the visible area of the document in CSS pixels is x=300 y=300 w=50 h=100
    return fm;
  }

  void DoPinchTest(bool aShouldTriggerPinch,
                   nsTArray<uint32_t>* aAllowedTouchBehaviors = nullptr) {
    apzc->SetFrameMetrics(GetPinchableFrameMetrics());
    MakeApzcZoomable();

    if (aShouldTriggerPinch) {
      // One repaint request for each gesture.
      EXPECT_CALL(*mcc, RequestContentRepaint(_)).Times(2);
    } else {
      EXPECT_CALL(*mcc, RequestContentRepaint(_)).Times(0);
    }

    int touchInputId = 0;
    if (mGestureBehavior == AsyncPanZoomController::USE_GESTURE_DETECTOR) {
      PinchWithTouchInputAndCheckStatus(apzc, ScreenIntPoint(250, 300), 1.25,
                                        touchInputId, aShouldTriggerPinch,
                                        aAllowedTouchBehaviors);
    } else {
      PinchWithPinchInputAndCheckStatus(apzc, ScreenIntPoint(250, 300), 1.25,
                                        aShouldTriggerPinch);
    }

    apzc->AssertStateIsReset();

    FrameMetrics fm = apzc->GetFrameMetrics();

    if (aShouldTriggerPinch) {
      // the visible area of the document in CSS pixels is now x=325 y=330 w=40
      // h=80
      EXPECT_EQ(2.5f, fm.GetZoom().scale);
      EXPECT_EQ(325, fm.GetVisualScrollOffset().x);
      EXPECT_EQ(330, fm.GetVisualScrollOffset().y);
    } else {
      // The frame metrics should stay the same since touch-action:none makes
      // apzc ignore pinch gestures.
      EXPECT_EQ(2.0f, fm.GetZoom().scale);
      EXPECT_EQ(300, fm.GetVisualScrollOffset().x);
      EXPECT_EQ(300, fm.GetVisualScrollOffset().y);
    }

    // part 2 of the test, move to the top-right corner of the page and pinch
    // and make sure we stay in the correct spot
    fm.SetZoom(CSSToParentLayerScale(2.0));
    fm.SetVisualScrollOffset(CSSPoint(930, 5));
    apzc->SetFrameMetrics(fm);
    // the visible area of the document in CSS pixels is x=930 y=5 w=50 h=100

    if (mGestureBehavior == AsyncPanZoomController::USE_GESTURE_DETECTOR) {
      PinchWithTouchInputAndCheckStatus(apzc, ScreenIntPoint(250, 300), 0.5,
                                        touchInputId, aShouldTriggerPinch,
                                        aAllowedTouchBehaviors);
    } else {
      PinchWithPinchInputAndCheckStatus(apzc, ScreenIntPoint(250, 300), 0.5,
                                        aShouldTriggerPinch);
    }

    apzc->AssertStateIsReset();

    fm = apzc->GetFrameMetrics();

    if (aShouldTriggerPinch) {
      // the visible area of the document in CSS pixels is now x=805 y=0 w=100
      // h=200
      EXPECT_EQ(1.0f, fm.GetZoom().scale);
      EXPECT_EQ(805, fm.GetVisualScrollOffset().x);
      EXPECT_EQ(0, fm.GetVisualScrollOffset().y);
    } else {
      EXPECT_EQ(2.0f, fm.GetZoom().scale);
      EXPECT_EQ(930, fm.GetVisualScrollOffset().x);
      EXPECT_EQ(5, fm.GetVisualScrollOffset().y);
    }
  }
};

class APZCPinchGestureDetectorTester : public APZCPinchTester {
 public:
  APZCPinchGestureDetectorTester()
      : APZCPinchTester(AsyncPanZoomController::USE_GESTURE_DETECTOR) {}

  void DoPinchWithPreventDefaultTest() {
    FrameMetrics originalMetrics = GetPinchableFrameMetrics();
    apzc->SetFrameMetrics(originalMetrics);

    MakeApzcWaitForMainThread();
    MakeApzcZoomable();

    uint64_t blockId = 0;
    PinchWithTouchInput(apzc, ScreenIntPoint(250, 300), 1.25,
                        PinchOptions().OutInputBlockId(&blockId));

    // Send the prevent-default notification for the touch block
    apzc->ContentReceivedInputBlock(blockId, true);

    // verify the metrics didn't change (i.e. the pinch was ignored)
    FrameMetrics fm = apzc->GetFrameMetrics();
    EXPECT_EQ(originalMetrics.GetZoom(), fm.GetZoom());
    EXPECT_EQ(originalMetrics.GetVisualScrollOffset().x,
              fm.GetVisualScrollOffset().x);
    EXPECT_EQ(originalMetrics.GetVisualScrollOffset().y,
              fm.GetVisualScrollOffset().y);

    apzc->AssertStateIsReset();
  }
};

class APZCPinchLockingTester : public APZCPinchTester {
 private:
  ScreenIntPoint mFocus;
  float mSpan;
  uint32_t mPinchLockBufferMaxAge;

 public:
  APZCPinchLockingTester()
      : APZCPinchTester(AsyncPanZoomController::USE_GESTURE_DETECTOR),
        mFocus(ScreenIntPoint(200, 300)),
        mSpan(10.0) {}

  virtual void SetUp() {
    mPinchLockBufferMaxAge =
        StaticPrefs::apz_pinch_lock_buffer_max_age_AtStartup();

    APZCPinchTester::SetUp();
    apzc->SetFrameMetrics(GetPinchableFrameMetrics());
    MakeApzcZoomable();

    auto event = CreatePinchGestureInput(PinchGestureInput::PINCHGESTURE_START,
                                         mFocus, mSpan, mSpan, mcc->Time());
    apzc->ReceiveInputEvent(event);
    mcc->AdvanceBy(TimeDuration::FromMilliseconds(mPinchLockBufferMaxAge + 1));
  }

  void twoFingerPan() {
    ScreenCoord panDistance =
        StaticPrefs::apz_pinch_lock_scroll_lock_threshold() * 1.2 *
        tm->GetDPI();

    mFocus = ScreenIntPoint((int)(mFocus.x.value + panDistance),
                            (int)(mFocus.y.value));

    auto event = CreatePinchGestureInput(PinchGestureInput::PINCHGESTURE_SCALE,
                                         mFocus, mSpan, mSpan, mcc->Time());
    apzc->ReceiveInputEvent(event);
    mcc->AdvanceBy(TimeDuration::FromMilliseconds(mPinchLockBufferMaxAge + 1));
  }

  void twoFingerZoom() {
    float pinchDistance =
        StaticPrefs::apz_pinch_lock_span_breakout_threshold() * 1.2 *
        tm->GetDPI();

    float newSpan = mSpan + pinchDistance;

    auto event = CreatePinchGestureInput(PinchGestureInput::PINCHGESTURE_SCALE,
                                         mFocus, newSpan, mSpan, mcc->Time());
    apzc->ReceiveInputEvent(event);
    mcc->AdvanceBy(TimeDuration::FromMilliseconds(mPinchLockBufferMaxAge + 1));
    mSpan = newSpan;
  }

  bool isPinchLockActive() {
    FrameMetrics originalMetrics = apzc->GetFrameMetrics();

    // Send a small scale input to the APZC
    float pinchDistance =
        StaticPrefs::apz_pinch_lock_span_breakout_threshold() * 0.8 *
        tm->GetDPI();
    auto event =
        CreatePinchGestureInput(PinchGestureInput::PINCHGESTURE_SCALE, mFocus,
                                mSpan + pinchDistance, mSpan, mcc->Time());
    apzc->ReceiveInputEvent(event);

    FrameMetrics result = apzc->GetFrameMetrics();
    bool lockActive = originalMetrics.GetZoom() == result.GetZoom() &&
                      originalMetrics.GetVisualScrollOffset().x ==
                          result.GetVisualScrollOffset().x &&
                      originalMetrics.GetVisualScrollOffset().y ==
                          result.GetVisualScrollOffset().y;

    // Avoid side effects, reset to original frame metrics
    apzc->SetFrameMetrics(originalMetrics);
    return lockActive;
  }
};

TEST_F(APZCPinchGestureDetectorTester,
       Pinch_UseGestureDetector_TouchActionNone) {
  nsTArray<uint32_t> behaviors = {mozilla::layers::AllowedTouchBehavior::NONE,
                                  mozilla::layers::AllowedTouchBehavior::NONE};
  DoPinchTest(false, &behaviors);
}

TEST_F(APZCPinchGestureDetectorTester,
       Pinch_UseGestureDetector_TouchActionZoom) {
  nsTArray<uint32_t> behaviors;
  behaviors.AppendElement(mozilla::layers::AllowedTouchBehavior::PINCH_ZOOM);
  behaviors.AppendElement(mozilla::layers::AllowedTouchBehavior::PINCH_ZOOM);
  DoPinchTest(true, &behaviors);
}

TEST_F(APZCPinchGestureDetectorTester,
       Pinch_UseGestureDetector_TouchActionNotAllowZoom) {
  nsTArray<uint32_t> behaviors;
  behaviors.AppendElement(mozilla::layers::AllowedTouchBehavior::NONE);
  behaviors.AppendElement(mozilla::layers::AllowedTouchBehavior::PINCH_ZOOM);
  DoPinchTest(false, &behaviors);
}

TEST_F(APZCPinchGestureDetectorTester,
       Pinch_UseGestureDetector_TouchActionNone_NoAPZZoom) {
  SCOPED_GFX_PREF_BOOL("apz.allow_zooming", false);

  // Since we are preventing the pinch action via touch-action we should not be
  // sending the pinch gesture notifications that would normally be sent when
  // apz_allow_zooming is false.
  EXPECT_CALL(*mcc, NotifyPinchGesture(_, _, _, _, _)).Times(0);
  nsTArray<uint32_t> behaviors = {mozilla::layers::AllowedTouchBehavior::NONE,
                                  mozilla::layers::AllowedTouchBehavior::NONE};
  DoPinchTest(false, &behaviors);
}

TEST_F(
    APZCPinchGestureDetectorTester,
    Pinch_UseGestureDetector_TouchActionPanY_APZZoom_When_ForceUserScalable) {
  SCOPED_GFX_PREF_BOOL("browser.ui.zoom.force-user-scalable", true);

  nsTArray<uint32_t> behaviors = {
      mozilla::layers::AllowedTouchBehavior::VERTICAL_PAN,
      mozilla::layers::AllowedTouchBehavior::VERTICAL_PAN};
  DoPinchTest(true, &behaviors);
}

TEST_F(
    APZCPinchGestureDetectorTester,
    Pinch_UseGestureDetector_TouchActionNone_NoAPZZoom_When_ForceUserScalable) {
  SCOPED_GFX_PREF_BOOL("browser.ui.zoom.force-user-scalable", true);

  EXPECT_CALL(*mcc, NotifyPinchGesture(_, _, _, _, _)).Times(0);
  nsTArray<uint32_t> behaviors = {
      {mozilla::layers::AllowedTouchBehavior::NONE,
       mozilla::layers::AllowedTouchBehavior::NONE},
  };
  DoPinchTest(false, &behaviors);
}

TEST_F(APZCPinchGestureDetectorTester, Pinch_PreventDefault) {
  DoPinchWithPreventDefaultTest();
}

TEST_F(APZCPinchGestureDetectorTester, Pinch_PreventDefault_NoAPZZoom) {
  SCOPED_GFX_PREF_BOOL("apz.allow_zooming", false);

  // Since we are preventing the pinch action we should not be sending the pinch
  // gesture notifications that would normally be sent when apz_allow_zooming is
  // false.
  EXPECT_CALL(*mcc, NotifyPinchGesture(_, _, _, _, _)).Times(0);

  DoPinchWithPreventDefaultTest();
}

TEST_F(APZCPinchGestureDetectorTester, Panning_TwoFingerFling_ZoomDisabled) {
  SCOPED_GFX_PREF_FLOAT("apz.fling_min_velocity_threshold", 0.0f);

  apzc->SetFrameMetrics(GetPinchableFrameMetrics());
  MakeApzcUnzoomable();

  // Perform a two finger pan
  PinchWithTouchInput(apzc, ScreenIntPoint(100, 200), 1,
                      PinchOptions().SecondFocus(ScreenIntPoint(100, 100)));

  // Expect to be in a flinging state
  apzc->AssertStateIsFling();
}

TEST_F(APZCPinchGestureDetectorTester, Pinch_DoesntFling_ZoomDisabled) {
  SCOPED_GFX_PREF_FLOAT("apz.fling_min_velocity_threshold", 0.0f);

  apzc->SetFrameMetrics(GetPinchableFrameMetrics());
  MakeApzcUnzoomable();

  // Perform a pinch
  PinchWithTouchInput(apzc, ScreenIntPoint(100, 200), 2,
                      PinchOptions()
                          .Flags(PinchFlags::LiftFinger2)
                          .Vertical(true)
                          .SecondFocus(ScreenIntPoint(100, 100)));

  // Lift second finger after a pause
  mcc->AdvanceBy(TimeDuration::FromMilliseconds(50));
  TouchUp(apzc, ScreenIntPoint(100, 100), mcc->Time());

  // Pinch should not trigger a fling
  EXPECT_EQ(apzc->GetVelocityVector().y, 0);
}

TEST_F(APZCPinchGestureDetectorTester, Panning_TwoFingerFling_ZoomEnabled) {
  SCOPED_GFX_PREF_FLOAT("apz.fling_min_velocity_threshold", 0.0f);

  apzc->SetFrameMetrics(GetPinchableFrameMetrics());
  MakeApzcZoomable();

  // Perform a two finger pan
  PinchWithTouchInput(apzc, ScreenIntPoint(100, 200), 1,
                      PinchOptions().SecondFocus(ScreenIntPoint(100, 100)));

  // Expect to NOT be in flinging state
  apzc->AssertStateIsReset();
}

TEST_F(APZCPinchGestureDetectorTester,
       Panning_TwoThenOneFingerFling_ZoomEnabled) {
  SCOPED_GFX_PREF_FLOAT("apz.fling_min_velocity_threshold", 0.0f);

  apzc->SetFrameMetrics(GetPinchableFrameMetrics());
  MakeApzcZoomable();

  // Perform a two finger pan lifting only the first finger
  PinchWithTouchInput(apzc, ScreenIntPoint(100, 200), 1,
                      PinchOptions()
                          .Flags(PinchFlags::LiftFinger2)
                          .SecondFocus(ScreenIntPoint(100, 100)));

  // Lift second finger after a pause
  mcc->AdvanceBy(TimeDuration::FromMilliseconds(50));
  TouchUp(apzc, ScreenIntPoint(100, 100), mcc->Time());

  // This gesture should activate the pinch lock, and result
  // in a fling even if the page is zoomable.
  apzc->AssertStateIsFling();
}

TEST_F(APZCPinchGestureDetectorTester,
       Panning_TwoThenOneFingerFling_ZoomDisabled) {
  SCOPED_GFX_PREF_FLOAT("apz.fling_min_velocity_threshold", 0.0f);

  apzc->SetFrameMetrics(GetPinchableFrameMetrics());
  MakeApzcUnzoomable();

  // Perform a two finger pan lifting only the first finger
  PinchWithTouchInput(apzc, ScreenIntPoint(100, 200), 1,
                      PinchOptions()
                          .Flags(PinchFlags::LiftFinger2)
                          .SecondFocus(ScreenIntPoint(100, 100)));

  // Lift second finger after a pause
  mcc->AdvanceBy(TimeDuration::FromMilliseconds(50));
  TouchUp(apzc, ScreenIntPoint(100, 100), mcc->Time());

  // This gesture should activate the pinch lock and result in a fling
  apzc->AssertStateIsFling();
}

TEST_F(APZCPinchTester, Panning_TwoFinger_ZoomDisabled) {
  // set up APZ
  apzc->SetFrameMetrics(GetPinchableFrameMetrics());
  MakeApzcUnzoomable();

  nsEventStatus statuses[3];  // scalebegin, scale, scaleend
  PinchWithPinchInput(apzc, ScreenIntPoint(250, 350), ScreenIntPoint(200, 300),
                      10, &statuses);

  FrameMetrics fm = apzc->GetFrameMetrics();

  // It starts from (300, 300), then moves the focus point from (250, 350) to
  // (200, 300) pans by (50, 50) screen pixels, but there is a 2x zoom, which
  // causes the scroll offset to change by half of that (25, 25) pixels.
  EXPECT_EQ(325, fm.GetVisualScrollOffset().x);
  EXPECT_EQ(325, fm.GetVisualScrollOffset().y);
  EXPECT_EQ(2.0, fm.GetZoom().scale);
}

TEST_F(APZCPinchTester, Panning_Beyond_LayoutViewport) {
  SCOPED_GFX_PREF_INT("apz.axis_lock.mode", 0);

  apzc->SetFrameMetrics(GetPinchableFrameMetrics());
  MakeApzcZoomable();

  // Case 1 - visual viewport is still inside layout viewport.
  Pan(apzc, 350, 300, PanOptions::NoFling);
  FrameMetrics fm = apzc->GetFrameMetrics();
  // It starts from (300, 300) pans by (0, 50) screen pixels, but there is a
  // 2x zoom, which causes the scroll offset to change by half of that (0, 25).
  // But the visual viewport is still inside the layout viewport.
  EXPECT_EQ(300, fm.GetVisualScrollOffset().x);
  EXPECT_EQ(325, fm.GetVisualScrollOffset().y);
  EXPECT_EQ(300, fm.GetLayoutViewport().X());
  EXPECT_EQ(300, fm.GetLayoutViewport().Y());

  // Case 2 - visual viewport crosses the bottom boundary of the layout
  // viewport.
  Pan(apzc, 525, 325, PanOptions::NoFling);
  fm = apzc->GetFrameMetrics();
  // It starts from (300, 325) pans by (0, 200) screen pixels, but there is a
  // 2x zoom, which causes the scroll offset to change by half of that
  // (0, 100). The visual viewport crossed the bottom boundary of the layout
  // viewport by 25px.
  EXPECT_EQ(300, fm.GetVisualScrollOffset().x);
  EXPECT_EQ(425, fm.GetVisualScrollOffset().y);
  EXPECT_EQ(300, fm.GetLayoutViewport().X());
  EXPECT_EQ(325, fm.GetLayoutViewport().Y());

  // Case 3 - visual viewport crosses the top boundary of the layout viewport.
  Pan(apzc, 425, 775, PanOptions::NoFling);
  fm = apzc->GetFrameMetrics();
  // It starts from (300, 425) pans by (0, -350) screen pixels, but there is a
  // 2x zoom, which causes the scroll offset to change by half of that
  // (0, -175). The visual viewport crossed the top of the layout viewport by
  // 75px.
  EXPECT_EQ(300, fm.GetVisualScrollOffset().x);
  EXPECT_EQ(250, fm.GetVisualScrollOffset().y);
  EXPECT_EQ(300, fm.GetLayoutViewport().X());
  EXPECT_EQ(250, fm.GetLayoutViewport().Y());

  // Case 4 - visual viewport crosses the left boundary of the layout viewport.
  Pan(apzc, ScreenIntPoint(150, 10), ScreenIntPoint(350, 10),
      PanOptions::NoFling);
  fm = apzc->GetFrameMetrics();
  // It starts from (300, 250) pans by (-200, 0) screen pixels, but there is a
  // 2x zoom, which causes the scroll offset to change by half of that
  // (-100, 0). The visual viewport crossed the left boundary of the layout
  // viewport by 100px.
  EXPECT_EQ(200, fm.GetVisualScrollOffset().x);
  EXPECT_EQ(250, fm.GetVisualScrollOffset().y);
  EXPECT_EQ(200, fm.GetLayoutViewport().X());
  EXPECT_EQ(250, fm.GetLayoutViewport().Y());

  // Case 5 - visual viewport crosses the right boundary of the layout viewport.
  Pan(apzc, ScreenIntPoint(350, 10), ScreenIntPoint(150, 10),
      PanOptions::NoFling);
  fm = apzc->GetFrameMetrics();
  // It starts from (200, 250) pans by (200, 0) screen pixels, but there is a
  // 2x zoom, which causes the scroll offset to change by half of that
  // (100, 0). The visual viewport crossed the right boundary of the layout
  // viewport by 50px.
  EXPECT_EQ(300, fm.GetVisualScrollOffset().x);
  EXPECT_EQ(250, fm.GetVisualScrollOffset().y);
  EXPECT_EQ(250, fm.GetLayoutViewport().X());
  EXPECT_EQ(250, fm.GetLayoutViewport().Y());

  // Case 6 - visual viewport crosses both the vertical and horizontal
  // boundaries of the layout viewport by moving diagonally towards the
  // top-right corner.
  Pan(apzc, ScreenIntPoint(350, 200), ScreenIntPoint(150, 400),
      PanOptions::NoFling);
  fm = apzc->GetFrameMetrics();
  // It starts from (300, 250) pans by (200, -200) screen pixels, but there is
  // a 2x zoom, which causes the scroll offset to change by half of that
  // (100, -100). The visual viewport moved by (100, -100) outside the
  // boundary of the layout viewport.
  EXPECT_EQ(400, fm.GetVisualScrollOffset().x);
  EXPECT_EQ(150, fm.GetVisualScrollOffset().y);
  EXPECT_EQ(350, fm.GetLayoutViewport().X());
  EXPECT_EQ(150, fm.GetLayoutViewport().Y());
}

TEST_F(APZCPinchGestureDetectorTester, Pinch_APZZoom_Disabled) {
  SCOPED_GFX_PREF_BOOL("apz.allow_zooming", false);

  FrameMetrics originalMetrics = GetPinchableFrameMetrics();
  apzc->SetFrameMetrics(originalMetrics);

  // When apz_allow_zooming is false, the ZoomConstraintsClient produces
  // ZoomConstraints with mAllowZoom set to false.
  MakeApzcUnzoomable();

  // With apz_allow_zooming false, we expect the NotifyPinchGesture function to
  // get called as the pinch progresses, but the metrics shouldn't change.
  EXPECT_CALL(*mcc,
              NotifyPinchGesture(PinchGestureInput::PINCHGESTURE_START,
                                 apzc->GetGuid(), _, LayoutDeviceCoord(0), _))
      .Times(1);
  EXPECT_CALL(*mcc, NotifyPinchGesture(PinchGestureInput::PINCHGESTURE_SCALE,
                                       apzc->GetGuid(), _, _, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*mcc,
              NotifyPinchGesture(PinchGestureInput::PINCHGESTURE_END,
                                 apzc->GetGuid(), _, LayoutDeviceCoord(0), _))
      .Times(1);

  PinchWithTouchInput(apzc, ScreenIntPoint(250, 300), 1.25);

  // verify the metrics didn't change (i.e. the pinch was ignored inside APZ)
  FrameMetrics fm = apzc->GetFrameMetrics();
  EXPECT_EQ(originalMetrics.GetZoom(), fm.GetZoom());
  EXPECT_EQ(originalMetrics.GetVisualScrollOffset().x,
            fm.GetVisualScrollOffset().x);
  EXPECT_EQ(originalMetrics.GetVisualScrollOffset().y,
            fm.GetVisualScrollOffset().y);

  apzc->AssertStateIsReset();
}

TEST_F(APZCPinchGestureDetectorTester, Pinch_NoSpan) {
  SCOPED_GFX_PREF_BOOL("apz.allow_zooming", false);

  FrameMetrics originalMetrics = GetPinchableFrameMetrics();
  apzc->SetFrameMetrics(originalMetrics);

  // When apz_allow_zooming is false, the ZoomConstraintsClient produces
  // ZoomConstraints with mAllowZoom set to false.
  MakeApzcUnzoomable();

  // With apz_allow_zooming false, we expect the NotifyPinchGesture function to
  // get called as the pinch progresses, but the metrics shouldn't change.
  EXPECT_CALL(*mcc,
              NotifyPinchGesture(PinchGestureInput::PINCHGESTURE_START,
                                 apzc->GetGuid(), _, LayoutDeviceCoord(0), _))
      .Times(1);
  EXPECT_CALL(*mcc, NotifyPinchGesture(PinchGestureInput::PINCHGESTURE_SCALE,
                                       apzc->GetGuid(), _, _, _))
      .Times(AtLeast(1));
  EXPECT_CALL(*mcc,
              NotifyPinchGesture(PinchGestureInput::PINCHGESTURE_END,
                                 apzc->GetGuid(), _, LayoutDeviceCoord(0), _))
      .Times(1);

  int inputId = 0;
  ScreenIntPoint focus(250, 300);

  // Do a pinch holding a zero span and moving the focus by y=100

  const TimeDuration TIME_BETWEEN_TOUCH_EVENT =
      TimeDuration::FromMilliseconds(50);
  const auto touchBehaviors = Some(nsTArray<uint32_t>{kDefaultTouchBehavior});

  MultiTouchInput mtiStart =
      MultiTouchInput(MultiTouchInput::MULTITOUCH_START, 0, mcc->Time(), 0);
  mtiStart.mTouches.AppendElement(CreateSingleTouchData(inputId, focus));
  mtiStart.mTouches.AppendElement(CreateSingleTouchData(inputId + 1, focus));
  apzc->ReceiveInputEvent(mtiStart, touchBehaviors);
  mcc->AdvanceBy(TIME_BETWEEN_TOUCH_EVENT);

  focus.y -= 35 + 1;  // this is to get over the PINCH_START_THRESHOLD in
                      // GestureEventListener.cpp
  MultiTouchInput mtiMove1 =
      MultiTouchInput(MultiTouchInput::MULTITOUCH_MOVE, 0, mcc->Time(), 0);
  mtiMove1.mTouches.AppendElement(CreateSingleTouchData(inputId, focus));
  mtiMove1.mTouches.AppendElement(CreateSingleTouchData(inputId + 1, focus));
  apzc->ReceiveInputEvent(mtiMove1);
  mcc->AdvanceBy(TIME_BETWEEN_TOUCH_EVENT);

  focus.y -= 100;  // do a two-finger scroll of 100 screen pixels
  MultiTouchInput mtiMove2 =
      MultiTouchInput(MultiTouchInput::MULTITOUCH_MOVE, 0, mcc->Time(), 0);
  mtiMove2.mTouches.AppendElement(CreateSingleTouchData(inputId, focus));
  mtiMove2.mTouches.AppendElement(CreateSingleTouchData(inputId + 1, focus));
  apzc->ReceiveInputEvent(mtiMove2);
  mcc->AdvanceBy(TIME_BETWEEN_TOUCH_EVENT);

  MultiTouchInput mtiEnd =
      MultiTouchInput(MultiTouchInput::MULTITOUCH_END, 0, mcc->Time(), 0);
  mtiEnd.mTouches.AppendElement(CreateSingleTouchData(inputId, focus));
  mtiEnd.mTouches.AppendElement(CreateSingleTouchData(inputId + 1, focus));
  apzc->ReceiveInputEvent(mtiEnd);

  // Done, check the metrics to make sure we scrolled by 100 screen pixels,
  // which is 50 CSS pixels for the pinchable frame metrics.

  FrameMetrics fm = apzc->GetFrameMetrics();
  EXPECT_EQ(originalMetrics.GetZoom(), fm.GetZoom());
  EXPECT_EQ(originalMetrics.GetVisualScrollOffset().x,
            fm.GetVisualScrollOffset().x);
  EXPECT_EQ(originalMetrics.GetVisualScrollOffset().y + 50,
            fm.GetVisualScrollOffset().y);

  apzc->AssertStateIsReset();
}

TEST_F(APZCPinchTester, Pinch_TwoFinger_APZZoom_Disabled_Bug1354185) {
  // Set up APZ such that mZoomConstraints.mAllowZoom is false.
  SCOPED_GFX_PREF_BOOL("apz.allow_zooming", false);
  apzc->SetFrameMetrics(GetPinchableFrameMetrics());
  MakeApzcUnzoomable();

  // We expect a repaint request for scrolling.
  EXPECT_CALL(*mcc, RequestContentRepaint(_)).Times(1);

  // Send only the PINCHGESTURE_START and PINCHGESTURE_SCALE events,
  // in order to trigger a call to AsyncPanZoomController::OnScale
  // but not to AsyncPanZoomController::OnScaleEnd.
  ScreenIntPoint aFocus(250, 350);
  ScreenIntPoint aSecondFocus(200, 300);
  float aScale = 10;
  auto event = CreatePinchGestureInput(PinchGestureInput::PINCHGESTURE_START,
                                       aFocus, 10.0, 10.0, mcc->Time());
  apzc->ReceiveInputEvent(event);

  event =
      CreatePinchGestureInput(PinchGestureInput::PINCHGESTURE_SCALE,
                              aSecondFocus, 10.0f * aScale, 10.0, mcc->Time());
  apzc->ReceiveInputEvent(event);
}

TEST_F(APZCPinchLockingTester, Pinch_Locking_Free) {
  SCOPED_GFX_PREF_INT("apz.pinch_lock.mode", 0);  // PINCH_FREE

  twoFingerPan();
  EXPECT_FALSE(isPinchLockActive());
}

TEST_F(APZCPinchLockingTester, Pinch_Locking_Normal_Lock) {
  SCOPED_GFX_PREF_INT("apz.pinch_lock.mode", 1);  // PINCH_NORMAL

  twoFingerPan();
  EXPECT_TRUE(isPinchLockActive());
}

TEST_F(APZCPinchLockingTester, Pinch_Locking_Normal_Lock_Break) {
  SCOPED_GFX_PREF_INT("apz.pinch_lock.mode", 1);  // PINCH_NORMAL

  twoFingerPan();
  twoFingerZoom();
  EXPECT_TRUE(isPinchLockActive());
}

TEST_F(APZCPinchLockingTester, Pinch_Locking_Sticky_Lock) {
  SCOPED_GFX_PREF_INT("apz.pinch_lock.mode", 2);  // PINCH_STICKY

  twoFingerPan();
  EXPECT_TRUE(isPinchLockActive());
}

TEST_F(APZCPinchLockingTester, Pinch_Locking_Sticky_Lock_Break) {
  SCOPED_GFX_PREF_INT("apz.pinch_lock.mode", 2);  // PINCH_STICKY

  twoFingerPan();
  twoFingerZoom();
  EXPECT_FALSE(isPinchLockActive());
}

TEST_F(APZCPinchLockingTester, Pinch_Locking_Sticky_Lock_Break_Lock) {
  SCOPED_GFX_PREF_INT("apz.pinch_lock.mode", 2);  // PINCH_STICKY

  twoFingerPan();
  twoFingerZoom();
  twoFingerPan();
  EXPECT_TRUE(isPinchLockActive());
}
