/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This class is called by the editor to handle spellchecking after various
 * events. The main entrypoint is SpellCheckAfterEditorChange, which is called
 * when the text is changed.
 *
 * It is VERY IMPORTANT that we do NOT do any operations that might cause DOM
 * notifications to be flushed when we are called from the editor. This is
 * because the call might originate from a frame, and flushing the
 * notifications might cause that frame to be deleted.
 *
 * We post an event and do all of the spellchecking in that event handler.
 * We store all DOM pointers in ranges because they are kept up-to-date with
 * DOM changes that may have happened while the event was on the queue.
 *
 * We also allow the spellcheck to be suspended and resumed later. This makes
 * large pastes or initializations with a lot of text not hang the browser UI.
 *
 * An optimization is the mNeedsCheckAfterNavigation flag. This is set to
 * true when we get any change, and false once there is no possibility
 * something changed that we need to check on navigation. Navigation events
 * tend to be a little tricky because we want to check the current word on
 * exit if something has changed. If we navigate inside the word, we don't want
 * to do anything. As a result, this flag is cleared in FinishNavigationEvent
 * when we know that we are checking as a result of navigation.
 */

#include "mozInlineSpellChecker.h"

#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/EditAction.h"
#include "mozilla/EditorBase.h"
#include "mozilla/EditorDOMPoint.h"
#include "mozilla/EditorSpellCheck.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Logging.h"
#include "mozilla/RangeUtils.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "mozilla/TextEvents.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/KeyboardEvent.h"
#include "mozilla/dom/KeyboardEventBinding.h"
#include "mozilla/dom/MouseEvent.h"
#include "mozilla/dom/NodeList.h"
#include "mozilla/dom/Selection.h"
#include "mozInlineSpellWordUtil.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsGenericHTMLElement.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsIRunnable.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsThreadUtils.h"
#include "nsUnicharUtils.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsContentUtils.h"
#include "nsIObserverService.h"
#include "prtime.h"

using mozilla::LogLevel;
using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::ipc;

// the number of milliseconds that we will take at once to do spellchecking
#define INLINESPELL_CHECK_TIMEOUT 1

// The number of words to check before we look at the time to see if
// INLINESPELL_CHECK_TIMEOUT ms have elapsed. This prevents us from getting
// stuck and not moving forward because the INLINESPELL_CHECK_TIMEOUT might
// be too short to a low-end machine.
#define INLINESPELL_MINIMUM_WORDS_BEFORE_TIMEOUT 5

// The maximum number of words to check word via IPC.
#define INLINESPELL_MAXIMUM_CHUNKED_WORDS_PER_TASK 25

// These notifications are broadcast when spell check starts and ends.  STARTED
// must always be followed by ENDED.
#define INLINESPELL_STARTED_TOPIC "inlineSpellChecker-spellCheck-started"
#define INLINESPELL_ENDED_TOPIC "inlineSpellChecker-spellCheck-ended"

static mozilla::LazyLogModule sInlineSpellCheckerLog("InlineSpellChecker");

static const PRTime kMaxSpellCheckTimeInUsec =
    INLINESPELL_CHECK_TIMEOUT * PR_USEC_PER_MSEC;

mozInlineSpellStatus::mozInlineSpellStatus(
    mozInlineSpellChecker* aSpellChecker, const Operation aOp,
    RefPtr<dom::Range>&& aRange, RefPtr<dom::Range>&& aCreatedRange,
    RefPtr<dom::Range>&& aAnchorRange, const bool aForceNavigationWordCheck,
    const int32_t aNewNavigationPositionOffset,
    SetAnchorToCaret aSetAnchorToCaret)
    : mSpellChecker(aSpellChecker),
      mRange(std::move(aRange)),
      mOp(aOp),
      mCreatedRange(std::move(aCreatedRange)),
      mAnchorRange(std::move(aAnchorRange)),
      mForceNavigationWordCheck(aForceNavigationWordCheck),
      mSetAnchorToCaret(aSetAnchorToCaret),
      mNewNavigationPositionOffset(aNewNavigationPositionOffset) {}

// mozInlineSpellStatus::CreateForEditorChange
//
//    This is the most complicated case. For changes, we need to compute the
//    range of stuff that changed based on the old and new caret positions,
//    as well as use a range possibly provided by the editor (start and end,
//    which are usually nullptr) to get a range with the union of these.

// static
Result<UniquePtr<mozInlineSpellStatus>, nsresult>
mozInlineSpellStatus::CreateForEditorChange(
    mozInlineSpellChecker& aSpellChecker, const EditSubAction aEditSubAction,
    nsINode* aAnchorNode, uint32_t aAnchorOffset, nsINode* aPreviousNode,
    uint32_t aPreviousOffset, nsINode* aStartNode, uint32_t aStartOffset,
    nsINode* aEndNode, uint32_t aEndOffset) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Verbose, ("%s", __FUNCTION__));

  if (NS_WARN_IF(!aAnchorNode) || NS_WARN_IF(!aPreviousNode)) {
    return Err(NS_ERROR_FAILURE);
  }

  bool deleted = aEditSubAction == EditSubAction::eDeleteSelectedContent;
  if (aEditSubAction == EditSubAction::eInsertTextComingFromIME) {
    // IME may remove the previous node if it cancels composition when
    // there is no text around the composition.
    deleted = !aPreviousNode->IsInComposedDoc();
  }

  // save the anchor point as a range so we can find the current word later
  RefPtr<dom::Range> anchorRange =
      mozInlineSpellStatus::PositionToCollapsedRange(aAnchorNode,
                                                     aAnchorOffset);
  if (NS_WARN_IF(!anchorRange)) {
    return Err(NS_ERROR_FAILURE);
  }

  // Deletes are easy, the range is just the current anchor. We set the range
  // to check to be empty, FinishInitOnEvent will fill in the range to be
  // the current word.
  RefPtr<dom::Range> range =
      deleted ? nullptr : dom::Range::Create(aPreviousNode);

  // On insert save this range: DoSpellCheck optimizes things in this range.
  // Otherwise, just leave this nullptr.
  RefPtr<dom::Range> createdRange =
      (aEditSubAction == EditSubAction::eInsertText) ? range : nullptr;

  UniquePtr<mozInlineSpellStatus> status{
      /* The constructor is `private`, hence the explicit allocation. */
      new mozInlineSpellStatus{&aSpellChecker,
                               deleted ? eOpChangeDelete : eOpChange,
                               std::move(range), std::move(createdRange),
                               std::move(anchorRange), false, 0}};
  if (deleted) {
    return status;
  }

  // ...we need to put the start and end in the correct order
  ErrorResult errorResult;
  int16_t cmpResult = status->mAnchorRange->ComparePoint(
      *aPreviousNode, aPreviousOffset, errorResult);
  if (NS_WARN_IF(errorResult.Failed())) {
    return Err(errorResult.StealNSResult());
  }
  nsresult rv;
  if (cmpResult < 0) {
    // previous anchor node is before the current anchor
    rv = status->mRange->SetStartAndEnd(aPreviousNode, aPreviousOffset,
                                        aAnchorNode, aAnchorOffset);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
  } else {
    // previous anchor node is after (or the same as) the current anchor
    rv = status->mRange->SetStartAndEnd(aAnchorNode, aAnchorOffset,
                                        aPreviousNode, aPreviousOffset);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
  }

  // if we were given a range, we need to expand our range to encompass it
  if (aStartNode && aEndNode) {
    cmpResult =
        status->mRange->ComparePoint(*aStartNode, aStartOffset, errorResult);
    if (NS_WARN_IF(errorResult.Failed())) {
      return Err(errorResult.StealNSResult());
    }
    if (cmpResult < 0) {  // given range starts before
      rv = status->mRange->SetStart(aStartNode, aStartOffset);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return Err(rv);
      }
    }

    cmpResult =
        status->mRange->ComparePoint(*aEndNode, aEndOffset, errorResult);
    if (NS_WARN_IF(errorResult.Failed())) {
      return Err(errorResult.StealNSResult());
    }
    if (cmpResult > 0) {  // given range ends after
      rv = status->mRange->SetEnd(aEndNode, aEndOffset);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return Err(rv);
      }
    }
  }

  return status;
}

// mozInlineSpellStatus::CreateForNavigation
//
//    For navigation events, we just need to store the new and old positions.
//
//    In some cases, we detect that we shouldn't check. If this event should
//    not be processed, *aContinue will be false.

// static
Result<UniquePtr<mozInlineSpellStatus>, nsresult>
mozInlineSpellStatus::CreateForNavigation(
    mozInlineSpellChecker& aSpellChecker, bool aForceCheck,
    int32_t aNewPositionOffset, nsINode* aOldAnchorNode,
    uint32_t aOldAnchorOffset, nsINode* aNewAnchorNode,
    uint32_t aNewAnchorOffset, bool* aContinue) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Verbose, ("%s", __FUNCTION__));

  RefPtr<dom::Range> anchorRange =
      mozInlineSpellStatus::PositionToCollapsedRange(aNewAnchorNode,
                                                     aNewAnchorOffset);
  if (NS_WARN_IF(!anchorRange)) {
    return Err(NS_ERROR_FAILURE);
  }

  UniquePtr<mozInlineSpellStatus> status{
      /* The constructor is `private`, hence the explicit allocation. */
      new mozInlineSpellStatus{&aSpellChecker, eOpNavigation, nullptr, nullptr,
                               std::move(anchorRange), aForceCheck,
                               aNewPositionOffset}};

  // get the root node for checking
  EditorBase* editorBase = status->mSpellChecker->mEditorBase;
  if (NS_WARN_IF(!editorBase)) {
    return Err(NS_ERROR_FAILURE);
  }
  Element* root = editorBase->GetRoot();
  if (NS_WARN_IF(!root)) {
    return Err(NS_ERROR_FAILURE);
  }
  // the anchor node might not be in the DOM anymore, check
  if (root && aOldAnchorNode &&
      !aOldAnchorNode->IsShadowIncludingInclusiveDescendantOf(root)) {
    *aContinue = false;
    return status;
  }

  status->mOldNavigationAnchorRange =
      mozInlineSpellStatus::PositionToCollapsedRange(aOldAnchorNode,
                                                     aOldAnchorOffset);
  if (NS_WARN_IF(!status->mOldNavigationAnchorRange)) {
    return Err(NS_ERROR_FAILURE);
  }

  *aContinue = true;
  return status;
}

// mozInlineSpellStatus::CreateForSelection
//
//    It is easy for selections since we always re-check the spellcheck
//    selection.

// static
UniquePtr<mozInlineSpellStatus> mozInlineSpellStatus::CreateForSelection(
    mozInlineSpellChecker& aSpellChecker) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Verbose, ("%s", __FUNCTION__));

  UniquePtr<mozInlineSpellStatus> status{
      /* The constructor is `private`, hence the explicit allocation. */
      new mozInlineSpellStatus{&aSpellChecker, eOpSelection, nullptr, nullptr,
                               nullptr, false, 0}};
  return status;
}

// mozInlineSpellStatus::CreateForRange
//
//    Called to cause the spellcheck of the given range. This will look like
//    a change operation over the given range.

// static
UniquePtr<mozInlineSpellStatus> mozInlineSpellStatus::CreateForRange(
    mozInlineSpellChecker& aSpellChecker, dom::Range* aRange,
    SetAnchorToCaret aSetAnchorToCaret) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Debug,
          ("%s: range=%p", __FUNCTION__, aRange));

  UniquePtr<mozInlineSpellStatus> status{
      /* The constructor is `private`, hence the explicit allocation. */
      new mozInlineSpellStatus{&aSpellChecker, eOpChange, nullptr, nullptr,
                               nullptr, false, 0, aSetAnchorToCaret}};

  status->mRange = aRange;
  return status;
}

// mozInlineSpellStatus::FinishInitOnEvent
//
//    Called when the event is triggered to complete initialization that
//    might require the WordUtil. This calls to the operation-specific
//    initializer, and also sets the range to be the entire element if it
//    is nullptr.
//
//    Watch out: the range might still be nullptr if there is nothing to do,
//    the caller will have to check for this.

nsresult mozInlineSpellStatus::FinishInitOnEvent(
    mozInlineSpellWordUtil& aWordUtil) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Verbose,
          ("%s: mRange=%p", __FUNCTION__, mRange.get()));

  nsresult rv;
  if (!mRange) {
    rv = mSpellChecker->MakeSpellCheckRange(nullptr, 0, nullptr, 0,
                                            getter_AddRefs(mRange));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (mSetAnchorToCaret == SetAnchorToCaret::Yes) {
    // Set anchor range to caret position - we do this now instead of when
    // creating the status in case the web app moved the selection in the
    // meantime (especially likely for EditContext).
    mSetAnchorToCaret = SetAnchorToCaret::No;
    MOZ_ASSERT(!mAnchorRange);
    if (Document* doc = GetDocument()) {
      Selection* selection = doc->GetSelection(IgnoreErrors());
      if (selection && selection->RangeCount() && selection->IsCollapsed()) {
        mAnchorRange = selection->GetRangeAt(0);
        mSpellChecker->mNeedsCheckAfterNavigation = true;
        mSpellChecker->SaveCurrentSelectionPosition();
      }
    }
  }

  switch (mOp) {
    case eOpChange:
      if (mAnchorRange) return FillNoCheckRangeFromAnchor(aWordUtil);
      break;
    case eOpChangeDelete:
      if (mAnchorRange) {
        rv = FillNoCheckRangeFromAnchor(aWordUtil);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      // Delete events will have no range for the changed text (because it was
      // deleted), and CreateForEditorChange will set it to nullptr. Here, we
      // select the entire word to cause any underlining to be removed.
      mRange = mNoCheckRange;
      break;
    case eOpNavigation:
      return FinishNavigationEvent(aWordUtil);
    case eOpSelection:
      // this gets special handling in ResumeCheck
      break;
    case eOpResume:
      // everything should be initialized already in this case
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Bad operation");
      return NS_ERROR_NOT_INITIALIZED;
  }
  return NS_OK;
}

// mozInlineSpellStatus::FinishNavigationEvent
//
//    This verifies that we need to check the word at the previous caret
//    position. Now that we have the word util, we can find the word belonging
//    to the previous caret position. If the new position is inside that word,
//    we don't want to do anything. In this case, we'll nullptr out mRange so
//    that the caller will know not to continue.
//
//    Notice that we don't set mNoCheckRange. We check here whether the cursor
//    is in the word that needs checking, so it isn't necessary. Plus, the
//    spellchecker isn't guaranteed to only check the given word, and it could
//    remove the underline from the new word under the cursor.

nsresult mozInlineSpellStatus::FinishNavigationEvent(
    mozInlineSpellWordUtil& aWordUtil) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Verbose, ("%s", __FUNCTION__));

  RefPtr<EditorBase> editorBase = mSpellChecker->mEditorBase;
  if (!editorBase) {
    return NS_ERROR_FAILURE;  // editor is gone
  }

  MOZ_ASSERT(mAnchorRange, "No anchor for navigation!");

  if (!mOldNavigationAnchorRange->IsPositioned()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  // get the DOM position of the old caret, the range should be collapsed
  nsCOMPtr<nsINode> oldAnchorNode =
      mOldNavigationAnchorRange->GetStartContainer();
  uint32_t oldAnchorOffset = mOldNavigationAnchorRange->StartOffset();

  // find the word on the old caret position, this is the one that we MAY need
  // to check
  RefPtr<dom::Range> oldWord;
  nsresult rv = aWordUtil.GetRangeForWord(oldAnchorNode,
                                          static_cast<int32_t>(oldAnchorOffset),
                                          getter_AddRefs(oldWord));
  NS_ENSURE_SUCCESS(rv, rv);

  // aWordUtil.GetRangeForWord flushes pending notifications, check editor
  // again.
  if (!mSpellChecker->mEditorBase) {
    return NS_ERROR_FAILURE;  // editor is gone
  }

  // get the DOM position of the new caret, the range should be collapsed
  nsCOMPtr<nsINode> newAnchorNode = mAnchorRange->GetStartContainer();
  uint32_t newAnchorOffset = mAnchorRange->StartOffset();

  // see if the new cursor position is in the word of the old cursor position
  bool isInRange = false;
  if (!mForceNavigationWordCheck) {
    ErrorResult err;
    isInRange = oldWord->IsPointInRange(
        *newAnchorNode, newAnchorOffset + mNewNavigationPositionOffset, err);
    if (NS_WARN_IF(err.Failed())) {
      return err.StealNSResult();
    }
  }

  if (isInRange) {
    // caller should give up
    mRange = nullptr;
  } else {
    // check the old word
    mRange = oldWord;

    // Once we've spellchecked the current word, we don't need to spellcheck
    // for any more navigation events.
    mSpellChecker->mNeedsCheckAfterNavigation = false;
  }
  return NS_OK;
}

// mozInlineSpellStatus::FillNoCheckRangeFromAnchor
//
//    Given the mAnchorRange object, computes the range of the word it is on
//    (if any) and fills that range into mNoCheckRange. This is used for
//    change and navigation events to know which word we should skip spell
//    checking on

nsresult mozInlineSpellStatus::FillNoCheckRangeFromAnchor(
    mozInlineSpellWordUtil& aWordUtil) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Verbose, ("%s", __FUNCTION__));

  if (!mAnchorRange->IsPositioned()) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  nsCOMPtr<nsINode> anchorNode = mAnchorRange->GetStartContainer();
  uint32_t anchorOffset = mAnchorRange->StartOffset();
  return aWordUtil.GetRangeForWord(anchorNode,
                                   static_cast<int32_t>(anchorOffset),
                                   getter_AddRefs(mNoCheckRange));
}

// mozInlineSpellStatus::GetDocument
//
//    Returns the Document object for the document for the
//    current spellchecker.

Document* mozInlineSpellStatus::GetDocument() const {
  if (!mSpellChecker->mEditorBase) {
    return nullptr;
  }

  return mSpellChecker->mEditorBase->GetDocument();
}

// mozInlineSpellStatus::PositionToCollapsedRange
//
//    Converts a given DOM position to a collapsed range covering that
//    position. We use ranges to store DOM positions becuase they stay
//    updated as the DOM is changed.

// static
already_AddRefed<dom::Range> mozInlineSpellStatus::PositionToCollapsedRange(
    nsINode* aNode, uint32_t aOffset) {
  if (NS_WARN_IF(!aNode)) {
    return nullptr;
  }
  IgnoredErrorResult ignoredError;
  RefPtr<dom::Range> range =
      dom::Range::Create(aNode, aOffset, aNode, aOffset, ignoredError);
  NS_WARNING_ASSERTION(!ignoredError.Failed(),
                       "Creating collapsed range failed");
  return range.forget();
}

// mozInlineSpellResume

class mozInlineSpellResume : public Runnable {
 public:
  mozInlineSpellResume(UniquePtr<mozInlineSpellStatus>&& aStatus,
                       uint32_t aDisabledAsyncToken)
      : Runnable("mozInlineSpellResume"),
        mDisabledAsyncToken(aDisabledAsyncToken),
        mStatus(std::move(aStatus)) {}

  nsresult Post() {
    nsCOMPtr<nsIRunnable> runnable(this);
    return NS_DispatchToCurrentThreadQueue(runnable.forget(), 1000,
                                           EventQueuePriority::Idle);
  }

  NS_IMETHOD Run() override {
    // Discard the resumption if the spell checker was disabled after the
    // resumption was scheduled.
    if (mDisabledAsyncToken ==
        mStatus->mSpellChecker->GetDisabledAsyncToken()) {
      mStatus->mSpellChecker->ResumeCheck(std::move(mStatus));
    }
    return NS_OK;
  }

 private:
  uint32_t mDisabledAsyncToken;
  UniquePtr<mozInlineSpellStatus> mStatus;
};

// Used as the nsIEditorSpellCheck::InitSpellChecker callback.
class InitEditorSpellCheckCallback final : public nsIEditorSpellCheckCallback {
  ~InitEditorSpellCheckCallback() = default;

 public:
  NS_DECL_ISUPPORTS

  explicit InitEditorSpellCheckCallback(mozInlineSpellChecker* aSpellChecker)
      : mSpellChecker(aSpellChecker) {}

  NS_IMETHOD EditorSpellCheckDone() override {
    return mSpellChecker ? mSpellChecker->EditorSpellCheckInited() : NS_OK;
  }

  void Cancel() { mSpellChecker = nullptr; }

 private:
  RefPtr<mozInlineSpellChecker> mSpellChecker;
};
NS_IMPL_ISUPPORTS(InitEditorSpellCheckCallback, nsIEditorSpellCheckCallback)

NS_INTERFACE_MAP_BEGIN(mozInlineSpellChecker)
  NS_INTERFACE_MAP_ENTRY(nsIInlineSpellChecker)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventListener)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMEventListener)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(mozInlineSpellChecker)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(mozInlineSpellChecker)
NS_IMPL_CYCLE_COLLECTING_RELEASE(mozInlineSpellChecker)

NS_IMPL_CYCLE_COLLECTION_WEAK(mozInlineSpellChecker, mEditorBase, mSpellCheck,
                              mCurrentSelectionAnchorNode, mPendingSpellCheck,
                              mPendingDOMMutationRange)

mozInlineSpellChecker::SpellCheckingState
    mozInlineSpellChecker::gCanEnableSpellChecking =
        mozInlineSpellChecker::SpellCheck_Uninitialized;

mozInlineSpellChecker::mozInlineSpellChecker()
    : mNumWordsInSpellSelection(0),
      mMaxNumWordsInSpellSelection(
          StaticPrefs::extensions_spellcheck_inline_max_misspellings()),
      mNumPendingSpellChecks(0),
      mNumPendingUpdateCurrentDictionary(0),
      mDisabledAsyncToken(0),
      mNeedsCheckAfterNavigation(false),
      mFullSpellCheckScheduled(false),
      mIsListeningToEditSubActions(false) {}

mozInlineSpellChecker::~mozInlineSpellChecker() = default;

EditorSpellCheck* mozInlineSpellChecker::GetEditorSpellCheck() {
  return mSpellCheck ? mSpellCheck : mPendingSpellCheck;
}

NS_IMETHODIMP
mozInlineSpellChecker::GetSpellChecker(nsIEditorSpellCheck** aSpellCheck) {
  *aSpellCheck = mSpellCheck;
  NS_IF_ADDREF(*aSpellCheck);
  return NS_OK;
}

NS_IMETHODIMP
mozInlineSpellChecker::Init(nsIEditor* aEditor) {
  mEditorBase = aEditor ? aEditor->AsEditorBase() : nullptr;
  return NS_OK;
}

// mozInlineSpellChecker::Cleanup
//
//    Called by the editor when the editor is going away. This is important
//    because we remove listeners. We do NOT clean up anything else in this
//    function, because it can get called while DoSpellCheck is running!
//
//    Getting the style information there can cause DOM notifications to be
//    flushed, which can cause editors to go away which will bring us here.
//    We can not do anything that will cause DoSpellCheck to freak out.

MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult
mozInlineSpellChecker::Cleanup(bool aDestroyingFrames) {
  mNumWordsInSpellSelection = 0;
  RefPtr<Selection> spellCheckSelection = GetSpellCheckSelection();
  nsresult rv = NS_OK;
  if (!spellCheckSelection) {
    // Ensure we still unregister event listeners (but return a failure code)
    UnregisterEventListeners();
    rv = NS_ERROR_FAILURE;
  } else {
    if (!aDestroyingFrames) {
      spellCheckSelection->RemoveAllRanges(IgnoreErrors());
    }

    rv = UnregisterEventListeners();
  }

  // Notify ENDED observers now.  If we wait to notify as we normally do when
  // these async operations finish, then in the meantime the editor may create
  // another inline spell checker and cause more STARTED and ENDED
  // notifications to be broadcast.  Interleaved notifications for the same
  // editor but different inline spell checkers could easily confuse
  // observers.  They may receive two consecutive STARTED notifications for
  // example, which we guarantee will not happen.

  RefPtr<EditorBase> editorBase = std::move(mEditorBase);
  if (mPendingSpellCheck) {
    // Cancel the pending editor spell checker initialization.
    mPendingSpellCheck = nullptr;
    mPendingInitEditorSpellCheckCallback->Cancel();
    mPendingInitEditorSpellCheckCallback = nullptr;
    ChangeNumPendingSpellChecks(-1, editorBase);
  }

  // Increment this token so that pending UpdateCurrentDictionary calls and
  // scheduled spell checks are discarded when they finish.
  mDisabledAsyncToken++;

  if (mNumPendingUpdateCurrentDictionary > 0) {
    // Account for pending UpdateCurrentDictionary calls.
    ChangeNumPendingSpellChecks(-mNumPendingUpdateCurrentDictionary,
                                editorBase);
    mNumPendingUpdateCurrentDictionary = 0;
  }
  if (mNumPendingSpellChecks > 0) {
    // If mNumPendingSpellChecks is still > 0 at this point, the remainder is
    // pending scheduled spell checks.
    ChangeNumPendingSpellChecks(-mNumPendingSpellChecks, editorBase);
  }

  mFullSpellCheckScheduled = false;
  mPendingDOMMutationRange = nullptr;

  return rv;
}

// mozInlineSpellChecker::CanEnableInlineSpellChecking
//
//    This function can be called to see if it seems likely that we can enable
//    spellchecking before actually creating the InlineSpellChecking objects.
//
//    The problem is that we can't get the dictionary list without actually
//    creating a whole bunch of spellchecking objects. This function tries to
//    do that and caches the result so we don't have to keep allocating those
//    objects if there are no dictionaries or spellchecking.
//
//    Whenever dictionaries are added or removed at runtime, this value must be
//    updated before an observer notification is sent out about the change, to
//    avoid editors getting a wrong cached result.

bool  // static
mozInlineSpellChecker::CanEnableInlineSpellChecking() {
  if (gCanEnableSpellChecking == SpellCheck_Uninitialized) {
    gCanEnableSpellChecking = SpellCheck_NotAvailable;

    nsCOMPtr<nsIEditorSpellCheck> spellchecker = new EditorSpellCheck();

    bool canSpellCheck = false;
    nsresult rv = spellchecker->CanSpellCheck(&canSpellCheck);
    NS_ENSURE_SUCCESS(rv, false);

    if (canSpellCheck) gCanEnableSpellChecking = SpellCheck_Available;
  }
  return (gCanEnableSpellChecking == SpellCheck_Available);
}

void  // static
mozInlineSpellChecker::UpdateCanEnableInlineSpellChecking() {
  gCanEnableSpellChecking = SpellCheck_Uninitialized;
}

// mozInlineSpellChecker::RegisterEventListeners
//
//    The inline spell checker listens to mouse events and keyboard navigation
//    events.

nsresult mozInlineSpellChecker::RegisterEventListeners() {
  if (MOZ_UNLIKELY(NS_WARN_IF(!mEditorBase))) {
    return NS_ERROR_FAILURE;
  }

  StartToListenToEditSubActions();

  RefPtr<Document> doc = mEditorBase->GetDocument();
  if (MOZ_UNLIKELY(NS_WARN_IF(!doc))) {
    return NS_ERROR_FAILURE;
  }
  EventListenerManager* eventListenerManager =
      doc->GetOrCreateListenerManager();
  if (MOZ_UNLIKELY(NS_WARN_IF(!eventListenerManager))) {
    return NS_ERROR_FAILURE;
  }
  eventListenerManager->AddEventListenerByType(
      this, u"blur"_ns, TrustedEventsAtSystemGroupCapture());
  eventListenerManager->AddEventListenerByType(
      this, u"click"_ns, TrustedEventsAtSystemGroupCapture());
  eventListenerManager->AddEventListenerByType(
      this, u"keydown"_ns, TrustedEventsAtSystemGroupCapture());
  return NS_OK;
}

// mozInlineSpellChecker::UnregisterEventListeners

nsresult mozInlineSpellChecker::UnregisterEventListeners() {
  if (MOZ_UNLIKELY(NS_WARN_IF(!mEditorBase))) {
    return NS_ERROR_FAILURE;
  }

  EndListeningToEditSubActions();

  RefPtr<Document> doc = mEditorBase->GetDocument();
  if (MOZ_UNLIKELY(NS_WARN_IF(!doc))) {
    return NS_ERROR_FAILURE;
  }
  EventListenerManager* eventListenerManager =
      doc->GetOrCreateListenerManager();
  if (MOZ_UNLIKELY(NS_WARN_IF(!eventListenerManager))) {
    return NS_ERROR_FAILURE;
  }
  eventListenerManager->RemoveEventListenerByType(
      this, u"blur"_ns, TrustedEventsAtSystemGroupCapture());
  eventListenerManager->RemoveEventListenerByType(
      this, u"click"_ns, TrustedEventsAtSystemGroupCapture());
  eventListenerManager->RemoveEventListenerByType(
      this, u"keydown"_ns, TrustedEventsAtSystemGroupCapture());
  return NS_OK;
}

// mozInlineSpellChecker::GetEnableRealTimeSpell

NS_IMETHODIMP
mozInlineSpellChecker::GetEnableRealTimeSpell(bool* aEnabled) {
  NS_ENSURE_ARG_POINTER(aEnabled);
  *aEnabled = mSpellCheck != nullptr || mPendingSpellCheck != nullptr;
  return NS_OK;
}

// mozInlineSpellChecker::SetEnableRealTimeSpell

NS_IMETHODIMP
mozInlineSpellChecker::SetEnableRealTimeSpell(bool aEnabled) {
  if (!aEnabled) {
    mSpellCheck = nullptr;
    return Cleanup(false);
  }

  if (mSpellCheck) {
    // spellcheck the current contents. SpellCheckRange doesn't supply a created
    // range to DoSpellCheck, which in our case is the entire range. But this
    // optimization doesn't matter because there is nothing in the spellcheck
    // selection when starting, which triggers a better optimization.
    return SpellCheckRange(nullptr);
  }

  if (mPendingSpellCheck) {
    // The editor spell checker is already being initialized.
    return NS_OK;
  }

  mPendingSpellCheck = new EditorSpellCheck();
  mPendingSpellCheck->SetFilterType(nsIEditorSpellCheck::FILTERTYPE_MAIL);

  mPendingInitEditorSpellCheckCallback = new InitEditorSpellCheckCallback(this);
  nsresult rv = mPendingSpellCheck->InitSpellChecker(
      mEditorBase, false, mPendingInitEditorSpellCheckCallback);
  if (NS_FAILED(rv)) {
    mPendingSpellCheck = nullptr;
    mPendingInitEditorSpellCheckCallback = nullptr;
    NS_ENSURE_SUCCESS(rv, rv);
  }

  ChangeNumPendingSpellChecks(1);

  return NS_OK;
}

// Called when nsIEditorSpellCheck::InitSpellChecker completes.
nsresult mozInlineSpellChecker::EditorSpellCheckInited() {
  MOZ_ASSERT(mPendingSpellCheck, "Spell check should be pending!");

  // spell checking is enabled, register our event listeners to track navigation
  RegisterEventListeners();

  mSpellCheck = mPendingSpellCheck;
  mPendingSpellCheck = nullptr;
  mPendingInitEditorSpellCheckCallback = nullptr;
  ChangeNumPendingSpellChecks(-1);

  // spellcheck the current contents. SpellCheckRange doesn't supply a created
  // range to DoSpellCheck, which in our case is the entire range. But this
  // optimization doesn't matter because there is nothing in the spellcheck
  // selection when starting, which triggers a better optimization.
  return SpellCheckRange(nullptr);
}

// Changes the number of pending spell checks by the given delta.  If the number
// becomes zero or nonzero, observers are notified.  See NotifyObservers for
// info on the aEditor parameter.
void mozInlineSpellChecker::ChangeNumPendingSpellChecks(
    int32_t aDelta, EditorBase* aEditorBase) {
  const int32_t oldNumPending = mNumPendingSpellChecks;
  mNumPendingSpellChecks += aDelta;
  MOZ_ASSERT(mNumPendingSpellChecks >= 0,
             "Unbalanced ChangeNumPendingSpellChecks calls!");
  if (oldNumPending == 0 && mNumPendingSpellChecks > 0) {
    NotifyObservers(INLINESPELL_STARTED_TOPIC, aEditorBase);
  } else if (oldNumPending > 0 && mNumPendingSpellChecks == 0) {
    NotifyObservers(INLINESPELL_ENDED_TOPIC, aEditorBase);
  }
}

// Broadcasts the given topic to observers.  aEditor is passed to observers if
// nonnull; otherwise mEditorBase is passed.
void mozInlineSpellChecker::NotifyObservers(const char* aTopic,
                                            EditorBase* aEditorBase) {
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!os) return;
  // XXX Do we need to grab the editor here?  If it's necessary, each observer
  //     should do it instead.
  RefPtr<EditorBase> editorBase = aEditorBase ? aEditorBase : mEditorBase.get();
  os->NotifyObservers(static_cast<nsIEditor*>(editorBase.get()), aTopic,
                      nullptr);
}

// mozInlineSpellChecker::SpellCheckAfterEditorChange
//
//    Called by the editor when nearly anything happens to change the content.
//
//    The start and end positions specify a range for the thing that happened,
//    but these are usually nullptr, even when you'd think they would be useful
//    because you want the range (for example, pasting). We ignore them in
//    this case.

nsresult mozInlineSpellChecker::SpellCheckAfterEditorChange(
    EditSubAction aEditSubAction, Selection& aSelection,
    nsINode* aPreviousSelectedNode, uint32_t aPreviousSelectedOffset,
    nsINode* aStartNode, uint32_t aStartOffset, nsINode* aEndNode,
    uint32_t aEndOffset) {
  nsresult rv;
  if (!mSpellCheck) return NS_OK;  // disabling spell checking is not an error

  // this means something has changed, and we never check the current word,
  // therefore, we should spellcheck for subsequent caret navigations
  mNeedsCheckAfterNavigation = true;

  // the anchor node is the position of the caret
  Result<UniquePtr<mozInlineSpellStatus>, nsresult> res =
      mozInlineSpellStatus::CreateForEditorChange(
          *this, aEditSubAction, aSelection.GetAnchorNode(),
          aSelection.AnchorOffset(), aPreviousSelectedNode,
          aPreviousSelectedOffset, aStartNode, aStartOffset, aEndNode,
          aEndOffset);
  if (NS_WARN_IF(res.isErr())) {
    return res.unwrapErr();
  }

  rv = ScheduleSpellCheck(res.unwrap());
  NS_ENSURE_SUCCESS(rv, rv);

  // remember the current caret position after every change
  SaveCurrentSelectionPosition();
  return NS_OK;
}

// mozInlineSpellChecker::SpellCheckRange
//
//    Spellchecks all the words in the given range.
//    Supply a nullptr range and this will check the entire editor.

nsresult mozInlineSpellChecker::SpellCheckRange(dom::Range* aRange) {
  if (!mSpellCheck) {
    NS_WARNING_ASSERTION(
        mPendingSpellCheck,
        "Trying to spellcheck, but checking seems to be disabled");
    return NS_ERROR_NOT_INITIALIZED;
  }

  UniquePtr<mozInlineSpellStatus> status =
      mozInlineSpellStatus::CreateForRange(*this, aRange);
  return ScheduleSpellCheck(std::move(status));
}

nsresult mozInlineSpellChecker::SpellCheckNodesIgnoringWordAtCaret(
    nsINode& aContainer, nsIContent& aStartContent, nsIContent& aEndContent) {
  if (!mSpellCheck) {
    NS_WARNING_ASSERTION(
        mPendingSpellCheck,
        "Trying to spellcheck, but checking seems to be disabled");
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (mFullSpellCheckScheduled) {
    // We're going to spellcheck everything anyway.
    return NS_OK;
  }

  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Debug,
          ("%s: pending=%d, coalescing=%d", __FUNCTION__,
           mNumPendingSpellChecks, !!mPendingDOMMutationRange));
  if (mPendingDOMMutationRange) {
    const RawRangeBoundary start = RawRangeBoundary::FromChild(aStartContent);
    const RawRangeBoundary end = RawRangeBoundary::After(aEndContent);
    const Maybe<int32_t> compareStart =
        nsContentUtils::ComparePoints<TreeKind::DOM>(
            start, mPendingDOMMutationRange->StartRef());
    const Maybe<int32_t> compareEnd =
        nsContentUtils::ComparePoints<TreeKind::DOM>(
            end, mPendingDOMMutationRange->EndRef());
    if (compareStart.isSome() && compareEnd.isSome()) [[likely]] {
      if (*compareStart >= 0 && *compareEnd <= 0) {
        // The new range is contained in the pending range, we need to do
        // nothing.
        return NS_OK;
      }
      const RawRangeBoundary mergedStart =
          *compareStart < 0 ? start
                            : mPendingDOMMutationRange->StartRef().AsRaw();
      const RawRangeBoundary mergedEnd =
          *compareEnd > 0 ? end : mPendingDOMMutationRange->EndRef().AsRaw();
      if (NS_SUCCEEDED(mPendingDOMMutationRange->SetStartAndEnd(
              mergedStart, mergedEnd))) [[likely]] {
        return NS_OK;
      }
    }
    // A disconnected range or failed merge still has its queued check.  Start
    // a new batch so that the new dirty interval is not lost.
    mPendingDOMMutationRange = nullptr;
  }

  RefPtr<dom::Range> range = dom::Range::Create(&aStartContent);
  range->SelectNodesInContainer(&aContainer, &aStartContent, &aEndContent);
  // Bound live mutation-observing ranges without scanning unchanged text
  // outside the dirty interval in a potentially large editing host.
  if (mNumPendingSpellChecks >=
      std::max(
          0,
          StaticPrefs::
              extensions_spellcheck_inline_dom_mutation_coalescing_threshold())) {
    mPendingDOMMutationRange = range;
  }
  const nsresult rv = ScheduleSpellCheck(mozInlineSpellStatus::CreateForRange(
      *this, range, mozInlineSpellStatus::SetAnchorToCaret::Yes));
  if (NS_FAILED(rv) && mPendingDOMMutationRange == range) {
    mPendingDOMMutationRange = nullptr;
  }
  return rv;
}

// mozInlineSpellChecker::GetMisspelledWord

NS_IMETHODIMP
mozInlineSpellChecker::GetMisspelledWord(nsINode* aNode, uint32_t aOffset,
                                         dom::Range** newword) {
  if (NS_WARN_IF(!aNode)) {
    return NS_ERROR_INVALID_ARG;
  }
  RefPtr<Selection> spellCheckSelection = GetSpellCheckSelection();
  if (NS_WARN_IF(!spellCheckSelection)) {
    return NS_ERROR_FAILURE;
  }
  return IsPointInSelection(*spellCheckSelection, aNode, aOffset, newword);
}

// mozInlineSpellChecker::ReplaceWord

NS_IMETHODIMP
mozInlineSpellChecker::ReplaceWord(nsINode* aNode, uint32_t aOffset,
                                   const nsAString& aNewWord) {
  if (NS_WARN_IF(!mEditorBase) || NS_WARN_IF(aNewWord.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<dom::Range> range;
  nsresult res = GetMisspelledWord(aNode, aOffset, getter_AddRefs(range));
  NS_ENSURE_SUCCESS(res, res);

  if (!range) {
    return NS_OK;
  }

  // In usual cases, any words shouldn't include line breaks, but technically,
  // they may include and we need to avoid `HTMLTextAreaElement.value` returns
  // \r.  Therefore, we need to handle it here.
  nsString newWord(aNewWord);
  if (mEditorBase->IsTextEditor()) {
    nsContentUtils::PlatformToDOMLineBreaks(newWord);
  }

  // Blink dispatches cancelable `beforeinput` event at collecting misspelled
  // word so that we should allow to dispatch cancelable event.
  RefPtr<EditorBase> editorBase(mEditorBase);
  DebugOnly<nsresult> rv = editorBase->ReplaceTextAsAction(
      newWord, range, EditorBase::AllowBeforeInputEventCancelable::Yes);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to insert the new word");
  return NS_OK;
}

// mozInlineSpellChecker::AddWordToDictionary

NS_IMETHODIMP
mozInlineSpellChecker::AddWordToDictionary(const nsAString& word) {
  NS_ENSURE_TRUE(mSpellCheck, NS_ERROR_NOT_INITIALIZED);

  nsresult rv = mSpellCheck->AddWordToDictionary(word);
  NS_ENSURE_SUCCESS(rv, rv);

  UniquePtr<mozInlineSpellStatus> status =
      mozInlineSpellStatus::CreateForSelection(*this);
  return ScheduleSpellCheck(std::move(status));
}

//  mozInlineSpellChecker::RemoveWordFromDictionary

NS_IMETHODIMP
mozInlineSpellChecker::RemoveWordFromDictionary(const nsAString& word) {
  NS_ENSURE_TRUE(mSpellCheck, NS_ERROR_NOT_INITIALIZED);

  nsresult rv = mSpellCheck->RemoveWordFromDictionary(word);
  NS_ENSURE_SUCCESS(rv, rv);

  UniquePtr<mozInlineSpellStatus> status =
      mozInlineSpellStatus::CreateForRange(*this, nullptr);
  return ScheduleSpellCheck(std::move(status));
}

// mozInlineSpellChecker::IgnoreWord

NS_IMETHODIMP
mozInlineSpellChecker::IgnoreWord(const nsAString& word) {
  NS_ENSURE_TRUE(mSpellCheck, NS_ERROR_NOT_INITIALIZED);

  nsresult rv = mSpellCheck->IgnoreWordAllOccurrences(word);
  NS_ENSURE_SUCCESS(rv, rv);

  UniquePtr<mozInlineSpellStatus> status =
      mozInlineSpellStatus::CreateForSelection(*this);
  return ScheduleSpellCheck(std::move(status));
}

// mozInlineSpellChecker::IgnoreWords

NS_IMETHODIMP
mozInlineSpellChecker::IgnoreWords(const nsTArray<nsString>& aWordsToIgnore) {
  NS_ENSURE_TRUE(mSpellCheck, NS_ERROR_NOT_INITIALIZED);

  // add each word to the ignore list and then recheck the document
  for (auto& word : aWordsToIgnore) {
    mSpellCheck->IgnoreWordAllOccurrences(word);
  }

  UniquePtr<mozInlineSpellStatus> status =
      mozInlineSpellStatus::CreateForSelection(*this);
  return ScheduleSpellCheck(std::move(status));
}

// mozInlineSpellChecker::MakeSpellCheckRange
//
//    Given begin and end positions, this function constructs a range as
//    required for ScheduleSpellCheck. If the start and end nodes are nullptr,
//    then the entire range will be selected, and you can supply -1 as the
//    offset to the end range to select all of that node.
//
//    If the resulting range would be empty, nullptr is put into *aRange and the
//    function succeeds.

nsresult mozInlineSpellChecker::MakeSpellCheckRange(nsINode* aStartNode,
                                                    int32_t aStartOffset,
                                                    nsINode* aEndNode,
                                                    int32_t aEndOffset,
                                                    dom::Range** aRange) const {
  nsresult rv;
  *aRange = nullptr;

  if (NS_WARN_IF(!mEditorBase)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<Document> doc = mEditorBase->GetDocument();
  if (NS_WARN_IF(!doc)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<dom::Range> range = dom::Range::Create(doc);

  // possibly use full range of the editor
  if (!aStartNode || !aEndNode) {
    Element* domRootElement = mEditorBase->GetRoot();
    if (NS_WARN_IF(!domRootElement)) {
      return NS_ERROR_FAILURE;
    }
    aStartNode = aEndNode = domRootElement;
    aStartOffset = 0;
    aEndOffset = -1;
  }

  if (aEndOffset == -1) {
    // It's hard to say whether it's better to just do nsINode::GetChildCount or
    // get the ChildNodes() and then its length.  The latter is faster if we
    // keep going through this code for the same nodes (because it caches the
    // length).  The former is faster if we keep getting different nodes here...
    //
    // Let's do the thing which can't end up with bad O(N^2) behavior.
    aEndOffset = aEndNode->ChildNodes()->Length();
  }

  // sometimes we are are requested to check an empty range (possibly an empty
  // document). This will result in assertions later.
  if (aStartNode == aEndNode && aStartOffset == aEndOffset) return NS_OK;

  if (aEndOffset) {
    rv = range->SetStartAndEnd(aStartNode, aStartOffset, aEndNode, aEndOffset);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  } else {
    if (NS_WARN_IF(!aEndNode->IsContent())) {
      return NS_ERROR_FAILURE;
    }
    rv = range->SetStartAndEnd(RawRangeBoundary(aStartNode, aStartOffset),
                               RawRangeBoundary::After(*aEndNode->AsContent()));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  range.swap(*aRange);
  return NS_OK;
}

nsresult mozInlineSpellChecker::SpellCheckBetweenNodes(nsINode* aStartNode,
                                                       int32_t aStartOffset,
                                                       nsINode* aEndNode,
                                                       int32_t aEndOffset) {
  RefPtr<dom::Range> range;
  nsresult rv = MakeSpellCheckRange(aStartNode, aStartOffset, aEndNode,
                                    aEndOffset, getter_AddRefs(range));
  NS_ENSURE_SUCCESS(rv, rv);

  if (!range) return NS_OK;  // range is empty: nothing to do

  UniquePtr<mozInlineSpellStatus> status =
      mozInlineSpellStatus::CreateForRange(*this, range);
  return ScheduleSpellCheck(std::move(status));
}

// mozInlineSpellChecker::ShouldSpellCheckNode
//
//    There are certain conditions when we don't want to spell check a node. In
//    particular quotations, moz signatures, etc. This routine returns false
//    for these cases.

// static
bool mozInlineSpellChecker::ShouldSpellCheckNode(EditorBase* aEditorBase,
                                                 nsINode* aNode) {
  MOZ_ASSERT(aNode);
  if (!aNode->IsContent()) return false;

  nsIContent* content = aNode->AsContent();

  if (aEditorBase->IsMailEditor()) {
    nsIContent* parent = content->GetParent();
    while (parent) {
      if (parent->IsHTMLElement(nsGkAtoms::blockquote) &&
          parent->AsElement()->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                                           nsGkAtoms::cite, eIgnoreCase)) {
        return false;
      }
      if (parent->IsAnyOfHTMLElements(nsGkAtoms::pre, nsGkAtoms::div) &&
          parent->AsElement()->AttrValueIs(kNameSpaceID_None, nsGkAtoms::_class,
                                           nsGkAtoms::mozsignature,
                                           eIgnoreCase)) {
        return false;
      }
      if (parent->IsHTMLElement(nsGkAtoms::div) &&
          parent->AsElement()->AttrValueIs(kNameSpaceID_None, nsGkAtoms::_class,
                                           nsGkAtoms::mozfwcontainer,
                                           eIgnoreCase)) {
        return false;
      }

      parent = parent->GetParent();
    }
  } else {
    // Check spelling only if the node is editable, and GetSpellcheck() is true
    // on the nearest HTMLElement ancestor.
    if (!content->IsEditable()) {
      return false;
    }

    // Make sure that we can always turn on spell checking for inputs/textareas.
    // Note that because of the previous check, at this point we know that the
    // node is editable.
    if (content->IsInNativeAnonymousSubtree()) {
      nsIContent* node =
          content->GetClosestNativeAnonymousSubtreeRootParentOrHost();
      if (node && node->IsTextControlElement()) {
        return true;
      }
    }

    // Get HTML element ancestor (might be aNode itself, although probably that
    // has to be a text node in real life here)
    nsIContent* parent = content;
    while (!parent->IsHTMLElement()) {
      parent = parent->GetParent();
      if (!parent) {
        return true;
      }
    }

    // See if it's spellcheckable
    return static_cast<nsGenericHTMLElement*>(parent)->Spellcheck();
  }

  return true;
}

// mozInlineSpellChecker::ScheduleSpellCheck
//
//    This is called by code to do the actual spellchecking. We will set up
//    the proper structures for calls to DoSpellCheck.

nsresult mozInlineSpellChecker::ScheduleSpellCheck(
    UniquePtr<mozInlineSpellStatus>&& aStatus) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Debug,
          ("%s: mFullSpellCheckScheduled=%i", __FUNCTION__,
           mFullSpellCheckScheduled));

  if (mFullSpellCheckScheduled) {
    // Just ignore this; we're going to spell-check everything anyway
    return NS_OK;
  }
  // Cache the value because we are going to move aStatus's ownership to
  // the new created mozInlineSpellResume instance.
  bool isFullSpellCheck = aStatus->IsFullSpellCheck();

  RefPtr<mozInlineSpellResume> resume =
      new mozInlineSpellResume(std::move(aStatus), mDisabledAsyncToken);
  NS_ENSURE_TRUE(resume, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = resume->Post();
  if (NS_SUCCEEDED(rv)) {
    if (isFullSpellCheck) {
      // We're going to check everything.  Suppress further spell-check attempts
      // until that happens.
      mFullSpellCheckScheduled = true;
    }
    ChangeNumPendingSpellChecks(1);
  }
  return rv;
}

// mozInlineSpellChecker::DoSpellCheckSelection
//
//    Called to re-check all misspelled words. We iterate over all ranges in
//    the selection and call DoSpellCheck on them. This is used when a word
//    is ignored or added to the dictionary: all instances of that word should
//    be removed from the selection.
//
//    FIXME-PERFORMANCE: This takes as long as it takes and is not resumable.
//    Typically, checking this small amount of text is relatively fast, but
//    for large numbers of words, a lag may be noticeable.

nsresult mozInlineSpellChecker::DoSpellCheckSelection(
    mozInlineSpellWordUtil& aWordUtil, Selection* aSpellCheckSelection) {
  nsresult rv;

  // clear out mNumWordsInSpellSelection since we'll be rebuilding the ranges.
  mNumWordsInSpellSelection = 0;

  // Since we could be modifying the ranges for the spellCheckSelection while
  // looping on the spell check selection, keep a separate array of range
  // elements inside the selection
  nsTArray<RefPtr<dom::Range>> ranges;

  const uint32_t rangeCount = aSpellCheckSelection->RangeCount();
  for (const uint32_t idx : IntegerRange(rangeCount)) {
    MOZ_ASSERT(aSpellCheckSelection->RangeCount() == rangeCount);
    dom::Range* range = aSpellCheckSelection->GetRangeAt(idx);
    MOZ_ASSERT(range);
    if (MOZ_LIKELY(range)) {
      ranges.AppendElement(range);
    }
  }

  // We have saved the ranges above. Clearing the spellcheck selection here
  // isn't necessary (rechecking each word will modify it as necessary) but
  // provides better performance. By ensuring that no ranges need to be
  // removed in DoSpellCheck, we can save checking range inclusion which is
  // slow.
  aSpellCheckSelection->RemoveAllRanges(IgnoreErrors());

  // We use this state object for all calls, and just update its range. Note
  // that we don't need to call FinishInit since we will be filling in the
  // necessary information.
  UniquePtr<mozInlineSpellStatus> status =
      mozInlineSpellStatus::CreateForRange(*this, nullptr);

  bool doneChecking;
  for (uint32_t idx : IntegerRange(rangeCount)) {
    // We can consider this word as "added" since we know it has no spell
    // check range over it that needs to be deleted. All the old ranges
    // were cleared above. We also need to clear the word count so that we
    // check all words instead of stopping early.
    status->mRange = ranges[idx];
    rv = DoSpellCheck(aWordUtil, aSpellCheckSelection, status, &doneChecking);
    NS_ENSURE_SUCCESS(rv, rv);
    MOZ_ASSERT(
        doneChecking,
        "We gave the spellchecker one word, but it didn't finish checking?!?!");
  }

  return NS_OK;
}

class MOZ_STACK_CLASS mozInlineSpellChecker::SpellCheckerSlice {
 public:
  /**
   * @param aStatus must be non-nullptr.
   */
  SpellCheckerSlice(mozInlineSpellChecker& aInlineSpellChecker,
                    mozInlineSpellWordUtil& aWordUtil,
                    mozilla::dom::Selection& aSpellCheckSelection,
                    const mozilla::UniquePtr<mozInlineSpellStatus>& aStatus,
                    bool& aDoneChecking)
      : mInlineSpellChecker{aInlineSpellChecker},
        mWordUtil{aWordUtil},
        mSpellCheckSelection{aSpellCheckSelection},
        mStatus{aStatus},
        mDoneChecking{aDoneChecking} {
    MOZ_ASSERT(aStatus);
  }

  [[nodiscard]] nsresult Execute();

 private:
  // Creates an async request to check the words and update the ranges for the
  // misspellings.
  //
  // @param aWords normalized words corresponding to aNodeOffsetRangesForWords.
  // @param aOldRangesForSomeWords ranges from previous spellcheckings which
  //                               might need to be removed. Its length might
  //                               differ from `aWords.Length()`.
  // @param aNodeOffsetRangesForWords One range for each word in aWords. So
  //                                  `aNodeOffsetRangesForWords.Length() ==
  //                                  aWords.Length()`.
  void CheckWordsAndUpdateRangesForMisspellings(
      const nsTArray<nsString>& aWords,
      nsTArray<RefPtr<dom::Range>>&& aOldRangesForSomeWords,
      nsTArray<NodeOffsetRange>&& aNodeOffsetRangesForWords);

  void RemoveRanges(const nsTArray<RefPtr<dom::Range>>& aRanges);

  bool ShouldSpellCheckRange(const dom::Range& aRange) const;

  bool IsInNoCheckRange(const nsINode& aNode, int32_t aOffset) const;

  mozInlineSpellChecker& mInlineSpellChecker;
  mozInlineSpellWordUtil& mWordUtil;
  mozilla::dom::Selection& mSpellCheckSelection;
  const mozilla::UniquePtr<mozInlineSpellStatus>& mStatus;
  bool& mDoneChecking;
};

bool mozInlineSpellChecker::SpellCheckerSlice::ShouldSpellCheckRange(
    const dom::Range& aRange) const {
  if (aRange.Collapsed()) {
    return false;
  }

  nsINode* beginNode = aRange.GetStartContainer();
  nsINode* endNode = aRange.GetEndContainer();

  const nsINode* rootNode = mWordUtil.GetRootNode();
  return beginNode->IsInComposedDoc() && endNode->IsInComposedDoc() &&
         beginNode->IsShadowIncludingInclusiveDescendantOf(rootNode) &&
         endNode->IsShadowIncludingInclusiveDescendantOf(rootNode);
}

bool mozInlineSpellChecker::SpellCheckerSlice::IsInNoCheckRange(
    const nsINode& aNode, int32_t aOffset) const {
  ErrorResult erv;
  return mStatus->GetNoCheckRange() &&
         mStatus->GetNoCheckRange()->IsPointInRange(aNode, aOffset, erv);
}

void mozInlineSpellChecker::SpellCheckerSlice::RemoveRanges(
    const nsTArray<RefPtr<dom::Range>>& aRanges) {
  for (uint32_t i = 0; i < aRanges.Length(); i++) {
    mInlineSpellChecker.RemoveRange(&mSpellCheckSelection, aRanges[i]);
  }
}

// mozInlineSpellChecker::SpellCheckerSlice::Execute
//
//    This function checks words intersecting the given range, excluding those
//    inside mStatus->mNoCheckRange (can be nullptr). Words inside aNoCheckRange
//    will have any spell selection removed (this is used to hide the
//    underlining for the word that the caret is in). aNoCheckRange should be
//    on word boundaries.
//
//    mResume->mCreatedRange is a possibly nullptr range of new text that was
//    inserted.  Inside this range, we don't bother to check whether things are
//    inside the spellcheck selection, which speeds up large paste operations
//    considerably.
//
//    Normal case when editing text by typing
//       h e l l o   w o r k d   h o w   a r e   y o u
//                            ^ caret
//                   [-------] mRange
//                   [-------] mNoCheckRange
//      -> does nothing (range is the same as the no check range)
//
//    Case when pasting:
//             [---------- pasted text ----------]
//       h e l l o   w o r k d   h o w   a r e   y o u
//                                                ^ caret
//                                               [---] aNoCheckRange
//      -> recheck all words in range except those in aNoCheckRange
//
//    If checking is complete, *aDoneChecking will be set. If there is more
//    but we ran out of time, this will be false and the range will be
//    updated with the stuff that still needs checking.

nsresult mozInlineSpellChecker::SpellCheckerSlice::Execute() {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Debug, ("%s", __FUNCTION__));

  mDoneChecking = true;

  if (NS_WARN_IF(!mInlineSpellChecker.mSpellCheck)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (mInlineSpellChecker.IsSpellCheckSelectionFull()) {
    return NS_OK;
  }

  // get the editor for ShouldSpellCheckNode, this may fail in reasonable
  // circumstances since the editor could have gone away
  RefPtr<EditorBase> editorBase = mInlineSpellChecker.mEditorBase;
  if (!editorBase || editorBase->Destroyed()) {
    return NS_ERROR_FAILURE;
  }

  if (!ShouldSpellCheckRange(*mStatus->mRange)) {
    // Just bail out and don't try to spell-check this
    return NS_OK;
  }

  // see if the selection has any ranges, if not, then we can optimize checking
  // range inclusion later (we have no ranges when we are initially checking or
  // when there are no misspelled words yet).
  const int32_t originalRangeCount = mSpellCheckSelection.RangeCount();

  // set the starting DOM position to be the beginning of our range
  if (nsresult rv = mWordUtil.SetPositionAndEnd(mStatus->mRange->StartRef(),
                                                mStatus->mRange->EndRef());
      NS_FAILED(rv)) {
    // Just bail out and don't try to spell-check this
    return NS_OK;
  }

  // aWordUtil.SetPosition flushes pending notifications, check editor again.
  if (!mInlineSpellChecker.mEditorBase) {
    return NS_ERROR_FAILURE;
  }

  int32_t wordsChecked = 0;
  PRTime beginTime = PR_Now();

  nsTArray<nsString> normalizedWords;
  nsTArray<RefPtr<dom::Range>> oldRangesToRemove;
  nsTArray<NodeOffsetRange> checkRanges;
  mozInlineSpellWordUtil::Word word;
  static const size_t requestChunkSize =
      INLINESPELL_MAXIMUM_CHUNKED_WORDS_PER_TASK;

  while (mWordUtil.GetNextWord(word)) {
    // get the range for the current word.
    nsINode* const beginNode = word.mNodeOffsetRange.Begin().Node();
    nsINode* const endNode = word.mNodeOffsetRange.End().Node();
    // TODO: Make them `uint32_t`
    const int32_t beginOffset = word.mNodeOffsetRange.Begin().Offset();
    const int32_t endOffset = word.mNodeOffsetRange.End().Offset();

    // see if we've done enough words in this round and run out of time.
    if (wordsChecked >= INLINESPELL_MINIMUM_WORDS_BEFORE_TIMEOUT &&
        PR_Now() > PRTime(beginTime + kMaxSpellCheckTimeInUsec)) {
      // stop checking, our time limit has been exceeded.
      MOZ_LOG(
          sInlineSpellCheckerLog, LogLevel::Verbose,
          ("%s: we have run out of time, schedule next round.", __FUNCTION__));

      CheckWordsAndUpdateRangesForMisspellings(normalizedWords,
                                               std::move(oldRangesToRemove),
                                               std::move(checkRanges));

      // move the range to encompass the stuff that needs checking.
      nsresult rv = mStatus->mRange->SetStart(
          beginNode, AssertedCast<uint32_t>(beginOffset));
      if (NS_FAILED(rv)) {
        // The range might be unhappy because the beginning is after the
        // end. This is possible when the requested end was in the middle
        // of a word, just ignore this situation and assume we're done.
        return NS_OK;
      }
      mDoneChecking = false;
      return NS_OK;
    }

    MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Debug,
            ("%s: got word \"%s\"%s", __FUNCTION__,
             NS_ConvertUTF16toUTF8(word.mText).get(),
             word.mSkipChecking ? " (not checking)" : ""));

    // see if there is a spellcheck range that already intersects the word
    // and remove it. We only need to remove old ranges, so don't bother if
    // there were no ranges when we started out.
    if (originalRangeCount > 0) {
      ErrorResult erv;
      // likewise, if this word is inside new text, we won't bother testing
      if (!mStatus->GetCreatedRange() ||
          !mStatus->GetCreatedRange()->IsPointInRange(
              *beginNode, AssertedCast<uint32_t>(beginOffset), erv)) {
        MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Debug,
                ("%s: removing ranges for some interval.", __FUNCTION__));

        nsTArray<RefPtr<dom::Range>> ranges;
        mSpellCheckSelection.GetRangesForInterval(
            *beginNode, AssertedCast<uint32_t>(beginOffset), *endNode,
            AssertedCast<uint32_t>(endOffset), true, ranges, erv);
        RETURN_NSRESULT_ON_FAILURE(erv);
        oldRangesToRemove.AppendElements(std::move(ranges));
      }
    }

    // some words are special and don't need checking
    if (word.mSkipChecking) {
      continue;
    }

    // some nodes we don't spellcheck
    if (!mozInlineSpellChecker::ShouldSpellCheckNode(editorBase, beginNode)) {
      continue;
    }

    // Don't check spelling if we're inside the noCheckRange. This needs to
    // be done after we clear any old selection because the excluded word
    // might have been previously marked.
    //
    // We do a simple check to see if the beginning of our word is in the
    // exclusion range. Because the exclusion range is a multiple of a word,
    // this is sufficient.
    if (IsInNoCheckRange(*beginNode, beginOffset)) {
      continue;
    }

    // check spelling and add to selection if misspelled
    mozInlineSpellWordUtil::NormalizeWord(word.mText);
    normalizedWords.AppendElement(word.mText);
    checkRanges.AppendElement(word.mNodeOffsetRange);
    wordsChecked++;
    if (normalizedWords.Length() >= requestChunkSize) {
      CheckWordsAndUpdateRangesForMisspellings(normalizedWords,
                                               std::move(oldRangesToRemove),
                                               std::move(checkRanges));
      normalizedWords.Clear();
      oldRangesToRemove = {};
      // Set new empty data for spellcheck range in DOM to avoid
      // clang-tidy detection.
      checkRanges = nsTArray<NodeOffsetRange>();
    }
  }

  CheckWordsAndUpdateRangesForMisspellings(
      normalizedWords, std::move(oldRangesToRemove), std::move(checkRanges));

  return NS_OK;
}

nsresult mozInlineSpellChecker::DoSpellCheck(
    mozInlineSpellWordUtil& aWordUtil, Selection* aSpellCheckSelection,
    const UniquePtr<mozInlineSpellStatus>& aStatus, bool* aDoneChecking) {
  MOZ_ASSERT(aDoneChecking);

  SpellCheckerSlice spellCheckerSlice{*this, aWordUtil, *aSpellCheckSelection,
                                      aStatus, *aDoneChecking};

  return spellCheckerSlice.Execute();
}

// An RAII helper that calls ChangeNumPendingSpellChecks on destruction.
class MOZ_RAII AutoChangeNumPendingSpellChecks final {
 public:
  explicit AutoChangeNumPendingSpellChecks(mozInlineSpellChecker* aSpellChecker,
                                           int32_t aDelta)
      : mSpellChecker(aSpellChecker), mDelta(aDelta) {}

  ~AutoChangeNumPendingSpellChecks() {
    mSpellChecker->ChangeNumPendingSpellChecks(mDelta);
  }

 private:
  RefPtr<mozInlineSpellChecker> mSpellChecker;
  int32_t mDelta;
};

void mozInlineSpellChecker::SpellCheckerSlice::
    CheckWordsAndUpdateRangesForMisspellings(
        const nsTArray<nsString>& aWords,
        nsTArray<RefPtr<dom::Range>>&& aOldRangesForSomeWords,
        nsTArray<NodeOffsetRange>&& aNodeOffsetRangesForWords) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Verbose,
          ("%s: aWords.Length()=%i", __FUNCTION__,
           static_cast<int>(aWords.Length())));

  MOZ_ASSERT(aWords.Length() == aNodeOffsetRangesForWords.Length());

  // TODO:
  // aOldRangesForSomeWords is sorted in the same order as aWords. Could be used
  // to remove ranges more efficiently.

  if (aWords.IsEmpty()) {
    RemoveRanges(aOldRangesForSomeWords);
    return;
  }

  mInlineSpellChecker.ChangeNumPendingSpellChecks(1);

  RefPtr<mozInlineSpellChecker> inlineSpellChecker = &mInlineSpellChecker;
  RefPtr<Selection> spellCheckerSelection = &mSpellCheckSelection;
  uint32_t token = mInlineSpellChecker.mDisabledAsyncToken;
  mInlineSpellChecker.mSpellCheck->CheckCurrentWordsNoSuggest(aWords)->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [inlineSpellChecker, spellCheckerSelection,
       nodeOffsetRangesForWords = std::move(aNodeOffsetRangesForWords),
       oldRangesForSomeWords = std::move(aOldRangesForSomeWords),
       token](const nsTArray<bool>& aIsMisspelled) {
        if (token != inlineSpellChecker->GetDisabledAsyncToken()) {
          // This result is never used
          return;
        }

        if (!inlineSpellChecker->mEditorBase ||
            inlineSpellChecker->mEditorBase->Destroyed()) {
          return;
        }

        AutoChangeNumPendingSpellChecks pendingChecks(inlineSpellChecker, -1);

        if (inlineSpellChecker->IsSpellCheckSelectionFull()) {
          return;
        }

        inlineSpellChecker->UpdateRangesForMisspelledWords(
            nodeOffsetRangesForWords, oldRangesForSomeWords, aIsMisspelled,
            *spellCheckerSelection);
      },
      [inlineSpellChecker, token](nsresult aRv) {
        if (!inlineSpellChecker->mEditorBase ||
            inlineSpellChecker->mEditorBase->Destroyed()) {
          return;
        }

        if (token != inlineSpellChecker->GetDisabledAsyncToken()) {
          // This result is never used
          return;
        }

        inlineSpellChecker->ChangeNumPendingSpellChecks(-1);
      });
}

// mozInlineSpellChecker::ResumeCheck
//
//    Called by the resume event when it fires. We will try to pick up where
//    the last resume left off.

nsresult mozInlineSpellChecker::ResumeCheck(
    UniquePtr<mozInlineSpellStatus>&& aStatus) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Debug, ("%s", __FUNCTION__));

  // Observers should be notified that spell check has ended only after spell
  // check is done below, but since there are many early returns in this method
  // and the number of pending spell checks must be decremented regardless of
  // whether the spell check actually happens, use this RAII object.
  AutoChangeNumPendingSpellChecks autoChangeNumPending(this, -1);

  if (mPendingDOMMutationRange == aStatus->mRange) {
    mPendingDOMMutationRange = nullptr;
  }

  if (aStatus->IsFullSpellCheck()) {
    // Allow posting new spellcheck resume events from inside
    // ResumeCheck, now that we're actually firing.
    MOZ_ASSERT(mFullSpellCheckScheduled,
               "How could this be false?  The full spell check is "
               "calling us!!");
    mFullSpellCheckScheduled = false;
  }

  if (!mSpellCheck) return NS_OK;  // spell checking has been turned off

  if (!mEditorBase) {
    return NS_OK;
  }

  Maybe<mozInlineSpellWordUtil> wordUtil{
      mozInlineSpellWordUtil::Create(*mEditorBase)};
  if (!wordUtil) {
    return NS_OK;  // editor doesn't like us, don't assert
  }

  RefPtr<Selection> spellCheckSelection = GetSpellCheckSelection();
  if (NS_WARN_IF(!spellCheckSelection)) {
    return NS_ERROR_FAILURE;
  }

  nsTArray<nsCString> currentDictionaries;
  nsresult rv = mSpellCheck->GetCurrentDictionaries(currentDictionaries);
  if (NS_FAILED(rv)) {
    MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Debug,
            ("%s: no active dictionary.", __FUNCTION__));

    // no active dictionary
    for (const uint32_t index :
         Reversed(IntegerRange(spellCheckSelection->RangeCount()))) {
      RefPtr<dom::Range> checkRange = spellCheckSelection->GetRangeAt(index);
      if (MOZ_LIKELY(checkRange)) {
        RemoveRange(spellCheckSelection, checkRange);
      }
    }
    return NS_OK;
  }

  CleanupRangesInSelection(spellCheckSelection);

  rv = aStatus->FinishInitOnEvent(*wordUtil);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!aStatus->mRange) return NS_OK;  // empty range, nothing to do

  bool doneChecking = true;
  if (aStatus->GetOperation() == mozInlineSpellStatus::eOpSelection)
    rv = DoSpellCheckSelection(*wordUtil, spellCheckSelection);
  else
    rv = DoSpellCheck(*wordUtil, spellCheckSelection, aStatus, &doneChecking);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!doneChecking) rv = ScheduleSpellCheck(std::move(aStatus));
  return rv;
}

// mozInlineSpellChecker::IsPointInSelection
//
//    Determines if a given (node,offset) point is inside the given
//    selection. If so, the specific range of the selection that
//    intersects is places in *aRange. (There may be multiple disjoint
//    ranges in a selection.)
//
//    If there is no intersection, *aRange will be nullptr.

// static
nsresult mozInlineSpellChecker::IsPointInSelection(Selection& aSelection,
                                                   nsINode* aNode,
                                                   uint32_t aOffset,
                                                   dom::Range** aRange) {
  *aRange = nullptr;

  nsTArray<dom::Range*> ranges;
  nsresult rv = aSelection.GetDynamicRangesForIntervalArray(
      aNode, aOffset, aNode, aOffset, true, &ranges);
  NS_ENSURE_SUCCESS(rv, rv);

  if (ranges.Length() == 0) return NS_OK;  // no matches

  // there may be more than one range returned, and we don't know what do
  // do with that, so just get the first one
  NS_ADDREF(*aRange = ranges[0]);
  return NS_OK;
}

nsresult mozInlineSpellChecker::CleanupRangesInSelection(
    Selection* aSelection) {
  // integrity check - remove ranges that have collapsed to nothing. This
  // can happen if the node containing a highlighted word was removed.
  if (!aSelection) return NS_ERROR_FAILURE;

  // TODO: Rewrite this with reversed ranged-loop, it might make this simpler.
  int64_t count = aSelection->RangeCount();
  for (int64_t index = 0; index < count; index++) {
    dom::Range* checkRange =
        aSelection->GetRangeAt(static_cast<uint32_t>(index));
    if (MOZ_LIKELY(checkRange)) {
      if (checkRange->Collapsed()) {
        RemoveRange(aSelection, checkRange);
        index--;
        count--;
      }
    }
  }

  return NS_OK;
}

// mozInlineSpellChecker::RemoveRange
//
//    For performance reasons, we have an upper bound on the number of word
//    ranges  in the spell check selection. When removing a range from the
//    selection, we need to decrement mNumWordsInSpellSelection

nsresult mozInlineSpellChecker::RemoveRange(Selection* aSpellCheckSelection,
                                            dom::Range* aRange) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Debug, ("%s", __FUNCTION__));

  NS_ENSURE_ARG_POINTER(aSpellCheckSelection);
  NS_ENSURE_ARG_POINTER(aRange);

  ErrorResult rv;
  RefPtr<dom::Range> range{aRange};
  RefPtr<Selection> selection{aSpellCheckSelection};
  selection->RemoveRangeAndUnselectFramesAndNotifyListeners(*range, rv);
  if (!rv.Failed()) {
    if (mNumWordsInSpellSelection) {
      mNumWordsInSpellSelection--;
    }
  }

  return rv.StealNSResult();
}

struct mozInlineSpellChecker::CompareRangeAndNodeOffsetRange {
  static bool Equals(const RefPtr<dom::Range>& aRange,
                     const NodeOffsetRange& aNodeOffsetRange) {
    return aNodeOffsetRange == *aRange;
  }
};

void mozInlineSpellChecker::UpdateRangesForMisspelledWords(
    const nsTArray<NodeOffsetRange>& aNodeOffsetRangesForWords,
    const nsTArray<RefPtr<dom::Range>>& aOldRangesForSomeWords,
    const nsTArray<bool>& aIsMisspelled, Selection& aSpellCheckerSelection) {
  MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Verbose, ("%s", __FUNCTION__));

  MOZ_ASSERT(aNodeOffsetRangesForWords.Length() == aIsMisspelled.Length());

  // When the spellchecker checks text containing words separated by "/", it may
  // happen that some words checked in one timeslice, are checked again in a
  // following timeslice. E.g. for "foo/baz/qwertz", it may happen that "foo"
  // and "baz" are checked in one timeslice and two ranges are added for them.
  // In the following timeslice "foo" and "baz" are checked again but since
  // their corresponding ranges are already in the spellcheck-Selection
  // they don't have to be added again and since "foo" and "baz" still contain
  // spelling mistakes, they don't have to be removed.
  //
  // In this case, it's more efficient to keep the existing ranges.

  AutoTArray<bool, INLINESPELL_MAXIMUM_CHUNKED_WORDS_PER_TASK>
      oldRangesMarkedForRemoval;
  for (size_t i = 0; i < aOldRangesForSomeWords.Length(); ++i) {
    oldRangesMarkedForRemoval.AppendElement(true);
  }

  AutoTArray<bool, INLINESPELL_MAXIMUM_CHUNKED_WORDS_PER_TASK>
      nodeOffsetRangesMarkedForAdding;
  for (size_t i = 0; i < aNodeOffsetRangesForWords.Length(); ++i) {
    nodeOffsetRangesMarkedForAdding.AppendElement(false);
  }

  for (size_t i = 0; i < aIsMisspelled.Length(); i++) {
    if (!aIsMisspelled[i]) {
      continue;
    }

    const NodeOffsetRange& nodeOffsetRange = aNodeOffsetRangesForWords[i];
    const size_t indexOfOldRangeToKeep = aOldRangesForSomeWords.IndexOf(
        nodeOffsetRange, 0, CompareRangeAndNodeOffsetRange{});
    if (indexOfOldRangeToKeep != aOldRangesForSomeWords.NoIndex &&
        aOldRangesForSomeWords[indexOfOldRangeToKeep]->IsInSelection(
            aSpellCheckerSelection)) {
      /** TODO: warn in case the old range doesn't
        belong to the selection. This is not critical,
        because other code can always remove them
        before the actual spellchecking happens. */
      MOZ_LOG(sInlineSpellCheckerLog, LogLevel::Verbose,
              ("%s: reusing old range.", __FUNCTION__));

      oldRangesMarkedForRemoval[indexOfOldRangeToKeep] = false;
    } else {
      nodeOffsetRangesMarkedForAdding[i] = true;
    }
  }

  for (size_t i = 0; i < oldRangesMarkedForRemoval.Length(); ++i) {
    if (oldRangesMarkedForRemoval[i]) {
      RemoveRange(&aSpellCheckerSelection, aOldRangesForSomeWords[i]);
    }
  }

  // Add ranges after removing the marked old ones, so that the Selection can
  // become full again.
  for (size_t i = 0; i < nodeOffsetRangesMarkedForAdding.Length(); ++i) {
    if (nodeOffsetRangesMarkedForAdding[i]) {
      RefPtr<dom::Range> wordRange =
          mozInlineSpellWordUtil::MakeRange(aNodeOffsetRangesForWords[i]);
      // If we somehow can't make a range for this word, just ignore
      // it.
      if (wordRange) {
        AddRange(&aSpellCheckerSelection, wordRange);
      }
    }
  }
}

// mozInlineSpellChecker::AddRange
//
//    For performance reasons, we have an upper bound on the number of word
//    ranges we'll add to the spell check selection. Once we reach that upper
//    bound, stop adding the ranges

nsresult mozInlineSpellChecker::AddRange(Selection* aSpellCheckSelection,
                                         dom::Range* aRange) {
  NS_ENSURE_ARG_POINTER(aSpellCheckSelection);
  NS_ENSURE_ARG_POINTER(aRange);

  nsresult rv = NS_OK;

  if (!IsSpellCheckSelectionFull()) {
    IgnoredErrorResult err;
    aSpellCheckSelection->AddRangeAndSelectFramesAndNotifyListeners(*aRange,
                                                                    err);
    if (err.Failed()) {
      rv = err.StealNSResult();
    } else {
      mNumWordsInSpellSelection++;
    }
  }

  return rv;
}

already_AddRefed<Selection> mozInlineSpellChecker::GetSpellCheckSelection() {
  if (NS_WARN_IF(!mEditorBase)) {
    return nullptr;
  }
  RefPtr<Selection> selection =
      mEditorBase->GetSelection(SelectionType::eSpellCheck);
  if (!selection) {
    return nullptr;
  }
  return selection.forget();
}

nsresult mozInlineSpellChecker::SaveCurrentSelectionPosition() {
  if (NS_WARN_IF(!mEditorBase)) {
    return NS_OK;  // XXX Why NS_OK?
  }

  // figure out the old caret position based on the current selection
  RefPtr<Selection> selection = mEditorBase->GetSelection();
  if (NS_WARN_IF(!selection)) {
    return NS_ERROR_FAILURE;
  }

  mCurrentSelectionAnchorNode = selection->GetFocusNode();
  mCurrentSelectionOffset = selection->FocusOffset();

  return NS_OK;
}

// mozInlineSpellChecker::HandleNavigationEvent
//
//    Acts upon mouse clicks and keyboard navigation changes, spell checking
//    the previous word if the new navigation location moves us to another
//    word.
//
//    This is complicated by the fact that our mouse events are happening after
//    selection has been changed to account for the mouse click. But keyboard
//    events are happening before the caret selection has changed. Working
//    around this by letting keyboard events setting forceWordSpellCheck to
//    true. aNewPositionOffset also tries to work around this for the
//    DOM_VK_RIGHT and DOM_VK_LEFT cases.

nsresult mozInlineSpellChecker::HandleNavigationEvent(
    bool aForceWordSpellCheck, int32_t aNewPositionOffset) {
  nsresult rv;

  // If we already handled the navigation event and there is no possibility
  // anything has changed since then, we don't have to do anything. This
  // optimization makes a noticeable difference when you hold down a navigation
  // key like Page Down.
  if (!mNeedsCheckAfterNavigation) return NS_OK;

  nsCOMPtr<nsINode> currentAnchorNode = mCurrentSelectionAnchorNode;
  uint32_t currentAnchorOffset = mCurrentSelectionOffset;

  // now remember the new focus position resulting from the event
  rv = SaveCurrentSelectionPosition();
  NS_ENSURE_SUCCESS(rv, rv);

  bool shouldPost;
  Result<UniquePtr<mozInlineSpellStatus>, nsresult> res =
      mozInlineSpellStatus::CreateForNavigation(
          *this, aForceWordSpellCheck, aNewPositionOffset, currentAnchorNode,
          currentAnchorOffset, mCurrentSelectionAnchorNode,
          mCurrentSelectionOffset, &shouldPost);

  if (NS_WARN_IF(res.isErr())) {
    return res.unwrapErr();
  }

  if (shouldPost) {
    rv = ScheduleSpellCheck(res.unwrap());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

NS_IMETHODIMP mozInlineSpellChecker::HandleEvent(Event* aEvent) {
  WidgetEvent* widgetEvent = aEvent->WidgetEventPtr();
  if (MOZ_UNLIKELY(!widgetEvent)) {
    return NS_OK;
  }

  switch (widgetEvent->mMessage) {
    case eBlur:
      OnBlur(*aEvent);
      return NS_OK;
    case ePointerClick:
      OnPointerClick(*aEvent);
      return NS_OK;
    case eKeyDown:
      OnKeyDown(*aEvent);
      return NS_OK;
    default:
      MOZ_ASSERT_UNREACHABLE("You must forgot to handle new event type");
      return NS_OK;
  }
}

void mozInlineSpellChecker::OnBlur(Event& aEvent) {
  // force spellcheck on blur, for instance when tabbing out of a textbox
  HandleNavigationEvent(true);
}

void mozInlineSpellChecker::OnPointerClick(Event& aPointerEvent) {
  MouseEvent* const mouseEvent = aPointerEvent.AsMouseEvent();
  if (MOZ_UNLIKELY(!mouseEvent)) {
    return;
  }

  // ignore any errors from HandleNavigationEvent as we don't want to prevent
  // anyone else from seeing this event.
  HandleNavigationEvent(mouseEvent->Button() != 0);
}

void mozInlineSpellChecker::OnKeyDown(Event& aKeyEvent) {
  WidgetKeyboardEvent* widgetKeyboardEvent =
      aKeyEvent.WidgetEventPtr()->AsKeyboardEvent();
  if (MOZ_UNLIKELY(!widgetKeyboardEvent)) {
    return;
  }

  // we only care about navigation keys that moved selection
  switch (widgetKeyboardEvent->mKeyNameIndex) {
    case KEY_NAME_INDEX_ArrowRight:
      // XXX Does this work with RTL text?
      HandleNavigationEvent(false, 1);
      return;
    case KEY_NAME_INDEX_ArrowLeft:
      // XXX Does this work with RTL text?
      HandleNavigationEvent(false, -1);
      return;
    case KEY_NAME_INDEX_ArrowUp:
    case KEY_NAME_INDEX_ArrowDown:
    case KEY_NAME_INDEX_Home:
    case KEY_NAME_INDEX_End:
    case KEY_NAME_INDEX_PageDown:
    case KEY_NAME_INDEX_PageUp:
      HandleNavigationEvent(true /* force a spelling correction */);
      return;
    default:
      return;
  }
}

// Used as the nsIEditorSpellCheck::UpdateCurrentDictionary callback.
class UpdateCurrentDictionaryCallback final
    : public nsIEditorSpellCheckCallback {
 public:
  NS_DECL_ISUPPORTS

  explicit UpdateCurrentDictionaryCallback(mozInlineSpellChecker* aSpellChecker,
                                           uint32_t aDisabledAsyncToken)
      : mSpellChecker(aSpellChecker),
        mDisabledAsyncToken(aDisabledAsyncToken) {}

  NS_IMETHOD EditorSpellCheckDone() override {
    // Ignore this callback if SetEnableRealTimeSpell(false) was called after
    // the UpdateCurrentDictionary call that triggered it.
    return mSpellChecker->GetDisabledAsyncToken() > mDisabledAsyncToken
               ? NS_OK
               : mSpellChecker->CurrentDictionaryUpdated();
  }

 private:
  ~UpdateCurrentDictionaryCallback() = default;

  RefPtr<mozInlineSpellChecker> mSpellChecker;
  uint32_t mDisabledAsyncToken;
};
NS_IMPL_ISUPPORTS(UpdateCurrentDictionaryCallback, nsIEditorSpellCheckCallback)

NS_IMETHODIMP mozInlineSpellChecker::UpdateCurrentDictionary() {
  // mSpellCheck is null and mPendingSpellCheck is nonnull while the spell
  // checker is being initialized.  Calling UpdateCurrentDictionary on
  // mPendingSpellCheck simply queues the dictionary update after the init.
  RefPtr<EditorSpellCheck> spellCheck =
      mSpellCheck ? mSpellCheck : mPendingSpellCheck;
  if (!spellCheck) {
    return NS_OK;
  }

  RefPtr<UpdateCurrentDictionaryCallback> cb =
      new UpdateCurrentDictionaryCallback(this, mDisabledAsyncToken);
  NS_ENSURE_STATE(cb);
  nsresult rv = spellCheck->UpdateCurrentDictionary(cb);
  if (NS_FAILED(rv)) {
    cb = nullptr;
    return rv;
  }
  mNumPendingUpdateCurrentDictionary++;
  ChangeNumPendingSpellChecks(1);

  return NS_OK;
}

// Called when nsIEditorSpellCheck::UpdateCurrentDictionary completes.
nsresult mozInlineSpellChecker::CurrentDictionaryUpdated() {
  mNumPendingUpdateCurrentDictionary--;
  MOZ_ASSERT(mNumPendingUpdateCurrentDictionary >= 0,
             "CurrentDictionaryUpdated called without corresponding "
             "UpdateCurrentDictionary call!");
  ChangeNumPendingSpellChecks(-1);

  nsresult rv = SpellCheckRange(nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
mozInlineSpellChecker::GetSpellCheckPending(bool* aPending) {
  *aPending = mNumPendingSpellChecks > 0;
  return NS_OK;
}
