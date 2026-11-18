/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_mozInlineSpellChecker_h
#define mozilla_mozInlineSpellChecker_h

#include "nsCycleCollectionParticipant.h"
#include "nsIDOMEventListener.h"
#include "nsIEditorSpellCheck.h"
#include "nsIInlineSpellChecker.h"
#include "mozInlineSpellWordUtil.h"
#include "mozilla/EditorDOMPoint.h"
#include "mozilla/Result.h"
#include "mozilla/dom/Range.h"
#include "nsWeakReference.h"

class InitEditorSpellCheckCallback;
class mozInlineSpellChecker;
class mozInlineSpellResume;
class UpdateCurrentDictionaryCallback;

namespace mozilla {
class EditorBase;
class EditorSpellCheck;
enum class EditSubAction : int32_t;
enum class JoinNodesDirection;

namespace dom {
class Event;
}  // namespace dom
}  // namespace mozilla

class mozInlineSpellStatus {
 public:
  static mozilla::Result<mozilla::UniquePtr<mozInlineSpellStatus>, nsresult>
  CreateForEditorChange(mozInlineSpellChecker& aSpellChecker,
                        mozilla::EditSubAction aEditSubAction,
                        nsINode* aAnchorNode, uint32_t aAnchorOffset,
                        nsINode* aPreviousNode, uint32_t aPreviousOffset,
                        nsINode* aStartNode, uint32_t aStartOffset,
                        nsINode* aEndNode, uint32_t aEndOffset);

  static mozilla::Result<mozilla::UniquePtr<mozInlineSpellStatus>, nsresult>
  CreateForNavigation(mozInlineSpellChecker& aSpellChecker, bool aForceCheck,
                      int32_t aNewPositionOffset, nsINode* aOldAnchorNode,
                      uint32_t aOldAnchorOffset, nsINode* aNewAnchorNode,
                      uint32_t aNewAnchorOffset, bool* aContinue);

  static mozilla::UniquePtr<mozInlineSpellStatus> CreateForSelection(
      mozInlineSpellChecker& aSpellChecker);

  enum class SetAnchorToCaret : bool { No, Yes };
  static mozilla::UniquePtr<mozInlineSpellStatus> CreateForRange(
      mozInlineSpellChecker& aSpellChecker, mozilla::dom::Range* aRange,
      SetAnchorToCaret aSetAnchorToCaret = SetAnchorToCaret::No);

  nsresult FinishInitOnEvent(mozInlineSpellWordUtil& aWordUtil);

  // Return true if we plan to spell-check everything
  bool IsFullSpellCheck() const { return mOp == eOpChange && !mRange; }

  const RefPtr<mozInlineSpellChecker> mSpellChecker;

  enum Operation {
    eOpChange,        // for SpellCheckAfterEditorChange except
                      // deleteSelection
    eOpChangeDelete,  // for SpellCheckAfterEditorChange with
                      // deleteSelection
    eOpNavigation,    // for HandleNavigationEvent
    eOpSelection,     // re-check all misspelled words
    eOpResume
  };

  // See `mOp`.
  Operation GetOperation() const { return mOp; }

  // Used for events where we have already computed the range to use. It can
  // also be nullptr in these cases where we need to check the entire range.
  RefPtr<mozilla::dom::Range> mRange;

  // See `mCreatedRange`.
  const mozilla::dom::Range* GetCreatedRange() const { return mCreatedRange; }

  // See `mNoCheckRange`.
  const mozilla::dom::Range* GetNoCheckRange() const { return mNoCheckRange; }

 private:
  // @param aSpellChecker must be non-nullptr.
  // @param aOp see mOp.
  // @param aRange see mRange.
  // @param aCreatedRange see mCreatedRange.
  // @param aAnchorRange see mAnchorRange.
  // @param aForceNavigationWordCheck see mForceNavigationWordCheck.
  // @param aNewNavigationPositionOffset see mNewNavigationPositionOffset.
  explicit mozInlineSpellStatus(
      mozInlineSpellChecker* aSpellChecker, Operation aOp,
      RefPtr<mozilla::dom::Range>&& aRange,
      RefPtr<mozilla::dom::Range>&& aCreatedRange,
      RefPtr<mozilla::dom::Range>&& aAnchorRange,
      bool aForceNavigationWordCheck, int32_t aNewNavigationPositionOffset,
      SetAnchorToCaret aSetAnchorToCaret = SetAnchorToCaret::No);

  // For resuming a previously started check.
  const Operation mOp;

  //
  // If we happen to know something was inserted, this is that range.
  // Can be nullptr (this only allows an optimization, so not setting doesn't
  // hurt)
  const RefPtr<const mozilla::dom::Range> mCreatedRange;

  // Contains the range computed for the current word. Can be nullptr.
  RefPtr<mozilla::dom::Range> mNoCheckRange;

  // Indicates the position of the cursor for the event (so we can compute
  // mNoCheckRange). It can be nullptr if we don't care about the cursor
  // position (such as for the intial check of everything).
  //
  // For mOp == eOpNavigation, this is the NEW position of the cursor
  RefPtr<const mozilla::dom::Range> mAnchorRange;

  // -----
  // The following members are only for navigation events and are only
  // stored for FinishNavigationEvent to initialize the other members.
  // -----

  // this is the OLD position of the cursor
  RefPtr<mozilla::dom::Range> mOldNavigationAnchorRange;

  // Set when we should force checking the current word. See
  // mozInlineSpellChecker::HandleNavigationEvent for a description of why we
  // have this.
  const bool mForceNavigationWordCheck;

  // Set mAnchorRange to document selection if it is collapsed
  // when spell check occurs.
  SetAnchorToCaret mSetAnchorToCaret;

  // Contains the offset passed in to HandleNavigationEvent
  const int32_t mNewNavigationPositionOffset;

  nsresult FinishNavigationEvent(mozInlineSpellWordUtil& aWordUtil);

  nsresult FillNoCheckRangeFromAnchor(mozInlineSpellWordUtil& aWordUtil);

  mozilla::dom::Document* GetDocument() const;
  static already_AddRefed<mozilla::dom::Range> PositionToCollapsedRange(
      nsINode* aNode, uint32_t aOffset);
};

class mozInlineSpellChecker final : public nsIInlineSpellChecker,
                                    public nsIDOMEventListener,
                                    public nsSupportsWeakReference {
 private:
  friend class mozInlineSpellStatus;
  friend class InitEditorSpellCheckCallback;
  friend class UpdateCurrentDictionaryCallback;
  friend class AutoChangeNumPendingSpellChecks;

  // Access with CanEnableInlineSpellChecking
  enum SpellCheckingState {
    SpellCheck_Uninitialized = -1,
    SpellCheck_NotAvailable = 0,
    SpellCheck_Available = 1
  };
  static SpellCheckingState gCanEnableSpellChecking;

  RefPtr<mozilla::EditorBase> mEditorBase;
  RefPtr<mozilla::EditorSpellCheck> mSpellCheck;
  RefPtr<mozilla::EditorSpellCheck> mPendingSpellCheck;

  int32_t mNumWordsInSpellSelection;
  const int32_t mMaxNumWordsInSpellSelection;

  // we need to keep track of the current text position in the document
  // so we can spell check the old word when the user clicks around the
  // document.
  nsCOMPtr<nsINode> mCurrentSelectionAnchorNode;
  uint32_t mCurrentSelectionOffset = 0;

  // Tracks the number of pending spell checks *and* async operations that may
  // lead to spell checks, like updating the current dictionary.  This is
  // necessary so that observers can know when to wait for spell check to
  // complete.
  int32_t mNumPendingSpellChecks;

  // Shared with the queued status until ResumeCheck starts processing it.
  RefPtr<mozilla::dom::Range> mPendingDOMMutationRange;

  // The number of calls to UpdateCurrentDictionary that haven't finished yet.
  int32_t mNumPendingUpdateCurrentDictionary;

  // This number is incremented each time the spell checker is disabled so that
  // pending scheduled spell checks and UpdateCurrentDictionary calls can be
  // ignored when they finish.
  uint32_t mDisabledAsyncToken;

  // When mPendingSpellCheck is non-null, this is the callback passed when
  // it was initialized.
  RefPtr<InitEditorSpellCheckCallback> mPendingInitEditorSpellCheckCallback;

  // Set when we have spellchecked after the last edit operation. See the
  // commment at the top of the .cpp file for more info.
  bool mNeedsCheckAfterNavigation;

  // Set when we have a pending mozInlineSpellResume which will check
  // the whole document.
  bool mFullSpellCheckScheduled;

  // Set to true when this instance needs to listen to edit actions of
  // the editor.
  bool mIsListeningToEditSubActions;

  class SpellCheckerSlice;

 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIINLINESPELLCHECKER
  NS_DECL_NSIDOMEVENTLISTENER
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(mozInlineSpellChecker,
                                           nsIDOMEventListener)

  mozilla::EditorSpellCheck* GetEditorSpellCheck();

  // See `mDisabledAsyncToken`.
  uint32_t GetDisabledAsyncToken() const { return mDisabledAsyncToken; }

  // returns true if there are any spell checking dictionaries available
  static bool CanEnableInlineSpellChecking();
  // update the cached value whenever the list of available dictionaries changes
  static void UpdateCanEnableInlineSpellChecking();

  mozInlineSpellChecker();

  // spell checks all of the words between two nodes
  nsresult SpellCheckBetweenNodes(nsINode* aStartNode, int32_t aStartOffset,
                                  nsINode* aEndNode, int32_t aEndOffset);

  // examines the dom node in question and returns true if the inline spell
  // checker should skip the node (i.e. the text is inside of a block quote
  // or an e-mail signature...)
  static bool ShouldSpellCheckNode(mozilla::EditorBase* aEditorBase,
                                   nsINode* aNode);

  // spell check the text contained within aRange, potentially scheduling
  // another check in the future if the time threshold is reached
  nsresult ScheduleSpellCheck(
      mozilla::UniquePtr<mozInlineSpellStatus>&& aStatus);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult
  DoSpellCheckSelection(mozInlineSpellWordUtil& aWordUtil,
                        mozilla::dom::Selection* aSpellCheckSelection);

  nsresult DoSpellCheck(mozInlineSpellWordUtil& aWordUtil,
                        mozilla::dom::Selection* aSpellCheckSelection,
                        const mozilla::UniquePtr<mozInlineSpellStatus>& aStatus,
                        bool* aDoneChecking);

  // helper routine to determine if a point is inside of the passed in
  // selection.
  static nsresult IsPointInSelection(mozilla::dom::Selection& aSelection,
                                     nsINode* aNode, uint32_t aOffset,
                                     mozilla::dom::Range** aRange);

  nsresult CleanupRangesInSelection(mozilla::dom::Selection* aSelection);

  /**
   * Spellcheck aStartContent to aEndContent which are children of aContainer
   * (inclusive) without the word at the caret.  This is called when web apps
   * modify the DOM tree directly.
   */
  nsresult SpellCheckNodesIgnoringWordAtCaret(nsINode& aContainer,
                                              nsIContent& aStartContent,
                                              nsIContent& aEndContent);

  /**
   * @param aRange needs to be kept alive by the caller.
   */
  // TODO: annotate with `MOZ_CAN_RUN_SCRIPT` instead
  // (https://bugzilla.mozilla.org/show_bug.cgi?id=1620540).
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult
  RemoveRange(mozilla::dom::Selection* aSpellCheckSelection,
              mozilla::dom::Range* aRange);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult
  AddRange(mozilla::dom::Selection* aSpellCheckSelection,
           mozilla::dom::Range* aRange);
  bool IsSpellCheckSelectionFull() const {
    return mNumWordsInSpellSelection >= mMaxNumWordsInSpellSelection;
  }

  nsresult MakeSpellCheckRange(nsINode* aStartNode, int32_t aStartOffset,
                               nsINode* aEndNode, int32_t aEndOffset,
                               mozilla::dom::Range** aRange) const;

  // DOM and editor event registration helper routines
  nsresult RegisterEventListeners();
  nsresult UnregisterEventListeners();
  nsresult HandleNavigationEvent(bool aForceWordSpellCheck,
                                 int32_t aNewPositionOffset = 0);

  already_AddRefed<mozilla::dom::Selection> GetSpellCheckSelection();
  nsresult SaveCurrentSelectionPosition();

  nsresult ResumeCheck(mozilla::UniquePtr<mozInlineSpellStatus>&& aStatus);

  nsresult SpellCheckAfterEditorChange(mozilla::EditSubAction aEditSubAction,
                                       mozilla::dom::Selection& aSelection,
                                       nsINode* aPreviousSelectedNode,
                                       uint32_t aPreviousSelectedOffset,
                                       nsINode* aStartNode,
                                       uint32_t aStartOffset, nsINode* aEndNode,
                                       uint32_t aEndOffset);

 protected:
  virtual ~mozInlineSpellChecker();

  struct CompareRangeAndNodeOffsetRange;

  // Ensures that all misspelled words have corresponding ranges in
  // aSpellCheckerSelection. Reuses those of the old ranges, which still
  // correspond to misspelled words and adds new ranges for those misspelled
  // words for which no corresponding old range exists.
  // Removes the old ranges which aren't reused from aSpellCheckerSelection.
  //
  // @param aNodeOffsetRangesForWords corresponds to aIsMisspelled.
  //                                  `aNodeOffsetRangesForWords.Length() ==
  //                                  aIsMisspelled.Length()`.
  // @param aOldRangesForSomeWords ranges belonging to aSpellCheckerSelection.
  //                               Its length may differ from
  //                               `aNodeOffsetRangesForWords.Length()`.
  // @param aIsMisspelled indicates which words are misspelled.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void UpdateRangesForMisspelledWords(
      const nsTArray<NodeOffsetRange>& aNodeOffsetRangesForWords,
      const nsTArray<RefPtr<mozilla::dom::Range>>& aOldRangesForSomeWords,
      const nsTArray<bool>& aIsMisspelled,
      mozilla::dom::Selection& aSpellCheckerSelection);

  // called when async nsIEditorSpellCheck methods complete
  nsresult EditorSpellCheckInited();
  nsresult CurrentDictionaryUpdated();

  // track the number of pending spell checks and async operations that may lead
  // to spell checks, notifying observers accordingly
  void ChangeNumPendingSpellChecks(int32_t aDelta,
                                   mozilla::EditorBase* aEditorBase = nullptr);
  void NotifyObservers(const char* aTopic, mozilla::EditorBase* aEditorBase);

  void StartToListenToEditSubActions() { mIsListeningToEditSubActions = true; }
  void EndListeningToEditSubActions() { mIsListeningToEditSubActions = false; }

  void OnBlur(mozilla::dom::Event& aEvent);
  void OnPointerClick(mozilla::dom::Event& aPointerEvent);
  void OnKeyDown(mozilla::dom::Event& aKeyEvent);
};

#endif  // #ifndef mozilla_mozInlineSpellChecker_h
