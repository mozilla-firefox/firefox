/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "APZCTreeManagerTester.h"
#include "APZTestCommon.h"
#include "InputUtils.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_mousewheel.h"

class APZCSnappingTesterMock : public APZCTreeManagerTester {
 public:
  APZCSnappingTesterMock() { CreateMockHitTester(); }
};

TEST_F(APZCSnappingTesterMock, Bug1265510) {
  // Needed because the test uses SmoothWheel()
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);

  const char* treeShape = "x(x)";
  LayerIntRect layerVisibleRect[] = {LayerIntRect(0, 0, 100, 100),
                                     LayerIntRect(0, 100, 100, 100)};
  CreateScrollData(treeShape, layerVisibleRect);
  SetScrollableFrameMetrics(root, START_SCROLL_ID, CSSRect(0, 0, 100, 200));
  SetScrollableFrameMetrics(layers[1], START_SCROLL_ID + 1,
                            CSSRect(0, 0, 100, 200));
  SetScrollHandoff(layers[1], root);

  ScrollSnapInfo snap;
  snap.mScrollSnapStrictnessY = StyleScrollSnapStrictness::Mandatory;
  snap.mSnapportSize =
      CSSSize::ToAppUnits(layerVisibleRect[0].Size() * LayerToCSSScale(1.0));

  snap.mSnapTargets.AppendElement(ScrollSnapInfo::SnapTarget(
      Nothing(), Some(0 * AppUnitsPerCSSPixel()),
      CSSRect::ToAppUnits(CSSRect(0, 0, 10, 10)), StyleScrollSnapStop::Normal,
      ScrollSnapTargetId{1}));
  snap.mSnapTargets.AppendElement(ScrollSnapInfo::SnapTarget(
      Nothing(), Some(100 * AppUnitsPerCSSPixel()),
      CSSRect::ToAppUnits(CSSRect(0, 100, 10, 10)), StyleScrollSnapStop::Normal,
      ScrollSnapTargetId{2}));

  ModifyFrameMetrics(root, [&](ScrollMetadata& aSm, FrameMetrics&) {
    aSm.SetSnapInfo(ScrollSnapInfo(snap));
  });

  auto registration =
      std::make_unique<ScopedLayerTreeRegistration>(LayersId{0}, mcc);
  UpdateHitTestingTree();

  TestAsyncPanZoomController* outer = ApzcOf(layers[0]);
  TestAsyncPanZoomController* inner = ApzcOf(layers[1]);

  // Position the mouse near the bottom of the outer frame and scroll by 60px.
  // (6 lines of 10px each). APZC will actually scroll to y=100 because of the
  // mandatory snap coordinate there.
  QueueMockHitResult(START_SCROLL_ID);
  SmoothWheel(manager, ScreenIntPoint(50, 80), ScreenPoint(0, 6), mcc->Time());
  // Advance in 5ms increments until we've scrolled by 70px. At this point, the
  // closest snap point is y=100, and the inner frame should be under the mouse
  // cursor.
  while (outer
             ->GetCurrentAsyncScrollOffset(
                 AsyncTransformConsumer::eForEventHandling)
             .y < 70) {
    mcc->AdvanceByMillis(5);
    outer->AdvanceAnimations(mcc->GetSampleTime());
  }
  // Now do another wheel in a new transaction. This should start scrolling the
  // inner frame; we verify that it does by checking the inner scroll position.
  mcc->AdvanceBy(TimeDuration::FromMilliseconds(
      StaticPrefs::mousewheel_transaction_timeout() + 100));
  QueueMockHitResult(START_SCROLL_ID + 1);
  SmoothWheel(manager, ScreenIntPoint(50, 80), ScreenPoint(0, 6), mcc->Time());
  mcc->AdvanceByMillis(5);
  inner->AdvanceAnimationsUntilEnd();
  EXPECT_LT(0.0f, inner
                      ->GetCurrentAsyncScrollOffset(
                          AsyncTransformConsumer::eForEventHandling)
                      .y);

  // However, the outer frame should also continue to the snap point, otherwise
  // it is demonstrating incorrect behaviour by violating the mandatory
  // snapping.
  outer->AdvanceAnimationsUntilEnd();
  EXPECT_EQ(100.0f, outer
                        ->GetCurrentAsyncScrollOffset(
                            AsyncTransformConsumer::eForEventHandling)
                        .y);
}

TEST_F(APZCSnappingTesterMock, Snap_After_Pinch) {
  const char* treeShape = "x";
  LayerIntRect layerVisibleRect[] = {
      LayerIntRect(0, 0, 100, 100),
  };
  CreateScrollData(treeShape, layerVisibleRect);
  SetScrollableFrameMetrics(root, START_SCROLL_ID, CSSRect(0, 0, 100, 200));

  // Set up some basic scroll snapping
  ScrollSnapInfo snap;
  snap.mScrollSnapStrictnessY = StyleScrollSnapStrictness::Mandatory;
  snap.mSnapportSize =
      CSSSize::ToAppUnits(layerVisibleRect[0].Size() * LayerToCSSScale(1.0));

  snap.mSnapTargets.AppendElement(ScrollSnapInfo::SnapTarget(
      Nothing(), Some(0 * AppUnitsPerCSSPixel()),
      CSSRect::ToAppUnits(CSSRect(0, 0, 10, 10)), StyleScrollSnapStop::Normal,
      ScrollSnapTargetId{1}));
  snap.mSnapTargets.AppendElement(ScrollSnapInfo::SnapTarget(
      Nothing(), Some(100 * AppUnitsPerCSSPixel()),
      CSSRect::ToAppUnits(CSSRect(0, 100, 10, 10)), StyleScrollSnapStop::Normal,
      ScrollSnapTargetId{2}));

  // Save the scroll snap info on the root APZC.
  // Also mark the root APZC as "root content", since APZC only allows
  // zooming on the root content APZC.
  ModifyFrameMetrics(root, [&](ScrollMetadata& aSm, FrameMetrics& aMetrics) {
    aSm.SetSnapInfo(ScrollSnapInfo(snap));
    aMetrics.SetIsRootContent(true);
  });

  auto registration =
      std::make_unique<ScopedLayerTreeRegistration>(LayersId{0}, mcc);
  UpdateHitTestingTree();

  RefPtr<TestAsyncPanZoomController> apzc = ApzcOf(root);

  // Allow zooming
  apzc->UpdateZoomConstraints(ZoomConstraints(
      true, true, CSSToParentLayerScale(0.25f), CSSToParentLayerScale(4.0f)));

  PinchWithPinchInput(apzc, ScreenIntPoint(50, 50), ScreenIntPoint(50, 50),
                      1.2f);

  apzc->AssertInSmoothMsdScroll();
}

// Currently fails on Android because on the platform we have a different
// VelocityTracker.
#ifndef MOZ_WIDGET_ANDROID
TEST_F(APZCSnappingTesterMock, SnapOnPanEndWithZeroVelocity) {
  // Use pref values for desktop everywhere.
  SCOPED_GFX_PREF_FLOAT("apz.fling_friction", 0.002);
  SCOPED_GFX_PREF_FLOAT("apz.fling_stopped_threshold", 0.01);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_x1", 0.0);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_x2", 1.0);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_y1", 0.0);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_y2", 1.0);
  SCOPED_GFX_PREF_INT("apz.velocity_relevance_time_ms", 100);

  const char* treeShape = "x";
  LayerIntRect layerVisibleRect[] = {
      LayerIntRect(0, 0, 100, 100),
  };
  CreateScrollData(treeShape, layerVisibleRect);
  SetScrollableFrameMetrics(root, START_SCROLL_ID, CSSRect(0, 0, 100, 400));

  // Set up two snap points, 30 and 100.
  ScrollSnapInfo snap;
  snap.mScrollSnapStrictnessY = StyleScrollSnapStrictness::Mandatory;
  snap.mSnapportSize =
      CSSSize::ToAppUnits(layerVisibleRect[0].Size() * LayerToCSSScale(1.0));
  snap.mSnapTargets.AppendElement(ScrollSnapInfo::SnapTarget(
      Nothing(), Some(30 * AppUnitsPerCSSPixel()),
      CSSRect::ToAppUnits(CSSRect(0, 30, 10, 10)), StyleScrollSnapStop::Normal,
      ScrollSnapTargetId{1}));
  snap.mSnapTargets.AppendElement(ScrollSnapInfo::SnapTarget(
      Nothing(), Some(100 * AppUnitsPerCSSPixel()),
      CSSRect::ToAppUnits(CSSRect(0, 100, 10, 10)), StyleScrollSnapStop::Normal,
      ScrollSnapTargetId{2}));

  // Save the scroll snap info on the root APZC.
  ModifyFrameMetrics(root, [&](ScrollMetadata& aSm, FrameMetrics& aMetrics) {
    aSm.SetSnapInfo(ScrollSnapInfo(snap));
  });

  auto registration =
      std::make_unique<ScopedLayerTreeRegistration>(LayersId{0}, mcc);
  UpdateHitTestingTree();

  RefPtr<TestAsyncPanZoomController> apzc = ApzcOf(root);

  // Send a series of pan gestures to scroll to position at 50.
  const ScreenIntPoint position = ScreenIntPoint(50, 30);
  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_START, manager, position,
             ScreenPoint(0, 10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, position,
             ScreenPoint(0, 40), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());

  // Make sure the velocity just before sending a pan-end is zero.
  EXPECT_EQ(apzc->GetVelocityVector().y, 0);

  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_END, manager, position,
             ScreenPoint(0, 0), mcc->Time());

  // Now a smooth animation has been triggered for snapping to 30.
  apzc->AssertInSmoothMsdScroll();

  apzc->AdvanceAnimationsUntilEnd();
  // The snapped position should be 30 rather than 100 because it's the nearest
  // snap point.
  EXPECT_EQ(apzc->GetCurrentAsyncScrollOffset(
                    AsyncPanZoomController::eForEventHandling)
                .y,
            30);
}

// Smililar to above SnapOnPanEndWithZeroVelocity but with positive velocity so
// that the snap position would be the one in the scrolling direction.
TEST_F(APZCSnappingTesterMock, SnapOnPanEndWithPositiveVelocity) {
  // Use pref values for desktop everywhere.
  SCOPED_GFX_PREF_FLOAT("apz.fling_friction", 0.002);
  SCOPED_GFX_PREF_FLOAT("apz.fling_stopped_threshold", 0.01);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_x1", 0.0);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_x2", 1.0);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_y1", 0.0);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_y2", 1.0);
  SCOPED_GFX_PREF_INT("apz.velocity_relevance_time_ms", 100);

  const char* treeShape = "x";
  LayerIntRect layerVisibleRect[] = {
      LayerIntRect(0, 0, 100, 100),
  };
  CreateScrollData(treeShape, layerVisibleRect);
  SetScrollableFrameMetrics(root, START_SCROLL_ID, CSSRect(0, 0, 100, 400));

  // Set up two snap points, 30 and 100.
  ScrollSnapInfo snap;
  snap.mScrollSnapStrictnessY = StyleScrollSnapStrictness::Mandatory;
  snap.mSnapportSize =
      CSSSize::ToAppUnits(layerVisibleRect[0].Size() * LayerToCSSScale(1.0));
  snap.mSnapTargets.AppendElement(ScrollSnapInfo::SnapTarget(
      Nothing(), Some(30 * AppUnitsPerCSSPixel()),
      CSSRect::ToAppUnits(CSSRect(0, 30, 10, 10)), StyleScrollSnapStop::Normal,
      ScrollSnapTargetId{1}));
  snap.mSnapTargets.AppendElement(ScrollSnapInfo::SnapTarget(
      Nothing(), Some(100 * AppUnitsPerCSSPixel()),
      CSSRect::ToAppUnits(CSSRect(0, 100, 10, 10)), StyleScrollSnapStop::Normal,
      ScrollSnapTargetId{2}));

  // Save the scroll snap info on the root APZC.
  ModifyFrameMetrics(root, [&](ScrollMetadata& aSm, FrameMetrics& aMetrics) {
    aSm.SetSnapInfo(ScrollSnapInfo(snap));
  });

  auto registration =
      std::make_unique<ScopedLayerTreeRegistration>(LayersId{0}, mcc);
  UpdateHitTestingTree();

  RefPtr<TestAsyncPanZoomController> apzc = ApzcOf(root);

  // Send a series of pan gestures that a pan-end event happens at 65
  const ScreenIntPoint position = ScreenIntPoint(50, 30);
  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_START, manager, position,
             ScreenPoint(0, 10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, position,
             ScreenPoint(0, 35), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, position,
             ScreenPoint(0, 20), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());

  // There should be positive velocity in this case.
  EXPECT_GT(apzc->GetVelocityVector().y, 0);

  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_END, manager, position,
             ScreenPoint(0, 0), mcc->Time());
  mcc->AdvanceByMillis(5);

  // A smooth animation has been triggered by the pan-end event above.
  apzc->AssertInSmoothMsdScroll();

  apzc->AdvanceAnimationsUntilEnd();
  EXPECT_EQ(apzc->GetCurrentAsyncScrollOffset(
                    AsyncPanZoomController::eForEventHandling)
                .y,
            100);
}

// Test that a fling whose snap destination is the current scroll position does
// not fall back to an unsnapped fling.
TEST_F(APZCSnappingTesterMock, FlingSnapToCurrentPosition) {
  // Use pref values for desktop everywhere.
  SCOPED_GFX_PREF_FLOAT("apz.fling_friction", 0.002);
  SCOPED_GFX_PREF_FLOAT("apz.fling_stopped_threshold", 0.01);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_x1", 0.0);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_x2", 1.0);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_y1", 0.0);
  SCOPED_GFX_PREF_FLOAT("apz.fling_curve_function_y2", 1.0);
  SCOPED_GFX_PREF_INT("apz.velocity_relevance_time_ms", 100);
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", false);

  const char* treeShape = "x";
  LayerIntRect layerVisibleRect[] = {
      LayerIntRect(0, 0, 100, 100),
  };
  CreateScrollData(treeShape, layerVisibleRect);
  SetScrollableFrameMetrics(root, START_SCROLL_ID, CSSRect(0, 0, 100, 400));

  // Set up a single snap point, at 0.
  ScrollSnapInfo snap;
  snap.mScrollSnapStrictnessY = StyleScrollSnapStrictness::Mandatory;
  snap.mSnapportSize =
      CSSSize::ToAppUnits(layerVisibleRect[0].Size() * LayerToCSSScale(1.0));
  snap.mSnapTargets.AppendElement(ScrollSnapInfo::SnapTarget(
      Nothing(), Some(0), CSSRect::ToAppUnits(CSSRect(0, 0, 10, 10)),
      StyleScrollSnapStop::Normal, ScrollSnapTargetId{1}));

  ModifyFrameMetrics(root, [&](ScrollMetadata& aSm, FrameMetrics& aMetrics) {
    aSm.SetSnapInfo(ScrollSnapInfo(snap));
  });

  auto registration =
      std::make_unique<ScopedLayerTreeRegistration>(LayersId{0}, mcc);
  UpdateHitTestingTree();

  RefPtr<TestAsyncPanZoomController> apzc = ApzcOf(root);

  // Pan down quickly, then pan back up more slowly and by more than we
  // scrolled down, so that we end up back at 0 but with a downward velocity.
  const ScreenIntPoint position = ScreenIntPoint(50, 30);
  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_START, manager, position,
             ScreenPoint(0, 0), mcc->Time());
  mcc->AdvanceByMillis(6);
  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, position,
             ScreenPoint(0, 40), mcc->Time());
  mcc->AdvanceByMillis(30);
  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, position,
             ScreenPoint(0, -80), mcc->Time());
  mcc->AdvanceByMillis(5);

  EXPECT_EQ(apzc->GetCurrentAsyncScrollOffset(
                    AsyncPanZoomController::eForEventHandling)
                .y,
            0);

  // End the pan with simulated momentum, as on GTK, so that the pan-end
  // triggers a fling.
  QueueMockHitResult(START_SCROLL_ID);
  PanGesture(PanGestureInput::PANGESTURE_END, manager, position,
             ScreenPoint(0, 0), mcc->Time(), MODIFIER_NONE,
             /*aSimulateMomentum=*/true);

  apzc->AssertStateIsReset();
  EXPECT_EQ(apzc->GetVelocityVector(), ParentLayerPoint());
  apzc->AdvanceAnimationsUntilEnd();
  EXPECT_EQ(apzc->GetCurrentAsyncScrollOffset(
                    AsyncPanZoomController::eForEventHandling)
                .y,
            0);
}
#endif
