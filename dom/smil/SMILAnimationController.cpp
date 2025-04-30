/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SMILAnimationController.h"

#include <algorithm>

#include "mozilla/AutoRestore.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/SMILTimedElement.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/SVGAnimationElement.h"
#include "nsContentUtils.h"
#include "nsCSSProps.h"
#include "nsRefreshDriver.h"
#include "mozilla/dom/Document.h"
#include "SMILCompositor.h"
#include "SMILCSSProperty.h"

using namespace mozilla::dom;

namespace mozilla {

//----------------------------------------------------------------------
// SMILAnimationController implementation

//----------------------------------------------------------------------
// ctors, dtors, factory methods

SMILAnimationController::SMILAnimationController(Document* aDoc)
    : mDocument(aDoc) {
  MOZ_ASSERT(aDoc, "need a non-null document");

  if (nsRefreshDriver* refreshDriver = GetRefreshDriver()) {
    mStartTime = refreshDriver->MostRecentRefresh();
  } else {
    mStartTime = mozilla::TimeStamp::Now();
  }
  mCurrentSampleTime = mStartTime;

  Begin();
}

SMILAnimationController::~SMILAnimationController() {
  NS_ASSERTION(mAnimationElementTable.IsEmpty(),
               "Animation controller shouldn't be tracking any animation"
               " elements when it dies");
}

void SMILAnimationController::Disconnect() {
  MOZ_ASSERT(mDocument, "disconnecting when we weren't connected...?");
  MOZ_ASSERT(mRefCnt.get() == 1,
             "Expecting to disconnect when doc is sole remaining owner");
  NS_ASSERTION(mPauseState & SMILTimeContainer::PAUSE_PAGEHIDE,
               "Expecting to be paused for pagehide before disconnect");
  mDocument = nullptr;  // (raw pointer)
}

//----------------------------------------------------------------------
// SMILTimeContainer methods:

void SMILAnimationController::Pause(uint32_t aType) {
  SMILTimeContainer::Pause(aType);
  UpdateSampling();
}

void SMILAnimationController::Resume(uint32_t aType) {
  bool wasPaused = !!mPauseState;
  // Update mCurrentSampleTime so that calls to GetParentTime--used for
  // calculating parent offsets--are accurate
  mCurrentSampleTime = mozilla::TimeStamp::Now();

  SMILTimeContainer::Resume(aType);

  if (wasPaused && !mPauseState) {
    UpdateSampling();
  }
}

SMILTime SMILAnimationController::GetParentTime() const {
  return (SMILTime)(mCurrentSampleTime - mStartTime).ToMilliseconds();
}

// nsRefreshDriver Callback function
void SMILAnimationController::WillRefresh(mozilla::TimeStamp aTime) {
  if (!mIsSampling) {
    return;
  }
  // Although we never expect aTime to go backwards, when we initialise the
  // animation controller, if we can't get hold of a refresh driver we
  // initialise mCurrentSampleTime to Now(). It may be possible that after
  // doing so we get sampled by a refresh driver whose most recent refresh time
  // predates when we were initialised, so to be safe we make sure to take the
  // most recent time here.
  aTime = std::max(mCurrentSampleTime, aTime);

  // Sleep detection: If the time between samples is a whole lot greater than we
  // were expecting then we assume the computer went to sleep or someone's
  // messing with the clock. In that case, fiddle our parent offset and use our
  // average time between samples to calculate the new sample time. This
  // prevents us from hanging while trying to catch up on all the missed time.

  // Smoothing of coefficient for the average function. 0.2 should let us track
  // the sample rate reasonably tightly without being overly affected by
  // occasional delays.
  static const double SAMPLE_DUR_WEIGHTING = 0.2;
  // If the elapsed time exceeds our expectation by this number of times we'll
  // initiate special behaviour to basically ignore the intervening time.
  static const double SAMPLE_DEV_THRESHOLD = 200.0;

  SMILTime elapsedTime =
      (SMILTime)(aTime - mCurrentSampleTime).ToMilliseconds();
  if (mAvgTimeBetweenSamples == 0) {
    // First sample.
    mAvgTimeBetweenSamples = elapsedTime;
  } else {
    if (elapsedTime > SAMPLE_DEV_THRESHOLD * mAvgTimeBetweenSamples) {
      // Unexpectedly long delay between samples.
      NS_WARNING(
          "Detected really long delay between samples, continuing from "
          "previous sample");
      mParentOffset += elapsedTime - mAvgTimeBetweenSamples;
    }
    // Update the moving average. Due to truncation here the average will
    // normally be a little less than it should be but that's probably ok.
    mAvgTimeBetweenSamples =
        (SMILTime)(elapsedTime * SAMPLE_DUR_WEIGHTING +
                   mAvgTimeBetweenSamples * (1.0 - SAMPLE_DUR_WEIGHTING));
  }
  mCurrentSampleTime = aTime;

  Sample();
  UpdateSampling();
}

//----------------------------------------------------------------------
// Animation element registration methods:

void SMILAnimationController::RegisterAnimationElement(
    SVGAnimationElement* aAnimationElement) {
  const bool wasEmpty = mAnimationElementTable.IsEmpty();
  mAnimationElementTable.PutEntry(aAnimationElement);
  if (wasEmpty) {
    UpdateSampling();
  }
}

void SMILAnimationController::UnregisterAnimationElement(
    SVGAnimationElement* aAnimationElement) {
  mAnimationElementTable.RemoveEntry(aAnimationElement);
  if (mAnimationElementTable.IsEmpty()) {
    UpdateSampling();
  }
}

//----------------------------------------------------------------------
// Page show/hide

void SMILAnimationController::OnPageShow() {
  Resume(SMILTimeContainer::PAUSE_PAGEHIDE);
}

void SMILAnimationController::OnPageHide() {
  Pause(SMILTimeContainer::PAUSE_PAGEHIDE);
}

//----------------------------------------------------------------------
// Cycle-collection support

void SMILAnimationController::Traverse(
    nsCycleCollectionTraversalCallback* aCallback) {
  // Traverse last compositor table
  if (mLastCompositorTable) {
    for (SMILCompositor& compositor : *mLastCompositorTable) {
      compositor.Traverse(aCallback);
    }
  }
}

void SMILAnimationController::Unlink() { mLastCompositorTable = nullptr; }

//----------------------------------------------------------------------
// Timer-related implementation helpers

bool SMILAnimationController::ShouldSample() const {
  return !mPauseState && !mAnimationElementTable.IsEmpty() &&
         !mChildContainerTable.IsEmpty();
}

void SMILAnimationController::UpdateSampling() {
  const bool shouldSample = ShouldSample();
  if (!shouldSample) {
    mIsSampling = false;
    return;
  }
  mDocument->MaybeScheduleRenderingPhases(
      {RenderingPhase::UpdateAnimationsAndSendEvents});
  if (!mIsSampling) {
    mIsSampling = true;
    // We're effectively resuming from a pause so update our current sample time
    // or else it will confuse our "average time between samples" calculations.
    mCurrentSampleTime = mozilla::TimeStamp::Now();
    // TODO(emilio): Not doing the sync first sample breaks
    // test_smilDynamicDelayedBeginElement.xhtml which tests for this
    // explicitly. Maybe we can avoid this?
    Sample();  // Run the first sample manually.
  }
}

//----------------------------------------------------------------------
// Sample-related methods and callbacks

static void ProcessDiscards(
    const SMILAnimationController::DiscardArray& aDiscards) {
  SMILAnimationController::DiscardArray::ForwardIterator iter(aDiscards);
  while (iter.HasMore()) {
    iter.GetNext()->Remove();
  }
}

void SMILAnimationController::DoSample() {
  DoSample(true);  // Skip unchanged time containers
}

void SMILAnimationController::DoSample(bool aSkipUnchangedContainers) {
  if (!mDocument) {
    NS_ERROR("Shouldn't be sampling after document has disconnected");
    return;
  }
  if (mRunningSample) {
    NS_ERROR("Shouldn't be recursively sampling");
    return;
  }

  mResampleNeeded = false;

  // Set running sample flag -- do this before flushing styles so that when we
  // flush styles we don't end up requesting extra samples
  AutoRestore<bool> autoRestoreRunningSample(mRunningSample);
  mRunningSample = true;

  // STEP 1: Bring model up to date
  // (i)  Rewind elements where necessary
  // (ii) Run milestone samples
  RewindElements();
  DoMilestoneSamples();

  // STEP 2: Sample the child time containers
  //
  // When we sample the child time containers they will simply record the sample
  // time in document time.
  TimeContainerHashtable activeContainers(mChildContainerTable.Count());
  for (SMILTimeContainer* container : mChildContainerTable.Keys()) {
    if (!container) {
      continue;
    }

    if (!container->IsPausedByType(SMILTimeContainer::PAUSE_BEGIN) &&
        (container->NeedsSample() || !aSkipUnchangedContainers)) {
      container->ClearMilestones();
      container->Sample();
      container->MarkSeekFinished();
      activeContainers.PutEntry(container);
    }
  }

  // STEP 3: (i)  Sample the timed elements AND
  //         (ii) Create a table of compositors
  //
  // (i) Here we sample the timed elements (fetched from the
  // SVGAnimationElements) which determine from the active time if the
  // element is active and what its simple time etc. is. This information is
  // then passed to its time client (SMILAnimationFunction).
  //
  // (ii) During the same loop we also build up a table that contains one
  // compositor for each animated attribute and which maps animated elements to
  // the corresponding compositor for their target attribute.
  //
  // Note that this compositor table needs to be allocated on the heap so we can
  // store it until the next sample. This lets us find out which elements were
  // animated in sample 'n-1' but not in sample 'n' (and hence need to have
  // their animation effects removed in sample 'n').
  //
  // Parts (i) and (ii) are not functionally related but we combine them here to
  // save iterating over the animation elements twice.

  // Create the compositor table
  UniquePtr<SMILCompositorTable> currentCompositorTable(
      new SMILCompositorTable(0));
  nsTArray<RefPtr<SVGAnimationElement>> animElems(
      mAnimationElementTable.Count());

  SMILAnimationController::DiscardArray discards;

  for (SVGAnimationElement* animElem : mAnimationElementTable.Keys()) {
    SampleTimedElement(animElem, discards, &activeContainers);
    AddAnimationToCompositorTable(animElem, currentCompositorTable.get());
    animElems.AppendElement(animElem);
  }
  activeContainers.Clear();
  ProcessDiscards(discards);
  discards.Clear();

  // STEP 4: Compare previous sample's compositors against this sample's.
  // (Transfer cached base values across, & remove animation effects from
  // no-longer-animated targets.)
  if (mLastCompositorTable) {
    // * Transfer over cached base values, from last sample's compositors
    for (SMILCompositor& compositor : *currentCompositorTable) {
      SMILCompositor* lastCompositor =
          mLastCompositorTable->GetEntry(compositor.GetKey());

      if (lastCompositor) {
        compositor.StealCachedBaseValue(lastCompositor);
        if (!lastCompositor->HasSameNumberOfAnimationFunctionsAs(compositor)) {
          // If we have multiple animations on the same element, they share a
          // compositor. If an active animation ends, it will no longer be in
          // the compositor table. We need to force compositing to ensure we
          // render the element with any remaining frozen animations even though
          // they would not normally trigger compositing.
          compositor.ToggleForceCompositing();
        }
      }
    }

    // * For each compositor in current sample's hash table, remove entry from
    // prev sample's hash table -- we don't need to clear animation
    // effects of those compositors, since they're still being animated.
    for (const auto& key : currentCompositorTable->Keys()) {
      mLastCompositorTable->RemoveEntry(key);
    }

    // * For each entry that remains in prev sample's hash table (i.e. for
    // every target that's no longer animated), clear animation effects.
    for (SMILCompositor& compositor : *mLastCompositorTable) {
      compositor.ClearAnimationEffects();
    }
  }

  // return early if there are no active animations to avoid a style flush
  if (currentCompositorTable->IsEmpty()) {
    mLastCompositorTable = nullptr;
    return;
  }

  // STEP 5: Compose currently-animated attributes.
  // XXXdholbert: This step traverses our animation targets in an effectively
  // random order. For animation from/to 'inherit' values to work correctly
  // when the inherited value is *also* being animated, we really should be
  // traversing our animated nodes in an ancestors-first order (bug 501183)
  bool mightHavePendingStyleUpdates = false;
  for (auto& compositor : *currentCompositorTable) {
    compositor.ComposeAttribute(mightHavePendingStyleUpdates);
  }

  // Update last compositor table
  mLastCompositorTable = std::move(currentCompositorTable);
  mMightHavePendingStyleUpdates = mightHavePendingStyleUpdates;

  NS_ASSERTION(!mResampleNeeded, "Resample dirty flag set during sample!");
}

void SMILAnimationController::RewindElements() {
  const bool rewindNeeded = std::any_of(
      mChildContainerTable.Keys().cbegin(), mChildContainerTable.Keys().cend(),
      [](SMILTimeContainer* container) { return container->NeedsRewind(); });

  if (!rewindNeeded) return;

  for (SVGAnimationElement* animElem : mAnimationElementTable.Keys()) {
    SMILTimeContainer* timeContainer = animElem->GetTimeContainer();
    if (timeContainer && timeContainer->NeedsRewind()) {
      animElem->TimedElement().Rewind();
    }
  }

  for (SMILTimeContainer* container : mChildContainerTable.Keys()) {
    container->ClearNeedsRewind();
  }
}

void SMILAnimationController::DoMilestoneSamples() {
  // We need to sample the timing model but because SMIL operates independently
  // of the frame-rate, we can get one sample at t=0s and the next at t=10min.
  //
  // In between those two sample times a whole string of significant events
  // might be expected to take place: events firing, new interdependencies
  // between animations resolved and dissolved, etc.
  //
  // Furthermore, at any given time, we want to sample all the intervals that
  // end at that time BEFORE any that begin. This behaviour is implied by SMIL's
  // endpoint-exclusive timing model.
  //
  // So we have the animations (specifically the timed elements) register the
  // next significant moment (called a milestone) in their lifetime and then we
  // step through the model at each of these moments and sample those animations
  // registered for those times. This way events can fire in the correct order,
  // dependencies can be resolved etc.

  SMILTime sampleTime = std::numeric_limits<SMILTime>::min();

  while (true) {
    // We want to find any milestones AT OR BEFORE the current sample time so we
    // initialise the next milestone to the moment after (1ms after, to be
    // precise) the current sample time and see if there are any milestones
    // before that. Any other milestones will be dealt with in a subsequent
    // sample.
    SMILMilestone nextMilestone(GetCurrentTimeAsSMILTime() + 1, true);
    for (SMILTimeContainer* container : mChildContainerTable.Keys()) {
      if (container->IsPausedByType(SMILTimeContainer::PAUSE_BEGIN)) {
        continue;
      }
      SMILMilestone thisMilestone;
      bool didGetMilestone =
          container->GetNextMilestoneInParentTime(thisMilestone);
      if (didGetMilestone && thisMilestone < nextMilestone) {
        nextMilestone = thisMilestone;
      }
    }

    if (nextMilestone.mTime > GetCurrentTimeAsSMILTime()) {
      break;
    }

    nsTArray<RefPtr<dom::SVGAnimationElement>> elements;
    for (SMILTimeContainer* container : mChildContainerTable.Keys()) {
      if (container->IsPausedByType(SMILTimeContainer::PAUSE_BEGIN)) {
        continue;
      }
      container->PopMilestoneElementsAtMilestone(nextMilestone, elements);
    }

    // During the course of a sampling we don't want to actually go backwards.
    // Due to negative offsets, early ends and the like, a timed element might
    // register a milestone that is actually in the past. That's fine, but it's
    // still only going to get *sampled* with whatever time we're up to and no
    // earlier.
    //
    // Because we're only performing this clamping at the last moment, the
    // animations will still all get sampled in the correct order and
    // dependencies will be appropriately resolved.
    sampleTime = std::max(nextMilestone.mTime, sampleTime);

    SMILAnimationController::DiscardArray discards;

    for (RefPtr<dom::SVGAnimationElement>& elem : elements) {
      MOZ_ASSERT(elem, "nullptr animation element in list");
      SMILTimeContainer* container = elem->GetTimeContainer();
      if (!container)
        // The container may be nullptr if the element has been detached from
        // its parent since registering a milestone.
        continue;

      SMILTimeValue containerTimeValue =
          container->ParentToContainerTime(sampleTime);
      if (!containerTimeValue.IsDefinite()) continue;

      // Clamp the converted container time to non-negative values.
      SMILTime containerTime =
          std::max<SMILTime>(0, containerTimeValue.GetMillis());

      if (nextMilestone.mIsEnd) {
        elem->TimedElement().SampleEndAt(containerTime, discards);
      } else {
        elem->TimedElement().SampleAt(containerTime, discards);
      }
    }

    elements.Clear();
    ProcessDiscards(discards);
  }
}

/*static*/
void SMILAnimationController::SampleTimedElement(
    SVGAnimationElement* aElement, DiscardArray& aDiscards,
    TimeContainerHashtable* aActiveContainers) {
  SMILTimeContainer* timeContainer = aElement->GetTimeContainer();
  if (!timeContainer) return;

  // We'd like to call timeContainer->NeedsSample() here and skip all timed
  // elements that belong to paused time containers that don't need a sample,
  // but that doesn't work because we've already called Sample() on all the time
  // containers so the paused ones don't need a sample any more and they'll
  // return false.
  //
  // Instead we build up a hashmap of active time containers during the previous
  // step (SampleTimeContainer) and then test here if the container for this
  // timed element is in the list.
  if (!aActiveContainers->GetEntry(timeContainer)) return;

  SMILTime containerTime = timeContainer->GetCurrentTimeAsSMILTime();

  MOZ_ASSERT(!timeContainer->IsSeeking(),
             "Doing a regular sample but the time container is still seeking");
  aElement->TimedElement().SampleAt(containerTime, aDiscards);
}

/*static*/
void SMILAnimationController::AddAnimationToCompositorTable(
    SVGAnimationElement* aElement, SMILCompositorTable* aCompositorTable) {
  // Add a compositor to the hash table if there's not already one there
  SMILTargetIdentifier key;
  if (!GetTargetIdentifierForAnimation(aElement, key))
    // Something's wrong/missing about animation's target; skip this animation
    return;

  SMILAnimationFunction& func = aElement->AnimationFunction();

  // Only add active animation functions. If there are no active animations
  // targeting an attribute, no compositor will be created and any previously
  // applied animations will be cleared.
  if (func.IsActiveOrFrozen()) {
    // Look up the compositor for our target, & add our animation function
    // to its list of animation functions.
    SMILCompositor* result = aCompositorTable->PutEntry(key);
    result->AddAnimationFunction(&func);

  } else if (func.HasChanged()) {
    // Look up the compositor for our target, and force it to skip the
    // "nothing's changed so don't bother compositing" optimization for this
    // sample. |func| is inactive, but it's probably *newly* inactive (since
    // it's got HasChanged() == true), so we need to make sure to recompose
    // its target.
    SMILCompositor* result = aCompositorTable->PutEntry(key);
    result->ToggleForceCompositing();

    // We've now made sure that |func|'s inactivity will be reflected as of
    // this sample. We need to clear its HasChanged() flag so that it won't
    // trigger this same clause in future samples (until it changes again).
    func.ClearHasChanged();
  }
}

static inline bool IsTransformAttribute(int32_t aNamespaceID,
                                        nsAtom* aAttributeName) {
  return aNamespaceID == kNameSpaceID_None &&
         (aAttributeName == nsGkAtoms::transform ||
          aAttributeName == nsGkAtoms::patternTransform ||
          aAttributeName == nsGkAtoms::gradientTransform);
}

// Helper function that, given a SVGAnimationElement, looks up its target
// element & target attribute and populates a SMILTargetIdentifier
// for this target.
/*static*/
bool SMILAnimationController::GetTargetIdentifierForAnimation(
    SVGAnimationElement* aAnimElem, SMILTargetIdentifier& aResult) {
  // Look up target (animated) element
  Element* targetElem = aAnimElem->GetTargetElementContent();
  if (!targetElem)
    // Animation has no target elem -- skip it.
    return false;

  // Look up target (animated) attribute
  // SMILANIM section 3.1, attributeName may
  // have an XMLNS prefix to indicate the XML namespace.
  RefPtr<nsAtom> attributeName;
  int32_t attributeNamespaceID;
  if (!aAnimElem->GetTargetAttributeName(&attributeNamespaceID,
                                         getter_AddRefs(attributeName)))
    // Animation has no target attr -- skip it.
    return false;

  // animateTransform can only animate transforms, conversely transforms
  // can only be animated by animateTransform
  if (IsTransformAttribute(attributeNamespaceID, attributeName) !=
      (aAnimElem->IsSVGElement(nsGkAtoms::animateTransform)))
    return false;

  // Construct the key
  aResult.mElement = targetElem;
  aResult.mAttributeName = attributeName;
  aResult.mAttributeNamespaceID = attributeNamespaceID;

  return true;
}

void SMILAnimationController::PreTraverse() { PreTraverseInSubtree(nullptr); }

void SMILAnimationController::PreTraverseInSubtree(Element* aRoot) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mMightHavePendingStyleUpdates) {
    return;
  }

  nsPresContext* context = mDocument->GetPresContext();
  if (!context) {
    return;
  }

  for (SVGAnimationElement* animElement : mAnimationElementTable.Keys()) {
    SMILTargetIdentifier key;
    if (!GetTargetIdentifierForAnimation(animElement, key)) {
      // Something's wrong/missing about animation's target; skip this animation
      continue;
    }

    // Ignore restyles that aren't in the flattened tree subtree rooted at
    // aRoot.
    if (aRoot && !nsContentUtils::ContentIsFlattenedTreeDescendantOf(
                     key.mElement, aRoot)) {
      continue;
    }

    context->RestyleManager()->PostRestyleEventForAnimations(
        key.mElement, PseudoStyleRequest::NotPseudo(),
        RestyleHint::RESTYLE_SMIL);
  }

  // Only clear the mMightHavePendingStyleUpdates flag if we definitely posted
  // all restyles.
  if (!aRoot) {
    mMightHavePendingStyleUpdates = false;
  }
}

//----------------------------------------------------------------------
// Add/remove child time containers

nsresult SMILAnimationController::AddChild(SMILTimeContainer& aChild) {
  const bool wasEmpty = mChildContainerTable.IsEmpty();
  TimeContainerPtrKey* key = mChildContainerTable.PutEntry(&aChild);
  NS_ENSURE_TRUE(key, NS_ERROR_OUT_OF_MEMORY);
  if (wasEmpty) {
    UpdateSampling();
  }
  return NS_OK;
}

void SMILAnimationController::RemoveChild(SMILTimeContainer& aChild) {
  mChildContainerTable.RemoveEntry(&aChild);
  if (mChildContainerTable.IsEmpty()) {
    UpdateSampling();
  }
}

// Helper method
nsRefreshDriver* SMILAnimationController::GetRefreshDriver() {
  if (!mDocument) {
    NS_ERROR("Requesting refresh driver after document has disconnected!");
    return nullptr;
  }

  nsPresContext* context = mDocument->GetPresContext();
  return context ? context->RefreshDriver() : nullptr;
}

void SMILAnimationController::FlagDocumentNeedsFlush() {
  if (PresShell* presShell = mDocument->GetPresShell()) {
    presShell->SetNeedStyleFlush();
  }
}

}  // namespace mozilla
