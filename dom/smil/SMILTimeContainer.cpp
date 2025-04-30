/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SMILTimeContainer.h"

#include "mozilla/AutoRestore.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/SMILTimedElement.h"
#include "mozilla/SMILTimeValue.h"
#include <algorithm>

namespace mozilla {

SMILTimeContainer::SMILTimeContainer()
    : mParent(nullptr),
      mCurrentTime(0L),
      mParentOffset(0L),
      mPauseStart(0L),
      mNeedsPauseSample(false),
      mNeedsRewind(false),
      mIsSeeking(false),
#ifdef DEBUG
      mHoldingEntries(false),
#endif
      mPauseState(PAUSE_BEGIN) {
}

SMILTimeContainer::~SMILTimeContainer() {
  if (mParent) {
    mParent->RemoveChild(*this);
  }
}

SMILTimeValue SMILTimeContainer::ContainerToParentTime(
    SMILTime aContainerTime) const {
  // If we're paused, then future times are indefinite
  if (IsPaused() && aContainerTime > mCurrentTime)
    return SMILTimeValue::Indefinite();

  return SMILTimeValue(aContainerTime + mParentOffset);
}

SMILTimeValue SMILTimeContainer::ParentToContainerTime(
    SMILTime aParentTime) const {
  // If we're paused, then any time after when we paused is indefinite
  if (IsPaused() && aParentTime > mPauseStart)
    return SMILTimeValue::Indefinite();

  return SMILTimeValue(aParentTime - mParentOffset);
}

void SMILTimeContainer::Begin() {
  Resume(PAUSE_BEGIN);
  if (mPauseState) {
    mNeedsPauseSample = true;
  }

  // This is a little bit complicated here. Ideally we'd just like to call
  // Sample() and force an initial sample but this turns out to be a bad idea
  // because this may mean that NeedsSample() no longer reports true and so when
  // we come to the first real sample our parent will skip us over altogether.
  // So we force the time to be updated and adopt the policy to never call
  // Sample() ourselves but to always leave that to our parent or client.

  UpdateCurrentTime();
}

void SMILTimeContainer::Pause(uint32_t aType) {
  bool didStartPause = false;

  if (!mPauseState && aType) {
    mPauseStart = GetParentTime();
    mNeedsPauseSample = true;
    didStartPause = true;
  }

  mPauseState |= aType;

  if (didStartPause) {
    NotifyTimeChange();
  }
}

void SMILTimeContainer::Resume(uint32_t aType) {
  if (!mPauseState) return;

  mPauseState &= ~aType;

  if (!mPauseState) {
    SMILTime extraOffset = GetParentTime() - mPauseStart;
    mParentOffset += extraOffset;
    NotifyTimeChange();
  }
}

SMILTime SMILTimeContainer::GetCurrentTimeAsSMILTime() const {
  // The following behaviour is consistent with:
  // http://www.w3.org/2003/01/REC-SVG11-20030114-errata
  //  #getCurrentTime_setCurrentTime_undefined_before_document_timeline_begin
  // which says that if GetCurrentTime is called before the document timeline
  // has begun we should just return 0.
  if (IsPausedByType(PAUSE_BEGIN)) return 0L;

  return mCurrentTime;
}

void SMILTimeContainer::SetCurrentTime(SMILTime aSeekTo) {
  // SVG 1.1 doesn't specify what to do for negative times so we adopt SVGT1.2's
  // behaviour of clamping negative times to 0.
  aSeekTo = std::max<SMILTime>(0, aSeekTo);

  // The following behaviour is consistent with:
  // http://www.w3.org/2003/01/REC-SVG11-20030114-errata
  //  #getCurrentTime_setCurrentTime_undefined_before_document_timeline_begin
  // which says that if SetCurrentTime is called before the document timeline
  // has begun we should still adjust the offset.
  SMILTime parentTime = GetParentTime();
  mParentOffset = parentTime - aSeekTo;
  mIsSeeking = true;

  if (IsPaused()) {
    mNeedsPauseSample = true;
    mPauseStart = parentTime;
  }

  if (aSeekTo < mCurrentTime) {
    // Backwards seek
    mNeedsRewind = true;
    ClearMilestones();
  }

  // Force an update to the current time in case we get a call to GetCurrentTime
  // before another call to Sample().
  UpdateCurrentTime();

  NotifyTimeChange();
}

SMILTime SMILTimeContainer::GetParentTime() const {
  if (mParent) return mParent->GetCurrentTimeAsSMILTime();

  return 0L;
}

void SMILTimeContainer::SyncPauseTime() {
  if (IsPaused()) {
    SMILTime parentTime = GetParentTime();
    SMILTime extraOffset = parentTime - mPauseStart;
    mParentOffset += extraOffset;
    mPauseStart = parentTime;
  }
}

void SMILTimeContainer::Sample() {
  if (!NeedsSample()) {
    return;
  }

  UpdateCurrentTime();
  DoSample();

  mNeedsPauseSample = false;
}

nsresult SMILTimeContainer::SetParent(SMILTimeContainer* aParent) {
  if (mParent) {
    mParent->RemoveChild(*this);
    // When we're not attached to a parent time container, GetParentTime() will
    // return 0. We need to adjust our pause state information to be relative to
    // this new time base.
    // Note that since "current time = parent time - parent offset" setting the
    // parent offset and pause start as follows preserves our current time even
    // while parent time = 0.
    mParentOffset = -mCurrentTime;
    mPauseStart = 0L;
  }

  mParent = aParent;

  nsresult rv = NS_OK;
  if (mParent) {
    rv = mParent->AddChild(*this);
  }

  return rv;
}

void SMILTimeContainer::AddMilestone(
    const SMILMilestone& aMilestone,
    mozilla::dom::SVGAnimationElement& aElement) {
  // We record the milestone time and store it along with the element but this
  // time may change (e.g. if attributes are changed on the timed element in
  // between samples). If this happens, then we may do an unecessary sample
  // but that's pretty cheap.
  MOZ_ASSERT(!mHoldingEntries);
  mMilestoneEntries.Push(MilestoneEntry(aMilestone, aElement));
}

void SMILTimeContainer::ClearMilestones() {
  MOZ_ASSERT(!mHoldingEntries);
  mMilestoneEntries.Clear();
}

bool SMILTimeContainer::GetNextMilestoneInParentTime(
    SMILMilestone& aNextMilestone) const {
  if (mMilestoneEntries.IsEmpty()) return false;

  SMILTimeValue parentTime =
      ContainerToParentTime(mMilestoneEntries.Top().mMilestone.mTime);
  if (!parentTime.IsDefinite()) return false;

  aNextMilestone = SMILMilestone(parentTime.GetMillis(),
                                 mMilestoneEntries.Top().mMilestone.mIsEnd);

  return true;
}

bool SMILTimeContainer::PopMilestoneElementsAtMilestone(
    const SMILMilestone& aMilestone, AnimElemArray& aMatchedElements) {
  if (mMilestoneEntries.IsEmpty()) return false;

  SMILTimeValue containerTime = ParentToContainerTime(aMilestone.mTime);
  if (!containerTime.IsDefinite()) return false;

  SMILMilestone containerMilestone(containerTime.GetMillis(),
                                   aMilestone.mIsEnd);

  MOZ_ASSERT(mMilestoneEntries.Top().mMilestone >= containerMilestone,
             "Trying to pop off earliest times but we have earlier ones that "
             "were overlooked");

  MOZ_ASSERT(!mHoldingEntries);

  bool gotOne = false;
  while (!mMilestoneEntries.IsEmpty() &&
         mMilestoneEntries.Top().mMilestone == containerMilestone) {
    aMatchedElements.AppendElement(mMilestoneEntries.Pop().mTimebase);
    gotOne = true;
  }

  return gotOne;
}

void SMILTimeContainer::Traverse(
    nsCycleCollectionTraversalCallback* aCallback) {
#ifdef DEBUG
  AutoRestore<bool> saveHolding(mHoldingEntries);
  mHoldingEntries = true;
#endif
  const MilestoneEntry* p = mMilestoneEntries.Elements();
  while (p < mMilestoneEntries.Elements() + mMilestoneEntries.Length()) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback, "mTimebase");
    aCallback->NoteXPCOMChild(static_cast<nsIContent*>(p->mTimebase.get()));
    ++p;
  }
}

void SMILTimeContainer::Unlink() {
  MOZ_ASSERT(!mHoldingEntries);
  mMilestoneEntries.Clear();
}

void SMILTimeContainer::UpdateCurrentTime() {
  SMILTime now = IsPaused() ? mPauseStart : GetParentTime();
  MOZ_ASSERT(now >= mParentOffset,
             "Container has negative time with respect to parent");
  const auto updatedCurrentTime = CheckedInt<SMILTime>(now) - mParentOffset;
  mCurrentTime = updatedCurrentTime.isValid()
                     ? updatedCurrentTime.value()
                     : std::numeric_limits<SMILTime>::max();
}

void SMILTimeContainer::NotifyTimeChange() {
  // Called when the container time is changed with respect to the document
  // time. When this happens time dependencies in other time containers need to
  // re-resolve their times because begin and end times are stored in container
  // time.
  //
  // To get the list of timed elements with dependencies we simply re-use the
  // milestone elements. This is because any timed element with dependents and
  // with significant transitions yet to fire should have their next milestone
  // registered. Other timed elements don't matter.

  // Copy the timed elements to a separate array before calling
  // HandleContainerTimeChange on each of them in case doing so mutates
  // mMilestoneEntries.
  nsTArray<RefPtr<mozilla::dom::SVGAnimationElement>> elems;

  {
#ifdef DEBUG
    AutoRestore<bool> saveHolding(mHoldingEntries);
    mHoldingEntries = true;
#endif
    for (const MilestoneEntry* p = mMilestoneEntries.Elements();
         p < mMilestoneEntries.Elements() + mMilestoneEntries.Length(); ++p) {
      elems.AppendElement(p->mTimebase.get());
    }
  }

  for (auto& elem : elems) {
    elem->TimedElement().HandleContainerTimeChange();
  }
}

}  // namespace mozilla
