/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EventQueue.h"

#include "mozilla/PerfStats.h"
#include "mozilla/ProfilerMarkers.h"

#include "LocalAccessible-inl.h"
#include "nsEventShell.h"
#include "DocAccessibleChild.h"
#include "nsTextEquivUtils.h"
#ifdef A11Y_LOG
#  include "Logging.h"
#endif
#include "Relation.h"

namespace mozilla {
namespace a11y {

// Defines the number of selection add/remove events in the queue when they
// aren't packed into single selection within event.
const unsigned int kSelChangeCountToPack = 5;

////////////////////////////////////////////////////////////////////////////////
// EventQueue
////////////////////////////////////////////////////////////////////////////////

bool EventQueue::PushEvent(AccEvent* aEvent) {
  NS_ASSERTION((aEvent->mAccessible && aEvent->mAccessible->IsApplication()) ||
                   aEvent->Document() == mDocument,
               "Queued event belongs to another document!");

  if (aEvent->mEventType == nsIAccessibleEvent::EVENT_FOCUS) {
    mFocusEvent = aEvent;
    return true;
  }

  if (aEvent->mEventRule == AccEvent::eRemoveDupes && !mEvents.IsEmpty()) {
    // Check for duplicate events. If aEvent is identical to an older event, do
    // not append aEvent. We do this here rather than in CoalesceEvents because
    // CoalesceEvents never *removes* events; it only sets them to eDoNotEmit.
    // If there are many duplicate events and we appended them, this would
    // result in a massive event queue and coalescing would become increasingly
    // slow with each event queued. Doing it here, we avoid appending a
    // duplicate event in the first place.
    uint32_t last = mEvents.Length() - 1;
    for (uint32_t index = last; index <= last; --index) {
      AccEvent* checkEvent = mEvents[index];
      if (checkEvent->mEventType == aEvent->mEventType &&
          checkEvent->mEventRule == aEvent->mEventRule &&
          checkEvent->mAccessible == aEvent->mAccessible) {
        aEvent->mEventRule = AccEvent::eDoNotEmit;
        return true;
      }
    }
  }

  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier, or change the return type to void.
  mEvents.AppendElement(aEvent);

  // Filter events.
  CoalesceEvents();

  if (aEvent->mEventType == nsIAccessibleEvent::EVENT_NAME_CHANGE ||
      aEvent->mEventType == nsIAccessibleEvent::EVENT_TEXT_REMOVED ||
      aEvent->mEventType == nsIAccessibleEvent::EVENT_TEXT_INSERTED) {
    MOZ_ASSERT(aEvent->mEventRule != AccEvent::eDoNotEmit);
    PushNameOrDescriptionChange(aEvent);
  }
  return true;
}

bool EventQueue::PushNameOrDescriptionChangeToRelations(
    LocalAccessible* aAccessible, RelationType aType) {
  MOZ_ASSERT(aType == RelationType::LABEL_FOR ||
             aType == RelationType::DESCRIPTION_FOR);

  bool pushed = false;
  uint32_t eventType = aType == RelationType::LABEL_FOR
                           ? nsIAccessibleEvent::EVENT_NAME_CHANGE
                           : nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE;
  Relation rel = aAccessible->RelationByType(aType);
  while (LocalAccessible* relTarget = rel.LocalNext()) {
    RefPtr<AccEvent> nameChangeEvent = new AccEvent(eventType, relTarget);
    pushed |= PushEvent(nameChangeEvent);
  }

  return pushed;
}

bool EventQueue::PushNameOrDescriptionChange(AccEvent* aOrigEvent) {
  // Fire name/description change event on parent or related LocalAccessible
  // being labelled/described given that this event hasn't been coalesced, the
  // dependent's name/description was calculated from this subtree, and the
  // subtree was changed.
  LocalAccessible* target = aOrigEvent->mAccessible;
  // If the text of a text leaf changed without replacing the leaf, the only
  // event we get is text inserted on the container. Or, a reorder event may
  // change the container's name. In this case, we might need to fire a name
  // change event on the target itself.
  const bool maybeTargetNameChanged =
      (aOrigEvent->mEventType == nsIAccessibleEvent::EVENT_TEXT_REMOVED ||
       aOrigEvent->mEventType == nsIAccessibleEvent::EVENT_TEXT_INSERTED ||
       aOrigEvent->mEventType == nsIAccessibleEvent::EVENT_REORDER ||
       aOrigEvent->mEventType == nsIAccessibleEvent::EVENT_INNER_REORDER) &&
      nsTextEquivUtils::HasNameRule(target, eNameFromSubtreeRule);
  const bool doName = target->HasNameDependent() || maybeTargetNameChanged;
  const bool doDesc = target->HasDescriptionDependent();

  if (!doName && !doDesc) {
    return false;
  }
  bool pushed = false;
  bool nameCheckAncestor = true;
  // Only continue traversing up the tree if it's possible that the parent
  // LocalAccessible's name (or a LocalAccessible being labelled by this
  // LocalAccessible or an ancestor) can depend on this LocalAccessible's name.
  LocalAccessible* parent = target;
  do {
    // Test possible name dependent parent.
    if (doName) {
      if (nameCheckAncestor && (maybeTargetNameChanged || parent != target) &&
          nsTextEquivUtils::HasNameRule(parent, eNameFromSubtreeRule)) {
        // HTML file inputs always get part of their name from the subtree, even
        // if the author provided a name.
        bool fireNameChange = parent->IsHTMLFileInput();
        if (!fireNameChange) {
          nsAutoString name;
          ENameValueFlag nameFlag = parent->Name(name);
          switch (nameFlag) {
            case eNameOK:
              // Descendants of subtree may have been removed, making the name
              // void.
              fireNameChange = name.IsVoid();
              break;
            case eNameFromSubtree:
              // If name is obtained from subtree, fire name change event.
              fireNameChange = true;
              break;
            case eNameFromTooltip:
              // If the descendants of this accessible were removed, the name
              // may be calculated using the tooltip. We can guess that the name
              // was obtained from the subtree before.
              fireNameChange = true;
              break;
            default:
              MOZ_ASSERT_UNREACHABLE("All name flags not covered!");
          }
        }

        if (fireNameChange) {
          RefPtr<AccEvent> nameChangeEvent =
              new AccEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, parent);
          pushed |= PushEvent(nameChangeEvent);
        }
        nameCheckAncestor = false;
      }

      pushed |= PushNameOrDescriptionChangeToRelations(parent,
                                                       RelationType::LABEL_FOR);
    }

    if (doDesc) {
      pushed |= PushNameOrDescriptionChangeToRelations(
          parent, RelationType::DESCRIPTION_FOR);
    }

    if (parent->IsDoc()) {
      // Never cross document boundaries.
      break;
    }
    parent = parent->LocalParent();
  } while (parent &&
           nsTextEquivUtils::HasNameRule(parent, eNameFromSubtreeIfReqRule));

  return pushed;
}

////////////////////////////////////////////////////////////////////////////////
// EventQueue: private

void EventQueue::CoalesceEvents() {
  AUTO_PROFILER_MARKER_TEXT("EventQueue::CoalesceEvents", A11Y, {}, ""_ns);
  PerfStats::AutoMetricRecording<PerfStats::Metric::A11Y_CoalesceEvents>
      autoRecording;
  // DO NOT ADD CODE ABOVE THIS BLOCK: THIS CODE IS MEASURING TIMINGS.

  NS_ASSERTION(mEvents.Length(), "There should be at least one pending event!");
  uint32_t tail = mEvents.Length() - 1;
  AccEvent* tailEvent = mEvents[tail];

  switch (tailEvent->mEventRule) {
    case AccEvent::eCoalesceReorder: {
      DebugOnly<LocalAccessible*> target = tailEvent->mAccessible.get();
      MOZ_ASSERT(
          target->IsApplication() || target->IsOuterDoc() ||
              target->IsXULTree(),
          "Only app or outerdoc accessible reorder events are in the queue");
      MOZ_ASSERT(tailEvent->GetEventType() == nsIAccessibleEvent::EVENT_REORDER,
                 "only reorder events should be queued");
      break;  // case eCoalesceReorder
    }

    case AccEvent::eCoalesceOfSameType: {
      // Coalesce old events by newer event.
      for (uint32_t index = tail - 1; index < tail; index--) {
        AccEvent* accEvent = mEvents[index];
        if (accEvent->mEventType == tailEvent->mEventType &&
            accEvent->mEventRule == tailEvent->mEventRule) {
          accEvent->mEventRule = AccEvent::eDoNotEmit;
          return;
        }
      }
      break;  // case eCoalesceOfSameType
    }

    case AccEvent::eCoalesceSelectionChange: {
      AccSelChangeEvent* tailSelChangeEvent = downcast_accEvent(tailEvent);
      for (uint32_t index = tail - 1; index < tail; index--) {
        AccEvent* thisEvent = mEvents[index];
        if (thisEvent->mEventRule == tailEvent->mEventRule) {
          AccSelChangeEvent* thisSelChangeEvent = downcast_accEvent(thisEvent);

          // Coalesce selection change events within same control.
          if (tailSelChangeEvent->mWidget == thisSelChangeEvent->mWidget) {
            CoalesceSelChangeEvents(tailSelChangeEvent, thisSelChangeEvent,
                                    index);
            return;
          }
        }
      }
      break;  // eCoalesceSelectionChange
    }

    case AccEvent::eCoalesceStateChange: {
      // If state change event is duped then ignore previous event. If state
      // change event is opposite to previous event then no event is emitted
      // (accessible state wasn't changed).
      for (uint32_t index = tail - 1; index < tail; index--) {
        AccEvent* thisEvent = mEvents[index];
        if (thisEvent->mEventRule != AccEvent::eDoNotEmit &&
            thisEvent->mEventType == tailEvent->mEventType &&
            thisEvent->mAccessible == tailEvent->mAccessible) {
          AccStateChangeEvent* thisSCEvent = downcast_accEvent(thisEvent);
          AccStateChangeEvent* tailSCEvent = downcast_accEvent(tailEvent);
          if (thisSCEvent->mState == tailSCEvent->mState) {
            thisEvent->mEventRule = AccEvent::eDoNotEmit;
            if (thisSCEvent->mIsEnabled != tailSCEvent->mIsEnabled) {
              tailEvent->mEventRule = AccEvent::eDoNotEmit;
            }
          }
        }
      }
      break;  // eCoalesceStateChange
    }

    case AccEvent::eCoalesceTextSelChange: {
      // Coalesce older event by newer event for the same selection or target.
      // Events for same selection may have different targets and vice versa one
      // target may be pointed by different selections (for latter see
      // bug 927159).
      for (uint32_t index = tail - 1; index < tail; index--) {
        AccEvent* thisEvent = mEvents[index];
        if (thisEvent->mEventRule != AccEvent::eDoNotEmit &&
            thisEvent->mEventType == tailEvent->mEventType) {
          AccTextSelChangeEvent* thisTSCEvent = downcast_accEvent(thisEvent);
          AccTextSelChangeEvent* tailTSCEvent = downcast_accEvent(tailEvent);
          if (thisTSCEvent->mSel == tailTSCEvent->mSel ||
              thisEvent->mAccessible == tailEvent->mAccessible) {
            thisEvent->mEventRule = AccEvent::eDoNotEmit;
          }
        }
      }
      break;  // eCoalesceTextSelChange
    }

    default:
      // eRemoveDupes is handled in PushEvent.
      break;  // case eRemoveDupes, eAllowDupes, eDoNotEmit
  }  // switch
}

void EventQueue::CoalesceSelChangeEvents(AccSelChangeEvent* aTailEvent,
                                         AccSelChangeEvent* aThisEvent,
                                         uint32_t aThisIndex) {
  aTailEvent->mPreceedingCount = aThisEvent->mPreceedingCount + 1;

  // Pack all preceding events into single selection within event
  // when we receive too much selection add/remove events.
  if (aTailEvent->mPreceedingCount >= kSelChangeCountToPack) {
    aTailEvent->mEventType = nsIAccessibleEvent::EVENT_SELECTION_WITHIN;
    aTailEvent->mAccessible = aTailEvent->mWidget;
    aThisEvent->mEventRule = AccEvent::eDoNotEmit;

    // Do not emit any preceding selection events for same widget if they
    // weren't coalesced yet.
    if (aThisEvent->mEventType != nsIAccessibleEvent::EVENT_SELECTION_WITHIN) {
      for (uint32_t jdx = aThisIndex - 1; jdx < aThisIndex; jdx--) {
        AccEvent* prevEvent = mEvents[jdx];
        if (prevEvent->mEventRule == aTailEvent->mEventRule) {
          AccSelChangeEvent* prevSelChangeEvent = downcast_accEvent(prevEvent);
          if (prevSelChangeEvent->mWidget == aTailEvent->mWidget) {
            prevSelChangeEvent->mEventRule = AccEvent::eDoNotEmit;
          }
        }
      }
    }
    return;
  }

  // Pack sequential selection remove and selection add events into
  // single selection change event.
  if (aTailEvent->mPreceedingCount == 1 &&
      aTailEvent->mItem != aThisEvent->mItem) {
    if (aTailEvent->mSelChangeType == AccSelChangeEvent::eSelectionAdd &&
        aThisEvent->mSelChangeType == AccSelChangeEvent::eSelectionRemove) {
      aThisEvent->mEventRule = AccEvent::eDoNotEmit;
      aTailEvent->mEventType = nsIAccessibleEvent::EVENT_SELECTION;
      aTailEvent->mPackedEvent = aThisEvent;
      return;
    }

    if (aThisEvent->mSelChangeType == AccSelChangeEvent::eSelectionAdd &&
        aTailEvent->mSelChangeType == AccSelChangeEvent::eSelectionRemove) {
      aTailEvent->mEventRule = AccEvent::eDoNotEmit;
      aThisEvent->mEventType = nsIAccessibleEvent::EVENT_SELECTION;
      aThisEvent->mPackedEvent = aTailEvent;
      return;
    }
  }

  // Unpack the packed selection change event because we've got one
  // more selection add/remove.
  if (aThisEvent->mEventType == nsIAccessibleEvent::EVENT_SELECTION) {
    if (aThisEvent->mPackedEvent) {
      aThisEvent->mPackedEvent->mEventType =
          aThisEvent->mPackedEvent->mSelChangeType ==
                  AccSelChangeEvent::eSelectionAdd
              ? nsIAccessibleEvent::EVENT_SELECTION_ADD
              : nsIAccessibleEvent::EVENT_SELECTION_REMOVE;

      aThisEvent->mPackedEvent->mEventRule = AccEvent::eCoalesceSelectionChange;

      aThisEvent->mPackedEvent = nullptr;
    }

    aThisEvent->mEventType =
        aThisEvent->mSelChangeType == AccSelChangeEvent::eSelectionAdd
            ? nsIAccessibleEvent::EVENT_SELECTION_ADD
            : nsIAccessibleEvent::EVENT_SELECTION_REMOVE;

    return;
  }

  // Convert into selection add since control has single selection but other
  // selection events for this control are queued.
  if (aTailEvent->mEventType == nsIAccessibleEvent::EVENT_SELECTION) {
    aTailEvent->mEventType = nsIAccessibleEvent::EVENT_SELECTION_ADD;
  }
}

////////////////////////////////////////////////////////////////////////////////
// EventQueue: event queue

void EventQueue::ProcessEventQueue() {
  // Process only currently queued events.
  const nsTArray<RefPtr<AccEvent> > events = std::move(mEvents);
  nsTArray<uint64_t> selectedIDs;
  nsTArray<uint64_t> unselectedIDs;

  uint32_t eventCount = events.Length();
#ifdef A11Y_LOG
  if ((eventCount > 0 || mFocusEvent) && logging::IsEnabled(logging::eEvents)) {
    logging::MsgBegin("EVENTS", "events processing");
    logging::Address("document", mDocument);
    logging::MsgEnd();
  }
#endif

  if (mFocusEvent) {
    // Always fire a pending focus event before all other events. We do this for
    // two reasons:
    // 1. It prevents extraneous screen reader speech if the name, states, etc.
    // of the currently focused item change at the same time as another item is
    // focused. If aria-activedescendant is used, even setting
    // aria-activedescendant before changing other properties results in the
    // property change events being queued before the focus event because we
    // process  aria-activedescendant async.
    // 2. It improves perceived performance. Focus changes should be reported as
    // soon as possible, so clients should handle focus events before they spend
    // time on anything else.
    RefPtr<AccEvent> event = std::move(mFocusEvent);
    if (!event->mAccessible->IsDefunct()) {
      FocusMgr()->ProcessFocusEvent(event);
    }
  }

  for (uint32_t idx = 0; idx < eventCount; idx++) {
    AccEvent* event = events[idx];
    uint32_t eventType = event->mEventType;
    LocalAccessible* target = event->GetAccessible();
    if (!target || target->IsDefunct()) {
      continue;
    }

    // Collect select changes
    if (IPCAccessibilityActive()) {
      if ((event->mEventRule == AccEvent::eDoNotEmit &&
           (eventType == nsIAccessibleEvent::EVENT_SELECTION_ADD ||
            eventType == nsIAccessibleEvent::EVENT_SELECTION_REMOVE ||
            eventType == nsIAccessibleEvent::EVENT_SELECTION)) ||
          eventType == nsIAccessibleEvent::EVENT_SELECTION_WITHIN) {
        // The selection even was either dropped or morphed to a
        // selection-within. We need to collect the items from all these events
        // and manually push their new state to the parent process.
        AccSelChangeEvent* selChangeEvent = downcast_accEvent(event);
        LocalAccessible* item = selChangeEvent->mItem;
        if (!item->IsDefunct()) {
          uint64_t itemID =
              item->IsDoc() ? 0 : reinterpret_cast<uint64_t>(item->UniqueID());
          bool selected = selChangeEvent->mSelChangeType ==
                          AccSelChangeEvent::eSelectionAdd;
          if (selected) {
            selectedIDs.AppendElement(itemID);
          } else {
            unselectedIDs.AppendElement(itemID);
          }
        }
      }
    }

    if (event->mEventRule == AccEvent::eDoNotEmit) {
      continue;
    }

    // Dispatch caret moved and text selection change events.
    if (eventType == nsIAccessibleEvent::EVENT_TEXT_SELECTION_CHANGED) {
      SelectionMgr()->ProcessTextSelChangeEvent(event);
      continue;
    }

    // Fire selected state change events in support to selection events.
    if (eventType == nsIAccessibleEvent::EVENT_SELECTION_ADD) {
      nsEventShell::FireEvent(event->mAccessible, states::SELECTED, true,
                              event->mIsFromUserInput);

    } else if (eventType == nsIAccessibleEvent::EVENT_SELECTION_REMOVE) {
      nsEventShell::FireEvent(event->mAccessible, states::SELECTED, false,
                              event->mIsFromUserInput);

    } else if (eventType == nsIAccessibleEvent::EVENT_SELECTION) {
      AccSelChangeEvent* selChangeEvent = downcast_accEvent(event);
      nsEventShell::FireEvent(
          event->mAccessible, states::SELECTED,
          (selChangeEvent->mSelChangeType == AccSelChangeEvent::eSelectionAdd),
          event->mIsFromUserInput);

      if (selChangeEvent->mPackedEvent) {
        nsEventShell::FireEvent(selChangeEvent->mPackedEvent->mAccessible,
                                states::SELECTED,
                                (selChangeEvent->mPackedEvent->mSelChangeType ==
                                 AccSelChangeEvent::eSelectionAdd),
                                selChangeEvent->mPackedEvent->mIsFromUserInput);
      }
    }

    nsEventShell::FireEvent(event);

    if (!mDocument) {
      return;
    }

    // Some mutation events may be queued incidentally by this function. Send
    // them immediately so they stay in order. This can happen due to code in
    // DoInitialUpdate and TextUpdater that calls FireDelayedEvent for mutation
    // events, rather than QueueMutationEvent. DoInitialUpdate can do this with
    // reorder events, and TextUpdater can do this with text inserted/removed
    // events. Process these events now to avoid sending them out-of-order.
    if (eventType == nsIAccessibleEvent::EVENT_REORDER ||
        eventType == nsIAccessibleEvent::EVENT_TEXT_INSERTED ||
        eventType == nsIAccessibleEvent::EVENT_TEXT_REMOVED) {
      if (auto* ipcDoc = mDocument->IPCDoc()) {
        ipcDoc->SendQueuedMutationEvents();
      }
    }
  }

  if (mDocument && IPCAccessibilityActive() &&
      (!selectedIDs.IsEmpty() || !unselectedIDs.IsEmpty())) {
    DocAccessibleChild* ipcDoc = mDocument->IPCDoc();
    ipcDoc->SendSelectedAccessiblesChanged(selectedIDs, unselectedIDs);
  }
}

}  // namespace a11y
}  // namespace mozilla
