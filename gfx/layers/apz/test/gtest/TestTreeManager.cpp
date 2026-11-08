/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "APZCTreeManagerTester.h"
#include "APZTestCommon.h"
#include "InputUtils.h"
#include "Units.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/StaticPrefs_apz.h"

class APZCTreeManagerGenericTester : public APZCTreeManagerTester {
 protected:
  void CreateSimpleMultiLayerTree() {
    const char* treeShape = "x(xx)";
    // LayerID               0 12
    LayerIntRect layerVisibleRect[] = {
        LayerIntRect(0, 0, 100, 100),
        LayerIntRect(0, 0, 100, 50),
        LayerIntRect(0, 50, 100, 50),
    };
    CreateScrollData(treeShape, layerVisibleRect);
  }

  void CreatePotentiallyLeakingTree() {
    const char* treeShape = "x(x(x(x))x(x(x)))";
    // LayerID               0 1 2 3  4 5 6
    CreateScrollData(treeShape);
    SetScrollableFrameMetrics(layers[0], START_SCROLL_ID);
    SetScrollableFrameMetrics(layers[2], START_SCROLL_ID + 1);
    SetScrollableFrameMetrics(layers[5], START_SCROLL_ID + 1);
    SetScrollableFrameMetrics(layers[3], START_SCROLL_ID + 2);
    SetScrollableFrameMetrics(layers[6], START_SCROLL_ID + 3);
  }

  void CreateTwoLayerTree(int32_t aRootContentLayerIndex) {
    const char* treeShape = "x(x)";
    // LayerID               0 1
    LayerIntRect layerVisibleRect[] = {
        LayerIntRect(0, 0, 100, 100),
        LayerIntRect(0, 0, 100, 100),
    };
    CreateScrollData(treeShape, layerVisibleRect);
    SetScrollableFrameMetrics(layers[0], START_SCROLL_ID);
    SetScrollableFrameMetrics(layers[1], START_SCROLL_ID + 1);
    SetScrollHandoff(layers[1], layers[0]);

    // Make layers[aRootContentLayerIndex] the root content
    ModifyFrameMetrics(layers[aRootContentLayerIndex],
                       [](ScrollMetadata& sm, FrameMetrics& fm) {
                         fm.SetIsRootContent(true);
                       });
  }
};

TEST_F(APZCTreeManagerGenericTester, ScrollablePaintedLayers) {
  CreateSimpleMultiLayerTree();
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);

  // both layers have the same scrollId
  SetScrollableFrameMetrics(layers[1], START_SCROLL_ID);
  SetScrollableFrameMetrics(layers[2], START_SCROLL_ID);
  UpdateHitTestingTree();

  TestAsyncPanZoomController* nullAPZC = nullptr;
  // so they should have the same APZC
  EXPECT_FALSE(HasScrollableFrameMetrics(layers[0]));
  EXPECT_NE(nullAPZC, ApzcOf(layers[1]));
  EXPECT_NE(nullAPZC, ApzcOf(layers[2]));
  EXPECT_EQ(ApzcOf(layers[1]), ApzcOf(layers[2]));
}

TEST_F(APZCTreeManagerGenericTester, Bug1068268) {
  CreatePotentiallyLeakingTree();
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);

  UpdateHitTestingTree();
  RefPtr<HitTestingTreeNode> root = manager->GetRootNode();
  RefPtr<HitTestingTreeNode> node2 = root->GetFirstChild()->GetFirstChild();
  RefPtr<HitTestingTreeNode> node5 = root->GetLastChild()->GetLastChild();

  EXPECT_EQ(ApzcOf(layers[2]), node5->GetApzc());
  EXPECT_EQ(ApzcOf(layers[2]), node2->GetApzc());
  EXPECT_EQ(ApzcOf(layers[0]), ApzcOf(layers[2])->GetParent());
  EXPECT_EQ(ApzcOf(layers[2]), ApzcOf(layers[5]));

  EXPECT_EQ(node2->GetFirstChild(), node2->GetLastChild());
  EXPECT_EQ(ApzcOf(layers[3]), node2->GetLastChild()->GetApzc());
  EXPECT_EQ(node5->GetFirstChild(), node5->GetLastChild());
  EXPECT_EQ(ApzcOf(layers[6]), node5->GetLastChild()->GetApzc());
  EXPECT_EQ(ApzcOf(layers[2]), ApzcOf(layers[3])->GetParent());
  EXPECT_EQ(ApzcOf(layers[5]), ApzcOf(layers[6])->GetParent());
}

class APZCTreeManagerGenericTesterMock : public APZCTreeManagerGenericTester {
 public:
  APZCTreeManagerGenericTesterMock() { CreateMockHitTester(); }
};

TEST_F(APZCTreeManagerGenericTesterMock, Bug1194876) {
  // Create a layer tree with parent and child scrollable layers, with the
  // child being the root content.
  CreateTwoLayerTree(1);
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);
  UpdateHitTestingTree();

  uint64_t blockId;
  nsTArray<ScrollableLayerGuid> targets;

  // First touch goes down, APZCTM will hit layers[1] because it is on top of
  // layers[0], but we tell it the real target APZC is layers[0].
  MultiTouchInput mti;
  mti = CreateMultiTouchInput(MultiTouchInput::MULTITOUCH_START, mcc->Time());
  mti.mTouches.AppendElement(
      SingleTouchData(0, ScreenIntPoint(25, 50), ScreenSize(0, 0), 0, 0));
  QueueMockHitResult(START_SCROLL_ID + 1,
                     {CompositorHitTestFlags::eVisibleToHitTest,
                      CompositorHitTestFlags::eIrregularArea});
  blockId = manager->ReceiveInputEvent(mti).mInputBlockId;
  manager->ContentReceivedInputBlock(blockId, false);
  targets.AppendElement(ApzcOf(layers[0])->GetGuid());
  manager->SetTargetAPZC(blockId, targets);

  // Around here, the above touch will get processed by ApzcOf(layers[0])

  // Second touch goes down (first touch remains down), APZCTM will again hit
  // layers[1]. Again we tell it both touches landed on layers[0], but because
  // layers[1] is the RCD layer, it will end up being the multitouch target.
  mti.mTouches.AppendElement(
      SingleTouchData(1, ScreenIntPoint(75, 50), ScreenSize(0, 0), 0, 0));
  // Each touch will get hit-tested, so queue two hit-test results.
  QueueMockHitResult(START_SCROLL_ID + 1,
                     {CompositorHitTestFlags::eVisibleToHitTest,
                      CompositorHitTestFlags::eIrregularArea});
  QueueMockHitResult(START_SCROLL_ID + 1,
                     {CompositorHitTestFlags::eVisibleToHitTest,
                      CompositorHitTestFlags::eIrregularArea});
  blockId = manager->ReceiveInputEvent(mti).mInputBlockId;
  manager->ContentReceivedInputBlock(blockId, false);
  targets.AppendElement(ApzcOf(layers[0])->GetGuid());
  manager->SetTargetAPZC(blockId, targets);

  // Around here, the above multi-touch will get processed by ApzcOf(layers[1]).
  // We want to ensure that ApzcOf(layers[0]) has had its state cleared, because
  // otherwise it will do things like dispatch spurious long-tap events.

  EXPECT_CALL(*mcc, HandleTap(TapType::eLongTap, _, _, _, _, _)).Times(0);
}

TEST_F(APZCTreeManagerGenericTesterMock, TargetChangesMidGesture_Bug1570559) {
  // Create a layer tree with parent and child scrollable layers, with the
  // parent being the root content.
  CreateTwoLayerTree(0);
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);
  UpdateHitTestingTree();

  uint64_t blockId;
  nsTArray<ScrollableLayerGuid> targets;

  // First touch goes down. APZCTM hits the child layer because it is on top
  // (and we confirm this target), but do not prevent-default the event, causing
  // the child APZC's gesture detector to start a long-tap timeout task.
  MultiTouchInput mti =
      CreateMultiTouchInput(MultiTouchInput::MULTITOUCH_START, mcc->Time());
  mti.mTouches.AppendElement(
      SingleTouchData(0, ScreenIntPoint(25, 50), ScreenSize(0, 0), 0, 0));
  QueueMockHitResult(START_SCROLL_ID + 1,
                     {CompositorHitTestFlags::eVisibleToHitTest,
                      CompositorHitTestFlags::eIrregularArea});
  blockId = manager->ReceiveInputEvent(mti).mInputBlockId;
  manager->ContentReceivedInputBlock(blockId, /* default prevented = */ false);
  targets.AppendElement(ApzcOf(layers[1])->GetGuid());
  manager->SetTargetAPZC(blockId, targets);

  // Second touch goes down (first touch remains down). APZCTM again hits the
  // child and we confirm this, but multi-touch events are routed to the root
  // content APZC which is the parent. This event is prevent-defaulted, so we
  // clear the parent's gesture state. The bug is that we fail to clear the
  // child's gesture state.
  mti.mTouches.AppendElement(
      SingleTouchData(1, ScreenIntPoint(75, 50), ScreenSize(0, 0), 0, 0));
  // Each touch will get hit-tested, so queue two hit-test results.
  QueueMockHitResult(START_SCROLL_ID + 1,
                     {CompositorHitTestFlags::eVisibleToHitTest,
                      CompositorHitTestFlags::eIrregularArea});
  QueueMockHitResult(START_SCROLL_ID + 1,
                     {CompositorHitTestFlags::eVisibleToHitTest,
                      CompositorHitTestFlags::eIrregularArea});
  blockId = manager->ReceiveInputEvent(mti).mInputBlockId;
  manager->ContentReceivedInputBlock(blockId, /* default prevented = */ true);
  targets.AppendElement(ApzcOf(layers[1])->GetGuid());
  manager->SetTargetAPZC(blockId, targets);

  // If we've failed to clear the child's gesture state, then the long tap
  // timeout task will fire in TearDown() and a long-tap will be dispatched.
  EXPECT_CALL(*mcc, HandleTap(TapType::eLongTap, _, _, _, _, _)).Times(0);
}

// The next two tests check that APZ clamps the scroll offset it composites even
// if the main thread fails to do so. (The main thread will always clamp its
// scroll offset internally, but it may not send APZ the clamped version for
// scroll offset synchronization reasons.)
TEST_F(APZCTreeManagerTester, Bug1551582) {
  // The simple layer tree has a scrollable rect of 500x500 and a composition
  // bounds of 200x200, leading to a scroll range of (0,0,300,300).
  CreateSimpleScrollingLayer();
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);
  UpdateHitTestingTree();

  // Simulate the main thread scrolling to the end of the scroll range.
  ModifyFrameMetrics(root, [](ScrollMetadata& aSm, FrameMetrics& aMetrics) {
    aMetrics.SetLayoutScrollOffset(CSSPoint(300, 300));
    nsTArray<ScrollPositionUpdate> scrollUpdates;
    scrollUpdates.AppendElement(ScrollPositionUpdate::NewScroll(
        ScrollOrigin::Other, CSSPoint::ToAppUnits(CSSPoint(300, 300))));
    aSm.SetScrollUpdates(scrollUpdates);
    aMetrics.SetScrollGeneration(scrollUpdates.LastElement().GetGeneration());
  });
  UpdateHitTestingTree();

  // Sanity check.
  RefPtr<TestAsyncPanZoomController> apzc = ApzcOf(root);
  CSSPoint compositedScrollOffset = apzc->GetCompositedScrollOffset();
  EXPECT_EQ(CSSPoint(300, 300), compositedScrollOffset);

  // Simulate the main thread shrinking the scrollable rect to 400x400 (and
  // thereby the scroll range to (0,0,200,200) without sending a new scroll
  // offset update for the clamped scroll position (200,200).
  ModifyFrameMetrics(root, [](ScrollMetadata& aSm, FrameMetrics& aMetrics) {
    aMetrics.SetScrollableRect(CSSRect(0, 0, 400, 400));
  });
  UpdateHitTestingTree();

  // Check that APZ has clamped the scroll offset to (200,200) for us.
  compositedScrollOffset = apzc->GetCompositedScrollOffset();
  EXPECT_EQ(CSSPoint(200, 200), compositedScrollOffset);
}
TEST_F(APZCTreeManagerTester, Bug1557424) {
  // The simple layer tree has a scrollable rect of 500x500 and a composition
  // bounds of 200x200, leading to a scroll range of (0,0,300,300).
  CreateSimpleScrollingLayer();
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);
  UpdateHitTestingTree();

  // Simulate the main thread scrolling to the end of the scroll range.
  ModifyFrameMetrics(root, [](ScrollMetadata& aSm, FrameMetrics& aMetrics) {
    aMetrics.SetLayoutScrollOffset(CSSPoint(300, 300));
    nsTArray<ScrollPositionUpdate> scrollUpdates;
    scrollUpdates.AppendElement(ScrollPositionUpdate::NewScroll(
        ScrollOrigin::Other, CSSPoint::ToAppUnits(CSSPoint(300, 300))));
    aSm.SetScrollUpdates(scrollUpdates);
    aMetrics.SetScrollGeneration(scrollUpdates.LastElement().GetGeneration());
  });
  UpdateHitTestingTree();

  // Sanity check.
  RefPtr<TestAsyncPanZoomController> apzc = ApzcOf(root);
  CSSPoint compositedScrollOffset = apzc->GetCompositedScrollOffset();
  EXPECT_EQ(CSSPoint(300, 300), compositedScrollOffset);

  // Simulate the main thread expanding the composition bounds to 300x300 (and
  // thereby shrinking the scroll range to (0,0,200,200) without sending a new
  // scroll offset update for the clamped scroll position (200,200).
  ModifyFrameMetrics(root, [](ScrollMetadata& aSm, FrameMetrics& aMetrics) {
    aMetrics.SetCompositionBounds(ParentLayerRect(0, 0, 300, 300));
  });
  UpdateHitTestingTree();

  // Check that APZ has clamped the scroll offset to (200,200) for us.
  compositedScrollOffset = apzc->GetCompositedScrollOffset();
  EXPECT_EQ(CSSPoint(200, 200), compositedScrollOffset);
}

TEST_F(APZCTreeManagerTester, Bug1805601) {
  // The simple layer tree has a scrollable rect of 500x500 and a composition
  // bounds of 200x200, leading to a scroll range of (0,0,300,300) at unit zoom.
  CreateSimpleScrollingLayer();
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);
  UpdateHitTestingTree();
  RefPtr<TestAsyncPanZoomController> apzc = ApzcOf(root);
  FrameMetrics& compositorMetrics = apzc->GetFrameMetrics();
  EXPECT_EQ(CSSRect(0, 0, 300, 300), compositorMetrics.CalculateScrollRange());

  // Zoom the page in by 2x. This needs to be reflected in each of the pres
  // shell resolution, cumulative resolution, and zoom. This makes the scroll
  // range (0,0,400,400).
  compositorMetrics.SetZoom(CSSToParentLayerScale(2.0));
  EXPECT_EQ(CSSRect(0, 0, 400, 400), compositorMetrics.CalculateScrollRange());

  // Scroll to an area inside the 2x scroll range but outside the original one.
  compositorMetrics.ClampAndSetVisualScrollOffset(CSSPoint(350, 350));
  EXPECT_EQ(CSSPoint(350, 350), compositorMetrics.GetVisualScrollOffset());

  // Simulate a main-thread update where the zoom is reset to 1x but the visual
  // scroll offset is unmodified.
  ModifyFrameMetrics(root, [](ScrollMetadata& aSm, FrameMetrics& aMetrics) {
    // Changes to |compositorMetrics| are not reflected in |aMetrics|, which
    // is the "layer tree" copy, so we don't need to explicitly set the zoom to
    // 1.0 (it still has that as the initial value), but we do need to set
    // the visual scroll offset to the same value the APZ copy has.
    aMetrics.SetVisualScrollOffset(CSSPoint(350, 350));

    // Needed to get APZ to accept the 1.0 zoom in |aMetrics|, otherwise
    // it will act as though its zoom is newer (e.g. an async zoom that hasn't
    // been repainted yet) and ignore ours.
    aSm.SetResolutionUpdated(true);
  });
  UpdateHitTestingTree();

  // Check that APZ clamped the scroll offset.
  EXPECT_EQ(CSSRect(0, 0, 300, 300), compositorMetrics.CalculateScrollRange());
  EXPECT_EQ(CSSPoint(300, 300), compositorMetrics.GetVisualScrollOffset());
}

TEST_F(APZCTreeManagerTester,
       InstantKeyScrollBetweenTwoSamplingsWithSameTimeStamp) {
  if (!StaticPrefs::apz_keyboard_enabled_AtStartup()) {
    // On Android apz.keyboard.enabled is false by default and it's can't be
    // changed here since it's `mirror: once`, so we just skip this test.
    return;
  }

  // For instant scrolling, i.e. no async animation should not be involved.
  SCOPED_GFX_PREF_BOOL("general.smoothScroll", false);

  // Set up a keyboard shortcuts map to scroll page down.
  AutoTArray<KeyboardShortcut, 1> shortcuts{KeyboardShortcut(
      KeyboardInput::KEY_DOWN, 0, 0, 0, 0,
      KeyboardScrollAction(
          KeyboardScrollAction::KeyboardScrollActionType::eScrollPage, true))};
  KeyboardMap keyboardMap(std::move(shortcuts));
  manager->SetKeyboardMap(keyboardMap);

  // Set up a scrollable layer.
  CreateSimpleScrollingLayer();
  ScopedLayerTreeRegistration registration(LayersId{0}, mcc);
  UpdateHitTestingTree();

  // Setup the scrollable layer is scrollable by key events.
  FocusTarget focusTarget;
  focusTarget.mSequenceNumber = 1;
  focusTarget.mData =
      AsVariant<FocusTarget::ScrollTargets>({START_SCROLL_ID, START_SCROLL_ID});
  manager->UpdateFocusState(LayersId{0}, LayersId{0}, focusTarget);

  // A vsync tick happens.
  mcc->AdvanceByMillis(16);

  // The first sampling happens, there's no change have happened, thus no need
  // to composite.
  EXPECT_FALSE(manager->AdvanceAnimations(mcc->GetSampleTime()));

  // A key event causing scroll page down happens.
  WidgetKeyboardEvent widgetEvent(true, eKeyDown, nullptr);
  KeyboardInput input(widgetEvent);
  (void)manager->ReceiveInputEvent(input);

  // Simulate WebRender compositing frames until APZ tells it the scroll offset
  // has stopped changing.
  // Important to trigger the bug: the first composite has the same time stamp
  // as the earlier one above.
  ParentLayerPoint compositedScrollOffset;
  while (true) {
    bool needMoreFrames = manager->AdvanceAnimations(mcc->GetSampleTime());
    compositedScrollOffset = ApzcOf(root)->GetCurrentAsyncScrollOffset(
        AsyncPanZoomController::eForCompositing);
    if (!needMoreFrames) {
      break;
    }
    mcc->AdvanceBy(TimeDuration::FromMilliseconds(16));
  }

  // Check that the effect of the keyboard scroll has been composited.
  EXPECT_GT(compositedScrollOffset.y, 0);
}

TEST_F(APZCTreeManagerGenericTesterMock,
       PanGestureWithCtrlModifier_Bug1917488) {
  // The outer layer represents the browser chrome, and has layers id 0.
  // The inner layer represents web content, and will initally get layers id 1.
  const char* treeShape = "x(x)";
  LayerIntRect layerVisibleRect[] = {
      LayerIntRect(0, 0, 100, 100),
      LayerIntRect(0, 0, 100, 100),
  };
  CreateScrollData(treeShape, layerVisibleRect);

  // We need one of these for every LayersId the test will use!
  ScopedLayerTreeRegistration registration0(LayersId{0}, mcc);
  ScopedLayerTreeRegistration registration1(LayersId{1}, mcc);
  ScopedLayerTreeRegistration registration2(LayersId{2}, mcc);

  // In this test, we only bother to give the inner layer an APZC.
  ViewID scrollId = START_SCROLL_ID;
  SetScrollableFrameMetrics(layers[1], scrollId, CSSRect(0, 0, 100, 100));

  // Set the referent id of the root layer to 1.
  // Note that this causes the *descendants* of the root layer
  // to get a layers id of 1 during the hit-testing tree update.
  LayersId tab1LayersId{1};
  ScrollableLayerGuid tab1Guid(tab1LayersId, 0, scrollId);
  root->SetReferentId(tab1LayersId);
  UpdateHitTestingTree();

  // Perform a pan gesture on the content layer.
  // For the START event, do not use the PanGesture() helper, because we want to
  // check the mLayersId that APZ sets on the input event object. This
  // determines what content process the event will gets dispatched to.
  ScreenIntPoint panPoint(50, 50);
  QueueMockHitResult(tab1Guid);
  PanGestureInput panStart(PanGestureInput::PANGESTURE_START, mcc->Time(),
                           panPoint, ScreenPoint(0, 10), MODIFIER_NONE);
  manager->ReceiveInputEvent(panStart);
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(tab1Guid);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, panPoint,
             ScreenPoint(0, 10), mcc->Time());
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(tab1Guid);
  PanGesture(PanGestureInput::PANGESTURE_END, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time());

  // The layers id assigned to the events of this gesture should be 1.
  EXPECT_EQ(panStart.mLayersId, tab1LayersId);

  // Simulate a tab switch by updating the referent id of the root layer
  // to a new layers id, 2.
  LayersId tab2LayersId{2};
  ScrollableLayerGuid tab2Guid(tab2LayersId, 0, scrollId);
  root->SetReferentId(tab2LayersId);
  UpdateHitTestingTree();

  // Perform another pan gesture on the content layer, but this time with
  // the Control modifier held down.
  QueueMockHitResult(tab2Guid);
  PanGestureInput zoomStart(PanGestureInput::PANGESTURE_START, mcc->Time(),
                            panPoint, ScreenPoint(0, 10), MODIFIER_CONTROL);
  manager->ReceiveInputEvent(zoomStart);
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(tab2Guid);
  PanGesture(PanGestureInput::PANGESTURE_PAN, manager, panPoint,
             ScreenPoint(0, 10), mcc->Time(), MODIFIER_CONTROL);
  mcc->AdvanceByMillis(5);
  QueueMockHitResult(tab2Guid);
  PanGesture(PanGestureInput::PANGESTURE_END, manager, panPoint,
             ScreenPoint(0, 0), mcc->Time(), MODIFIER_CONTROL);

  // The layers id assigned to the events in the zoom gesture should be 2.
  // (In the buggy scenario, it's 1, meaning the events get incorrectly
  // sent to the previous tab's content process.)
  EXPECT_EQ(zoomStart.mLayersId, tab2LayersId);
}

class APZDoubleTapInParentProcessTester
    : public APZCTreeManagerGenericTesterMock {
 protected:
  // A content document loaded in the parent process, such as about:license,
  // shares the chrome document's layers id. layers[0] is the chrome document's
  // root, layers[1] the content document's root, layers[2] a subframe in the
  // content document, layers[3] fixed content in the content document,
  // layers[4] a scroll container in the fixed content and layers[5] a scroll
  // container in the chrome document, e.g. the tab strip.
  void CreateParentProcessContentTree() {
    const char* treeShape = "x(x(x)x(x)x)";
    LayerIntRect layerVisibleRect[] = {
        LayerIntRect(0, 0, 100, 100), LayerIntRect(0, 20, 100, 80),
        LayerIntRect(0, 20, 50, 50),  LayerIntRect(50, 50, 50, 50),
        LayerIntRect(50, 50, 50, 50), LayerIntRect(0, 0, 100, 20),
    };
    CreateScrollData(treeShape, layerVisibleRect);
    SetScrollableFrameMetrics(layers[0], START_SCROLL_ID);
    SetScrollableFrameMetrics(layers[1], START_SCROLL_ID + 1);
    SetScrollableFrameMetrics(layers[2], START_SCROLL_ID + 2);
    layers[3]->SetFixedPositionScrollContainerId(START_SCROLL_ID + 1);
    SetScrollableFrameMetrics(layers[4], START_SCROLL_ID + 4);
    SetScrollableFrameMetrics(layers[5], START_SCROLL_ID + 5);
    ModifyFrameMetrics(layers[1], [](ScrollMetadata& sm, FrameMetrics& fm) {
      fm.SetIsRootContent(true);
    });

    registration =
        std::make_unique<ScopedLayerTreeRegistration>(LayersId{0}, mcc);
    UpdateHitTestingTree();

    ApzcOf(layers[1])->UpdateZoomConstraints(ZoomConstraints(
        true, true, CSSToParentLayerScale(0.25f), CSSToParentLayerScale(4.0f)));
  }

  UniquePtr<ScopedLayerTreeRegistration> registration;
};

TEST_F(APZDoubleTapInParentProcessTester, DoubleTapOnRootContent) {
  CreateParentProcessContentTree();

  EXPECT_CALL(*mcc, HandleTap(TapType::eDoubleTap, _, _,
                              ApzcOf(layers[1])->GetGuid(), _, _))
      .Times(1);

  QueueMockHitResult(START_SCROLL_ID + 1);
  TapGestureInput doubleTap(TapGestureInput::TAPGESTURE_DOUBLE, mcc->Time(),
                            ScreenIntPoint(75, 50), MODIFIER_NONE);
  manager->ReceiveInputEvent(doubleTap);
}

TEST_F(APZDoubleTapInParentProcessTester, DoubleTapOnSubframe) {
  CreateParentProcessContentTree();

  EXPECT_CALL(*mcc, HandleTap(TapType::eDoubleTap, _, _,
                              ApzcOf(layers[1])->GetGuid(), _, _))
      .Times(1);

  QueueMockHitResult(START_SCROLL_ID + 2);
  TapGestureInput doubleTap(TapGestureInput::TAPGESTURE_DOUBLE, mcc->Time(),
                            ScreenIntPoint(25, 40), MODIFIER_NONE);
  manager->ReceiveInputEvent(doubleTap);
}

TEST_F(APZDoubleTapInParentProcessTester, DoubleTapOnFixedContent) {
  CreateParentProcessContentTree();

  EXPECT_CALL(*mcc, HandleTap(TapType::eDoubleTap, _, _,
                              ApzcOf(layers[1])->GetGuid(), _, _))
      .Times(1);

  QueueMockHitResult(START_SCROLL_ID + 4);
  TapGestureInput doubleTap(TapGestureInput::TAPGESTURE_DOUBLE, mcc->Time(),
                            ScreenIntPoint(75, 75), MODIFIER_NONE);
  manager->ReceiveInputEvent(doubleTap);
}

TEST_F(APZDoubleTapInParentProcessTester, DoubleTapOnChrome) {
  CreateParentProcessContentTree();

  EXPECT_CALL(*mcc, HandleTap(TapType::eDoubleTap, _, _, _, _, _)).Times(0);

  QueueMockHitResult(START_SCROLL_ID + 5);
  TapGestureInput doubleTap(TapGestureInput::TAPGESTURE_DOUBLE, mcc->Time(),
                            ScreenIntPoint(50, 10), MODIFIER_NONE);
  manager->ReceiveInputEvent(doubleTap);
}
