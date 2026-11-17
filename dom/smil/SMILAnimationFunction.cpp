/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SMILAnimationFunction.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "mozilla/DebugOnly.h"
#include "mozilla/SMILAttr.h"
#include "mozilla/SMILCSSValueType.h"
#include "mozilla/SMILNullType.h"
#include "mozilla/SMILParserUtils.h"
#include "mozilla/SMILTimedElement.h"
#include "mozilla/dom/SVGAnimationElement.h"
#include "nsAttrValueInlines.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsReadableUtils.h"
#include "nsString.h"

using namespace mozilla::dom;

namespace mozilla {

//----------------------------------------------------------------------
// Static members

// Any negative number should be fine as a sentinel here,
// because valid distances are non-negative.
#define COMPUTE_DISTANCE_ERROR (-1)

//----------------------------------------------------------------------
// Constructors etc.

void SMILAnimationFunction::SetAnimationElement(
    SVGAnimationElement* aAnimationElement) {
  mAnimationElement = aAnimationElement;
}

bool SMILAnimationFunction::SetAttr(nsAtom* aAttribute, const nsAString& aValue,
                                    nsAttrValue& aResult,
                                    nsresult* aParseResult) {
  // Some elements such as set and discard don't support all possible attributes
  if (IsDisallowedAttribute(aAttribute)) {
    aResult.SetTo(aValue);
    if (aParseResult) {
      *aParseResult = NS_OK;
    }
    return true;
  }

  bool foundMatch = true;
  nsresult parseResult = NS_OK;

  // The attributes 'by', 'from', 'to', and 'values' may be parsed differently
  // depending on the element & attribute we're animating.  So instead of
  // parsing them now we re-parse them at every sample.
  if (aAttribute == nsGkAtoms::by || aAttribute == nsGkAtoms::from ||
      aAttribute == nsGkAtoms::to || aAttribute == nsGkAtoms::values) {
    // We parse to, from, by, values at sample time.
    // XXX Need to flag which attribute has changed and then when we parse it at
    // sample time, report any errors and reset the flag
    mHasChanged = true;
    aResult.SetTo(aValue);
  } else if (aAttribute == nsGkAtoms::accumulate) {
    parseResult = SetAccumulate(aValue, aResult);
  } else if (aAttribute == nsGkAtoms::additive) {
    parseResult = SetAdditive(aValue, aResult);
  } else if (aAttribute == nsGkAtoms::calcMode) {
    parseResult = SetCalcMode(aValue, aResult);
  } else if (aAttribute == nsGkAtoms::keyTimes) {
    parseResult = SetKeyTimes(aValue, aResult);
  } else if (aAttribute == nsGkAtoms::keySplines) {
    parseResult = SetKeySplines(aValue, aResult);
  } else {
    foundMatch = false;
  }

  if (foundMatch && aParseResult) {
    *aParseResult = parseResult;
  }

  return foundMatch;
}

bool SMILAnimationFunction::UnsetAttr(nsAtom* aAttribute) {
  if (IsDisallowedAttribute(aAttribute)) {
    return true;
  }

  bool foundMatch = true;

  if (aAttribute == nsGkAtoms::by || aAttribute == nsGkAtoms::from ||
      aAttribute == nsGkAtoms::to || aAttribute == nsGkAtoms::values) {
    mHasChanged = true;
  } else if (aAttribute == nsGkAtoms::accumulate) {
    UnsetAccumulate();
  } else if (aAttribute == nsGkAtoms::additive) {
    UnsetAdditive();
  } else if (aAttribute == nsGkAtoms::calcMode) {
    UnsetCalcMode();
  } else if (aAttribute == nsGkAtoms::keyTimes) {
    UnsetKeyTimes();
  } else if (aAttribute == nsGkAtoms::keySplines) {
    UnsetKeySplines();
  } else {
    foundMatch = false;
  }

  return foundMatch;
}

void SMILAnimationFunction::SampleAt(SMILTime aSampleTime,
                                     const SMILTimeValue& aSimpleDuration,
                                     uint32_t aRepeatIteration) {
  // * Update mHasChanged ("Might this sample be different from prev one?")
  // Were we previously sampling a fill="freeze" final val? (We're not anymore.)
  mHasChanged |= mLastValue;

  // Are we sampling at a new point in simple duration? And does that matter?
  mHasChanged |=
      (mSampleTime != aSampleTime || mSimpleDuration != aSimpleDuration) &&
      !IsValueFixedForSimpleDuration();

  // Are we on a new repeat and accumulating across repeats?
  if (mErrorFlags.isEmpty()) {  // (can't call GetAccumulate() if we've had
                                // parse errors)
    mHasChanged |= (mRepeatIteration != aRepeatIteration) && GetAccumulate();
  }

  mSampleTime = aSampleTime;
  mSimpleDuration = aSimpleDuration;
  mRepeatIteration = aRepeatIteration;
  mLastValue = false;
}

void SMILAnimationFunction::SampleLastValue(uint32_t aRepeatIteration) {
  if (!mLastValue || mRepeatIteration != aRepeatIteration) {
    mHasChanged = true;
  }

  mRepeatIteration = aRepeatIteration;
  mLastValue = true;
}

void SMILAnimationFunction::Activate(SMILTime aBeginTime) {
  mBeginTime = aBeginTime;
  mIsActive = true;
  mIsFrozen = false;
  mHasChanged = true;
}

void SMILAnimationFunction::Inactivate(bool aIsFrozen) {
  mIsActive = false;
  mIsFrozen = aIsFrozen;
  mHasChanged = true;
}

void SMILAnimationFunction::ComposeResult(const SMILAttr& aSMILAttr,
                                          SMILValue& aResult) {
  mHasChanged = false;
  mPrevSampleWasSingleValueAnimation = false;
  mWasSkippedInPrevSample = false;

  // Skip animations that are inactive or in error
  if (!IsActiveOrFrozen() || !mErrorFlags.isEmpty()) return;

  // Get the animation values
  SMILValueArray values;
  nsresult rv = GetValues(aSMILAttr, values);
  if (NS_FAILED(rv)) return;

  // Check that we have the right number of keySplines and keyTimes
  CheckValueListDependentAttrs(values.Length());
  if (!mErrorFlags.isEmpty()) return;

  // If this interval is active, we must have a non-negative mSampleTime
  MOZ_ASSERT(mSampleTime >= 0 || !mIsActive,
             "Negative sample time for active animation");
  MOZ_ASSERT(mSimpleDuration.IsResolved() || mLastValue,
             "Unresolved simple duration for active or frozen animation");

  // If we want to add but don't have a base value then just fail outright.
  // This can happen when we skipped getting the base value because there's an
  // animation function in the sandwich that should replace it but that function
  // failed unexpectedly.
  bool isAdditive = IsAdditive();
  if (isAdditive && aResult.IsNull()) return;

  SMILValue result;

  if (values.Length() == 1 && !IsToAnimation()) {
    // Single-valued animation
    result = values[0];
    mPrevSampleWasSingleValueAnimation = !(GetAccumulate() && mRepeatIteration);

  } else if (mLastValue) {
    // Sampling last value
    result = values.LastElement();
  } else {
    // Interpolation
    if (NS_FAILED(InterpolateResult(values, result, aResult))) return;
  }
  if (!IsToAnimation() && GetAccumulate() && mRepeatIteration) {
    // If the target attribute type doesn't support addition, Add will
    // fail and we leave result untouched.
    result.Add(values.LastElement(), mRepeatIteration);
  }

  // If additive animation isn't required or isn't supported, set the value.
  if (!isAdditive || NS_FAILED(aResult.SandwichAdd(result))) {
    aResult = std::move(result);
  }
}

int32_t SMILAnimationFunction::CompareTo(
    const SMILAnimationFunction* aOther,
    nsContentUtils::NodeIndexCache& aCache) const {
  NS_ENSURE_TRUE(aOther, 0);

  if (aOther == this) {
    // std::sort will sometimes compare an element to itself. It's fine.
    return 0;
  }

  // Inactive animations sort first
  if (!IsActiveOrFrozen() && aOther->IsActiveOrFrozen()) return -1;

  if (IsActiveOrFrozen() && !aOther->IsActiveOrFrozen()) return 1;

  // Sort based on begin time
  if (mBeginTime != aOther->GetBeginTime())
    return mBeginTime > aOther->GetBeginTime() ? 1 : -1;

  // Next sort based on syncbase dependencies: the dependent element sorts after
  // its syncbase
  const SMILTimedElement& thisTimedElement = mAnimationElement->TimedElement();
  const SMILTimedElement& otherTimedElement =
      aOther->mAnimationElement->TimedElement();
  if (thisTimedElement.IsTimeDependent(otherTimedElement)) return 1;
  if (otherTimedElement.IsTimeDependent(thisTimedElement)) return -1;

  // Animations that appear later in the document sort after those earlier in
  // the document
  MOZ_ASSERT(!HasSameAnimationElement(aOther),
             "Two animations cannot have the same animation content element!");

  return nsContentUtils::CompareTreePosition<TreeKind::ShadowIncludingDOM>(
      mAnimationElement, aOther->mAnimationElement, nullptr, &aCache);
}

bool SMILAnimationFunction::WillReplace() const {
  /*
   * In IsAdditive() we don't consider to-animation to be additive as it is
   * a special case that is dealt with differently in the compositing method.
   * Here, however, we return FALSE for to-animation (i.e. it will NOT replace
   * the underlying value) as it builds on the underlying value.
   */
  return mErrorFlags.isEmpty() && !(IsAdditive() || IsToAnimation());
}

bool SMILAnimationFunction::HasChanged() const {
  return mHasChanged || mValueNeedsReparsingEverySample;
}

bool SMILAnimationFunction::UpdateCachedTarget(
    const SMILTargetIdentifier& aNewTarget) {
  if (!mLastTarget.Equals(aNewTarget)) {
    mLastTarget = aNewTarget;
    return true;
  }
  return false;
}

//----------------------------------------------------------------------
// Implementation helpers

nsresult SMILAnimationFunction::InterpolateResult(const SMILValueArray& aValues,
                                                  SMILValue& aResult,
                                                  SMILValue& aBaseValue) {
  // Sanity check animation values
  if ((!IsToAnimation() && aValues.Length() < 2) ||
      (IsToAnimation() && aValues.Length() != 1)) {
    NS_ERROR("Unexpected number of values");
    return NS_ERROR_FAILURE;
  }

  if (IsToAnimation() && aBaseValue.IsNull()) {
    return NS_ERROR_FAILURE;
  }

  // Get the normalised progress through the simple duration.
  //
  // If we have an indefinite simple duration, just set the progress to be
  // 0 which will give us the expected behaviour of the animation being fixed at
  // its starting point.
  double simpleProgress = 0.0;

  if (mSimpleDuration.IsDefinite()) {
    SMILTime dur = mSimpleDuration.GetMillis();

    MOZ_ASSERT(dur >= 0, "Simple duration should not be negative");
    MOZ_ASSERT(mSampleTime >= 0, "Sample time should not be negative");

    if (mSampleTime >= dur || mSampleTime < 0) {
      NS_ERROR("Animation sampled outside interval");
      return NS_ERROR_FAILURE;
    }

    if (dur > 0) {
      simpleProgress = (double)mSampleTime / dur;
    }  // else leave simpleProgress at 0.0 (e.g. if mSampleTime == dur == 0)
  }

  nsresult rv = NS_OK;
  SMILCalcMode calcMode = GetCalcMode();

  // Force discrete calcMode for visibility since StyleAnimationValue will
  // try to interpolate it using the special clamping behavior defined for
  // CSS.
  if (SMILCSSValueType::PropertyFromValue(aValues[0]) ==
      eCSSProperty_visibility) {
    calcMode = SMILCalcMode::Discrete;
  }

  if (calcMode != SMILCalcMode::Discrete) {
    // Get the normalised progress between adjacent values
    const SMILValue* from = nullptr;
    const SMILValue* to = nullptr;
    // Init to -1 to make sure that if we ever forget to set this, the
    // MOZ_ASSERT that tests that intervalProgress is in range will fail.
    double intervalProgress = -1.0;
    if (IsToAnimation()) {
      from = &aBaseValue;
      to = &aValues[0];
      if (calcMode == SMILCalcMode::Paced) {
        // Note: key[Times/Splines/Points] are ignored for calcMode="paced"
        intervalProgress = simpleProgress;
      } else {
        double scaledSimpleProgress =
            ScaleSimpleProgress(simpleProgress, calcMode, 1.0);
        intervalProgress = ScaleIntervalProgress(scaledSimpleProgress, 0);
      }
    } else if (calcMode == SMILCalcMode::Paced) {
      rv = ComputePacedPosition(aValues, simpleProgress, intervalProgress, from,
                                to);
      // Note: If the above call fails, we'll skip the "from->Interpolate"
      // call below, and we'll drop into the SMILCalcMode::Discrete section
      // instead. (as the spec says we should, because our failure was
      // presumably due to the values being non-additive)
    } else {  // calcMode == SMILCalcMode::Linear or calcMode ==
              // SMILCalcMode::Spline
      double scaledSimpleProgress =
          ScaleSimpleProgress(simpleProgress, calcMode, aValues.Length() - 1);
      uint32_t index = (uint32_t)std::floor(scaledSimpleProgress);
      from = &aValues[index];
      to = &aValues[index + 1];
      intervalProgress =
          ScaleIntervalProgress(scaledSimpleProgress - index, index);
    }

    if (NS_SUCCEEDED(rv)) {
      MOZ_ASSERT(from, "NULL from-value during interpolation");
      MOZ_ASSERT(to, "NULL to-value during interpolation");
      MOZ_ASSERT(0.0 <= intervalProgress && intervalProgress < 1.0,
                 "Interval progress should be in the range [0, 1)");
      rv = from->Interpolate(*to, intervalProgress, aResult);
    }
  }

  // Discrete-CalcMode case
  // Note: If interpolation failed (isn't supported for this type), the SVG
  // spec says to force discrete mode.
  if (calcMode == SMILCalcMode::Discrete || NS_FAILED(rv)) {
    if (IsToAnimation()) {
      // We don't follow SMIL 3, 12.6.4, where discrete to animations
      // are the same as <set> animations.  Instead, we treat it as a
      // discrete animation with two values (the underlying value and
      // the to="" value), and honor keyTimes="" as well.
      double scaledSimpleProgress =
          ScaleSimpleProgress(simpleProgress, SMILCalcMode::Discrete, 2.0);
      uint32_t index = (uint32_t)std::floor(scaledSimpleProgress);
      aResult = index == 0 ? aBaseValue : aValues[0];
    } else {
      double scaledSimpleProgress = ScaleSimpleProgress(
          simpleProgress, SMILCalcMode::Discrete, aValues.Length());
      uint32_t index = (uint32_t)std::floor(scaledSimpleProgress);
      aResult = aValues[index];

      // For animation of CSS properties, normally when interpolating we perform
      // a zero-value fixup which means that empty values (values with type
      // SMILCSSValueType but a null pointer value) are converted into
      // a suitable zero value based on whatever they're being interpolated
      // with. For discrete animation, however, since we don't interpolate,
      // that never happens. In some rare cases, such as discrete non-additive
      // by-animation, we can arrive here with |aResult| being such an empty
      // value so we need to manually perform the fixup.
      //
      // We could define a generic method for this on SMILValue but its faster
      // and simpler to just special case SMILCSSValueType.
      if (aResult.mType == &SMILCSSValueType::sSingleton) {
        // We have currently only ever encountered this case for the first
        // value of a by-animation (which has two values) and since we have no
        // way of testing other cases we just skip them (but assert if we
        // ever do encounter them so that we can add code to handle them).
        if (index + 1 >= aValues.Length()) {
          MOZ_ASSERT(aResult.mU.mPtr, "The last value should not be empty");
        } else {
          // Base the type of the zero value on the next element in the series.
          SMILCSSValueType::FinalizeValue(aResult, aValues[index + 1]);
        }
      }
    }
    rv = NS_OK;
  }
  return rv;
}

/*
 * Given the simple progress for a paced animation, this method:
 *  - determines which two elements of the values array we're in between
 *    (returned as aFrom and aTo)
 *  - determines where we are between them
 *    (returned as aIntervalProgress)
 *
 * Returns NS_OK, or NS_ERROR_FAILURE if our values don't support distance
 * computation.
 */
nsresult SMILAnimationFunction::ComputePacedPosition(
    const SMILValueArray& aValues, double aSimpleProgress,
    double& aIntervalProgress, const SMILValue*& aFrom, const SMILValue*& aTo) {
  NS_ASSERTION(0.0f <= aSimpleProgress && aSimpleProgress < 1.0f,
               "aSimpleProgress is out of bounds");
  NS_ASSERTION(GetCalcMode() == SMILCalcMode::Paced,
               "Calling paced-specific function, but not in paced mode");
  MOZ_ASSERT(aValues.Length() >= 2, "Unexpected number of values");

  // Trivial case: If we have just 2 values, then there's only one interval
  // for us to traverse, and our progress across that interval is the exact
  // same as our overall progress.
  if (aValues.Length() == 2) {
    aIntervalProgress = aSimpleProgress;
    aFrom = &aValues[0];
    aTo = &aValues[1];
    return NS_OK;
  }

  double totalDistance = ComputePacedTotalDistance(aValues);
  if (totalDistance == COMPUTE_DISTANCE_ERROR) return NS_ERROR_FAILURE;

  // If we have 0 total distance, then it's unclear where our "paced" position
  // should be.  We can just fail, which drops us into discrete animation mode.
  // (That's fine, since our values are apparently indistinguishable anyway.)
  if (totalDistance == 0.0) {
    return NS_ERROR_FAILURE;
  }

  // total distance we should have moved at this point in time.
  // (called 'remainingDist' due to how it's used in loop below)
  double remainingDist = aSimpleProgress * totalDistance;

  // Must be satisfied, because totalDistance is a sum of (non-negative)
  // distances, and aSimpleProgress is non-negative
  NS_ASSERTION(remainingDist >= 0, "distance values must be non-negative");

  // Find where remainingDist puts us in the list of values
  // Note: We could optimize this next loop by caching the
  // interval-distances in an array, but maybe that's excessive.
  for (uint32_t i = 0; i < aValues.Length() - 1; i++) {
    // Note: The following assertion is valid because remainingDist should
    // start out non-negative, and this loop never shaves off more than its
    // current value.
    NS_ASSERTION(remainingDist >= 0, "distance values must be non-negative");

    double curIntervalDist;

    DebugOnly<nsresult> rv =
        aValues[i].ComputeDistance(aValues[i + 1], curIntervalDist);
    MOZ_ASSERT(NS_SUCCEEDED(rv),
               "If we got through ComputePacedTotalDistance, we should "
               "be able to recompute each sub-distance without errors");

    NS_ASSERTION(curIntervalDist >= 0, "distance values must be non-negative");
    // Clamp distance value at 0, just in case ComputeDistance is evil.
    curIntervalDist = std::max(curIntervalDist, 0.0);

    if (remainingDist >= curIntervalDist) {
      remainingDist -= curIntervalDist;
    } else {
      // NOTE: If we get here, then curIntervalDist necessarily is not 0. Why?
      // Because this clause is only hit when remainingDist < curIntervalDist,
      // and if curIntervalDist were 0, that would mean remainingDist would
      // have to be < 0.  But that can't happen, because remainingDist (as
      // a distance) is non-negative by definition.
      NS_ASSERTION(curIntervalDist != 0,
                   "We should never get here with this set to 0...");

      // We found the right spot -- an interpolated position between
      // values i and i+1.
      aFrom = &aValues[i];
      aTo = &aValues[i + 1];
      aIntervalProgress = remainingDist / curIntervalDist;
      return NS_OK;
    }
  }

  MOZ_ASSERT_UNREACHABLE(
      "shouldn't complete loop & get here -- if we do, "
      "then aSimpleProgress was probably out of bounds");
  return NS_ERROR_FAILURE;
}

/*
 * Computes the total distance to be travelled by a paced animation.
 *
 * Returns the total distance, or returns COMPUTE_DISTANCE_ERROR if
 * our values don't support distance computation.
 */
double SMILAnimationFunction::ComputePacedTotalDistance(
    const SMILValueArray& aValues) const {
  NS_ASSERTION(GetCalcMode() == SMILCalcMode::Paced,
               "Calling paced-specific function, but not in paced mode");

  double totalDistance = 0.0;
  for (uint32_t i = 0; i < aValues.Length() - 1; i++) {
    double tmpDist;
    nsresult rv = aValues[i].ComputeDistance(aValues[i + 1], tmpDist);
    if (NS_FAILED(rv)) {
      return COMPUTE_DISTANCE_ERROR;
    }

    // Clamp distance value to 0, just in case we have an evil ComputeDistance
    // implementation somewhere
    MOZ_ASSERT(tmpDist >= 0.0f, "distance values must be non-negative");
    tmpDist = std::max(tmpDist, 0.0);

    totalDistance += tmpDist;
  }

  return totalDistance;
}

double SMILAnimationFunction::ScaleSimpleProgress(double aProgress,
                                                  SMILCalcMode aCalcMode,
                                                  double aValueMultiplier) {
  auto Scale = [this](double aProgress, SMILCalcMode aCalcMode) {
    if (!HasAttr(nsGkAtoms::keyTimes)) return aProgress;

    uint32_t numTimes = mKeyTimes.Length();

    if (numTimes < 2) return aProgress;

    uint32_t i = 0;
    for (; i < numTimes - 2 && aProgress >= mKeyTimes[i + 1]; ++i) {
    }

    if (aCalcMode == SMILCalcMode::Discrete) {
      // discrete calcMode behaviour differs in that each keyTime defines the
      // time from when the corresponding value is set, and therefore the last
      // value needn't be 1. So check if we're in the last 'interval', that is,
      // the space between the final value and 1.0.
      if (aProgress >= mKeyTimes[i + 1]) {
        MOZ_ASSERT(i == numTimes - 2,
                   "aProgress is not in range of the current interval, yet the "
                   "current interval is not the last bounded interval either.");
        ++i;
      }
      return (double)i / numTimes;
    }

    double& intervalStart = mKeyTimes[i];
    double& intervalEnd = mKeyTimes[i + 1];

    double intervalLength = intervalEnd - intervalStart;
    if (intervalLength <= 0.0) return intervalStart;

    return (i + (aProgress - intervalStart) / intervalLength) /
           double(numTimes - 1);
  };

  double scaledSimpleProgress = Scale(aProgress, aCalcMode);

  // Floating-point errors can mean that, for example, a sample time of 29s in
  // a 100s duration animation gives us a simple progress of 0.28999999999
  // instead of the 0.29 we'd expect. Normally this isn't a noticeable
  // problem, but when we have sudden jumps in animation values (e.g.
  // discrete animation) we can get unexpected results.
  //
  // To counteract this, before we perform a floor() on the animation
  // progress, we add a tiny fudge factor to push us into the correct interval
  // in cases where floating-point errors might cause us to fall short.
  static const double kFloatingPointFudgeFactor = 1.0e-16;
  if (std::floor(scaledSimpleProgress * aValueMultiplier) !=
          std::floor((scaledSimpleProgress + kFloatingPointFudgeFactor) *
                     aValueMultiplier) &&
      scaledSimpleProgress + kFloatingPointFudgeFactor <= 1.0) {
    scaledSimpleProgress += kFloatingPointFudgeFactor;
  }
  return scaledSimpleProgress * aValueMultiplier;
}

double SMILAnimationFunction::ScaleIntervalProgress(double aProgress,
                                                    uint32_t aIntervalIndex) {
  if (GetCalcMode() != SMILCalcMode::Spline) return aProgress;

  if (!HasAttr(nsGkAtoms::keySplines)) return aProgress;

  MOZ_ASSERT(aIntervalIndex < mKeySplines.Length(), "Invalid interval index");

  SMILKeySpline const& spline = mKeySplines[aIntervalIndex];
  return spline.GetSplineValue(aProgress);
}

bool SMILAnimationFunction::HasAttr(nsAtom* aAttName) const {
  if (IsDisallowedAttribute(aAttName)) {
    return false;
  }
  return mAnimationElement->HasAttr(aAttName);
}

const nsAttrValue* SMILAnimationFunction::GetAttr(nsAtom* aAttName) const {
  if (IsDisallowedAttribute(aAttName)) {
    return nullptr;
  }
  return mAnimationElement->GetParsedAttr(aAttName);
}

bool SMILAnimationFunction::GetAttr(nsAtom* aAttName,
                                    nsAString& aResult) const {
  if (IsDisallowedAttribute(aAttName)) {
    return false;
  }
  return mAnimationElement->GetAttr(aAttName, aResult);
}

/*
 * A utility function to make querying an attribute that corresponds to an
 * SMILValue a little neater.
 *
 * @param aAttName    The attribute name (in the global namespace).
 * @param aSMILAttr   The SMIL attribute to perform the parsing.
 * @param[out] aResult        The resulting SMILValue.
 * @param[out] aPreventCachingOfSandwich
 *                    If |aResult| contains dependencies on its context that
 *                    should prevent the result of the animation sandwich from
 *                    being cached and reused in future samples (as reported
 *                    by SMILAttr::ValueFromString), then this outparam
 *                    will be set to true. Otherwise it is left unmodified.
 *
 * Returns false if a parse error occurred, otherwise returns true.
 */
bool SMILAnimationFunction::ParseAttr(nsAtom* aAttName,
                                      const SMILAttr& aSMILAttr,
                                      SMILValue& aResult,
                                      bool& aPreventCachingOfSandwich) const {
  nsAutoString attValue;
  if (GetAttr(aAttName, attValue)) {
    nsresult rv = aSMILAttr.ValueFromString(attValue, mAnimationElement,
                                            aResult, aPreventCachingOfSandwich);
    if (NS_FAILED(rv)) return false;
  }
  return true;
}

/*
 * SMILANIM specifies the following rules for animation function values:
 *
 * (1) if values is set, it overrides everything
 * (2) for from/to/by animation at least to or by must be specified, from on its
 *     own (or nothing) is an error--which we will ignore
 * (3) if both by and to are specified only to will be used, by will be ignored
 * (4) if by is specified without from (by animation), forces additive behaviour
 * (5) if to is specified without from (to animation), special care needs to be
 *     taken when compositing animation as such animations are composited last.
 *
 * This helper method applies these rules to fill in the values list and to set
 * some internal state.
 */
nsresult SMILAnimationFunction::GetValues(const SMILAttr& aSMILAttr,
                                          SMILValueArray& aResult) {
  if (!mAnimationElement) return NS_ERROR_FAILURE;

  mValueNeedsReparsingEverySample = false;
  SMILValueArray result;

  // If "values" is set, use it
  if (HasAttr(nsGkAtoms::values)) {
    nsAutoString attValue;
    GetAttr(nsGkAtoms::values, attValue);
    bool preventCachingOfSandwich = false;
    if (!SMILParserUtils::ParseValues(attValue, mAnimationElement, aSMILAttr,
                                      result, preventCachingOfSandwich)) {
      return NS_ERROR_FAILURE;
    }

    if (preventCachingOfSandwich) {
      mValueNeedsReparsingEverySample = true;
    }
    // Else try to/from/by
  } else {
    bool preventCachingOfSandwich = false;
    bool parseOk = true;
    SMILValue to, from, by;
    parseOk &=
        ParseAttr(nsGkAtoms::to, aSMILAttr, to, preventCachingOfSandwich);
    parseOk &=
        ParseAttr(nsGkAtoms::from, aSMILAttr, from, preventCachingOfSandwich);
    parseOk &=
        ParseAttr(nsGkAtoms::by, aSMILAttr, by, preventCachingOfSandwich);

    if (preventCachingOfSandwich) {
      mValueNeedsReparsingEverySample = true;
    }

    if (!parseOk || !result.SetCapacity(2, fallible)) {
      return NS_ERROR_FAILURE;
    }

    // AppendElement() below must succeed, because SetCapacity() succeeded.
    if (!to.IsNull()) {
      if (!from.IsNull()) {
        MOZ_ALWAYS_TRUE(result.AppendElement(from, fallible));
        MOZ_ALWAYS_TRUE(result.AppendElement(to, fallible));
      } else {
        MOZ_ALWAYS_TRUE(result.AppendElement(to, fallible));
      }
    } else if (!by.IsNull()) {
      SMILValue effectiveFrom(by.mType);
      if (!from.IsNull()) effectiveFrom = from;
      // Set values to 'from; from + by'
      MOZ_ALWAYS_TRUE(result.AppendElement(effectiveFrom, fallible));
      SMILValue effectiveTo(effectiveFrom);
      if (!effectiveTo.IsNull() && NS_SUCCEEDED(effectiveTo.Add(by))) {
        MOZ_ALWAYS_TRUE(result.AppendElement(effectiveTo, fallible));
      } else {
        // Using by-animation with non-additive type or bad base-value
        return NS_ERROR_FAILURE;
      }
    } else {
      // No values, no to, no by -- call it a day
      return NS_ERROR_FAILURE;
    }
  }

  aResult = std::move(result);

  return NS_OK;
}

void SMILAnimationFunction::CheckValueListDependentAttrs(uint32_t aNumValues) {
  CheckKeyTimes(aNumValues);
  CheckKeySplines(aNumValues);
}

/**
 * Performs checks for the keyTimes attribute required by the SMIL spec but
 * which depend on other attributes and therefore needs to be updated as
 * dependent attributes are set.
 */
void SMILAnimationFunction::CheckKeyTimes(uint32_t aNumValues) {
  if (!HasAttr(nsGkAtoms::keyTimes)) return;

  SMILCalcMode calcMode = GetCalcMode();

  // attribute is ignored for calcMode = paced
  if (calcMode == SMILCalcMode::Paced) {
    SetKeyTimesErrorFlag(false);
    return;
  }

  uint32_t numKeyTimes = mKeyTimes.Length();
  if (numKeyTimes < 1) {
    // keyTimes isn't set or failed preliminary checks
    SetKeyTimesErrorFlag(true);
    return;
  }

  // no. keyTimes == no. values
  // For to-animation the number of values is considered to be 2.
  bool matchingNumOfValues = numKeyTimes == (IsToAnimation() ? 2 : aNumValues);
  if (!matchingNumOfValues) {
    SetKeyTimesErrorFlag(true);
    return;
  }

  // first value must be 0
  if (mKeyTimes[0] != 0.0) {
    SetKeyTimesErrorFlag(true);
    return;
  }

  // last value must be 1 for linear or spline calcModes
  if (calcMode != SMILCalcMode::Discrete && numKeyTimes > 1 &&
      mKeyTimes.LastElement() != 1.0) {
    SetKeyTimesErrorFlag(true);
    return;
  }

  SetKeyTimesErrorFlag(false);
}

void SMILAnimationFunction::CheckKeySplines(uint32_t aNumValues) {
  // attribute is ignored if calc mode is not spline
  if (GetCalcMode() != SMILCalcMode::Spline) {
    SetKeySplinesErrorFlag(false);
    return;
  }

  // calc mode is spline but the attribute is not set
  if (!HasAttr(nsGkAtoms::keySplines)) {
    SetKeySplinesErrorFlag(false);
    return;
  }

  if (mKeySplines.IsEmpty()) {
    // keyTimes isn't set or failed preliminary checks
    SetKeySplinesErrorFlag(true);
    return;
  }

  // ignore splines if there's only one value
  if (aNumValues == 1 && !IsToAnimation()) {
    SetKeySplinesErrorFlag(false);
    return;
  }

  // no. keySpline specs == no. values - 1
  uint32_t splineSpecs = mKeySplines.Length();
  if ((splineSpecs != aNumValues - 1 && !IsToAnimation()) ||
      (IsToAnimation() && splineSpecs != 1)) {
    SetKeySplinesErrorFlag(true);
    return;
  }

  SetKeySplinesErrorFlag(false);
}

bool SMILAnimationFunction::IsValueFixedForSimpleDuration() const {
  return mSimpleDuration.IsIndefinite() ||
         (!mHasChanged && mPrevSampleWasSingleValueAnimation);
}

//----------------------------------------------------------------------
// Property getters

bool SMILAnimationFunction::GetAccumulate() const {
  const nsAttrValue* value = GetAttr(nsGkAtoms::accumulate);
  if (!value) return false;

  return value->GetEnumValue();
}

bool SMILAnimationFunction::GetAdditive() const {
  const nsAttrValue* value = GetAttr(nsGkAtoms::additive);
  if (!value) return false;

  return value->GetEnumValue();
}

SMILAnimationFunction::SMILCalcMode SMILAnimationFunction::GetCalcMode() const {
  const nsAttrValue* value = GetAttr(nsGkAtoms::calcMode);
  if (!value) return SMILCalcMode::Linear;

  return SMILCalcMode(value->GetEnumValue());
}

//----------------------------------------------------------------------
// Property setters / un-setters:

nsresult SMILAnimationFunction::SetAccumulate(const nsAString& aAccumulate,
                                              nsAttrValue& aResult) {
  mHasChanged = true;
  bool parseResult =
      aResult.ParseEnumValue(aAccumulate, sAccumulateTable, true);
  SetAccumulateErrorFlag(!parseResult);
  return parseResult ? NS_OK : NS_ERROR_FAILURE;
}

void SMILAnimationFunction::UnsetAccumulate() {
  SetAccumulateErrorFlag(false);
  mHasChanged = true;
}

nsresult SMILAnimationFunction::SetAdditive(const nsAString& aAdditive,
                                            nsAttrValue& aResult) {
  mHasChanged = true;
  bool parseResult = aResult.ParseEnumValue(aAdditive, sAdditiveTable, true);
  SetAdditiveErrorFlag(!parseResult);
  return parseResult ? NS_OK : NS_ERROR_FAILURE;
}

void SMILAnimationFunction::UnsetAdditive() {
  SetAdditiveErrorFlag(false);
  mHasChanged = true;
}

nsresult SMILAnimationFunction::SetCalcMode(const nsAString& aCalcMode,
                                            nsAttrValue& aResult) {
  mHasChanged = true;
  bool parseResult = aResult.ParseEnumValue(aCalcMode, sCalcModeTable, true);
  SetCalcModeErrorFlag(!parseResult);
  return parseResult ? NS_OK : NS_ERROR_FAILURE;
}

void SMILAnimationFunction::UnsetCalcMode() {
  SetCalcModeErrorFlag(false);
  mHasChanged = true;
}

nsresult SMILAnimationFunction::SetKeySplines(const nsAString& aKeySplines,
                                              nsAttrValue& aResult) {
  mKeySplines.Clear();
  aResult.SetTo(aKeySplines);

  mHasChanged = true;

  if (!SMILParserUtils::ParseKeySplines(aKeySplines, mKeySplines)) {
    mKeySplines.Clear();
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void SMILAnimationFunction::UnsetKeySplines() {
  mKeySplines.Clear();
  SetKeySplinesErrorFlag(false);
  mHasChanged = true;
}

nsresult SMILAnimationFunction::SetKeyTimes(const nsAString& aKeyTimes,
                                            nsAttrValue& aResult) {
  mKeyTimes.Clear();
  aResult.SetTo(aKeyTimes);

  mHasChanged = true;

  if (!SMILParserUtils::ParseSemicolonDelimitedProgressList(aKeyTimes, true,
                                                            mKeyTimes)) {
    mKeyTimes.Clear();
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void SMILAnimationFunction::UnsetKeyTimes() {
  mKeyTimes.Clear();
  SetKeyTimesErrorFlag(false);
  mHasChanged = true;
}

}  // namespace mozilla
