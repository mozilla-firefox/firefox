/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SMIL_SMILANIMATIONCONTROLLER_H_
#define DOM_SMIL_SMILANIMATIONCONTROLLER_H_

#include "mozilla/Attributes.h"
#include "mozilla/SMILCompositorTable.h"
#include "mozilla/SMILMilestone.h"
#include "mozilla/SMILTimeContainer.h"
#include "mozilla/UniquePtr.h"
#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsTHashtable.h"
#include "nsHashKeys.h"

class nsRefreshDriver;

namespace mozilla {
struct SMILTargetIdentifier;
namespace dom {
class Element;
class SVGAnimationElement;
}  // namespace dom

//----------------------------------------------------------------------
// SMILAnimationController
//
// The animation controller maintains the animation timer and determines the
// sample times and sample rate for all SMIL animations in a document. There is
// at most one animation controller per document so that frame-rate tuning can
// be performed at a document-level.
//
// The animation controller can contain many child time containers (timed
// document root objects) which may correspond to SVG document fragments within
// a compound document. These time containers can be paused individually or
// here, at the document level.
//
class SMILAnimationController final : public SMILTimeContainer {
 public:
  explicit SMILAnimationController(mozilla::dom::Document* aDoc);

  NS_INLINE_DECL_REFCOUNTING(SMILAnimationController)

  using DiscardArray = nsTObserverArray<RefPtr<dom::Element>>;

  // Clears mDocument pointer. (Called by our mozilla::dom::Document when it's
  // going away)
  void Disconnect();

  // SMILContainer
  void Pause(uint32_t aType) override;
  void Resume(uint32_t aType) override;
  SMILTime GetParentTime() const override;

  // Returns mDocument's refresh driver, if it's got one.
  nsRefreshDriver* GetRefreshDriver();
  void WillRefresh(mozilla::TimeStamp aTime);

  // Methods for registering and enumerating animation elements
  void RegisterAnimationElement(
      mozilla::dom::SVGAnimationElement* aAnimationElement);
  void UnregisterAnimationElement(
      mozilla::dom::SVGAnimationElement* aAnimationElement);

  // Methods for resampling all animations
  // (A resample performs the same operations as a sample but doesn't advance
  // the current time and doesn't check if the container is paused)
  // This will flush pending style changes for the document.
  void Resample() { DoSample(false); }

  void SetResampleNeeded() {
    if (!mRunningSample && !mResampleNeeded) {
      FlagDocumentNeedsFlush();
      mResampleNeeded = true;
    }
  }

  // This will flush pending style changes for the document.
  void FlushResampleRequests() {
    if (!mResampleNeeded) return;

    Resample();
  }

  // Methods for handling page transitions
  void OnPageShow();
  void OnPageHide();

  // Methods for supporting cycle-collection
  void Traverse(nsCycleCollectionTraversalCallback* aCallback);
  void Unlink();

  // Helper to check if we have any animation elements at all
  bool HasRegisteredAnimations() const {
    return mAnimationElementTable.Count() != 0;
  }

  bool MightHavePendingStyleUpdates() const {
    return mMightHavePendingStyleUpdates;
  }

  void PreTraverse();
  void PreTraverseInSubtree(mozilla::dom::Element* aRoot);

 protected:
  ~SMILAnimationController();

  // alias declarations
  using TimeContainerPtrKey = nsPtrHashKey<SMILTimeContainer>;
  using TimeContainerHashtable = nsTHashtable<TimeContainerPtrKey>;
  using AnimationElementPtrKey = nsPtrHashKey<dom::SVGAnimationElement>;
  using AnimationElementHashtable = nsTHashtable<AnimationElementPtrKey>;

  // Methods for controlling whether we're sampling
  void UpdateSampling();
  bool ShouldSample() const;

  // Sample-related callbacks and implementation helpers
  void DoSample() override;
  void DoSample(bool aSkipUnchangedContainers);

  void RewindElements();

  void DoMilestoneSamples();

  static void SampleTimedElement(dom::SVGAnimationElement* aElement,
                                 DiscardArray& aDiscards,
                                 TimeContainerHashtable* aActiveContainers);

  static void AddAnimationToCompositorTable(
      mozilla::dom::SVGAnimationElement* aElement,
      SMILCompositorTable* aCompositorTable);

  static bool GetTargetIdentifierForAnimation(
      mozilla::dom::SVGAnimationElement* aAnimElem,
      SMILTargetIdentifier& aResult);

  // Methods for adding/removing time containers
  nsresult AddChild(SMILTimeContainer& aChild) override;
  void RemoveChild(SMILTimeContainer& aChild) override;

  void FlagDocumentNeedsFlush();

  // Members

  AnimationElementHashtable mAnimationElementTable;
  TimeContainerHashtable mChildContainerTable;
  mozilla::TimeStamp mCurrentSampleTime;
  mozilla::TimeStamp mStartTime;

  // Average time between samples from the refresh driver. This is used to
  // detect large unexpected gaps between samples such as can occur when the
  // computer sleeps. The nature of the SMIL model means that catching up these
  // large gaps can be expensive as, for example, many events may need to be
  // dispatched for the intervening time when no samples were received.
  //
  // In such cases, we ignore the intervening gap and continue sampling from
  // when we were expecting the next sample to arrive.
  //
  // Note that we only do this for SMIL and not CSS transitions (which doesn't
  // have so much work to do to catch up) nor scripted animations (which expect
  // animation time to follow real time).
  //
  // This behaviour does not affect pausing (since we're not *expecting* any
  // samples then) nor seeking (where the SMIL model behaves somewhat
  // differently such as not dispatching events).
  SMILTime mAvgTimeBetweenSamples = 0;

  bool mResampleNeeded = false;
  bool mRunningSample = false;

  // Have we updated animated values without adding them to the restyle tracker?
  bool mMightHavePendingStyleUpdates = false;

  // Whether we've started sampling. This is only needed because the first
  // sample is supposed to run sync.
  bool mIsSampling = false;

  // Store raw ptr to mDocument.  It owns the controller, so controller
  // shouldn't outlive it
  mozilla::dom::Document* mDocument;

  // Contains compositors used in our last sample.  We keep this around
  // so we can detect when an element/attribute used to be animated,
  // but isn't anymore for some reason. (e.g. if its <animate> element is
  // removed or retargeted)
  UniquePtr<SMILCompositorTable> mLastCompositorTable;
};

}  // namespace mozilla

#endif  // DOM_SMIL_SMILANIMATIONCONTROLLER_H_
