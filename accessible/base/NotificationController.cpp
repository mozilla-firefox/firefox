/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NotificationController.h"

#include "DocAccessible-inl.h"
#include "DocAccessibleChild.h"
#include "HTMLListAccessible.h"
#include "LocalAccessible-inl.h"
#include "TextLeafAccessible.h"
#include "TextUpdater.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/PerfStats.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/glean/AccessibleMetrics.h"
#include "nsAccessibilityService.h"
#include "nsCoreUtils.h"
#include "nsEventShell.h"
#include "nsIContentInlines.h"

using namespace mozilla;
using namespace mozilla::a11y;
using namespace mozilla::dom;

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector
////////////////////////////////////////////////////////////////////////////////

NotificationController::NotificationController(DocAccessible* aDocument,
                                               PresShell* aPresShell)
    : EventQueue(aDocument),
      mObservingState(eNotObservingRefresh),
      mPresShell(aPresShell),
      mEventGeneration(0) {
  // Schedule initial accessible tree construction.
  ScheduleProcessing();
}

NotificationController::~NotificationController() {
  NS_ASSERTION(!mDocument, "Controller wasn't shutdown properly!");
  if (mDocument) {
    Shutdown();
  }
  MOZ_RELEASE_ASSERT(mObservingState == eNotObservingRefresh,
                     "Must unregister before being destroyed");
}

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector: AddRef/Release and cycle collection

NS_IMPL_CYCLE_COLLECTING_NATIVE_ADDREF(NotificationController)
NS_IMPL_CYCLE_COLLECTING_NATIVE_RELEASE(NotificationController)

NS_IMPL_CYCLE_COLLECTION_CLASS(NotificationController)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(NotificationController)
  if (tmp->mDocument) {
    tmp->Shutdown();
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(NotificationController)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mHangingChildDocuments)
  for (const auto& entry : tmp->mContentInsertions) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mContentInsertions key");
    cb.NoteXPCOMChild(entry.GetKey());
    nsTArray<nsCOMPtr<nsIContent>>* list = entry.GetData().get();
    for (uint32_t i = 0; i < list->Length(); i++) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mContentInsertions value item");
      cb.NoteXPCOMChild(list->ElementAt(i));
    }
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFocusEvent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEvents)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRelocations)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector: public

void NotificationController::Shutdown() {
  if (mObservingState != eNotObservingRefresh &&
      mPresShell->RemoveRefreshObserver(this, FlushType::Display)) {
    // Note, this was our last chance to unregister, since we're about to
    // clear mPresShell further down in this function.
    mObservingState = eNotObservingRefresh;
  }
  MOZ_RELEASE_ASSERT(mObservingState == eNotObservingRefresh,
                     "Must unregister before being destroyed (and we just "
                     "passed our last change to unregister)");
  // Immediately null out mPresShell, to prevent us from being registered as a
  // refresh observer again.
  mPresShell = nullptr;

  // Shutdown handling child documents.
  int32_t childDocCount = mHangingChildDocuments.Length();
  for (int32_t idx = childDocCount - 1; idx >= 0; idx--) {
    if (!mHangingChildDocuments[idx]->IsDefunct()) {
      mHangingChildDocuments[idx]->Shutdown();
    }
  }

  mHangingChildDocuments.Clear();

  mDocument = nullptr;

  mTextArray.Clear();
  mContentInsertions.Clear();
  mNotifications.Clear();
  mFocusEvent = nullptr;
  mEvents.Clear();
  mRelocations.Clear();
}

void NotificationController::CoalesceHideEvent(AccHideEvent* aHideEvent) {
  LocalAccessible* parent = aHideEvent->LocalParent();
  while (parent) {
    if (parent->IsDoc()) {
      break;
    }

    if (parent->HideEventTarget()) {
      DropMutationEvent(aHideEvent);
      break;
    }

    if (parent->ShowEventTarget()) {
      AccShowEvent* showEvent =
          downcast_accEvent(mMutationMap.GetEvent(parent, EventMap::ShowEvent));
      if (showEvent->EventGeneration() < aHideEvent->EventGeneration()) {
        DropMutationEvent(aHideEvent);
        break;
      }
    }

    parent = parent->LocalParent();
  }
}

bool NotificationController::QueueMutationEvent(AccTreeMutationEvent* aEvent) {
  if (aEvent->GetEventType() == nsIAccessibleEvent::EVENT_HIDE) {
    // We have to allow there to be a hide and then a show event for a target
    // because of targets getting moved.  However we need to coalesce a show and
    // then a hide for a target which means we need to check for that here.
    if (aEvent->GetAccessible()->ShowEventTarget()) {
      AccTreeMutationEvent* showEvent =
          mMutationMap.GetEvent(aEvent->GetAccessible(), EventMap::ShowEvent);
      DropMutationEvent(showEvent);
      return false;
    }

    // Don't queue a hide event on an accessible that's already being moved. It
    // or an ancestor should already have a hide event queued.
    if (mDocument &&
        mDocument->IsAccessibleBeingMoved(aEvent->GetAccessible())) {
      return false;
    }

    // If this is an additional hide event, the accessible may be hidden, or
    // moved again after a move. Preserve the original hide event since
    // its properties are consistent with the tree that existed before
    // the next batch of mutation events is processed.
    if (aEvent->GetAccessible()->HideEventTarget()) {
      return false;
    }
  }

  AccMutationEvent* mutEvent = downcast_accEvent(aEvent);
  mEventGeneration++;
  mutEvent->SetEventGeneration(mEventGeneration);

  if (!mFirstMutationEvent) {
    mFirstMutationEvent = aEvent;
    ScheduleProcessing();
  }

  if (mLastMutationEvent) {
    NS_ASSERTION(!mLastMutationEvent->NextEvent(),
                 "why isn't the last event the end?");
    mLastMutationEvent->SetNextEvent(aEvent);
  }

  aEvent->SetPrevEvent(mLastMutationEvent);
  mLastMutationEvent = aEvent;
  mMutationMap.PutEvent(aEvent);

  // Because we could be hiding the target of a show event we need to get rid
  // of any such events.
  if (aEvent->GetEventType() == nsIAccessibleEvent::EVENT_HIDE) {
    CoalesceHideEvent(downcast_accEvent(aEvent));

    // mLastMutationEvent will point to something other than aEvent if and only
    // if aEvent was just coalesced away.  In that case a parent accessible
    // must already have the required reorder and text change events so we are
    // done here.
    if (mLastMutationEvent != aEvent) {
      return false;
    }
  }

  if (aEvent->GetEventType() == nsIAccessibleEvent::EVENT_HIDE ||
      aEvent->GetEventType() == nsIAccessibleEvent::EVENT_SHOW) {
    LocalAccessible* target = aEvent->GetAccessible();
    // We need to do this here while the relation is still intact. During the
    // tick, where we we call PushNameOrDescriptionChange, it will be too late
    // since we will already have unparented the label and severed the relation.
    if (PushNameOrDescriptionChangeToRelations(target,
                                               RelationType::LABEL_FOR) ||
        PushNameOrDescriptionChangeToRelations(target,
                                               RelationType::DESCRIPTION_FOR)) {
      ScheduleProcessing();
    }
  }

  // We need to fire a reorder event after all of the events targeted at shown
  // or hidden children of a container.  So either queue a new one, or move an
  // existing one to the end of the queue if the container already has a
  // reorder event.
  LocalAccessible* container = aEvent->GetAccessible()->LocalParent();
  RefPtr<AccReorderEvent> reorder;
  if (!container->ReorderEventTarget()) {
    reorder = new AccReorderEvent(container);
    container->SetReorderEventTarget(true);
    mMutationMap.PutEvent(reorder);
  } else {
    AccReorderEvent* event = downcast_accEvent(
        mMutationMap.GetEvent(container, EventMap::ReorderEvent));
    reorder = event;
    if (mFirstMutationEvent == event) {
      mFirstMutationEvent = event->NextEvent();
    } else {
      event->PrevEvent()->SetNextEvent(event->NextEvent());
    }

    event->NextEvent()->SetPrevEvent(event->PrevEvent());
    event->SetNextEvent(nullptr);
  }

  reorder->SetEventGeneration(mEventGeneration);
  reorder->SetPrevEvent(mLastMutationEvent);
  mLastMutationEvent->SetNextEvent(reorder);
  mLastMutationEvent = reorder;

  // It is not possible to have a text change event for something other than a
  // hyper text accessible.
  if (!container->IsHyperText()) {
    return true;
  }

  MOZ_ASSERT(mutEvent);

  nsString text;
  aEvent->GetAccessible()->AppendTextTo(text);
  if (text.IsEmpty()) {
    return true;
  }

  LocalAccessible* target = aEvent->GetAccessible();
  int32_t offset = container->AsHyperText()->GetChildOffset(target);
  AccTreeMutationEvent* prevEvent = aEvent->PrevEvent();
  while (prevEvent &&
         prevEvent->GetEventType() == nsIAccessibleEvent::EVENT_REORDER) {
    prevEvent = prevEvent->PrevEvent();
  }

  if (prevEvent &&
      prevEvent->GetEventType() == nsIAccessibleEvent::EVENT_HIDE &&
      mutEvent->IsHide()) {
    AccHideEvent* prevHide = downcast_accEvent(prevEvent);
    AccTextChangeEvent* prevTextChange = prevHide->mTextChangeEvent;
    if (prevTextChange && prevHide->LocalParent() == mutEvent->LocalParent()) {
      if (prevHide->mNextSibling == target) {
        target->AppendTextTo(prevTextChange->mModifiedText);
        prevHide->mTextChangeEvent.swap(mutEvent->mTextChangeEvent);
      } else if (prevHide->mPrevSibling == target) {
        nsString temp;
        target->AppendTextTo(temp);

        uint32_t extraLen = temp.Length();
        temp += prevTextChange->mModifiedText;
        ;
        prevTextChange->mModifiedText = temp;
        prevTextChange->mStart -= extraLen;
        prevHide->mTextChangeEvent.swap(mutEvent->mTextChangeEvent);
      }
    }
  } else if (prevEvent && mutEvent->IsShow() &&
             prevEvent->GetEventType() == nsIAccessibleEvent::EVENT_SHOW) {
    AccShowEvent* prevShow = downcast_accEvent(prevEvent);
    AccTextChangeEvent* prevTextChange = prevShow->mTextChangeEvent;
    if (prevTextChange && prevShow->LocalParent() == target->LocalParent()) {
      int32_t index = target->IndexInParent();
      int32_t prevIndex = prevShow->GetAccessible()->IndexInParent();
      if (prevIndex + 1 == index) {
        target->AppendTextTo(prevTextChange->mModifiedText);
        prevShow->mTextChangeEvent.swap(mutEvent->mTextChangeEvent);
      } else if (index + 1 == prevIndex) {
        nsString temp;
        target->AppendTextTo(temp);
        prevTextChange->mStart -= temp.Length();
        temp += prevTextChange->mModifiedText;
        prevTextChange->mModifiedText = temp;
        prevShow->mTextChangeEvent.swap(mutEvent->mTextChangeEvent);
      }
    }
  }

  if (!mutEvent->mTextChangeEvent) {
    mutEvent->mTextChangeEvent = new AccTextChangeEvent(
        container, offset, text, mutEvent->IsShow(),
        aEvent->mIsFromUserInput ? eFromUserInput : eNoUserInput);
  }

  return true;
}

void NotificationController::DropMutationEvent(AccTreeMutationEvent* aEvent) {
  const uint32_t eventType = aEvent->GetEventType();
  MOZ_ASSERT(eventType != nsIAccessibleEvent::EVENT_INNER_REORDER,
             "Inner reorder has already been dropped, cannot drop again");
  if (eventType == nsIAccessibleEvent::EVENT_REORDER) {
    // We don't fully drop reorder events, we just change them to inner reorder
    // events.
    AccReorderEvent* reorderEvent = downcast_accEvent(aEvent);

    MOZ_ASSERT(reorderEvent);
    reorderEvent->SetInner();
    return;
  }
  if (eventType == nsIAccessibleEvent::EVENT_SHOW) {
    // unset the event bits since the event isn't being fired any more.
    aEvent->GetAccessible()->SetShowEventTarget(false);
  } else if (eventType == nsIAccessibleEvent::EVENT_HIDE) {
    // unset the event bits since the event isn't being fired any more.
    aEvent->GetAccessible()->SetHideEventTarget(false);

    AccHideEvent* hideEvent = downcast_accEvent(aEvent);
    MOZ_ASSERT(hideEvent);

    if (hideEvent->NeedsShutdown()) {
      mDocument->UncacheChildrenInSubtree(aEvent->GetAccessible());
      mDocument->ShutdownChildrenInSubtree(aEvent->GetAccessible());
    }
  } else {
    MOZ_ASSERT_UNREACHABLE("Mutation event has non-mutation event type");
  }

  // Do the work to splice the event out of the list.
  if (mFirstMutationEvent == aEvent) {
    mFirstMutationEvent = aEvent->NextEvent();
  } else {
    aEvent->PrevEvent()->SetNextEvent(aEvent->NextEvent());
  }

  if (mLastMutationEvent == aEvent) {
    mLastMutationEvent = aEvent->PrevEvent();
  } else {
    aEvent->NextEvent()->SetPrevEvent(aEvent->PrevEvent());
  }

  aEvent->SetPrevEvent(nullptr);
  aEvent->SetNextEvent(nullptr);
  mMutationMap.RemoveEvent(aEvent);
}

void NotificationController::CoalesceMutationEvents() {
  AUTO_PROFILER_MARKER_TEXT("NotificationController::CoalesceMutationEvents",
                            A11Y, {}, ""_ns);
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_CoalesceMutationEvents>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  AccTreeMutationEvent* event = mFirstMutationEvent;
  while (event) {
    AccTreeMutationEvent* nextEvent = event->NextEvent();
    uint32_t eventType = event->GetEventType();
    if (event->GetEventType() == nsIAccessibleEvent::EVENT_REORDER) {
      LocalAccessible* acc = event->GetAccessible();
      while (acc) {
        if (acc->IsDoc()) {
          break;
        }

        // if a parent of the reorder event's target is being hidden that
        // hide event's target must have a parent that is also a reorder event
        // target.  That means we don't need this reorder event.
        if (acc->HideEventTarget()) {
          DropMutationEvent(event);
          break;
        }

        LocalAccessible* parent = acc->LocalParent();
        if (parent && parent->ReorderEventTarget()) {
          AccReorderEvent* reorder = downcast_accEvent(
              mMutationMap.GetEvent(parent, EventMap::ReorderEvent));

          // We want to make sure that a reorder event comes after any show or
          // hide events targeted at the children of its target.  We keep the
          // invariant that event generation goes up as you are farther in the
          // queue, so we want to use the spot of the event with the higher
          // generation number, and keep that generation number.
          if (reorder &&
              reorder->EventGeneration() < event->EventGeneration()) {
            reorder->SetEventGeneration(event->EventGeneration());

            // It may be true that reorder was before event, and we coalesced
            // away all the show / hide events between them.  In that case
            // event is already immediately after reorder in the queue and we
            // do not need to rearrange the list of events.
            if (event != reorder->NextEvent()) {
              // There really should be a show or hide event before the first
              // reorder event.
              if (reorder->PrevEvent()) {
                reorder->PrevEvent()->SetNextEvent(reorder->NextEvent());
              } else {
                mFirstMutationEvent = reorder->NextEvent();
              }

              reorder->NextEvent()->SetPrevEvent(reorder->PrevEvent());
              event->PrevEvent()->SetNextEvent(reorder);
              reorder->SetPrevEvent(event->PrevEvent());
              event->SetPrevEvent(reorder);
              reorder->SetNextEvent(event);
            }
          }
          DropMutationEvent(event);
          break;
        }

        acc = parent;
      }
    } else if (eventType == nsIAccessibleEvent::EVENT_SHOW) {
      LocalAccessible* parent = event->GetAccessible()->LocalParent();
      while (parent) {
        if (parent->IsDoc()) {
          break;
        }

        // if the parent of a show event is being either shown or hidden then
        // we don't need to fire a show event for a subtree of that change.
        if (parent->ShowEventTarget() || parent->HideEventTarget()) {
          DropMutationEvent(event);
          break;
        }

        parent = parent->LocalParent();
      }
    } else if (eventType == nsIAccessibleEvent::EVENT_HIDE) {
      MOZ_ASSERT(eventType == nsIAccessibleEvent::EVENT_HIDE,
                 "mutation event list has an invalid event");

      AccHideEvent* hideEvent = downcast_accEvent(event);
      CoalesceHideEvent(hideEvent);
    }

    event = nextEvent;
  }
}

void NotificationController::ScheduleChildDocBinding(DocAccessible* aDocument) {
  // Schedule child document binding to the tree.
  mHangingChildDocuments.AppendElement(aDocument);
  ScheduleProcessing();
}

void NotificationController::ScheduleContentInsertion(
    LocalAccessible* aContainer, nsTArray<nsCOMPtr<nsIContent>>& aInsertions) {
  if (!aInsertions.IsEmpty()) {
    mContentInsertions.GetOrInsertNew(aContainer)->AppendElements(aInsertions);
    ScheduleProcessing();
  }
}

void NotificationController::ScheduleProcessing() {
  // If notification flush isn't planned yet, start notification flush
  // asynchronously (after style and layout).
  // Note: the mPresShell null-check might be unnecessary; it's just to prevent
  // a null-deref here, if we somehow get called after we've been shut down.
  if (mObservingState == eNotObservingRefresh && mPresShell) {
    if (mDocument->IsPrintDoc()) {
      // A print document is a static clone and thus doesn't need a refresh
      // tick.
      return;
    }
    if (mPresShell->AddRefreshObserver(this, FlushType::Display,
                                       "Accessibility notifications")) {
      mObservingState = eRefreshObserving;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector: protected

bool NotificationController::IsUpdatePending() const {
  return mPresShell->NeedStyleFlush() || mPresShell->NeedLayoutFlush() ||
         mObservingState == eRefreshProcessingForUpdate || WaitingForParent() ||
         mContentInsertions.Count() != 0 || mNotifications.Length() != 0 ||
         !mTextArray.IsEmpty() ||
         !mDocument->HasLoadState(DocAccessible::eTreeConstructed);
}

bool NotificationController::WaitingForParent() const {
  DocAccessible* parentdoc = mDocument->ParentDocument();
  if (!parentdoc) {
    return false;
  }

  NotificationController* parent = parentdoc->mNotificationController;
  if (!parent || parent == this) {
    // Do not wait for nothing or ourselves
    return false;
  }

  // Wait for parent's notifications processing
  return parent->mContentInsertions.Count() != 0 ||
         parent->mNotifications.Length() != 0;
}

void NotificationController::ProcessMutationEvents() {
  // Firing an event can indirectly run script; e.g. an XPCOM event observer
  // or querying a XUL interface. Further mutations might be queued as a result.
  // It's important that the mutation queue and state bits from one tick don't
  // interfere with the next tick. Otherwise, we can end up dropping events.
  // Therefore:
  // 1. Clear the state bits, which we only need for coalescence.
  for (AccTreeMutationEvent* event = mFirstMutationEvent; event;
       event = event->NextEvent()) {
    LocalAccessible* acc = event->GetAccessible();
    acc->SetShowEventTarget(false);
    acc->SetHideEventTarget(false);
    acc->SetReorderEventTarget(false);
  }
  // 2. Keep the current queue locally, but clear the queue on the instance.
  RefPtr<AccTreeMutationEvent> firstEvent = mFirstMutationEvent;
  mFirstMutationEvent = mLastMutationEvent = nullptr;
  mMutationMap.Clear();
  mEventGeneration = 0;

  // Group the show events by the parent of their target.
  nsTHashMap<nsPtrHashKey<LocalAccessible>, nsTArray<AccTreeMutationEvent*>>
      showEvents;
  for (AccTreeMutationEvent* event = firstEvent; event;
       event = event->NextEvent()) {
    if (event->GetEventType() != nsIAccessibleEvent::EVENT_SHOW) {
      continue;
    }

    LocalAccessible* parent = event->GetAccessible()->LocalParent();
    showEvents.LookupOrInsert(parent).AppendElement(event);
  }

  // We need to fire show events for the children of an accessible in the order
  // of their indices at this point.  So sort each set of events for the same
  // container by the index of their target. We do this before firing any events
  // because firing an event might indirectly run script which might alter the
  // tree, breaking our sort. However, we don't actually fire the events yet.
  for (auto iter = showEvents.Iter(); !iter.Done(); iter.Next()) {
    struct AccIdxComparator {
      bool LessThan(const AccTreeMutationEvent* a,
                    const AccTreeMutationEvent* b) const {
        int32_t aIdx = a->GetAccessible()->IndexInParent();
        int32_t bIdx = b->GetAccessible()->IndexInParent();
        MOZ_ASSERT(aIdx >= 0 && bIdx >= 0 && (a == b || aIdx != bIdx));
        return aIdx < bIdx;
      }
      bool Equals(const AccTreeMutationEvent* a,
                  const AccTreeMutationEvent* b) const {
        DebugOnly<int32_t> aIdx = a->GetAccessible()->IndexInParent();
        DebugOnly<int32_t> bIdx = b->GetAccessible()->IndexInParent();
        MOZ_ASSERT(aIdx >= 0 && bIdx >= 0 && (a == b || aIdx != bIdx));
        return a == b;
      }
    };

    nsTArray<AccTreeMutationEvent*>& events = iter.Data();
    events.Sort(AccIdxComparator());
  }

  // there is no reason to fire a hide event for a child of a show event
  // target.  That can happen if something is inserted into the tree and
  // removed before the next refresh driver tick, but it should not be
  // observable outside gecko so it should be safe to coalesce away any such
  // events.  This means that it should be fine to fire all of the hide events
  // first, and then deal with any shown subtrees.
  for (AccTreeMutationEvent* event = firstEvent; event;
       event = event->NextEvent()) {
    if (event->GetEventType() != nsIAccessibleEvent::EVENT_HIDE) {
      continue;
    }

    nsEventShell::FireEvent(event);
    if (!mDocument) {
      return;
    }

    AccMutationEvent* mutEvent = downcast_accEvent(event);
    if (mutEvent->mTextChangeEvent) {
      nsEventShell::FireEvent(mutEvent->mTextChangeEvent);
      if (!mDocument) {
        return;
      }
    }

    AccHideEvent* hideEvent = downcast_accEvent(event);
    if (hideEvent->NeedsShutdown()) {
      mDocument->ShutdownChildrenInSubtree(event->mAccessible);
    }
  }

  // Fire the show events we sorted earlier.
  for (auto iter = showEvents.Iter(); !iter.Done(); iter.Next()) {
    nsTArray<AccTreeMutationEvent*>& events = iter.Data();
    for (AccTreeMutationEvent* event : events) {
      nsEventShell::FireEvent(event);
      if (!mDocument) {
        return;
      }

      AccMutationEvent* mutEvent = downcast_accEvent(event);
      if (mutEvent->mTextChangeEvent) {
        nsEventShell::FireEvent(mutEvent->mTextChangeEvent);
        if (!mDocument) {
          return;
        }
      }
    }
  }

  // Now we can fire the reorder events after all the show and hide events.
  for (const uint32_t reorderType : {nsIAccessibleEvent::EVENT_INNER_REORDER,
                                     nsIAccessibleEvent::EVENT_REORDER}) {
    for (AccTreeMutationEvent* event = firstEvent; event;
         event = event->NextEvent()) {
      if (event->GetEventType() != reorderType) {
        continue;
      }

      if (event->GetAccessible()->IsDefunct()) {
        // An inner reorder target may have been hidden itself and no
        // longer bound to the document.
        MOZ_ASSERT(reorderType == nsIAccessibleEvent::EVENT_INNER_REORDER,
                   "An 'outer' reorder target should not be defunct");
        continue;
      }

      nsEventShell::FireEvent(event);
      if (!mDocument) {
        return;
      }

      // The mutation in the container can change its name, or an ancestor's
      // name. A labelled/described by relation would also need to be notified
      // if this is the case.
      if (PushNameOrDescriptionChange(event)) {
        ScheduleProcessing();
      }

      LocalAccessible* target = event->GetAccessible();
      target->Document()->MaybeNotifyOfValueChange(target);
      if (!mDocument) {
        return;
      }
    }
  }

  // Our events are in a doubly linked list. Clear the pointers to reduce
  // pressure on the cycle collector. Even though clearing the previous pointers
  // removes cycles, this isn't enough. The cycle collector still gets bogged
  // down when there are lots of mutation events if the next pointers aren't
  // cleared. Even without the cycle collector, not clearing the next pointers
  // potentially results in deep recursion because releasing each event releases
  // its next event.
  RefPtr<AccTreeMutationEvent> event = firstEvent;
  while (event) {
    RefPtr<AccTreeMutationEvent> next = event->NextEvent();
    event->SetNextEvent(nullptr);
    event->SetPrevEvent(nullptr);
    event = next;
  }
}

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector: private

void NotificationController::WillRefresh(mozilla::TimeStamp aTime) {
  AUTO_PROFILER_MARKER_UNTYPED("NotificationController::WillRefresh", A11Y, {});

  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_WillRefresh>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.
  auto timer = glean::a11y::tree_update_timing.Measure();
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  AUTO_PROFILER_LABEL("NotificationController::WillRefresh", A11Y);

  // If mDocument is null, the document accessible that this notification
  // controller was created for is now shut down. This means we've lost our
  // ability to unregister ourselves, which is bad. (However, it also shouldn't
  // be logically possible for us to get here with a null mDocument; the only
  // thing that clears that pointer is our Shutdown() method, which first
  // unregisters and fatally asserts if that fails).
  MOZ_RELEASE_ASSERT(
      mDocument,
      "The document was shut down while refresh observer is attached!");

  if (AppShutdown::IsShutdownImpending()) {
    return;
  }

  // Wait until an update, we have started, or an interruptible reflow is
  // finished. We also check the existance of our pres context and root pres
  // context, since if we can't reach either of these the frame tree is being
  // destroyed.
  nsPresContext* pc = mPresShell->GetPresContext();
  if (mObservingState == eRefreshProcessing ||
      mObservingState == eRefreshProcessingForUpdate ||
      mPresShell->IsReflowInterrupted() || !pc || !pc->GetRootPresContext()) {
    return;
  }

  if (mDocument->IPCDoc() && mDocument->IPCDoc()->HasUnackedMutationEvents()) {
    // We've sent mutation events to the parent process, but we haven't
    // received its ACK yet. We defer accessibility updates until we do.
    // Otherwise, we might flood the IPDL queue with many later mutation events
    // while the parent process is still trying to process earlier ones, getting
    // further and further behind and causing the browser to hang for extended
    // periods. If the same nodes are repeatedly changing or being recreated,
    // deferring updates can significantly reduce the overall number of events
    // because we avoid generating events for the intermediate changes that
    // occur while the parent process is busy. This also avoids the associated
    // work to update the tree in the content process. We must defer all
    // work here, not just mutation events, because otherwise, the tree and
    // other events might get out of sync with the mutation events we've
    // processed. Queued content insertions, events, etc. will be processed in
    // a subsequent tick after we receive the ACK, though some of them may be
    // irrelevant (and thus dropped) by the time that happens if a DOM node or
    // Accessible was removed in the interim.
    return;
  }

  // The refresh driver doesn't flush style or layout for a document where
  // rendering is suppressed, but we require up-to-date style and layout. Defer
  // updates until rendering is no longer suppressed. A PresShell which hasn't
  // been initialized has no frames, so it doesn't need to be flushed.
  //
  // XXX Bug 2089366: If style and layout are already up-to-date (e.g. a sync
  // flush), the document might still be painted (e.g. a render-blocked
  // same-process iframe), so we update in that case. Once bug 2089366 is fixed,
  // painting won't ever occur in this case, so the NeedStyleFlush and
  // NeedLayoutFlush checks can be removed then.
  if (mPresShell->DidInitialize() &&
      mDocument->DocumentNode()->IsRenderingSuppressed() &&
      (mPresShell->NeedStyleFlush() || mPresShell->NeedLayoutFlush())) {
    return;
  }

  // Process parent's notifications before ours, to get proper ordering between
  // e.g. tab event and content event.
  if (WaitingForParent()) {
    mDocument->ParentDocument()->mNotificationController->WillRefresh(aTime);
    if (!mDocument || AppShutdown::IsShutdownImpending()) {
      return;
    }
  }

  // Any generic notifications should be queued if we're processing content
  // insertions or generic notifications.
  mObservingState = eRefreshProcessingForUpdate;

  // Initial accessible tree construction.
  if (!mDocument->HasLoadState(DocAccessible::eTreeConstructed)) {
    // (1) If document is not bound to parent at this point, or
    // (2) the PresShell is not initialized (and it isn't about:blank),
    // then the document is not ready yet (process notifications later).
    if (!mDocument->IsBoundToParent() ||
        (!mPresShell->DidInitialize() &&
         !mDocument->DocumentNode()->IsInitialDocument())) {
      mObservingState = eRefreshObserving;
      return;
    }

#ifdef A11Y_LOG
    if (logging::IsEnabled(logging::eTree)) {
      logging::MsgBegin("TREE", "initial tree created");
      logging::Address("document", mDocument);
      logging::MsgEnd();
    }
#endif

    if (!mDocument->DocumentNode()->IsCurrentActiveDocument()) {
      // Our document is no longer current for its WindowGlobal; e.g. we're
      // the initial about:blank and a new document has replaced us in that
      // WindowGlobal. Shut ourselves down instead of building an accessibility
      // tree (and IPC actor) nobody will use.
      mDocument->Shutdown();
      return;
    }

    mDocument->DoInitialUpdate();
    if (AppShutdown::IsShutdownImpending()) {
      return;
    }

    NS_ASSERTION(mContentInsertions.Count() == 0,
                 "Pending content insertions while initial accessible tree "
                 "isn't created!");
  }

  mDocument->ProcessPendingUpdates();

  // Process rendered text change notifications. Even though we want to process
  // them in the order in which they were queued, we still want to avoid
  // duplicates.
  nsTHashSet<nsIContent*> textHash;
  for (nsIContent* textNode : mTextArray) {
    if (!textHash.EnsureInserted(textNode)) {
      continue;  // Already processed.
    }
    LocalAccessible* textAcc = mDocument->GetAccessible(textNode);

    // If the text node is not in tree or doesn't have a frame, or placed in
    // another document, then this case should have been handled already by
    // content removal notifications.
    nsINode* containerNode = textNode->GetFlattenedTreeParentNode();
    if (!containerNode || textNode->OwnerDoc() != mDocument->DocumentNode()) {
      MOZ_ASSERT(!textAcc,
                 "Text node was removed but accessible is kept alive!");
      continue;
    }

    if (containerNode->IsGeneratedContentContainerForMarker()) {
      // This is the text of a bullet. Unlike most TextLeafAccessibles which are
      // backed by a DOM text node, an HTMLListBulletAccessible is backed by the
      // ::marker element and gets its text differently.
      if (LocalAccessible* bullet = mDocument->GetAccessible(containerNode)) {
        nsAutoString text;
        HTMLListBulletAccessible::ComputeText(containerNode->AsContent(), text);
        TextUpdater::Run(mDocument, bullet->AsTextLeaf(), text);
      }
      continue;
    }

    nsIFrame* textFrame = textNode->GetPrimaryFrame();
    if (!textFrame) {
      MOZ_ASSERT(!textAcc,
                 "Text node isn't rendered but accessible is kept alive!");
      continue;
    }

#ifdef A11Y_LOG
    nsIContent* containerElm =
        containerNode->IsElement() ? containerNode->AsElement() : nullptr;
#endif

    nsIFrame::RenderedText text = textFrame->GetRenderedText(
        0, UINT32_MAX, nsIFrame::TextOffsetType::OffsetsInContentText,
        nsIFrame::TrailingWhitespace::DontTrim);

    if (textAcc) {
      // Remove the TextLeafAccessible if:
      // 1. The rendered text is empty; or
      // 2. The text is invisible, semantically irrelevant whitespace before a
      // hard line break, and there is no CSS alt text.
      if (text.mString.IsEmpty() ||
          (nsCoreUtils::IsTrimmedWhitespaceBeforeHardLineBreak(textFrame) &&
           !nsCoreUtils::GetCssAltContent(textNode))) {
#ifdef A11Y_LOG
        if (logging::IsEnabled(logging::eTree | logging::eText)) {
          logging::MsgBegin("TREE", "text node lost its content; doc: %p",
                            mDocument);
          logging::Node("container", containerElm);
          logging::Node("content", textNode);
          logging::MsgEnd();
        }
#endif

        mDocument->ContentRemoved(textAcc);
        continue;
      }

      // Update text of the accessible and fire text change events.
#ifdef A11Y_LOG
      if (logging::IsEnabled(logging::eText)) {
        logging::MsgBegin("TEXT", "text may be changed; doc: %p", mDocument);
        logging::Node("container", containerElm);
        logging::Node("content", textNode);
        logging::MsgEntry(
            "old text '%s'",
            NS_ConvertUTF16toUTF8(textAcc->AsTextLeaf()->Text()).get());
        logging::MsgEntry("new text: '%s'",
                          NS_ConvertUTF16toUTF8(text.mString).get());
        logging::MsgEnd();
      }
#endif

      if (nsCoreUtils::GetCssAltContent(textNode)) {
        // A11y doesn't care about the text rendered by layout if there is CSS
        // content alt text. We skip this here rather than when the update is
        // queued because the TextLeafAccessible might not exist yet and we
        // might need to create it below.
        continue;
      }

      TextUpdater::Run(mDocument, textAcc->AsTextLeaf(), text.mString);
      continue;
    }

    // Append an accessible if rendered text is not empty.
    if (!text.mString.IsEmpty()) {
#ifdef A11Y_LOG
      if (logging::IsEnabled(logging::eTree | logging::eText)) {
        logging::MsgBegin("TREE", "text node gains new content; doc: %p",
                          mDocument);
        logging::Node("container", containerElm);
        logging::Node("content", textNode);
        logging::MsgEnd();
      }
#endif

      MOZ_ASSERT(mDocument->AccessibleOrTrueContainer(containerNode),
                 "Text node having rendered text hasn't accessible document!");

      LocalAccessible* container =
          mDocument->AccessibleOrTrueContainer(containerNode, true);
      if (container) {
        nsTArray<nsCOMPtr<nsIContent>>* list =
            mContentInsertions.GetOrInsertNew(container);
        list->AppendElement(textNode);
      }
    }
  }
  textHash.Clear();
  mTextArray.Clear();

  // Process content inserted notifications to update the tree.
  // Processing an insertion can indirectly run script (e.g. querying a XUL
  // interface), which might result in another insertion being queued.
  // We don't want to lose any queued insertions if this happens. Therefore, we
  // move the current insertions into a temporary data structure and process
  // them from there. Any insertions queued during processing will get handled
  // in subsequent refresh driver ticks.
  const auto contentInsertions = std::move(mContentInsertions);
  for (const auto& entry : contentInsertions) {
    mDocument->ProcessContentInserted(entry.GetKey(), entry.GetData().get());
    if (!mDocument) {
      return;
    }
  }

  // Bind hanging child documents unless we are using IPC and the
  // document has no IPC actor.  If we fail to bind the child doc then
  // shut it down.
  uint32_t hangingDocCnt = mHangingChildDocuments.Length();
  nsTArray<RefPtr<DocAccessible>> newChildDocs;
  for (uint32_t idx = 0; idx < hangingDocCnt; idx++) {
    DocAccessible* childDoc = mHangingChildDocuments[idx];
    if (childDoc->IsDefunct()) {
      continue;
    }

    if (mDocument->ShouldSendToParentProcess() && !mDocument->IPCDoc()) {
      childDoc->Shutdown();
      continue;
    }

    nsIContent* ownerContent = childDoc->DocumentNode()->GetEmbedderElement();
    if (ownerContent) {
      LocalAccessible* outerDocAcc = mDocument->GetAccessible(ownerContent);
      if (outerDocAcc && outerDocAcc->AppendChild(childDoc)) {
        if (mDocument->AppendChildDocument(childDoc)) {
          newChildDocs.AppendElement(std::move(mHangingChildDocuments[idx]));
          continue;
        }

        outerDocAcc->RemoveChild(childDoc);
      }

      // Failed to bind the child document, destroy it.
      childDoc->Shutdown();
    }
  }

  // Clear the hanging documents list, even if we didn't bind them.
  mHangingChildDocuments.Clear();
  MOZ_ASSERT(mDocument, "Illicit document shutdown");
  if (!mDocument) {
    return;
  }

  // If the document is ready and all its subdocuments are completely loaded
  // then process the document load.
  if (mDocument->HasLoadState(DocAccessible::eReady) &&
      !mDocument->HasLoadState(DocAccessible::eCompletelyLoaded) &&
      hangingDocCnt == 0) {
    uint32_t childDocCnt = mDocument->ChildDocumentCount(), childDocIdx = 0;
    for (; childDocIdx < childDocCnt; childDocIdx++) {
      DocAccessible* childDoc = mDocument->GetChildDocumentAt(childDocIdx);
      if (!childDoc->HasLoadState(DocAccessible::eCompletelyLoaded)) {
        break;
      }
    }

    if (childDocIdx == childDocCnt) {
      mDocument->ProcessLoad();
      if (!mDocument) {
        return;
      }
    }
  }

  // Process invalidation list of the document after all accessible tree
  // mutation is done.
  mDocument->ProcessInvalidationList();

  ProcessRelocations();

  // Process only currently queued generic notifications.
  // These are used for processing aria-activedescendant, DOMMenuItemActive,
  // etc. Therefore, they must be processed after relocations, since relocated
  // subtrees might not have been created before relocation processing and the
  // target might be inside a relocated subtree.
  const nsTArray<RefPtr<Notification>> notifications =
      std::move(mNotifications);

  uint32_t notificationCount = notifications.Length();
  for (uint32_t idx = 0; idx < notificationCount; idx++) {
    notifications[idx]->Process();
    if (!mDocument) {
      return;
    }
  }

  if (AppShutdown::IsShutdownImpending()) {
    return;
  }

  // If a generic notification occurs after this point then we may be allowed to
  // process it synchronously.  However we do not want to reenter if fireing
  // events causes script to run.
  mObservingState = eRefreshProcessing;

  mDocument->SendAccessiblesWillMove();

  // Send any queued cache updates before we fire any mutation events so the
  // cache is up to date when mutation events are fired. We do this after
  // insertions (but not their events) so that cache updates dependent on the
  // tree work correctly; e.g. line start calculation.
  if (IPCAccessibilityActive() && mDocument) {
    mDocument->ProcessQueuedCacheUpdates();
  }

  CoalesceMutationEvents();
  ProcessMutationEvents();

  // ProcessMutationEvents for content process documents merely queues mutation
  // events. Send those events in a batch now if applicable.
  if (mDocument && mDocument->IPCDoc()) {
    mDocument->IPCDoc()->SendQueuedMutationEvents();
  }

  // When firing mutation events, mObservingState is set to
  // eRefreshProcessing. Any calls to ScheduleProcessing() that
  // occur before mObservingState is reset will be dropped because we only
  // schedule a tick if mObservingState == eNotObservingRefresh.
  // This sometimes results in our viewport cache being out-of-date after
  // processing mutation events. Call ProcessQueuedCacheUpdates again to
  // ensure it is updated.
  if (IPCAccessibilityActive() && mDocument) {
    mDocument->ProcessQueuedCacheUpdates();
  }

  if (mDocument) {
    mDocument->ClearMutationData();
  }

  if (AppShutdown::IsShutdownImpending()) {
    return;
  }

  ProcessEventQueue();
  if (mDocument) {
    // Process an anchor jump (if any) now that the tree and focus are up to
    // date.
    mDocument->ProcessAnchorJump();
  }

  if (mDocument && mDocument->IPCDoc()) {
    // There should not be any more mutation events in the mutation event queue.
    // ProcessEventQueue should have sent all of them.
    MOZ_ASSERT(mDocument->IPCDoc()->MutationEventQueueLength() == 0,
               "Mutation event queue is non-empty.");
    if (mDocument->IPCDoc()->HasUnackedMutationEvents()) {
      // Now that all mutation events have been sent, request an ACK from the
      // parent process. This request will be after the mutation events in the
      // IPDL queue, so the parent process will respond once it has finished
      // handling all the mutation events.
      (void)mDocument->IPCDoc()->SendRequestAckMutationEvents();
    }
  }

  if (mDocument->ShouldSendToParentProcess()) {
    size_t newDocCount = newChildDocs.Length();
    for (size_t i = 0; i < newDocCount; i++) {
      DocAccessible* childDoc = newChildDocs[i];
      if (childDoc->IsDefunct()) {
        continue;
      }

      LocalAccessible* parent = childDoc->LocalParent();
      DocAccessibleChild* parentIPCDoc = mDocument->IPCDoc();
      MOZ_DIAGNOSTIC_ASSERT(parentIPCDoc);
      uint64_t id = reinterpret_cast<uintptr_t>(parent->UniqueID());
      MOZ_DIAGNOSTIC_ASSERT(id);
      DocAccessibleChild* ipcDoc = childDoc->IPCDoc();
      if (ipcDoc) {
        parentIPCDoc->SendBindChildDoc(WrapNotNull(ipcDoc), id);
        continue;
      }

      if (WindowGlobalChild* wgc =
              childDoc->DocumentNode()->GetWindowGlobalChild()) {
        RefPtr<DocAccessibleChild> newIpcDoc =
            new DocAccessibleChild(childDoc, wgc);
        childDoc->SetIPCDoc(newIpcDoc);
        wgc->SendPDocAccessibleConstructor(newIpcDoc, id,
                                           childDoc->IsPrintDoc());
      }
    }
  }

  if (!mDocument) {
    // A null mDocument means we've gotten a Shutdown() call (presumably via
    // some script that we triggered above), and that means we're done here.
    // Note: in this case, it's important that don't modify mObservingState;
    // Shutdown() will have *unregistered* us as a refresh observer, and we
    // don't want to mistakenly overwrite mObservingState and fool ourselves
    // into thinking we've re-registered when we really haven't!
    MOZ_ASSERT(mObservingState == eNotObservingRefresh,
               "We've been shutdown, which means we should've been "
               "unregistered as a refresh observer");
    return;
  }
  mObservingState = eRefreshObserving;

  // Stop further processing if there are no new notifications of any kind or
  // events and document load is processed.
  if (mContentInsertions.Count() == 0 && mNotifications.IsEmpty() &&
      !mFocusEvent && mEvents.IsEmpty() && mTextArray.IsEmpty() &&
      mHangingChildDocuments.IsEmpty() &&
      mDocument->HasLoadState(DocAccessible::eCompletelyLoaded) &&
      mPresShell->RemoveRefreshObserver(this, FlushType::Display)) {
    mObservingState = eNotObservingRefresh;
  }
}

void NotificationController::ProcessRelocations() {
  for (uint32_t idx = 0; idx < mRelocations.Length(); idx++) {
    // owner should be in a document and have na associated DOM node (docs
    // sometimes don't)
    if (mRelocations[idx]->IsInDocument() &&
        mRelocations[idx]->HasOwnContent()) {
      mDocument->DoARIAOwnsRelocation(mRelocations[idx]);
    }
  }
  mRelocations.Clear();
}

void NotificationController::EventMap::PutEvent(AccTreeMutationEvent* aEvent) {
  EventType type = GetEventType(aEvent);
  uint64_t addr = reinterpret_cast<uintptr_t>(aEvent->GetAccessible());
  MOZ_ASSERT((addr & 0x3) == 0, "accessible is not 4 byte aligned");
  addr |= type;
  mTable.InsertOrUpdate(addr, RefPtr{aEvent});
}

AccTreeMutationEvent* NotificationController::EventMap::GetEvent(
    LocalAccessible* aTarget, EventType aType) {
  uint64_t addr = reinterpret_cast<uintptr_t>(aTarget);
  MOZ_ASSERT((addr & 0x3) == 0, "target is not 4 byte aligned");

  addr |= aType;
  return mTable.GetWeak(addr);
}

void NotificationController::EventMap::RemoveEvent(
    AccTreeMutationEvent* aEvent) {
  EventType type = GetEventType(aEvent);
  uint64_t addr = reinterpret_cast<uintptr_t>(aEvent->GetAccessible());
  MOZ_ASSERT((addr & 0x3) == 0, "accessible is not 4 byte aligned");
  addr |= type;

  MOZ_ASSERT(mTable.GetWeak(addr) == aEvent, "mTable has the wrong event");
  mTable.Remove(addr);
}

NotificationController::EventMap::EventType
NotificationController::EventMap::GetEventType(AccTreeMutationEvent* aEvent) {
  switch (aEvent->GetEventType()) {
    case nsIAccessibleEvent::EVENT_SHOW:
      return ShowEvent;
    case nsIAccessibleEvent::EVENT_HIDE:
      return HideEvent;
    case nsIAccessibleEvent::EVENT_REORDER:
    case nsIAccessibleEvent::EVENT_INNER_REORDER:
      return ReorderEvent;
    default:
      MOZ_ASSERT_UNREACHABLE("event has invalid type");
      return ShowEvent;
  }
}
