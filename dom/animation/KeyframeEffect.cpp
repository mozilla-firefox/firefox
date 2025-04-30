/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/KeyframeEffect.h"

#include "mozilla/dom/Animation.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/KeyframeAnimationOptionsBinding.h"
// For UnrestrictedDoubleOrKeyframeAnimationOptions;
#include "mozilla/dom/KeyframeEffectBinding.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/layers/AnimationInfo.h"
#include "mozilla/AnimationUtils.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/ComputedStyleInlines.h"
#include "mozilla/EffectSet.h"
#include "mozilla/FloatingPoint.h"  // For IsFinite
#include "mozilla/LayerAnimationInfo.h"
#include "mozilla/LookAndFeel.h"  // For LookAndFeel::GetInt
#include "mozilla/KeyframeUtils.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/SVGObserverUtils.h"
#include "nsCSSPropertyID.h"
#include "nsComputedDOMStyle.h"  // nsComputedDOMStyle::GetComputedStyle
#include "nsContentUtils.h"
#include "nsCSSPropertyIDSet.h"
#include "nsCSSProps.h"             // For nsCSSProps::PropHasFlags
#include "nsCSSPseudoElements.h"    // For PseudoStyleType
#include "nsDOMMutationObserver.h"  // For nsAutoAnimationMutationBatch
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsPresContextInlines.h"
#include "nsRefreshDriver.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "WindowRenderer.h"

namespace mozilla {

void AnimationProperty::SetPerformanceWarning(
    const AnimationPerformanceWarning& aWarning, const dom::Element* aElement) {
  if (mPerformanceWarning && *mPerformanceWarning == aWarning) {
    return;
  }

  mPerformanceWarning = Some(aWarning);

  nsAutoString localizedString;
  if (StaticPrefs::layers_offmainthreadcomposition_log_animations() &&
      mPerformanceWarning->ToLocalizedString(localizedString)) {
    nsAutoCString logMessage = NS_ConvertUTF16toUTF8(localizedString);
    AnimationUtils::LogAsyncAnimationFailure(logMessage, aElement);
  }
}

bool PropertyValuePair::operator==(const PropertyValuePair& aOther) const {
  if (mProperty != aOther.mProperty) {
    return false;
  }
  if (mServoDeclarationBlock == aOther.mServoDeclarationBlock) {
    return true;
  }
  if (!mServoDeclarationBlock || !aOther.mServoDeclarationBlock) {
    return false;
  }
  return Servo_DeclarationBlock_Equals(mServoDeclarationBlock,
                                       aOther.mServoDeclarationBlock);
}

namespace dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(KeyframeEffect, AnimationEffect,
                                   mTarget.mElement)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(KeyframeEffect, AnimationEffect)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(KeyframeEffect)
NS_INTERFACE_MAP_END_INHERITING(AnimationEffect)

NS_IMPL_ADDREF_INHERITED(KeyframeEffect, AnimationEffect)
NS_IMPL_RELEASE_INHERITED(KeyframeEffect, AnimationEffect)

KeyframeEffect::KeyframeEffect(Document* aDocument,
                               OwningAnimationTarget&& aTarget,
                               TimingParams&& aTiming,
                               const KeyframeEffectParams& aOptions)
    : AnimationEffect(aDocument, std::move(aTiming)),
      mTarget(std::move(aTarget)),
      mEffectOptions(aOptions) {}

KeyframeEffect::KeyframeEffect(Document* aDocument,
                               OwningAnimationTarget&& aTarget,
                               const KeyframeEffect& aOther)
    : AnimationEffect(aDocument, TimingParams{aOther.SpecifiedTiming()}),
      mTarget(std::move(aTarget)),
      mEffectOptions{aOther.IterationComposite(), aOther.Composite(),
                     mTarget.mPseudoRequest},
      mKeyframes(aOther.mKeyframes.Clone()),
      mProperties(aOther.mProperties.Clone()),
      mBaseValues(aOther.mBaseValues.Clone()) {}

JSObject* KeyframeEffect::WrapObject(JSContext* aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  return KeyframeEffect_Binding::Wrap(aCx, this, aGivenProto);
}

IterationCompositeOperation KeyframeEffect::IterationComposite() const {
  return mEffectOptions.mIterationComposite;
}

void KeyframeEffect::SetIterationComposite(
    const IterationCompositeOperation& aIterationComposite) {
  if (mEffectOptions.mIterationComposite == aIterationComposite) {
    return;
  }

  if (mAnimation && mAnimation->IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(mAnimation);
  }

  mEffectOptions.mIterationComposite = aIterationComposite;
  RequestRestyle(EffectCompositor::RestyleType::Layer);
}

CompositeOperation KeyframeEffect::Composite() const {
  return mEffectOptions.mComposite;
}

void KeyframeEffect::SetComposite(const CompositeOperation& aComposite) {
  if (mEffectOptions.mComposite == aComposite) {
    return;
  }

  mEffectOptions.mComposite = aComposite;

  if (mAnimation && mAnimation->IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(mAnimation);
  }

  if (mTarget) {
    RefPtr<const ComputedStyle> computedStyle =
        GetTargetComputedStyle(Flush::None);
    if (computedStyle) {
      UpdateProperties(computedStyle);
    }
  }
}

void KeyframeEffect::NotifySpecifiedTimingUpdated() {
  // Use the same document for a pseudo element and its parent element.
  // Use nullptr if we don't have mTarget, so disable the mutation batch.
  nsAutoAnimationMutationBatch mb(mTarget ? mTarget.mElement->OwnerDoc()
                                          : nullptr);

  if (mAnimation) {
    mAnimation->NotifyEffectTimingUpdated();

    if (mAnimation->IsRelevant()) {
      MutationObservers::NotifyAnimationChanged(mAnimation);
    }

    RequestRestyle(EffectCompositor::RestyleType::Layer);
  }
}

void KeyframeEffect::NotifyAnimationTimingUpdated(
    PostRestyleMode aPostRestyle) {
  UpdateTargetRegistration();

  // If the effect is not relevant it will be removed from the target
  // element's effect set. However, effects not in the effect set
  // will not be included in the set of candidate effects for running on
  // the compositor and hence they won't have their compositor status
  // updated. As a result, we need to make sure we clear their compositor
  // status here.
  bool isRelevant = mAnimation && mAnimation->IsRelevant();
  if (!isRelevant) {
    ResetIsRunningOnCompositor();
  }

  // Request restyle if necessary.
  if (aPostRestyle == PostRestyleMode::IfNeeded && mAnimation &&
      !mProperties.IsEmpty() && HasComputedTimingChanged()) {
    EffectCompositor::RestyleType restyleType =
        CanThrottle() ? EffectCompositor::RestyleType::Throttled
                      : EffectCompositor::RestyleType::Standard;
    RequestRestyle(restyleType);
  }

  // Detect changes to "in effect" status since we need to recalculate the
  // animation cascade for this element whenever that changes.
  // Note that updating mInEffectOnLastAnimationTimingUpdate has to be done
  // after above CanThrottle() call since the function uses the flag inside it.
  bool inEffect = IsInEffect();
  if (inEffect != mInEffectOnLastAnimationTimingUpdate) {
    MarkCascadeNeedsUpdate();
    mInEffectOnLastAnimationTimingUpdate = inEffect;
  }

  // If we're no longer "in effect", our ComposeStyle method will never be
  // called and we will never have a chance to update mProgressOnLastCompose
  // and mCurrentIterationOnLastCompose.
  // We clear them here to ensure that if we later become "in effect" we will
  // request a restyle (above).
  if (!inEffect) {
    mProgressOnLastCompose.SetNull();
    mCurrentIterationOnLastCompose = 0;
  }
}

static bool KeyframesEqualIgnoringComputedOffsets(
    const nsTArray<Keyframe>& aLhs, const nsTArray<Keyframe>& aRhs) {
  if (aLhs.Length() != aRhs.Length()) {
    return false;
  }

  for (size_t i = 0, len = aLhs.Length(); i < len; ++i) {
    const Keyframe& a = aLhs[i];
    const Keyframe& b = aRhs[i];
    if (a.mOffset != b.mOffset || a.mTimingFunction != b.mTimingFunction ||
        a.mPropertyValues != b.mPropertyValues) {
      return false;
    }
  }
  return true;
}

// https://drafts.csswg.org/web-animations/#dom-keyframeeffect-setkeyframes
void KeyframeEffect::SetKeyframes(JSContext* aContext,
                                  JS::Handle<JSObject*> aKeyframes,
                                  ErrorResult& aRv) {
  nsTArray<Keyframe> keyframes = KeyframeUtils::GetKeyframesFromObject(
      aContext, mDocument, aKeyframes, "KeyframeEffect.setKeyframes", aRv);
  if (aRv.Failed()) {
    return;
  }

  RefPtr<const ComputedStyle> style = GetTargetComputedStyle(Flush::None);
  SetKeyframes(std::move(keyframes), style, nullptr /* AnimationTimeline */);
}

void KeyframeEffect::SetKeyframes(nsTArray<Keyframe>&& aKeyframes,
                                  const ComputedStyle* aStyle,
                                  const AnimationTimeline* aTimeline) {
  if (KeyframesEqualIgnoringComputedOffsets(aKeyframes, mKeyframes)) {
    return;
  }

  mKeyframes = std::move(aKeyframes);
  KeyframeUtils::DistributeKeyframes(mKeyframes);

  if (mAnimation && mAnimation->IsRelevant()) {
    MutationObservers::NotifyAnimationChanged(mAnimation);
  }

  // We need to call UpdateProperties() unless the target element doesn't have
  // style (e.g. the target element is not associated with any document).
  if (aStyle) {
    UpdateProperties(aStyle, aTimeline);
  }
}

void KeyframeEffect::ReplaceTransitionStartValue(AnimationValue&& aStartValue) {
  if (!aStartValue.mServo) {
    return;
  }

  // A typical transition should have a single property and a single segment.
  //
  // (And for atypical transitions, that is, those updated by script, we don't
  // apply the replacing behavior.)
  if (mProperties.Length() != 1 || mProperties[0].mSegments.Length() != 1) {
    return;
  }

  // Likewise, check that the keyframes are of the expected shape.
  if (mKeyframes.Length() != 2 || mKeyframes[0].mPropertyValues.Length() != 1) {
    return;
  }

  // Check that the value we are about to substitute in is actually for the
  // same property.
  AnimatedPropertyID property(eCSSProperty_UNKNOWN);
  Servo_AnimationValue_GetPropertyId(aStartValue.mServo, &property);
  if (property != mProperties[0].mProperty) {
    return;
  }

  mKeyframes[0].mPropertyValues[0].mServoDeclarationBlock =
      Servo_AnimationValue_Uncompute(aStartValue.mServo).Consume();
  mProperties[0].mSegments[0].mFromValue = std::move(aStartValue);
}

static bool IsEffectiveProperty(const EffectSet& aEffects,
                                const AnimatedPropertyID& aProperty) {
  return !aEffects.PropertiesWithImportantRules().HasProperty(aProperty) ||
         !aEffects.PropertiesForAnimationsLevel().HasProperty(aProperty);
}

const AnimationProperty* KeyframeEffect::GetEffectiveAnimationOfProperty(
    const AnimatedPropertyID& aProperty, const EffectSet& aEffects) const {
  MOZ_ASSERT(mTarget && &aEffects == EffectSet::Get(mTarget));

  for (const AnimationProperty& property : mProperties) {
    if (aProperty != property.mProperty) {
      continue;
    }

    // Only include the property if it is not overridden by !important rules in
    // the transitions level.
    return IsEffectiveProperty(aEffects, property.mProperty) ? &property
                                                             : nullptr;
  }
  return nullptr;
}

bool KeyframeEffect::HasEffectiveAnimationOfPropertySet(
    const nsCSSPropertyIDSet& aPropertySet, const EffectSet& aEffectSet) const {
  for (const AnimationProperty& property : mProperties) {
    if (aPropertySet.HasProperty(property.mProperty) &&
        IsEffectiveProperty(aEffectSet, property.mProperty)) {
      return true;
    }
  }
  return false;
}

nsCSSPropertyIDSet KeyframeEffect::GetPropertiesForCompositor(
    EffectSet& aEffects, const nsIFrame* aFrame) const {
  MOZ_ASSERT(&aEffects == EffectSet::Get(mTarget));

  nsCSSPropertyIDSet properties;

  if (!mAnimation || !mAnimation->IsRelevant()) {
    return properties;
  }

  static constexpr nsCSSPropertyIDSet compositorAnimatables =
      nsCSSPropertyIDSet::CompositorAnimatables();
  static constexpr nsCSSPropertyIDSet transformLikeProperties =
      nsCSSPropertyIDSet::TransformLikeProperties();

  nsCSSPropertyIDSet transformSet;
  AnimationPerformanceWarning::Type dummyWarning;

  for (const AnimationProperty& property : mProperties) {
    if (!compositorAnimatables.HasProperty(property.mProperty)) {
      continue;
    }

    // Transform-like properties are combined together on the compositor so we
    // need to evaluate them as a group. We build up a separate set here then
    // evaluate it as a separate step below.
    if (transformLikeProperties.HasProperty(property.mProperty)) {
      transformSet.AddProperty(property.mProperty.mID);
      continue;
    }

    KeyframeEffect::MatchForCompositor matchResult =
        IsMatchForCompositor(nsCSSPropertyIDSet{property.mProperty.mID}, aFrame,
                             aEffects, dummyWarning);
    if (matchResult ==
            KeyframeEffect::MatchForCompositor::NoAndBlockThisProperty ||
        matchResult == KeyframeEffect::MatchForCompositor::No) {
      continue;
    }
    properties.AddProperty(property.mProperty.mID);
  }

  if (!transformSet.IsEmpty()) {
    KeyframeEffect::MatchForCompositor matchResult =
        IsMatchForCompositor(transformSet, aFrame, aEffects, dummyWarning);
    if (matchResult == KeyframeEffect::MatchForCompositor::Yes ||
        matchResult == KeyframeEffect::MatchForCompositor::IfNeeded) {
      properties |= transformSet;
    }
  }

  return properties;
}

AnimatedPropertyIDSet KeyframeEffect::GetPropertySet() const {
  AnimatedPropertyIDSet result;

  for (const AnimationProperty& property : mProperties) {
    result.AddProperty(property.mProperty);
  }

  return result;
}

#ifdef DEBUG
bool SpecifiedKeyframeArraysAreEqual(const nsTArray<Keyframe>& aA,
                                     const nsTArray<Keyframe>& aB) {
  if (aA.Length() != aB.Length()) {
    return false;
  }

  for (size_t i = 0; i < aA.Length(); i++) {
    const Keyframe& a = aA[i];
    const Keyframe& b = aB[i];
    if (a.mOffset != b.mOffset || a.mTimingFunction != b.mTimingFunction ||
        a.mPropertyValues != b.mPropertyValues) {
      return false;
    }
  }

  return true;
}
#endif

static bool HasCurrentColor(
    const nsTArray<AnimationPropertySegment>& aSegments) {
  for (const AnimationPropertySegment& segment : aSegments) {
    if ((!segment.mFromValue.IsNull() && segment.mFromValue.IsCurrentColor()) ||
        (!segment.mToValue.IsNull() && segment.mToValue.IsCurrentColor())) {
      return true;
    }
  }
  return false;
}
void KeyframeEffect::UpdateProperties(const ComputedStyle* aStyle,
                                      const AnimationTimeline* aTimeline) {
  MOZ_ASSERT(aStyle);

  nsTArray<AnimationProperty> properties = BuildProperties(aStyle);

  bool propertiesChanged = mProperties != properties;

  // We need to update base styles even if any properties are not changed at all
  // since base styles might have been changed due to parent style changes, etc.
  bool baseStylesChanged = false;
  EnsureBaseStyles(aStyle, properties, aTimeline,
                   !propertiesChanged ? &baseStylesChanged : nullptr);

  if (!propertiesChanged) {
    if (baseStylesChanged) {
      RequestRestyle(EffectCompositor::RestyleType::Layer);
    }
    return;
  }

  // Preserve the state of the mIsRunningOnCompositor flag.
  nsCSSPropertyIDSet runningOnCompositorProperties;

  for (const AnimationProperty& property : mProperties) {
    if (property.mIsRunningOnCompositor) {
      runningOnCompositorProperties.AddProperty(property.mProperty.mID);
    }
  }

  mProperties = std::move(properties);
  UpdateEffectSet();

  mCumulativeChanges = {};
  for (AnimationProperty& property : mProperties) {
    property.mIsRunningOnCompositor =
        runningOnCompositorProperties.HasProperty(property.mProperty);
    CalculateCumulativeChangesForProperty(property);
  }

  MarkCascadeNeedsUpdate();

  if (mAnimation) {
    mAnimation->NotifyEffectPropertiesUpdated();
  }

  RequestRestyle(EffectCompositor::RestyleType::Layer);
}

void KeyframeEffect::UpdateBaseStyle(const ComputedStyle* aStyle) {
  EnsureBaseStyles(aStyle, BuildProperties(aStyle), nullptr, nullptr);
}

void KeyframeEffect::EnsureBaseStyles(
    const ComputedStyle* aComputedValues,
    const nsTArray<AnimationProperty>& aProperties,
    const AnimationTimeline* aTimeline, bool* aBaseStylesChanged) {
  if (aBaseStylesChanged != nullptr) {
    *aBaseStylesChanged = false;
  }

  if (!mTarget) {
    return;
  }

  BaseValuesHashmap previousBaseStyles;
  if (aBaseStylesChanged != nullptr) {
    previousBaseStyles = std::move(mBaseValues);
  }

  mBaseValues.Clear();

  nsPresContext* presContext =
      nsContentUtils::GetContextForContent(mTarget.mElement);
  // If |aProperties| is empty we're not going to dereference |presContext| so
  // we don't care if it is nullptr.
  //
  // We could just return early when |aProperties| is empty and save looking up
  // the pres context, but that won't save any effort normally since we don't
  // call this function if we have no keyframes to begin with. Furthermore, the
  // case where |presContext| is nullptr is so rare (we've only ever seen in
  // fuzzing, and even then we've never been able to reproduce it reliably)
  // it's not worth the runtime cost of an extra branch.
  MOZ_ASSERT(presContext || aProperties.IsEmpty(),
             "Typically presContext should not be nullptr but if it is"
             " we should have also failed to calculate the computed values"
             " passed-in as aProperties");

  if (!aTimeline) {
    // If we pass a valid timeline, we use it (note: this happens when we create
    // a new animation or replace the old one, for CSS Animations and CSS
    // Transitions). Otherwise, we check the timeline from |mAnimation|.
    aTimeline = mAnimation ? mAnimation->GetTimeline() : nullptr;
  }

  RefPtr<const ComputedStyle> baseComputedStyle;
  for (const AnimationProperty& property : aProperties) {
    EnsureBaseStyle(property, presContext, aComputedValues, aTimeline,
                    baseComputedStyle);
  }

  if (aBaseStylesChanged != nullptr &&
      std::any_of(
          mBaseValues.cbegin(), mBaseValues.cend(), [&](const auto& entry) {
            return AnimationValue(entry.GetData()) !=
                   AnimationValue(previousBaseStyles.Get(entry.GetKey()));
          })) {
    *aBaseStylesChanged = true;
  }
}

void KeyframeEffect::EnsureBaseStyle(
    const AnimationProperty& aProperty, nsPresContext* aPresContext,
    const ComputedStyle* aComputedStyle, const AnimationTimeline* aTimeline,
    RefPtr<const ComputedStyle>& aBaseComputedStyle) {
  auto needBaseStyleForScrollTimeline =
      [this](const AnimationProperty& aProperty,
             const AnimationTimeline* aTimeline) {
        static constexpr TimeDuration zeroDuration;
        const TimingParams& timing = NormalizedTiming();
        // For scroll-timeline with a positive delay, it's possible to scroll
        // back and forth between delay phase and active phase, so we need to
        // keep its base style and maybe use it to override the animations in
        // delay on the compositor.
        return aTimeline && aTimeline->IsScrollTimeline() &&
               nsCSSPropertyIDSet::CompositorAnimatables().HasProperty(
                   aProperty.mProperty) &&
               (timing.Delay() > zeroDuration ||
                timing.EndDelay() > zeroDuration);
      };
  auto hasAdditiveValues = [](const AnimationProperty& aProperty) {
    for (const AnimationPropertySegment& segment : aProperty.mSegments) {
      if (!segment.HasReplaceableValues()) {
        return true;
      }
    }
    return false;
  };

  // Note: Check base style for compositor (i.e. for scroll-driven animations)
  // first because it is much cleaper.
  const bool needBaseStyle =
      needBaseStyleForScrollTimeline(aProperty, aTimeline) ||
      hasAdditiveValues(aProperty);
  if (!needBaseStyle) {
    return;
  }

  if (!aBaseComputedStyle) {
    MOZ_ASSERT(mTarget, "Should have a valid target");

    Element* animatingElement =
        mTarget.mElement->GetPseudoElement(mTarget.mPseudoRequest);
    if (!animatingElement) {
      return;
    }
    aBaseComputedStyle = aPresContext->StyleSet()->GetBaseContextForElement(
        animatingElement, aComputedStyle);
  }
  RefPtr<StyleAnimationValue> baseValue =
      Servo_ComputedValues_ExtractAnimationValue(aBaseComputedStyle,
                                                 &aProperty.mProperty)
          .Consume();
  mBaseValues.InsertOrUpdate(aProperty.mProperty, std::move(baseValue));
}

void KeyframeEffect::WillComposeStyle() {
  ComputedTiming computedTiming = GetComputedTiming();
  mProgressOnLastCompose = computedTiming.mProgress;
  mCurrentIterationOnLastCompose = computedTiming.mCurrentIteration;
}

void KeyframeEffect::ComposeStyleRule(StyleAnimationValueMap& aAnimationValues,
                                      const AnimationProperty& aProperty,
                                      const AnimationPropertySegment& aSegment,
                                      const ComputedTiming& aComputedTiming) {
  auto* opaqueTable =
      reinterpret_cast<RawServoAnimationValueTable*>(&mBaseValues);
  Servo_AnimationCompose(&aAnimationValues, opaqueTable, &aProperty.mProperty,
                         &aSegment, &aProperty.mSegments.LastElement(),
                         &aComputedTiming, mEffectOptions.mIterationComposite);
}

void KeyframeEffect::ComposeStyle(
    StyleAnimationValueMap& aComposeResult,
    const InvertibleAnimatedPropertyIDSet& aPropertiesToSkip,
    EndpointBehavior aEndpointBehavior) {
  ComputedTiming computedTiming = GetComputedTiming(nullptr, aEndpointBehavior);

  // If the progress is null, we don't have fill data for the current
  // time so we shouldn't animate.
  if (computedTiming.mProgress.IsNull()) {
    return;
  }

  for (size_t propIdx = 0, propEnd = mProperties.Length(); propIdx != propEnd;
       ++propIdx) {
    const AnimationProperty& prop = mProperties[propIdx];

    MOZ_ASSERT(prop.mSegments[0].mFromKey == 0.0, "incorrect first from key");
    MOZ_ASSERT(prop.mSegments[prop.mSegments.Length() - 1].mToKey == 1.0,
               "incorrect last to key");

    if (aPropertiesToSkip.HasProperty(prop.mProperty)) {
      continue;
    }

    MOZ_ASSERT(prop.mSegments.Length() > 0,
               "property should not be in animations if it has no segments");

    // FIXME: Maybe cache the current segment?
    const AnimationPropertySegment *segment = prop.mSegments.Elements(),
                                   *segmentEnd =
                                       segment + prop.mSegments.Length();
    while (segment->mToKey <= computedTiming.mProgress.Value()) {
      MOZ_ASSERT(segment->mFromKey <= segment->mToKey, "incorrect keys");
      if ((segment + 1) == segmentEnd) {
        break;
      }
      ++segment;
      MOZ_ASSERT(segment->mFromKey == (segment - 1)->mToKey, "incorrect keys");
    }
    MOZ_ASSERT(segment->mFromKey <= segment->mToKey, "incorrect keys");
    MOZ_ASSERT(segment >= prop.mSegments.Elements() &&
                   size_t(segment - prop.mSegments.Elements()) <
                       prop.mSegments.Length(),
               "out of array bounds");

    ComposeStyleRule(aComposeResult, prop, *segment, computedTiming);
  }

  // If the animation produces a change hint that affects the overflow region,
  // we need to record the current time to unthrottle the animation
  // periodically when the animation is being throttled because it's scrolled
  // out of view.
  if (HasPropertiesThatMightAffectOverflow()) {
    nsPresContext* presContext =
        nsContentUtils::GetContextForContent(mTarget.mElement);
    EffectSet* effectSet = EffectSet::Get(mTarget);
    if (presContext && effectSet) {
      TimeStamp now = presContext->RefreshDriver()->MostRecentRefresh();
      effectSet->UpdateLastOverflowAnimationSyncTime(now);
    }
  }
}

bool KeyframeEffect::IsRunningOnCompositor() const {
  // We consider animation is running on compositor if there is at least
  // one property running on compositor.
  // Animation.IsRunningOnCompotitor will return more fine grained
  // information in bug 1196114.
  for (const AnimationProperty& property : mProperties) {
    if (property.mIsRunningOnCompositor) {
      return true;
    }
  }
  return false;
}

void KeyframeEffect::SetIsRunningOnCompositor(nsCSSPropertyID aProperty,
                                              bool aIsRunning) {
  MOZ_ASSERT(aProperty != eCSSPropertyExtra_variable,
             "Can't animate variables on compositor");
  MOZ_ASSERT(
      nsCSSProps::PropHasFlags(aProperty, CSSPropFlags::CanAnimateOnCompositor),
      "Property being animated on compositor is not a recognized "
      "compositor-animatable property");

  for (AnimationProperty& property : mProperties) {
    if (property.mProperty.mID == aProperty) {
      property.mIsRunningOnCompositor = aIsRunning;
      // We currently only set a performance warning message when animations
      // cannot be run on the compositor, so if this animation is running
      // on the compositor we don't need a message.
      if (aIsRunning) {
        property.mPerformanceWarning.reset();
      } else if (mAnimation && mAnimation->IsPartialPrerendered()) {
        ResetPartialPrerendered();
      }
      return;
    }
  }
}

void KeyframeEffect::SetIsRunningOnCompositor(
    const nsCSSPropertyIDSet& aPropertySet, bool aIsRunning) {
  for (AnimationProperty& property : mProperties) {
    if (aPropertySet.HasProperty(property.mProperty)) {
      MOZ_ASSERT(nsCSSProps::PropHasFlags(property.mProperty.mID,
                                          CSSPropFlags::CanAnimateOnCompositor),
                 "Property being animated on compositor is a recognized "
                 "compositor-animatable property");
      property.mIsRunningOnCompositor = aIsRunning;
      // We currently only set a performance warning message when animations
      // cannot be run on the compositor, so if this animation is running
      // on the compositor we don't need a message.
      if (aIsRunning) {
        property.mPerformanceWarning.reset();
      }
    }
  }

  if (!aIsRunning && mAnimation && mAnimation->IsPartialPrerendered()) {
    ResetPartialPrerendered();
  }
}

void KeyframeEffect::ResetIsRunningOnCompositor() {
  for (AnimationProperty& property : mProperties) {
    property.mIsRunningOnCompositor = false;
  }

  if (mAnimation && mAnimation->IsPartialPrerendered()) {
    ResetPartialPrerendered();
  }
}

void KeyframeEffect::ResetPartialPrerendered() {
  MOZ_ASSERT(mAnimation && mAnimation->IsPartialPrerendered());

  nsIFrame* frame = GetPrimaryFrame();
  if (!frame) {
    return;
  }

  nsIWidget* widget = frame->GetNearestWidget();
  if (!widget) {
    return;
  }

  if (WindowRenderer* windowRenderer = widget->GetWindowRenderer()) {
    windowRenderer->RemovePartialPrerenderedAnimation(
        mAnimation->IdOnCompositor(), mAnimation);
  }
}

static const KeyframeEffectOptions& KeyframeEffectOptionsFromUnion(
    const UnrestrictedDoubleOrKeyframeEffectOptions& aOptions) {
  MOZ_ASSERT(aOptions.IsKeyframeEffectOptions());
  return aOptions.GetAsKeyframeEffectOptions();
}

static const KeyframeEffectOptions& KeyframeEffectOptionsFromUnion(
    const UnrestrictedDoubleOrKeyframeAnimationOptions& aOptions) {
  MOZ_ASSERT(aOptions.IsKeyframeAnimationOptions());
  return aOptions.GetAsKeyframeAnimationOptions();
}

template <class OptionsType>
static KeyframeEffectParams KeyframeEffectParamsFromUnion(
    const OptionsType& aOptions, CallerType aCallerType, ErrorResult& aRv) {
  KeyframeEffectParams result;
  if (aOptions.IsUnrestrictedDouble()) {
    return result;
  }

  const KeyframeEffectOptions& options =
      KeyframeEffectOptionsFromUnion(aOptions);

  // |result.mPseudoRequest| uses the default value, i.e. NotPseudo.
  result.mIterationComposite = options.mIterationComposite;
  result.mComposite = options.mComposite;

  if (DOMStringIsNull(options.mPseudoElement)) {
    return result;
  }

  Maybe<PseudoStyleRequest> pseudoRequest =
      nsCSSPseudoElements::ParsePseudoElement(options.mPseudoElement);
  if (!pseudoRequest) {
    // Per the spec, we throw SyntaxError for syntactically invalid pseudos.
    aRv.ThrowSyntaxError(
        nsPrintfCString("'%s' is a syntactically invalid pseudo-element.",
                        NS_ConvertUTF16toUTF8(options.mPseudoElement).get()));
    return result;
  }

  result.mPseudoRequest = std::move(*pseudoRequest);
  if (!AnimationUtils::IsSupportedPseudoForAnimations(result.mPseudoRequest)) {
    // Per the spec, we throw SyntaxError for unsupported pseudos.
    aRv.ThrowSyntaxError(
        nsPrintfCString("'%s' is an unsupported pseudo-element.",
                        NS_ConvertUTF16toUTF8(options.mPseudoElement).get()));
  }

  return result;
}

template <class OptionsType>
/* static */
already_AddRefed<KeyframeEffect> KeyframeEffect::ConstructKeyframeEffect(
    const GlobalObject& aGlobal, Element* aTarget,
    JS::Handle<JSObject*> aKeyframes, const OptionsType& aOptions,
    ErrorResult& aRv) {
  // We should get the document from `aGlobal` instead of the current Realm
  // to make this works in Xray case.
  //
  // In all non-Xray cases, `aGlobal` matches the current Realm, so this
  // matches the spec behavior.
  //
  // In Xray case, the new objects should be created using the document of
  // the target global, but the KeyframeEffect constructors are called in the
  // caller's compartment to access `aKeyframes` object.
  Document* doc = AnimationUtils::GetDocumentFromGlobal(aGlobal.Get());
  if (!doc) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  KeyframeEffectParams effectOptions =
      KeyframeEffectParamsFromUnion(aOptions, aGlobal.CallerType(), aRv);
  // An invalid Pseudo-element aborts all further steps.
  if (aRv.Failed()) {
    return nullptr;
  }

  TimingParams timingParams = TimingParams::FromOptionsUnion(aOptions, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  RefPtr<KeyframeEffect> effect = new KeyframeEffect(
      doc, OwningAnimationTarget(aTarget, effectOptions.mPseudoRequest),
      std::move(timingParams), effectOptions);

  effect->SetKeyframes(aGlobal.Context(), aKeyframes, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  return effect.forget();
}

nsTArray<AnimationProperty> KeyframeEffect::BuildProperties(
    const ComputedStyle* aStyle) {
  MOZ_ASSERT(aStyle);

  nsTArray<AnimationProperty> result;
  // If mTarget is false (i.e. mTarget.mElement is null), return an empty
  // property array.
  if (!mTarget) {
    return result;
  }

  // When GetComputedKeyframeValues or GetAnimationPropertiesFromKeyframes
  // calculate computed values from |mKeyframes|, they could possibly
  // trigger a subsequent restyle in which we rebuild animations. If that
  // happens we could find that |mKeyframes| is overwritten while it is
  // being iterated over. Normally that shouldn't happen but just in case we
  // make a copy of |mKeyframes| first and iterate over that instead.
  auto keyframesCopy(mKeyframes.Clone());

  result = KeyframeUtils::GetAnimationPropertiesFromKeyframes(
      keyframesCopy, mTarget.mElement, mTarget.mPseudoRequest, aStyle,
      mEffectOptions.mComposite);

#ifdef DEBUG
  MOZ_ASSERT(SpecifiedKeyframeArraysAreEqual(mKeyframes, keyframesCopy),
             "Apart from the computed offset members, the keyframes array"
             " should not be modified");
#endif

  mKeyframes = std::move(keyframesCopy);
  return result;
}

template <typename FrameEnumFunc>
static void EnumerateContinuationsOrIBSplitSiblings(nsIFrame* aFrame,
                                                    FrameEnumFunc&& aFunc) {
  while (aFrame) {
    aFunc(aFrame);
    aFrame = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(aFrame);
  }
}

void KeyframeEffect::UpdateTarget(Element* aElement,
                                  const PseudoStyleRequest& aPseudoRequest) {
  OwningAnimationTarget newTarget(aElement, aPseudoRequest);

  if (mTarget == newTarget) {
    // Assign the same target, skip it.
    return;
  }

  if (mTarget) {
    // Call ResetIsRunningOnCompositor() prior to UnregisterTarget() since
    // ResetIsRunningOnCompositor() might try to get the EffectSet associated
    // with this keyframe effect to remove partial pre-render animation from
    // the layer manager.
    ResetIsRunningOnCompositor();
    UnregisterTarget();

    RequestRestyle(EffectCompositor::RestyleType::Layer);

    nsAutoAnimationMutationBatch mb(mTarget.mElement->OwnerDoc());
    if (mAnimation) {
      MutationObservers::NotifyAnimationRemoved(mAnimation);
    }
  }

  mTarget = newTarget;

  if (mTarget) {
    UpdateTargetRegistration();
    RefPtr<const ComputedStyle> computedStyle =
        GetTargetComputedStyle(Flush::None);
    if (computedStyle) {
      UpdateProperties(computedStyle);
    }

    RequestRestyle(EffectCompositor::RestyleType::Layer);

    nsAutoAnimationMutationBatch mb(mTarget.mElement->OwnerDoc());
    if (mAnimation) {
      MutationObservers::NotifyAnimationAdded(mAnimation);
    }
  }

  if (mAnimation) {
    mAnimation->NotifyEffectTargetUpdated();
  }
}

void KeyframeEffect::UpdateTargetRegistration() {
  if (!mTarget) {
    return;
  }

  bool isRelevant = mAnimation && mAnimation->IsRelevant();

  // Animation::IsRelevant() returns a cached value. It only updates when
  // something calls Animation::UpdateRelevance. Whenever our timing changes,
  // we should be notifying our Animation before calling this, so
  // Animation::IsRelevant() should be up-to-date by the time we get here.
  MOZ_ASSERT(isRelevant ==
                 ((IsCurrent() || IsInEffect()) && mAnimation &&
                  mAnimation->ReplaceState() != AnimationReplaceState::Removed),
             "Out of date Animation::IsRelevant value");

  if (isRelevant && !mInEffectSet) {
    EffectSet* effectSet = EffectSet::GetOrCreate(mTarget);
    effectSet->AddEffect(*this);
    mInEffectSet = true;
    UpdateEffectSet(effectSet);
    nsIFrame* frame = GetPrimaryFrame();
    EnumerateContinuationsOrIBSplitSiblings(
        frame, [](nsIFrame* aFrame) { aFrame->MarkNeedsDisplayItemRebuild(); });
  } else if (!isRelevant && mInEffectSet) {
    UnregisterTarget();
  }
}

void KeyframeEffect::UnregisterTarget() {
  if (!mInEffectSet) {
    return;
  }

  EffectSet* effectSet = EffectSet::Get(mTarget);
  MOZ_ASSERT(effectSet,
             "If mInEffectSet is true, there must be an EffectSet"
             " on the target element");
  mInEffectSet = false;
  if (effectSet) {
    effectSet->RemoveEffect(*this);

    if (effectSet->IsEmpty()) {
      EffectSet::DestroyEffectSet(mTarget);
    }
  }
  nsIFrame* frame = GetPrimaryFrame();
  EnumerateContinuationsOrIBSplitSiblings(
      frame, [](nsIFrame* aFrame) { aFrame->MarkNeedsDisplayItemRebuild(); });
}

void KeyframeEffect::RequestRestyle(
    EffectCompositor::RestyleType aRestyleType) {
  if (!mTarget) {
    return;
  }
  nsPresContext* presContext =
      nsContentUtils::GetContextForContent(mTarget.mElement);
  if (presContext && mAnimation) {
    presContext->EffectCompositor()->RequestRestyle(
        mTarget.mElement, mTarget.mPseudoRequest, aRestyleType,
        mAnimation->CascadeLevel());
  }
}

already_AddRefed<const ComputedStyle> KeyframeEffect::GetTargetComputedStyle(
    Flush aFlushType) const {
  if (!GetRenderedDocument()) {
    return nullptr;
  }

  MOZ_ASSERT(mTarget,
             "Should only have a document when we have a target element");

  OwningAnimationTarget kungfuDeathGrip(mTarget.mElement,
                                        mTarget.mPseudoRequest);

  return aFlushType == Flush::Style
             ? nsComputedDOMStyle::GetComputedStyle(mTarget.mElement,
                                                    mTarget.mPseudoRequest)
             : nsComputedDOMStyle::GetComputedStyleNoFlush(
                   mTarget.mElement, mTarget.mPseudoRequest);
}

#ifdef DEBUG
void DumpAnimationProperties(
    const StylePerDocumentStyleData* aRawData,
    nsTArray<AnimationProperty>& aAnimationProperties) {
  for (auto& p : aAnimationProperties) {
    printf("%s\n",
           nsCString(nsCSSProps::GetStringValue(p.mProperty.mID)).get());
    for (auto& s : p.mSegments) {
      nsAutoCString fromValue, toValue;
      s.mFromValue.SerializeSpecifiedValue(p.mProperty, aRawData, fromValue);
      s.mToValue.SerializeSpecifiedValue(p.mProperty, aRawData, toValue);
      printf("  %f..%f: %s..%s\n", s.mFromKey, s.mToKey, fromValue.get(),
             toValue.get());
    }
  }
}
#endif

/* static */
already_AddRefed<KeyframeEffect> KeyframeEffect::Constructor(
    const GlobalObject& aGlobal, Element* aTarget,
    JS::Handle<JSObject*> aKeyframes,
    const UnrestrictedDoubleOrKeyframeEffectOptions& aOptions,
    ErrorResult& aRv) {
  return ConstructKeyframeEffect(aGlobal, aTarget, aKeyframes, aOptions, aRv);
}

/* static */
already_AddRefed<KeyframeEffect> KeyframeEffect::Constructor(
    const GlobalObject& aGlobal, Element* aTarget,
    JS::Handle<JSObject*> aKeyframes,
    const UnrestrictedDoubleOrKeyframeAnimationOptions& aOptions,
    ErrorResult& aRv) {
  return ConstructKeyframeEffect(aGlobal, aTarget, aKeyframes, aOptions, aRv);
}

/* static */
already_AddRefed<KeyframeEffect> KeyframeEffect::Constructor(
    const GlobalObject& aGlobal, KeyframeEffect& aSource, ErrorResult& aRv) {
  Document* doc = AnimationUtils::GetCurrentRealmDocument(aGlobal.Context());
  if (!doc) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // Create a new KeyframeEffect object with aSource's target,
  // iteration composite operation, composite operation, and spacing mode.
  // The constructor creates a new AnimationEffect object by
  // aSource's TimingParams.
  // Note: we don't need to re-throw exceptions since the value specified on
  //       aSource's timing object can be assumed valid.
  RefPtr<KeyframeEffect> effect =
      new KeyframeEffect(doc, OwningAnimationTarget{aSource.mTarget}, aSource);
  // Copy cumulative changes. mCumulativeChangeHint should be the same as the
  // source one because both of targets are the same.
  effect->mCumulativeChanges = aSource.mCumulativeChanges;
  return effect.forget();
}

void KeyframeEffect::SetPseudoElement(const nsAString& aPseudoElement,
                                      ErrorResult& aRv) {
  if (DOMStringIsNull(aPseudoElement)) {
    UpdateTarget(mTarget.mElement, PseudoStyleRequest::NotPseudo());
    return;
  }

  // Note: ParsePseudoELement() returns Some(NotPseudo) for the null string,
  // so we handle null case before this.
  Maybe<PseudoStyleRequest> pseudoRequest =
      nsCSSPseudoElements::ParsePseudoElement(aPseudoElement);
  if (!pseudoRequest || pseudoRequest->IsNotPseudo()) {
    // Per the spec, we throw SyntaxError for syntactically invalid pseudos.
    aRv.ThrowSyntaxError(
        nsPrintfCString("'%s' is a syntactically invalid pseudo-element.",
                        NS_ConvertUTF16toUTF8(aPseudoElement).get()));
    return;
  }

  if (!AnimationUtils::IsSupportedPseudoForAnimations(*pseudoRequest)) {
    // Per the spec, we throw SyntaxError for unsupported pseudos.
    aRv.ThrowSyntaxError(
        nsPrintfCString("'%s' is an unsupported pseudo-element.",
                        NS_ConvertUTF16toUTF8(aPseudoElement).get()));
    return;
  }

  UpdateTarget(mTarget.mElement, *pseudoRequest);
}

static void CreatePropertyValue(
    const AnimatedPropertyID& aProperty, float aOffset,
    const Maybe<StyleComputedTimingFunction>& aTimingFunction,
    const AnimationValue& aValue, dom::CompositeOperation aComposite,
    const StylePerDocumentStyleData* aRawData,
    AnimationPropertyValueDetails& aResult) {
  aResult.mOffset = aOffset;

  if (!aValue.IsNull()) {
    nsAutoCString stringValue;
    aValue.SerializeSpecifiedValue(aProperty, aRawData, stringValue);
    aResult.mValue.Construct(stringValue);
  }

  if (aTimingFunction) {
    aResult.mEasing.Construct();
    aTimingFunction->AppendToString(aResult.mEasing.Value());
  } else {
    aResult.mEasing.Construct("linear"_ns);
  }

  aResult.mComposite = aComposite;
}

void KeyframeEffect::GetProperties(
    nsTArray<AnimationPropertyDetails>& aProperties, ErrorResult& aRv) const {
  const StylePerDocumentStyleData* rawData =
      mDocument->EnsureStyleSet().RawData();

  for (const AnimationProperty& property : mProperties) {
    AnimationPropertyDetails propertyDetails;
    property.mProperty.ToString(propertyDetails.mProperty);
    propertyDetails.mRunningOnCompositor = property.mIsRunningOnCompositor;

    nsAutoString localizedString;
    if (property.mPerformanceWarning &&
        property.mPerformanceWarning->ToLocalizedString(localizedString)) {
      propertyDetails.mWarning.Construct(localizedString);
    }

    if (!propertyDetails.mValues.SetCapacity(property.mSegments.Length(),
                                             mozilla::fallible)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }

    for (size_t segmentIdx = 0, segmentLen = property.mSegments.Length();
         segmentIdx < segmentLen; segmentIdx++) {
      const AnimationPropertySegment& segment = property.mSegments[segmentIdx];

      binding_detail::FastAnimationPropertyValueDetails fromValue;
      CreatePropertyValue(property.mProperty, segment.mFromKey,
                          segment.mTimingFunction, segment.mFromValue,
                          segment.mFromComposite, rawData, fromValue);
      // We don't apply timing functions for zero-length segments, so
      // don't return one here.
      if (segment.mFromKey == segment.mToKey) {
        fromValue.mEasing.Reset();
      }
      // Even though we called SetCapacity before, this could fail, since we
      // might add multiple elements to propertyDetails.mValues for an element
      // of property.mSegments in the cases mentioned below.
      if (!propertyDetails.mValues.AppendElement(fromValue,
                                                 mozilla::fallible)) {
        aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }

      // Normally we can ignore the to-value for this segment since it is
      // identical to the from-value from the next segment. However, we need
      // to add it if either:
      // a) this is the last segment, or
      // b) the next segment's from-value differs.
      if (segmentIdx == segmentLen - 1 ||
          property.mSegments[segmentIdx + 1].mFromValue != segment.mToValue) {
        binding_detail::FastAnimationPropertyValueDetails toValue;
        CreatePropertyValue(property.mProperty, segment.mToKey, Nothing(),
                            segment.mToValue, segment.mToComposite, rawData,
                            toValue);
        // It doesn't really make sense to have a timing function on the
        // last property value or before a sudden jump so we just drop the
        // easing property altogether.
        toValue.mEasing.Reset();
        if (!propertyDetails.mValues.AppendElement(toValue,
                                                   mozilla::fallible)) {
          aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
          return;
        }
      }
    }

    aProperties.AppendElement(propertyDetails);
  }
}

void KeyframeEffect::GetKeyframes(JSContext* aCx, nsTArray<JSObject*>& aResult,
                                  ErrorResult& aRv) const {
  MOZ_ASSERT(aResult.IsEmpty());
  MOZ_ASSERT(!aRv.Failed());

  if (!aResult.SetCapacity(mKeyframes.Length(), mozilla::fallible)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  bool isCSSAnimation = mAnimation && mAnimation->AsCSSAnimation();

  // For Servo, when we have CSS Animation @keyframes with variables, we convert
  // shorthands to longhands if needed, and store a reference to the unparsed
  // value. When it comes time to serialize, however, what do you serialize for
  // a longhand that comes from a variable reference in a shorthand? Servo says,
  // "an empty string" which is not particularly helpful.
  //
  // We should just store shorthands as-is (bug 1391537) and then return the
  // variable references, but for now, since we don't do that, and in order to
  // be consistent with Gecko, we just expand the variables (assuming we have
  // enough context to do so). For that we need to grab the ComputedStyle so we
  // know what custom property values to provide.
  RefPtr<const ComputedStyle> computedStyle;
  if (isCSSAnimation) {
    // The following will flush style but that's ok since if you update a
    // variable's computed value, you expect to see that updated value in the
    // result of getKeyframes().
    //
    // If we don't have a target, the following will return null. In that case
    // we might end up returning variables as-is or empty string. That should be
    // acceptable however, since such a case is rare and this is only
    // short-term (and unshipped) behavior until bug 1391537 is fixed.
    computedStyle = GetTargetComputedStyle(Flush::Style);
  }

  const StylePerDocumentStyleData* rawData =
      mDocument->EnsureStyleSet().RawData();

  for (const Keyframe& keyframe : mKeyframes) {
    // Set up a dictionary object for the explicit members
    BaseComputedKeyframe keyframeDict;
    if (keyframe.mOffset) {
      keyframeDict.mOffset.SetValue(keyframe.mOffset.value());
    }
    MOZ_ASSERT(keyframe.mComputedOffset != Keyframe::kComputedOffsetNotSet,
               "Invalid computed offset");
    keyframeDict.mComputedOffset.Construct(keyframe.mComputedOffset);
    if (keyframe.mTimingFunction) {
      keyframeDict.mEasing.Truncate();
      keyframe.mTimingFunction.ref().AppendToString(keyframeDict.mEasing);
    }  // else if null, leave easing as its default "linear".

    keyframeDict.mComposite = keyframe.mComposite;

    JS::Rooted<JS::Value> keyframeJSValue(aCx);
    if (!ToJSValue(aCx, keyframeDict, &keyframeJSValue)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    JS::Rooted<JSObject*> keyframeObject(aCx, &keyframeJSValue.toObject());
    for (const PropertyValuePair& propertyValue : keyframe.mPropertyValues) {
      nsAutoCString stringValue;
      if (propertyValue.mServoDeclarationBlock) {
        Servo_DeclarationBlock_SerializeOneValue(
            propertyValue.mServoDeclarationBlock, &propertyValue.mProperty,
            &stringValue, computedStyle, rawData);
      } else if (auto* value = mBaseValues.GetWeak(propertyValue.mProperty)) {
        Servo_AnimationValue_Serialize(value, &propertyValue.mProperty, rawData,
                                       &stringValue);
      }

      // Basically, we need to do the mapping:
      // * eCSSProperty_offset => "cssOffset"
      // * eCSSProperty_float => "cssFloat"
      // This means if property refers to the CSS "offset"/"float" property,
      // return the string "cssOffset"/"cssFloat". (So avoid overlapping
      // "offset" property in BaseKeyframe.)
      // https://drafts.csswg.org/web-animations/#property-name-conversion
      const char* name = nullptr;
      nsAutoCString customName;
      switch (propertyValue.mProperty.mID) {
        case nsCSSPropertyID::eCSSPropertyExtra_variable:
          customName.Append("--");
          customName.Append(nsAtomCString(propertyValue.mProperty.mCustomName));
          name = customName.get();
          break;
        case nsCSSPropertyID::eCSSProperty_offset:
          name = "cssOffset";
          break;
        case nsCSSPropertyID::eCSSProperty_float:
          // FIXME: Bug 1582314: Should handle cssFloat manually if we remove it
          // from nsCSSProps::PropertyIDLName().
        default:
          name = nsCSSProps::PropertyIDLName(propertyValue.mProperty.mID);
      }

      JS::Rooted<JS::Value> value(aCx);
      if (!NonVoidUTF8StringToJsval(aCx, stringValue, &value) ||
          !JS_DefineProperty(aCx, keyframeObject, name, value,
                             JSPROP_ENUMERATE)) {
        aRv.Throw(NS_ERROR_FAILURE);
        return;
      }
    }

    aResult.AppendElement(keyframeObject);
  }
}

/* static */ const TimeDuration
KeyframeEffect::OverflowRegionRefreshInterval() {
  // The amount of time we can wait between updating throttled animations
  // on the main thread that influence the overflow region.
  static const TimeDuration kOverflowRegionRefreshInterval =
      TimeDuration::FromMilliseconds(200);

  return kOverflowRegionRefreshInterval;
}

static bool CanOptimizeAwayDueToOpacity(const KeyframeEffect& aEffect,
                                        const nsIFrame& aFrame) {
  if (!aFrame.Style()->IsInOpacityZeroSubtree()) {
    return false;
  }

  // Find the root of the opacity: 0 subtree.
  const nsIFrame* root = &aFrame;
  while (true) {
    auto* parent = root->GetInFlowParent();
    if (!parent || !parent->Style()->IsInOpacityZeroSubtree()) {
      break;
    }
    root = parent;
  }

  MOZ_ASSERT(root && root->Style()->IsInOpacityZeroSubtree());

  // Even if we're in an opacity: zero subtree, if the root of the subtree may
  // have an opacity animation, we can't optimize us away, as we may become
  // visible ourselves.
  return (root != &aFrame || !aEffect.HasOpacityChange()) &&
         !root->HasAnimationOfOpacity();
}

bool KeyframeEffect::CanThrottleIfNotVisible(nsIFrame& aFrame) const {
  // Unless we are newly in-effect, we can throttle the animation if the
  // animation is paint only and the target frame is out of view or the document
  // is in background tabs.
  if (!mInEffectOnLastAnimationTimingUpdate || !CanIgnoreIfNotVisible()) {
    return false;
  }

  PresShell* presShell = GetPresShell();
  if (presShell && !presShell->IsActive()) {
    return true;
  }

  // The frame may be indirectly rendered as a mask or clipPath or via a use
  // element.
  if (SVGObserverUtils::SelfOrAncestorHasRenderingObservers(&aFrame)) {
    return false;
  }

  const bool isVisibilityHidden =
      !aFrame.IsVisibleOrMayHaveVisibleDescendants();
  const bool canOptimizeAwayVisibility =
      isVisibilityHidden && !HasVisibilityChange();

  const bool invisible = canOptimizeAwayVisibility ||
                         CanOptimizeAwayDueToOpacity(*this, aFrame) ||
                         aFrame.IsScrolledOutOfView();
  if (!invisible) {
    return false;
  }

  // If there are no overflow change hints, we don't need to worry about
  // unthrottling the animation periodically to update scrollbar positions for
  // the overflow region.
  if (!HasPropertiesThatMightAffectOverflow()) {
    return true;
  }

  // Don't throttle finite animations since the animation might suddenly
  // come into view and if it was throttled it will be out-of-sync.
  if (HasFiniteActiveDuration()) {
    return false;
  }

  return isVisibilityHidden ? CanThrottleOverflowChangesInScrollable(aFrame)
                            : CanThrottleOverflowChanges(aFrame);
}

bool KeyframeEffect::CanThrottle() const {
  // Unthrottle if we are not in effect or current. This will be the case when
  // our owning animation has finished, is idle, or when we are in the delay
  // phase (but without a backwards fill). In each case the computed progress
  // value produced on each tick will be the same so we will skip requesting
  // unnecessary restyles in NotifyAnimationTimingUpdated. Any calls we *do* get
  // here will be because of a change in state (e.g. we are newly finished or
  // newly no longer in effect) in which case we shouldn't throttle the sample.
  if (!IsInEffect() || !IsCurrent()) {
    return false;
  }

  nsIFrame* const frame = GetStyleFrame();
  if (!frame) {
    // There are two possible cases here.
    // a) No target element
    // b) The target element has no frame, e.g. because it is in a display:none
    //    subtree.
    // In either case we can throttle the animation because there is no
    // need to update on the main thread.
    return true;
  }

  // Do not throttle any animations during print preview.
  if (frame->PresContext()->IsPrintingOrPrintPreview()) {
    return false;
  }

  if (CanThrottleIfNotVisible(*frame)) {
    return true;
  }

  EffectSet* effectSet = nullptr;
  for (const AnimationProperty& property : mProperties) {
    if (!property.mIsRunningOnCompositor) {
      return false;
    }

    MOZ_ASSERT(nsCSSPropertyIDSet::CompositorAnimatables().HasProperty(
                   property.mProperty),
               "The property should be able to run on the compositor");
    if (!effectSet) {
      effectSet = EffectSet::Get(mTarget);
      MOZ_ASSERT(effectSet,
                 "CanThrottle should be called on an effect "
                 "associated with a target element");
    }

    DisplayItemType displayItemType =
        LayerAnimationInfo::GetDisplayItemTypeForProperty(
            property.mProperty.mID);

    // Note that AnimationInfo::GetGenarationFromFrame() is supposed to work
    // with the primary frame instead of the style frame.
    Maybe<uint64_t> generation = layers::AnimationInfo::GetGenerationFromFrame(
        GetPrimaryFrame(), displayItemType);
    // Unthrottle if the animation needs to be brought up to date
    if (!generation || effectSet->GetAnimationGeneration() != *generation) {
      return false;
    }

    MOZ_ASSERT(HasEffectiveAnimationOfProperty(property.mProperty, *effectSet),
               "There should be an effective animation of the property while "
               "it is marked as being run on the compositor");

    // If this is a transform animation that affects the overflow region,
    // we should unthrottle the animation periodically.
    if (HasPropertiesThatMightAffectOverflow() &&
        !CanThrottleOverflowChangesInScrollable(*frame)) {
      return false;
    }
  }

  return true;
}

bool KeyframeEffect::CanThrottleOverflowChanges(const nsIFrame& aFrame) const {
  TimeStamp now = aFrame.PresContext()->RefreshDriver()->MostRecentRefresh();

  EffectSet* effectSet = EffectSet::Get(mTarget);
  MOZ_ASSERT(effectSet,
             "CanOverflowTransformChanges is expected to be called"
             " on an effect in an effect set");
  MOZ_ASSERT(mAnimation,
             "CanOverflowTransformChanges is expected to be called"
             " on an effect with a parent animation");
  TimeStamp lastSyncTime = effectSet->LastOverflowAnimationSyncTime();
  // If this animation can cause overflow, we can throttle some of the ticks.
  return (!lastSyncTime.IsNull() &&
          (now - lastSyncTime) < OverflowRegionRefreshInterval());
}

bool KeyframeEffect::CanThrottleOverflowChangesInScrollable(
    nsIFrame& aFrame) const {
  // If the target element is not associated with any documents, we don't care
  // it.
  Document* doc = GetRenderedDocument();
  if (!doc) {
    return true;
  }

  // If we know that the animation cannot cause overflow,
  // we can just disable flushes for this animation.

  // If we have no intersection observers, we don't care about overflow.
  if (!doc->HasIntersectionObservers()) {
    return true;
  }

  if (CanThrottleOverflowChanges(aFrame)) {
    return true;
  }

  // If the nearest scroll container ancestor has overflow:hidden,
  // we don't care about overflow.
  ScrollContainerFrame* scrollContainerFrame =
      nsLayoutUtils::GetNearestScrollContainerFrame(&aFrame);
  if (!scrollContainerFrame) {
    return true;
  }

  ScrollStyles ss = scrollContainerFrame->GetScrollStyles();
  if (ss.mVertical == StyleOverflow::Hidden &&
      ss.mHorizontal == StyleOverflow::Hidden &&
      scrollContainerFrame->GetLogicalScrollPosition() == nsPoint(0, 0)) {
    return true;
  }

  return false;
}

nsIFrame* KeyframeEffect::GetStyleFrame() const {
  nsIFrame* frame = GetPrimaryFrame();
  if (!frame) {
    return nullptr;
  }

  return nsLayoutUtils::GetStyleFrame(frame);
}

nsIFrame* KeyframeEffect::GetPrimaryFrame() const {
  nsIFrame* frame = nullptr;
  if (!mTarget) {
    return frame;
  }

  switch (mTarget.mPseudoRequest.mType) {
    case PseudoStyleType::before:
      frame = nsLayoutUtils::GetBeforeFrame(mTarget.mElement);
      break;
    case PseudoStyleType::after:
      frame = nsLayoutUtils::GetAfterFrame(mTarget.mElement);
      break;
    case PseudoStyleType::marker:
      frame = nsLayoutUtils::GetMarkerFrame(mTarget.mElement);
      break;
    case PseudoStyleType::viewTransition:
    case PseudoStyleType::viewTransitionGroup:
    case PseudoStyleType::viewTransitionImagePair:
    case PseudoStyleType::viewTransitionOld:
    case PseudoStyleType::viewTransitionNew:
      if (Element* pseudoElement =
              mTarget.mElement->GetPseudoElement(mTarget.mPseudoRequest)) {
        frame = pseudoElement->GetPrimaryFrame();
      }
      break;
    default:
      frame = mTarget.mElement->GetPrimaryFrame();
      MOZ_ASSERT(mTarget.mPseudoRequest.IsNotPseudo(),
                 "unknown mTarget.mPseudoRequest");
  }

  return frame;
}

Document* KeyframeEffect::GetRenderedDocument() const {
  if (!mTarget) {
    return nullptr;
  }
  return mTarget.mElement->GetComposedDoc();
}

PresShell* KeyframeEffect::GetPresShell() const {
  Document* doc = GetRenderedDocument();
  if (!doc) {
    return nullptr;
  }
  return doc->GetPresShell();
}

/* static */
bool KeyframeEffect::IsGeometricProperty(const nsCSSPropertyID aProperty) {
  MOZ_ASSERT(!nsCSSProps::IsShorthand(aProperty),
             "Property should be a longhand property");

  switch (aProperty) {
    case eCSSProperty_bottom:
    case eCSSProperty_height:
    case eCSSProperty_left:
    case eCSSProperty_margin_bottom:
    case eCSSProperty_margin_left:
    case eCSSProperty_margin_right:
    case eCSSProperty_margin_top:
    case eCSSProperty_padding_bottom:
    case eCSSProperty_padding_left:
    case eCSSProperty_padding_right:
    case eCSSProperty_padding_top:
    case eCSSProperty_right:
    case eCSSProperty_top:
    case eCSSProperty_width:
      return true;
    default:
      return false;
  }
}

/* static */
bool KeyframeEffect::CanAnimateTransformOnCompositor(
    const nsIFrame* aFrame,
    AnimationPerformanceWarning::Type& aPerformanceWarning /* out */) {
  // In some cases, such as when we are simply collecting all the compositor
  // animations regardless of the frame on which they run in order to calculate
  // change hints, |aFrame| will be the style frame. However, even in that case
  // we should look at the primary frame since that is where the transform will
  // be applied.
  const nsIFrame* primaryFrame =
      nsLayoutUtils::GetPrimaryFrameFromStyleFrame(aFrame);

  // Async 'transform' animations of aFrames with SVG transforms is not
  // supported.  See bug 779599.
  if (primaryFrame->GetParentSVGTransforms()) {
    aPerformanceWarning = AnimationPerformanceWarning::Type::TransformSVG;
    return false;
  }

  // If there's any content that might have non-scaling stroke then we can't
  // run in the compositor.
  if (primaryFrame->IsSVGFrame() &&
      primaryFrame->HasAnyStateBits(
          NS_STATE_SVG_MAY_CONTAIN_NON_SCALING_STROKE)) {
    aPerformanceWarning = AnimationPerformanceWarning::Type::NonScalingStroke;
    return false;
  }

  return true;
}

bool KeyframeEffect::ShouldBlockAsyncTransformAnimations(
    const nsIFrame* aFrame, const nsCSSPropertyIDSet& aPropertySet,
    AnimationPerformanceWarning::Type& aPerformanceWarning /* out */) const {
  // If we depend on the SVG url (no matter whether there are any offset-path
  // animations), we cannot run any transform-like animations in the compositor
  // because we cannot resolve the url in the compositor if its style uses url.
  if (aFrame->StyleDisplay()->mOffsetPath.IsUrl()) {
    return true;
  }

  EffectSet* effectSet = EffectSet::Get(mTarget);
  // The various transform properties ('transform', 'scale' etc.) get combined
  // on the compositor.
  //
  // As a result, if we have an animation of 'scale' and 'translate', but the
  // 'translate' property is covered by an !important rule, we will not be
  // able to combine the result on the compositor since we won't have the
  // !important rule to incorporate. In that case we should run all the
  // transform-related animations on the main thread (where we have the
  // !important rule).
  nsCSSPropertyIDSet blockedProperties =
      effectSet->PropertiesForAnimationsLevel().Intersect(
          effectSet->PropertiesWithImportantRules());
  if (blockedProperties.Intersects(aPropertySet)) {
    aPerformanceWarning =
        AnimationPerformanceWarning::Type::TransformIsBlockedByImportantRules;
    return true;
  }

  MOZ_ASSERT(mAnimation);
  for (const AnimationProperty& property : mProperties) {
    // If there is a property for animations level that is overridden by
    // !important rules, it should not block other animations from running
    // on the compositor.
    // NOTE: We don't currently check for !important rules for properties that
    // don't run on the compositor. As result such properties (e.g. margin-left)
    // can still block async animations even if they are overridden by
    // !important rules.
    if (effectSet &&
        effectSet->PropertiesWithImportantRules().HasProperty(
            property.mProperty) &&
        effectSet->PropertiesForAnimationsLevel().HasProperty(
            property.mProperty)) {
      continue;
    }

    // Check for unsupported transform animations
    if (LayerAnimationInfo::GetCSSPropertiesFor(DisplayItemType::TYPE_TRANSFORM)
            .HasProperty(property.mProperty)) {
      if (!CanAnimateTransformOnCompositor(aFrame, aPerformanceWarning)) {
        return true;
      }
    }

    // If there are any offset-path animations whose animation values are url(),
    // we have to sync with the main thread when resolving it.
    if (property.mProperty.mID == eCSSProperty_offset_path) {
      for (const auto& seg : property.mSegments) {
        if (seg.mFromValue.IsOffsetPathUrl() ||
            seg.mToValue.IsOffsetPathUrl()) {
          return true;
        }
      }
    }
  }

  return false;
}

bool KeyframeEffect::HasGeometricProperties() const {
  for (const AnimationProperty& property : mProperties) {
    if (IsGeometricProperty(property.mProperty.mID)) {
      return true;
    }
  }

  return false;
}

void KeyframeEffect::SetPerformanceWarning(
    const nsCSSPropertyIDSet& aPropertySet,
    const AnimationPerformanceWarning& aWarning) {
  nsCSSPropertyIDSet curr = aPropertySet;
  for (AnimationProperty& property : mProperties) {
    if (!curr.HasProperty(property.mProperty)) {
      continue;
    }
    property.SetPerformanceWarning(aWarning, mTarget.mElement);
    curr.RemoveProperty(property.mProperty.mID);
    if (curr.IsEmpty()) {
      return;
    }
  }
}

void KeyframeEffect::CalculateCumulativeChangesForProperty(
    const AnimationProperty& aProperty) {
  if (aProperty.mProperty.IsCustom()) {
    // Custom properties don't affect rendering on their own.
    return;
  }

  constexpr auto kInterestingFlags =
      CSSPropFlags::AffectsLayout | CSSPropFlags::AffectsOverflow;
  if (aProperty.mProperty.mID == eCSSProperty_opacity) {
    mCumulativeChanges.mOpacity = true;
    return;  // We know opacity is visual-only.
  }

  if (aProperty.mProperty.mID == eCSSProperty_visibility) {
    mCumulativeChanges.mVisibility = true;
    return;  // We know visibility is visual-only.
  }

  if (aProperty.mProperty.mID == eCSSProperty_background_color) {
    if (!mCumulativeChanges.mHasBackgroundColorCurrentColor) {
      mCumulativeChanges.mHasBackgroundColorCurrentColor =
          HasCurrentColor(aProperty.mSegments);
    }
    return;  // We know background-color is visual-only.
  }

  auto flags = nsCSSProps::PropFlags(aProperty.mProperty.mID);
  if (!(flags & kInterestingFlags)) {
    return;  // Property is visual-only.
  }

  bool anyChange = false;
  for (const AnimationPropertySegment& segment : aProperty.mSegments) {
    if (!segment.HasReplaceableValues() ||
        segment.mFromValue != segment.mToValue) {
      // We can't know non-replaceable values until we compose the animation, so
      // assume a change there.
      anyChange = true;
      break;
    }
  }

  if (!anyChange) {
    return;
  }

  mCumulativeChanges.mOverflow |= bool(flags & CSSPropFlags::AffectsOverflow);
  mCumulativeChanges.mLayout |= bool(flags & CSSPropFlags::AffectsLayout);
}

void KeyframeEffect::SetAnimation(Animation* aAnimation) {
  if (mAnimation == aAnimation) {
    return;
  }

  // Restyle for the old animation.
  RequestRestyle(EffectCompositor::RestyleType::Layer);

  mAnimation = aAnimation;

  UpdateNormalizedTiming();

  // The order of these function calls is important:
  // NotifyAnimationTimingUpdated() need the updated mIsRelevant flag to check
  // if it should create the effectSet or not, and MarkCascadeNeedsUpdate()
  // needs a valid effectSet, so we should call them in this order.
  if (mAnimation) {
    mAnimation->UpdateRelevance();
  }
  NotifyAnimationTimingUpdated(PostRestyleMode::IfNeeded);
  if (mAnimation) {
    MarkCascadeNeedsUpdate();
  }
}

bool KeyframeEffect::CanIgnoreIfNotVisible() const {
  if (!StaticPrefs::dom_animations_offscreen_throttling()) {
    return false;
  }
  return !mCumulativeChanges.mLayout;
}

void KeyframeEffect::MarkCascadeNeedsUpdate() {
  if (!mTarget) {
    return;
  }

  EffectSet* effectSet = EffectSet::Get(mTarget);
  if (!effectSet) {
    return;
  }
  effectSet->MarkCascadeNeedsUpdate();
}

/* static */
bool KeyframeEffect::HasComputedTimingChanged(
    const ComputedTiming& aComputedTiming,
    IterationCompositeOperation aIterationComposite,
    const Nullable<double>& aProgressOnLastCompose,
    uint64_t aCurrentIterationOnLastCompose) {
  // Typically we don't need to request a restyle if the progress hasn't
  // changed since the last call to ComposeStyle. The one exception is if the
  // iteration composite mode is 'accumulate' and the current iteration has
  // changed, since that will often produce a different result.
  return aComputedTiming.mProgress != aProgressOnLastCompose ||
         (aIterationComposite == IterationCompositeOperation::Accumulate &&
          aComputedTiming.mCurrentIteration != aCurrentIterationOnLastCompose);
}

bool KeyframeEffect::HasComputedTimingChanged() const {
  ComputedTiming computedTiming = GetComputedTiming();
  return HasComputedTimingChanged(
      computedTiming, mEffectOptions.mIterationComposite,
      mProgressOnLastCompose, mCurrentIterationOnLastCompose);
}

bool KeyframeEffect::ContainsAnimatedScale(const nsIFrame* aFrame) const {
  // For display:table content, transform animations run on the table wrapper
  // frame. If we are being passed a frame that doesn't support transforms
  // (i.e. the inner table frame) we could just return false, but it possibly
  // means we looked up the wrong EffectSet so for now we just assert instead.
  MOZ_ASSERT(aFrame && aFrame->SupportsCSSTransforms(),
             "We should be passed a frame that supports transforms");

  if (!IsCurrent()) {
    return false;
  }

  if (!mAnimation ||
      mAnimation->ReplaceState() == AnimationReplaceState::Removed) {
    return false;
  }

  for (const AnimationProperty& prop : mProperties) {
    if (prop.mProperty.mID != eCSSProperty_transform &&
        prop.mProperty.mID != eCSSProperty_scale &&
        prop.mProperty.mID != eCSSProperty_rotate) {
      continue;
    }

    AnimationValue baseStyle = BaseStyle(prop.mProperty);
    if (!baseStyle.IsNull()) {
      gfx::MatrixScales size = baseStyle.GetScaleValue(aFrame);
      if (size != gfx::MatrixScales()) {
        return true;
      }
    }

    // This is actually overestimate because there are some cases that combining
    // the base value and from/to value produces 1:1 scale. But it doesn't
    // really matter.
    for (const AnimationPropertySegment& segment : prop.mSegments) {
      if (!segment.mFromValue.IsNull()) {
        gfx::MatrixScales from = segment.mFromValue.GetScaleValue(aFrame);
        if (from != gfx::MatrixScales()) {
          return true;
        }
      }
      if (!segment.mToValue.IsNull()) {
        gfx::MatrixScales to = segment.mToValue.GetScaleValue(aFrame);
        if (to != gfx::MatrixScales()) {
          return true;
        }
      }
    }
  }

  return false;
}

void KeyframeEffect::UpdateEffectSet(EffectSet* aEffectSet) const {
  if (!mInEffectSet) {
    return;
  }

  EffectSet* effectSet = aEffectSet ? aEffectSet : EffectSet::Get(mTarget);
  if (!effectSet) {
    return;
  }

  nsIFrame* styleFrame = GetStyleFrame();
  if (HasAnimationOfPropertySet(nsCSSPropertyIDSet::OpacityProperties())) {
    effectSet->SetMayHaveOpacityAnimation();
    EnumerateContinuationsOrIBSplitSiblings(styleFrame, [](nsIFrame* aFrame) {
      aFrame->SetMayHaveOpacityAnimation();
    });
  }

  nsIFrame* primaryFrame = GetPrimaryFrame();
  if (HasAnimationOfPropertySet(
          nsCSSPropertyIDSet::TransformLikeProperties())) {
    effectSet->SetMayHaveTransformAnimation();
    // For table frames, it's not clear if we should iterate over the
    // continuations of the table wrapper or the inner table frame.
    //
    // Fortunately, this is not currently an issue because we only split tables
    // when printing (page breaks) where we don't run animations.
    EnumerateContinuationsOrIBSplitSiblings(primaryFrame, [](nsIFrame* aFrame) {
      aFrame->SetMayHaveTransformAnimation();
    });
  }
}

KeyframeEffect::MatchForCompositor KeyframeEffect::IsMatchForCompositor(
    const nsCSSPropertyIDSet& aPropertySet, const nsIFrame* aFrame,
    const EffectSet& aEffects,
    AnimationPerformanceWarning::Type& aPerformanceWarning /* out */) const {
  MOZ_ASSERT(mAnimation);

  if (!mAnimation->IsRelevant()) {
    return KeyframeEffect::MatchForCompositor::No;
  }

  if (mAnimation->ShouldBeSynchronizedWithMainThread(aPropertySet, aFrame,
                                                     aPerformanceWarning)) {
    // For a given |aFrame|, we don't want some animations of |aProperty| to
    // run on the compositor and others to run on the main thread, so if any
    // need to be synchronized with the main thread, run them all there.
    return KeyframeEffect::MatchForCompositor::NoAndBlockThisProperty;
  }

  if (mAnimation->UsingScrollTimeline()) {
    const ScrollTimeline* scrollTimeline =
        mAnimation->GetTimeline()->AsScrollTimeline();
    // We don't send this animation to the compositor if
    // 1. the APZ is disabled entirely or for the source, or
    // 2. the associated scroll-timeline is inactive, or
    // 3. the scrolling direction is not available (i.e. no scroll range).
    // 4. the scroll style of the scroller is overflow:hidden.
    if (!scrollTimeline->APZIsActiveForSource() ||
        !scrollTimeline->IsActive() ||
        !scrollTimeline->ScrollingDirectionIsAvailable() ||
        scrollTimeline->SourceScrollStyle() == StyleOverflow::Hidden) {
      return KeyframeEffect::MatchForCompositor::No;
    }

    // FIXME: Bug 1818346. Support OMTA for view-timeline. We disable it for now
    // because we need to make view-timeline-inset animations run on the OMTA as
    // well before enable this.
    if (scrollTimeline->IsViewTimeline()) {
      return KeyframeEffect::MatchForCompositor::No;
    }
  }

  if (!HasEffectiveAnimationOfPropertySet(aPropertySet, aEffects)) {
    return KeyframeEffect::MatchForCompositor::No;
  }

  // If we know that the animation is not visible, we don't need to send the
  // animation to the compositor.
  if (!aFrame->IsVisibleOrMayHaveVisibleDescendants() ||
      CanOptimizeAwayDueToOpacity(*this, *aFrame) ||
      aFrame->IsScrolledOutOfView()) {
    return KeyframeEffect::MatchForCompositor::NoAndBlockThisProperty;
  }

  if (aPropertySet.HasProperty(eCSSProperty_background_color)) {
    if (!StaticPrefs::gfx_omta_background_color()) {
      return KeyframeEffect::MatchForCompositor::No;
    }

    // We don't yet support off-main-thread background-color animations on
    // canvas frame or on <html> or <body> which generate
    // nsDisplayCanvasBackgroundColor or nsDisplaySolidColor display item.
    if (aFrame->IsCanvasFrame() ||
        (aFrame->GetContent() &&
         (aFrame->GetContent()->IsHTMLElement(nsGkAtoms::body) ||
          aFrame->GetContent()->IsHTMLElement(nsGkAtoms::html)))) {
      return KeyframeEffect::MatchForCompositor::No;
    }
  }

  // We can't run this background color animation on the compositor if there
  // is any `current-color` keyframe.
  if (mCumulativeChanges.mHasBackgroundColorCurrentColor) {
    aPerformanceWarning = AnimationPerformanceWarning::Type::HasCurrentColor;
    return KeyframeEffect::MatchForCompositor::NoAndBlockThisProperty;
  }

  return mAnimation->IsPlaying() ? KeyframeEffect::MatchForCompositor::Yes
                                 : KeyframeEffect::MatchForCompositor::IfNeeded;
}

}  // namespace dom
}  // namespace mozilla
