/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WRHitTester.h"
#include "AsyncPanZoomController.h"
#include "APZCTreeManager.h"
#include "TreeTraversal.h"  // for BreadthFirstSearch
#include "mozilla/gfx/CompositorHitTestInfo.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "nsDebug.h"        // for NS_ASSERTION
#include "nsIXULRuntime.h"  // for FissionAutostart
#include "mozilla/gfx/Matrix.h"

#define APZCTM_LOG(...) \
  MOZ_LOG(APZCTreeManager::sLog, LogLevel::Debug, (__VA_ARGS__))

namespace mozilla {
namespace layers {

using mozilla::gfx::CompositorHitTestFlags;
using mozilla::gfx::CompositorHitTestInvisibleToHit;

static bool CheckCloseToIdentity(const gfx::Matrix4x4& aMatrix) {
  // We allow a factor of 1/2048 in the multiply part of the matrix, so that if
  // we multiply by a point on a screen of size 2048 we would be off by at most
  // 1 pixel approximately.
  const float multiplyEps = 1 / 2048.f;
  // We allow 1 pixel in the translate part of the matrix.
  const float translateEps = 1.f;

  if (!FuzzyEqualsAdditive(aMatrix._11, 1.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._12, 0.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._13, 0.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._14, 0.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._21, 0.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._22, 1.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._23, 0.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._24, 0.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._31, 0.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._32, 0.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._33, 1.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._34, 0.f, multiplyEps) ||
      !FuzzyEqualsAdditive(aMatrix._41, 0.f, translateEps) ||
      !FuzzyEqualsAdditive(aMatrix._42, 0.f, translateEps) ||
      !FuzzyEqualsAdditive(aMatrix._43, 0.f, translateEps) ||
      !FuzzyEqualsAdditive(aMatrix._44, 1.f, multiplyEps)) {
    return false;
  }
  return true;
}

// Checks that within the constraints of floating point math we can invert it
// reasonably enough that multiplying by the computed inverse is close to the
// identity.
static bool CheckInvertibleWithFinitePrecision(const gfx::Matrix4x4& aMatrix) {
  auto inverse = aMatrix.MaybeInverse();
  if (inverse.isNothing()) {
    // Should we return false?
    return true;
  }
  if (!CheckCloseToIdentity(aMatrix * *inverse)) {
    return false;
  }
  if (!CheckCloseToIdentity(*inverse * aMatrix)) {
    return false;
  }
  return true;
}

IAPZHitTester::HitTestResult WRHitTester::GetAPZCAtPoint(
    const ScreenPoint& aHitTestPoint,
    const RecursiveMutexAutoLock& aProofOfTreeLock) {
  HitTestResult hit;
  RefPtr<wr::WebRenderAPI> wr = mTreeManager->GetWebRenderAPI();
  if (!wr) {
    // If WebRender isn't running, fall back to the root APZC.
    // This is mostly for the benefit of GTests which do not
    // run a WebRender instance, but gracefully falling back
    // here allows those tests which are not specifically
    // testing the hit-test algorithm to still work.
    hit.mTargetApzc = FindRootApzcForLayersId(GetRootLayersId());
    hit.mHitResult = CompositorHitTestFlags::eVisibleToHitTest;
    return hit;
  }

  APZCTM_LOG("Hit-testing point %s with WR\n", ToString(aHitTestPoint).c_str());
  std::vector<wr::WrHitResult> results =
      wr->HitTest(wr::ToWorldPoint(aHitTestPoint));

  Maybe<wr::WrHitResult> chosenResult;
  // It's possible for the WebRender hit test to produce a result with
  // a LayersId whose corresponding layer tree has already been
  // torn down (e.g. during window/tab shutdown, or navigation to
  // a different domain). This is expected, so avoid debug assertions
  // firing in this scenario.
  DebugOnly<bool> layersIdExists = true;
  for (const wr::WrHitResult& result : results) {
    ScrollableLayerGuid guid{result.mLayersId, 0, result.mScrollId};
    APZCTM_LOG("Examining result with guid %s hit info 0x%x... ",
               ToString(guid).c_str(), result.mHitInfo.serialize());
    if (result.mHitInfo == CompositorHitTestInvisibleToHit) {
      APZCTM_LOG("skipping due to invisibility.\n");
      continue;
    }
    RefPtr<HitTestingTreeNode> node =
        GetTargetNode(guid, &ScrollableLayerGuid::EqualsIgnoringPresShell);
    if (!node) {
      APZCTM_LOG("no corresponding node found, falling back to root.\n");

#ifdef DEBUG
      // We can enter here during normal codepaths for cases where the
      // nsDisplayCompositorHitTestInfo item emitted a scrollId of
      // NULL_SCROLL_ID to the webrender display list. The semantics of that
      // is to fall back to the root APZC for the layers id, so that's what
      // we do here.
      // If we enter this codepath and scrollId is not NULL_SCROLL_ID, then
      // that's more likely to be due to a race condition between rebuilding
      // the APZ tree and updating the WR scene/hit-test information, resulting
      // in WR giving us a hit result for a scene that is not active in APZ.
      // Such a scenario would need debugging and fixing.
      // In non-Fission mode, make this assertion non-fatal because there is
      // a known issue related to inactive scroll frames that can cause this
      // to fire (see bug 1634763), which is fixed in Fission mode and not
      // worth fixing in non-Fission mode.
      layersIdExists =
          CompositorBridgeParent::HasLayerTreeState(result.mLayersId);
      if (FissionAutostart()) {
        MOZ_ASSERT(result.mScrollId == ScrollableLayerGuid::NULL_SCROLL_ID ||
                   !layersIdExists);
      } else {
        NS_ASSERTION(
            result.mScrollId == ScrollableLayerGuid::NULL_SCROLL_ID,
            "Inconsistency between WebRender display list and APZ scroll data");
      }
#endif
      node = FindRootNodeForLayersId(result.mLayersId);
      if (!node) {
        // Should never happen, but handle gracefully in release builds just
        // in case.
        MOZ_ASSERT(!layersIdExists,
                   "No root node found for hit-test result layers id");
        chosenResult = Some(result);
        break;
      }
    }
    MOZ_ASSERT(node->GetApzc());  // any node returned must have an APZC
    EventRegionsOverride flags = node->GetEventRegionsOverride();
    if (flags & EventRegionsOverride::ForceEmptyHitRegion) {
      // This result is inside a subtree that is invisible to hit-testing.
      APZCTM_LOG("skipping due to FEHR subtree.\n");
      continue;
    }

    if (!CheckInvertibleWithFinitePrecision(
            mTreeManager->GetScreenToApzcTransform(node->GetApzc())
                .ToUnknownMatrix())) {
      APZCTM_LOG("skipping due to check inverse accuracy\n");
      continue;
    }

    APZCTM_LOG("selecting as chosen result.\n");
    chosenResult = Some(result);
    hit.mTargetApzc = node->GetApzc();
    if (flags & EventRegionsOverride::ForceDispatchToContent) {
      chosenResult->mHitInfo += CompositorHitTestFlags::eApzAwareListeners;
    }
    break;
  }
  if (!chosenResult) {
    return hit;
  }

  MOZ_ASSERT(
      hit.mTargetApzc || !layersIdExists,
      "A hit-test result with a valid layers id should have a target APZC");
  hit.mLayersId = chosenResult->mLayersId;
  ScrollableLayerGuid::ViewID scrollId = chosenResult->mScrollId;
  gfx::CompositorHitTestInfo hitInfo = chosenResult->mHitInfo;
  Maybe<uint64_t> animationId = chosenResult->mAnimationId;
  SideBits sideBits = chosenResult->mSideBits;

  APZCTM_LOG("Successfully matched APZC %p (hit result 0x%x)\n",
             hit.mTargetApzc.get(), hitInfo.serialize());

  const bool isScrollbar =
      hitInfo.contains(gfx::CompositorHitTestFlags::eScrollbar);
  const bool isScrollbarThumb =
      hitInfo.contains(gfx::CompositorHitTestFlags::eScrollbarThumb);
  const ScrollDirection direction =
      hitInfo.contains(gfx::CompositorHitTestFlags::eScrollbarVertical)
          ? ScrollDirection::eVertical
          : ScrollDirection::eHorizontal;
  HitTestingTreeNode* scrollbarNode = nullptr;
  if (isScrollbar || isScrollbarThumb) {
    scrollbarNode = BreadthFirstSearch<ReverseIterator>(
        GetRootNode(), [&](HitTestingTreeNode* aNode) {
          return (aNode->GetLayersId() == hit.mLayersId) &&
                 (aNode->IsScrollbarNode() == isScrollbar) &&
                 (aNode->IsScrollThumbNode() == isScrollbarThumb) &&
                 (aNode->GetScrollbarDirection() == direction) &&
                 (aNode->GetScrollTargetId() == scrollId);
        });
  }

  hit.mHitResult = hitInfo;

  if (scrollbarNode) {
    RefPtr<HitTestingTreeNode> scrollbarRef = scrollbarNode;
    InitializeHitTestingTreeNodeAutoLock(hit.mScrollbarNode, aProofOfTreeLock,
                                         scrollbarRef);
  }

  hit.mFixedPosSides = sideBits;
  if (animationId.isSome()) {
    RefPtr<HitTestingTreeNode> positionedNode = nullptr;

    positionedNode = BreadthFirstSearch<ReverseIterator>(
        GetRootNode(), [&](HitTestingTreeNode* aNode) {
          return (aNode->GetFixedPositionAnimationId() == animationId ||
                  aNode->GetStickyPositionAnimationId() == animationId);
        });

    if (positionedNode) {
      MOZ_ASSERT(positionedNode->GetLayersId() == chosenResult->mLayersId,
                 "Found node layers id does not match the hit result");
      MOZ_ASSERT((positionedNode->GetFixedPositionAnimationId().isSome() ||
                  positionedNode->GetStickyPositionAnimationId().isSome()),
                 "A a matching fixed/sticky position node should be found");
      InitializeHitTestingTreeNodeAutoLock(hit.mNode, aProofOfTreeLock,
                                           positionedNode);
    }

#if defined(MOZ_WIDGET_ANDROID)
    if (hit.mNode && hit.mNode->GetFixedPositionAnimationId().isSome()) {
      // If the hit element is a fixed position element, the side bits from
      // the hit-result item tag are used. For now just ensure that these
      // match what is found in the hit-testing tree node.
      MOZ_ASSERT(sideBits == hit.mNode->GetFixedPosSides(),
                 "Fixed position side bits do not match");
    } else if (hit.mTargetApzc && hit.mTargetApzc->IsRootContent()) {
      // If the hit element is not a fixed position element, then the hit test
      // result item's side bits should not be populated.
      MOZ_ASSERT(sideBits == SideBits::eNone,
                 "Hit test results have side bits only for pos:fixed");
    }
#endif
  }

  hit.mHitOverscrollGutter =
      hit.mTargetApzc && hit.mTargetApzc->IsInOverscrollGutter(aHitTestPoint);

  return hit;
}

}  // namespace layers
}  // namespace mozilla
