/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextComposition.h"

#include "ContentEventHandler.h"
#include "IMEContentObserver.h"
#include "IMEStateManager.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/EditorBase.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/RangeBoundary.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_intl.h"
#include "mozilla/TextEvents.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/EditContext.h"
#include "nsContentUtils.h"
#include "nsIContent.h"
#include "nsIMutationObserver.h"
#include "nsPresContext.h"

#ifdef XP_MACOSX
// Some defiens will be conflict with OSX SDK
#  define TextRange _TextRange
#  define TextRangeArray _TextRangeArray
#  define Comment _Comment
#endif

#ifdef XP_MACOSX
#  undef TextRange
#  undef TextRangeArray
#  undef Comment
#endif

using namespace mozilla::widget;

namespace mozilla {

#define IDEOGRAPHIC_SPACE (u"\x3000"_ns)

static uint32_t GetOrCreateCompositionId(WidgetCompositionEvent* aEvent) {
  // If we're in the parent process, return new composition ID.
  if (XRE_IsParentProcess()) {
    static uint32_t sNextCompositionId = 1u;
    if (MOZ_UNLIKELY(sNextCompositionId == UINT32_MAX)) {
      sNextCompositionId = 1u;
    }
    // FYI: When we send the event to a remote process, TextComposition will
    // set aEvent->mCompositionId to this value.  Therefore, we don't need to
    // set it here.
    return sNextCompositionId++;
  }
  // If aEvent comes from the parent process, the event has composition ID
  // considered by the parent process.  Then, we should use it.
  // Otherwise, aEvent is synthesized in this process, it won't cross the
  // process boundary between this process and the parent process.  Therefore,
  // we don't need to set meaningful composition ID for the text composition.
  return aEvent->mCompositionId;
}

/******************************************************************************
 * TextComposition
 ******************************************************************************/

bool TextComposition::sHandlingSelectionEvent = false;

TextComposition::TextComposition(nsPresContext* aPresContext, nsINode* aNode,
                                 BrowserParent* aBrowserParent,
                                 WidgetCompositionEvent* aCompositionEvent)
    : mPresContext(aPresContext),
      mNode(aNode),
      mBrowserParent(aBrowserParent),
      mNativeContext(aCompositionEvent->mNativeIMEContext),
      mCompositionId(GetOrCreateCompositionId(aCompositionEvent)),
      mCompositionStartOffset(0),
      mTargetClauseOffsetInComposition(0),
      mCompositionStartOffsetInTextNode(UINT32_MAX),
      mCompositionLengthInTextNode(UINT32_MAX),
      mIsSynthesizedForTests(aCompositionEvent->mFlags.mIsSynthesizedForTests),
      mIsComposing(false),
      mIsRequestingCommit(false),
      mIsRequestingCancel(false),
      mRequestedToCommitOrCancel(false),
      mHasDispatchedDOMTextEvent(false),
      mHasReceivedCommitEvent(false),
      mWasNativeCompositionEndEventDiscarded(false),
      mAllowControlCharacters(
          StaticPrefs::dom_compositionevent_allow_control_characters()),
      mWasCompositionStringEmpty(true) {
  MOZ_ASSERT(aCompositionEvent->mNativeIMEContext.IsValid());
}

void TextComposition::Destroy() {
  mPresContext = nullptr;
  mNode = nullptr;
  mBrowserParent = nullptr;
  mContainerTextNode = nullptr;
  mCompositionStartOffsetInTextNode = UINT32_MAX;
  mCompositionLengthInTextNode = UINT32_MAX;
  // TODO: If the editor is still alive and this is held by it, we should tell
  //       this being destroyed for cleaning up the stuff.
}

void TextComposition::OnCharacterDataChanged(
    Text& aText, const CharacterDataChangeInfo& aInfo) {
  if (mContainerTextNode != &aText ||
      mCompositionStartOffsetInTextNode == UINT32_MAX ||
      mCompositionLengthInTextNode == UINT32_MAX) {
    return;
  }

  // Ignore changes after composition string.
  if (aInfo.mChangeStart >=
      mCompositionStartOffsetInTextNode + mCompositionLengthInTextNode) {
    return;
  }

  // If the change ends before the composition string, we need only to adjust
  // the start offset.
  if (aInfo.mChangeEnd <= mCompositionStartOffsetInTextNode) {
    MOZ_ASSERT(aInfo.LengthOfRemovedText() <=
               mCompositionStartOffsetInTextNode);
    mCompositionStartOffsetInTextNode -= aInfo.LengthOfRemovedText();
    mCompositionStartOffsetInTextNode += aInfo.mReplaceLength;
    return;
  }

  // If this is caused by a splitting text node, the composition string
  // may be split out to the new right node.  In the case,
  // CompositionTransaction::DoTransaction handles it with walking the
  // following text nodes.  Therefore, we should NOT shrink the composing
  // range for avoind breaking the fix of bug 1310912.  Although the handling
  // looks buggy so that we need to move the handling into here later.
  if (aInfo.mDetails &&
      aInfo.mDetails->mType == CharacterDataChangeInfo::Details::eSplit) {
    return;
  }

  // If the change removes/replaces the last character of the composition
  // string, we should shrink the composition range before the change start.
  // Then, the replace string will be never updated by coming composition
  // updates.
  if (aInfo.mChangeEnd >=
      mCompositionStartOffsetInTextNode + mCompositionLengthInTextNode) {
    // If deleting the first character of the composition string, collapse IME
    // selection temporarily.  Updating composition string will insert new
    // composition string there.
    if (aInfo.mChangeStart <= mCompositionStartOffsetInTextNode) {
      mCompositionStartOffsetInTextNode = aInfo.mChangeStart;
      mCompositionLengthInTextNode = 0u;
      return;
    }
    // If some characters in the composition still stay, composition range
    // should be shrunken.
    MOZ_ASSERT(aInfo.mChangeStart > mCompositionStartOffsetInTextNode);
    mCompositionLengthInTextNode =
        aInfo.mChangeStart - mCompositionStartOffsetInTextNode;
    return;
  }

  // If removed range starts in the composition string, we need only adjust
  // the length to make composition range contain the replace string.
  if (aInfo.mChangeStart >= mCompositionStartOffsetInTextNode) {
    if (!mCompositionLengthInTextNode) {
      // However, don't extend composition range if there is no composition
      // string.
      return;
    }
    MOZ_ASSERT(aInfo.LengthOfRemovedText() <= mCompositionLengthInTextNode);
    mCompositionLengthInTextNode -= aInfo.LengthOfRemovedText();
    mCompositionLengthInTextNode += aInfo.mReplaceLength;
    return;
  }

  // If preceding characters of the composition string is also removed, new
  // composition start will be there and new composition ends at current
  // position.
  const uint32_t removedLengthInCompositionString =
      aInfo.mChangeEnd - mCompositionStartOffsetInTextNode;
  mCompositionStartOffsetInTextNode = aInfo.mChangeStart;
  if (!mCompositionLengthInTextNode) {
    // However, don't extend composition range if there is no composition
    // string.
    return;
  }
  mCompositionLengthInTextNode -= removedLengthInCompositionString;
  mCompositionLengthInTextNode += aInfo.mReplaceLength;
}

bool TextComposition::IsValidStateForComposition(nsIWidget* aWidget) const {
  return !Destroyed() && aWidget && !aWidget->Destroyed() &&
         mPresContext->GetPresShell() &&
         !mPresContext->PresShell()->IsDestroying();
}

bool TextComposition::MaybeDispatchCompositionUpdate(
    const WidgetCompositionEvent* aCompositionEvent) {
  MOZ_RELEASE_ASSERT(!mBrowserParent);

  if (!IsValidStateForComposition(aCompositionEvent->mWidget)) {
    return false;
  }

  // Note that we don't need to dispatch eCompositionUpdate event even if
  // mHasDispatchedDOMTextEvent is false and eCompositionCommit event is
  // dispatched with empty string immediately after eCompositionStart
  // because composition string has never been changed from empty string to
  // non-empty string in such composition even if selected string was not
  // empty string (mLastData isn't set to selected text when this receives
  // eCompositionStart).
  if (mLastData == aCompositionEvent->mData) {
    // Even if the new composition event does not update the composition string,
    // it may change IME selection.
    mLastRanges = aCompositionEvent->mRanges;
    return true;
  }
  CloneAndDispatchAs(aCompositionEvent, eCompositionUpdate);
  return IsValidStateForComposition(aCompositionEvent->mWidget);
}

BaseEventFlags TextComposition::CloneAndDispatchAs(
    const WidgetCompositionEvent* aCompositionEvent, EventMessage aMessage,
    nsEventStatus* aStatus, EventDispatchingCallback* aCallBack) {
  MOZ_RELEASE_ASSERT(!mBrowserParent);

  MOZ_ASSERT(IsValidStateForComposition(aCompositionEvent->mWidget),
             "Should be called only when it's safe to dispatch an event");

  WidgetCompositionEvent compositionEvent(aCompositionEvent->IsTrusted(),
                                          aMessage, aCompositionEvent->mWidget);
  compositionEvent.mTimeStamp = aCompositionEvent->mTimeStamp;
  compositionEvent.mData = aCompositionEvent->mData;
  compositionEvent.mNativeIMEContext = aCompositionEvent->mNativeIMEContext;
  compositionEvent.mOriginalMessage = aCompositionEvent->mMessage;
  compositionEvent.mFlags.mIsSynthesizedForTests =
      aCompositionEvent->mFlags.mIsSynthesizedForTests;

  nsEventStatus dummyStatus = nsEventStatus_eConsumeNoDefault;
  nsEventStatus* status = aStatus ? aStatus : &dummyStatus;
  if (aMessage == eCompositionUpdate) {
    mLastData = compositionEvent.mData;
    mLastRanges = aCompositionEvent->mRanges;
  }

  DispatchEvent(&compositionEvent, status, aCallBack, aCompositionEvent);
  return compositionEvent.mFlags;
}

void TextComposition::DispatchEvent(
    WidgetCompositionEvent* aDispatchEvent, nsEventStatus* aStatus,
    EventDispatchingCallback* aCallBack,
    const WidgetCompositionEvent* aOriginalEvent) {
  if (aDispatchEvent->mMessage == eCompositionChange) {
    aDispatchEvent->mFlags.mOnlySystemGroupDispatchInContent = true;
  }
  RefPtr<nsINode> node = mNode;
  RefPtr<nsPresContext> presContext = mPresContext;
  if (auto* element = nsGenericHTMLElement::FromNode(node)) {
    if (RefPtr<dom::EditContext> editContext = element->GetEditContext()) {
      // Only compositionstart and compositionend are sent to EditContext
      if (aDispatchEvent->mMessage == eCompositionStart) {
        editContext->StartComposition(*aDispatchEvent);
      } else if (aDispatchEvent->mMessage == eCompositionEnd) {
        editContext->EndComposition(*aDispatchEvent);
      }
      // Internally, we want to dispatch this event to the EditContext's
      // associated element, since that is what the EditorEventListener is
      // listening to. But according to the spec, composition events should only
      // be dispatched to the EditContext.
      aDispatchEvent->mFlags.mOnlySystemGroupDispatch = true;
    }
  }
  EventDispatcher::Dispatch(node, presContext, aDispatchEvent, nullptr, aStatus,
                            aCallBack);

  OnCompositionEventDispatched(aDispatchEvent);
}

void TextComposition::OnCompositionEventDiscarded(
    WidgetCompositionEvent* aCompositionEvent) {
  // Note that this method is never called for synthesized events for emulating
  // commit or cancel composition.

  MOZ_ASSERT(aCompositionEvent->IsTrusted(),
             "Shouldn't be called with untrusted event");

  if (mBrowserParent) {
    (void)mBrowserParent->SendCompositionEvent(*aCompositionEvent,
                                               mCompositionId);
  }

  // XXX If composition events are discarded, should we dispatch them with
  //     runnable event?  However, even if we do so, it might make native IME
  //     confused due to async modification.  Especially when native IME is
  //     TSF.
  if (!aCompositionEvent->CausesDOMCompositionEndEvent()) {
    return;
  }

  mWasNativeCompositionEndEventDiscarded = true;
}

static inline bool IsControlChar(uint32_t aCharCode) {
  return aCharCode < ' ' || aCharCode == 0x7F;
}

static size_t FindFirstControlCharacter(const nsAString& aStr) {
  const char16_t* sourceBegin = aStr.BeginReading();
  const char16_t* sourceEnd = aStr.EndReading();

  for (const char16_t* source = sourceBegin; source < sourceEnd; ++source) {
    if (*source != '\t' && IsControlChar(*source)) {
      return source - sourceBegin;
    }
  }

  return -1;
}

static void RemoveControlCharactersFrom(nsAString& aStr,
                                        TextRangeArray* aRanges) {
  size_t firstControlCharOffset = FindFirstControlCharacter(aStr);
  if (firstControlCharOffset == (size_t)-1) {
    return;
  }

  nsAutoString copy(aStr);
  const char16_t* sourceBegin = copy.BeginReading();
  const char16_t* sourceEnd = copy.EndReading();

  char16_t* dest = aStr.BeginWriting();
  if (NS_WARN_IF(!dest)) {
    return;
  }

  char16_t* curDest = dest + firstControlCharOffset;
  size_t i = firstControlCharOffset;
  for (const char16_t* source = sourceBegin + firstControlCharOffset;
       source < sourceEnd; ++source) {
    if (*source == '\t' || *source == '\n' || !IsControlChar(*source)) {
      *curDest = *source;
      ++curDest;
      ++i;
    } else if (aRanges) {
      aRanges->RemoveCharacter(i);
    }
  }

  aStr.SetLength(curDest - dest);
}

nsString TextComposition::CommitStringIfCommittedAsIs() const {
  nsString result(mLastData);
  if (!mAllowControlCharacters) {
    RemoveControlCharactersFrom(result, nullptr);
  }
  if (StaticPrefs::intl_ime_remove_placeholder_character_at_commit() &&
      mLastData == IDEOGRAPHIC_SPACE) {
    return EmptyString();
  }
  return result;
}

void TextComposition::DispatchCompositionEvent(
    WidgetCompositionEvent* aCompositionEvent, nsEventStatus* aStatus,
    EventDispatchingCallback* aCallBack, bool aIsSynthesized) {
  mWasCompositionStringEmpty = mString.IsEmpty();

  if (aCompositionEvent->IsFollowedByCompositionEnd()) {
    mHasReceivedCommitEvent = true;
  }

  // If this instance has requested to commit or cancel composition but
  // is not synthesizing commit event, that means that the IME commits or
  // cancels the composition asynchronously.  Typically, iBus behaves so.
  // Then, synthesized events which were dispatched immediately after
  // the request has already committed our editor's composition string and
  // told it to web apps.  Therefore, we should ignore the delayed events.
  if (mRequestedToCommitOrCancel && !aIsSynthesized) {
    *aStatus = nsEventStatus_eConsumeNoDefault;
    // If this composition is handled in a content process, we need to send the
    // delayed commit event to the remote process because the content process's
    // TextEventDispatcher needs to manage its composing state with the event.
    if (!mBrowserParent) {
      return;
    }
  }

  // If the content is a container of BrowserParent, composition should be in
  // the remote process.
  if (mBrowserParent) {
    (void)mBrowserParent->SendCompositionEvent(*aCompositionEvent,
                                               mCompositionId);
    aCompositionEvent->StopPropagation();
    if (aCompositionEvent->CausesDOMTextEvent()) {
      mLastData = aCompositionEvent->mData;
      mLastRanges = aCompositionEvent->mRanges;
      // Although, the composition event hasn't been actually handled yet,
      // emulate an editor to be handling the composition event.
      EditorWillHandleCompositionChangeEvent(aCompositionEvent);
      EditorDidHandleCompositionChangeEvent();
    }
    return;
  }

  if (!mAllowControlCharacters) {
    RemoveControlCharactersFrom(aCompositionEvent->mData,
                                aCompositionEvent->mRanges);
  }
  if (aCompositionEvent->mMessage == eCompositionCommitAsIs) {
    NS_ASSERTION(!aCompositionEvent->mRanges,
                 "mRanges of eCompositionCommitAsIs should be null");
    aCompositionEvent->mRanges = nullptr;
    NS_ASSERTION(aCompositionEvent->mData.IsEmpty(),
                 "mData of eCompositionCommitAsIs should be empty string");
    if (StaticPrefs::intl_ime_remove_placeholder_character_at_commit() &&
        mLastData == IDEOGRAPHIC_SPACE) {
      // If the last data is an ideographic space (FullWidth space), it might be
      // a placeholder character of some Chinese IME.  So, committing with
      // this data might not be expected by users.  Let's use empty string.
      aCompositionEvent->mData.Truncate();
    } else {
      aCompositionEvent->mData = mLastData;
    }
  } else if (aCompositionEvent->mMessage == eCompositionCommit) {
    NS_ASSERTION(!aCompositionEvent->mRanges,
                 "mRanges of eCompositionCommit should be null");
    aCompositionEvent->mRanges = nullptr;
  }

  if (!IsValidStateForComposition(aCompositionEvent->mWidget)) {
    *aStatus = nsEventStatus_eConsumeNoDefault;
    return;
  }

  // IME may commit composition with empty string for a commit request or
  // with non-empty string for a cancel request.  We should prevent such
  // unexpected result.  E.g., web apps may be confused if they implement
  // autocomplete which attempts to commit composition forcibly when the user
  // selects one of suggestions but composition string is cleared by IME.
  // Note that most Chinese IMEs don't expose actual composition string to us.
  // They typically tell us an IDEOGRAPHIC SPACE or empty string as composition
  // string.  Therefore, we should hack it only when:
  // 1. committing string is empty string at requesting commit but the last
  //    data isn't IDEOGRAPHIC SPACE.
  // 2. non-empty string is committed at requesting cancel.
  if (!aIsSynthesized && (mIsRequestingCommit || mIsRequestingCancel)) {
    nsString* committingData = nullptr;
    switch (aCompositionEvent->mMessage) {
      case eCompositionEnd:
      case eCompositionChange:
      case eCompositionCommitAsIs:
      case eCompositionCommit:
        committingData = &aCompositionEvent->mData;
        break;
      default:
        NS_WARNING(
            "Unexpected event comes during committing or "
            "canceling composition");
        break;
    }
    if (committingData) {
      if (mIsRequestingCommit && committingData->IsEmpty() &&
          mLastData != IDEOGRAPHIC_SPACE) {
        committingData->Assign(mLastData);
      } else if (mIsRequestingCancel && !committingData->IsEmpty()) {
        committingData->Truncate();
      }
    }
  }

  bool dispatchEvent = true;
  bool dispatchDOMTextEvent = aCompositionEvent->CausesDOMTextEvent();

  // When mIsComposing is false but the committing string is different from
  // the last data (E.g., previous eCompositionChange event made the
  // composition string empty or didn't have clause information), we don't
  // need to dispatch redundant DOM text event.  (But note that we need to
  // dispatch eCompositionChange event if we have not dispatched
  // eCompositionChange event yet and commit string replaces selected string
  // with empty string since selected string hasn't been replaced with empty
  // string yet.)
  if (dispatchDOMTextEvent &&
      aCompositionEvent->mMessage != eCompositionChange && !mIsComposing &&
      mHasDispatchedDOMTextEvent && mLastData == aCompositionEvent->mData) {
    dispatchEvent = dispatchDOMTextEvent = false;
  }

  // widget may dispatch redundant eCompositionChange event
  // which modifies neither composition string, clauses nor caret
  // position.  In such case, we shouldn't dispatch DOM events.
  if (dispatchDOMTextEvent &&
      aCompositionEvent->mMessage == eCompositionChange &&
      mLastData == aCompositionEvent->mData && mRanges &&
      aCompositionEvent->mRanges &&
      mRanges->Equals(*aCompositionEvent->mRanges)) {
    dispatchEvent = dispatchDOMTextEvent = false;
  }

  if (dispatchDOMTextEvent) {
    if (!MaybeDispatchCompositionUpdate(aCompositionEvent)) {
      return;
    }
  }

  if (dispatchEvent) {
    // If the composition event should cause a DOM text event, we should
    // overwrite the event message as eCompositionChange because due to
    // the limitation of mapping between event messages and DOM event types,
    // we cannot map multiple event messages to a DOM event type.
    if (dispatchDOMTextEvent &&
        aCompositionEvent->mMessage != eCompositionChange) {
      mHasDispatchedDOMTextEvent = true;
      aCompositionEvent->mFlags = CloneAndDispatchAs(
          aCompositionEvent, eCompositionChange, aStatus, aCallBack);
    } else {
      if (aCompositionEvent->mMessage == eCompositionChange) {
        mHasDispatchedDOMTextEvent = true;
      }
      DispatchEvent(aCompositionEvent, aStatus, aCallBack);
    }
  } else {
    *aStatus = nsEventStatus_eConsumeNoDefault;
  }

  if (!IsValidStateForComposition(aCompositionEvent->mWidget)) {
    return;
  }

  // Emulate editor behavior of compositionchange event (DOM text event) handler
  // if no editor handles composition events.
  if (dispatchDOMTextEvent && !HasEditor()) {
    EditorWillHandleCompositionChangeEvent(aCompositionEvent);
    EditorDidHandleCompositionChangeEvent();
  }

  if (aCompositionEvent->CausesDOMCompositionEndEvent()) {
    // Dispatch a compositionend event if it's necessary.
    if (aCompositionEvent->mMessage != eCompositionEnd) {
      CloneAndDispatchAs(aCompositionEvent, eCompositionEnd);
    }
    MOZ_ASSERT(!mIsComposing, "Why is the editor still composing?");
    MOZ_ASSERT(!HasEditor(), "Why does the editor still keep to hold this?");
  }

  MaybeNotifyIMEOfCompositionEventHandled(aCompositionEvent);
}

// static
void TextComposition::HandleSelectionEvent(
    nsPresContext* aPresContext, BrowserParent* aBrowserParent,
    WidgetSelectionEvent* aSelectionEvent) {
  // If the content is a container of BrowserParent, composition should be in
  // the remote process.
  if (aBrowserParent) {
    (void)aBrowserParent->SendSelectionEvent(*aSelectionEvent);
    aSelectionEvent->StopPropagation();
    return;
  }

  AutoRestore<bool> saveHandlingSelectionEvent(sHandlingSelectionEvent);
  sHandlingSelectionEvent = true;

  if (RefPtr<IMEContentObserver> contentObserver =
          IMEStateManager::GetActiveContentObserver()) {
    contentObserver->MaybeHandleSelectionEvent(aPresContext, aSelectionEvent);
    return;
  }

  ContentEventHandler handler(aPresContext);
  // XXX During setting selection, a selection listener may change selection
  //     again.  In such case, sHandlingSelectionEvent doesn't indicate if
  //     the selection change is caused by a selection event.  However, it
  //     must be non-realistic scenario.
  handler.OnSelectionEvent(aSelectionEvent);
}

uint32_t TextComposition::GetSelectionStartOffset() {
  nsCOMPtr<nsIWidget> widget = mPresContext->GetRootWidget();
  WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                 widget);
  // Due to a bug of widget, mRanges may not be nullptr even though composition
  // string is empty.  So, we need to check it here for avoiding to return
  // odd start offset.
  if (!mLastData.IsEmpty() && mRanges && mRanges->HasClauses()) {
    querySelectedTextEvent.InitForQuerySelectedText(
        ToSelectionType(mRanges->GetFirstClause()->mRangeType));
  } else {
    NS_WARNING_ASSERTION(
        !mLastData.IsEmpty() || !mRanges || !mRanges->HasClauses(),
        "Shouldn't have empty clause info when composition string is empty");
    querySelectedTextEvent.InitForQuerySelectedText(SelectionType::eNormal);
  }

  // The editor which has this composition is observed by active
  // IMEContentObserver, we can use the cache of it.
  RefPtr<IMEContentObserver> contentObserver =
      IMEStateManager::GetActiveContentObserver();
  bool doQuerySelection = true;
  if (contentObserver) {
    if (contentObserver->IsObserving(*this)) {
      doQuerySelection = false;
      contentObserver->HandleQueryContentEvent(&querySelectedTextEvent);
    }
    // If another editor already has focus, we cannot retrieve selection
    // in the editor which has this composition...
    else if (NS_WARN_IF(contentObserver->GetPresContext() == mPresContext)) {
      return 0;  // XXX Is this okay?
    }
  }

  // Otherwise, using slow path (i.e., compute every time with
  // ContentEventHandler)
  if (doQuerySelection) {
    ContentEventHandler handler(mPresContext);
    handler.HandleQueryContentEvent(&querySelectedTextEvent);
  }

  if (NS_WARN_IF(querySelectedTextEvent.DidNotFindSelection())) {
    return 0;  // XXX Is this okay?
  }
  return querySelectedTextEvent.mReply->AnchorOffset();
}

void TextComposition::OnCompositionEventDispatched(
    const WidgetCompositionEvent* aCompositionEvent) {
  MOZ_RELEASE_ASSERT(!mBrowserParent);

  if (!IsValidStateForComposition(aCompositionEvent->mWidget)) {
    return;
  }

  // Every composition event may cause changing composition start offset,
  // especially when there is no composition string.  Therefore, we need to
  // update mCompositionStartOffset with the latest offset.

  MOZ_ASSERT(aCompositionEvent->mMessage != eCompositionStart ||
                 mWasCompositionStringEmpty,
             "mWasCompositionStringEmpty should be true if the dispatched "
             "event is eCompositionStart");

  if (mWasCompositionStringEmpty &&
      !aCompositionEvent->CausesDOMCompositionEndEvent()) {
    // If there was no composition string, current selection start may be the
    // offset for inserting composition string.
    // Update composition start offset with current selection start.
    mCompositionStartOffset = GetSelectionStartOffset();
    mTargetClauseOffsetInComposition = 0;
  }

  if (aCompositionEvent->CausesDOMTextEvent()) {
    mTargetClauseOffsetInComposition = aCompositionEvent->TargetClauseOffset();
  }
}

void TextComposition::OnStartOffsetUpdatedInChild(uint32_t aStartOffset) {
  mCompositionStartOffset = aStartOffset;
}

void TextComposition::MaybeNotifyIMEOfCompositionEventHandled(
    const WidgetCompositionEvent* aCompositionEvent) {
  if (aCompositionEvent->mMessage != eCompositionStart &&
      !aCompositionEvent->CausesDOMTextEvent()) {
    return;
  }

  RefPtr<IMEContentObserver> contentObserver =
      IMEStateManager::GetActiveContentObserver();
  // When IMEContentObserver is managing the editor which has this composition,
  // composition event handled notification should be sent after the observer
  // notifies all pending notifications.  Therefore, we should use it.
  // XXX If IMEContentObserver suddenly loses focus after here and notifying
  //     widget of pending notifications, we won't notify widget of composition
  //     event handled.  Although, this is a bug but it should be okay since
  //     destroying IMEContentObserver notifies IME of blur.  So, native IME
  //     handler can treat it as this notification too.
  if (contentObserver && contentObserver->IsObserving(*this)) {
    contentObserver->MaybeNotifyCompositionEventHandled(
        aCompositionEvent->mMessage);
    return;
  }
  // Otherwise, e.g., this composition is in non-active window, we should
  // notify widget directly.
  NotifyIME(NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED);
}

void TextComposition::DispatchCompositionEventRunnable(
    EventMessage aEventMessage, const nsAString& aData,
    bool aIsSynthesizingCommit) {
  nsContentUtils::AddScriptRunner(MakeAndAddRef<CompositionEventDispatcher>(
      this, mNode, aEventMessage, aData, aIsSynthesizingCommit));
}

nsresult TextComposition::RequestToCommit(nsIWidget* aWidget, bool aDiscard) {
  MOZ_ASSERT(this == IMEStateManager::GetTextCompositionFor(aWidget));
  // If this composition is already requested to be committed or canceled,
  // or has already finished in IME, we don't need to request it again because
  // request from this instance shouldn't cause committing nor canceling current
  // composition in IME, and even if the first request failed, new request
  // won't success, probably.  And we shouldn't synthesize events for
  // committing or canceling composition twice or more times.
  if (!CanRequsetIMEToCommitOrCancelComposition()) {
    return NS_OK;
  }

  RefPtr<TextComposition> kungFuDeathGrip(this);
  const nsAutoString lastData(mLastData);

  if (IMEStateManager::CanSendNotificationToWidget()) {
    AutoRestore<bool> saveRequestingCancel(mIsRequestingCancel);
    AutoRestore<bool> saveRequestingCommit(mIsRequestingCommit);
    if (aDiscard) {
      mIsRequestingCancel = true;
      mIsRequestingCommit = false;
    } else {
      mIsRequestingCancel = false;
      mIsRequestingCommit = true;
    }
    // Request native IME to commit or cancel composition synchronously. Note
    // that even if we're in a content process, we use synchronous IPC to
    // request it. Therefore, if native IME handles the request synchronously,
    // DispatchCompositionEvent() is called with eCompositionCommit or
    // eCompositionCommitAsIs with setting aIsSynthesized to `false` during this
    // call.
    //
    // If the native IME does not handle the request synchronously, the
    // corresponding commit event must come later.
    //
    // FYI: CompositionEvents caused by a call of NotifyIME() may be
    //      discarded by PresShell if it's not safe to dispatch the event.
    nsresult rv = aWidget->NotifyIME(
        IMENotification(aDiscard ? REQUEST_TO_CANCEL_COMPOSITION
                                 : REQUEST_TO_COMMIT_COMPOSITION));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  mRequestedToCommitOrCancel = true;

  // If the request is handled by IME synchronously, this has already been
  // destroyed so that we do nothing anymore.
  if (Destroyed()) {
    return NS_OK;
  }

  // If the composition is handled in a content process and the IME does not
  // commit composition synchronously, we should wait the commit event from IME
  // and we need to send it later.
  if (mBrowserParent) {
    return NS_OK;
  }

  // Otherwise, synthesize the commit in content because most web apps must
  // assume that committing composition is handled synchronously. This will
  // call DispatchCompositionEvent() with eCompositionCommit or
  // eCompositionCommitAsIs event with setting aIsSynthesized to `true` before
  // receiving eCompositionCommit or eCompositionCommitAsIs coming from native
  // IME handler.
  nsAutoString data(aDiscard ? EmptyString() : lastData);
  if (data == mLastData) {
    DispatchCompositionEventRunnable(eCompositionCommitAsIs, u""_ns, true);
  } else {
    DispatchCompositionEventRunnable(eCompositionCommit, data, true);
  }
  return NS_OK;
}

nsresult TextComposition::NotifyIME(IMEMessage aMessage) {
  NS_ENSURE_TRUE(mPresContext, NS_ERROR_NOT_AVAILABLE);
  return IMEStateManager::NotifyIME(aMessage, mPresContext, mBrowserParent);
}

void TextComposition::EditorWillHandleCompositionChangeEvent(
    const WidgetCompositionEvent* aCompositionChangeEvent) {
  mIsComposing = aCompositionChangeEvent->IsComposing();
  mRanges = aCompositionChangeEvent->mRanges;
  mEditorIsHandlingEvent = true;

  MOZ_ASSERT(
      mLastData == aCompositionChangeEvent->mData,
      "The text of a compositionchange event must be same as previous data "
      "attribute value of the latest compositionupdate event");
}

void TextComposition::OnEditorDestroyed() {
  MOZ_RELEASE_ASSERT(!mBrowserParent);

  MOZ_ASSERT(!mEditorIsHandlingEvent,
             "The editor should have stopped listening events");
  nsCOMPtr<nsIWidget> widget = GetWidget();
  if (NS_WARN_IF(!widget)) {
    // XXX If this could happen, how do we notify IME of destroying the editor?
    return;
  }

  // Try to cancel the composition.
  RequestToCommit(widget, true);
}

void TextComposition::EditorDidHandleCompositionChangeEvent() {
  mString = mLastData;
  mEditorIsHandlingEvent = false;
}

void TextComposition::StartHandlingComposition(EditorBase* aEditorBase) {
  MOZ_RELEASE_ASSERT(!mBrowserParent);

  MOZ_ASSERT(!HasEditor(), "There is a handling editor already");
  mEditorBaseWeak = do_GetWeakReference(static_cast<nsIEditor*>(aEditorBase));
}

void TextComposition::EndHandlingComposition(EditorBase* aEditorBase) {
  MOZ_RELEASE_ASSERT(!mBrowserParent);

#ifdef DEBUG
  RefPtr<EditorBase> editorBase = GetEditorBase();
  MOZ_ASSERT(!editorBase || editorBase == aEditorBase,
             "Another editor handled the composition?");
#endif  // #ifdef DEBUG
  mEditorBaseWeak = nullptr;
}

already_AddRefed<EditorBase> TextComposition::GetEditorBase() const {
  nsCOMPtr<nsIEditor> editor = do_QueryReferent(mEditorBaseWeak);
  RefPtr<EditorBase> editorBase = static_cast<EditorBase*>(editor.get());
  return editorBase.forget();
}

bool TextComposition::HasEditor() const {
  return mEditorBaseWeak && mEditorBaseWeak->IsAlive();
}

RawRangeBoundary TextComposition::FirstIMESelectionStartRef() const {
  RefPtr<EditorBase> editorBase = GetEditorBase();
  if (!editorBase) {
    return RawRangeBoundary();
  }

  nsISelectionController* selectionController =
      editorBase->GetSelectionController();
  if (NS_WARN_IF(!selectionController)) {
    return RawRangeBoundary();
  }

  const nsRange* firstRange = nullptr;
  static const SelectionType kIMESelectionTypes[] = {
      SelectionType::eIMERawClause, SelectionType::eIMESelectedRawClause,
      SelectionType::eIMEConvertedClause, SelectionType::eIMESelectedClause};
  for (auto selectionType : kIMESelectionTypes) {
    dom::Selection* selection =
        selectionController->GetSelection(ToRawSelectionType(selectionType));
    if (!selection) {
      continue;
    }
    const uint32_t rangeCount = selection->RangeCount();
    for (const uint32_t i : IntegerRange(rangeCount)) {
      MOZ_ASSERT(selection->RangeCount() == rangeCount);
      const nsRange* range = selection->GetRangeAt(i);
      MOZ_ASSERT(range);
      if (MOZ_UNLIKELY(NS_WARN_IF(!range)) ||
          MOZ_UNLIKELY(NS_WARN_IF(!range->GetStartContainer()))) {
        continue;
      }
      if (!firstRange) {
        firstRange = range;
        continue;
      }
      // In most cases, all composition string should be in same text node.
      if (firstRange->GetStartContainer() == range->GetStartContainer()) {
        if (firstRange->StartOffset() > range->StartOffset()) {
          firstRange = range;
        }
        continue;
      }
      // However, if web apps have inserted different nodes in composition
      // string, composition string may span 2 or more nodes.
      if (firstRange->GetStartContainer()->GetNextSibling() ==
          range->GetStartContainer()) {
        // Fast path for some known applications like Google Keep.
        firstRange = range;
        continue;
      }
      // Unfortunately, really slow path.
      // The ranges should always have a common ancestor, hence, be comparable.
      // XXX Should use TreeKind::DOM? Editable state won't cross shadow DOM
      // boundaries.
      if (*nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
              range->StartRef(), firstRange->StartRef()) == -1) {
        firstRange = range;
      }
    }
  }
  return firstRange ? firstRange->StartRef().AsRaw() : RawRangeBoundary();
}

RawRangeBoundary TextComposition::LastIMESelectionEndRef() const {
  RefPtr<EditorBase> editorBase = GetEditorBase();
  if (!editorBase) {
    return RawRangeBoundary();
  }

  nsISelectionController* selectionController =
      editorBase->GetSelectionController();
  if (NS_WARN_IF(!selectionController)) {
    return RawRangeBoundary();
  }

  const nsRange* lastRange = nullptr;
  static const SelectionType kIMESelectionTypes[] = {
      SelectionType::eIMERawClause, SelectionType::eIMESelectedRawClause,
      SelectionType::eIMEConvertedClause, SelectionType::eIMESelectedClause};
  for (auto selectionType : kIMESelectionTypes) {
    dom::Selection* selection =
        selectionController->GetSelection(ToRawSelectionType(selectionType));
    if (!selection) {
      continue;
    }
    const uint32_t rangeCount = selection->RangeCount();
    for (const uint32_t i : IntegerRange(rangeCount)) {
      MOZ_ASSERT(selection->RangeCount() == rangeCount);
      const nsRange* range = selection->GetRangeAt(i);
      MOZ_ASSERT(range);
      if (MOZ_UNLIKELY(NS_WARN_IF(!range)) ||
          MOZ_UNLIKELY(NS_WARN_IF(!range->GetEndContainer()))) {
        continue;
      }
      if (!lastRange) {
        lastRange = range;
        continue;
      }
      // In most cases, all composition string should be in same text node.
      if (lastRange->GetEndContainer() == range->GetEndContainer()) {
        if (lastRange->EndOffset() < range->EndOffset()) {
          lastRange = range;
        }
        continue;
      }
      // However, if web apps have inserted different nodes in composition
      // string, composition string may span 2 or more nodes.
      if (lastRange->GetEndContainer() ==
          range->GetEndContainer()->GetNextSibling()) {
        // Fast path for some known applications like Google Keep.
        lastRange = range;
        continue;
      }
      // Unfortunately, really slow path.
      // The ranges should always have a common ancestor, hence, be comparable.
      // XXX Should use TreeKind::DOM? Editable state won't cross shadow DOM
      // boundaries.
      if (*nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
              lastRange->EndRef(), range->EndRef()) == -1) {
        lastRange = range;
      }
    }
  }
  return lastRange ? lastRange->EndRef().AsRaw() : RawRangeBoundary();
}

/******************************************************************************
 * TextComposition::CompositionEventDispatcher
 ******************************************************************************/

TextComposition::CompositionEventDispatcher::CompositionEventDispatcher(
    TextComposition* aTextComposition, nsINode* aEventTarget,
    EventMessage aEventMessage, const nsAString& aData,
    bool aIsSynthesizedEvent)
    : Runnable("TextComposition::CompositionEventDispatcher"),
      mTextComposition(aTextComposition),
      mEventTarget(aEventTarget),
      mData(aData),
      mEventMessage(aEventMessage),
      mIsSynthesizedEvent(aIsSynthesizedEvent) {}

NS_IMETHODIMP
TextComposition::CompositionEventDispatcher::Run() {
  // The widget can be different from the widget which has dispatched
  // composition events because GetWidget() returns a widget which is proper
  // for calling NotifyIME().  However, this must no be problem since both
  // widget should share native IME context.  Therefore, even if an event
  // handler uses the widget for requesting IME to commit or cancel, it works.
  nsCOMPtr<nsIWidget> widget(mTextComposition->GetWidget());
  if (!mTextComposition->IsValidStateForComposition(widget)) {
    return NS_OK;  // cannot dispatch any events anymore
  }

  RefPtr<nsPresContext> presContext = mTextComposition->mPresContext;
  nsCOMPtr<nsINode> eventTarget = mEventTarget;
  RefPtr<BrowserParent> browserParent = mTextComposition->mBrowserParent;
  nsEventStatus status = nsEventStatus_eIgnore;
  switch (mEventMessage) {
    case eCompositionStart: {
      WidgetCompositionEvent compStart(true, eCompositionStart, widget);
      compStart.mNativeIMEContext = mTextComposition->mNativeContext;
      WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                     widget);
      ContentEventHandler handler(presContext);
      handler.OnQuerySelectedText(&querySelectedTextEvent);
      NS_ASSERTION(querySelectedTextEvent.Succeeded(),
                   "Failed to get selected text");
      if (querySelectedTextEvent.FoundSelection()) {
        compStart.mData = querySelectedTextEvent.mReply->DataRef();
      }
      compStart.mFlags.mIsSynthesizedForTests =
          mTextComposition->IsSynthesizedForTests();
      IMEStateManager::DispatchCompositionEvent(
          eventTarget, presContext, browserParent, &compStart, &status, nullptr,
          mIsSynthesizedEvent);
      break;
    }
    case eCompositionChange:
    case eCompositionCommitAsIs:
    case eCompositionCommit: {
      WidgetCompositionEvent compEvent(true, mEventMessage, widget);
      compEvent.mNativeIMEContext = mTextComposition->mNativeContext;
      if (mEventMessage != eCompositionCommitAsIs) {
        compEvent.mData = mData;
      }
      compEvent.mFlags.mIsSynthesizedForTests =
          mTextComposition->IsSynthesizedForTests();
      IMEStateManager::DispatchCompositionEvent(
          eventTarget, presContext, browserParent, &compEvent, &status, nullptr,
          mIsSynthesizedEvent);
      break;
    }
    default:
      MOZ_CRASH("Unsupported event");
  }
  return NS_OK;
}

/******************************************************************************
 * TextCompositionArray
 ******************************************************************************/

TextCompositionArray::index_type TextCompositionArray::IndexOf(
    const NativeIMEContext& aNativeIMEContext) {
  if (!aNativeIMEContext.IsValid()) {
    return NoIndex;
  }
  for (index_type i = Length(); i > 0; --i) {
    if (ElementAt(i - 1)->GetNativeIMEContext() == aNativeIMEContext) {
      return i - 1;
    }
  }
  return NoIndex;
}

TextCompositionArray::index_type TextCompositionArray::IndexOf(
    nsIWidget* aWidget) {
  return IndexOf(aWidget->GetNativeIMEContext());
}

TextCompositionArray::index_type TextCompositionArray::IndexOf(
    nsPresContext* aPresContext) {
  for (index_type i = Length(); i > 0; --i) {
    if (ElementAt(i - 1)->GetPresContext() == aPresContext) {
      return i - 1;
    }
  }
  return NoIndex;
}

TextCompositionArray::index_type TextCompositionArray::IndexOf(
    nsPresContext* aPresContext, nsINode* aNode) {
  index_type index = IndexOf(aPresContext);
  if (index == NoIndex) {
    return NoIndex;
  }
  nsINode* node = ElementAt(index)->GetEventTargetNode();
  return node == aNode ? index : NoIndex;
}

TextComposition* TextCompositionArray::GetCompositionFor(nsIWidget* aWidget) {
  index_type i = IndexOf(aWidget);
  if (i == NoIndex) {
    return nullptr;
  }
  return ElementAt(i);
}

TextComposition* TextCompositionArray::GetCompositionFor(
    const WidgetCompositionEvent* aCompositionEvent) {
  index_type i = IndexOf(aCompositionEvent->mNativeIMEContext);
  if (i == NoIndex) {
    return nullptr;
  }
  return ElementAt(i);
}

TextComposition* TextCompositionArray::GetCompositionFor(
    nsPresContext* aPresContext) {
  index_type i = IndexOf(aPresContext);
  if (i == NoIndex) {
    return nullptr;
  }
  return ElementAt(i);
}

TextComposition* TextCompositionArray::GetCompositionFor(
    nsPresContext* aPresContext, nsINode* aNode) {
  index_type i = IndexOf(aPresContext, aNode);
  if (i == NoIndex) {
    return nullptr;
  }
  return ElementAt(i);
}

TextComposition* TextCompositionArray::GetCompositionInContent(
    nsPresContext* aPresContext, nsIContent* aContent) {
  // There should be only one composition per content object.
  for (TextComposition* const composition : Reversed(*this)) {
    nsINode* node = composition->GetEventTargetNode();
    if (node && node->IsInclusiveFlatTreeDescendantOf(aContent)) {
      return composition;
    }
  }
  return nullptr;
}

}  // namespace mozilla
