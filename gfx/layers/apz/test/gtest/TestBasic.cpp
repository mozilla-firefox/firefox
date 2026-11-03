/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "APZCBasicTester.h"
#include "APZTestCommon.h"
#include "InputBlockState.h"
#include "InputUtils.h"
#include "mozilla/ScrollPositionUpdate.h"
#include "mozilla/layers/ScrollableLayerGuid.h"

using LayersUpdateFlags = AsyncPanZoomController::LayersUpdateFlags;

class APZCTouchSwipeTester : public APZCBasicTester {
 public:
  enum MayTriggerSwipeEnum {
    MAY_TRIGGER_SWIPE,
    NOT_TRIGGER_SWIPE,
  };

 protected:
  APZEventResult SendTouchWithMayTriggerSwipe(
      MultiTouchInput::MultiTouchType aType, const ScreenIntPoint& aPoint,
      MayTriggerSwipeEnum aMayTriggerSwipe = MAY_TRIGGER_SWIPE) {
    mcc->AdvanceByMillis(10);
    MultiTouchInput input = CreateMultiTouchInput(aType, mcc->Time());
    input.mTouches.AppendElement(CreateSingleTouchData(0, aPoint));
    input.mMayTriggerSwipe = aMayTriggerSwipe == MAY_TRIGGER_SWIPE;
    return apzc->ReceiveInputEvent(input);
  }

  TouchBlockState* Block() {
    return tm->GetInputQueue()->GetCurrentTouchBlock();
  }

  void StartSwipeMove() {
    SendTouchWithMayTriggerSwipe(MultiTouchInput::MULTITOUCH_START,
                                 ScreenIntPoint(50, 50));
    ASSERT_NE(Block(), nullptr);
    EXPECT_FALSE(Block()->IsWaitingForBrowserGestureResponse());
    EXPECT_TRUE(Block()->IsReadyForHandling());
    SendTouchWithMayTriggerSwipe(MultiTouchInput::MULTITOUCH_MOVE,
                                 ScreenIntPoint(50, 10));
    EXPECT_TRUE(Block()->IsWaitingForBrowserGestureResponse());
    EXPECT_FALSE(Block()->IsReadyForHandling());
    // The page has not scrolled while waiting for the browser's decision.
    EXPECT_EQ(CSSPoint(), apzc->GetFrameMetrics().GetVisualScrollOffset());
  }
};

TEST_F(APZCTouchSwipeTester, Consumed) {
  StartSwipeMove();
  tm->GetInputQueue()->SetBrowserGestureResponse(
      Block()->GetBlockId(), BrowserGestureResponse::Consumed);
  EXPECT_FALSE(Block()->IsWaitingForBrowserGestureResponse());
  // The browser owns the swipe, so the queued move does not scroll the page.
  EXPECT_EQ(CSSPoint(), apzc->GetFrameMetrics().GetVisualScrollOffset());

  // Later moves and lifting the finger also leave the page in place.
  SendTouchWithMayTriggerSwipe(MultiTouchInput::MULTITOUCH_MOVE,
                               ScreenIntPoint(50, 0), NOT_TRIGGER_SWIPE);
  EXPECT_FALSE(Block()->IsWaitingForBrowserGestureResponse());
  EXPECT_EQ(CSSPoint(), apzc->GetFrameMetrics().GetVisualScrollOffset());
  SendTouchWithMayTriggerSwipe(MultiTouchInput::MULTITOUCH_END,
                               ScreenIntPoint(50, 0), NOT_TRIGGER_SWIPE);
  EXPECT_EQ(CSSPoint(), apzc->GetFrameMetrics().GetVisualScrollOffset());
}

TEST_F(APZCTouchSwipeTester, NotConsumed) {
  StartSwipeMove();
  tm->GetInputQueue()->SetBrowserGestureResponse(
      Block()->GetBlockId(), BrowserGestureResponse::NotConsumed);
  EXPECT_FALSE(Block()->IsWaitingForBrowserGestureResponse());
  // Rejecting the swipe lets APZ scroll with the queued move.
  const float firstScrollY = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
  EXPECT_GT(firstScrollY, 0);

  // Subsequent moves keep scrolling without waiting for another response.
  SendTouchWithMayTriggerSwipe(MultiTouchInput::MULTITOUCH_MOVE,
                               ScreenIntPoint(50, 0), NOT_TRIGGER_SWIPE);
  EXPECT_FALSE(Block()->IsWaitingForBrowserGestureResponse());
  EXPECT_GT(apzc->GetFrameMetrics().GetVisualScrollOffset().y, firstScrollY);
  SendTouchWithMayTriggerSwipe(MultiTouchInput::MULTITOUCH_END,
                               ScreenIntPoint(50, 0), NOT_TRIGGER_SWIPE);
}

TEST_F(APZCTouchSwipeTester, SlopAndTap) {
  // A tap never needs a browser response and does not scroll the page.
  SendTouchWithMayTriggerSwipe(MultiTouchInput::MULTITOUCH_START,
                               ScreenIntPoint(50, 50));
  ASSERT_NE(Block(), nullptr);
  EXPECT_FALSE(Block()->IsWaitingForBrowserGestureResponse());
  EXPECT_TRUE(Block()->IsReadyForHandling());

  const auto result = SendTouchWithMayTriggerSwipe(
      MultiTouchInput::MULTITOUCH_MOVE, ScreenIntPoint(50, 50));
  EXPECT_EQ(nsEventStatus_eConsumeNoDefault, result.GetStatus());
  EXPECT_FALSE(Block()->IsWaitingForBrowserGestureResponse());
  EXPECT_TRUE(Block()->IsReadyForHandling());
  SendTouchWithMayTriggerSwipe(MultiTouchInput::MULTITOUCH_END,
                               ScreenIntPoint(50, 50));
  EXPECT_FALSE(Block()->IsWaitingForBrowserGestureResponse());
  EXPECT_EQ(CSSPoint(), apzc->GetFrameMetrics().GetVisualScrollOffset());
}

TEST_F(APZCTouchSwipeTester, TouchWithoutMayTriggerSwipeDoesNotWait) {
  TouchDown(apzc, ScreenIntPoint(50, 50), mcc->Time());
  mcc->AdvanceByMillis(10);
  TouchMove(apzc, ScreenIntPoint(50, 10), mcc->Time());
  ASSERT_NE(Block(), nullptr);
  EXPECT_FALSE(Block()->IsWaitingForBrowserGestureResponse());
  EXPECT_GT(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 0);
  TouchUp(apzc, ScreenIntPoint(50, 10), mcc->Time());
}

TEST_F(APZCBasicTester, Overzoom) {
  // the visible area of the document in CSS pixels is x=10 y=0 w=100 h=100
  FrameMetrics fm;
  fm.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  fm.SetScrollableRect(CSSRect(0, 0, 125, 150));
  fm.SetVisualScrollOffset(CSSPoint(10, 0));
  fm.SetZoom(CSSToParentLayerScale(1.0));
  fm.SetIsRootContent(true);
  apzc->SetFrameMetrics(fm);

  MakeApzcZoomable();

  EXPECT_CALL(*mcc, RequestContentRepaint(_)).Times(1);

  PinchWithPinchInputAndCheckStatus(apzc, ScreenIntPoint(50, 50), 0.5, true);

  fm = apzc->GetFrameMetrics();
  EXPECT_EQ(0.8f, fm.GetZoom().scale);
  // bug 936721 - PGO builds introduce rounding error so
  // use a fuzzy match instead
  EXPECT_LT(std::abs(fm.GetVisualScrollOffset().x), 1e-5);
  EXPECT_LT(std::abs(fm.GetVisualScrollOffset().y), 1e-5);
}

TEST_F(APZCBasicTester, ZoomLimits) {
  SCOPED_GFX_PREF_FLOAT("apz.min_zoom", 0.9f);
  SCOPED_GFX_PREF_FLOAT("apz.max_zoom", 2.0f);

  // the visible area of the document in CSS pixels is x=10 y=0 w=100 h=100
  FrameMetrics fm;
  fm.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  fm.SetScrollableRect(CSSRect(0, 0, 125, 150));
  fm.SetZoom(CSSToParentLayerScale(1.0));
  fm.SetIsRootContent(true);
  apzc->SetFrameMetrics(fm);

  MakeApzcZoomable();

  // This should take the zoom scale to 0.8, but we've capped it at 0.9.
  PinchWithPinchInputAndCheckStatus(apzc, ScreenIntPoint(50, 50), 0.5, true);

  fm = apzc->GetFrameMetrics();
  EXPECT_EQ(0.9f, fm.GetZoom().scale);

  // This should take the zoom scale to 2.7, but we've capped it at 2.
  PinchWithPinchInputAndCheckStatus(apzc, ScreenIntPoint(50, 50), 3, true);

  fm = apzc->GetFrameMetrics();
  EXPECT_EQ(2.0f, fm.GetZoom().scale);
}

TEST_F(APZCBasicTester, SimpleTransform) {
  ParentLayerPoint pointOut;
  AsyncTransform viewTransformOut;
  apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);

  EXPECT_EQ(ParentLayerPoint(), pointOut);
  EXPECT_EQ(AsyncTransform(), viewTransformOut);
}

TEST_F(APZCBasicTester, ComplexTransform) {
  // This test assumes there is a page that gets rendered to
  // two layers. In CSS pixels, the first layer is 50x50 and
  // the second layer is 25x50. The widget scale factor is 3.0
  // and the presShell resolution is 2.0. Therefore, these layers
  // end up being 300x300 and 150x300 in layer pixels.
  //
  // The second (child) layer has an additional CSS transform that
  // stretches it by 2.0 on the x-axis. Therefore, after applying
  // CSS transforms, the two layers are the same size in screen
  // pixels.
  //
  // The screen itself is 24x24 in screen pixels (therefore 4x4 in
  // CSS pixels). The displayport is 1 extra CSS pixel on all
  // sides.

  RefPtr childApzc =
      mozilla::MakeRefPtr<TestAsyncPanZoomController>(LayersId{0}, mcc, tm);

  const char* treeShape = "x(x)";
  // LayerID                     0 1
  LayerIntRect layerVisibleRect[] = {
      LayerIntRect(0, 0, 300, 300),
      LayerIntRect(0, 0, 150, 300),
  };
  Matrix4x4 transforms[] = {
      Matrix4x4(),
      Matrix4x4(),
  };
  transforms[0].PostScale(
      0.5f, 0.5f,
      1.0f);  // this results from the 2.0 resolution on the root layer
  transforms[1].PostScale(
      2.0f, 1.0f,
      1.0f);  // this is the 2.0 x-axis CSS transform on the child layer

  auto layers = TestWRScrollData::Create(treeShape, *updater, layerVisibleRect,
                                         transforms);

  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 24, 24));
  metrics.SetDisplayPort(CSSRect(-1, -1, 6, 6));
  metrics.SetVisualScrollOffset(CSSPoint(10, 10));
  metrics.SetLayoutViewport(CSSRect(10, 10, 8, 8));
  metrics.SetScrollableRect(CSSRect(0, 0, 50, 50));
  metrics.SetCumulativeResolution(LayoutDeviceToLayerScale(2));
  metrics.SetPresShellResolution(2.0f);
  metrics.SetZoom(CSSToParentLayerScale(6));
  metrics.SetDevPixelsPerCSSPixel(CSSToLayoutDeviceScale(3));
  metrics.SetScrollId(START_SCROLL_ID);

  ScrollMetadata childMetadata = metadata;
  FrameMetrics& childMetrics = childMetadata.GetMetrics();
  childMetrics.SetScrollId(START_SCROLL_ID + 1);

  layers[0]->AppendScrollMetadata(layers, metadata);
  layers[1]->AppendScrollMetadata(layers, childMetadata);

  ParentLayerPoint pointOut;
  AsyncTransform viewTransformOut;

  // Both the parent and child layer should behave exactly the same here,
  // because the CSS transform on the child layer does not affect the
  // SampleContentTransformForFrame code

  // initial transform
  apzc->SetFrameMetrics(metrics);
  apzc->NotifyMainThreadTransaction(
      metadata,
      LayersUpdateFlags{.mIsFirstPaint = true, .mThisLayerTreeUpdated = true});
  apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);
  EXPECT_EQ(AsyncTransform(LayerToParentLayerScale(1), ParentLayerPoint()),
            viewTransformOut);
  EXPECT_EQ(ParentLayerPoint(60, 60), pointOut);

  childApzc->SetFrameMetrics(childMetrics);
  childApzc->NotifyMainThreadTransaction(
      childMetadata,
      LayersUpdateFlags{.mIsFirstPaint = true, .mThisLayerTreeUpdated = true});
  childApzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);
  EXPECT_EQ(AsyncTransform(LayerToParentLayerScale(1), ParentLayerPoint()),
            viewTransformOut);
  EXPECT_EQ(ParentLayerPoint(60, 60), pointOut);

  // do an async scroll by 5 pixels and check the transform
  metrics.ScrollBy(CSSPoint(5, 0));
  apzc->SetFrameMetrics(metrics);
  apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);
  EXPECT_EQ(
      AsyncTransform(LayerToParentLayerScale(1), ParentLayerPoint(-30, 0)),
      viewTransformOut);
  EXPECT_EQ(ParentLayerPoint(90, 60), pointOut);

  childMetrics.ScrollBy(CSSPoint(5, 0));
  childApzc->SetFrameMetrics(childMetrics);
  childApzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);
  EXPECT_EQ(
      AsyncTransform(LayerToParentLayerScale(1), ParentLayerPoint(-30, 0)),
      viewTransformOut);
  EXPECT_EQ(ParentLayerPoint(90, 60), pointOut);

  // do an async zoom of 1.5x and check the transform
  metrics.ZoomBy(1.5f);
  apzc->SetFrameMetrics(metrics);
  apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);
  EXPECT_EQ(
      AsyncTransform(LayerToParentLayerScale(1.5), ParentLayerPoint(-45, 0)),
      viewTransformOut);
  EXPECT_EQ(ParentLayerPoint(135, 90), pointOut);

  childMetrics.ZoomBy(1.5f);
  childApzc->SetFrameMetrics(childMetrics);
  childApzc->SampleContentTransformForFrame(&viewTransformOut, pointOut);
  EXPECT_EQ(
      AsyncTransform(LayerToParentLayerScale(1.5), ParentLayerPoint(-45, 0)),
      viewTransformOut);
  EXPECT_EQ(ParentLayerPoint(135, 90), pointOut);

  childApzc->Destroy();
}

TEST_F(APZCBasicTester, Fling) {
  SCOPED_GFX_PREF_FLOAT("apz.fling_min_velocity_threshold", 0.0f);
  int touchStart = 50;
  int touchEnd = 10;
  ParentLayerPoint pointOut;
  AsyncTransform viewTransformOut;

  // Fling down. Each step scroll further down
  Pan(apzc, touchStart, touchEnd);
  ParentLayerPoint lastPoint;
  for (int i = 1; i < 50; i += 1) {
    apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut,
                                         TimeDuration::FromMilliseconds(1));
    EXPECT_GT(pointOut.y, lastPoint.y);
    lastPoint = pointOut;
  }
}

#ifndef MOZ_WIDGET_ANDROID  // Maybe fails on Android
static ScrollGenerationCounter sGenerationCounter;

TEST_F(APZCBasicTester, ResumeInterruptedTouchDrag_Bug1592435) {
  // Start a touch-drag and scroll some amount, not lifting the finger.
  SCOPED_GFX_PREF_FLOAT("apz.touch_start_tolerance", 1.0f / 1000.0f);
  ScreenIntPoint touchPos(10, 50);
  TouchDown(apzc, touchPos, mcc->Time());
  for (int i = 0; i < 20; ++i) {
    touchPos.y -= 1;
    mcc->AdvanceByMillis(1);
    TouchMove(apzc, touchPos, mcc->Time());
  }

  // Take note of the scroll offset before the interruption.
  CSSPoint scrollOffsetBeforeInterruption =
      apzc->GetFrameMetrics().GetVisualScrollOffset();

  // Have the main thread interrupt the touch-drag by sending
  // a main thread scroll update to a nearby location.
  CSSPoint mainThreadOffset = scrollOffsetBeforeInterruption;
  mainThreadOffset.y -= 5;
  ScrollMetadata metadata = apzc->GetScrollMetadata();
  metadata.GetMetrics().SetLayoutScrollOffset(mainThreadOffset);
  nsTArray<ScrollPositionUpdate> scrollUpdates;
  scrollUpdates.AppendElement(ScrollPositionUpdate::NewScroll(
      ScrollOrigin::Other, CSSPoint::ToAppUnits(mainThreadOffset)));
  metadata.SetScrollUpdates(scrollUpdates);
  metadata.GetMetrics().SetScrollGeneration(
      scrollUpdates.LastElement().GetGeneration());
  apzc->NotifyMainThreadTransaction(
      metadata,
      LayersUpdateFlags{.mIsFirstPaint = false, .mThisLayerTreeUpdated = true});

  // Continue and finish the touch-drag gesture.
  for (int i = 0; i < 20; ++i) {
    touchPos.y -= 1;
    mcc->AdvanceByMillis(1);
    TouchMove(apzc, touchPos, mcc->Time());
  }

  // Check that the portion of the touch-drag that occurred after
  // the interruption caused additional scrolling.
  CSSPoint finalScrollOffset = apzc->GetFrameMetrics().GetVisualScrollOffset();
  EXPECT_GT(finalScrollOffset.y, scrollOffsetBeforeInterruption.y);

  // Now do the same thing, but for a visual scroll update.
  scrollOffsetBeforeInterruption =
      apzc->GetFrameMetrics().GetVisualScrollOffset();
  mainThreadOffset = scrollOffsetBeforeInterruption;
  mainThreadOffset.y -= 5;
  metadata = apzc->GetScrollMetadata();
  metadata.GetMetrics().SetVisualDestination(mainThreadOffset);
  metadata.GetMetrics().SetScrollGeneration(
      sGenerationCounter.NewMainThreadGeneration());
  metadata.GetMetrics().SetVisualScrollUpdateType(
      ScrollOffsetUpdateType::MainThread);
  scrollUpdates.Clear();
  metadata.SetScrollUpdates(scrollUpdates);
  apzc->NotifyMainThreadTransaction(
      metadata,
      LayersUpdateFlags{.mIsFirstPaint = false, .mThisLayerTreeUpdated = true});
  for (int i = 0; i < 20; ++i) {
    touchPos.y -= 1;
    mcc->AdvanceByMillis(1);
    TouchMove(apzc, touchPos, mcc->Time());
  }
  finalScrollOffset = apzc->GetFrameMetrics().GetVisualScrollOffset();
  EXPECT_GT(finalScrollOffset.y, scrollOffsetBeforeInterruption.y);

  // Clean up by ending the touch gesture.
  mcc->AdvanceByMillis(1);
  TouchUp(apzc, touchPos, mcc->Time());
}
#endif

TEST_F(APZCBasicTester, RelativeScrollOffset) {
  // Set up initial conditions: zoomed in, layout offset at (100, 100),
  // visual offset at (120, 120); the relative offset is therefore (20, 20).
  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetScrollableRect(CSSRect(0, 0, 1000, 1000));
  metrics.SetLayoutViewport(CSSRect(100, 100, 100, 100));
  metrics.SetZoom(CSSToParentLayerScale(2.0));
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetVisualScrollOffset(CSSPoint(120, 120));
  metrics.SetIsRootContent(true);
  apzc->SetFrameMetrics(metrics);

  // Scroll the layout viewport to (200, 200).
  ScrollMetadata mainThreadMetadata = metadata;
  FrameMetrics& mainThreadMetrics = mainThreadMetadata.GetMetrics();
  mainThreadMetrics.SetLayoutScrollOffset(CSSPoint(200, 200));
  nsTArray<ScrollPositionUpdate> scrollUpdates;
  scrollUpdates.AppendElement(ScrollPositionUpdate::NewScroll(
      ScrollOrigin::Other, CSSPoint::ToAppUnits(CSSPoint(200, 200))));
  mainThreadMetadata.SetScrollUpdates(scrollUpdates);
  mainThreadMetrics.SetScrollGeneration(
      scrollUpdates.LastElement().GetGeneration());
  apzc->NotifyMainThreadTransaction(
      mainThreadMetadata,
      LayersUpdateFlags{.mIsFirstPaint = false, .mThisLayerTreeUpdated = true});

  // Check that the relative offset has been preserved.
  metrics = apzc->GetFrameMetrics();
  EXPECT_EQ(metrics.GetLayoutScrollOffset(), CSSPoint(200, 200));
  EXPECT_EQ(metrics.GetVisualScrollOffset(), CSSPoint(220, 220));
}

TEST_F(APZCBasicTester, MultipleSmoothScrollsSmooth) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  // We want to test that if we send multiple smooth scroll requests that we
  // still smoothly animate, ie that we get non-zero change every frame while
  // the animation is running.

  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 10000));
  metrics.SetLayoutViewport(CSSRect(0, 0, 100, 100));
  metrics.SetZoom(CSSToParentLayerScale(1.0));
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetVisualScrollOffset(CSSPoint(0, 0));
  metrics.SetIsRootContent(true);
  apzc->SetFrameMetrics(metrics);

  // Structure of this test.
  //   -send a pure relative smooth scroll request via
  //   NotifyMainThreadTransaction
  //   -advance animations a few times, check that scroll offset is increasing
  //   after the first few advances
  //   -send a pure relative smooth scroll request via
  //   NotifyMainThreadTransaction
  //   -advance animations a few times, check that scroll offset is increasing
  //   -send a pure relative smooth scroll request via
  //   NotifyMainThreadTransaction
  //   -advance animations a few times, check that scroll offset is increasing

  ScrollMetadata metadata2 = metadata;
  nsTArray<ScrollPositionUpdate> scrollUpdates2;
  scrollUpdates2.AppendElement(ScrollPositionUpdate::NewPureRelativeScroll(
      ScrollOrigin::Other, ScrollMode::Smooth,
      CSSPoint::ToAppUnits(CSSPoint(0, 200))));
  metadata2.SetScrollUpdates(scrollUpdates2);
  metadata2.GetMetrics().SetScrollGeneration(
      scrollUpdates2.LastElement().GetGeneration());
  apzc->NotifyMainThreadTransaction(
      metadata2,
      LayersUpdateFlags{.mIsFirstPaint = false, .mThisLayerTreeUpdated = true});

  // Get the animation going
  for (uint32_t i = 0; i < 3; i++) {
    SampleAnimationOneFrame();
  }

  float offset =
      apzc->GetCurrentAsyncScrollOffset(AsyncPanZoomController::eForCompositing)
          .y;
  ASSERT_GT(offset, 0);
  float lastOffset = offset;

  for (uint32_t i = 0; i < 2; i++) {
    for (uint32_t j = 0; j < 3; j++) {
      SampleAnimationOneFrame();
      offset = apzc->GetCurrentAsyncScrollOffset(
                       AsyncPanZoomController::eForCompositing)
                   .y;
      ASSERT_GT(offset, lastOffset);
      lastOffset = offset;
    }

    ScrollMetadata metadata3 = metadata;
    nsTArray<ScrollPositionUpdate> scrollUpdates3;
    scrollUpdates3.AppendElement(ScrollPositionUpdate::NewPureRelativeScroll(
        ScrollOrigin::Other, ScrollMode::Smooth,
        CSSPoint::ToAppUnits(CSSPoint(0, 200))));
    metadata3.SetScrollUpdates(scrollUpdates3);
    metadata3.GetMetrics().SetScrollGeneration(
        scrollUpdates3.LastElement().GetGeneration());
    apzc->NotifyMainThreadTransaction(
        metadata3, LayersUpdateFlags{.mIsFirstPaint = false,
                                     .mThisLayerTreeUpdated = true});
  }

  for (uint32_t j = 0; j < 7; j++) {
    SampleAnimationOneFrame();
    offset = apzc->GetCurrentAsyncScrollOffset(
                     AsyncPanZoomController::eForCompositing)
                 .y;
    ASSERT_GT(offset, lastOffset);
    lastOffset = offset;
  }
}

TEST_F(APZCBasicTester, NotifyMainThreadTransaction_WithScrollUpdate) {
  // Set an empty metadata as if the APZC is now newly created.
  // This replicates when a document in a background tab now becomes forground.
  ScrollMetadata metadata;
  apzc->SetScrollMetadata(metadata);
  ASSERT_TRUE(apzc->GetScrollMetadata().IsDefault());

  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetDisplayPort(CSSRect(0, 0, 10, 10));
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 10, 10));
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 100));

  // Set layout/visual scroll offsets as if the document has scrolled when the
  // document was foregound.
  metrics.SetVisualScrollOffset(CSSPoint(10, 10));
  metrics.SetLayoutViewport(CSSRect(10, 10, 10, 10));
  metrics.SetScrollId(START_SCROLL_ID);

  // Add a new relative scroll update (10, 10) -> (15, 15).
  AutoTArray<ScrollPositionUpdate, 1> scrollUpdates;
  scrollUpdates.AppendElement(ScrollPositionUpdate::NewRelativeScroll(
      CSSPoint::ToAppUnits(CSSPoint(10, 10)),
      CSSPoint::ToAppUnits(CSSPoint(15, 15))));
  metadata.SetScrollUpdates(scrollUpdates);
  metrics.SetScrollGeneration(scrollUpdates.LastElement().GetGeneration());
  // With the above scroll update, now the layout/visual scroll offsets (on the
  // main-thread) need to be updated.
  metrics.SetVisualScrollOffset(CSSPoint(15, 15));
  metrics.SetLayoutViewport(CSSRect(15, 15, 10, 10));

  // It's not first-paint when switching tab.
  apzc->NotifyMainThreadTransaction(
      metadata,
      LayersUpdateFlags{.mIsFirstPaint = false, .mThisLayerTreeUpdated = true});

  // The layout/visual scroll ofsets and the relative scroll update need to be
  // reflected.
  ASSERT_EQ(apzc->GetFrameMetrics().GetLayoutScrollOffset(), CSSPoint(15, 15));
  ASSERT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset(), CSSPoint(15, 15));
}

TEST_F(APZCBasicTester, NotifyMainThreadTransaction_WithMultipleScrollUpdates) {
  // Set an empty metadata as if the APZC is now newly created.
  // This replicates when a document in a background tab now becomes foreground.
  ScrollMetadata metadata;
  apzc->SetScrollMetadata(metadata);
  ASSERT_TRUE(apzc->GetScrollMetadata().IsDefault());

  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetDisplayPort(CSSRect(0, 0, 10, 10));
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 10, 10));
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 100));

  metrics.SetVisualScrollOffset(CSSPoint(0, 0));
  metrics.SetLayoutViewport(CSSRect(0, 0, 10, 10));
  metrics.SetScrollId(START_SCROLL_ID);

  AutoTArray<ScrollPositionUpdate, 2> scrollUpdates;
  // Append a new scroll frame as if the scroll frame was reconstructed.
  scrollUpdates.AppendElement(ScrollPositionUpdate::NewScrollframe(
      CSSPoint::ToAppUnits(CSSPoint(0, 0))));
  // Append a new relative scroll update (0, 0) -> (20, 20).
  scrollUpdates.AppendElement(ScrollPositionUpdate::NewRelativeScroll(
      CSSPoint::ToAppUnits(CSSPoint(0, 0)),
      CSSPoint::ToAppUnits(CSSPoint(20, 20))));
  metadata.SetScrollUpdates(scrollUpdates);
  metrics.SetScrollGeneration(scrollUpdates.LastElement().GetGeneration());
  // With the above scroll updates, now the layout/visual scroll offsets (on the
  // main-thread) need to be updated.
  metrics.SetVisualScrollOffset(CSSPoint(20, 20));
  metrics.SetLayoutViewport(CSSRect(20, 20, 10, 10));

  // It's not first-paint when switching tab.
  apzc->NotifyMainThreadTransaction(
      metadata,
      LayersUpdateFlags{.mIsFirstPaint = false, .mThisLayerTreeUpdated = true});

  // The layout/visual scroll ofsets and the relative scroll update need to be
  // reflected.
  ASSERT_EQ(apzc->GetFrameMetrics().GetLayoutScrollOffset(), CSSPoint(20, 20));
  ASSERT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset(), CSSPoint(20, 20));
}

class APZCSmoothScrollTester : public APZCBasicTester {
 public:
  // Test that a smooth scroll animation correctly handles its destination
  // being updated by a relative scroll delta from the main thread (a "content
  // shift").
  void TestContentShift() {
    // Set up scroll frame. Starting scroll position is (0, 0).
    ScrollMetadata metadata;
    FrameMetrics& metrics = metadata.GetMetrics();
    metrics.SetScrollableRect(CSSRect(0, 0, 100, 10000));
    metrics.SetLayoutViewport(CSSRect(0, 0, 100, 100));
    metrics.SetZoom(CSSToParentLayerScale(1.0));
    metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
    metrics.SetVisualScrollOffset(CSSPoint(0, 0));
    metrics.SetIsRootContent(true);
    apzc->SetFrameMetrics(metrics);

    // Start smooth scroll via main-thread request.
    nsTArray<ScrollPositionUpdate> scrollUpdates;
    scrollUpdates.AppendElement(ScrollPositionUpdate::NewPureRelativeScroll(
        ScrollOrigin::Other, ScrollMode::Smooth,
        CSSPoint::ToAppUnits(CSSPoint(0, 1000))));
    metadata.SetScrollUpdates(scrollUpdates);
    metrics.SetScrollGeneration(scrollUpdates.LastElement().GetGeneration());
    apzc->NotifyMainThreadTransaction(
        metadata, LayersUpdateFlags{.mIsFirstPaint = false,
                                    .mThisLayerTreeUpdated = true});

    // Sample the smooth scroll animation until we get past y=500.
    apzc->AssertInSmoothScroll();
    float y = 0;
    while (y < 500) {
      SampleAnimationOneFrame();
      y = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    }

    // Send a relative scroll of y = -400.
    scrollUpdates.Clear();
    scrollUpdates.AppendElement(ScrollPositionUpdate::NewRelativeScroll(
        CSSPoint::ToAppUnits(CSSPoint(0, 500)),
        CSSPoint::ToAppUnits(CSSPoint(0, 100))));
    metadata.SetScrollUpdates(scrollUpdates);
    metrics.SetScrollGeneration(scrollUpdates.LastElement().GetGeneration());
    apzc->NotifyMainThreadTransaction(
        metadata, LayersUpdateFlags{.mIsFirstPaint = false,
                                    .mThisLayerTreeUpdated = false});

    // Verify the relative scroll was applied but didn't cancel the animation.
    float y2 = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    ASSERT_EQ(y2, y - 400);
    apzc->AssertInSmoothScroll();

    // Sample the animation again and check that it respected the relative
    // scroll.
    SampleAnimationOneFrame();
    float y3 = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    ASSERT_GT(y3, y2);
    ASSERT_LT(y3, 500);

    // Continue animation until done and check that it ended up at a correctly
    // adjusted destination.
    apzc->AdvanceAnimationsUntilEnd();
    float y4 = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    ASSERT_EQ(y4, 600);  // 1000 (initial destination) - 400 (relative scroll)
  }

  // Test that a smooth scroll animation correctly handles a content
  // shift, followed by an UpdateDelta due to a new input event.
  void TestContentShiftThenUpdateDelta() {
    // Set up scroll frame. Starting position is (0, 0).
    ScrollMetadata metadata;
    FrameMetrics& metrics = metadata.GetMetrics();
    metrics.SetScrollableRect(CSSRect(0, 0, 1000, 10000));
    metrics.SetLayoutViewport(CSSRect(0, 0, 1000, 1000));
    metrics.SetZoom(CSSToParentLayerScale(1.0));
    metrics.SetCompositionBounds(ParentLayerRect(0, 0, 1000, 1000));
    metrics.SetVisualScrollOffset(CSSPoint(0, 0));
    metrics.SetIsRootContent(true);
    // Set the line scroll amount to 100 pixels. Note that SmoothWheel() takes
    // a delta denominated in lines.
    metadata.SetLineScrollAmount({100, 100});
    // The page scroll amount also needs to be set, otherwise the wheel handling
    // code will get confused by things like the "don't scroll more than one
    // page" check.
    metadata.SetPageScrollAmount({1000, 1000});
    apzc->SetScrollMetadata(metadata);

    // Send a wheel event to trigger smooth scrolling by 5 lines (= 500 pixels).
    SmoothWheel(apzc, ScreenIntPoint(50, 50), ScreenPoint(0, 5), mcc->Time());
    apzc->AssertInWheelScroll();

    // Sample the wheel scroll animation until we get past y=200.
    float y = 0;
    while (y < 200) {
      SampleAnimationOneFrame();
      y = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    }

    // Apply a content shift of y=100.
    nsTArray<ScrollPositionUpdate> scrollUpdates;
    scrollUpdates.AppendElement(ScrollPositionUpdate::NewRelativeScroll(
        CSSPoint::ToAppUnits(CSSPoint(0, 200)),
        CSSPoint::ToAppUnits(CSSPoint(0, 300))));
    metadata.SetScrollUpdates(scrollUpdates);
    metrics.SetScrollGeneration(scrollUpdates.LastElement().GetGeneration());
    apzc->NotifyMainThreadTransaction(
        metadata, LayersUpdateFlags{.mIsFirstPaint = false,
                                    .mThisLayerTreeUpdated = true});

    // Check that the content shift was applied but didn't cancel the animation.
    // At this point, the animation's internal state should be targeting a
    // destination of y=600.
    float y2 = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    ASSERT_EQ(y2, y + 100);
    apzc->AssertInWheelScroll();

    // Sample the animation until we get past y=400.
    while (y < 400) {
      SampleAnimationOneFrame();
      y = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    }

    // Send another wheel event to trigger smooth scrolling by another 5 lines
    // (=500 pixels). This should update the animation to target a destination
    // of y=1100.
    SmoothWheel(apzc, ScreenIntPoint(50, 50), ScreenPoint(0, 5), mcc->Time());

    // Continue the animation until done and check that it ended up at y=1100.
    apzc->AdvanceAnimationsUntilEnd();
    float yEnd = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    ASSERT_EQ(yEnd, 1100);
  }

  // Test that a content shift does not cause a smooth scroll animation to
  // overshoot its (updated) destination.
  void TestContentShiftDoesNotCauseOvershoot() {
    // Follow the same steps as in TestContentShiftThenUpdateDelta(),
    // except use a content shift of y=1000.
    ScrollMetadata metadata;
    FrameMetrics& metrics = metadata.GetMetrics();
    metrics.SetScrollableRect(CSSRect(0, 0, 1000, 10000));
    metrics.SetLayoutViewport(CSSRect(0, 0, 1000, 1000));
    metrics.SetZoom(CSSToParentLayerScale(1.0));
    metrics.SetCompositionBounds(ParentLayerRect(0, 0, 1000, 1000));
    metrics.SetVisualScrollOffset(CSSPoint(0, 0));
    metrics.SetIsRootContent(true);
    metadata.SetLineScrollAmount({100, 100});
    metadata.SetPageScrollAmount({1000, 1000});
    apzc->SetScrollMetadata(metadata);

    // First wheel event, smooth scroll destination is y=500.
    SmoothWheel(apzc, ScreenIntPoint(50, 50), ScreenPoint(0, 5), mcc->Time());
    apzc->AssertInWheelScroll();

    // Sample until we get past y=200.
    float y = 0;
    while (y < 200) {
      SampleAnimationOneFrame();
      y = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    }

    // Apply a content shift of y=1000. The current scroll position is now
    // y>1200, and the updated destination is y=1500.
    nsTArray<ScrollPositionUpdate> scrollUpdates;
    scrollUpdates.AppendElement(ScrollPositionUpdate::NewRelativeScroll(
        CSSPoint::ToAppUnits(CSSPoint(0, 200)),
        CSSPoint::ToAppUnits(CSSPoint(0, 1200))));
    metadata.SetScrollUpdates(scrollUpdates);
    metrics.SetScrollGeneration(scrollUpdates.LastElement().GetGeneration());
    apzc->NotifyMainThreadTransaction(
        metadata, LayersUpdateFlags{.mIsFirstPaint = false,
                                    .mThisLayerTreeUpdated = true});
    float y2 = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    ASSERT_EQ(y2, y + 1000);
    apzc->AssertInWheelScroll();

    // Sample until we get past y=1300.
    while (y < 1300) {
      SampleAnimationOneFrame();
      y = apzc->GetFrameMetrics().GetVisualScrollOffset().y;
    }

    // Second wheel event, destination is now y=2000.
    // MSD physics has a bug where the UpdateDelta() causes the content shift
    // to be applied in duplicate on the next sample, causing the scroll
    // position to be y>2000!
    SmoothWheel(apzc, ScreenIntPoint(50, 50), ScreenPoint(0, 5), mcc->Time());

    // Check that the scroll position remains <= 2000 until the end of the
    // animation.
    while (apzc->IsWheelScrollAnimationRunning()) {
      SampleAnimationOneFrame();
      ASSERT_LE(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 2000);
    }
    ASSERT_EQ(2000, apzc->GetFrameMetrics().GetVisualScrollOffset().y);
  }

  // Test that receiving a wheel event with a timestamp far in the future does
  // not cause scrolling to get stuck.
  void TestWheelEventWithFutureStamp() {
    // Set up scroll frame. Starting scroll position is (0, 0).
    ScrollMetadata metadata;
    FrameMetrics& metrics = metadata.GetMetrics();
    metrics.SetScrollableRect(CSSRect(0, 0, 1000, 10000));
    metrics.SetLayoutViewport(CSSRect(0, 0, 1000, 1000));
    metrics.SetZoom(CSSToParentLayerScale(1.0));
    metrics.SetCompositionBounds(ParentLayerRect(0, 0, 1000, 1000));
    metrics.SetVisualScrollOffset(CSSPoint(0, 0));
    metrics.SetIsRootContent(true);
    // Set the line scroll amount to 100 pixels. Note that SmoothWheel() takes
    // a delta denominated in lines.
    metadata.SetLineScrollAmount({100, 100});
    // The page scroll amount also needs to be set, otherwise the wheel handling
    // code will get confused by things like the "don't scroll more than one
    // page" check.
    metadata.SetPageScrollAmount({1000, 1000});
    apzc->SetScrollMetadata(metadata);

    // Send a wheel event to trigger smooth scrolling by 5 lines (= 500 pixels).
    // Give the wheel event a timestamp "far" (here, 1 minute) into the future.
    // This simulates a scenario, observed in bug 1926830, where a bug in the
    // system toolkit or widget layers causes something to introduce a skew into
    // the timestamps received from widget code.
    TimeStamp futureTimeStamp = mcc->Time() + TimeDuration::FromSeconds(60);
    SmoothWheel(apzc, ScreenIntPoint(50, 50), ScreenPoint(0, 5),
                futureTimeStamp);
    apzc->AssertInWheelScroll();

    // Sample the animation 10 frames (a shorter overall duration than the
    // timestamp skew).
    for (int i = 0; i < 10; ++i) {
      SampleAnimationOneFrame();
    }

    // Assert that we have scrolled. Without a mitigation in place for the
    // timestamp skew, we may wait for the frame (vsync) time to catch up with
    // the event's timestamp before doing any scrolling.
    ASSERT_GT(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 0);

    // Clean up by letting the animation run until completion.
    apzc->AdvanceAnimationsUntilEnd();
  }

  // Test that receiving a key event with a timestamp far in the future does
  // not cause scrolling to get stuck.
  void TestKeyEventWithFutureStamp() {
    // Set up scroll frame. Starting scroll position is (0, 0).
    ScrollMetadata metadata;
    FrameMetrics& metrics = metadata.GetMetrics();
    metrics.SetScrollableRect(CSSRect(0, 0, 1000, 10000));
    metrics.SetLayoutViewport(CSSRect(0, 0, 1000, 1000));
    metrics.SetZoom(CSSToParentLayerScale(1.0));
    metrics.SetCompositionBounds(ParentLayerRect(0, 0, 1000, 1000));
    metrics.SetVisualScrollOffset(CSSPoint(0, 0));
    metrics.SetScrollId(START_SCROLL_ID);
    metrics.SetIsRootContent(true);
    // Set the line scroll amount to 100 pixels. The key event we send
    // will scroll by a multiple of this amount.
    metadata.SetLineScrollAmount({100, 100});
    apzc->SetScrollMetadata(metadata);

    // Note that, since we are sending the key event to the APZC instance
    // directly, we don't need to set up a keyboard map or focus state.

    // Send a key event to trigger smooth scrolling by a few lines (the number
    // of lines is determined by toolkit.scrollbox.verticalScrollDistance).
    WidgetKeyboardEvent keyEvent(true, eKeyDown, nullptr);
    // Give the key event a timestamp "far" (here, 1 minute) into the future.
    // This simulates a scenario, observed in bug 1926830, where a bug in the
    // system toolkit or widget layers causes something to introduce a skew into
    // the timestamps received from widget code.
    TimeStamp futureTimeStamp = mcc->Time() + TimeDuration::FromSeconds(60);
    keyEvent.mTimeStamp = futureTimeStamp;
    KeyboardInput keyInput(keyEvent);
    // The KeyboardScrollAction needs to be specified on the event explicitly,
    // since the mapping from eKeyDown to it happens in APZCTreeManager which
    // we are bypassing here.
    keyInput.mAction = {KeyboardScrollAction::eScrollLine, /*aForward=*/true};
    (void)apzc->ReceiveInputEvent(keyInput);
    apzc->AssertInKeyboardScroll();

    // Sample the animation 10 frames (a shorter overall duration than the
    // timestamp skew).
    for (int i = 0; i < 10; ++i) {
      SampleAnimationOneFrame();
    }

    // Assert that we have scrolled. Without a mitigation in place for the
    // timestamp skew, we may wait for the frame (vsync) time to catch up with
    // the event's timestamp before doing any scrolling.
    ASSERT_GT(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 0);

    // Clean up by letting the animation run until completion.
    apzc->AdvanceAnimationsUntilEnd();
  }
};

TEST_F(APZCSmoothScrollTester, ContentShiftBezier) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", false);
  TestContentShift();
}

TEST_F(APZCSmoothScrollTester, ContentShiftMsd) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", true);
  TestContentShift();
}

TEST_F(APZCSmoothScrollTester, ContentShiftThenUpdateDeltaBezier) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", false);
  TestContentShiftThenUpdateDelta();
}

TEST_F(APZCSmoothScrollTester, ContentShiftThenUpdateDeltaMsd) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", true);
  TestContentShiftThenUpdateDelta();
}

TEST_F(APZCSmoothScrollTester, ContentShiftDoesNotCauseOvershootBezier) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", false);
  TestContentShiftDoesNotCauseOvershoot();
}

TEST_F(APZCSmoothScrollTester, ContentShiftDoesNotCauseOvershootMsd) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", true);
  TestContentShiftDoesNotCauseOvershoot();
}

TEST_F(APZCSmoothScrollTester, FutureWheelTimestampBezier) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", false);
  TestWheelEventWithFutureStamp();
}

TEST_F(APZCSmoothScrollTester, FutureWheelTimestampMsd) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", true);
  TestWheelEventWithFutureStamp();
}

TEST_F(APZCSmoothScrollTester, FutureKeyTimestampBezier) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", false);
  TestKeyEventWithFutureStamp();
}

TEST_F(APZCSmoothScrollTester, FutureKeyTimestampMsd) {
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", true);
  SCOPED_GFX_PREF_BOOL("general.smoothScroll.msdPhysics.enabled", true);
  TestKeyEventWithFutureStamp();
}

TEST_F(APZCBasicTester, ZoomAndScrollableRectChangeAfterZoomChange) {
  // We want to check that a small scrollable rect change (which causes us to
  // reclamp our scroll position, including in the sampled state) does not move
  // the scroll offset in the sample state based the zoom in the apzc, only
  // based on the zoom in the sampled state.

  // First we zoom in to the right hand side. Then start zooming out, then send
  // a scrollable rect change and check that it doesn't change the sampled state
  // scroll offset.

  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 1000));
  metrics.SetLayoutViewport(CSSRect(0, 0, 100, 100));
  metrics.SetVisualScrollOffset(CSSPoint(0, 0));
  metrics.SetZoom(CSSToParentLayerScale(1.0));
  metrics.SetIsRootContent(true);
  apzc->SetFrameMetrics(metrics);

  MakeApzcZoomable();

  // Zoom to right side.
  ZoomTarget zoomTarget{CSSRect(75, 25, 25, 25)};
  apzc->ZoomToRect(zoomTarget, 0);

  // Run the animation to completion, should take 250ms/16.67ms = 15 frames, but
  // do extra to make sure.
  for (uint32_t i = 0; i < 30; i++) {
    SampleAnimationOneFrame();
  }

  EXPECT_FALSE(apzc->IsAsyncZooming());

  // Zoom out.
  ZoomTarget zoomTarget2{CSSRect(0, 0, 100, 100)};
  apzc->ZoomToRect(zoomTarget2, 0);

  // Run the animation a few times to get it going.
  for (uint32_t i = 0; i < 2; i++) {
    SampleAnimationOneFrame();
  }

  // Check that it is decreasing in scale.
  float prevScale =
      apzc->GetCurrentPinchZoomScale(AsyncPanZoomController::eForCompositing)
          .scale;
  for (uint32_t i = 0; i < 2; i++) {
    SampleAnimationOneFrame();
    float scale =
        apzc->GetCurrentPinchZoomScale(AsyncPanZoomController::eForCompositing)
            .scale;
    ASSERT_GT(prevScale, scale);
    prevScale = scale;
  }

  float offset =
      apzc->GetCurrentAsyncScrollOffset(AsyncPanZoomController::eForCompositing)
          .x;

  // Change the scrollable rect slightly to trigger a reclamp.
  ScrollMetadata metadata2 = metadata;
  metadata2.GetMetrics().SetScrollableRect(CSSRect(0, 0, 100, 1000.2));
  apzc->NotifyMainThreadTransaction(
      metadata2,
      LayersUpdateFlags{.mIsFirstPaint = false, .mThisLayerTreeUpdated = true});

  float newOffset =
      apzc->GetCurrentAsyncScrollOffset(AsyncPanZoomController::eForCompositing)
          .x;

  ASSERT_EQ(newOffset, offset);
}

TEST_F(APZCBasicTester, ZoomToRectAndCompositionBoundsChange) {
  // We want to check that content sending a composition bounds change (due to
  // addition of scrollbars) during a zoom animation does not cause us to take
  // the out of date content resolution.

  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetCompositionBoundsWidthIgnoringScrollbars(ParentLayerCoord{100});
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 1000));
  metrics.SetLayoutViewport(CSSRect(0, 0, 100, 100));
  metrics.SetVisualScrollOffset(CSSPoint(0, 0));
  metrics.SetZoom(CSSToParentLayerScale(1.0));
  metrics.SetIsRootContent(true);
  apzc->SetFrameMetrics(metrics);

  MakeApzcZoomable();

  // Start a zoom to a rect.
  ZoomTarget zoomTarget{CSSRect(25, 25, 25, 25)};
  apzc->ZoomToRect(zoomTarget, 0);

  // Run the animation a few times to get it going.
  // Check that it is increasing in scale.
  float prevScale =
      apzc->GetCurrentPinchZoomScale(AsyncPanZoomController::eForCompositing)
          .scale;
  for (uint32_t i = 0; i < 3; i++) {
    SampleAnimationOneFrame();
    float scale =
        apzc->GetCurrentPinchZoomScale(AsyncPanZoomController::eForCompositing)
            .scale;
    ASSERT_GE(scale, prevScale);
    prevScale = scale;
  }

  EXPECT_TRUE(apzc->IsAsyncZooming());

  // Simulate the appearance of a scrollbar by reducing the width of
  // the composition bounds, while keeping
  // mCompositionBoundsWidthIgnoringScrollbars unchanged.
  ScrollMetadata metadata2 = metadata;
  metadata2.GetMetrics().SetCompositionBounds(ParentLayerRect(0, 0, 90, 100));
  apzc->NotifyMainThreadTransaction(
      metadata2,
      LayersUpdateFlags{.mIsFirstPaint = false, .mThisLayerTreeUpdated = true});

  float scale =
      apzc->GetCurrentPinchZoomScale(AsyncPanZoomController::eForCompositing)
          .scale;

  ASSERT_EQ(scale, prevScale);

  // Run the rest of the animation to completion, should take 250ms/16.67ms = 15
  // frames total, but do extra to make sure.
  for (uint32_t i = 0; i < 30; i++) {
    SampleAnimationOneFrame();
    scale =
        apzc->GetCurrentPinchZoomScale(AsyncPanZoomController::eForCompositing)
            .scale;
    ASSERT_GE(scale, prevScale);
    prevScale = scale;
  }

  EXPECT_FALSE(apzc->IsAsyncZooming());
}

TEST_F(APZCBasicTester, StartTolerance) {
  SCOPED_GFX_PREF_FLOAT("apz.touch_start_tolerance", 10 / tm->GetDPI());

  FrameMetrics fm;
  fm.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  fm.SetScrollableRect(CSSRect(0, 0, 100, 300));
  fm.SetVisualScrollOffset(CSSPoint(0, 50));
  fm.SetIsRootContent(true);
  apzc->SetFrameMetrics(fm);

  TouchDown(apzc, {50, 50}, mcc->Time());

  CSSPoint initialScrollOffset =
      apzc->GetFrameMetrics().GetVisualScrollOffset();

  mcc->AdvanceByMillis(1);
  TouchMove(apzc, {50, 70}, mcc->Time());

  // Expect 10 pixels of scrolling: the distance from (50,50) to (50,70)
  // minus the 10-pixel touch start tolerance.
  ASSERT_EQ(initialScrollOffset.y - 10,
            apzc->GetFrameMetrics().GetVisualScrollOffset().y);

  mcc->AdvanceByMillis(1);
  TouchMove(apzc, {50, 90}, mcc->Time());

  // Expect 30 pixels of scrolling: the distance from (50,50) to (50,90)
  // minus the 10-pixel touch start tolerance.
  ASSERT_EQ(initialScrollOffset.y - 30,
            apzc->GetFrameMetrics().GetVisualScrollOffset().y);

  // Clean up by ending the touch gesture.
  mcc->AdvanceByMillis(1);
  TouchUp(apzc, {50, 90}, mcc->Time());
}

TEST_F(APZCBasicTester, Bug1198900) {
  // This is just a test that cancels a wheel event to make sure it doesn't
  // crash. The wheel block needs to be waiting for a content response for the
  // cancellation to have any effect.
  MakeApzcWaitForMainThread();

  uint64_t blockId =
      Wheel(apzc, ScreenIntPoint(100, 50), ScreenPoint(0, 10), mcc->Time())
          .mInputBlockId;
  // The event is sitting in the input queue, so it hasn't scrolled anything.
  EXPECT_EQ(CSSPoint(0, 0), apzc->GetFrameMetrics().GetVisualScrollOffset());

  apzc->ContentReceivedInputBlock(blockId, /* preventDefault= */ true);
  // The block was cancelled, so the event never gets processed.
  EXPECT_EQ(CSSPoint(0, 0), apzc->GetFrameMetrics().GetVisualScrollOffset());
}

// A helper class for the ImmediatelyInterruptedSmoothScroll_Bug1984589
// test below, which overrides APZCTreeManager::GetFrameTime() to
// advance the time by 1ms every time GetFrameTime() is queried. This
// is needed to reproduce the bug (specifically to ensure that in the
// NotifyMainThreadTransaction call with two scroll updates, some time has
// elapsed between the two updates).
class APZCFrameTimeTester : public APZCBasicTester {
  class FrameTimeAPZCTreeManager : public TestAPZCTreeManager {
   public:
    explicit FrameTimeAPZCTreeManager(MockContentControllerDelayed* aMcc)
        : TestAPZCTreeManager(aMcc) {}

   protected:
    SampleTime GetFrameTime() override {
      SampleTime result = mcc->GetSampleTime();
      mcc->AdvanceByMillis(1);
      return result;
    }
  };

 protected:
  TestAPZCTreeManager* CreateTreeManager() override {
    return new FrameTimeAPZCTreeManager(mcc);
  }
};

TEST_F(APZCFrameTimeTester, ImmediatelyInterruptedSmoothScroll_Bug1984589) {
  // Set up a vertically scrollable scroll frame, with the starting scroll
  // position at the bottom.
  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 1000));
  metrics.SetLayoutViewport(CSSRect(0, 900, 100, 100));
  metrics.SetZoom(CSSToParentLayerScale(1.0));
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetVisualScrollOffset(CSSPoint(0, 900));
  metrics.SetIsRootContent(true);
  apzc->SetFrameMetrics(metrics);

  // Simulate a main-thread transaction with two absolute scroll updates with
  // SmoothMsd scroll mode: one that scrolls back to the top, and one that
  // "interrupts" it by scrolling back to the bottom.
  nsTArray<ScrollPositionUpdate> scrollUpdates;
  scrollUpdates.AppendElement(ScrollPositionUpdate::NewSmoothScroll(
      ScrollMode::SmoothMsd, ScrollOrigin::Other,
      CSSPoint::ToAppUnits(CSSPoint(0, 0)), ScrollTriggeredByScript::Yes,
      nullptr, ViewportType::Layout));
  scrollUpdates.AppendElement(ScrollPositionUpdate::NewSmoothScroll(
      ScrollMode::SmoothMsd, ScrollOrigin::Other,
      CSSPoint::ToAppUnits(CSSPoint(0, 900)), ScrollTriggeredByScript::Yes,
      nullptr, ViewportType::Layout));
  metadata.SetScrollUpdates(scrollUpdates);
  metrics.SetScrollGeneration(scrollUpdates.LastElement().GetGeneration());
  apzc->NotifyMainThreadTransaction(
      metadata,
      LayersUpdateFlags{.mIsFirstPaint = false, .mThisLayerTreeUpdated = true});

  // Sample smooth scroll animations until they complete,
  // and assert that at no point does the scroll position leave (0, 900).
  do {
    ASSERT_EQ(apzc->GetFrameMetrics().GetVisualScrollOffset().y, 900);
  } while (SampleAnimationOneFrame());
}
