/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ViewTransition.h"

#include "mozilla/gfx/2D.h"
#include "WindowRenderer.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/ViewTransitionBinding.h"
#include "mozilla/image/WebRenderImageProvider.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/EffectSet.h"
#include "mozilla/ElementAnimationData.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/SVGIntegrationUtils.h"
#include "mozilla/WritingModes.h"
#include "nsDisplayList.h"
#include "nsFrameState.h"
#include "nsITimer.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsCanvasFrame.h"
#include "nsString.h"
#include "nsViewManager.h"
#include "Units.h"

namespace mozilla::dom {

LazyLogModule gViewTransitionsLog("ViewTransitions");

static void SetCaptured(nsIFrame* aFrame, bool aCaptured) {
  aFrame->AddOrRemoveStateBits(NS_FRAME_CAPTURED_IN_VIEW_TRANSITION, aCaptured);
  aFrame->InvalidateFrameSubtree();
  if (aFrame->Style()->IsRootElementStyle()) {
    aFrame->PresShell()->GetRootFrame()->InvalidateFrameSubtree();
  }
}

// Set capture's old transform to a <transform-function> that would map
// element's border box from the snapshot containing block origin to its
// current visual position.
//
// Since we're using viewport as the snapshot origin, we can use
// GetBoundingClientRect() effectively...
//
// TODO(emilio): This might need revision.
static CSSToCSSMatrix4x4Flagged EffectiveTransform(nsIFrame* aFrame) {
  CSSToCSSMatrix4x4Flagged matrix;
  if (aFrame->GetSize().IsEmpty() || aFrame->Style()->IsRootElementStyle()) {
    return matrix;
  }

  CSSSize untransformedSize = CSSSize::FromAppUnits(aFrame->GetSize());
  CSSRect boundingRect = CSSRect::FromAppUnits(aFrame->GetBoundingClientRect());
  if (boundingRect.Size() != untransformedSize) {
    float sx = boundingRect.width / untransformedSize.width;
    float sy = boundingRect.height / untransformedSize.height;
    matrix = CSSToCSSMatrix4x4Flagged::Scaling(sx, sy, 0.0f);
  }
  auto inkOverflowOffset = aFrame->InkOverflowRectRelativeToSelf().TopLeft();
  if (inkOverflowOffset != nsPoint()) {
    auto cssOffset = CSSPoint::FromAppUnits(inkOverflowOffset);
    matrix.PostTranslate(cssOffset.x, cssOffset.y, 0.0f);
  }
  if (boundingRect.TopLeft() != CSSPoint()) {
    matrix.PostTranslate(boundingRect.x, boundingRect.y, 0.0f);
  }
  return matrix;
}

static RefPtr<gfx::DataSourceSurface> CaptureFallbackSnapshot(
    nsIFrame* aFrame) {
  VT_LOG_DEBUG("CaptureFallbackSnapshot(%s)", aFrame->ListTag().get());
  nsPresContext* pc = aFrame->PresContext();
  const bool isRoot = aFrame->Style()->IsRootElementStyle();
  nsIFrame* frameToCapture =
      isRoot ? pc->PresShell()->GetCanvasFrame() : aFrame;
  const nsRect rect = isRoot ? ViewTransition::SnapshotContainingBlockRect(pc)
                             : aFrame->InkOverflowRectRelativeToSelf();
  const auto surfaceRect = LayoutDeviceIntRect::FromAppUnitsToOutside(
      rect, pc->AppUnitsPerDevPixel());

  // TODO: Should we use the DrawTargetRecorder infra or what not?
  const auto format = gfx::SurfaceFormat::B8G8R8A8;
  RefPtr<gfx::DrawTarget> dt = gfx::Factory::CreateDrawTarget(
      gfxPlatform::GetPlatform()->GetSoftwareBackend(),
      surfaceRect.Size().ToUnknownSize(), format);
  if (NS_WARN_IF(!dt) || NS_WARN_IF(!dt->IsValid())) {
    return nullptr;
  }

  {
    using PaintFrameFlags = nsLayoutUtils::PaintFrameFlags;
    gfxContext thebes(dt);
    // TODO: This matches the drawable code we use for -moz-element(), but is
    // this right?
    const PaintFrameFlags flags = PaintFrameFlags::InTransform;
    nsLayoutUtils::PaintFrame(&thebes, frameToCapture, rect,
                              NS_RGBA(0, 0, 0, 0),
                              nsDisplayListBuilderMode::Painting, flags);
  }

  RefPtr<gfx::SourceSurface> surf = dt->GetBackingSurface();
  if (NS_WARN_IF(!surf)) {
    return nullptr;
  }
  return surf->GetDataSurface();
}

static constexpr wr::ImageKey kNoKey{{0}, 0};

struct OldSnapshotData {
  wr::ImageKey mImageKey = kNoKey;
  nsSize mSize;
  RefPtr<gfx::DataSourceSurface> mFallback;
  RefPtr<layers::RenderRootStateManager> mManager;

  OldSnapshotData() = default;

  explicit OldSnapshotData(nsIFrame* aFrame)
      : mSize(aFrame->InkOverflowRectRelativeToSelf().Size()) {
    if (!StaticPrefs::dom_viewTransitions_wr_old_capture()) {
      mFallback = CaptureFallbackSnapshot(aFrame);
    }
  }

  void EnsureKey(layers::RenderRootStateManager* aManager,
                 wr::IpcResourceUpdateQueue& aResources) {
    if (mImageKey != kNoKey) {
      MOZ_ASSERT(mManager == aManager, "Stale manager?");
      return;
    }
    if (StaticPrefs::dom_viewTransitions_wr_old_capture()) {
      mManager = aManager;
      mImageKey = aManager->WrBridge()->GetNextImageKey();
      aResources.AddSnapshotImage(wr::SnapshotImageKey{mImageKey});
      return;
    }
    if (NS_WARN_IF(!mFallback)) {
      return;
    }
    gfx::DataSourceSurface::ScopedMap map(mFallback,
                                          gfx::DataSourceSurface::READ);
    if (NS_WARN_IF(!map.IsMapped())) {
      return;
    }
    mManager = aManager;
    mImageKey = aManager->WrBridge()->GetNextImageKey();
    auto size = mFallback->GetSize();
    auto format = mFallback->GetFormat();
    wr::ImageDescriptor desc(size, format);
    Range<uint8_t> bytes(map.GetData(), map.GetStride() * size.height);
    Unused << NS_WARN_IF(!aResources.AddImage(mImageKey, desc, bytes));
  }

  ~OldSnapshotData() {
    if (mManager) {
      mManager->AddImageKeyForDiscard(mImageKey);
    }
  }
};

struct CapturedElementOldState {
  OldSnapshotData mSnapshot;
  // Whether we tried to capture an image. Note we might fail to get a
  // snapshot, so this might not be the same as !!mImage.
  bool mTriedImage = false;

  // Encompasses width and height.
  nsSize mSize;
  CSSToCSSMatrix4x4Flagged mTransform;
  StyleWritingModeProperty mWritingMode =
      StyleWritingModeProperty::HorizontalTb;
  StyleDirection mDirection = StyleDirection::Ltr;
  StyleTextOrientation mTextOrientation = StyleTextOrientation::Mixed;
  StyleBlend mMixBlendMode = StyleBlend::Normal;
  StyleOwnedSlice<StyleFilter> mBackdropFilters;
  // Note: it's unfortunate we cannot just store the bits here. color-scheme
  // property uses idents for serialization. If the idents and bits are not
  // aligned, we assert it in ToCSS.
  StyleColorScheme mColorScheme;

  CapturedElementOldState(nsIFrame* aFrame,
                          const nsSize& aSnapshotContainingBlockSize)
      : mSnapshot(aFrame),
        mTriedImage(true),
        mSize(aFrame->Style()->IsRootElementStyle()
                  ? aSnapshotContainingBlockSize
                  : aFrame->InkOverflowRect().Size()),
        mTransform(EffectiveTransform(aFrame)),
        mWritingMode(aFrame->StyleVisibility()->mWritingMode),
        mDirection(aFrame->StyleVisibility()->mDirection),
        mTextOrientation(aFrame->StyleVisibility()->mTextOrientation),
        mMixBlendMode(aFrame->StyleEffects()->mMixBlendMode),
        mBackdropFilters(aFrame->StyleEffects()->mBackdropFilters),
        mColorScheme(aFrame->StyleUI()->mColorScheme) {}

  CapturedElementOldState() = default;
};

// https://drafts.csswg.org/css-view-transitions/#captured-element
struct ViewTransition::CapturedElement {
  CapturedElementOldState mOldState;
  RefPtr<Element> mNewElement;
  wr::SnapshotImageKey mNewSnapshotKey{kNoKey};
  nsSize mNewSnapshotSize;

  CapturedElement() = default;

  CapturedElement(nsIFrame* aFrame, const nsSize& aSnapshotContainingBlockSize)
      : mOldState(aFrame, aSnapshotContainingBlockSize) {}

  // https://drafts.csswg.org/css-view-transitions-1/#captured-element-style-definitions
  nsTArray<Keyframe> mGroupKeyframes;
  // The group animation-name rule and group styles rule, merged into one.
  RefPtr<StyleLockedDeclarationBlock> mGroupRule;
  // The image pair isolation rule.
  RefPtr<StyleLockedDeclarationBlock> mImagePairRule;
  // The rules for ::view-transition-old(<name>).
  RefPtr<StyleLockedDeclarationBlock> mOldRule;
  // The rules for ::view-transition-new(<name>).
  RefPtr<StyleLockedDeclarationBlock> mNewRule;

  ~CapturedElement() {
    if (wr::AsImageKey(mNewSnapshotKey) != kNoKey) {
      MOZ_ASSERT(mOldState.mSnapshot.mManager);
      mOldState.mSnapshot.mManager->AddSnapshotImageKeyForDiscard(
          mNewSnapshotKey);
    }
  }
};

static inline void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCb,
    const ViewTransition::CapturedElement& aField, const char* aName,
    uint32_t aFlags = 0) {
  ImplCycleCollectionTraverse(aCb, aField.mNewElement, aName, aFlags);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ViewTransition, mDocument,
                                      mUpdateCallback,
                                      mUpdateCallbackDonePromise, mReadyPromise,
                                      mFinishedPromise, mNamedElements,
                                      mViewTransitionRoot)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ViewTransition)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ViewTransition)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ViewTransition)

ViewTransition::ViewTransition(Document& aDoc,
                               ViewTransitionUpdateCallback* aCb)
    : mDocument(&aDoc), mUpdateCallback(aCb) {}

ViewTransition::~ViewTransition() { ClearTimeoutTimer(); }

Maybe<nsSize> ViewTransition::GetOldSize(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return {};
  }
  return Some(el->mOldState.mSnapshot.mSize);
}

Maybe<nsSize> ViewTransition::GetNewSize(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return {};
  }
  return Some(el->mNewSnapshotSize);
}

const wr::ImageKey* ViewTransition::GetOldImageKey(
    nsAtom* aName, layers::RenderRootStateManager* aManager,
    wr::IpcResourceUpdateQueue& aResources) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return nullptr;
  }
  el->mOldState.mSnapshot.EnsureKey(aManager, aResources);
  return &el->mOldState.mSnapshot.mImageKey;
}

const wr::ImageKey* ViewTransition::GetNewImageKey(nsAtom* aName) const {
  auto* el = mNamedElements.Get(aName);
  if (NS_WARN_IF(!el)) {
    return nullptr;
  }
  return &el->mNewSnapshotKey._0;
}

const wr::ImageKey* ViewTransition::GetImageKeyForCapturedFrame(
    nsIFrame* aFrame, layers::RenderRootStateManager* aManager,
    wr::IpcResourceUpdateQueue& aResources) const {
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aFrame->HasAnyStateBits(NS_FRAME_CAPTURED_IN_VIEW_TRANSITION));

  if (!StaticPrefs::dom_viewTransitions_live_capture()) {
    return nullptr;
  }

  nsAtom* name = aFrame->StyleUIReset()->mViewTransitionName._0.AsAtom();
  if (NS_WARN_IF(name->IsEmpty())) {
    return nullptr;
  }
  const bool isOld = mPhase < Phase::Animating;

  VT_LOG("ViewTransition::GetImageKeyForCapturedFrame(%s, old=%d)\n",
         nsAtomCString(name).get(), isOld);

  if (isOld) {
    const auto* key = GetOldImageKey(name, aManager, aResources);
    VT_LOG(" > old image is %s", key ? ToString(*key).c_str() : "null");
    return key;
  }
  auto* el = mNamedElements.Get(name);
  if (NS_WARN_IF(!el)) {
    return nullptr;
  }
  if (NS_WARN_IF(el->mNewElement != aFrame->GetContent())) {
    return nullptr;
  }
  if (wr::AsImageKey(el->mNewSnapshotKey) == kNoKey) {
    MOZ_ASSERT(!el->mOldState.mSnapshot.mManager ||
                   el->mOldState.mSnapshot.mManager == aManager,
               "Stale manager?");
    el->mNewSnapshotKey = {aManager->WrBridge()->GetNextImageKey()};
    el->mOldState.mSnapshot.mManager = aManager;
    aResources.AddSnapshotImage(el->mNewSnapshotKey);
  }
  VT_LOG(" > new image is %s", ToString(el->mNewSnapshotKey._0).c_str());
  return &el->mNewSnapshotKey._0;
}

nsIGlobalObject* ViewTransition::GetParentObject() const {
  return mDocument ? mDocument->GetParentObject() : nullptr;
}

Promise* ViewTransition::GetUpdateCallbackDone(ErrorResult& aRv) {
  if (!mUpdateCallbackDonePromise) {
    mUpdateCallbackDonePromise = Promise::Create(GetParentObject(), aRv);
  }
  return mUpdateCallbackDonePromise;
}

Promise* ViewTransition::GetReady(ErrorResult& aRv) {
  if (!mReadyPromise) {
    mReadyPromise = Promise::Create(GetParentObject(), aRv);
  }
  return mReadyPromise;
}

Promise* ViewTransition::GetFinished(ErrorResult& aRv) {
  if (!mFinishedPromise) {
    mFinishedPromise = Promise::Create(GetParentObject(), aRv);
  }
  return mFinishedPromise;
}

// This performs the step 5 in setup view transition.
// https://drafts.csswg.org/css-view-transitions-1/#setup-view-transition
void ViewTransition::MaybeScheduleUpdateCallback() {
  // 1. If transition’s phase is "done", then abort these steps.
  // Note: This happens if transition was skipped before this point.
  if (mPhase == Phase::Done) {
    return;
  }

  RefPtr doc = mDocument;

  // 2. Schedule the update callback for transition.
  doc->ScheduleViewTransitionUpdateCallback(this);

  // 3. Flush the update callback queue.
  doc->FlushViewTransitionUpdateCallbackQueue();
}

// https://drafts.csswg.org/css-view-transitions-1/#call-the-update-callback
void ViewTransition::CallUpdateCallback(ErrorResult& aRv) {
  MOZ_ASSERT(mDocument);
  // Step 1:  Assert: transition's phase is "done", or before
  // "update-callback-called".
  MOZ_ASSERT(mPhase == Phase::Done ||
             UnderlyingValue(mPhase) <
                 UnderlyingValue(Phase::UpdateCallbackCalled));

  // Step 5: If transition's phase is not "done", then set transition's phase
  // to "update-callback-called".
  //
  // NOTE(emilio): This is swapped with the spec because the spec is broken,
  // see https://github.com/w3c/csswg-drafts/issues/10822
  if (mPhase != Phase::Done) {
    mPhase = Phase::UpdateCallbackCalled;
  }

  // Step 2: Let callbackPromise be null.
  RefPtr<Promise> callbackPromise;
  if (!mUpdateCallback) {
    // Step 3: If transition's update callback is null, then set callbackPromise
    // to a promise resolved with undefined, in transition’s relevant Realm.
    callbackPromise =
        Promise::CreateResolvedWithUndefined(GetParentObject(), aRv);
  } else {
    // Step 4: Otherwise set callbackPromise to the result of invoking
    // transition’s update callback. MOZ_KnownLive because the callback can only
    // go away when we get CCd.
    callbackPromise = MOZ_KnownLive(mUpdateCallback)->Call(aRv);
  }
  if (aRv.Failed()) {
    // TODO(emilio): Do we need extra error handling here?
    return;
  }
  MOZ_ASSERT(callbackPromise);
  // Step 8: React to callbackPromise with fulfillSteps and rejectSteps.
  callbackPromise->AddCallbacksWithCycleCollectedArgs(
      [](JSContext*, JS::Handle<JS::Value>, ErrorResult& aRv,
         ViewTransition* aVt) {
        // We clear the timeout when we are ready to activate. Otherwise, any
        // animations with the duration longer than
        // StaticPrefs::dom_viewTransitions_timeout_ms() will be interrupted.
        // FIXME: We may need a better solution to tweak the timeout, e.g. reset
        // the timeout to a longer value or so on.
        aVt->ClearTimeoutTimer();

        // Step 6: Let fulfillSteps be to following steps:
        if (Promise* ucd = aVt->GetUpdateCallbackDone(aRv)) {
          // 6.1: Resolve transition's update callback done promise with
          // undefined.
          ucd->MaybeResolveWithUndefined();
        }
        // Unlike other timings, this is not guaranteed to happen with clean
        // layout, and Activate() needs to look at the frame tree to capture the
        // new state, so we need to flush frames. Do it here so that we deal
        // with other potential script execution skipping the transition or
        // what not in a consistent way.
        aVt->mDocument->FlushPendingNotifications(FlushType::Layout);
        if (aVt->mPhase == Phase::Done) {
          // "Skip a transition" step 8. We need to resolve "finished" after
          // update-callback-done.
          if (Promise* finished = aVt->GetFinished(aRv)) {
            finished->MaybeResolveWithUndefined();
          }
        }
        aVt->Activate();
      },
      [](JSContext*, JS::Handle<JS::Value> aReason, ErrorResult& aRv,
         ViewTransition* aVt) {
        // Clear the timeout because we are ready to skip the view transitions.
        aVt->ClearTimeoutTimer();

        // Step 7: Let rejectSteps be to following steps:
        if (Promise* ucd = aVt->GetUpdateCallbackDone(aRv)) {
          // 7.1: Reject transition's update callback done promise with reason.
          ucd->MaybeReject(aReason);
        }

        // 7.2: If transition's phase is "done", then return.
        if (aVt->mPhase == Phase::Done) {
          // "Skip a transition" step 8. We need to resolve "finished" after
          // update-callback-done.
          if (Promise* finished = aVt->GetFinished(aRv)) {
            finished->MaybeReject(aReason);
          }
          return;
        }

        // 7.3: Mark as handled transition's ready promise.
        if (Promise* ready = aVt->GetReady(aRv)) {
          MOZ_ALWAYS_TRUE(ready->SetAnyPromiseIsHandled());
        }
        aVt->SkipTransition(SkipTransitionReason::UpdateCallbackRejected,
                            aReason);
      },
      RefPtr(this));

  // Step 9: To skip a transition after a timeout, the user agent may perform
  // the following steps in parallel:
  MOZ_ASSERT(!mTimeoutTimer);
  ClearTimeoutTimer();  // Be safe just in case.
  mTimeoutTimer = NS_NewTimer();
  mTimeoutTimer->InitWithNamedFuncCallback(
      TimeoutCallback, this, StaticPrefs::dom_viewTransitions_timeout_ms(),
      nsITimer::TYPE_ONE_SHOT, "ViewTransition::TimeoutCallback");
}

void ViewTransition::ClearTimeoutTimer() {
  if (mTimeoutTimer) {
    mTimeoutTimer->Cancel();
    mTimeoutTimer = nullptr;
  }
}

void ViewTransition::TimeoutCallback(nsITimer* aTimer, void* aClosure) {
  RefPtr vt = static_cast<ViewTransition*>(aClosure);
  MOZ_DIAGNOSTIC_ASSERT(aTimer == vt->mTimeoutTimer);
  vt->Timeout();
}

void ViewTransition::Timeout() {
  ClearTimeoutTimer();
  if (mPhase != Phase::Done && mDocument) {
    SkipTransition(SkipTransitionReason::Timeout);
  }
}

static already_AddRefed<Element> MakePseudo(Document& aDoc,
                                            PseudoStyleType aType,
                                            nsAtom* aName) {
  RefPtr<Element> el = aDoc.CreateHTMLElement(nsGkAtoms::div);
  if (!aName) {
    MOZ_ASSERT(aType == PseudoStyleType::viewTransition);
    el->SetIsNativeAnonymousRoot();
  }
  el->SetPseudoElementType(aType);
  if (aName) {
    el->SetAttr(nsGkAtoms::name, nsDependentAtomString(aName), IgnoreErrors());
  }
  // This is not needed, but useful for debugging.
  el->SetAttr(nsGkAtoms::type,
              nsDependentAtomString(nsCSSPseudoElements::GetPseudoAtom(aType)),
              IgnoreErrors());
  return el.forget();
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp, const nsACString& aValue) {
  return Servo_DeclarationBlock_SetPropertyById(
      aDecls, aProp, &aValue,
      /* is_important = */ false, aDoc->DefaultStyleAttrURLData(),
      StyleParsingMode::DEFAULT, eCompatibility_FullStandards,
      aDoc->CSSLoader(), StyleCssRuleType::Style, {});
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document*,
                    nsCSSPropertyID aProp, float aLength, nsCSSUnit aUnit) {
  return Servo_DeclarationBlock_SetLengthValue(aDecls, aProp, aLength, aUnit);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document*,
                    nsCSSPropertyID aProp, const CSSToCSSMatrix4x4Flagged& aM) {
  MOZ_ASSERT(aProp == eCSSProperty_transform);
  AutoTArray<StyleTransformOperation, 1> ops;
  ops.AppendElement(
      StyleTransformOperation::Matrix3D(StyleGenericMatrix3D<StyleNumber>{
          aM._11, aM._12, aM._13, aM._14, aM._21, aM._22, aM._23, aM._24,
          aM._31, aM._32, aM._33, aM._34, aM._41, aM._42, aM._43, aM._44}));
  return Servo_DeclarationBlock_SetTransform(aDecls, aProp, &ops);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp, const StyleWritingModeProperty aWM) {
  return Servo_DeclarationBlock_SetKeywordValue(aDecls, aProp, (int32_t)aWM);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp, const StyleDirection aDirection) {
  return Servo_DeclarationBlock_SetKeywordValue(aDecls, aProp,
                                                (int32_t)aDirection);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp,
                    const StyleTextOrientation aTextOrientation) {
  return Servo_DeclarationBlock_SetKeywordValue(aDecls, aProp,
                                                (int32_t)aTextOrientation);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document* aDoc,
                    nsCSSPropertyID aProp, const StyleBlend aBlend) {
  return Servo_DeclarationBlock_SetKeywordValue(aDecls, aProp, (int32_t)aBlend);
}

static bool SetProp(
    StyleLockedDeclarationBlock* aDecls, Document*, nsCSSPropertyID aProp,
    const StyleOwnedSlice<mozilla::StyleFilter>& aBackdropFilters) {
  return Servo_DeclarationBlock_SetBackdropFilter(aDecls, aProp,
                                                  &aBackdropFilters);
}

static bool SetProp(StyleLockedDeclarationBlock* aDecls, Document*,
                    nsCSSPropertyID aProp,
                    const StyleColorScheme& aColorScheme) {
  return Servo_DeclarationBlock_SetColorScheme(aDecls, aProp, &aColorScheme);
}

static StyleLockedDeclarationBlock* EnsureRule(
    RefPtr<StyleLockedDeclarationBlock>& aRule) {
  if (!aRule) {
    aRule = Servo_DeclarationBlock_CreateEmpty().Consume();
  }
  return aRule.get();
}

// TODO: backdrop-filter support.
static nsTArray<Keyframe> BuildGroupKeyframes(
    Document* aDoc, const CSSToCSSMatrix4x4Flagged& aTransform,
    const nsSize& aSize) {
  nsTArray<Keyframe> result;
  auto& firstKeyframe = *result.AppendElement();
  firstKeyframe.mOffset = Some(0.0);
  PropertyValuePair transform{
      AnimatedPropertyID(eCSSProperty_transform),
      Servo_DeclarationBlock_CreateEmpty().Consume(),
  };
  SetProp(transform.mServoDeclarationBlock, aDoc, eCSSProperty_transform,
          aTransform);
  PropertyValuePair width{
      AnimatedPropertyID(eCSSProperty_width),
      Servo_DeclarationBlock_CreateEmpty().Consume(),
  };
  CSSSize cssSize = CSSSize::FromAppUnits(aSize);
  SetProp(width.mServoDeclarationBlock, aDoc, eCSSProperty_width, cssSize.width,
          eCSSUnit_Pixel);
  PropertyValuePair height{
      AnimatedPropertyID(eCSSProperty_height),
      Servo_DeclarationBlock_CreateEmpty().Consume(),
  };
  SetProp(height.mServoDeclarationBlock, aDoc, eCSSProperty_height,
          cssSize.height, eCSSUnit_Pixel);
  firstKeyframe.mPropertyValues.AppendElement(std::move(transform));
  firstKeyframe.mPropertyValues.AppendElement(std::move(width));
  firstKeyframe.mPropertyValues.AppendElement(std::move(height));

  auto& lastKeyframe = *result.AppendElement();
  lastKeyframe.mOffset = Some(1.0);
  lastKeyframe.mPropertyValues.AppendElement(
      PropertyValuePair{AnimatedPropertyID(eCSSProperty_transform)});
  lastKeyframe.mPropertyValues.AppendElement(
      PropertyValuePair{AnimatedPropertyID(eCSSProperty_width)});
  lastKeyframe.mPropertyValues.AppendElement(
      PropertyValuePair{AnimatedPropertyID(eCSSProperty_height)});
  return result;
}

bool ViewTransition::GetGroupKeyframes(nsAtom* aAnimationName,
                                       nsTArray<Keyframe>& aResult) const {
  MOZ_ASSERT(StringBeginsWith(nsDependentAtomString(aAnimationName),
                              kGroupAnimPrefix));
  RefPtr<nsAtom> transitionName = NS_Atomize(Substring(
      nsDependentAtomString(aAnimationName), kGroupAnimPrefix.Length()));
  auto* el = mNamedElements.Get(transitionName);
  if (NS_WARN_IF(!el) || NS_WARN_IF(el->mGroupKeyframes.IsEmpty())) {
    return false;
  }
  aResult = el->mGroupKeyframes.Clone();
  return true;
}

// https://drafts.csswg.org/css-view-transitions-1/#setup-transition-pseudo-elements
void ViewTransition::SetupTransitionPseudoElements() {
  MOZ_ASSERT(!mViewTransitionRoot);

  nsAutoScriptBlocker scriptBlocker;

  RefPtr docElement = mDocument->GetRootElement();
  if (!docElement) {
    return;
  }

  // Step 1 is a declaration.

  // Step 2: Set document's show view transition tree to true.
  // (we lazily create this pseudo-element so we don't need the flag for now at
  // least).
  mViewTransitionRoot =
      MakePseudo(*mDocument, PseudoStyleType::viewTransition, nullptr);
#ifdef DEBUG
  // View transition pseudos don't care about frame tree ordering, so can be
  // restyled just fine.
  mViewTransitionRoot->SetProperty(nsGkAtoms::restylableAnonymousNode,
                                   reinterpret_cast<void*>(true));
#endif

  MOZ_ASSERT(mNames.Length() == mNamedElements.Count());
  // Step 3: For each transitionName -> capturedElement of transition’s named
  // elements:
  for (nsAtom* transitionName : mNames) {
    // We don't need to notify while constructing the tree.
    constexpr bool kNotify = false;
    CapturedElement& capturedElement = *mNamedElements.Get(transitionName);
    // Let group be a new ::view-transition-group(), with its view transition
    // name set to transitionName.
    RefPtr<Element> group = MakePseudo(
        *mDocument, PseudoStyleType::viewTransitionGroup, transitionName);
    // Append group to transition’s transition root pseudo-element.
    mViewTransitionRoot->AppendChildTo(group, kNotify, IgnoreErrors());
    // Let imagePair be a new ::view-transition-image-pair(), with its view
    // transition name set to transitionName.
    RefPtr<Element> imagePair = MakePseudo(
        *mDocument, PseudoStyleType::viewTransitionImagePair, transitionName);
    // Append imagePair to group.
    group->AppendChildTo(imagePair, kNotify, IgnoreErrors());
    // If capturedElement's old image is not null, then:
    if (capturedElement.mOldState.mTriedImage) {
      // Let old be a new ::view-transition-old(), with its view transition
      // name set to transitionName, displaying capturedElement's old image as
      // its replaced content.
      RefPtr<Element> old = MakePseudo(
          *mDocument, PseudoStyleType::viewTransitionOld, transitionName);
      // Append old to imagePair.
      imagePair->AppendChildTo(old, kNotify, IgnoreErrors());
    } else {
      // Moved around for simplicity. If capturedElement's old image is null,
      // then: Assert: capturedElement's new element is not null.
      MOZ_ASSERT(capturedElement.mNewElement);
      // Set capturedElement's image animation name rule to a new ...
      auto* rule = EnsureRule(capturedElement.mNewRule);
      SetProp(rule, mDocument, eCSSProperty_animation_name,
              "-ua-view-transition-fade-in"_ns);
    }
    // If capturedElement's new element is not null, then:
    if (capturedElement.mNewElement) {
      // Let new be a new ::view-transition-new(), with its view transition
      // name set to transitionName.
      RefPtr<Element> new_ = MakePseudo(
          *mDocument, PseudoStyleType::viewTransitionNew, transitionName);
      // Append new to imagePair.
      imagePair->AppendChildTo(new_, kNotify, IgnoreErrors());
    } else {
      // Moved around from the next step for simplicity.
      // Assert: capturedElement's old image is not null.
      // Set capturedElement's image animation name rule to a new CSSStyleRule
      // representing the following CSS, and append it to document’s dynamic
      // view transition style sheet:
      MOZ_ASSERT(capturedElement.mOldState.mTriedImage);
      SetProp(EnsureRule(capturedElement.mOldRule), mDocument,
              eCSSProperty_animation_name, "-ua-view-transition-fade-out"_ns);

      // Moved around from "update pseudo-element styles" because it's a one
      // time operation.
      auto* rule = EnsureRule(capturedElement.mGroupRule);
      auto oldRect = CSSPixel::FromAppUnits(capturedElement.mOldState.mSize);
      SetProp(rule, mDocument, eCSSProperty_width, oldRect.width,
              eCSSUnit_Pixel);
      SetProp(rule, mDocument, eCSSProperty_height, oldRect.height,
              eCSSUnit_Pixel);
      SetProp(rule, mDocument, eCSSProperty_transform,
              capturedElement.mOldState.mTransform);
      SetProp(rule, mDocument, eCSSProperty_writing_mode,
              capturedElement.mOldState.mWritingMode);
      SetProp(rule, mDocument, eCSSProperty_direction,
              capturedElement.mOldState.mDirection);
      SetProp(rule, mDocument, eCSSProperty_text_orientation,
              capturedElement.mOldState.mTextOrientation);
      SetProp(rule, mDocument, eCSSProperty_mix_blend_mode,
              capturedElement.mOldState.mMixBlendMode);
      SetProp(rule, mDocument, eCSSProperty_backdrop_filter,
              capturedElement.mOldState.mBackdropFilters);
      SetProp(rule, mDocument, eCSSProperty_color_scheme,
              capturedElement.mOldState.mColorScheme);
    }
    // If both of capturedElement's old image and new element are not null,
    // then:
    if (capturedElement.mOldState.mTriedImage && capturedElement.mNewElement) {
      NS_ConvertUTF16toUTF8 dynamicAnimationName(
          kGroupAnimPrefix + nsDependentAtomString(transitionName));

      capturedElement.mGroupKeyframes =
          BuildGroupKeyframes(mDocument, capturedElement.mOldState.mTransform,
                              capturedElement.mOldState.mSize);

      // Set capturedElement's group animation name rule to ...
      SetProp(EnsureRule(capturedElement.mGroupRule), mDocument,
              eCSSProperty_animation_name, dynamicAnimationName);

      // Set capturedElement's image pair isolation rule to ...
      SetProp(EnsureRule(capturedElement.mImagePairRule), mDocument,
              eCSSProperty_isolation, "isolate"_ns);

      // Set capturedElement's image animation name rule to ...
      SetProp(
          EnsureRule(capturedElement.mOldRule), mDocument,
          eCSSProperty_animation_name,
          "-ua-view-transition-fade-out, -ua-mix-blend-mode-plus-lighter"_ns);
      SetProp(
          EnsureRule(capturedElement.mNewRule), mDocument,
          eCSSProperty_animation_name,
          "-ua-view-transition-fade-in, -ua-mix-blend-mode-plus-lighter"_ns);
    }
  }
  BindContext context(*docElement, BindContext::ForNativeAnonymous);
  if (NS_FAILED(mViewTransitionRoot->BindToTree(context, *docElement))) {
    mViewTransitionRoot->UnbindFromTree();
    mViewTransitionRoot = nullptr;
    return;
  }
  if (mDocument->DevToolsAnonymousAndShadowEventsEnabled()) {
    mViewTransitionRoot->QueueDevtoolsAnonymousEvent(/* aIsRemove = */ false);
  }
  if (PresShell* ps = mDocument->GetPresShell()) {
    ps->ContentAppended(mViewTransitionRoot);
  }
}

// https://drafts.csswg.org/css-view-transitions-1/#style-transition-pseudo-elements-algorithm
bool ViewTransition::UpdatePseudoElementStyles(bool aNeedsInvalidation) {
  // 1. For each transitionName -> capturedElement of transition's "named
  // elements".
  for (auto& entry : mNamedElements) {
    nsAtom* transitionName = entry.GetKey();
    CapturedElement& capturedElement = *entry.GetData();
    // If capturedElement's new element is null, then:
    // We already did this in SetupTransitionPseudoElements().
    if (!capturedElement.mNewElement) {
      continue;
    }
    // Otherwise.
    // Return failure if any of the following conditions is true:
    //  * capturedElement's new element has a flat tree ancestor that skips its
    //    contents.
    //  * capturedElement's new element is not rendered.
    //  * capturedElement has more than one box fragment.
    nsIFrame* frame = capturedElement.mNewElement->GetPrimaryFrame();
    if (!frame || frame->IsHiddenByContentVisibilityOnAnyAncestor() ||
        frame->GetPrevContinuation() || frame->GetNextContinuation()) {
      return false;
    }
    auto* rule = EnsureRule(capturedElement.mGroupRule);
    // Let newRect be snapshot containing block if capturedElement is the
    // document element, otherwise, capturedElement’s border box.
    // NOTE: Needs ink overflow rect instead to get the correct rendering, see
    // https://github.com/w3c/csswg-drafts/issues/12092.
    // TODO(emilio, bug 1961139): Maybe revisit this.
    auto newRect = frame->Style()->IsRootElementStyle()
                       ? SnapshotContainingBlockRect()
                       : frame->InkOverflowRectRelativeToSelf();
    auto size = CSSPixel::FromAppUnits(newRect);
    // NOTE(emilio): Intentionally not short-circuiting. Int cast is needed to
    // silence warning.
    bool groupStyleChanged =
        int(SetProp(rule, mDocument, eCSSProperty_width, size.width,
                    eCSSUnit_Pixel)) |
        SetProp(rule, mDocument, eCSSProperty_height, size.height,
                eCSSUnit_Pixel) |
        SetProp(rule, mDocument, eCSSProperty_transform,
                EffectiveTransform(frame)) |
        SetProp(rule, mDocument, eCSSProperty_writing_mode,
                frame->StyleVisibility()->mWritingMode) |
        SetProp(rule, mDocument, eCSSProperty_direction,
                frame->StyleVisibility()->mDirection) |
        SetProp(rule, mDocument, eCSSProperty_text_orientation,
                frame->StyleVisibility()->mTextOrientation) |
        SetProp(rule, mDocument, eCSSProperty_mix_blend_mode,
                frame->StyleEffects()->mMixBlendMode) |
        SetProp(rule, mDocument, eCSSProperty_backdrop_filter,
                frame->StyleEffects()->mBackdropFilters) |
        SetProp(rule, mDocument, eCSSProperty_color_scheme,
                frame->StyleUI()->mColorScheme);
    if (groupStyleChanged && aNeedsInvalidation) {
      auto* pseudo = FindPseudo(PseudoStyleRequest(
          PseudoStyleType::viewTransitionGroup, transitionName));
      MOZ_ASSERT(pseudo);
      // TODO(emilio): Maybe we need something more than recascade? But I don't
      // see how off-hand.
      nsLayoutUtils::PostRestyleEvent(pseudo, RestyleHint::RECASCADE_SELF,
                                      nsChangeHint(0));
    }

    // 5. Live capturing (nothing to do here regarding the capture itself, but
    // if the size has changed, then we need to invalidate the new frame).
    auto oldSize = capturedElement.mNewSnapshotSize;
    capturedElement.mNewSnapshotSize =
        frame->InkOverflowRectRelativeToSelf().Size();
    if (oldSize != capturedElement.mNewSnapshotSize && aNeedsInvalidation) {
      frame->PresShell()->FrameNeedsReflow(
          frame, IntrinsicDirty::FrameAndAncestors, NS_FRAME_IS_DIRTY);
    }
  }
  return true;
}

// https://drafts.csswg.org/css-view-transitions-1/#activate-view-transition
void ViewTransition::Activate() {
  // Step 1: If transition's phase is "done", then return.
  if (mPhase == Phase::Done) {
    return;
  }

  mDocument->SetRenderingSuppressedForViewTransitions(false);

  // Step 3: If transition's initial snapshot containing block size is not
  // equal to the snapshot containing block size, then skip the view transition
  // for transition, and return.
  if (mInitialSnapshotContainingBlockSize !=
      SnapshotContainingBlockRect().Size()) {
    return SkipTransition(SkipTransitionReason::Resize);
  }

  // Step 4: Capture the new state for transition.
  // Step 5 is done along step 4 for performance.
  if (auto skipReason = CaptureNewState()) {
    // We clear named elements to not leave lingering "captured in a view
    // transition" state.
    ClearNamedElements();
    // If failure is returned, then skip the view transition for transition...
    return SkipTransition(*skipReason);
  }

  // Step 6: Setup transition pseudo-elements for transition.
  SetupTransitionPseudoElements();

  // Step 7: Update pseudo-element styles for transition.
  // We don't need to invalidate the pseudo-element styles since we just
  // generated them.
  if (!UpdatePseudoElementStyles(/* aNeedsInvalidation = */ false)) {
    // If failure is returned, then skip the view transition for transition
    // with an "InvalidStateError" DOMException in transition's relevant Realm,
    // and return.
    return SkipTransition(SkipTransitionReason::PseudoUpdateFailure);
  }

  // Step 8: Set transition's phase to "animating".
  mPhase = Phase::Animating;
  // Step 9: Resolve transition's ready promise.
  if (Promise* ready = GetReady(IgnoreErrors())) {
    ready->MaybeResolveWithUndefined();
  }

  // Once this view transition is activated, we have to perform the pending
  // operations periodically.
  MOZ_ASSERT(mDocument);
  mDocument->EnsureViewTransitionOperationsHappen();
}

// https://drafts.csswg.org/css-view-transitions/#perform-pending-transition-operations
void ViewTransition::PerformPendingOperations() {
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT(mDocument->GetActiveViewTransition() == this);

  // Flush the update callback queue.
  // Note: this ensures that any changes to the DOM scheduled by other skipped
  // transitions are done before the old state for this transition is captured.
  // https://github.com/w3c/csswg-drafts/issues/11943
  RefPtr doc = mDocument;
  doc->FlushViewTransitionUpdateCallbackQueue();

  switch (mPhase) {
    case Phase::PendingCapture:
      return Setup();
    case Phase::Animating:
      return HandleFrame();
    default:
      break;
  }
}

// https://drafts.csswg.org/css-view-transitions/#snapshot-containing-block
nsRect ViewTransition::SnapshotContainingBlockRect(nsPresContext* aPc) {
  return aPc ? aPc->GetVisibleArea() : nsRect();
}

// https://drafts.csswg.org/css-view-transitions/#snapshot-containing-block
nsRect ViewTransition::SnapshotContainingBlockRect() const {
  nsPresContext* pc = mDocument->GetPresContext();
  return SnapshotContainingBlockRect(pc);
}

Element* ViewTransition::FindPseudo(const PseudoStyleRequest& aRequest) const {
  Element* root = GetRoot();
  if (!root) {
    return nullptr;
  }

  if (aRequest.mType == PseudoStyleType::viewTransition) {
    return root;
  }

  // Linear search ::view-transition-group by |aRequest.mIdentifier|.
  // Note: perhaps we can add a hashtable to improve the performance if it's
  // common that there are a lot of view-transition-names.
  Element* group = root->GetFirstElementChild();
  for (; group; group = group->GetNextElementSibling()) {
    MOZ_ASSERT(group->HasName(),
               "The generated ::view-transition-group() should have a name");
    nsAtom* name = group->GetParsedAttr(nsGkAtoms::name)->GetAtomValue();
    if (name == aRequest.mIdentifier) {
      break;
    }
  }

  // No one specifies view-transition-name or we mismatch all names.
  if (!group) {
    return nullptr;
  }

  if (aRequest.mType == PseudoStyleType::viewTransitionGroup) {
    return group;
  }

  Element* imagePair = group->GetFirstElementChild();
  MOZ_ASSERT(imagePair, "::view-transition-image-pair() should exist always");
  if (aRequest.mType == PseudoStyleType::viewTransitionImagePair) {
    return imagePair;
  }

  Element* child = imagePair->GetFirstElementChild();
  // Neither ::view-transition-old() nor ::view-transition-new() doesn't exist.
  if (!child) {
    return nullptr;
  }

  // Check if the first element matches our request.
  const PseudoStyleType type = child->GetPseudoElementType();
  if (type == aRequest.mType) {
    return child;
  }

  // Since the second child is either ::view-transition-new() or nullptr, so we
  // can reject viewTransitionOld request here.
  if (aRequest.mType == PseudoStyleType::viewTransitionOld) {
    return nullptr;
  }

  child = child->GetNextElementSibling();
  MOZ_ASSERT(aRequest.mType == PseudoStyleType::viewTransitionNew);
  MOZ_ASSERT(!child || !child->GetNextElementSibling(),
             "No more psuedo elements in this subtree");
  return child;
}

const StyleLockedDeclarationBlock* ViewTransition::GetDynamicRuleFor(
    const Element& aElement) const {
  if (!aElement.HasName()) {
    return nullptr;
  }
  nsAtom* name = aElement.GetParsedAttr(nsGkAtoms::name)->GetAtomValue();
  auto* capture = mNamedElements.Get(name);
  if (!capture) {
    return nullptr;
  }

  switch (aElement.GetPseudoElementType()) {
    case PseudoStyleType::viewTransitionNew:
      return capture->mNewRule.get();
    case PseudoStyleType::viewTransitionOld:
      return capture->mOldRule.get();
    case PseudoStyleType::viewTransitionImagePair:
      return capture->mImagePairRule.get();
    case PseudoStyleType::viewTransitionGroup:
      return capture->mGroupRule.get();
    default:
      return nullptr;
  }
}

// FIXME(emilio): This should actually iterate in paint order.
template <typename Callback>
static bool ForEachChildFrame(nsIFrame* aFrame, const Callback& aCb) {
  if (!aCb(aFrame)) {
    return false;
  }
  for (auto& [list, id] : aFrame->ChildLists()) {
    for (nsIFrame* f : list) {
      if (!ForEachChildFrame(f, aCb)) {
        return false;
      }
    }
  }
  return true;
}

template <typename Callback>
static void ForEachFrame(Document* aDoc, const Callback& aCb) {
  PresShell* ps = aDoc->GetPresShell();
  if (!ps) {
    return;
  }
  nsIFrame* root = ps->GetRootFrame();
  if (!root) {
    return;
  }
  ForEachChildFrame(root, aCb);
}

// https://drafts.csswg.org/css-view-transitions-1/#document-scoped-view-transition-name
static nsAtom* DocumentScopedTransitionNameFor(nsIFrame* aFrame) {
  auto* name = aFrame->StyleUIReset()->mViewTransitionName._0.AsAtom();
  if (name->IsEmpty()) {
    return nullptr;
  }
  // TODO(emilio): This isn't quite correct, per spec we're supposed to only
  // honor names coming from the document, but that's quite some magic,
  // and it's getting actively discussed, see:
  // https://github.com/w3c/csswg-drafts/issues/10808 and related
  return name;
}

// https://drafts.csswg.org/css-view-transitions/#capture-the-old-state
Maybe<SkipTransitionReason> ViewTransition::CaptureOldState() {
  MOZ_ASSERT(mNamedElements.IsEmpty());

  // Steps 1/2 are variable declarations.
  // Step 3: Let usedTransitionNames be a new set of strings.
  nsTHashSet<nsAtom*> usedTransitionNames;
  // Step 4: Let captureElements be a new list of elements.
  AutoTArray<std::pair<nsIFrame*, nsAtom*>, 32> captureElements;

  // Step 5: If the snapshot containing block size exceeds an
  // implementation-defined maximum, then return failure.
  // TODO(emilio): Implement a maximum if we deem it needed.
  //
  // Step 6: Set transition's initial snapshot containing block size to the
  // snapshot containing block size.
  mInitialSnapshotContainingBlockSize = SnapshotContainingBlockRect().Size();

  // Step 7: For each element of every element that is connected, and has a node
  // document equal to document, in paint order:
  Maybe<SkipTransitionReason> result;
  ForEachFrame(mDocument, [&](nsIFrame* aFrame) {
    auto* name = DocumentScopedTransitionNameFor(aFrame);
    if (!name) {
      // As a fast path we check for v-t-n first.
      // If transitionName is none, or element is not rendered, then continue.
      return true;
    }
    if (aFrame->IsHiddenByContentVisibilityOnAnyAncestor()) {
      // If any flat tree ancestor of this element skips its contents, then
      // continue.
      return true;
    }
    if (aFrame->GetPrevContinuation() || aFrame->GetNextContinuation()) {
      // If element has more than one box fragment, then continue.
      return true;
    }
    if (!usedTransitionNames.EnsureInserted(name)) {
      // If usedTransitionNames contains transitionName, then return failure.
      result.emplace(
          SkipTransitionReason::DuplicateTransitionNameCapturingOldState);
      return false;
    }
    SetCaptured(aFrame, true);
    captureElements.AppendElement(std::make_pair(aFrame, name));
    return true;
  });

  if (result) {
    for (auto& [f, name] : captureElements) {
      SetCaptured(f, false);
    }
    return result;
  }

  // Step 8: For each element in captureElements:
  // Step 9: For each element in captureElements, set element's captured
  // in a view transition to false.
  for (auto& [f, name] : captureElements) {
    MOZ_ASSERT(f);
    MOZ_ASSERT(f->GetContent()->IsElement());
    auto capture =
        MakeUnique<CapturedElement>(f, mInitialSnapshotContainingBlockSize);
    mNamedElements.InsertOrUpdate(name, std::move(capture));
    mNames.AppendElement(name);
  }

  if (StaticPrefs::dom_viewTransitions_wr_old_capture()) {
    // When snapshotting an iframe, we need to paint from the root subdoc.
    if (RefPtr<PresShell> ps =
            nsContentUtils::GetInProcessSubtreeRootDocument(mDocument)
                ->GetPresShell()) {
      VT_LOG("ViewTransitions::CaptureOldState(), requesting composite");
      // Build a display list and send it to WR in order to perform the
      // capturing of old content.
      RefPtr<nsViewManager> vm = ps->GetViewManager();
      ps->PaintAndRequestComposite(vm->GetRootView(),
                                   PaintFlags::PaintCompositeOffscreen);
      VT_LOG("ViewTransitions::CaptureOldState(), requesting composite end");
    }
  }

  for (auto& [f, name] : captureElements) {
    SetCaptured(f, false);
  }
  return result;
}

// https://drafts.csswg.org/css-view-transitions-1/#capture-the-new-state
Maybe<SkipTransitionReason> ViewTransition::CaptureNewState() {
  nsTHashSet<nsAtom*> usedTransitionNames;
  Maybe<SkipTransitionReason> result;
  ForEachFrame(mDocument, [&](nsIFrame* aFrame) {
    // As a fast path we check for v-t-n first.
    auto* name = DocumentScopedTransitionNameFor(aFrame);
    if (!name) {
      return true;
    }
    if (aFrame->IsHiddenByContentVisibilityOnAnyAncestor()) {
      // If any flat tree ancestor of this element skips its contents, then
      // continue.
      return true;
    }
    if (aFrame->GetPrevContinuation() || aFrame->GetNextContinuation()) {
      // If element has more than one box fragment, then continue.
      return true;
    }
    if (!usedTransitionNames.EnsureInserted(name)) {
      result.emplace(
          SkipTransitionReason::DuplicateTransitionNameCapturingNewState);
      return false;
    }
    bool wasPresent = true;
    auto& capturedElement = mNamedElements.LookupOrInsertWith(name, [&] {
      wasPresent = false;
      return MakeUnique<CapturedElement>();
    });
    if (!wasPresent) {
      mNames.AppendElement(name);
    }
    capturedElement->mNewElement = aFrame->GetContent()->AsElement();
    capturedElement->mNewSnapshotSize =
        aFrame->InkOverflowRectRelativeToSelf().Size();
    SetCaptured(aFrame, true);
    return true;
  });
  return result;
}

// https://drafts.csswg.org/css-view-transitions/#setup-view-transition
void ViewTransition::Setup() {
  // Step 2: Capture the old state for transition.
  if (auto skipReason = CaptureOldState()) {
    // If failure is returned, then skip the view transition for transition
    // with an "InvalidStateError" DOMException in transition’s relevant Realm,
    // and return.
    return SkipTransition(*skipReason);
  }

  // Step 3: Set document’s rendering suppression for view transitions to true.
  mDocument->SetRenderingSuppressedForViewTransitions(true);

  // Step 4: Queue a global task on the DOM manipulation task source, given
  // transition's relevant global object, to perform the following steps:
  //   4.1: If transition's phase is "done", then abort these steps.
  //   4.2: Schedule the update callback for transition.
  //   4.3: Flush the update callback queue.
  mDocument->Dispatch(
      NewRunnableMethod("ViewTransition::MaybeScheduleUpdateCallback", this,
                        &ViewTransition::MaybeScheduleUpdateCallback));
}

// https://drafts.csswg.org/css-view-transitions-1/#handle-transition-frame
void ViewTransition::HandleFrame() {
  // Steps 1-3: Steps 1-3: Compute active animations.
  const bool hasActiveAnimations = CheckForActiveAnimations();

  // Step 4: If hasActiveAnimations is false:
  if (!hasActiveAnimations) {
    // 4.1: Set transition's phase to "done".
    mPhase = Phase::Done;
    // 4.2: Clear view transition transition.
    ClearActiveTransition(false);
    // 4.3: Resolve transition's finished promise.
    if (Promise* finished = GetFinished(IgnoreErrors())) {
      finished->MaybeResolveWithUndefined();
    }
    return;
  }
  // Step 5: If transition’s initial snapshot containing block size is not equal
  // to the snapshot containing block size, then skip the view transition for
  // transition with an "InvalidStateError" DOMException in transition’s
  // relevant Realm, and return.
  if (SnapshotContainingBlockRect().Size() !=
      mInitialSnapshotContainingBlockSize) {
    SkipTransition(SkipTransitionReason::Resize);
    return;
  }

  // Step 6: Update pseudo-element styles for transition.
  if (!UpdatePseudoElementStyles(/* aNeedsInvalidation= */ true)) {
    // If failure is returned, then skip the view transition for transition
    // with an "InvalidStateError" DOMException in transition's relevant Realm,
    // and return.
    return SkipTransition(SkipTransitionReason::PseudoUpdateFailure);
  }

  // If the view transition is still animating after HandleFrame(), we have to
  // periodically perform operations to check if it is still animating in the
  // following ticks.
  mDocument->EnsureViewTransitionOperationsHappen();
}

static bool CheckForActiveAnimationsForEachPseudo(
    const Element& aRoot, const AnimationTimeline& aDocTimeline,
    const AnimationEventDispatcher& aDispatcher,
    PseudoStyleRequest&& aRequest) {
  // Check EffectSet because an Animation (either a CSS Animations or a
  // script animation) is associated with a KeyframeEffect. If the animation
  // doesn't have an associated effect, we can skip it per spec.
  // If the effect target is not the element we request, it shouldn't be in
  // |effects| either.
  EffectSet* effects = EffectSet::Get(&aRoot, aRequest);
  if (!effects) {
    return false;
  }

  for (const auto* effect : *effects) {
    // 3.1: For each animation whose timeline is a document timeline associated
    // with document, and contains at least one associated effect whose effect
    // target is element, set hasActiveAnimations to true if any of the
    // following conditions is true:
    //   * animation’s play state is paused or running.
    //   * document’s pending animation event queue has any events associated
    //     with animation.

    MOZ_ASSERT(effect && effect->GetAnimation(),
               "Only effects associated with an animation should be "
               "added to an element's effect set");
    const Animation* anim = effect->GetAnimation();

    // The animation's timeline is not the document timeline.
    if (anim->GetTimeline() != &aDocTimeline) {
      continue;
    }

    // Return true if any of the following conditions is true:
    // 1. animation’s play state is paused or running.
    // 2. document’s pending animation event queue has any events associated
    //    with animation.
    const auto playState = anim->PlayState();
    if (playState != AnimationPlayState::Paused &&
        playState != AnimationPlayState::Running &&
        !aDispatcher.HasQueuedEventsFor(anim)) {
      continue;
    }
    return true;
  }
  return false;
}

// This is the implementation of step 3 in HandleFrame(). For each element of
// transition’s transition root pseudo-element’s inclusive descendants, we check
// if it has active animations.
bool ViewTransition::CheckForActiveAnimations() const {
  MOZ_ASSERT(mDocument);

  if (StaticPrefs::dom_viewTransitions_remain_active()) {
    return true;
  }

  const Element* root = mDocument->GetRootElement();
  if (!root) {
    // The documentElement could be removed during animating via script.
    return false;
  }

  const AnimationTimeline* timeline = mDocument->Timeline();
  if (!timeline) {
    return false;
  }

  nsPresContext* presContext = mDocument->GetPresContext();
  if (!presContext) {
    return false;
  }

  const AnimationEventDispatcher* dispatcher =
      presContext->AnimationEventDispatcher();
  MOZ_ASSERT(dispatcher);

  auto checkForEachPseudo = [&](PseudoStyleRequest&& aRequest) {
    return CheckForActiveAnimationsForEachPseudo(*root, *timeline, *dispatcher,
                                                 std::move(aRequest));
  };

  bool hasActiveAnimations =
      checkForEachPseudo(PseudoStyleRequest(PseudoStyleType::viewTransition));
  for (nsAtom* name : mNamedElements.Keys()) {
    if (hasActiveAnimations) {
      break;
    }

    hasActiveAnimations =
        checkForEachPseudo({PseudoStyleType::viewTransitionGroup, name}) ||
        checkForEachPseudo({PseudoStyleType::viewTransitionImagePair, name}) ||
        checkForEachPseudo({PseudoStyleType::viewTransitionOld, name}) ||
        checkForEachPseudo({PseudoStyleType::viewTransitionNew, name});
  }
  return hasActiveAnimations;
}

void ViewTransition::ClearNamedElements() {
  for (auto& entry : mNamedElements) {
    if (auto* element = entry.GetData()->mNewElement.get()) {
      if (nsIFrame* f = element->GetPrimaryFrame()) {
        SetCaptured(f, false);
      }
    }
  }
  mNamedElements.Clear();
  mNames.Clear();
}

static void ClearViewTransitionsAnimationData(Element* aRoot) {
  if (!aRoot) {
    return;
  }

  auto* data = aRoot->GetAnimationData();
  if (!data) {
    return;
  }
  data->ClearViewTransitionPseudos();
}

// https://drafts.csswg.org/css-view-transitions-1/#clear-view-transition
void ViewTransition::ClearActiveTransition(bool aIsDocumentHidden) {
  // Steps 1-2
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT(mDocument->GetActiveViewTransition() == this);

  // Step 3
  ClearNamedElements();

  // Step 4: Clear show transition tree flag (we just destroy the pseudo tree,
  // see SetupTransitionPseudoElements).
  if (mViewTransitionRoot) {
    nsAutoScriptBlocker scriptBlocker;
    if (mDocument->DevToolsAnonymousAndShadowEventsEnabled()) {
      mViewTransitionRoot->QueueDevtoolsAnonymousEvent(/* aIsRemove = */ true);
    }
    if (PresShell* ps = mDocument->GetPresShell()) {
      ps->ContentWillBeRemoved(mViewTransitionRoot, nullptr);
    }
    mViewTransitionRoot->UnbindFromTree();
    mViewTransitionRoot = nullptr;

    // If the document is being destroyed, we cannot get the animation data
    // (e.g. it may crash when using nsINode::GetBoolFlag()), so we have to skip
    // this case. It's fine because those animations should still be stopped and
    // removed if no frame there.
    //
    // Another case is that the document is hidden. In that case, we don't setup
    // the pseudo elements, so it's fine to skip it as well.
    if (!aIsDocumentHidden) {
      ClearViewTransitionsAnimationData(mDocument->GetRootElement());
    }
  }
  mDocument->ClearActiveViewTransition();
}

void ViewTransition::SkipTransition(SkipTransitionReason aReason) {
  SkipTransition(aReason, JS::UndefinedHandleValue);
}

// https://drafts.csswg.org/css-view-transitions-1/#skip-the-view-transition
// https://drafts.csswg.org/css-view-transitions-1/#dom-viewtransition-skiptransition
void ViewTransition::SkipTransition(
    SkipTransitionReason aReason,
    JS::Handle<JS::Value> aUpdateCallbackRejectReason) {
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT_IF(aReason != SkipTransitionReason::JS, mPhase != Phase::Done);
  MOZ_ASSERT_IF(aReason != SkipTransitionReason::UpdateCallbackRejected,
                aUpdateCallbackRejectReason == JS::UndefinedHandleValue);
  if (mPhase == Phase::Done) {
    return;
  }
  // Step 3: If transition’s phase is before "update-callback-called", then
  // schedule the update callback for transition.
  if (UnderlyingValue(mPhase) < UnderlyingValue(Phase::UpdateCallbackCalled)) {
    mDocument->ScheduleViewTransitionUpdateCallback(this);
  }

  // Step 4: Set rendering suppression for view transitions to false.
  mDocument->SetRenderingSuppressedForViewTransitions(false);

  // Step 5: If document's active view transition is transition, Clear view
  // transition transition.
  if (mDocument->GetActiveViewTransition() == this) {
    ClearActiveTransition(aReason == SkipTransitionReason::DocumentHidden);
  }

  // Step 6: Set transition's phase to "done".
  mPhase = Phase::Done;

  // Step 7: Reject transition's ready promise with reason.
  Promise* ucd = GetUpdateCallbackDone(IgnoreErrors());
  if (Promise* readyPromise = GetReady(IgnoreErrors())) {
    switch (aReason) {
      case SkipTransitionReason::JS:
        readyPromise->MaybeRejectWithAbortError(
            "Skipped ViewTransition due to skipTransition() call");
        break;
      case SkipTransitionReason::ClobberedActiveTransition:
        readyPromise->MaybeRejectWithAbortError(
            "Skipped ViewTransition due to another transition starting");
        break;
      case SkipTransitionReason::DocumentHidden:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Skipped ViewTransition due to document being hidden");
        break;
      case SkipTransitionReason::Timeout:
        readyPromise->MaybeRejectWithTimeoutError(
            "Skipped ViewTransition due to timeout");
        break;
      case SkipTransitionReason::DuplicateTransitionNameCapturingOldState:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Duplicate view-transition-name value while capturing old state");
        break;
      case SkipTransitionReason::DuplicateTransitionNameCapturingNewState:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Duplicate view-transition-name value while capturing new state");
        break;
      case SkipTransitionReason::Resize:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Skipped view transition due to viewport resize");
        break;
      case SkipTransitionReason::PseudoUpdateFailure:
        readyPromise->MaybeRejectWithInvalidStateError(
            "Skipped view transition due to hidden new element");
        break;
      case SkipTransitionReason::UpdateCallbackRejected:
        readyPromise->MaybeReject(aUpdateCallbackRejectReason);

        // Step 8, The case we have to reject the finished promise. Do this here
        // to make sure it reacts to UpdateCallbackRejected.
        //
        // Note: we intentionally reject the finished promise after the ready
        // promise to make sure the order of promise callbacks is correct in
        // script.
        if (ucd) {
          MOZ_ASSERT(ucd->State() == Promise::PromiseState::Rejected);
          if (Promise* finished = GetFinished(IgnoreErrors())) {
            // Since the rejection of transition’s update callback done promise
            // isn’t explicitly handled here, if transition’s update callback
            // done promise rejects, then transition’s finished promise will
            // reject with the same reason.
            finished->MaybeReject(aUpdateCallbackRejectReason);
          }
        }
        break;
    }
  }

  // Step 8: Resolve transition's finished promise with the result of reacting
  // to transition's update callback done promise:
  // Note: It is not guaranteed that |mPhase| is Done in CallUpdateCallback().
  // There are two possible cases:
  // 1. If we skip the view transitions before updateCallbackDone callback
  //    is dispatched, we come here first. In this case we don't have to resolve
  //    the finsihed promise because CallUpdateCallback() will do it.
  // 2. If we skip the view transitions after updateCallbackDone callback, the
  //    finished promise hasn't been resolved because |mPhase| is not Done (i.e.
  //    |mPhase| is UpdateCallbackCalled) when we handle updateCallbackDone
  //    callback. Therefore, we have to resolve the finished promise based on
  //    the PromiseState of |mUpdateCallbackDone|.
  if (ucd && ucd->State() == Promise::PromiseState::Resolved) {
    if (Promise* finished = GetFinished(IgnoreErrors())) {
      // If the promise was fulfilled, then return undefined.
      finished->MaybeResolveWithUndefined();
    }
  }
}

JSObject* ViewTransition::WrapObject(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return ViewTransition_Binding::Wrap(aCx, this, aGivenProto);
}

};  // namespace mozilla::dom
