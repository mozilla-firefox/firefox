/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stack>
#include <unordered_set>
#include "APZCTreeManager.h"
#include "AsyncPanZoomController.h"
#include "Compositor.h"             // for Compositor
#include "DragTracker.h"            // for DragTracker
#include "GenericFlingAnimation.h"  // for FLING_LOG
#include "HitTestingTreeNode.h"     // for HitTestingTreeNode
#include "InputBlockState.h"        // for InputBlockState
#include "InputData.h"              // for InputData, etc
#include "WRHitTester.h"            // for WRHitTester
#include "apz/src/APZUtils.h"
#include "mozilla/RecursiveMutex.h"
#include "mozilla/dom/BrowserParent.h"      // for AreRecordReplayTabsActive
#include "mozilla/dom/MouseEventBinding.h"  // for MouseEvent constants
#include "mozilla/dom/InteractiveWidget.h"
#include "mozilla/dom/Touch.h"  // for Touch
#include "mozilla/gfx/CompositorHitTestInfo.h"
#include "mozilla/gfx/LoggingConstants.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/gfx/gfxVars.h"            // for gfxVars
#include "mozilla/gfx/GPUParent.h"          // for GPUParent
#include "mozilla/gfx/Logging.h"            // for gfx::TreeLog
#include "mozilla/gfx/Point.h"              // for Point
#include "mozilla/layers/APZSampler.h"      // for APZSampler
#include "mozilla/layers/APZThreadUtils.h"  // for AssertOnControllerThread, etc
#include "mozilla/layers/APZUpdater.h"      // for APZUpdater
#include "mozilla/layers/APZUtils.h"        // for AsyncTransform
#include "mozilla/layers/AsyncDragMetrics.h"        // for AsyncDragMetrics
#include "mozilla/layers/CompositorBridgeParent.h"  // for CompositorBridgeParent, etc
#include "mozilla/layers/DoubleTapToZoom.h"         // for ZoomTarget
#include "mozilla/layers/MatrixMessage.h"
#include "mozilla/layers/ScrollableLayerGuid.h"
#include "mozilla/layers/UiCompositorControllerParent.h"
#include "mozilla/layers/WebRenderScrollDataWrapper.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/mozalloc.h"     // for operator new
#include "mozilla/Preferences.h"  // for Preferences
#include "mozilla/StaticPrefs_accessibility.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/ToString.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/EventStateManager.h"  // for WheelPrefs
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsDebug.h"                 // for NS_WARNING
#include "nsPoint.h"                 // for nsIntPoint
#include "nsThreadUtils.h"           // for NS_IsMainThread
#include "ScrollThumbUtils.h"        // for ComputeTransformForScrollThumb
#include "OverscrollHandoffState.h"  // for OverscrollHandoffState
#include "TreeTraversal.h"           // for ForEachNode, BreadthFirstSearch, etc
#include "Units.h"                   // for ParentlayerPixel
#include "GestureEventListener.h"  // for GestureEventListener::setLongTapEnabled
#include "UnitTransforms.h"        // for ViewAs

mozilla::LazyLogModule mozilla::layers::APZCTreeManager::sLog("apz.manager");
#define APZCTM_LOG(...) \
  MOZ_LOG(APZCTreeManager::sLog, LogLevel::Debug, (__VA_ARGS__))
#define APZCTM_LOGV(...) \
  MOZ_LOG(APZCTreeManager::sLog, LogLevel::Verbose, (__VA_ARGS__))

static mozilla::LazyLogModule sApzKeyLog("apz.key");
#define APZ_KEY_LOG(...) MOZ_LOG(sApzKeyLog, LogLevel::Debug, (__VA_ARGS__))

namespace mozilla {
namespace layers {

using mozilla::gfx::CompositorHitTestFlags;
using mozilla::gfx::CompositorHitTestInfo;
using mozilla::gfx::CompositorHitTestInvisibleToHit;
using mozilla::gfx::LOG_DEFAULT;

typedef mozilla::gfx::Point Point;
typedef mozilla::gfx::Point4D Point4D;
typedef mozilla::gfx::Matrix4x4 Matrix4x4;

typedef CompositorBridgeParent::LayerTreeState LayerTreeState;

struct APZCTreeManager::TreeBuildingState {
  TreeBuildingState(LayersId aRootLayersId, LayersId aOriginatingLayersId,
                    APZTestData* aTestData, uint32_t aPaintSequence,
                    bool aIsTestLoggingEnabled)
      : mOriginatingLayersId(aOriginatingLayersId),
        mPaintLogger(aTestData, aPaintSequence, aIsTestLoggingEnabled) {
    CompositorBridgeParent::CallWithIndirectShadowTree(
        aRootLayersId, [this](LayerTreeState& aState) -> void {
          mCompositorController = aState.GetCompositorController();
        });
  }

  typedef std::unordered_map<AsyncPanZoomController*, gfx::Matrix4x4>
      DeferredTransformMap;

  // State that doesn't change as we recurse in the tree building
  RefPtr<CompositorController> mCompositorController;
  const LayersId mOriginatingLayersId;
  const APZPaintLogHelper mPaintLogger;

  // State that is updated as we perform the tree build

  // A list of nodes that need to be destroyed at the end of the tree building.
  // This is initialized with all nodes in the old tree, and nodes are removed
  // from it as we reuse them in the new tree.
  nsTArray<RefPtr<HitTestingTreeNode>> mNodesToDestroy;

  // This map is populated as we place APZCs into the new tree. Its purpose is
  // to facilitate re-using the same APZC for different layers that scroll
  // together (and thus have the same ScrollableLayerGuid). The presShellId
  // doesn't matter for this purpose, and we move the map to the APZCTreeManager
  // after we're done building, so it's useful to have the presshell-ignoring
  // map for that.
  std::unordered_map<ScrollableLayerGuid, ApzcMapData,
                     ScrollableLayerGuid::HashIgnoringPresShellFn,
                     ScrollableLayerGuid::EqualIgnoringPresShellFn>
      mApzcMap;

  // This is populated with all the HitTestingTreeNodes that are scroll thumbs
  // and have a scrollthumb animation id (which indicates that they need to be
  // sampled for WebRender on the sampler thread).
  std::vector<HitTestingTreeNode*> mScrollThumbs;
  // This is populated with all the scroll target nodes. We use in conjunction
  // with mScrollThumbs to build APZCTreeManager::mScrollThumbInfo.
  std::unordered_map<ScrollableLayerGuid, HitTestingTreeNode*,
                     ScrollableLayerGuid::HashIgnoringPresShellFn,
                     ScrollableLayerGuid::EqualIgnoringPresShellFn>
      mScrollTargets;

  // During the tree building process, the perspective transform component
  // of the ancestor transforms of some APZCs can be "deferred" to their
  // children, meaning they are added to the children's ancestor transforms
  // instead. Those deferred transforms are tracked here.
  DeferredTransformMap mPerspectiveTransformsDeferredToChildren;

  // As we recurse down through the tree, this picks up the zoom animation id
  // from a node in the layer tree, and propagates it downwards to the nearest
  // APZC instance that is for an RCD node. Generally it will be set on the
  // root node of the layers (sub-)tree, which may not be same as the RCD node
  // for the subtree, and so we need this mechanism to ensure it gets propagated
  // to the RCD's APZC instance. Once it is set on the APZC instance, the value
  // is cleared back to Nothing().
  Maybe<uint64_t> mZoomAnimationId;

  // See corresponding members of APZCTreeManager. These are the same thing, but
  // on the tree-walking state. They are populated while walking the tree in
  // a layers update, and then moved into APZCTreeManager.
  std::vector<FixedPositionInfo> mFixedPositionInfo;
  std::vector<RootScrollbarInfo> mRootScrollbarInfo;
  std::vector<StickyPositionInfo> mStickyPositionInfo;

  // As we recurse down through reflayers in the tree, this picks up the
  // cumulative EventRegionsOverride flags from the reflayers, and is used to
  // apply them to descendant layers.
  std::stack<EventRegionsOverride> mOverrideFlags;

  std::vector<LayersId> mUpdatedLayersIds;
};

class APZCTreeManager::CheckerboardFlushObserver : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  explicit CheckerboardFlushObserver(APZCTreeManager* aTreeManager)
      : mTreeManager(aTreeManager) {
    MOZ_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    MOZ_ASSERT(obsSvc);
    if (obsSvc) {
      obsSvc->AddObserver(this, "APZ:FlushActiveCheckerboard", false);
    }
  }

  void Unregister() {
    MOZ_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    if (obsSvc) {
      obsSvc->RemoveObserver(this, "APZ:FlushActiveCheckerboard");
    }
    mTreeManager = nullptr;
  }

 protected:
  virtual ~CheckerboardFlushObserver() = default;

 private:
  RefPtr<APZCTreeManager> mTreeManager;
};

NS_IMPL_ISUPPORTS(APZCTreeManager::CheckerboardFlushObserver, nsIObserver)

NS_IMETHODIMP
APZCTreeManager::CheckerboardFlushObserver::Observe(nsISupports* aSubject,
                                                    const char* aTopic,
                                                    const char16_t*) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mTreeManager.get());

  RecursiveMutexAutoLock lock(mTreeManager->mTreeLock);
  if (mTreeManager->mRootNode) {
    ForEachNode<ReverseIterator>(
        mTreeManager->mRootNode.get(), [](HitTestingTreeNode* aNode) {
          if (aNode->IsPrimaryHolder()) {
            MOZ_ASSERT(aNode->GetApzc());
            aNode->GetApzc()->FlushActiveCheckerboardReport();
          }
        });
  }
  if (XRE_IsGPUProcess()) {
    if (gfx::GPUParent* gpu = gfx::GPUParent::GetSingleton()) {
      nsCString topic("APZ:FlushActiveCheckerboard:Done");
      Unused << gpu->SendNotifyUiObservers(topic);
    }
  } else {
    MOZ_ASSERT(XRE_IsParentProcess());
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    if (obsSvc) {
      obsSvc->NotifyObservers(nullptr, "APZ:FlushActiveCheckerboard:Done",
                              nullptr);
    }
  }
  return NS_OK;
}

/**
 * A RAII class used for setting the focus sequence number on input events
 * as they are being processed. Any input event is assumed to be potentially
 * focus changing unless explicitly marked otherwise.
 */
class MOZ_RAII AutoFocusSequenceNumberSetter {
 public:
  AutoFocusSequenceNumberSetter(FocusState& aFocusState, InputData& aEvent)
      : mFocusState(aFocusState), mEvent(aEvent), mMayChangeFocus(true) {}

  void MarkAsNonFocusChanging() { mMayChangeFocus = false; }

  ~AutoFocusSequenceNumberSetter() {
    if (mMayChangeFocus) {
      mFocusState.ReceiveFocusChangingEvent();

      APZ_KEY_LOG(
          "Marking input with type=%d as focus changing with seq=%" PRIu64 "\n",
          static_cast<int>(mEvent.mInputType),
          mFocusState.LastAPZProcessedEvent());
    } else {
      APZ_KEY_LOG(
          "Marking input with type=%d as non focus changing with seq=%" PRIu64
          "\n",
          static_cast<int>(mEvent.mInputType),
          mFocusState.LastAPZProcessedEvent());
    }

    mEvent.mFocusSequenceNumber = mFocusState.LastAPZProcessedEvent();
  }

 private:
  FocusState& mFocusState;
  InputData& mEvent;
  bool mMayChangeFocus;
};

APZCTreeManager::APZCTreeManager(LayersId aRootLayersId,
                                 UniquePtr<IAPZHitTester> aHitTester)
    : mTestSampleTime(Nothing(), "APZCTreeManager::mTestSampleTime"),
      mInputQueue(new InputQueue()),
      mMapLock("APZCMapLock"),
      mRootLayersId(aRootLayersId),
      mSampler(nullptr),
      mUpdater(nullptr),
      mTreeLock("APZCTreeLock"),
      mRetainedTouchIdentifier(-1),
      mInScrollbarTouchDrag(false),
      mCurrentMousePosition(ScreenPoint(),
                            "APZCTreeManager::mCurrentMousePosition"),
      mApzcTreeLog("apzctree"),
      mTestDataLock("APZTestDataLock"),
      mDPI(160.0),
      mHitTester(std::move(aHitTester)),
      mScrollGenerationLock("APZScrollGenerationLock"),
      mInteractiveWidget(
          dom::InteractiveWidgetUtils::DefaultInteractiveWidgetMode()),
      mIsSoftwareKeyboardVisible(false),
      mHaveOOPIframes(false) {
  AsyncPanZoomController::InitializeGlobalState();
  mApzcTreeLog.ConditionOnPrefFunction(StaticPrefs::apz_printtree);

  if (!mHitTester) {
    mHitTester = MakeUnique<WRHitTester>();
  }
  mHitTester->Initialize(this);
}

APZCTreeManager::~APZCTreeManager() = default;

void APZCTreeManager::Init() {
  RefPtr<APZCTreeManager> self(this);
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "layers::APZCTreeManager::Init",
      [self] { self->mFlushObserver = new CheckerboardFlushObserver(self); }));
}

already_AddRefed<APZCTreeManager> APZCTreeManager::Create(
    LayersId aRootLayersId, UniquePtr<IAPZHitTester> aHitTester) {
  RefPtr<APZCTreeManager> manager =
      new APZCTreeManager(aRootLayersId, std::move(aHitTester));
  manager->Init();
  return manager.forget();
}

void APZCTreeManager::SetSampler(APZSampler* aSampler) {
  // We're either setting the sampler or clearing it
  MOZ_ASSERT((mSampler == nullptr) != (aSampler == nullptr));
  mSampler = aSampler;
}

void APZCTreeManager::SetUpdater(APZUpdater* aUpdater) {
  // We're either setting the updater or clearing it
  MOZ_ASSERT((mUpdater == nullptr) != (aUpdater == nullptr));
  mUpdater = aUpdater;
}

void APZCTreeManager::NotifyLayerTreeAdopted(
    LayersId aLayersId, const RefPtr<APZCTreeManager>& aOldApzcTreeManager) {
  AssertOnUpdaterThread();

  if (aOldApzcTreeManager) {
    aOldApzcTreeManager->mFocusState.RemoveFocusTarget(aLayersId);
    // While we could move the focus target information from the old APZC tree
    // manager into this one, it's safer to not do that, as we'll probably have
    // that information repopulated soon anyway (on the next layers update).
  }

  // There may be focus updates from the tab's content process in flight
  // triggered by events that were processed by the old tree manager,
  // which this tree manager does not expect.
  // Resetting the focus state avoids this (the state will sync with content
  // on the next focus update).
  mFocusState.Reset();

  UniquePtr<APZTestData> adoptedData;
  if (aOldApzcTreeManager) {
    MutexAutoLock lock(aOldApzcTreeManager->mTestDataLock);
    auto it = aOldApzcTreeManager->mTestData.find(aLayersId);
    if (it != aOldApzcTreeManager->mTestData.end()) {
      adoptedData = std::move(it->second);
      aOldApzcTreeManager->mTestData.erase(it);
    }
  }
  if (adoptedData) {
    MutexAutoLock lock(mTestDataLock);
    mTestData[aLayersId] = std::move(adoptedData);
  }
}

void APZCTreeManager::NotifyLayerTreeRemoved(LayersId aLayersId) {
  AssertOnUpdaterThread();

  mFocusState.RemoveFocusTarget(aLayersId);

  {  // scope lock
    MutexAutoLock lock(mTestDataLock);
    mTestData.erase(aLayersId);
  }
}

already_AddRefed<AsyncPanZoomController> APZCTreeManager::NewAPZCInstance(
    LayersId aLayersId, GeckoContentController* aController) {
  return MakeRefPtr<AsyncPanZoomController>(
             aLayersId, this, mInputQueue, aController,
             AsyncPanZoomController::USE_GESTURE_DETECTOR)
      .forget();
}

void APZCTreeManager::SetTestSampleTime(const Maybe<TimeStamp>& aTime) {
  auto testSampleTime = mTestSampleTime.Lock();
  testSampleTime.ref() = aTime;
}

SampleTime APZCTreeManager::GetFrameTime() {
  auto testSampleTime = mTestSampleTime.Lock();
  if (testSampleTime.ref()) {
    return SampleTime::FromTest(*testSampleTime.ref());
  }
  return SampleTime::FromNow();
}

void APZCTreeManager::SetAllowedTouchBehavior(
    uint64_t aInputBlockId, const nsTArray<TouchBehaviorFlags>& aValues) {
  if (!APZThreadUtils::IsControllerThread()) {
    APZThreadUtils::RunOnControllerThread(
        NewRunnableMethod<uint64_t, nsTArray<TouchBehaviorFlags>>(
            "layers::APZCTreeManager::SetAllowedTouchBehavior", this,
            &APZCTreeManager::SetAllowedTouchBehavior, aInputBlockId,
            aValues.Clone()));
    return;
  }

  APZThreadUtils::AssertOnControllerThread();

  mInputQueue->SetAllowedTouchBehavior(aInputBlockId, aValues);
}

void APZCTreeManager::SetBrowserGestureResponse(
    uint64_t aInputBlockId, BrowserGestureResponse aResponse) {
  if (!APZThreadUtils::IsControllerThread()) {
    APZThreadUtils::RunOnControllerThread(
        NewRunnableMethod<uint64_t, BrowserGestureResponse>(
            "layers::APZCTreeManager::SetBrowserGestureResponse", this,
            &APZCTreeManager::SetBrowserGestureResponse, aInputBlockId,
            aResponse));
    return;
  }

  APZThreadUtils::AssertOnControllerThread();

  mInputQueue->SetBrowserGestureResponse(aInputBlockId, aResponse);
}

std::vector<LayersId> APZCTreeManager::UpdateHitTestingTree(
    const WebRenderScrollDataWrapper& aRoot, LayersId aOriginatingLayersId,
    uint32_t aPaintSequenceNumber) {
  AssertOnUpdaterThread();

  RecursiveMutexAutoLock lock(mTreeLock);

  // For testing purposes, we log some data to the APZTestData associated with
  // the layers id that originated this update.
  APZTestData* testData = nullptr;
  const bool testLoggingEnabled = StaticPrefs::apz_test_logging_enabled();
  if (testLoggingEnabled) {
    MutexAutoLock lock(mTestDataLock);
    UniquePtr<APZTestData> ptr = MakeUnique<APZTestData>();
    auto result =
        mTestData.insert(std::make_pair(aOriginatingLayersId, std::move(ptr)));
    testData = result.first->second.get();
    testData->StartNewPaint(aPaintSequenceNumber);
  }

  TreeBuildingState state(mRootLayersId, aOriginatingLayersId, testData,
                          aPaintSequenceNumber, testLoggingEnabled);

  // We do this business with collecting the entire tree into an array because
  // otherwise it's very hard to determine which APZC instances need to be
  // destroyed. In the worst case, there are two scenarios: (a) a layer with an
  // APZC is removed from the layer tree and (b) a layer with an APZC is moved
  // in the layer tree from one place to a completely different place. In
  // scenario (a) we would want to destroy the APZC while walking the layer tree
  // and noticing that the layer/APZC is no longer there. But if we do that then
  // we run into a problem in scenario (b) because we might encounter that layer
  // later during the walk. To handle both of these we have to 'remember' that
  // the layer was not found, and then do the destroy only at the end of the
  // tree walk after we are sure that the layer was removed and not just
  // transplanted elsewhere. Doing that as part of a recursive tree walk is hard
  // and so maintaining a list and removing APZCs that are still alive is much
  // simpler.
  ForEachNode<ReverseIterator>(mRootNode.get(),
                               [&state](HitTestingTreeNode* aNode) {
                                 state.mNodesToDestroy.AppendElement(aNode);
                               });
  mRootNode = nullptr;
  mHaveOOPIframes = false;
  Maybe<LayersId> asyncZoomContainerSubtree = Nothing();
  LayersId currentRootContentLayersId{0};
  int asyncZoomContainerNestingDepth = 0;
  bool haveNestedAsyncZoomContainers = false;
  nsTArray<LayersId> subtreesWithRootContentOutsideAsyncZoomContainer;

  if (aRoot) {
    std::unordered_set<LayersId, LayersId::HashFn> seenLayersIds;
    std::stack<gfx::TreeAutoIndent<gfx::LOG_CRITICAL>> indents;
    std::stack<AncestorTransform> ancestorTransforms;
    HitTestingTreeNode* parent = nullptr;
    HitTestingTreeNode* next = nullptr;
    LayersId layersId = mRootLayersId;
    seenLayersIds.insert(mRootLayersId);
    ancestorTransforms.push(AncestorTransform());
    state.mOverrideFlags.push(EventRegionsOverride::NoOverride);
    nsTArray<Maybe<ZoomConstraints>> zoomConstraintsStack;
    uint64_t fixedSubtreeDepth = 0;

    // push a nothing to be used for anything outside an async zoom container
    zoomConstraintsStack.AppendElement(Nothing());

    mApzcTreeLog << "[start]\n";
    mTreeLock.AssertCurrentThreadIn();

    ForEachNode<ReverseIterator>(
        aRoot,
        [&](ScrollNode aLayerMetrics) {
          if (auto asyncZoomContainerId =
                  aLayerMetrics.GetAsyncZoomContainerId()) {
            if (asyncZoomContainerNestingDepth > 0) {
              haveNestedAsyncZoomContainers = true;
            }
            asyncZoomContainerSubtree = Some(layersId);
            ++asyncZoomContainerNestingDepth;

            auto it = mZoomConstraints.find(
                ScrollableLayerGuid(layersId, 0, *asyncZoomContainerId));
            if (it != mZoomConstraints.end()) {
              zoomConstraintsStack.AppendElement(Some(it->second));
            } else {
              zoomConstraintsStack.AppendElement(Nothing());
            }
          }

          if (aLayerMetrics.Metrics().IsRootContent()) {
            MutexAutoLock lock(mMapLock);
            mGeckoFixedLayerMargins =
                aLayerMetrics.Metrics().GetFixedLayerMargins();
            SetInteractiveWidgetMode(
                aLayerMetrics.Metadata().GetInteractiveWidget(), lock);
            SetIsSoftwareKeyboardVisible(
                aLayerMetrics.Metadata().IsSoftwareKeyboardVisible(), lock);
            currentRootContentLayersId = layersId;
          } else {
            MOZ_ASSERT(aLayerMetrics.Metrics().GetFixedLayerMargins() ==
                           ScreenMargin(),
                       "fixed-layer-margins should be 0 on non-root layer");
          }

          // Note that this check happens after the potential increment of
          // asyncZoomContainerNestingDepth, to allow the root content
          // metadata to be on the same node as the async zoom container.
          if (aLayerMetrics.Metrics().IsRootContent() &&
              asyncZoomContainerNestingDepth == 0) {
            subtreesWithRootContentOutsideAsyncZoomContainer.AppendElement(
                layersId);
          }

          HitTestingTreeNode* node = PrepareNodeForLayer(
              lock, aLayerMetrics, aLayerMetrics.Metrics(), layersId,
              zoomConstraintsStack.LastElement(), ancestorTransforms.top(),
              parent, next, state);
          MOZ_ASSERT(node);
          AsyncPanZoomController* apzc = node->GetApzc();
          aLayerMetrics.SetApzc(apzc);

          // Note that a "valid" animation id is always nonzero, so we don't
          // need to worry about handling the case where the animation id is
          // zero.
          if (node->GetScrollbarAnimationId()) {
            if (node->IsScrollThumbNode()) {
              state.mScrollThumbs.push_back(node);
            } else if (node->IsScrollbarContainerNode()) {
              // Only scrollbar containers for the root have an animation id.
              state.mRootScrollbarInfo.emplace_back(
                  *(node->GetScrollbarAnimationId()),
                  node->GetScrollbarDirection());
            }
          }

          if (node->GetFixedPositionAnimationId().isSome()) {
            // Only top-level fixed nodes should be adjusted
            // for dynamic toolbar movement.
            if (fixedSubtreeDepth == 0) {
              state.mFixedPositionInfo.emplace_back(node);
            }
            fixedSubtreeDepth += 1;
          }
          if (node->GetStickyPositionAnimationId().isSome()) {
            state.mStickyPositionInfo.emplace_back(node);
          }
          if (apzc && node->IsPrimaryHolder()) {
            state.mScrollTargets[apzc->GetGuid()] = node;
          }

          // Accumulate the CSS transform between layers that have an APZC.
          // In the terminology of the big comment above
          // APZCTreeManager::GetScreenToApzcTransform, if we are at layer M,
          // then aAncestorTransform is NC * OC * PC, and we left-multiply MC
          // and compute ancestorTransform to be MC * NC * OC * PC. This gets
          // passed down as the ancestor transform to layer L when we recurse
          // into the children below. If we are at a layer with an APZC, such as
          // P, then we reset the ancestorTransform to just PC, to start the new
          // accumulation as we go down.
          AncestorTransform currentTransform{
              aLayerMetrics.GetTransform(),
              aLayerMetrics.TransformIsPerspective()};
          if (!apzc) {
            currentTransform = currentTransform * ancestorTransforms.top();
          }
          ancestorTransforms.push(currentTransform);

          // Note that |node| at this point will not have any children,
          // otherwise we we would have to set next to node->GetFirstChild().
          MOZ_ASSERT(!node->GetFirstChild());
          parent = node;
          next = nullptr;

          // Update the layersId if we have a new one
          if (Maybe<LayersId> newLayersId = aLayerMetrics.GetReferentId()) {
            layersId = *newLayersId;
            seenLayersIds.insert(layersId);

            if (state.mOverrideFlags.size() > 1) {
              // At this point, if `state.mOverrideFlags` has 2 or more
              // elements, which means there's a node having a referent id
              // corresponding to the top level content document and this node
              // is an descendant of the node but for a different content
              // process.
              mHaveOOPIframes = true;
            }

            // Propagate any event region override flags down into all
            // descendant nodes from the reflayer that has the flag. This is an
            // optimization to avoid having to walk up the tree to check the
            // override flags. Note that we don't keep the flags on the reflayer
            // itself, because the semantics of the flags are that they apply
            // to all content in the layer subtree being referenced. This
            // matters with the WR hit-test codepath, because this reflayer may
            // be just one of many nodes associated with a particular APZC, and
            // calling GetTargetNode with a guid may return any one of the
            // nodes. If different nodes have different flags on them that can
            // make the WR hit-test result incorrect, but being strict about
            // only putting the flags on descendant layers avoids this problem.
            state.mOverrideFlags.push(state.mOverrideFlags.top() |
                                      aLayerMetrics.GetEventRegionsOverride());
          }

          indents.push(gfx::TreeAutoIndent<gfx::LOG_CRITICAL>(mApzcTreeLog));
        },
        [&](ScrollNode aLayerMetrics) {
          if (aLayerMetrics.GetAsyncZoomContainerId()) {
            --asyncZoomContainerNestingDepth;
            zoomConstraintsStack.RemoveLastElement();
          }
          if (aLayerMetrics.GetReferentId()) {
            state.mOverrideFlags.pop();
          }

          if (aLayerMetrics.GetFixedPositionAnimationId().isSome()) {
            fixedSubtreeDepth -= 1;
          }

          next = parent;
          parent = parent->GetParent();
          layersId = next->GetLayersId();
          ancestorTransforms.pop();
          indents.pop();
        });

    mApzcTreeLog << "[end]\n";

    MOZ_ASSERT(
        !asyncZoomContainerSubtree ||
            !subtreesWithRootContentOutsideAsyncZoomContainer.Contains(
                *asyncZoomContainerSubtree),
        "If there is an async zoom container, all scroll nodes with root "
        "content scroll metadata should be inside it");
    MOZ_ASSERT(!haveNestedAsyncZoomContainers,
               "Should not have nested async zoom container");

    // If we have perspective transforms deferred to children, do another
    // walk of the tree and actually apply them to the children.
    // We can't do this "as we go" in the previous traversal, because by the
    // time we realize we need to defer a perspective transform for an APZC,
    // we may already have processed a previous layer (including children
    // found in its subtree) that shares that APZC.
    if (!state.mPerspectiveTransformsDeferredToChildren.empty()) {
      ForEachNode<ReverseIterator>(
          mRootNode.get(), [&state](HitTestingTreeNode* aNode) {
            AsyncPanZoomController* apzc = aNode->GetApzc();
            if (!apzc) {
              return;
            }
            if (!aNode->IsPrimaryHolder()) {
              return;
            }

            AsyncPanZoomController* parent = apzc->GetParent();
            if (!parent) {
              return;
            }

            auto it =
                state.mPerspectiveTransformsDeferredToChildren.find(parent);
            if (it != state.mPerspectiveTransformsDeferredToChildren.end()) {
              apzc->SetAncestorTransform(AncestorTransform{
                  it->second * apzc->GetAncestorTransform(), false});
            }
          });
    }

    // Remove any layers ids for which we no longer have content from
    // mDetachedLayersIds.
    for (auto iter = mDetachedLayersIds.begin();
         iter != mDetachedLayersIds.end();) {
      // unordered_set::erase() invalidates the iterator pointing to the
      // element being erased, but returns an iterator to the next element.
      if (seenLayersIds.find(*iter) == seenLayersIds.end()) {
        iter = mDetachedLayersIds.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  // We do not support tree structures where the root node has siblings.
  MOZ_ASSERT(!(mRootNode && mRootNode->GetPrevSibling()));

  {  // scope lock and update our mApzcMap before we destroy all the unused
    // APZC instances
    MutexAutoLock lock(mMapLock);
    mApzcMap = std::move(state.mApzcMap);

    for (auto& mapping : mApzcMap) {
      AsyncPanZoomController* parent = mapping.second.apzc->GetParent();
      mapping.second.parent = parent ? Some(parent->GetGuid()) : Nothing();
    }

    mScrollThumbInfo.clear();
    // For non-webrender, state.mScrollThumbs will be empty so this will be a
    // no-op.
    for (HitTestingTreeNode* thumb : state.mScrollThumbs) {
      MOZ_ASSERT(thumb->IsScrollThumbNode());
      ScrollableLayerGuid targetGuid(thumb->GetLayersId(), 0,
                                     thumb->GetScrollTargetId());
      auto it = state.mScrollTargets.find(targetGuid);
      if (it == state.mScrollTargets.end()) {
        // It could be that |thumb| is a scrollthumb for content which didn't
        // have an APZC, for example if the content isn't layerized. Regardless,
        // we can't async-scroll it so we don't need to worry about putting it
        // in mScrollThumbInfo.
        continue;
      }
      HitTestingTreeNode* target = it->second;
      mScrollThumbInfo.emplace_back(
          *(thumb->GetScrollbarAnimationId()), thumb->GetTransform(),
          thumb->GetScrollbarData(), targetGuid, target->GetTransform(),
          target->IsAncestorOf(thumb));
    }

    mRootScrollbarInfo = std::move(state.mRootScrollbarInfo);
    mFixedPositionInfo = std::move(state.mFixedPositionInfo);
    mStickyPositionInfo = std::move(state.mStickyPositionInfo);
  }

  for (size_t i = 0; i < state.mNodesToDestroy.Length(); i++) {
    APZCTM_LOG("Destroying node at %p with APZC %p\n",
               state.mNodesToDestroy[i].get(),
               state.mNodesToDestroy[i]->GetApzc());
    state.mNodesToDestroy[i]->Destroy();
  }

  APZCTM_LOG("APZCTreeManager (%p)\n", this);
  if (mRootNode && MOZ_LOG_TEST(sLog, LogLevel::Debug)) {
    mRootNode->Dump("  ");
  }
  SendSubtreeTransformsToChromeMainThread(nullptr);

  return std::move(state.mUpdatedLayersIds);
}

void APZCTreeManager::UpdateFocusState(LayersId aRootLayerTreeId,
                                       LayersId aOriginatingLayersId,
                                       const FocusTarget& aFocusTarget) {
  AssertOnUpdaterThread();

  if (!StaticPrefs::apz_keyboard_enabled_AtStartup()) {
    return;
  }

  mFocusState.Update(aRootLayerTreeId, aOriginatingLayersId, aFocusTarget);
}

void APZCTreeManager::SampleForWebRender(const Maybe<VsyncId>& aVsyncId,
                                         wr::TransactionWrapper& aTxn,
                                         const SampleTime& aSampleTime) {
  AssertOnSamplerThread();
  MutexAutoLock lock(mMapLock);

  RefPtr<WebRenderBridgeParent> wrBridgeParent;
  RefPtr<CompositorController> controller;
  CompositorBridgeParent::CallWithIndirectShadowTree(
      mRootLayersId, [&](LayerTreeState& aState) -> void {
        controller = aState.GetCompositorController();
        wrBridgeParent = aState.mWrBridge;
      });

  const bool activeAnimations = AdvanceAnimationsInternal(lock, aSampleTime);
  if (activeAnimations && controller) {
    controller->ScheduleRenderOnCompositorThread(
        wr::RenderReasons::ANIMATED_PROPERTY);
  }
  APZCTM_LOGV(
      "APZCTreeManager(%p)::SampleForWebRender, want more composites: %d\n",
      this, (activeAnimations && controller));

  nsTArray<wr::WrTransformProperty> transforms;

  // Sample async transforms on scrollable layers.
  for (const auto& [_, mapData] : mApzcMap) {
    AsyncPanZoomController* apzc = mapData.apzc;

    if (Maybe<CompositionPayload> payload = apzc->NotifyScrollSampling()) {
      if (wrBridgeParent && aVsyncId) {
        wrBridgeParent->AddPendingScrollPayload(*payload, *aVsyncId);
      }
    }

    if (StaticPrefs::apz_test_logging_enabled()) {
      MutexAutoLock lock(mTestDataLock);

      ScrollableLayerGuid guid = apzc->GetGuid();
      auto it = mTestData.find(guid.mLayersId);
      if (it != mTestData.end()) {
        it->second->RecordSampledResult(
            apzc->GetCurrentAsyncVisualViewport(
                    AsyncPanZoomController::eForCompositing)
                .TopLeft(),
            (aSampleTime.Time() - TimeStamp::ProcessCreation())
                .ToMicroseconds(),
            guid.mLayersId, guid.mScrollId);
      }
    }

    wr::LayoutTransform zoomForMinimap =
        wr::ToLayoutTransform(gfx::Matrix4x4());
    if (Maybe<uint64_t> zoomAnimationId = apzc->GetZoomAnimationId()) {
      // for now we only support zooming on root content APZCs
      MOZ_ASSERT(apzc->IsRootContent());

      LayoutDeviceToParentLayerScale zoom = apzc->GetCurrentPinchZoomScale(
          AsyncPanZoomController::eForCompositing);

      AsyncTransform asyncVisualTransform = apzc->GetCurrentAsyncTransform(
          AsyncPanZoomController::eForCompositing,
          AsyncTransformComponents{AsyncTransformComponent::eVisual});

      wr::WrTransformProperty zoomTransform = wr::ToWrTransformProperty(
          *zoomAnimationId, LayoutDeviceToParentLayerMatrix4x4::Scaling(
                                zoom.scale, zoom.scale, 1.0f) *
                                AsyncTransformComponentMatrix::Translation(
                                    asyncVisualTransform.mTranslation));

      // Store the zoom transform to be added to the minimap data so that we
      // can take it into account correctly during minimap rendering.
      zoomForMinimap = zoomTransform.value;

      transforms.AppendElement(zoomTransform);
      aTxn.UpdateIsTransformAsyncZooming(*zoomAnimationId,
                                         apzc->IsAsyncZooming());
    }

    nsTArray<wr::SampledScrollOffset> sampledOffsets =
        apzc->GetSampledScrollOffsets();
    wr::ExternalScrollId scrollId{apzc->GetGuid().mScrollId,
                                  wr::AsPipelineId(apzc->GetGuid().mLayersId)};
    aTxn.UpdateScrollPosition(scrollId, sampledOffsets);

    if (StaticPrefs::apz_minimap_enabled()) {
      wr::MinimapData minimapData = apzc->GetMinimapData();
      minimapData.zoom_transform = zoomForMinimap;
      // If this APZC is inside the subtree of a root content APZC, find the
      // ID of that root content APZC.
      ScrollableLayerGuid enclosingRootContentId;
      ApzcMapData currentEntry = mapData;
      AsyncPanZoomController* current = currentEntry.apzc;
      while (current) {
        if (current->IsRootContent()) {
          enclosingRootContentId = current->GetGuid();
          break;
        }
        // We can't call AsyncPanZoomController::GetParent(), since that
        // requires acquiring the tree lock, and doing that on the sampler
        // thread would violate the lock ordering. Instead, get the parent
        // from the mApzcMap entry.
        if (auto parentGuid = currentEntry.parent) {
          auto iter = mApzcMap.find(*parentGuid);
          if (iter != mApzcMap.end()) {
            currentEntry = iter->second;
            current = currentEntry.apzc;
            continue;
          }
        }
        break;
      }
      minimapData.root_content_pipeline_id =
          wr::AsPipelineId(enclosingRootContentId.mLayersId);
      minimapData.root_content_scroll_id = enclosingRootContentId.mScrollId;
      aTxn.AddMinimapData(scrollId, minimapData);
    }

    if (apzc->IsRootContent()) {
      // Send the root frame metrics to java through the UIController
      if (RefPtr<UiCompositorControllerParent> uiController =
              UiCompositorControllerParent::GetFromRootLayerTreeId(
                  mRootLayersId)) {
        for (const auto& update : apzc->GetCompositorScrollUpdates()) {
          uiController->NotifyCompositorScrollUpdate(update);
        }
      }
    }
  }

  // Now collect all the async transforms needed for the scrollthumbs.
  for (const ScrollThumbInfo& info : mScrollThumbInfo) {
    auto it = mApzcMap.find(info.mTargetGuid);
    if (it == mApzcMap.end()) {
      // It could be that |info| is a scrollthumb for content which didn't
      // have an APZC, for example if the content isn't layerized. Regardless,
      // we can't async-scroll it so we don't need to worry about putting it
      // in mScrollThumbInfo.
      continue;
    }
    AsyncPanZoomController* scrollTargetApzc = it->second.apzc;
    MOZ_ASSERT(scrollTargetApzc);
    LayerToParentLayerMatrix4x4 transform =
        scrollTargetApzc->CallWithLastContentPaintMetrics(
            [&](const FrameMetrics& aMetrics) {
              return ComputeTransformForScrollThumb(
                  info.mThumbTransform * AsyncTransformMatrix(),
                  info.mTargetTransform.ToUnknownMatrix(), scrollTargetApzc,
                  aMetrics, info.mThumbData, info.mTargetIsAncestor);
            });
    transforms.AppendElement(
        wr::ToWrTransformProperty(info.mThumbAnimationId, transform));
  }

  // Move the root scrollbar in response to the dynamic toolbar transition.
  for (const RootScrollbarInfo& info : mRootScrollbarInfo) {
    // We only care about the horizontal scrollbar.
    if (info.mScrollDirection == ScrollDirection::eHorizontal) {
      ScreenPoint translation =
          ComputeFixedMarginsOffset(lock, SideBits::eBottom, ScreenMargin());

      LayerToParentLayerMatrix4x4 transform =
          LayerToParentLayerMatrix4x4::Translation(ViewAs<ParentLayerPixel>(
              translation, PixelCastJustification::ScreenIsParentLayerForRoot));

      transforms.AppendElement(
          wr::ToWrTransformProperty(info.mScrollbarAnimationId, transform));
    }
  }

  for (const FixedPositionInfo& info : mFixedPositionInfo) {
    MOZ_ASSERT(info.mFixedPositionAnimationId.isSome());
    if (!IsFixedToRootContent(info, lock)) {
      continue;
    }

    ScreenPoint translation = ComputeFixedMarginsOffset(
        lock, info.mFixedPosSides, mGeckoFixedLayerMargins);

    LayerToParentLayerMatrix4x4 transform =
        LayerToParentLayerMatrix4x4::Translation(ViewAs<ParentLayerPixel>(
            translation, PixelCastJustification::ScreenIsParentLayerForRoot));

    transforms.AppendElement(
        wr::ToWrTransformProperty(*info.mFixedPositionAnimationId, transform));
  }

  for (const StickyPositionInfo& info : mStickyPositionInfo) {
    MOZ_ASSERT(info.mStickyPositionAnimationId.isSome());
    SideBits sides = SidesStuckToRootContent(
        info, AsyncTransformConsumer::eForCompositing, lock);
    if (sides == SideBits::eNone) {
      continue;
    }

    ScreenPoint translation =
        ComputeFixedMarginsOffset(lock, sides, mGeckoFixedLayerMargins);

    LayerToParentLayerMatrix4x4 transform =
        LayerToParentLayerMatrix4x4::Translation(ViewAs<ParentLayerPixel>(
            translation, PixelCastJustification::ScreenIsParentLayerForRoot));

    transforms.AppendElement(
        wr::ToWrTransformProperty(*info.mStickyPositionAnimationId, transform));
  }

  aTxn.AppendTransformProperties(transforms);
}

ParentLayerRect APZCTreeManager::ComputeClippedCompositionBounds(
    const MutexAutoLock& aProofOfMapLock, ClippedCompositionBoundsMap& aDestMap,
    ScrollableLayerGuid aGuid) {
  if (auto iter = aDestMap.find(aGuid); iter != aDestMap.end()) {
    // We already computed it for this one, early-exit. This might happen
    // because on a later iteration of mApzcMap we might encounter an ancestor
    // of an APZC that we processed on an earlier iteration. In this case we
    // would have computed the ancestor's clipped composition bounds when
    // recursing up on the earlier iteration.
    return iter->second;
  }

  ParentLayerRect bounds = mApzcMap[aGuid].apzc->GetCompositionBounds();
  const auto& mapEntry = mApzcMap.find(aGuid);
  MOZ_ASSERT(mapEntry != mApzcMap.end());
  if (mapEntry->second.parent.isNothing()) {
    // Recursion base case, where the APZC with guid `aGuid` has no parent.
    // In this case, we don't need to clip `bounds` any further and can just
    // early exit.
    aDestMap.emplace(aGuid, bounds);
    return bounds;
  }

  ScrollableLayerGuid parentGuid = mapEntry->second.parent.value();
  auto parentBoundsEntry = aDestMap.find(parentGuid);
  // If aDestMap doesn't contain the parent entry yet, we recurse to compute
  // that one first.
  ParentLayerRect parentClippedBounds =
      (parentBoundsEntry == aDestMap.end())
          ? ComputeClippedCompositionBounds(aProofOfMapLock, aDestMap,
                                            parentGuid)
          : parentBoundsEntry->second;

  // The parent layer's async transform applies to the current layer to take
  // `bounds` into the same coordinate space as `parentClippedBounds`. However,
  // we're going to do the inverse operation and unapply this transform to
  // `parentClippedBounds` to bring it into the same coordinate space as
  // `bounds`.
  AsyncTransform appliesToLayer =
      mApzcMap[parentGuid].apzc->GetCurrentAsyncTransform(
          AsyncPanZoomController::eForCompositing);

  // Do the unapplication
  LayerRect parentClippedBoundsInParentLayerSpace =
      (parentClippedBounds - appliesToLayer.mTranslation) /
      appliesToLayer.mScale;

  // And then clip `bounds` by the parent's comp bounds in the current space.
  bounds = bounds.Intersect(
      ViewAs<ParentLayerPixel>(parentClippedBoundsInParentLayerSpace,
                               PixelCastJustification::MovingDownToChildren));

  // Done!
  aDestMap.emplace(aGuid, bounds);
  return bounds;
}

bool APZCTreeManager::AdvanceAnimationsInternal(
    const MutexAutoLock& aProofOfMapLock, const SampleTime& aSampleTime) {
  ClippedCompositionBoundsMap clippedCompBounds;
  bool activeAnimations = false;
  for (const auto& mapping : mApzcMap) {
    AsyncPanZoomController* apzc = mapping.second.apzc;
    // Note that this call is recursive, but it early-exits if called again
    // with the same guid. So this loop is still amortized O(n) with respect to
    // the number of APZCs.
    ParentLayerRect clippedBounds = ComputeClippedCompositionBounds(
        aProofOfMapLock, clippedCompBounds, mapping.first);

    apzc->ReportCheckerboard(aSampleTime, clippedBounds);
    activeAnimations |= apzc->AdvanceAnimations(aSampleTime);
  }
  return activeAnimations;
}

void APZCTreeManager::PrintLayerInfo(const ScrollNode& aLayer) {
  if (StaticPrefs::apz_printtree() && aLayer.Dump(mApzcTreeLog) > 0) {
    mApzcTreeLog << "\n";
  }
}

// mTreeLock is held, and checked with static analysis
void APZCTreeManager::AttachNodeToTree(HitTestingTreeNode* aNode,
                                       HitTestingTreeNode* aParent,
                                       HitTestingTreeNode* aNextSibling) {
  if (aNextSibling) {
    aNextSibling->SetPrevSibling(aNode);
  } else if (aParent) {
    aParent->SetLastChild(aNode);
  } else {
    MOZ_ASSERT(!mRootNode);
    mRootNode = aNode;
    aNode->MakeRoot();
  }
}

already_AddRefed<HitTestingTreeNode> APZCTreeManager::RecycleOrCreateNode(
    const RecursiveMutexAutoLock& aProofOfTreeLock, TreeBuildingState& aState,
    AsyncPanZoomController* aApzc, LayersId aLayersId) {
  // Find a node without an APZC and return it. Note that unless the layer tree
  // actually changes, this loop should generally do an early-return on the
  // first iteration, so it should be cheap in the common case.
  for (int32_t i = aState.mNodesToDestroy.Length() - 1; i >= 0; i--) {
    RefPtr<HitTestingTreeNode> node = aState.mNodesToDestroy[i];
    if (node->IsRecyclable(aProofOfTreeLock)) {
      aState.mNodesToDestroy.RemoveElementAt(i);
      node->RecycleWith(aProofOfTreeLock, aApzc, aLayersId);
      return node.forget();
    }
  }
  RefPtr<HitTestingTreeNode> node =
      new HitTestingTreeNode(aApzc, false, aLayersId);
  return node.forget();
}

void APZCTreeManager::StartScrollbarDrag(const ScrollableLayerGuid& aGuid,
                                         const AsyncDragMetrics& aDragMetrics) {
  if (!APZThreadUtils::IsControllerThread()) {
    APZThreadUtils::RunOnControllerThread(
        NewRunnableMethod<ScrollableLayerGuid, AsyncDragMetrics>(
            "layers::APZCTreeManager::StartScrollbarDrag", this,
            &APZCTreeManager::StartScrollbarDrag, aGuid, aDragMetrics));
    return;
  }

  APZThreadUtils::AssertOnControllerThread();

  RefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid);
  if (!apzc) {
    NotifyScrollbarDragRejected(aGuid);
    return;
  }

  uint64_t inputBlockId = aDragMetrics.mDragStartSequenceNumber;
  mInputQueue->ConfirmDragBlock(inputBlockId, apzc, aDragMetrics);
}

bool APZCTreeManager::StartAutoscroll(const ScrollableLayerGuid& aGuid,
                                      const ScreenPoint& aAnchorLocation) {
  APZThreadUtils::AssertOnControllerThread();

  RefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid);
  if (!apzc) {
    if (XRE_IsGPUProcess()) {
      // If we're in the compositor process, the "return false" will be
      // ignored because the query comes over the PAPZCTreeManager protocol
      // via an async message. In this case, send an explicit rejection
      // message to content.
      NotifyAutoscrollRejected(aGuid);
    }
    return false;
  }

  apzc->StartAutoscroll(aAnchorLocation);
  return true;
}

void APZCTreeManager::StopAutoscroll(const ScrollableLayerGuid& aGuid) {
  APZThreadUtils::AssertOnControllerThread();

  if (RefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid)) {
    apzc->StopAutoscroll();
  }
}

void APZCTreeManager::NotifyScrollbarDragInitiated(
    uint64_t aDragBlockId, const ScrollableLayerGuid& aGuid,
    ScrollDirection aDirection) const {
  RefPtr<GeckoContentController> controller =
      GetContentController(aGuid.mLayersId);
  if (controller) {
    controller->NotifyAsyncScrollbarDragInitiated(aDragBlockId, aGuid.mScrollId,
                                                  aDirection);
  }
}

void APZCTreeManager::NotifyScrollbarDragRejected(
    const ScrollableLayerGuid& aGuid) const {
  RefPtr<GeckoContentController> controller =
      GetContentController(aGuid.mLayersId);
  if (controller) {
    controller->NotifyAsyncScrollbarDragRejected(aGuid.mScrollId);
  }
}

void APZCTreeManager::NotifyAutoscrollRejected(
    const ScrollableLayerGuid& aGuid) const {
  RefPtr<GeckoContentController> controller =
      GetContentController(aGuid.mLayersId);
  MOZ_ASSERT(controller);
  controller->NotifyAsyncAutoscrollRejected(aGuid.mScrollId);
}

void SetHitTestData(HitTestingTreeNode* aNode,
                    const WebRenderScrollDataWrapper& aLayer,
                    const EventRegionsOverride& aOverrideFlags) {
  aNode->SetHitTestData(aLayer.GetVisibleRect(), aLayer.GetRemoteDocumentSize(),
                        aLayer.GetTransformTyped(), aOverrideFlags,
                        aLayer.GetAsyncZoomContainerId());
}

HitTestingTreeNode* APZCTreeManager::PrepareNodeForLayer(
    const RecursiveMutexAutoLock& aProofOfTreeLock, const ScrollNode& aLayer,
    const FrameMetrics& aMetrics, LayersId aLayersId,
    const Maybe<ZoomConstraints>& aZoomConstraints,
    const AncestorTransform& aAncestorTransform, HitTestingTreeNode* aParent,
    HitTestingTreeNode* aNextSibling, TreeBuildingState& aState) {
  mTreeLock.AssertCurrentThreadIn();  // for static analysis
  bool needsApzc = true;
  if (!aMetrics.IsScrollable()) {
    needsApzc = false;
  }

  // XXX: As a future optimization we can probably stick these things on the
  // TreeBuildingState, and update them as we change layers id during the
  // traversal
  RefPtr<GeckoContentController> geckoContentController;
  CompositorBridgeParent::CallWithIndirectShadowTree(
      aLayersId, [&](LayerTreeState& lts) -> void {
        geckoContentController = lts.mController;
      });

  if (!geckoContentController) {
    needsApzc = false;
  }

  if (Maybe<uint64_t> zoomAnimationId = aLayer.GetZoomAnimationId()) {
    aState.mZoomAnimationId = zoomAnimationId;
  }

  RefPtr<HitTestingTreeNode> node = nullptr;
  if (!needsApzc) {
    // Note: if layer properties must be propagated to nodes, RecvUpdate in
    // LayerTransactionParent.cpp must ensure that APZ will be notified
    // when those properties change.
    node = RecycleOrCreateNode(aProofOfTreeLock, aState, nullptr, aLayersId);
    AttachNodeToTree(node, aParent, aNextSibling);
    SetHitTestData(node, aLayer, aState.mOverrideFlags.top());
    node->SetScrollbarData(aLayer.GetScrollbarAnimationId(),
                           aLayer.GetScrollbarData());
    node->SetFixedPosData(aLayer.GetFixedPositionScrollContainerId(),
                          aLayer.GetFixedPositionSides(),
                          aLayer.GetFixedPositionAnimationId());
    node->SetStickyPosData(aLayer.GetStickyScrollContainerId(),
                           aLayer.GetStickyScrollRangeOuter(),
                           aLayer.GetStickyScrollRangeInner(),
                           aLayer.GetStickyPositionAnimationId());
    PrintLayerInfo(aLayer);
    return node;
  }

  RefPtr<AsyncPanZoomController> apzc;
  // If we get here, aLayer is a scrollable layer and somebody
  // has registered a GeckoContentController for it, so we need to ensure
  // it has an APZC instance to manage its scrolling.

  // aState.mApzcMap allows reusing the exact same APZC instance for different
  // layers with the same FrameMetrics data. This is needed because in some
  // cases content that is supposed to scroll together is split into multiple
  // layers because of e.g. non-scrolling content interleaved in z-index order.
  ScrollableLayerGuid guid(aLayersId, aMetrics.GetPresShellId(),
                           aMetrics.GetScrollId());
  auto insertResult = aState.mApzcMap.insert(std::make_pair(
      guid,
      ApzcMapData{static_cast<AsyncPanZoomController*>(nullptr), Nothing()}));
  if (!insertResult.second) {
    apzc = insertResult.first->second.apzc;
    PrintLayerInfo(aLayer);
  }
  APZCTM_LOG(
      "Found APZC %p for layer %p with identifiers %" PRIx64 " %" PRId64 "\n",
      apzc.get(), aLayer.GetLayer(), uint64_t(guid.mLayersId), guid.mScrollId);

  aState.mUpdatedLayersIds.push_back(aLayersId);

  // If we haven't encountered a layer already with the same metrics, then we
  // need to do the full reuse-or-make-an-APZC algorithm, which is contained
  // inside the block below.
  if (apzc == nullptr) {
    apzc = aLayer.GetApzc();

    // If the content represented by the scrollable layer has changed (which may
    // be possible because of DLBI heuristics) then we don't want to keep using
    // the same old APZC for the new content. Also, when reparenting a tab into
    // a new window a layer might get moved to a different layer tree with a
    // different APZCTreeManager. In these cases we don't want to reuse the same
    // APZC, so null it out so we run through the code to find another one or
    // create one.
    if (apzc && (!apzc->Matches(guid) || !apzc->HasTreeManager(this))) {
      apzc = nullptr;
    }

    // See if we can find an APZC from the previous tree that matches the
    // ScrollableLayerGuid from this layer. If there is one, then we know that
    // the layout of the page changed causing the layer tree to be rebuilt, but
    // the underlying content for the APZC is still there somewhere. Therefore,
    // we want to find the APZC instance and continue using it here.
    //
    // We particularly want to find the primary-holder node from the previous
    // tree that matches, because we don't want that node to get destroyed. If
    // it does get destroyed, then the APZC will get destroyed along with it by
    // definition, but we want to keep that APZC around in the new tree.
    // We leave non-primary-holder nodes in the destroy list because we don't
    // care about those nodes getting destroyed.
    for (size_t i = 0; i < aState.mNodesToDestroy.Length(); i++) {
      RefPtr<HitTestingTreeNode> n = aState.mNodesToDestroy[i];
      if (n->IsPrimaryHolder() && n->GetApzc() && n->GetApzc()->Matches(guid)) {
        node = n;
        if (apzc != nullptr) {
          // If there is an APZC already then it should match the one from the
          // old primary-holder node
          MOZ_ASSERT(apzc == node->GetApzc());
        }
        apzc = node->GetApzc();
        break;
      }
    }

    // The APZC we get off the layer may have been destroyed previously if the
    // layer was inactive or omitted from the layer tree for whatever reason
    // from a layers update. If it later comes back it will have a reference to
    // a destroyed APZC and so we need to throw that out and make a new one.
    bool newApzc = (apzc == nullptr || apzc->IsDestroyed());
    if (newApzc) {
      apzc = NewAPZCInstance(aLayersId, geckoContentController);
      apzc->SetCompositorController(aState.mCompositorController.get());
      MOZ_ASSERT(node == nullptr);
      node = new HitTestingTreeNode(apzc, true, aLayersId);
    } else {
      // If we are re-using a node for this layer clear the tree pointers
      // so that it doesn't continue pointing to nodes that might no longer
      // be in the tree. These pointers will get reset properly as we continue
      // building the tree. Also remove it from the set of nodes that are going
      // to be destroyed, because it's going to remain active.
      aState.mNodesToDestroy.RemoveElement(node);
      node->SetPrevSibling(nullptr);
      node->SetLastChild(nullptr);
    }

    if (aMetrics.IsRootContent()) {
      apzc->SetZoomAnimationId(aState.mZoomAnimationId);
      aState.mZoomAnimationId = Nothing();
    }

    APZCTM_LOG("Using APZC %p for layer %p with identifiers %" PRIx64
               " %" PRId64 "\n",
               apzc.get(), aLayer.GetLayer(), uint64_t(aLayersId),
               aMetrics.GetScrollId());

    apzc->NotifyLayersUpdated(aLayer.Metadata(), aLayer.IsFirstPaint(),
                              aLayersId == aState.mOriginatingLayersId);

    // Since this is the first time we are encountering an APZC with this guid,
    // the node holding it must be the primary holder. It may be newly-created
    // or not, depending on whether it went through the newApzc branch above.
    MOZ_ASSERT(node->IsPrimaryHolder() && node->GetApzc() &&
               node->GetApzc()->Matches(guid));

    SetHitTestData(node, aLayer, aState.mOverrideFlags.top());
    apzc->SetAncestorTransform(aAncestorTransform);

    PrintLayerInfo(aLayer);

    // Bind the APZC instance into the tree of APZCs
    AttachNodeToTree(node, aParent, aNextSibling);

    // For testing, log the parent scroll id of every APZC that has a
    // parent. This allows test code to reconstruct the APZC tree.
    // Note that we currently only do this for APZCs in the layer tree
    // that originated the update, because the only identifying information
    // we are logging about APZCs is the scroll id, and otherwise we could
    // confuse APZCs from different layer trees with the same scroll id.
    if (aLayersId == aState.mOriginatingLayersId) {
      if (apzc->HasNoParentWithSameLayersId()) {
        aState.mPaintLogger.LogTestData(aMetrics.GetScrollId(),
                                        "hasNoParentWithSameLayersId", true);
      } else {
        MOZ_ASSERT(apzc->GetParent());
        aState.mPaintLogger.LogTestData(aMetrics.GetScrollId(),
                                        "parentScrollId",
                                        apzc->GetParent()->GetGuid().mScrollId);
      }
      if (aMetrics.IsRootContent()) {
        aState.mPaintLogger.LogTestData(aMetrics.GetScrollId(), "isRootContent",
                                        true);
      }
      // Note that the async scroll offset is in ParentLayer pixels
      aState.mPaintLogger.LogTestData(
          aMetrics.GetScrollId(), "asyncScrollOffset",
          apzc->GetCurrentAsyncScrollOffset(
              AsyncPanZoomController::eForEventHandling));
      aState.mPaintLogger.LogTestData(aMetrics.GetScrollId(),
                                      "hasAsyncKeyScrolled",
                                      apzc->TestHasAsyncKeyScrolled());
    }

    // We must update the zoom constraints even if the apzc isn't new because it
    // might have moved.
    if (node->IsPrimaryHolder()) {
      if (aZoomConstraints) {
        apzc->UpdateZoomConstraints(*aZoomConstraints);

#ifdef DEBUG
        auto it = mZoomConstraints.find(guid);
        if (it != mZoomConstraints.end()) {
          MOZ_ASSERT(it->second == *aZoomConstraints);
        }
      } else {
        // We'd like to assert these things (if the first doesn't hold then at
        // least the second) but neither are not true because xul root content
        // gets zoomable zoom constraints, but which is not zoomable because it
        // doesn't have a root scroll frame.
        // clang-format off
        // MOZ_ASSERT(mZoomConstraints.find(guid) == mZoomConstraints.end());
        // auto it = mZoomConstraints.find(guid);
        // if (it != mZoomConstraints.end()) {
        //   MOZ_ASSERT(!it->second.mAllowZoom && !it->second.mAllowDoubleTapZoom);
        // }
        // clang-format on
#endif
      }
    }

    // Add a guid -> APZC mapping for the newly created APZC.
    insertResult.first->second.apzc = apzc;
  } else {
    // We already built an APZC earlier in this tree walk, but we have another
    // layer now that will also be using that APZC. The hit-test region on the
    // APZC needs to be updated to deal with the new layer's hit region.

    node = RecycleOrCreateNode(aProofOfTreeLock, aState, apzc, aLayersId);
    AttachNodeToTree(node, aParent, aNextSibling);

    // Even though different layers associated with a given APZC may be at
    // different levels in the layer tree (e.g. one being an uncle of another),
    // we require from Layout that the CSS transforms up to their common
    // ancestor be roughly the same. There are cases in which the transforms
    // are not exactly the same, for example if the parent is container layer
    // for an opacity, and this container layer has a resolution-induced scale
    // as its base transform and a prescale that is supposed to undo that scale.
    // Due to floating point inaccuracies those transforms can end up not quite
    // canceling each other. That's why we're using a fuzzy comparison here
    // instead of an exact one.
    // In addition, two ancestor transforms are allowed to differ if one of
    // them contains a perspective transform component and the other does not.
    // This represents situations where some content in a scrollable frame
    // is subject to a perspective transform and other content does not.
    // In such cases, go with the one that does not include the perspective
    // component; the perspective transform is remembered and applied to the
    // children instead.
    auto ancestorTransform = aAncestorTransform.CombinedTransform();
    auto existingAncestorTransform = apzc->GetAncestorTransform();
    if (!ancestorTransform.FuzzyEqualsMultiplicative(
            existingAncestorTransform)) {
      typedef TreeBuildingState::DeferredTransformMap::value_type PairType;
      if (!aAncestorTransform.ContainsPerspectiveTransform() &&
          !apzc->AncestorTransformContainsPerspective()) {
        // If this content is being presented in a paginated fashion (e.g.
        // print preview), the multiple layers may reflect multiple instances
        // of the same display item being rendered on different pages. In such
        // cases, it's expected that different instances can have different
        // transforms, since each page renders a different part of the item.
        if (!aLayer.Metadata().IsPaginatedPresentation()) {
          if (ancestorTransform.IsFinite() &&
              existingAncestorTransform.IsFinite()) {
            // Log separately from the assert because assert doesn't allow
            // printf-style arguments, but it's important for debugging that the
            // log identifies *which* scroll frame violated the condition.
            MOZ_LOG(sLog, LogLevel::Error,
                    ("Two layers that scroll together have different ancestor "
                     "transforms (guid=%s)",
                     ToString(apzc->GetGuid()).c_str()));
            MOZ_ASSERT(false,
                       "Two layers that scroll together have different "
                       "ancestor transforms");
          } else {
            MOZ_ASSERT(ancestorTransform.IsFinite() ==
                       existingAncestorTransform.IsFinite());
          }
        }
      } else if (!aAncestorTransform.ContainsPerspectiveTransform()) {
        aState.mPerspectiveTransformsDeferredToChildren.insert(
            PairType{apzc, apzc->GetAncestorTransformPerspective()});
        apzc->SetAncestorTransform(aAncestorTransform);
      } else {
        aState.mPerspectiveTransformsDeferredToChildren.insert(
            PairType{apzc, aAncestorTransform.GetPerspectiveTransform()});
      }
    }

    SetHitTestData(node, aLayer, aState.mOverrideFlags.top());
  }

  // Note: if layer properties must be propagated to nodes, RecvUpdate in
  // LayerTransactionParent.cpp must ensure that APZ will be notified
  // when those properties change.
  node->SetScrollbarData(aLayer.GetScrollbarAnimationId(),
                         aLayer.GetScrollbarData());
  node->SetFixedPosData(aLayer.GetFixedPositionScrollContainerId(),
                        aLayer.GetFixedPositionSides(),
                        aLayer.GetFixedPositionAnimationId());
  node->SetStickyPosData(aLayer.GetStickyScrollContainerId(),
                         aLayer.GetStickyScrollRangeOuter(),
                         aLayer.GetStickyScrollRangeInner(),
                         aLayer.GetStickyPositionAnimationId());
  return node;
}

template <typename PanGestureOrScrollWheelInput>
static bool WillHandleInput(const PanGestureOrScrollWheelInput& aPanInput) {
  if (!XRE_IsParentProcess() || !NS_IsMainThread()) {
    return true;
  }

  WidgetWheelEvent wheelEvent = aPanInput.ToWidgetEvent(nullptr);
  return APZInputBridge::ActionForWheelEvent(&wheelEvent).isSome();
}

/*static*/
void APZCTreeManager::FlushApzRepaints(LayersId aLayersId) {
  // Previously, paints were throttled and therefore this method was used to
  // ensure any pending paints were flushed. Now, paints are flushed
  // immediately, so it is safe to simply send a notification now.
  APZCTM_LOG("Flushing repaints for layers id 0x%" PRIx64 "\n",
             uint64_t(aLayersId));
  RefPtr<GeckoContentController> controller = GetContentController(aLayersId);
#ifndef MOZ_WIDGET_ANDROID
  // On Android, this code is run in production and may actually get a nullptr
  // controller here. On other platforms this code is test-only and should never
  // get a nullptr.
  MOZ_ASSERT(controller);
#endif
  if (controller) {
    controller->DispatchToRepaintThread(NewRunnableMethod(
        "layers::GeckoContentController::NotifyFlushComplete", controller,
        &GeckoContentController::NotifyFlushComplete));
  }
}

void APZCTreeManager::MarkAsDetached(LayersId aLayersId) {
  RecursiveMutexAutoLock lock(mTreeLock);
  mDetachedLayersIds.insert(aLayersId);
}

static bool HasNonLockModifier(Modifiers aModifiers) {
  return (aModifiers &
          (MODIFIER_ALT | MODIFIER_ALTGRAPH | MODIFIER_CONTROL | MODIFIER_FN |
           MODIFIER_META | MODIFIER_SHIFT | MODIFIER_SYMBOL)) != 0;
}

APZEventResult APZCTreeManager::ReceiveInputEvent(
    InputData& aEvent, InputBlockCallback&& aCallback) {
  APZThreadUtils::AssertOnControllerThread();
  InputHandlingState state{aEvent};

  // Use a RAII class for updating the focus sequence number of this event
  AutoFocusSequenceNumberSetter focusSetter(mFocusState, aEvent);

  switch (aEvent.mInputType) {
    case MULTITOUCH_INPUT: {
      MultiTouchInput& touchInput = aEvent.AsMultiTouchInput();
      ProcessTouchInput(state, touchInput);
      break;
    }
    case MOUSE_INPUT: {
      MouseInput& mouseInput = aEvent.AsMouseInput();
      MOZ_LOG(APZCTreeManager::sLog,
              mouseInput.mType == MouseInput::MOUSE_MOVE ? LogLevel::Verbose
                                                         : LogLevel::Debug,
              ("Received mouse input type %d at %s\n", (int)mouseInput.mType,
               ToString(mouseInput.mOrigin).c_str()));
      mouseInput.mHandledByAPZ = true;

      SetCurrentMousePosition(mouseInput.mOrigin);

      bool startsDrag = DragTracker::StartsDrag(mouseInput);
      if (startsDrag) {
        // If this is the start of a drag we need to unambiguously know if it's
        // going to land on a scrollbar or not. We can't apply an untransform
        // here without knowing that, so we need to ensure the untransform is
        // a no-op.
        FlushRepaintsToClearScreenToGeckoTransform();
      }
      const bool endsDrag = DragTracker::EndsDrag(mouseInput);
      if (endsDrag) {
        mDragBlockHitResult = HitTestResult();
      }

      state.mHit = GetTargetAPZCForMouseInput(mouseInput);

      bool hitScrollbar = (bool)state.mHit.mScrollbarNode;
      if (startsDrag && hitScrollbar) {
        RecursiveMutexAutoLock lock(mTreeLock);
        mDragBlockHitResult = mHitTester->CloneHitTestResult(lock, state.mHit);
      }

      // When the mouse is outside the window we still want to handle dragging
      // but we won't find an APZC. Fallback to root APZC then.
      {  // scope lock
        RecursiveMutexAutoLock lock(mTreeLock);
        if (!state.mHit.mTargetApzc && mRootNode) {
          state.mHit.mTargetApzc = mRootNode->GetApzc();
        }
      }

      if (state.mHit.mTargetApzc) {
        if (StaticPrefs::apz_test_logging_enabled() &&
            mouseInput.mType == MouseInput::MOUSE_HITTEST) {
          ScrollableLayerGuid guid = state.mHit.mTargetApzc->GetGuid();

          MutexAutoLock lock(mTestDataLock);
          auto it = mTestData.find(guid.mLayersId);
          MOZ_ASSERT(it != mTestData.end());
          it->second->RecordHitResult(mouseInput.mOrigin, state.mHit.mHitResult,
                                      guid.mLayersId, guid.mScrollId);
        }

        TargetConfirmationFlags confFlags{state.mHit.mHitResult};
        state.mResult = mInputQueue->ReceiveInputEvent(state.mHit.mTargetApzc,
                                                       confFlags, mouseInput);

        // If we're starting an async scrollbar drag
        bool apzDragEnabled = StaticPrefs::apz_drag_enabled();
        if (apzDragEnabled && startsDrag && state.mHit.mScrollbarNode &&
            state.mHit.mScrollbarNode->IsScrollThumbNode() &&
            state.mHit.mScrollbarNode->GetScrollbarData()
                .mThumbIsAsyncDraggable) {
          SetupScrollbarDrag(mouseInput, state.mHit.mScrollbarNode,
                             state.mHit.mTargetApzc.get());
        }

        if (state.mResult.GetStatus() == nsEventStatus_eConsumeDoDefault) {
          // This input event is part of a drag block, so whether or not it is
          // directed at a scrollbar depends on whether the drag block started
          // on a scrollbar.
          hitScrollbar = mInputQueue->IsDragOnScrollbar(hitScrollbar);
        }

        if (!hitScrollbar) {
          // The input was not targeted at a scrollbar, so we untransform it
          // like we do for other content. Scrollbars are "special" because they
          // have special handling in AsyncCompositionManager when resolution is
          // applied. TODO: we should find a better way to deal with this.
          ScreenToParentLayerMatrix4x4 transformToApzc =
              GetScreenToApzcTransform(state.mHit.mTargetApzc);
          ParentLayerToScreenMatrix4x4 transformToGecko =
              GetApzcToGeckoTransformForHit(state.mHit);
          ScreenToScreenMatrix4x4 outTransform =
              transformToApzc * transformToGecko;
          Maybe<ScreenPoint> untransformedRefPoint =
              UntransformBy(outTransform, mouseInput.mOrigin);
          if (untransformedRefPoint) {
            mouseInput.mOrigin = *untransformedRefPoint;
          }
        } else {
          // Likewise, if the input was targeted at a scrollbar, we don't want
          // to apply the callback transform in the main thread, so we remove
          // the scrollid from the guid. We need to keep the layersId intact so
          // that the response from the child process doesn't get discarded.
          state.mResult.mTargetGuid.mScrollId =
              ScrollableLayerGuid::NULL_SCROLL_ID;
        }
      }
      break;
    }
    case SCROLLWHEEL_INPUT: {
      FlushRepaintsToClearScreenToGeckoTransform();

      // Do this before early return for Fission hit testing.
      ScrollWheelInput& wheelInput = aEvent.AsScrollWheelInput();
      APZCTM_LOG("Received wheel input at %s with delta (%f, %f)\n",
                 ToString(wheelInput.mOrigin).c_str(), wheelInput.mDeltaX,
                 wheelInput.mDeltaY);
      state.mHit = GetTargetAPZC(wheelInput.mOrigin);

      wheelInput.mHandledByAPZ = WillHandleInput(wheelInput);
      if (!wheelInput.mHandledByAPZ) {
        return state.Finish(*this, std::move(aCallback));
      }

      if (state.mHit.mTargetApzc) {
        MOZ_ASSERT(state.mHit.mHitResult != CompositorHitTestInvisibleToHit);

        if (wheelInput.mAPZAction == APZWheelAction::PinchZoom) {
          // The mousewheel may have hit a subframe, but we want to send the
          // pinch-zoom events to the root-content APZC.
          {
            RecursiveMutexAutoLock lock(mTreeLock);
            state.mHit.mTargetApzc = FindRootContentApzcForLayersId(
                state.mHit.mTargetApzc->GetLayersId());
          }
          if (state.mHit.mTargetApzc) {
            SynthesizePinchGestureFromMouseWheel(wheelInput,
                                                 state.mHit.mTargetApzc);
          }
          state.mResult.SetStatusAsConsumeNoDefault();
          return state.Finish(*this, std::move(aCallback));
        }

        MOZ_ASSERT(wheelInput.mAPZAction == APZWheelAction::Scroll);

        // For wheel events, the call to ReceiveInputEvent below may result in
        // scrolling, which changes the async transform. However, the event we
        // want to pass to gecko should be the pre-scroll event coordinates,
        // transformed into the gecko space. (pre-scroll because the mouse
        // cursor is stationary during wheel scrolling, unlike touchmove
        // events). Since we just flushed the pending repaints the transform to
        // gecko space should only consist of overscroll-cancelling transforms.
        ScreenToScreenMatrix4x4 transformToGecko =
            GetScreenToApzcTransform(state.mHit.mTargetApzc) *
            GetApzcToGeckoTransformForHit(state.mHit);
        Maybe<ScreenPoint> untransformedOrigin =
            UntransformBy(transformToGecko, wheelInput.mOrigin);

        if (!untransformedOrigin) {
          return state.Finish(*this, std::move(aCallback));
        }

        state.mResult = mInputQueue->ReceiveInputEvent(
            state.mHit.mTargetApzc,
            TargetConfirmationFlags{state.mHit.mHitResult}, wheelInput);

        // Update the out-parameters so they are what the caller expects.
        wheelInput.mOrigin = *untransformedOrigin;
      }
      break;
    }
    case PANGESTURE_INPUT: {
      FlushRepaintsToClearScreenToGeckoTransform();

      // Do this before early return for Fission hit testing.
      PanGestureInput& panInput = aEvent.AsPanGestureInput();
      APZCTM_LOG("Received pan gesture input type %d at %s with delta %s\n",
                 (int)panInput.mType, ToString(panInput.mPanStartPoint).c_str(),
                 ToString(panInput.mPanDisplacement).c_str());
      state.mHit = GetTargetAPZC(panInput.mPanStartPoint);

      panInput.mHandledByAPZ = WillHandleInput(panInput);
      if (!panInput.mHandledByAPZ) {
        if (mInputQueue->GetCurrentPanGestureBlock()) {
          if (state.mHit.mTargetApzc &&
              (panInput.mType == PanGestureInput::PANGESTURE_END ||
               panInput.mType == PanGestureInput::PANGESTURE_CANCELLED)) {
            // If we've already been processing a pan gesture in an APZC but
            // fall into this _if_ branch, which means this pan-end or
            // pan-cancelled event will not be proccessed in the APZC, send a
            // pan-interrupted event to stop any on-going work for the pan
            // gesture, otherwise we will get stuck at an intermidiate state
            // becasue we might no longer receive any events which will be
            // handled by the APZC.
            PanGestureInput panInterrupted(
                PanGestureInput::PANGESTURE_INTERRUPTED, panInput.mTimeStamp,
                panInput.mPanStartPoint, panInput.mPanDisplacement,
                panInput.modifiers);
            Unused << mInputQueue->ReceiveInputEvent(
                state.mHit.mTargetApzc,
                TargetConfirmationFlags{state.mHit.mHitResult}, panInterrupted);
          }
        }
        return state.Finish(*this, std::move(aCallback));
      }

      // If/when we enable support for pan inputs off-main-thread, we'll need
      // to duplicate this EventStateManager code or something. See the call to
      // GetUserPrefsForWheelEvent in APZInputBridge.cpp for why these fields
      // are stored separately.
      MOZ_ASSERT(NS_IsMainThread());
      WidgetWheelEvent wheelEvent = panInput.ToWidgetEvent(nullptr);
      EventStateManager::GetUserPrefsForWheelEvent(
          &wheelEvent, &panInput.mUserDeltaMultiplierX,
          &panInput.mUserDeltaMultiplierY);

      if (state.mHit.mTargetApzc) {
        MOZ_ASSERT(state.mHit.mHitResult != CompositorHitTestInvisibleToHit);

        // For pan gesture events, the call to ReceiveInputEvent below may
        // result in scrolling, which changes the async transform. However, the
        // event we want to pass to gecko should be the pre-scroll event
        // coordinates, transformed into the gecko space. (pre-scroll because
        // the mouse cursor is stationary during pan gesture scrolling, unlike
        // touchmove events). Since we just flushed the pending repaints the
        // transform to gecko space should only consist of overscroll-cancelling
        // transforms.
        ScreenToScreenMatrix4x4 transformToGecko =
            GetScreenToApzcTransform(state.mHit.mTargetApzc) *
            GetApzcToGeckoTransformForHit(state.mHit);
        Maybe<ScreenPoint> untransformedStartPoint =
            UntransformBy(transformToGecko, panInput.mPanStartPoint);
        Maybe<ScreenPoint> untransformedDisplacement =
            UntransformVector(transformToGecko, panInput.mPanDisplacement,
                              panInput.mPanStartPoint);

        if (!untransformedStartPoint || !untransformedDisplacement) {
          return state.Finish(*this, std::move(aCallback));
        }

        panInput.mOverscrollBehaviorAllowsSwipe =
            state.mHit.mTargetApzc->OverscrollBehaviorAllowsSwipe();

        state.mResult = mInputQueue->ReceiveInputEvent(
            state.mHit.mTargetApzc,
            TargetConfirmationFlags{state.mHit.mHitResult}, panInput);

        // Update the out-parameters so they are what the caller expects.
        panInput.mPanStartPoint = *untransformedStartPoint;
        panInput.mPanDisplacement = *untransformedDisplacement;
      }
      break;
    }
    case PINCHGESTURE_INPUT: {
      PinchGestureInput& pinchInput = aEvent.AsPinchGestureInput();
      if (HasNonLockModifier(pinchInput.modifiers)) {
        APZCTM_LOG("Discarding pinch input due to modifiers 0x%x\n",
                   pinchInput.modifiers);
        return state.Finish(*this, std::move(aCallback));
      }

      state.mHit = GetTargetAPZC(pinchInput.mFocusPoint);

      // We always handle pinch gestures as pinch zooms.
      pinchInput.mHandledByAPZ = true;

      if (state.mHit.mTargetApzc) {
        MOZ_ASSERT(state.mHit.mHitResult != CompositorHitTestInvisibleToHit);

        if (!state.mHit.mTargetApzc->IsRootContent()) {
          state.mHit.mTargetApzc = FindZoomableApzc(state.mHit.mTargetApzc);
        }
      }

      if (state.mHit.mTargetApzc) {
        ScreenToScreenMatrix4x4 outTransform =
            GetScreenToApzcTransform(state.mHit.mTargetApzc) *
            GetApzcToGeckoTransformForHit(state.mHit);
        Maybe<ScreenPoint> untransformedFocusPoint =
            UntransformBy(outTransform, pinchInput.mFocusPoint);

        if (!untransformedFocusPoint) {
          return state.Finish(*this, std::move(aCallback));
        }

        state.mResult = mInputQueue->ReceiveInputEvent(
            state.mHit.mTargetApzc,
            TargetConfirmationFlags{state.mHit.mHitResult}, pinchInput);

        // Update the out-parameters so they are what the caller expects.
        pinchInput.mFocusPoint = *untransformedFocusPoint;
      }
      break;
    }
    case TAPGESTURE_INPUT: {  // note: no one currently sends these
      TapGestureInput& tapInput = aEvent.AsTapGestureInput();
      state.mHit = GetTargetAPZC(tapInput.mPoint);

      if (state.mHit.mTargetApzc) {
        MOZ_ASSERT(state.mHit.mHitResult != CompositorHitTestInvisibleToHit);

        ScreenToScreenMatrix4x4 outTransform =
            GetScreenToApzcTransform(state.mHit.mTargetApzc) *
            GetApzcToGeckoTransformForHit(state.mHit);
        Maybe<ScreenIntPoint> untransformedPoint =
            UntransformBy(outTransform, tapInput.mPoint);

        if (!untransformedPoint) {
          return state.Finish(*this, std::move(aCallback));
        }

        // Tap gesture events are not grouped into input blocks, and they're
        // never queued in InputQueue, but processed right away. So, we only
        // need to set |mTapGestureHitResult| for the duration of the
        // InputQueue::ReceiveInputEvent() call.
        {
          RecursiveMutexAutoLock lock(mTreeLock);
          mTapGestureHitResult =
              mHitTester->CloneHitTestResult(lock, state.mHit);
        }

        state.mResult = mInputQueue->ReceiveInputEvent(
            state.mHit.mTargetApzc,
            TargetConfirmationFlags{state.mHit.mHitResult}, tapInput);

        mTapGestureHitResult = HitTestResult();

        // Update the out-parameters so they are what the caller expects.
        tapInput.mPoint = *untransformedPoint;
      }
      break;
    }
    case KEYBOARD_INPUT: {
      // Disable async keyboard scrolling when accessibility.browsewithcaret is
      // enabled
      if (!StaticPrefs::apz_keyboard_enabled_AtStartup() ||
          StaticPrefs::accessibility_browsewithcaret()) {
        APZ_KEY_LOG("Skipping key input from invalid prefs\n");
        return state.Finish(*this, std::move(aCallback));
      }

      KeyboardInput& keyInput = aEvent.AsKeyboardInput();

      // Try and find a matching shortcut for this keyboard input
      Maybe<KeyboardShortcut> shortcut = mKeyboardMap.FindMatch(keyInput);

      if (!shortcut) {
        APZ_KEY_LOG("Skipping key input with no shortcut\n");

        // If we don't have a shortcut for this key event, then we can keep our
        // focus only if we know there are no key event listeners for this
        // target
        if (mFocusState.CanIgnoreKeyboardShortcutMisses()) {
          focusSetter.MarkAsNonFocusChanging();
        }
        return state.Finish(*this, std::move(aCallback));
      }

      // Check if this shortcut needs to be dispatched to content. Anything
      // matching this is assumed to be able to change focus.
      if (shortcut->mDispatchToContent) {
        APZ_KEY_LOG("Skipping key input with dispatch-to-content shortcut\n");
        return state.Finish(*this, std::move(aCallback));
      }

      // We know we have an action to execute on whatever is the current focus
      // target
      const KeyboardScrollAction& action = shortcut->mAction;

      // The current focus target depends on which direction the scroll is to
      // happen
      Maybe<ScrollableLayerGuid> targetGuid;
      switch (action.mType) {
        case KeyboardScrollAction::eScrollCharacter: {
          targetGuid = mFocusState.GetHorizontalTarget();
          break;
        }
        case KeyboardScrollAction::eScrollLine:
        case KeyboardScrollAction::eScrollPage:
        case KeyboardScrollAction::eScrollComplete: {
          targetGuid = mFocusState.GetVerticalTarget();
          break;
        }
      }

      // If we don't have a scroll target then either we have a stale focus
      // target, the focused element has event listeners, or the focused element
      // doesn't have a layerized scroll frame. In any case we need to dispatch
      // to content.
      if (!targetGuid) {
        APZ_KEY_LOG("Skipping key input with no current focus target\n");
        return state.Finish(*this, std::move(aCallback));
      }

      RefPtr<AsyncPanZoomController> targetApzc =
          GetTargetAPZC(targetGuid->mLayersId, targetGuid->mScrollId);

      if (!targetApzc) {
        APZ_KEY_LOG("Skipping key input with focus target but no APZC\n");
        return state.Finish(*this, std::move(aCallback));
      }

      // Attach the keyboard scroll action to the input event for processing
      // by the input queue.
      keyInput.mAction = action;

      APZ_KEY_LOG("Dispatching key input with apzc=%p\n", targetApzc.get());

      // Dispatch the event to the input queue.
      state.mResult = mInputQueue->ReceiveInputEvent(
          targetApzc, TargetConfirmationFlags{true}, keyInput);

      // Any keyboard event that is dispatched to the input queue at this point
      // should have been consumed
      MOZ_ASSERT(state.mResult.GetStatus() == nsEventStatus_eConsumeDoDefault ||
                 state.mResult.GetStatus() == nsEventStatus_eConsumeNoDefault);

      keyInput.mHandledByAPZ = true;
      focusSetter.MarkAsNonFocusChanging();

      break;
    }
  }
  return state.Finish(*this, std::move(aCallback));
}

static TouchBehaviorFlags ConvertToTouchBehavior(
    const CompositorHitTestInfo& info) {
  TouchBehaviorFlags result = AllowedTouchBehavior::UNKNOWN;
  if (info == CompositorHitTestInvisibleToHit) {
    result = AllowedTouchBehavior::NONE;
  } else if (info.contains(CompositorHitTestFlags::eIrregularArea)) {
    // Note that eApzAwareListeners and eInactiveScrollframe are similar
    // to eIrregularArea in some respects, but are not relevant for the
    // purposes of this function, which deals specifically with touch-action.
    result = AllowedTouchBehavior::UNKNOWN;
  } else {
    result = AllowedTouchBehavior::VERTICAL_PAN |
             AllowedTouchBehavior::HORIZONTAL_PAN |
             AllowedTouchBehavior::PINCH_ZOOM |
             AllowedTouchBehavior::ANIMATING_ZOOM;
    if (info.contains(CompositorHitTestFlags::eTouchActionPanXDisabled)) {
      result &= ~AllowedTouchBehavior::HORIZONTAL_PAN;
    }
    if (info.contains(CompositorHitTestFlags::eTouchActionPanYDisabled)) {
      result &= ~AllowedTouchBehavior::VERTICAL_PAN;
    }
    if (info.contains(CompositorHitTestFlags::eTouchActionPinchZoomDisabled)) {
      result &= ~AllowedTouchBehavior::PINCH_ZOOM;
    }
    if (info.contains(
            CompositorHitTestFlags::eTouchActionAnimatingZoomDisabled)) {
      result &= ~AllowedTouchBehavior::ANIMATING_ZOOM;
    }
  }
  return result;
}

APZCTreeManager::HitTestResult APZCTreeManager::GetTouchInputBlockAPZC(
    const MultiTouchInput& aEvent,
    nsTArray<TouchBehaviorFlags>* aOutTouchBehaviors) {
  HitTestResult hit;
  if (aEvent.mTouches.Length() == 0) {
    return hit;
  }

  FlushRepaintsToClearScreenToGeckoTransform();

  hit = GetTargetAPZC(aEvent.mTouches[0].mScreenPoint);
  // Don't set a layers id on multi-touch events.
  if (aEvent.mTouches.Length() != 1) {
    hit.mLayersId = LayersId{0};
  }

  aOutTouchBehaviors->AppendElement(ConvertToTouchBehavior(hit.mHitResult));
  for (size_t i = 1; i < aEvent.mTouches.Length(); i++) {
    HitTestResult hit2 = GetTargetAPZC(aEvent.mTouches[i].mScreenPoint);
    aOutTouchBehaviors->AppendElement(ConvertToTouchBehavior(hit2.mHitResult));
    hit.mTargetApzc = GetZoomableTarget(hit.mTargetApzc, hit2.mTargetApzc);
    APZCTM_LOG("Using APZC %p as the root APZC for multi-touch\n",
               hit.mTargetApzc.get());
    // A multi-touch gesture will not be a scrollbar drag, even if the
    // first touch point happened to hit a scrollbar.
    hit.mScrollbarNode.Clear();

    // XXX we should probably be combining the hit results from the different
    // touch points somehow, instead of just using the last one.
    hit.mHitResult = hit2.mHitResult;
  }

  return hit;
}

APZEventResult APZCTreeManager::InputHandlingState::Finish(
    APZCTreeManager& aTreeManager, InputBlockCallback&& aCallback) {
  // The validity check here handles both the case where mHit was
  // never populated (because this event did not trigger a hit-test),
  // and the case where it was populated with an invalid LayersId
  // (which can happen e.g. for multi-touch events).
  if (mHit.mLayersId.IsValid()) {
    mEvent.mLayersId = mHit.mLayersId;
  }

  if (mEvent.mInputType == SCROLLWHEEL_INPUT ||
      mEvent.mInputType == PANGESTURE_INPUT) {
    aTreeManager.MaybeOverrideLayersIdForWheelEvent(mEvent);
  }

  // Absorb events that are in targetted at a position in the gutter,
  // unless they are fixed position elements.
  if (mHit.mHitOverscrollGutter && mHit.mFixedPosSides == SideBits::eNone) {
    mResult.SetStatusAsConsumeNoDefault();
  }

  // If the event will have a delayed result then add the callback to the
  // APZCTreeManager.
  if (aCallback && mResult.WillHaveDelayedResult()) {
    aTreeManager.AddInputBlockCallback(mResult.mInputBlockId,
                                       std::move(aCallback));
  }

  return mResult;
}

void APZCTreeManager::ProcessTouchInput(InputHandlingState& aState,
                                        MultiTouchInput& aInput) {
  APZCTM_LOG("Received touch input type %d with touch points [%s]\n",
             (int)aInput.mType,
             [&] {
               nsCString result;
               for (const auto& touch : aInput.mTouches) {
                 result.AppendPrintf("%s",
                                     ToString(touch.mScreenPoint).c_str());
               }
               return result;
             }()
                 .get());

  aInput.mHandledByAPZ = true;
  nsTArray<TouchBehaviorFlags> touchBehaviors;
  HitTestingTreeNodeAutoLock hitScrollbarNode;
  if (aInput.mType == MultiTouchInput::MULTITOUCH_START) {
    // If we are panned into overscroll and a second finger goes down,
    // ignore that second touch point completely. The touch-start for it is
    // dropped completely; subsequent touch events until the touch-end for it
    // will have this touch point filtered out.
    // (By contrast, if we're in overscroll but not panning, such as after
    // putting two fingers down during an overscroll animation, we process the
    // second touch and proceed to pinch.)
    if (mTouchBlockHitResult.mTargetApzc &&
        mTouchBlockHitResult.mTargetApzc->IsInPanningState() &&
        BuildOverscrollHandoffChain(mTouchBlockHitResult.mTargetApzc)
            ->HasOverscrolledApzc()) {
      if (mRetainedTouchIdentifier == -1) {
        mRetainedTouchIdentifier =
            mTouchBlockHitResult.mTargetApzc->GetLastTouchIdentifier();
      }

      aState.mResult.SetStatusAsConsumeNoDefault();
      return;
    }

    aState.mHit = GetTouchInputBlockAPZC(aInput, &touchBehaviors);
    RecursiveMutexAutoLock lock(mTreeLock);
    // Repopulate mTouchBlockHitResult from the input state.
    mTouchBlockHitResult = mHitTester->CloneHitTestResult(lock, aState.mHit);
    hitScrollbarNode = std::move(aState.mHit.mScrollbarNode);

    // Check if this event starts a scrollbar touch-drag. The conditions
    // checked are similar to the ones we check for MOUSE_INPUT starting
    // a scrollbar mouse-drag.
    mInScrollbarTouchDrag =
        StaticPrefs::apz_drag_enabled() &&
        StaticPrefs::apz_drag_touch_enabled() && hitScrollbarNode &&
        hitScrollbarNode->IsScrollThumbNode() &&
        hitScrollbarNode->GetScrollbarData().mThumbIsAsyncDraggable;

    MOZ_ASSERT(touchBehaviors.Length() == aInput.mTouches.Length());
    for (size_t i = 0; i < touchBehaviors.Length(); i++) {
      APZCTM_LOG("Touch point has allowed behaviours 0x%02x\n",
                 touchBehaviors[i]);
      if (touchBehaviors[i] == AllowedTouchBehavior::UNKNOWN) {
        // If there's any unknown items in the list, throw it out and we'll
        // wait for the main thread to send us a notification.
        touchBehaviors.Clear();
        break;
      }
    }
  } else if (mTouchBlockHitResult.mTargetApzc) {
    APZCTM_LOG("Re-using APZC %p as continuation of event block\n",
               mTouchBlockHitResult.mTargetApzc.get());
    RecursiveMutexAutoLock lock(mTreeLock);
    aState.mHit = mHitTester->CloneHitTestResult(lock, mTouchBlockHitResult);
  }

  if (mInScrollbarTouchDrag) {
    aState.mResult = ProcessTouchInputForScrollbarDrag(
        aInput, hitScrollbarNode, mTouchBlockHitResult.mHitResult);
  } else {
    // If we receive a touch-cancel, it means all touches are finished, so we
    // can stop ignoring any that we were ignoring.
    if (aInput.mType == MultiTouchInput::MULTITOUCH_CANCEL) {
      mRetainedTouchIdentifier = -1;
    }

    // If we are currently ignoring any touch points, filter them out from the
    // set of touch points included in this event. Note that we modify aInput
    // itself, so that the touch points are also filtered out when the caller
    // passes the event on to content.
    if (mRetainedTouchIdentifier != -1) {
      for (size_t j = 0; j < aInput.mTouches.Length(); ++j) {
        if (aInput.mTouches[j].mIdentifier != mRetainedTouchIdentifier) {
          aInput.mTouches.RemoveElementAt(j);
          if (!touchBehaviors.IsEmpty()) {
            MOZ_ASSERT(touchBehaviors.Length() > j);
            touchBehaviors.RemoveElementAt(j);
          }
          --j;
        }
      }
      if (aInput.mTouches.IsEmpty()) {
        aState.mResult.SetStatusAsConsumeNoDefault();
        return;
      }
    }

    if (mTouchBlockHitResult.mTargetApzc) {
      MOZ_ASSERT(mTouchBlockHitResult.mHitResult !=
                 CompositorHitTestInvisibleToHit);

      aState.mResult = mInputQueue->ReceiveInputEvent(
          mTouchBlockHitResult.mTargetApzc,
          TargetConfirmationFlags{mTouchBlockHitResult.mHitResult}, aInput,
          touchBehaviors.IsEmpty() ? Nothing()
                                   : Some(std::move(touchBehaviors)));

      // For computing the event to pass back to Gecko, use up-to-date
      // transforms (i.e. not anything cached in an input block). This ensures
      // that transformToApzc and transformToGecko are in sync.
      // Note: we are not using ConvertToGecko() here, because we don't
      // want to multiply transformToApzc and transformToGecko once
      // for each touch point.
      ScreenToParentLayerMatrix4x4 transformToApzc =
          GetScreenToApzcTransform(mTouchBlockHitResult.mTargetApzc);
      ParentLayerToScreenMatrix4x4 transformToGecko =
          GetApzcToGeckoTransformForHit(mTouchBlockHitResult);
      ScreenToScreenMatrix4x4 outTransform = transformToApzc * transformToGecko;

      for (size_t i = 0; i < aInput.mTouches.Length(); i++) {
        SingleTouchData& touchData = aInput.mTouches[i];
        Maybe<ScreenIntPoint> untransformedScreenPoint =
            UntransformBy(outTransform, touchData.mScreenPoint);
        if (!untransformedScreenPoint) {
          aState.mResult.SetStatusAsIgnore();
          return;
        }
        touchData.mScreenPoint = *untransformedScreenPoint;
        AdjustEventPointForDynamicToolbar(touchData.mScreenPoint,
                                          mTouchBlockHitResult);
      }
    }
  }

  mTouchCounter.Update(aInput);

  // If it's the end of the touch sequence then clear out variables so we
  // don't keep dangling references and leak things.
  if (mTouchCounter.GetActiveTouchCount() == 0) {
    mTouchBlockHitResult = HitTestResult();
    mRetainedTouchIdentifier = -1;
    mInScrollbarTouchDrag = false;
  }
}

void APZCTreeManager::AdjustEventPointForDynamicToolbar(
    ScreenIntPoint& aEventPoint, const HitTestResult& aHit) {
  if (aHit.mFixedPosSides != SideBits::eNone) {
    MutexAutoLock lock(mMapLock);
    aEventPoint -= RoundedToInt(ComputeFixedMarginsOffset(
        lock, aHit.mFixedPosSides, mGeckoFixedLayerMargins));
  } else if (aHit.mNode && aHit.mNode->GetStickyPositionAnimationId()) {
    SideBits sideBits = SideBits::eNone;
    {
      RecursiveMutexAutoLock lock(mTreeLock);
      sideBits = SidesStuckToRootContent(
          aHit.mNode.Get(lock), AsyncTransformConsumer::eForEventHandling);
    }
    MutexAutoLock lock(mMapLock);
    aEventPoint -= RoundedToInt(
        ComputeFixedMarginsOffset(lock, sideBits, mGeckoFixedLayerMargins));
  }
}

static MouseInput::MouseType MultiTouchTypeToMouseType(
    MultiTouchInput::MultiTouchType aType) {
  switch (aType) {
    case MultiTouchInput::MULTITOUCH_START:
      return MouseInput::MOUSE_DOWN;
    case MultiTouchInput::MULTITOUCH_MOVE:
      return MouseInput::MOUSE_MOVE;
    case MultiTouchInput::MULTITOUCH_END:
    case MultiTouchInput::MULTITOUCH_CANCEL:
      return MouseInput::MOUSE_UP;
  }
  MOZ_ASSERT_UNREACHABLE("Invalid multi-touch type");
  return MouseInput::MOUSE_NONE;
}

APZEventResult APZCTreeManager::ProcessTouchInputForScrollbarDrag(
    MultiTouchInput& aTouchInput,
    const HitTestingTreeNodeAutoLock& aScrollThumbNode,
    const gfx::CompositorHitTestInfo& aHitInfo) {
  MOZ_ASSERT(mRetainedTouchIdentifier == -1);
  MOZ_ASSERT(mTouchBlockHitResult.mTargetApzc);
  MOZ_ASSERT(aTouchInput.mTouches.Length() == 1);

  // Synthesize a mouse event based on the touch event, so that we can
  // reuse code in InputQueue and APZC for handling scrollbar mouse-drags.
  MouseInput mouseInput{MultiTouchTypeToMouseType(aTouchInput.mType),
                        MouseInput::PRIMARY_BUTTON,
                        dom::MouseEvent_Binding::MOZ_SOURCE_TOUCH,
                        MouseButtonsFlag::ePrimaryFlag,
                        aTouchInput.mTouches[0].mScreenPoint,
                        aTouchInput.mTimeStamp,
                        aTouchInput.modifiers};
  mouseInput.mHandledByAPZ = true;

  TargetConfirmationFlags targetConfirmed{aHitInfo};
  APZEventResult result;
  result = mInputQueue->ReceiveInputEvent(mTouchBlockHitResult.mTargetApzc,
                                          targetConfirmed, mouseInput);

  // |aScrollThumbNode| is non-null iff. this is the event that starts the drag.
  // If so, set up the drag.
  if (aScrollThumbNode) {
    SetupScrollbarDrag(mouseInput, aScrollThumbNode,
                       mTouchBlockHitResult.mTargetApzc.get());
  }

  // Since the input was targeted at a scrollbar:
  //    - The original touch event (which will be sent on to content) will
  //      not be untransformed.
  //    - We don't want to apply the callback transform in the main thread,
  //      so we remove the scrollid from the guid.
  // Both of these match the behaviour of mouse events that target a scrollbar;
  // see the code for handling mouse events in ReceiveInputEvent() for
  // additional explanation.
  result.mTargetGuid.mScrollId = ScrollableLayerGuid::NULL_SCROLL_ID;

  return result;
}

void APZCTreeManager::SetupScrollbarDrag(
    MouseInput& aMouseInput, const HitTestingTreeNodeAutoLock& aScrollThumbNode,
    AsyncPanZoomController* aApzc) {
  DragBlockState* dragBlock = mInputQueue->GetCurrentDragBlock();
  if (!dragBlock) {
    return;
  }

  const ScrollbarData& thumbData = aScrollThumbNode->GetScrollbarData();
  MOZ_ASSERT(thumbData.mDirection.isSome());

  // Record the thumb's position at the start of the drag.
  // We snap back to this position if, during the drag, the mouse
  // gets sufficiently far away from the scrollbar.
  dragBlock->SetInitialThumbPos(thumbData.mThumbStart);

  // Under some conditions, we can confirm the drag block right away.
  // Otherwise, we have to wait for a main-thread confirmation.
  if (/* check that the scrollbar's target scroll frame is layerized */
      aScrollThumbNode->GetScrollTargetId() == aApzc->GetGuid().mScrollId &&
      !aApzc->IsScrollInfoLayer()) {
    uint64_t dragBlockId = dragBlock->GetBlockId();
    // AsyncPanZoomController::HandleInputEvent() will call
    // TransformToLocal() on the event, but we need its mLocalOrigin now
    // to compute a drag start offset for the AsyncDragMetrics.
    aMouseInput.TransformToLocal(aApzc->GetTransformToThis());
    OuterCSSCoord dragStart =
        aApzc->ConvertScrollbarPoint(aMouseInput.mLocalOrigin, thumbData);
    // ConvertScrollbarPoint() got the drag start offset relative to
    // the scroll track. Now get it relative to the thumb.
    // ScrollThumbData::mThumbStart stores the offset of the thumb
    // relative to the scroll track at the time of the last paint.
    // Since that paint, the thumb may have acquired an async transform
    // due to async scrolling, so look that up and apply it.
    LayerToParentLayerMatrix4x4 thumbTransform;
    {
      RecursiveMutexAutoLock lock(mTreeLock);
      thumbTransform =
          ComputeTransformForScrollThumbNode(aScrollThumbNode.Get(lock));
    }
    // Only consider the translation, since we do not support both
    // zooming and scrollbar dragging on any platform.
    // FIXME: We do now.
    OuterCSSCoord thumbStart =
        thumbData.mThumbStart +
        ((*thumbData.mDirection == ScrollDirection::eHorizontal)
             ? thumbTransform._41
             : thumbTransform._42);
    dragStart -= thumbStart;

    // Content can't prevent scrollbar dragging with preventDefault(),
    // so we don't need to wait for a content response. It's important
    // to do this before calling ConfirmDragBlock() since that can
    // potentially process and consume the block.
    dragBlock->SetContentResponse(false);

    NotifyScrollbarDragInitiated(dragBlockId, aApzc->GetGuid(),
                                 *thumbData.mDirection);

    mInputQueue->ConfirmDragBlock(
        dragBlockId, aApzc,
        AsyncDragMetrics(aApzc->GetGuid().mScrollId,
                         aApzc->GetGuid().mPresShellId, dragBlockId, dragStart,
                         *thumbData.mDirection));
  }
}

void APZCTreeManager::SynthesizePinchGestureFromMouseWheel(
    const ScrollWheelInput& aWheelInput,
    const RefPtr<AsyncPanZoomController>& aTarget) {
  MOZ_ASSERT(aTarget);

  ScreenPoint focusPoint = aWheelInput.mOrigin;

  // Compute span values based on the wheel delta.
  ScreenCoord oldSpan = 100;
  ScreenCoord newSpan = oldSpan + aWheelInput.mDeltaY;

  // There's no ambiguity as to the target for pinch gesture events.
  TargetConfirmationFlags confFlags{true};

  PinchGestureInput pinchStart{PinchGestureInput::PINCHGESTURE_START,
                               PinchGestureInput::MOUSEWHEEL,
                               aWheelInput.mTimeStamp,
                               ExternalPoint(0, 0),
                               focusPoint,
                               oldSpan,
                               oldSpan,
                               aWheelInput.modifiers};
  PinchGestureInput pinchScale1{PinchGestureInput::PINCHGESTURE_SCALE,
                                PinchGestureInput::MOUSEWHEEL,
                                aWheelInput.mTimeStamp,
                                ExternalPoint(0, 0),
                                focusPoint,
                                oldSpan,
                                oldSpan,
                                aWheelInput.modifiers};
  PinchGestureInput pinchScale2{PinchGestureInput::PINCHGESTURE_SCALE,
                                PinchGestureInput::MOUSEWHEEL,
                                aWheelInput.mTimeStamp,
                                ExternalPoint(0, 0),
                                focusPoint,
                                oldSpan,
                                newSpan,
                                aWheelInput.modifiers};
  PinchGestureInput pinchEnd{PinchGestureInput::PINCHGESTURE_END,
                             PinchGestureInput::MOUSEWHEEL,
                             aWheelInput.mTimeStamp,
                             ExternalPoint(0, 0),
                             focusPoint,
                             newSpan,
                             newSpan,
                             aWheelInput.modifiers};

  mInputQueue->ReceiveInputEvent(aTarget, confFlags, pinchStart);
  mInputQueue->ReceiveInputEvent(aTarget, confFlags, pinchScale1);
  mInputQueue->ReceiveInputEvent(aTarget, confFlags, pinchScale2);
  mInputQueue->ReceiveInputEvent(aTarget, confFlags, pinchEnd);
}

void APZCTreeManager::MaybeOverrideLayersIdForWheelEvent(InputData& aEvent) {
  APZThreadUtils::AssertOnControllerThread();

  InputBlockState* txn = nullptr;
  if (aEvent.mInputType == SCROLLWHEEL_INPUT &&
      aEvent.AsScrollWheelInput().mHandledByAPZ) {
    txn = mInputQueue->GetActiveWheelTransaction();
  } else if (aEvent.mInputType == PANGESTURE_INPUT &&
             aEvent.AsPanGestureInput().mHandledByAPZ) {
    txn = mInputQueue->GetCurrentPanGestureBlock();
  }

  APZCTM_LOG("Maybe override txn (0x%p) wheel transactions enabled=%d", txn,
             StaticPrefs::dom_event_wheel_event_groups_enabled());

  // If we're in a wheel transaction, subsequent events in the transaction
  // should be sent to the same content process as the first event, even
  // if content rendered by a different process has scrolled under the
  // cursor.
  if (!txn || !StaticPrefs::dom_event_wheel_event_groups_enabled()) {
    return;
  }

  Maybe<LayersId> layersId = txn->WheelTransactionLayersId();

  APZCTM_LOG("Maybe override layers id (%s) -> (%s)",
             ToString(aEvent.mLayersId).c_str(), ToString(layersId).c_str());

  if (layersId.isSome() && *layersId != LayersId{0}) {
    aEvent.mLayersId = *layersId;
  }
}

void APZCTreeManager::UpdateWheelTransaction(
    LayoutDeviceIntPoint aRefPoint, EventMessage aEventMessage,
    const Maybe<ScrollableLayerGuid>& aTargetGuid) {
  APZThreadUtils::AssertOnControllerThread();

  WheelBlockState* txn = mInputQueue->GetActiveWheelTransaction();
  if (!txn) {
    return;
  }

  // If the transaction has simply timed out, we don't need to do anything
  // else.
  if (txn->MaybeTimeout(TimeStamp::Now())) {
    return;
  }

  switch (aEventMessage) {
    case eMouseMove:
    case eDragOver: {
      ScreenIntPoint point = ViewAs<ScreenPixel>(
          aRefPoint,
          PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);

      txn->OnMouseMove(point, aTargetGuid);

      return;
    }
    case eKeyPress:
    case eKeyUp:
    case eKeyDown:
    case eMouseUp:
    case eMouseDown:
    case eMouseDoubleClick:
    case ePointerAuxClick:
    case ePointerClick:
    case eContextMenu:
    case eDrop:
      txn->EndTransaction();
      return;
    default:
      break;
  }
}

void APZCTreeManager::ProcessUnhandledEvent(LayoutDeviceIntPoint* aRefPoint,
                                            ScrollableLayerGuid* aOutTargetGuid,
                                            uint64_t* aOutFocusSequenceNumber,
                                            LayersId* aOutLayersId) {
  APZThreadUtils::AssertOnControllerThread();

  // Transform the aRefPoint.
  // If the event hits an overscrolled APZC, instruct the caller to ignore it.
  PixelCastJustification LDIsScreen =
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent;
  ScreenIntPoint refPointAsScreen = ViewAs<ScreenPixel>(*aRefPoint, LDIsScreen);
  HitTestResult hit = GetTargetAPZC(refPointAsScreen);
  if (aOutLayersId) {
    *aOutLayersId = hit.mLayersId;
  }
  if (hit.mTargetApzc) {
    MOZ_ASSERT(hit.mHitResult != CompositorHitTestInvisibleToHit);
    hit.mTargetApzc->GetGuid(aOutTargetGuid);
    ScreenToParentLayerMatrix4x4 transformToApzc =
        GetScreenToApzcTransform(hit.mTargetApzc);
    ParentLayerToScreenMatrix4x4 transformToGecko =
        GetApzcToGeckoTransformForHit(hit);
    ScreenToScreenMatrix4x4 outTransform = transformToApzc * transformToGecko;
    Maybe<ScreenIntPoint> untransformedRefPoint =
        UntransformBy(outTransform, refPointAsScreen);
    if (untransformedRefPoint) {
      *aRefPoint =
          ViewAs<LayoutDevicePixel>(*untransformedRefPoint, LDIsScreen);
    }
  }

  // Update the focus sequence number and attach it to the event
  mFocusState.ReceiveFocusChangingEvent();
  *aOutFocusSequenceNumber = mFocusState.LastAPZProcessedEvent();
}

void APZCTreeManager::SetKeyboardMap(const KeyboardMap& aKeyboardMap) {
  if (!APZThreadUtils::IsControllerThread()) {
    APZThreadUtils::RunOnControllerThread(NewRunnableMethod<KeyboardMap>(
        "layers::APZCTreeManager::SetKeyboardMap", this,
        &APZCTreeManager::SetKeyboardMap, aKeyboardMap));
    return;
  }

  APZThreadUtils::AssertOnControllerThread();

  mKeyboardMap = aKeyboardMap;
}

void APZCTreeManager::ZoomToRect(const ScrollableLayerGuid& aGuid,
                                 const ZoomTarget& aZoomTarget,
                                 const uint32_t aFlags) {
  if (!APZThreadUtils::IsControllerThread()) {
    APZThreadUtils::RunOnControllerThread(
        NewRunnableMethod<ScrollableLayerGuid, ZoomTarget, uint32_t>(
            "layers::APZCTreeManager::ZoomToRect", this,
            &APZCTreeManager::ZoomToRect, aGuid, aZoomTarget, aFlags));
    return;
  }

  // We could probably move this to run on the updater thread if needed, but
  // either way we should restrict it to a single thread. For now let's use the
  // controller thread.
  APZThreadUtils::AssertOnControllerThread();

  RefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aGuid);
  if (aFlags & ZOOM_TO_FOCUSED_INPUT) {
    // In the case of ZoomToFocusInput the targetRect is in the in-process root
    // content coordinates. We need to convert it to the top level content
    // coordiates.
    if (apzc) {
      CSSRect transformedRect =
          ConvertRectInApzcToRoot(apzc, aZoomTarget.targetRect);

      transformedRect.Inflate(15.0f, 0.0f);
      // Note that ZoomToFocusedInput doesn't use the other fields of
      // ZoomTarget.
      ZoomTarget zoomTarget{transformedRect};

      apzc = FindZoomableApzc(apzc);
      if (apzc) {
        uint32_t flags = aFlags;
        MutexAutoLock lock(mMapLock);
        if (IsSoftwareKeyboardVisible(lock) &&
            InteractiveWidgetMode(lock) ==
                dom::InteractiveWidget::ResizesVisual) {
          flags |= ZOOM_TO_FOCUSED_INPUT_ON_RESIZES_VISUAL;
        }
        apzc->ZoomToRect(zoomTarget, flags);
      }
    }
    return;
  }

  if (apzc) {
    apzc = FindZoomableApzc(apzc);
    if (apzc) {
      apzc->ZoomToRect(aZoomTarget, aFlags);
    }
  }
}

void APZCTreeManager::ContentReceivedInputBlock(uint64_t aInputBlockId,
                                                bool aPreventDefault) {
  if (!APZThreadUtils::IsControllerThread()) {
    APZThreadUtils::RunOnControllerThread(NewRunnableMethod<uint64_t, bool>(
        "layers::APZCTreeManager::ContentReceivedInputBlock", this,
        &APZCTreeManager::ContentReceivedInputBlock, aInputBlockId,
        aPreventDefault));
    return;
  }

  APZThreadUtils::AssertOnControllerThread();

  mInputQueue->ContentReceivedInputBlock(aInputBlockId, aPreventDefault);
}

void APZCTreeManager::SetTargetAPZC(
    uint64_t aInputBlockId, const nsTArray<ScrollableLayerGuid>& aTargets) {
  if (!APZThreadUtils::IsControllerThread()) {
    APZThreadUtils::RunOnControllerThread(
        NewRunnableMethod<uint64_t,
                          StoreCopyPassByRRef<nsTArray<ScrollableLayerGuid>>>(
            "layers::APZCTreeManager::SetTargetAPZC", this,
            &layers::APZCTreeManager::SetTargetAPZC, aInputBlockId,
            aTargets.Clone()));
    return;
  }

  RefPtr<AsyncPanZoomController> target = nullptr;
  if (aTargets.Length() > 0) {
    target = GetTargetAPZC(aTargets[0]);
  }
  for (size_t i = 1; i < aTargets.Length(); i++) {
    RefPtr<AsyncPanZoomController> apzc = GetTargetAPZC(aTargets[i]);
    target = GetZoomableTarget(target, apzc);
  }
  if (InputBlockState* block = mInputQueue->GetBlockForId(aInputBlockId)) {
    if (block->AsPinchGestureBlock() && aTargets.Length() == 1) {
      target = FindZoomableApzc(target);
    }
  }
  mInputQueue->SetConfirmedTargetApzc(aInputBlockId, target);
}

void APZCTreeManager::UpdateZoomConstraints(
    const ScrollableLayerGuid& aGuid,
    const Maybe<ZoomConstraints>& aConstraints) {
  if (!GetUpdater()->IsUpdaterThread()) {
    // This can happen if we're in the UI process and got a call directly from
    // nsBaseWidget or from a content process over PAPZCTreeManager. In that
    // case we get this call on the compositor thread, which may be different
    // from the updater thread. It can also happen in the GPU process if that is
    // enabled, since the call will go over PAPZCTreeManager and arrive on the
    // compositor thread in the GPU process.
    GetUpdater()->RunOnUpdaterThread(
        aGuid.mLayersId,
        NewRunnableMethod<ScrollableLayerGuid, Maybe<ZoomConstraints>>(
            "APZCTreeManager::UpdateZoomConstraints", this,
            &APZCTreeManager::UpdateZoomConstraints, aGuid, aConstraints));
    return;
  }

  AssertOnUpdaterThread();

  // Propagate the zoom constraints down to the subtree, stopping at APZCs
  // which have their own zoom constraints or are in a different layers id.
  if (aConstraints) {
    APZCTM_LOG("Recording constraints %s for guid %s\n",
               ToString(aConstraints.value()).c_str(), ToString(aGuid).c_str());
    mZoomConstraints[aGuid] = aConstraints.ref();
  } else {
    APZCTM_LOG("Removing constraints for guid %s\n", ToString(aGuid).c_str());
    mZoomConstraints.erase(aGuid);
  }

  RecursiveMutexAutoLock lock(mTreeLock);
  RefPtr<HitTestingTreeNode> node = DepthFirstSearchPostOrder<ReverseIterator>(
      mRootNode.get(), [&aGuid](HitTestingTreeNode* aNode) {
        bool matches = false;
        if (auto zoomId = aNode->GetAsyncZoomContainerId()) {
          matches = ScrollableLayerGuid::EqualsIgnoringPresShell(
              aGuid, ScrollableLayerGuid(aNode->GetLayersId(), 0, *zoomId));
        }
        return matches;
      });

  // This does not hold because we can get zoom constraints updates before the
  // layer tree update with the async zoom container (I assume).
  // clang-format off
  // MOZ_ASSERT(node || aConstraints.isNothing() ||
  //           (!aConstraints->mAllowZoom && !aConstraints->mAllowDoubleTapZoom));
  // clang-format on

  // If there is no async zoom container then the zoom constraints should not
  // allow zooming and building the HTT should have handled clearing the zoom
  // constraints from all nodes so we don't have to handle doing anything in
  // case there is no async zoom container.

  if (node && aConstraints) {
    ForEachNode<ReverseIterator>(node.get(), [&aConstraints, &node, &aGuid,
                                              this](HitTestingTreeNode* aNode) {
      if (aNode != node) {
        // don't go into other async zoom containers
        if (auto zoomId = aNode->GetAsyncZoomContainerId()) {
          MOZ_ASSERT(!ScrollableLayerGuid::EqualsIgnoringPresShell(
              aGuid, ScrollableLayerGuid(aNode->GetLayersId(), 0, *zoomId)));
          return TraversalFlag::Skip;
        }
        if (AsyncPanZoomController* childApzc = aNode->GetApzc()) {
          if (!ScrollableLayerGuid::EqualsIgnoringPresShell(
                  aGuid, childApzc->GetGuid())) {
            // We can have subtrees with their own zoom constraints - leave
            // these alone.
            if (this->mZoomConstraints.find(childApzc->GetGuid()) !=
                this->mZoomConstraints.end()) {
              return TraversalFlag::Skip;
            }
          }
        }
      }
      if (aNode->IsPrimaryHolder()) {
        MOZ_ASSERT(aNode->GetApzc());
        aNode->GetApzc()->UpdateZoomConstraints(aConstraints.ref());
      }
      return TraversalFlag::Continue;
    });
  }
}

void APZCTreeManager::FlushRepaintsToClearScreenToGeckoTransform() {
  // As the name implies, we flush repaint requests for the entire APZ tree in
  // order to clear the screen-to-gecko transform (aka the "untransform" applied
  // to incoming input events before they can be passed on to Gecko).
  //
  // The primary reason we do this is to avoid the problem where input events,
  // after being untransformed, end up hit-testing differently in Gecko. This
  // might happen in cases where the input event lands on content that is async-
  // scrolled into view, but Gecko still thinks it is out of view given the
  // visible area of a scrollframe.
  //
  // Another reason we want to clear the untransform is that if our APZ hit-test
  // hits a dispatch-to-content region then that's an ambiguous result and we
  // need to ask Gecko what actually got hit. In order to do this we need to
  // untransform the input event into Gecko space - but to do that we need to
  // know which APZC got hit! This leads to a circular dependency; the only way
  // to get out of it is to make sure that the untransform for all the possible
  // matched APZCs is the same. It is simplest to ensure that by flushing the
  // pending repaint requests, which makes all of the untransforms empty (and
  // therefore equal).
  RecursiveMutexAutoLock lock(mTreeLock);

  ForEachNode<ReverseIterator>(mRootNode.get(), [](HitTestingTreeNode* aNode) {
    if (aNode->IsPrimaryHolder()) {
      MOZ_ASSERT(aNode->GetApzc());
      aNode->GetApzc()->FlushRepaintForNewInputBlock();
    }
  });
}

void APZCTreeManager::ClearTree() {
  AssertOnUpdaterThread();

  // Ensure that no references to APZCs are alive in any lingering input
  // blocks. This breaks cycles from InputBlockState::mTargetApzc back to
  // the InputQueue.
  APZThreadUtils::RunOnControllerThread(NewRunnableMethod(
      "layers::InputQueue::Clear", mInputQueue, &InputQueue::Clear));

  RecursiveMutexAutoLock lock(mTreeLock);

  // Collect the nodes into a list, and then destroy each one.
  // We can't destroy them as we collect them, because ForEachNode()
  // does a pre-order traversal of the tree, and Destroy() nulls out
  // the fields needed to reach the children of the node.
  nsTArray<RefPtr<HitTestingTreeNode>> nodesToDestroy;
  ForEachNode<ReverseIterator>(mRootNode.get(),
                               [&nodesToDestroy](HitTestingTreeNode* aNode) {
                                 nodesToDestroy.AppendElement(aNode);
                               });

  for (size_t i = 0; i < nodesToDestroy.Length(); i++) {
    nodesToDestroy[i]->Destroy();
  }
  mRootNode = nullptr;

  {
    // Also remove references to APZC instances in the map
    MutexAutoLock lock(mMapLock);
    mApzcMap.clear();
  }

  RefPtr<APZCTreeManager> self(this);
  NS_DispatchToMainThread(
      NS_NewRunnableFunction("layers::APZCTreeManager::ClearTree", [self] {
        self->mFlushObserver->Unregister();
        self->mFlushObserver = nullptr;
      }));
}

RefPtr<HitTestingTreeNode> APZCTreeManager::GetRootNode() const {
  RecursiveMutexAutoLock lock(mTreeLock);
  return mRootNode;
}

/**
 * Transform a displacement from the ParentLayer coordinates of a source APZC
 * to the ParentLayer coordinates of a target APZC.
 * @param aTreeManager the tree manager for the APZC tree containing |aSource|
 *                     and |aTarget|
 * @param aSource the source APZC
 * @param aTarget the target APZC
 * @param aStartPoint the start point of the displacement
 * @param aEndPoint the end point of the displacement
 * @return true on success, false if aStartPoint or aEndPoint cannot be
 * transformed into target's coordinate space
 */
static bool TransformDisplacement(APZCTreeManager* aTreeManager,
                                  AsyncPanZoomController* aSource,
                                  AsyncPanZoomController* aTarget,
                                  ParentLayerPoint& aStartPoint,
                                  ParentLayerPoint& aEndPoint) {
  if (aSource == aTarget) {
    return true;
  }

  // Convert start and end points to Screen coordinates.
  ParentLayerToScreenMatrix4x4 untransformToApzc =
      aTreeManager->GetScreenToApzcTransform(aSource).Inverse();
  ScreenPoint screenStart = TransformBy(untransformToApzc, aStartPoint);
  ScreenPoint screenEnd = TransformBy(untransformToApzc, aEndPoint);

  // Convert start and end points to aTarget's ParentLayer coordinates.
  ScreenToParentLayerMatrix4x4 transformToApzc =
      aTreeManager->GetScreenToApzcTransform(aTarget);
  Maybe<ParentLayerPoint> startPoint =
      UntransformBy(transformToApzc, screenStart);
  Maybe<ParentLayerPoint> endPoint = UntransformBy(transformToApzc, screenEnd);
  if (!startPoint || !endPoint) {
    return false;
  }
  aEndPoint = *endPoint;
  aStartPoint = *startPoint;

  return true;
}

bool APZCTreeManager::DispatchScroll(
    AsyncPanZoomController* aPrev, ParentLayerPoint& aStartPoint,
    ParentLayerPoint& aEndPoint,
    OverscrollHandoffState& aOverscrollHandoffState) {
  const OverscrollHandoffChain& overscrollHandoffChain =
      aOverscrollHandoffState.mChain;
  uint32_t overscrollHandoffChainIndex = aOverscrollHandoffState.mChainIndex;
  RefPtr<AsyncPanZoomController> next;
  // If we have reached the end of the overscroll handoff chain, there is
  // nothing more to scroll, so we ignore the rest of the pan gesture.
  if (overscrollHandoffChainIndex >= overscrollHandoffChain.Length()) {
    // Nothing more to scroll - ignore the rest of the pan gesture.
    return false;
  }

  next = overscrollHandoffChain.GetApzcAtIndex(overscrollHandoffChainIndex);

  if (next == nullptr || next->IsDestroyed()) {
    return false;
  }

  // Convert the start and end points from |aPrev|'s coordinate space to
  // |next|'s coordinate space.
  if (!TransformDisplacement(this, aPrev, next, aStartPoint, aEndPoint)) {
    return false;
  }

  // Scroll |next|. If this causes overscroll, it will call DispatchScroll()
  // again with an incremented index.
  if (!next->AttemptScroll(aStartPoint, aEndPoint, aOverscrollHandoffState)) {
    // Transform |aStartPoint| and |aEndPoint| (which now represent the
    // portion of the displacement that wasn't consumed by APZCs later
    // in the handoff chain) back into |aPrev|'s coordinate space. This
    // allows the caller (which is |aPrev|) to interpret the unconsumed
    // displacement in its own coordinate space, and make use of it
    // (e.g. by going into overscroll).
    if (!TransformDisplacement(this, next, aPrev, aStartPoint, aEndPoint)) {
      NS_WARNING("Failed to untransform scroll points during dispatch");
    }
    return false;
  }

  // Return true to indicate the scroll was consumed entirely.
  return true;
}

ParentLayerPoint APZCTreeManager::DispatchFling(
    AsyncPanZoomController* aPrev, const FlingHandoffState& aHandoffState) {
  // If immediate handoff is disallowed, do not allow handoff beyond the
  // single APZC that's scrolled by the input block that triggered this fling.
  if (aHandoffState.mIsHandoff && !StaticPrefs::apz_allow_immediate_handoff() &&
      aHandoffState.mScrolledApzc == aPrev) {
    FLING_LOG("APZCTM dropping handoff due to disallowed immediate handoff\n");
    return aHandoffState.mVelocity;
  }

  const OverscrollHandoffChain* chain = aHandoffState.mChain;
  RefPtr<AsyncPanZoomController> current;
  uint32_t overscrollHandoffChainLength = chain->Length();
  uint32_t startIndex;

  // The fling's velocity needs to be transformed from the screen coordinates
  // of |aPrev| to the screen coordinates of |next|. To transform a velocity
  // correctly, we need to convert it to a displacement. For now, we do this
  // by anchoring it to a start point of (0, 0).
  // TODO: For this to be correct in the presence of 3D transforms, we should
  // use the end point of the touch that started the fling as the start point
  // rather than (0, 0).
  ParentLayerPoint startPoint;  // (0, 0)
  ParentLayerPoint endPoint;

  if (aHandoffState.mIsHandoff) {
    startIndex = chain->IndexOf(aPrev) + 1;

    // IndexOf will return aOverscrollHandoffChain->Length() if
    // |aPrev| is not found.
    if (startIndex >= overscrollHandoffChainLength) {
      return aHandoffState.mVelocity;
    }
  } else {
    startIndex = 0;
  }

  // This will store any velocity left over after the entire handoff.
  ParentLayerPoint finalResidualVelocity = aHandoffState.mVelocity;

  ParentLayerPoint currentVelocity = aHandoffState.mVelocity;
  for (; startIndex < overscrollHandoffChainLength; startIndex++) {
    current = chain->GetApzcAtIndex(startIndex);

    // Make sure the apzc about to be handled can be handled
    if (current == nullptr || current->IsDestroyed()) {
      break;
    }

    endPoint = startPoint + currentVelocity;

    RefPtr<AsyncPanZoomController> prevApzc =
        (startIndex > 0) ? chain->GetApzcAtIndex(startIndex - 1) : nullptr;

    // Only transform when current apzc can be transformed with previous
    if (prevApzc) {
      if (!TransformDisplacement(this, prevApzc, current, startPoint,
                                 endPoint)) {
        break;
      }
    }

    ParentLayerPoint availableVelocity = (endPoint - startPoint);
    ParentLayerPoint residualVelocity;

    FlingHandoffState transformedHandoffState = aHandoffState;
    transformedHandoffState.mVelocity = availableVelocity;

    // Obey overscroll-behavior.
    if (prevApzc) {
      residualVelocity += prevApzc->AdjustHandoffVelocityForOverscrollBehavior(
          transformedHandoffState.mVelocity);
    }

    residualVelocity += current->AttemptFling(transformedHandoffState);

    // If there's no residual velocity, there's nothing more to hand off.
    if (current->IsZero(residualVelocity)) {
      return ParentLayerPoint();
    }

    // If any of the velocity available to be handed off was consumed,
    // subtract the proportion of consumed velocity from finalResidualVelocity.
    // Note: it's important to compare |residualVelocity| to |availableVelocity|
    // here and not to |transformedHandoffState.mVelocity|, since the latter
    // may have been modified by AdjustHandoffVelocityForOverscrollBehavior().
    if (!current->IsZero(availableVelocity.x - residualVelocity.x)) {
      finalResidualVelocity.x *= (residualVelocity.x / availableVelocity.x);
    }
    if (!current->IsZero(availableVelocity.y - residualVelocity.y)) {
      finalResidualVelocity.y *= (residualVelocity.y / availableVelocity.y);
    }

    currentVelocity = residualVelocity;
  }

  // Return any residual velocity left over after the entire handoff process.
  return finalResidualVelocity;
}

already_AddRefed<AsyncPanZoomController> APZCTreeManager::GetTargetAPZC(
    const ScrollableLayerGuid& aGuid) {
  RecursiveMutexAutoLock lock(mTreeLock);
  RefPtr<HitTestingTreeNode> node = GetTargetNode(aGuid, nullptr);
  MOZ_ASSERT(!node || node->GetApzc());  // any node returned must have an APZC
  RefPtr<AsyncPanZoomController> apzc = node ? node->GetApzc() : nullptr;
  return apzc.forget();
}

already_AddRefed<AsyncPanZoomController> APZCTreeManager::GetTargetAPZC(
    const LayersId& aLayersId,
    const ScrollableLayerGuid::ViewID& aScrollId) const {
  MutexAutoLock lock(mMapLock);
  return GetTargetAPZC(aLayersId, aScrollId, lock);
}

already_AddRefed<AsyncPanZoomController> APZCTreeManager::GetTargetAPZC(
    const LayersId& aLayersId, const ScrollableLayerGuid::ViewID& aScrollId,
    const MutexAutoLock& aProofOfMapLock) const {
  mMapLock.AssertCurrentThreadOwns();
  ScrollableLayerGuid guid(aLayersId, 0, aScrollId);
  auto it = mApzcMap.find(guid);
  RefPtr<AsyncPanZoomController> apzc =
      (it != mApzcMap.end() ? it->second.apzc : nullptr);
  return apzc.forget();
}

already_AddRefed<HitTestingTreeNode> APZCTreeManager::GetTargetNode(
    const ScrollableLayerGuid& aGuid, GuidComparator aComparator) const {
  mTreeLock.AssertCurrentThreadIn();
  RefPtr<HitTestingTreeNode> target =
      DepthFirstSearchPostOrder<ReverseIterator>(
          mRootNode.get(), [&aGuid, &aComparator](HitTestingTreeNode* node) {
            bool matches = false;
            if (node->GetApzc()) {
              if (aComparator) {
                matches = aComparator(aGuid, node->GetApzc()->GetGuid());
              } else {
                matches = node->GetApzc()->Matches(aGuid);
              }
            }
            return matches;
          });
  return target.forget();
}

APZCTreeManager::HitTestResult APZCTreeManager::GetTargetAPZC(
    const ScreenPoint& aPoint) {
  RecursiveMutexAutoLock lock(mTreeLock);
  MOZ_ASSERT(mHitTester);
  return mHitTester->GetAPZCAtPoint(aPoint, lock);
}

APZCTreeManager::HitTestResult APZCTreeManager::GetTargetAPZCForMouseInput(
    const MouseInput& aMouseInput) {
  // If the mouse input isn't move or we are in a wheel transaction,
  // we need to do hit testing anyway.
  if (!StaticPrefs::apz_mousemove_hittest_optimization_enabled() ||
      aMouseInput.mType != MouseInput::MOUSE_MOVE ||
      mInputQueue->GetActiveWheelTransaction()) {
    return GetTargetAPZC(aMouseInput.mOrigin);
  }

  // If we are in dragging state, reuse the original target without
  // hit-testing again.
  if (mDragBlockHitResult.mTargetApzc) {
    RecursiveMutexAutoLock lock(mTreeLock);
    return mHitTester->CloneHitTestResult(lock, mDragBlockHitResult);
  }

  // If we have no OOP iframes, we don't need to do hit-testing again since
  // the layersId will never be changed.
  RecursiveMutexAutoLock lock(mTreeLock);
  if (!mHaveOOPIframes) {
    return HitTestResult{};
  }
  return GetTargetAPZC(aMouseInput.mOrigin);
}

APZCTreeManager::TargetApzcForNodeResult APZCTreeManager::FindHandoffParent(
    const AsyncPanZoomController* aApzc) {
  RefPtr<HitTestingTreeNode> node = GetTargetNode(aApzc->GetGuid(), nullptr);
  while (node) {
    auto result = GetTargetApzcForNode(node->GetParent());
    if (result.mApzc) {
      // avoid infinite recursion in the overscroll handoff chain.
      if (result.mApzc != aApzc) {
        return result;
      }
    }
    node = node->GetParent();
  }

  return {nullptr, false};
}

RefPtr<const OverscrollHandoffChain>
APZCTreeManager::BuildOverscrollHandoffChain(
    const RefPtr<AsyncPanZoomController>& aInitialTarget) {
  // Scroll grabbing is a mechanism that allows content to specify that
  // the initial target of a pan should be not the innermost scrollable
  // frame at the touch point (which is what GetTargetAPZC finds), but
  // something higher up in the tree.
  // It's not sufficient to just find the initial target, however, as
  // overscroll can be handed off to another APZC. Without scroll grabbing,
  // handoff just occurs from child to parent. With scroll grabbing, the
  // handoff order can be different, so we build a chain of APZCs in the
  // order in which scroll will be handed off to them.

  // Grab tree lock since we'll be walking the APZC tree.
  RecursiveMutexAutoLock lock(mTreeLock);

  // Build the chain. If there is a scroll parent link, we use that. This is
  // needed to deal with scroll info layers, because they participate in handoff
  // but do not follow the expected layer tree structure. If there are no
  // scroll parent links we just walk up the tree to find the scroll parent.
  OverscrollHandoffChain* result = new OverscrollHandoffChain;
  AsyncPanZoomController* apzc = aInitialTarget;
  while (apzc != nullptr) {
    result->Add(apzc);

    APZCTreeManager::TargetApzcForNodeResult handoffResult =
        FindHandoffParent(apzc);

    if (!handoffResult.mIsFixed && !apzc->IsRootForLayersId() &&
        apzc->GetScrollHandoffParentId() ==
            ScrollableLayerGuid::NULL_SCROLL_ID) {
      // This probably indicates a bug or missed case in layout code
      NS_WARNING("Found a non-root APZ with no handoff parent");
    }

    // If `apzc` is inside fixed content, we want to hand off to the document's
    // root APZC next. The scroll parent id wouldn't give us this because it's
    // based on ASRs.
    if (handoffResult.mIsFixed || apzc->GetScrollHandoffParentId() ==
                                      ScrollableLayerGuid::NULL_SCROLL_ID) {
      apzc = handoffResult.mApzc;
      continue;
    }

    // Guard against a possible infinite-loop condition. If we hit this, the
    // layout code that generates the handoff parents did something wrong.
    MOZ_ASSERT(apzc->GetScrollHandoffParentId() != apzc->GetGuid().mScrollId);
    RefPtr<AsyncPanZoomController> scrollParent = GetTargetAPZC(
        apzc->GetGuid().mLayersId, apzc->GetScrollHandoffParentId());
    apzc = scrollParent.get();
  }

  // Print the overscroll chain for debugging.
  for (uint32_t i = 0; i < result->Length(); ++i) {
    APZCTM_LOG("OverscrollHandoffChain[%d] = %p\n", i,
               result->GetApzcAtIndex(i).get());
  }

  return result;
}

void APZCTreeManager::SetLongTapEnabled(bool aLongTapEnabled) {
  if (!APZThreadUtils::IsControllerThread()) {
    APZThreadUtils::RunOnControllerThread(NewRunnableMethod<bool>(
        "layers::APZCTreeManager::SetLongTapEnabled", this,
        &APZCTreeManager::SetLongTapEnabled, aLongTapEnabled));
    return;
  }

  APZThreadUtils::AssertOnControllerThread();
  GestureEventListener::SetLongTapEnabled(aLongTapEnabled);
}

void APZCTreeManager::AddInputBlockCallback(uint64_t aInputBlockId,
                                            InputBlockCallback&& aCallback) {
  APZThreadUtils::AssertOnControllerThread();
  mInputQueue->AddInputBlockCallback(aInputBlockId, std::move(aCallback));
}

void APZCTreeManager::FindScrollThumbNode(
    const AsyncDragMetrics& aDragMetrics, LayersId aLayersId,
    HitTestingTreeNodeAutoLock& aOutThumbNode) {
  if (!aDragMetrics.mDirection) {
    // The AsyncDragMetrics has not been initialized yet - there will be
    // no matching node, so don't bother searching the tree.
    return;
  }

  RecursiveMutexAutoLock lock(mTreeLock);

  RefPtr<HitTestingTreeNode> result = DepthFirstSearch<ReverseIterator>(
      mRootNode.get(), [&aDragMetrics, &aLayersId](HitTestingTreeNode* aNode) {
        return aNode->MatchesScrollDragMetrics(aDragMetrics, aLayersId);
      });
  if (result) {
    aOutThumbNode.Initialize(lock, result.forget(), mTreeLock);
  }
}

APZCTreeManager::TargetApzcForNodeResult APZCTreeManager::GetTargetApzcForNode(
    const HitTestingTreeNode* aNode) {
  for (const HitTestingTreeNode* n = aNode;
       n && n->GetLayersId() == aNode->GetLayersId(); n = n->GetParent()) {
    // For a fixed node, GetApzc() may return an APZC for content in the
    // enclosing document, so we need to check GetFixedPosTarget() before
    // GetApzc().
    if (n->GetFixedPosTarget() != ScrollableLayerGuid::NULL_SCROLL_ID) {
      RefPtr<AsyncPanZoomController> fpTarget =
          GetTargetAPZC(n->GetLayersId(), n->GetFixedPosTarget());
      APZCTM_LOG("Found target APZC %p using fixed-pos lookup on %" PRIu64 "\n",
                 fpTarget.get(), n->GetFixedPosTarget());
      return {fpTarget.get(), true};
    }
    if (n->GetApzc()) {
      APZCTM_LOG("Found target %p using ancestor lookup\n", n->GetApzc());
      return {n->GetApzc(), false};
    }
  }
  return {nullptr, false};
}

HitTestingTreeNode* APZCTreeManager::FindRootNodeForLayersId(
    LayersId aLayersId) const {
  mTreeLock.AssertCurrentThreadIn();

  HitTestingTreeNode* resultNode = BreadthFirstSearch<ReverseIterator>(
      mRootNode.get(), [aLayersId](HitTestingTreeNode* aNode) {
        AsyncPanZoomController* apzc = aNode->GetApzc();
        return apzc && apzc->GetLayersId() == aLayersId &&
               apzc->IsRootForLayersId();
      });
  return resultNode;
}

already_AddRefed<AsyncPanZoomController> APZCTreeManager::FindZoomableApzc(
    AsyncPanZoomController* aStart) const {
  return GetZoomableTarget(aStart, aStart);
}

ScreenMargin APZCTreeManager::GetCompositorFixedLayerMargins() const {
  MutexAutoLock lock(mMapLock);
  return GetCompositorFixedLayerMargins(lock);
}

AsyncPanZoomController* APZCTreeManager::FindRootApzcFor(
    LayersId aLayersId) const {
  RecursiveMutexAutoLock lock(mTreeLock);

  HitTestingTreeNode* resultNode = FindRootNodeForLayersId(aLayersId);
  return resultNode ? resultNode->GetApzc() : nullptr;
}

AsyncPanZoomController* APZCTreeManager::FindRootContentApzcForLayersId(
    LayersId aLayersId) const {
  mTreeLock.AssertCurrentThreadIn();

  HitTestingTreeNode* resultNode = BreadthFirstSearch<ReverseIterator>(
      mRootNode.get(), [aLayersId](HitTestingTreeNode* aNode) {
        AsyncPanZoomController* apzc = aNode->GetApzc();
        return apzc && apzc->GetLayersId() == aLayersId &&
               apzc->IsRootContent();
      });
  return resultNode ? resultNode->GetApzc() : nullptr;
}

// clang-format off
/* The methods GetScreenToApzcTransform() and GetApzcToGeckoTransform() return
   some useful transformations that input events may need applied. This is best
   illustrated with an example. Consider a chain of layers, L, M, N, O, P, Q, R. Layer L
   is the layer that corresponds to the argument |aApzc|, and layer R is the root
   of the layer tree. Layer M is the parent of L, N is the parent of M, and so on.
   When layer L is displayed to the screen by the compositor, the set of transforms that
   are applied to L are (in order from top to bottom):

        L's CSS transform                   (hereafter referred to as transform matrix LC)
        L's async transform                 (hereafter referred to as transform matrix LA)
        M's CSS transform                   (hereafter referred to as transform matrix MC)
        M's async transform                 (hereafter referred to as transform matrix MA)
        ...
        R's CSS transform                   (hereafter referred to as transform matrix RC)
        R's async transform                 (hereafter referred to as transform matrix RA)

   If we want user input to modify L's async transform, we have to first convert
   user input from screen space to the coordinate space of L's async transform. Doing
   this involves applying the following transforms (in order from top to bottom):
        RA.Inverse()
        RC.Inverse()
        ...
        MA.Inverse()
        MC.Inverse()
   This combined transformation is returned by GetScreenToApzcTransform().

   Next, if we want user inputs sent to gecko for event-dispatching, we will need to strip
   out all of the async transforms that are involved in this chain. This is because async
   transforms are stored only in the compositor and gecko does not account for them when
   doing display-list-based hit-testing for event dispatching.
   Furthermore, because these input events are processed by Gecko in a FIFO queue that
   includes other things (specifically paint requests), it is possible that by time the
   input event reaches gecko, it will have painted something else. Therefore, we need to
   apply another transform to the input events to account for the possible disparity between
   what we know gecko last painted and the last paint request we sent to gecko. Let this
   transform be represented by LD, MD, ... RD.
   Therefore, given a user input in screen space, the following transforms need to be applied
   (in order from top to bottom):
        RT.Inverse()
        RN.Inverse()
        RC.Inverse()
        ...
        MT.Inverse()
        MN.Inverse()
        MC.Inverse()
        LT.Inverse()
        LN.Inverse()
        LC.Inverse()
        LC
        LD
        MC
        MD
        ...
        RC
        RD
   This sequence can be simplified and refactored to the following:
        GetScreenToApzcTransform()
        LA.Inverse()
        LD
        MC
        MD
        ...
        RC
        RD
   Since GetScreenToApzcTransform() can be obtained by calling that function, GetApzcToGeckoTransform()
   returns the remaining transforms (LA.Inverse() * LD * ... * RD), so that the caller code can
   combine it with GetScreenToApzcTransform() to get the final transform required in this case.

   Note that for many of these layers, there will be no AsyncPanZoomController attached, and
   so the async transform will be the identity transform. So, in the example above, if layers
   L and P have APZC instances attached, MT, MN, MD, NT, NN, ND, OT, ON, OD, QT, QN, QD, RT,
   RN and RD will be identity transforms.
   Additionally, for space-saving purposes, each APZC instance stores its layer's individual
   CSS transform and the accumulation of CSS transforms to its parent APZC. So the APZC for
   layer L would store LC and (MC * NC * OC), and the layer P would store PC and (QC * RC).
   The APZC instances track the last dispatched paint request and so are able to calculate LD and
   PD using those internally stored values.
   The APZCs also obviously have LT, LN, PT, and PN, so all of the above transformation combinations
   required can be generated.
 */
// clang-format on

/*
 * See the long comment above for a detailed explanation of this function.
 */
ScreenToParentLayerMatrix4x4 APZCTreeManager::GetScreenToApzcTransform(
    const AsyncPanZoomController* aApzc) const {
  Matrix4x4 result;
  RecursiveMutexAutoLock lock(mTreeLock);

  // The comments below assume there is a chain of layers L..R with L and P
  // having APZC instances as explained in the comment above. This function is
  // called with aApzc at L, and the loop below performs one iteration, where
  // parent is at P. The comments explain what values are stored in the
  // variables at these two levels. All the comments use standard matrix
  // notation where the leftmost matrix in a multiplication is applied first.

  // ancestorUntransform is PC.Inverse() * OC.Inverse() * NC.Inverse() *
  // MC.Inverse()
  Matrix4x4 ancestorUntransform = aApzc->GetAncestorTransform().Inverse();

  // result is initialized to PC.Inverse() * OC.Inverse() * NC.Inverse() *
  // MC.Inverse()
  result = ancestorUntransform;

  for (AsyncPanZoomController* parent = aApzc->GetParent(); parent;
       parent = parent->GetParent()) {
    // ancestorUntransform is updated to RC.Inverse() * QC.Inverse() when parent
    // == P
    ancestorUntransform = parent->GetAncestorTransform().Inverse();
    // asyncUntransform is updated to PA.Inverse() when parent == P
    Matrix4x4 asyncUntransform = parent
                                     ->GetAsyncTransformForInputTransformation(
                                         LayoutAndVisual, aApzc->GetLayersId())
                                     .Inverse()
                                     .ToUnknownMatrix();
    // untransformSinceLastApzc is RC.Inverse() * QC.Inverse() * PA.Inverse()
    Matrix4x4 untransformSinceLastApzc = ancestorUntransform * asyncUntransform;

    // result is RC.Inverse() * QC.Inverse() * PA.Inverse() * PC.Inverse() *
    // OC.Inverse() * NC.Inverse() * MC.Inverse()
    result = untransformSinceLastApzc * result;

    // The above value for result when parent == P matches the required output
    // as explained in the comment above this method. Note that any missing
    // terms are guaranteed to be identity transforms.
  }

  return ViewAs<ScreenToParentLayerMatrix4x4>(result);
}

/*
 * See the long comment above GetScreenToApzcTransform() for a detailed
 * explanation of this function.
 */
ParentLayerToParentLayerMatrix4x4 APZCTreeManager::GetApzcToApzcTransform(
    const AsyncPanZoomController* aStartApzc,
    const AsyncPanZoomController* aStopApzc,
    const AsyncTransformComponents& aComponents) const {
  Matrix4x4 result;
  RecursiveMutexAutoLock lock(mTreeLock);

  // The comments below assume there is a chain of layers L..R with L and P
  // having APZC instances as explained in the comment above, and |aStopApzc| is
  // nullptr. This function is called with aStartApzc at L, and the loop below
  // performs one iteration, where parent is at P. The comments explain what
  // values are stored in the variables at these two levels. All the comments
  // use standard matrix notation where the leftmost matrix in a multiplication
  // is applied first.

  // asyncUntransform is LA.Inverse()
  Matrix4x4 asyncUntransform = aStartApzc
                                   ->GetAsyncTransformForInputTransformation(
                                       aComponents, aStartApzc->GetLayersId())
                                   .Inverse()
                                   .ToUnknownMatrix();

  // result is initialized to LA.Inverse() * LD * MC * NC * OC * PC
  result = asyncUntransform *
           aStartApzc->GetTransformToLastDispatchedPaint(
               aComponents, aStartApzc->GetLayersId()) *
           aStartApzc->GetAncestorTransform();

  for (AsyncPanZoomController* parent = aStartApzc->GetParent();
       parent && parent != aStopApzc; parent = parent->GetParent()) {
    // result is LA.Inverse() * LD * MC * NC * OC * PC * PD * QC * RC
    //
    // Note: Do not pass the async transform components for the current target
    // to the parent.
    result = result *
             parent->GetTransformToLastDispatchedPaint(
                 LayoutAndVisual, aStartApzc->GetLayersId()) *
             parent->GetAncestorTransform();

    // The above value for result when parent == P matches the required output
    // as explained in the comment above this method. Note that any missing
    // terms are guaranteed to be identity transforms.
  }

  return ViewAs<ParentLayerToParentLayerMatrix4x4>(result);
}

ParentLayerToScreenMatrix4x4 APZCTreeManager::GetApzcToGeckoTransform(
    const AsyncPanZoomController* aApzc,
    const AsyncTransformComponents& aComponents) const {
  return ViewAs<ParentLayerToScreenMatrix4x4>(
      GetApzcToApzcTransform(aApzc, nullptr, aComponents),
      PixelCastJustification::ScreenIsParentLayerForRoot);
}

ParentLayerToScreenMatrix4x4 APZCTreeManager::GetApzcToGeckoTransformForHit(
    HitTestResult& aHitResult) const {
  // Fixed content is only subject to the visual component of the async
  // transform.
  AsyncTransformComponents components =
      aHitResult.mFixedPosSides == SideBits::eNone
          ? LayoutAndVisual
          : AsyncTransformComponents{AsyncTransformComponent::eVisual};
  return GetApzcToGeckoTransform(aHitResult.mTargetApzc, components);
}

CSSToCSSMatrix4x4 APZCTreeManager::GetOopifToRootContentTransform(
    AsyncPanZoomController* aApzc) const {
  MOZ_ASSERT(aApzc->IsRootForLayersId());

  RefPtr<AsyncPanZoomController> rootContentApzc = FindZoomableApzc(aApzc);
  MOZ_ASSERT(aApzc->GetLayersId() != rootContentApzc->GetLayersId(),
             "aApzc must be out-of-process of the rootContentApzc");
  if (!rootContentApzc || rootContentApzc == aApzc ||
      rootContentApzc->GetLayersId() == aApzc->GetLayersId()) {
    return CSSToCSSMatrix4x4();
  }
  ParentLayerToParentLayerMatrix4x4 result =
      GetApzcToApzcTransform(aApzc, rootContentApzc,
                             AsyncTransformComponent::eLayout) *
      // We need to multiply by the root content APZC's
      // GetPaintedResolutionTransform() here; See
      // https://phabricator.services.mozilla.com/D184440?vs=755900&id=757585#6173584
      // for the details.
      ViewAs<AsyncTransformComponentMatrix>(
          rootContentApzc->GetPaintedResolutionTransform());

  CSSToParentLayerScale rootZoom = rootContentApzc->GetZoom();

  if (rootZoom == CSSToParentLayerScale(0)) {
    rootZoom = CSSToParentLayerScale(1.0f);
  }

  CSSPoint rootScrollPosition = rootContentApzc->GetLayoutScrollOffset();

  return result.PreScale(aApzc->GetZoom())
      .PostScale(rootZoom.Inverse())
      .PostTranslate(rootScrollPosition);
}

CSSRect APZCTreeManager::ConvertRectInApzcToRoot(AsyncPanZoomController* aApzc,
                                                 const CSSRect& aRect) const {
  MOZ_ASSERT(aApzc->IsRootForLayersId());
  RefPtr<AsyncPanZoomController> rootContentApzc = FindZoomableApzc(aApzc);
  if (!rootContentApzc) {
    return aRect;
  }

  if (rootContentApzc == aApzc) {
    return aRect + rootContentApzc->GetLayoutScrollOffset();
  }

  return GetOopifToRootContentTransform(aApzc).TransformBounds(aRect);
}

ScreenPoint APZCTreeManager::GetCurrentMousePosition() const {
  auto pos = mCurrentMousePosition.Lock();
  return pos.ref();
}

void APZCTreeManager::SetCurrentMousePosition(const ScreenPoint& aNewPos) {
  auto pos = mCurrentMousePosition.Lock();
  pos.ref() = aNewPos;
}

static AsyncPanZoomController* GetApzcWithDifferentLayersIdByWalkingParents(
    AsyncPanZoomController* aApzc) {
  if (!aApzc) {
    return nullptr;
  }
  AsyncPanZoomController* parent = aApzc->GetParent();
  while (parent && (parent->GetLayersId() == aApzc->GetLayersId())) {
    parent = parent->GetParent();
  }
  return parent;
}

already_AddRefed<AsyncPanZoomController> APZCTreeManager::GetZoomableTarget(
    AsyncPanZoomController* aApzc1, AsyncPanZoomController* aApzc2) const {
  RecursiveMutexAutoLock lock(mTreeLock);
  RefPtr<AsyncPanZoomController> apzc;
  // For now, we only ever want to do pinching on the root-content APZC for
  // a given layers id.
  if (aApzc1 && aApzc2 && aApzc1->GetLayersId() == aApzc2->GetLayersId()) {
    // If the two APZCs have the same layers id, find the root-content APZC
    // for that layers id. Don't call CommonAncestor() because there may not
    // be a common ancestor for the layers id (e.g. if one APZCs is inside a
    // fixed-position element).
    apzc = FindRootContentApzcForLayersId(aApzc1->GetLayersId());
    if (apzc) {
      return apzc.forget();
    }
  }

  // Otherwise, find the common ancestor (to reach a common layers id), and then
  // walk up the apzc tree until we find a root-content APZC.
  apzc = CommonAncestor(aApzc1, aApzc2);
  RefPtr<AsyncPanZoomController> zoomable;
  while (apzc && !zoomable) {
    zoomable = FindRootContentApzcForLayersId(apzc->GetLayersId());
    apzc = GetApzcWithDifferentLayersIdByWalkingParents(apzc);
  }

  return zoomable.forget();
}

Maybe<ScreenIntPoint> APZCTreeManager::ConvertToGecko(
    const ScreenIntPoint& aPoint, AsyncPanZoomController* aApzc) {
  RecursiveMutexAutoLock lock(mTreeLock);
  // TODO: The current check assumes that a touch gesture and a touchpad tap
  // gesture can't both be active at the same time. If we turn on double-tap-
  // to-zoom on a touchscreen platform like Windows or Linux, this assumption
  // would no longer be valid, and we'd have to instead have TapGestureInput
  // track and inform this function whether it was created from touch events.
  const HitTestResult& hit = mInputQueue->GetCurrentTouchBlock()
                                 ? mTouchBlockHitResult
                                 : mTapGestureHitResult;
  AsyncTransformComponents components =
      hit.mFixedPosSides == SideBits::eNone
          ? LayoutAndVisual
          : AsyncTransformComponents{AsyncTransformComponent::eVisual};
  ScreenToScreenMatrix4x4 transformScreenToGecko =
      GetScreenToApzcTransform(aApzc) *
      GetApzcToGeckoTransform(aApzc, components);
  Maybe<ScreenIntPoint> geckoPoint =
      UntransformBy(transformScreenToGecko, aPoint);
  if (geckoPoint) {
    AdjustEventPointForDynamicToolbar(*geckoPoint, hit);
  }
  return geckoPoint;
}

already_AddRefed<AsyncPanZoomController> APZCTreeManager::CommonAncestor(
    AsyncPanZoomController* aApzc1, AsyncPanZoomController* aApzc2) const {
  mTreeLock.AssertCurrentThreadIn();
  RefPtr<AsyncPanZoomController> ancestor;

  // If either aApzc1 or aApzc2 is null, min(depth1, depth2) will be 0 and this
  // function will return null.

  // Calculate depth of the APZCs in the tree
  int depth1 = 0, depth2 = 0;
  for (AsyncPanZoomController* parent = aApzc1; parent;
       parent = parent->GetParent()) {
    depth1++;
  }
  for (AsyncPanZoomController* parent = aApzc2; parent;
       parent = parent->GetParent()) {
    depth2++;
  }

  // At most one of the following two loops will be executed; the deeper APZC
  // pointer will get walked up to the depth of the shallower one.
  int minDepth = depth1 < depth2 ? depth1 : depth2;
  while (depth1 > minDepth) {
    depth1--;
    aApzc1 = aApzc1->GetParent();
  }
  while (depth2 > minDepth) {
    depth2--;
    aApzc2 = aApzc2->GetParent();
  }

  // Walk up the ancestor chains of both APZCs, always staying at the same depth
  // for either APZC, and return the the first common ancestor encountered.
  while (true) {
    if (aApzc1 == aApzc2) {
      ancestor = aApzc1;
      break;
    }
    if (depth1 <= 0) {
      break;
    }
    aApzc1 = aApzc1->GetParent();
    aApzc2 = aApzc2->GetParent();
  }
  return ancestor.forget();
}

bool APZCTreeManager::IsFixedToRootContent(
    const FixedPositionInfo& aFixedInfo,
    const MutexAutoLock& aProofOfMapLock) const {
  ScrollableLayerGuid::ViewID fixedTarget = aFixedInfo.mFixedPosTarget;
  if (fixedTarget == ScrollableLayerGuid::NULL_SCROLL_ID) {
    return false;
  }
  auto it =
      mApzcMap.find(ScrollableLayerGuid(aFixedInfo.mLayersId, 0, fixedTarget));
  if (it == mApzcMap.end()) {
    return false;
  }
  RefPtr<AsyncPanZoomController> targetApzc = it->second.apzc;
  return targetApzc && targetApzc->IsRootContent();
}

SideBits APZCTreeManager::SidesStuckToRootContent(
    const HitTestingTreeNode* aNode, AsyncTransformConsumer aMode) const {
  MutexAutoLock lock(mMapLock);
  return SidesStuckToRootContent(StickyPositionInfo(aNode), aMode, lock);
}

SideBits APZCTreeManager::SidesStuckToRootContent(
    const StickyPositionInfo& aStickyInfo, AsyncTransformConsumer aMode,
    const MutexAutoLock& aProofOfMapLock) const {
  SideBits result = SideBits::eNone;

  ScrollableLayerGuid::ViewID stickyTarget = aStickyInfo.mStickyPosTarget;
  if (stickyTarget == ScrollableLayerGuid::NULL_SCROLL_ID) {
    return result;
  }

  // We support the dynamic toolbar at top and bottom.
  if ((aStickyInfo.mFixedPosSides & SideBits::eTopBottom) == SideBits::eNone) {
    return result;
  }

  auto it = mApzcMap.find(
      ScrollableLayerGuid(aStickyInfo.mLayersId, 0, stickyTarget));
  if (it == mApzcMap.end()) {
    return result;
  }
  RefPtr<AsyncPanZoomController> stickyTargetApzc = it->second.apzc;
  if (!stickyTargetApzc || !stickyTargetApzc->IsRootContent()) {
    return result;
  }

  ParentLayerPoint translation =
      stickyTargetApzc
          ->GetCurrentAsyncTransform(
              aMode, AsyncTransformComponents{AsyncTransformComponent::eLayout})
          .mTranslation;

  if (apz::IsStuckAtTop(translation.y, aStickyInfo.mStickyScrollRangeInner,
                        aStickyInfo.mStickyScrollRangeOuter)) {
    result |= SideBits::eTop;
  }
  if (apz::IsStuckAtBottom(translation.y, aStickyInfo.mStickyScrollRangeInner,
                           aStickyInfo.mStickyScrollRangeOuter)) {
    result |= SideBits::eBottom;
  }
  return result;
}

LayerToParentLayerMatrix4x4 APZCTreeManager::ComputeTransformForScrollThumbNode(
    const HitTestingTreeNode* aNode) const {
  mTreeLock.AssertCurrentThreadIn();
  // The async transform applied here is for hit-testing purposes, and is
  // intended to match the one we tell WebRender to apply in
  // SampleForWebRender for rendering purposes.
  MOZ_ASSERT(aNode->IsScrollThumbNode());
  // If the scrollable element scrolled by the thumb is layerized, compute and
  // apply the transformation that will be applied to the thumb in
  // AsyncCompositionManager.
  ScrollableLayerGuid guid{aNode->GetLayersId(), 0, aNode->GetScrollTargetId()};
  if (RefPtr<HitTestingTreeNode> scrollTargetNode =
          GetTargetNode(guid, &ScrollableLayerGuid::EqualsIgnoringPresShell)) {
    AsyncPanZoomController* scrollTargetApzc = scrollTargetNode->GetApzc();
    MOZ_ASSERT(scrollTargetApzc);
    return scrollTargetApzc->CallWithLastContentPaintMetrics(
        [&](const FrameMetrics& aMetrics) {
          return ComputeTransformForScrollThumb(
              aNode->GetTransform() * AsyncTransformMatrix(),
              scrollTargetNode->GetTransform().ToUnknownMatrix(),
              scrollTargetApzc, aMetrics, aNode->GetScrollbarData(),
              scrollTargetNode->IsAncestorOf(aNode));
        });
  }
  // Otherwise, the node does not have an async transform.
  return aNode->GetTransform() * AsyncTransformMatrix();
}

already_AddRefed<wr::WebRenderAPI> APZCTreeManager::GetWebRenderAPI() const {
  RefPtr<wr::WebRenderAPI> api;
  CompositorBridgeParent::CallWithIndirectShadowTree(
      mRootLayersId, [&](LayerTreeState& aState) -> void {
        if (aState.mWrBridge) {
          api = aState.mWrBridge->GetWebRenderAPI();
        }
      });
  return api.forget();
}

/*static*/
already_AddRefed<GeckoContentController> APZCTreeManager::GetContentController(
    LayersId aLayersId) {
  RefPtr<GeckoContentController> controller;
  CompositorBridgeParent::CallWithIndirectShadowTree(
      aLayersId,
      [&](LayerTreeState& aState) -> void { controller = aState.mController; });
  return controller.forget();
}

ScreenMargin APZCTreeManager::GetCompositorFixedLayerMargins(
    const MutexAutoLock& aProofOfMapLock) const {
  ScreenMargin result = mCompositorFixedLayerMargins;
  if (StaticPrefs::apz_fixed_margin_override_enabled()) {
    result.top = StaticPrefs::apz_fixed_margin_override_top();
    result.bottom = StaticPrefs::apz_fixed_margin_override_bottom();
  }
  return result;
}

bool APZCTreeManager::GetAPZTestData(LayersId aLayersId,
                                     APZTestData* aOutData) {
  AssertOnUpdaterThread();

  {  // copy the relevant test data into aOutData while holding the
     // mTestDataLock
    MutexAutoLock lock(mTestDataLock);
    auto it = mTestData.find(aLayersId);
    if (it == mTestData.end()) {
      return false;
    }
    *aOutData = *(it->second);
  }

  {  // add some additional "current state" into the returned APZTestData
    MutexAutoLock mapLock(mMapLock);

    ClippedCompositionBoundsMap clippedCompBounds;
    for (const auto& mapping : mApzcMap) {
      if (mapping.first.mLayersId != aLayersId) {
        continue;
      }

      ParentLayerRect clippedBounds = ComputeClippedCompositionBounds(
          mapLock, clippedCompBounds, mapping.first);
      AsyncPanZoomController* apzc = mapping.second.apzc;
      std::string viewId = std::to_string(mapping.first.mScrollId);
      std::string apzcState;
      if (apzc->GetCheckerboardMagnitude(clippedBounds)) {
        apzcState += "checkerboarding,";
      }
      if (apzc->IsOverscrolled()) {
        apzcState += "overscrolled,";
      }
      aOutData->RecordAdditionalData(viewId, apzcState);
    }
  }
  return true;
}

void APZCTreeManager::SendSubtreeTransformsToChromeMainThread(
    const AsyncPanZoomController* aAncestor) {
  RefPtr<GeckoContentController> controller =
      GetContentController(mRootLayersId);
  if (!controller) {
    return;
  }
  nsTArray<MatrixMessage> messages;
  bool underAncestor = (aAncestor == nullptr);
  bool shouldNotify = false;
  {
    RecursiveMutexAutoLock lock(mTreeLock);
    if (!mRootNode) {
      // Event dispatched during shutdown, after ClearTree().
      // Note, mRootNode needs to be checked with mTreeLock held.
      return;
    }
    // This formulation duplicates matrix multiplications closer
    // to the root of the tree. For now, aiming for separation
    // of concerns rather than minimum number of multiplications.
    ForEachNode<ReverseIterator>(
        mRootNode.get(),
        [&](HitTestingTreeNode* aNode) {
          mTreeLock.AssertCurrentThreadIn();
          bool atAncestor = (aAncestor && aNode->GetApzc() == aAncestor);
          MOZ_ASSERT(!(underAncestor && atAncestor));
          underAncestor |= atAncestor;
          if (!underAncestor) {
            return;
          }
          LayersId layersId = aNode->GetLayersId();
          HitTestingTreeNode* parent = aNode->GetParent();
          if (!parent) {
            messages.AppendElement(MatrixMessage(Some(LayerToScreenMatrix4x4()),
                                                 ScreenRect(), layersId));
          } else if (layersId != parent->GetLayersId()) {
            if (mDetachedLayersIds.find(layersId) != mDetachedLayersIds.end()) {
              messages.AppendElement(
                  MatrixMessage(Nothing(), ScreenRect(), layersId));
            } else {
              // It's important to pass aRemoteLayersId=layersId here (not
              // parent->GetLayersId()). layersId is the LayersId of the
              // remote content for which this transform is being computed.
              messages.AppendElement(MatrixMessage(
                  Some(parent->GetTransformToGecko(layersId)),
                  parent->GetRemoteDocumentScreenRect(layersId), layersId));
            }
          }
        },
        [&](HitTestingTreeNode* aNode) {
          bool atAncestor = (aAncestor && aNode->GetApzc() == aAncestor);
          if (atAncestor) {
            MOZ_ASSERT(underAncestor);
            underAncestor = false;
          }
        });
    if (messages != mLastMessages) {
      mLastMessages = messages;
      shouldNotify = true;
    }
  }
  if (shouldNotify) {
    controller->NotifyLayerTransforms(std::move(messages));
  }
}

void APZCTreeManager::SetFixedLayerMargins(ScreenIntCoord aTop,
                                           ScreenIntCoord aBottom) {
  MutexAutoLock lock(mMapLock);
  mCompositorFixedLayerMargins.top = ScreenCoord(aTop);
  mCompositorFixedLayerMargins.bottom = ScreenCoord(aBottom);
}

ScreenPoint APZCTreeManager::ComputeFixedMarginsOffset(
    const MutexAutoLock& aProofOfMapLock, SideBits aFixedSides,
    const ScreenMargin& aGeckoFixedLayerMargins) const {
  // If the software keyboard is visible and the interactive-widget is not
  // resizes-content, we don't need to move the position:fixed or sticky
  // elements at all.
  if (IsSoftwareKeyboardVisible(aProofOfMapLock) &&
      InteractiveWidgetMode(aProofOfMapLock) !=
          dom::InteractiveWidget::ResizesContent) {
    return ScreenPoint(0, 0);
  }

  // Work out the necessary translation, in screen space.
  ScreenPoint translation;

  ScreenMargin effectiveMargin =
      GetCompositorFixedLayerMargins(aProofOfMapLock) - aGeckoFixedLayerMargins;
  if (aFixedSides & SideBits::eLeft) {
    translation.x += effectiveMargin.left;
  } else if (aFixedSides & SideBits::eRight) {
    translation.x -= effectiveMargin.right;
  }

  if (aFixedSides & SideBits::eTop) {
    translation.y += effectiveMargin.top;
  } else if (aFixedSides & SideBits::eBottom) {
    translation.y -= effectiveMargin.bottom;
  }

  return translation;
}

/*static*/
LayerToParentLayerMatrix4x4 APZCTreeManager::ComputeTransformForScrollThumb(
    const LayerToParentLayerMatrix4x4& aCurrentTransform,
    const Matrix4x4& aScrollableContentTransform, AsyncPanZoomController* aApzc,
    const FrameMetrics& aMetrics, const ScrollbarData& aScrollbarData,
    bool aScrollbarIsDescendant) {
  return apz::ComputeTransformForScrollThumb(
      aCurrentTransform, aScrollableContentTransform, aApzc, aMetrics,
      aScrollbarData, aScrollbarIsDescendant);
}

APZSampler* APZCTreeManager::GetSampler() const {
  // We should always have a sampler here, since in practice the sampler
  // is destroyed at the same time that this APZCTreeMAnager instance is.
  MOZ_ASSERT(mSampler);
  return mSampler;
}

void APZCTreeManager::AssertOnSamplerThread() {
  GetSampler()->AssertOnSamplerThread();
}

APZUpdater* APZCTreeManager::GetUpdater() const {
  // We should always have an updater here, since in practice the updater
  // is destroyed at the same time that this APZCTreeManager instance is.
  MOZ_ASSERT(mUpdater);
  return mUpdater;
}

void APZCTreeManager::AssertOnUpdaterThread() {
  GetUpdater()->AssertOnUpdaterThread();
}

MOZ_PUSH_IGNORE_THREAD_SAFETY
void APZCTreeManager::LockTree() {
  AssertOnUpdaterThread();
  mTreeLock.Lock();
}

void APZCTreeManager::UnlockTree() {
  AssertOnUpdaterThread();
  mTreeLock.Unlock();
}
MOZ_POP_THREAD_SAFETY

void APZCTreeManager::SetDPI(float aDpiValue) {
  if (!APZThreadUtils::IsControllerThread()) {
    APZThreadUtils::RunOnControllerThread(
        NewRunnableMethod<float>("layers::APZCTreeManager::SetDPI", this,
                                 &APZCTreeManager::SetDPI, aDpiValue));
    return;
  }

  APZThreadUtils::AssertOnControllerThread();
  mDPI = aDpiValue;
}

float APZCTreeManager::GetDPI() const {
  APZThreadUtils::AssertOnControllerThread();
  return mDPI;
}

APZCTreeManager::FixedPositionInfo::FixedPositionInfo(
    const HitTestingTreeNode* aNode) {
  mFixedPositionAnimationId = aNode->GetFixedPositionAnimationId();
  mFixedPosSides = aNode->GetFixedPosSides();
  mFixedPosTarget = aNode->GetFixedPosTarget();
  mLayersId = aNode->GetLayersId();
}

APZCTreeManager::StickyPositionInfo::StickyPositionInfo(
    const HitTestingTreeNode* aNode) {
  mStickyPositionAnimationId = aNode->GetStickyPositionAnimationId();
  mFixedPosSides = aNode->GetFixedPosSides();
  mStickyPosTarget = aNode->GetStickyPosTarget();
  mLayersId = aNode->GetLayersId();
  mStickyScrollRangeInner = aNode->GetStickyScrollRangeInner();
  mStickyScrollRangeOuter = aNode->GetStickyScrollRangeOuter();
}

}  // namespace layers
}  // namespace mozilla
