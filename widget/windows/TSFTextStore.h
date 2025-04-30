/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TSFTextStore_h
#define TSFTextStore_h

#include "nsIWidget.h"
#include "nsWindow.h"

#include "TSFTextStoreBase.h"
#include "TSFUtils.h"
#include "WinUtils.h"
#include "WritingModes.h"

#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TextRange.h"
#include "mozilla/widget/IMEData.h"

#include <msctf.h>
#include <textstor.h>

struct ITfThreadMgr;
struct ITfDocumentMgr;
struct ITfDisplayAttributeMgr;
struct ITfCategoryMgr;
class nsWindow;

namespace mozilla::widget {

class TSFStaticSink;
struct MSGResult;

/*
 * Text Services Framework text store
 */

class TSFTextStore final : public TSFTextStoreBase,
                           public ITfContextOwnerCompositionSink,
                           public ITfMouseTrackerACP {
  friend class TSFStaticSink;

 public: /*IUnknown*/
  STDMETHODIMP QueryInterface(REFIID, void**);

  NS_INLINE_DECL_IUNKNOWN_ADDREF_RELEASE(TSFTextStore);

 public: /*ITextStoreACP*/
  STDMETHODIMP RequestLock(DWORD, HRESULT*);
  STDMETHODIMP QueryInsert(LONG, LONG, ULONG, LONG*, LONG*);
  STDMETHODIMP GetSelection(ULONG, ULONG, TS_SELECTION_ACP*, ULONG*);
  STDMETHODIMP SetSelection(ULONG, const TS_SELECTION_ACP*);
  STDMETHODIMP GetText(LONG, LONG, WCHAR*, ULONG, ULONG*, TS_RUNINFO*, ULONG,
                       ULONG*, LONG*);
  STDMETHODIMP SetText(DWORD, LONG, LONG, const WCHAR*, ULONG, TS_TEXTCHANGE*);
  STDMETHODIMP RequestSupportedAttrs(DWORD, ULONG, const TS_ATTRID*);
  STDMETHODIMP RequestAttrsAtPosition(LONG, ULONG, const TS_ATTRID*, DWORD);
  STDMETHODIMP RetrieveRequestedAttrs(ULONG, TS_ATTRVAL*, ULONG*);
  STDMETHODIMP GetEndACP(LONG*);
  STDMETHODIMP GetACPFromPoint(TsViewCookie, const POINT*, DWORD, LONG*);
  STDMETHODIMP GetTextExt(TsViewCookie, LONG, LONG, RECT*, BOOL*);
  STDMETHODIMP InsertTextAtSelection(DWORD, const WCHAR*, ULONG, LONG*, LONG*,
                                     TS_TEXTCHANGE*);

 public: /*ITfContextOwnerCompositionSink*/
  STDMETHODIMP OnStartComposition(ITfCompositionView*, BOOL*);
  STDMETHODIMP OnUpdateComposition(ITfCompositionView*, ITfRange*);
  STDMETHODIMP OnEndComposition(ITfCompositionView*);

 public: /*ITfMouseTrackerACP*/
  STDMETHODIMP AdviseMouseSink(ITfRangeACP*, ITfMouseSink*, DWORD*);
  STDMETHODIMP UnadviseMouseSink(DWORD);

 public:
  static bool ProcessRawKeyMessage(const MSG& aMsg);
  static void ProcessMessage(nsWindow* aWindow, UINT aMessage, WPARAM& aWParam,
                             LPARAM& aLParam, MSGResult& aResult);

  static void SetIMEOpenState(bool);
  static bool GetIMEOpenState(void);

  static void CommitComposition(bool aDiscard) {
    NS_ASSERTION(TSFUtils::IsAvailable(),
                 "Not in TSF mode, shouldn't be called");
    if (const RefPtr<TSFTextStore> textStore = TSFUtils::GetActiveTextStore()) {
      textStore->CommitCompositionInternal(aDiscard);
    }
  }

  // TODO: Move the following notification receiver methods to TSFUtils because
  // TSFEmptyTextStore might want to receive the notifications in the future.

  static nsresult OnTextChange(const IMENotification& aIMENotification) {
    NS_ASSERTION(TSFUtils::IsAvailable(),
                 "Not in TSF mode, shouldn't be called");
    if (const RefPtr<TSFTextStore> textStore = TSFUtils::GetActiveTextStore()) {
      return textStore->OnTextChangeInternal(aIMENotification);
    }
    return NS_OK;
  }

  static nsresult OnSelectionChange(const IMENotification& aIMENotification) {
    NS_ASSERTION(TSFUtils::IsAvailable(),
                 "Not in TSF mode, shouldn't be called");
    if (const RefPtr<TSFTextStore> textStore = TSFUtils::GetActiveTextStore()) {
      return textStore->OnSelectionChangeInternal(aIMENotification);
    }
    return NS_OK;
  }

  static nsresult OnLayoutChange() {
    NS_ASSERTION(TSFUtils::IsAvailable(),
                 "Not in TSF mode, shouldn't be called");
    if (const RefPtr<TSFTextStore> textStore = TSFUtils::GetActiveTextStore()) {
      return textStore->OnLayoutChangeInternal();
    }
    return NS_OK;
  }

  static nsresult OnUpdateComposition() {
    NS_ASSERTION(TSFUtils::IsAvailable(),
                 "Not in TSF mode, shouldn't be called");
    if (const RefPtr<TSFTextStore> textStore = TSFUtils::GetActiveTextStore()) {
      return textStore->OnUpdateCompositionInternal();
    }
    return NS_OK;
  }

  static nsresult OnMouseButtonEvent(const IMENotification& aIMENotification) {
    NS_ASSERTION(TSFUtils::IsAvailable(),
                 "Not in TSF mode, shouldn't be called");
    if (const RefPtr<TSFTextStore> textStore = TSFUtils::GetActiveTextStore()) {
      return textStore->OnMouseButtonEventInternal(aIMENotification);
    }
    return NS_OK;
  }

  [[nodiscard]] IMENotificationRequests GetIMENotificationRequests()
      const final;

  static bool IsComposing() {
    return TSFUtils::GetActiveTextStore() &&
           TSFUtils::GetActiveTextStore()->mComposition.isSome();
  }

  static bool IsComposingOn(nsWindow* aWidget) {
    return IsComposing() && TSFUtils::GetActiveTextStore()->mWidget == aWidget;
  }

  static nsWindow* GetEnabledWindow() {
    return TSFUtils::GetActiveTextStore()
               ? TSFUtils::GetActiveTextStore()->mWidget.get()
               : nullptr;
  }

  /**
   * Returns true if active keyboard layout is a legacy IMM-IME.
   */
  static bool IsIMM_IMEActive();

  /**
   * Returns true if active TIP is MS-IME for Japanese.
   */
  static bool IsMSJapaneseIMEActive();

  /**
   * Returns true if active TIP is Google Japanese Input.
   * Note that if Google Japanese Input is installed as an IMM-IME,
   * this return false even if Google Japanese Input is active.
   * So, you may need to check IMMHandler::IsGoogleJapaneseInputActive() too.
   */
  static bool IsGoogleJapaneseInputActive();

  /**
   * Returns true if active TIP is ATOK.
   */
  static bool IsATOKActive();

#ifdef DEBUG
  // Returns true when keyboard layout has IME (TIP).
  static bool CurrentKeyboardLayoutHasIME();
#endif  // #ifdef DEBUG

  void Destroy() final;

  [[nodiscard]] static Result<RefPtr<TSFTextStore>, nsresult> CreateAndSetFocus(
      nsWindow* aFocusedWindow, const InputContext& aContext);

 protected:
  TSFTextStore();
  ~TSFTextStore();

  bool Init(nsWindow* aWidget, const InputContext& aContext);
  void ReleaseTSFObjects();

  // This is called immediately after a call of OnLockGranted() of mSink.
  // Note that mLock isn't cleared yet when this is called.
  void DidLockGranted() final;

  // If aDispatchCompositionChangeEvent is true, this method will dispatch
  // compositionchange event if this is called during IME composing.
  // aDispatchCompositionChangeEvent should be true only when this is called
  // from SetSelection.  Because otherwise, the compositionchange event should
  // not be sent from here.
  HRESULT SetSelectionInternal(const TS_SELECTION_ACP*,
                               bool aDispatchCompositionChangeEvent = false);
  bool InsertTextAtSelectionInternal(const nsAString& aInsertStr,
                                     TS_TEXTCHANGE* aTextChange);
  void CommitCompositionInternal(bool);
  HRESULT GetDisplayAttribute(ITfProperty* aAttrProperty, ITfRange* aRange,
                              TF_DISPLAYATTRIBUTE* aResult);
  HRESULT RestartCompositionIfNecessary(ITfRange* pRangeNew = nullptr);
  class Composition;
  HRESULT RestartComposition(Composition& aCurrentComposition,
                             ITfCompositionView* aCompositionView,
                             ITfRange* aNewRange);

  // Following methods record composing action(s) to mPendingActions.
  // They will be flushed FlushPendingActions().
  HRESULT RecordCompositionStartAction(ITfCompositionView* aCompositionView,
                                       ITfRange* aRange,
                                       bool aPreserveSelection);
  HRESULT RecordCompositionStartAction(ITfCompositionView* aCompositionView,
                                       LONG aStart, LONG aLength,
                                       bool aPreserveSelection);
  HRESULT RecordCompositionUpdateAction();
  HRESULT RecordCompositionEndAction();

  void OnLayoutInformationAvailable();

  // FlushPendingActions() performs pending actions recorded in mPendingActions
  // and clear it.
  void FlushPendingActions();
  // MaybeFlushPendingNotifications() performs pending notifications to TSF.
  void MaybeFlushPendingNotifications();

  nsresult OnTextChangeInternal(const IMENotification& aIMENotification);
  nsresult OnSelectionChangeInternal(const IMENotification& aIMENotification);
  nsresult OnMouseButtonEventInternal(const IMENotification& aIMENotification);
  nsresult OnLayoutChangeInternal();
  nsresult OnUpdateCompositionInternal();

  // mPendingSelectionChangeData stores selection change data until notifying
  // TSF of selection change.  If two or more selection changes occur, this
  // stores the latest selection change data because only it is necessary.
  Maybe<SelectionChangeData> mPendingSelectionChangeData;

  // mPendingTextChangeData stores one or more text change data until notifying
  // TSF of text change.  If two or more text changes occur, this merges
  // every text change data.
  TextChangeData mPendingTextChangeData;

  void NotifyTSFOfTextChange();
  void NotifyTSFOfSelectionChange();
  bool NotifyTSFOfLayoutChange();
  void NotifyTSFOfLayoutChangeAgain();

  // Creates native caret over our caret.  This method only works on desktop
  // application.  Otherwise, this does nothing.
  void CreateNativeCaret();
  // Destroys native caret if there is.
  void MaybeDestroyNativeCaret();

  /**
   * MaybeHackNoErrorLayoutBugs() is a helper method of GetTextExt().  In
   * strictly speaking, TSF is aware of asynchronous layout computation like us.
   * However, Windows 10 version 1803 and older (including Windows 8.1 and
   * older) Windows has a bug which is that the caller of GetTextExt() of TSF
   * does not return TS_E_NOLAYOUT to TIP as is.  Additionally, even after
   * fixing this bug, some TIPs are not work well when we return TS_E_NOLAYOUT.
   * For avoiding this issue, this method checks current Windows version and
   * active TIP, and if in case we cannot return TS_E_NOLAYOUT, this modifies
   * aACPStart and aACPEnd to making sure that they are in range of unmodified
   * characters.
   *
   * @param aACPStart   Initial value should be acpStart of GetTextExt().
   *                    If this method returns true, this may be modified
   *                    to be in range of unmodified characters.
   * @param aACPEnd     Initial value should be acpEnd of GetTextExt().
   *                    If this method returns true, this may be modified
   *                    to be in range of unmodified characters.
   *                    And also this may become same as aACPStart.
   * @return            true if the caller shouldn't return TS_E_NOLAYOUT.
   *                    In this case, this method modifies aACPStart and/or
   *                    aACPEnd to compute rectangle of unmodified characters.
   *                    false if the caller can return TS_E_NOLAYOUT or
   *                    we cannot have proper unmodified characters.
   */
  bool MaybeHackNoErrorLayoutBugs(LONG& aACPStart, LONG& aACPEnd);

  uint32_t mHandlingKeyMessage = 0;
  void OnStartToHandleKeyMessage() {
    // If we're starting to handle another key message during handling a
    // key message, let's assume that the handling key message is handled by
    // TIP and it sends another key message for hacking something.
    // Let's try to dispatch a keyboard event now.
    // FYI: All callers of this method grab this instance with local variable.
    //      So, even after calling MaybeDispatchKeyboardEventAsProcessedByIME(),
    //      we're safe to access any members.
    if (!mDestroyed && sHandlingKeyMsg && !sIsKeyboardEventDispatched) {
      MaybeDispatchKeyboardEventAsProcessedByIME();
    }
    ++mHandlingKeyMessage;
  }
  void OnEndHandlingKeyMessage(bool aIsProcessedByTSF) {
    // If sHandlingKeyMsg has been handled by TSF or TIP and we're still
    // alive, but we haven't dispatch keyboard event for it, let's fire it now.
    // FYI: All callers of this method grab this instance with local variable.
    //      So, even after calling MaybeDispatchKeyboardEventAsProcessedByIME(),
    //      we're safe to access any members.
    if (!mDestroyed && sHandlingKeyMsg && aIsProcessedByTSF &&
        !sIsKeyboardEventDispatched) {
      MaybeDispatchKeyboardEventAsProcessedByIME();
    }
    MOZ_ASSERT(mHandlingKeyMessage);
    if (--mHandlingKeyMessage) {
      return;
    }
    // If TSFTextStore instance is destroyed during handling key message(s),
    // release all TSF objects when all nested key messages have been handled.
    if (mDestroyed) {
      ReleaseTSFObjects();
    }
  }

  /**
   * MaybeDispatchKeyboardEventAsProcessedByIME() tries to dispatch eKeyDown
   * event or eKeyUp event for sHandlingKeyMsg and marking the dispatching
   * event as "processed by IME".  Note that if the document is locked, this
   * just adds a pending action into the queue and sets
   * sIsKeyboardEventDispatched to true.
   */
  void MaybeDispatchKeyboardEventAsProcessedByIME();

  /**
   * DispatchKeyboardEventAsProcessedByIME() dispatches an eKeyDown or
   * eKeyUp event with NativeKey class and aMsg.
   */
  void DispatchKeyboardEventAsProcessedByIME(const MSG& aMsg);

  // Composition class stores a copy of the active composition string.  Only
  // the data is updated during an InsertTextAtSelection call if we have a
  // composition.  The data acts as a buffer until OnUpdateComposition is
  // called and the data is flushed to editor through eCompositionChange.
  // This allows all changes to be updated in batches to avoid inconsistencies
  // and artifacts.
  class Composition final : public OffsetAndData<LONG> {
   public:
    explicit Composition(ITfCompositionView* aCompositionView,
                         LONG aCompositionStartOffset,
                         const nsAString& aCompositionString)
        : OffsetAndData<LONG>(aCompositionStartOffset, aCompositionString),
          mView(aCompositionView) {}

    ITfCompositionView* GetView() const { return mView; }

    friend std::ostream& operator<<(std::ostream& aStream,
                                    const Composition& aComposition) {
      aStream << "{ mView=0x" << aComposition.mView.get()
              << ", OffsetAndData<LONG>="
              << static_cast<const OffsetAndData<LONG>&>(aComposition) << " }";
      return aStream;
    }

   private:
    RefPtr<ITfCompositionView> const mView;
  };
  // While the document is locked, we cannot dispatch any events which cause
  // DOM events since the DOM events' handlers may modify the locked document.
  // However, even while the document is locked, TSF may queries us.
  // For that, TSFTextStore modifies mComposition even while the document is
  // locked.  With mComposition, query methods can returns the text content
  // information.
  Maybe<Composition> mComposition;

  class Selection {
   public:
    bool HasRange() const { return mACP.isSome(); }
    const TS_SELECTION_ACP& ACPRef() const { return mACP.ref(); }

    explicit Selection(const TS_SELECTION_ACP& aSelection) {
      SetSelection(aSelection);
    }

    explicit Selection(uint32_t aOffsetToCollapse) {
      Collapse(aOffsetToCollapse);
    }

    explicit Selection(const SelectionChangeDataBase& aSelectionChangeData) {
      SetSelection(aSelectionChangeData);
    }

    explicit Selection(const WidgetQueryContentEvent& aQuerySelectionEvent) {
      SetSelection(aQuerySelectionEvent);
    }

    Selection(uint32_t aStart, uint32_t aLength, bool aReversed,
              const WritingMode& aWritingMode) {
      SetSelection(aStart, aLength, aReversed, aWritingMode);
    }

    void SetSelection(const TS_SELECTION_ACP& aSelection) {
      mACP = Some(aSelection);
      // Selection end must be active in our editor.
      if (mACP->style.ase != TS_AE_START) {
        mACP->style.ase = TS_AE_END;
      }
      // We're not support interim char selection for now.
      // XXX Probably, this is necessary for supporting South Asian languages.
      mACP->style.fInterimChar = FALSE;
    }

    bool SetSelection(const SelectionChangeDataBase& aSelectionChangeData) {
      MOZ_ASSERT(aSelectionChangeData.IsInitialized());
      if (!aSelectionChangeData.HasRange()) {
        if (mACP.isNothing()) {
          return false;
        }
        mACP.reset();
        // Let's keep the WritingMode because users don't want to change the UI
        // of TIP temporarily since no selection case is created only by web
        // apps, but they or TIP would restore selection at last point later.
        return true;
      }
      return SetSelection(aSelectionChangeData.mOffset,
                          aSelectionChangeData.Length(),
                          aSelectionChangeData.mReversed,
                          aSelectionChangeData.GetWritingMode());
    }

    bool SetSelection(const WidgetQueryContentEvent& aQuerySelectionEvent) {
      MOZ_ASSERT(aQuerySelectionEvent.mMessage == eQuerySelectedText);
      MOZ_ASSERT(aQuerySelectionEvent.Succeeded());
      if (aQuerySelectionEvent.DidNotFindSelection()) {
        if (mACP.isNothing()) {
          return false;
        }
        mACP.reset();
        // Let's keep the WritingMode because users don't want to change the UI
        // of TIP temporarily since no selection case is created only by web
        // apps, but they or TIP would restore selection at last point later.
        return true;
      }
      return SetSelection(aQuerySelectionEvent.mReply->StartOffset(),
                          aQuerySelectionEvent.mReply->DataLength(),
                          aQuerySelectionEvent.mReply->mReversed,
                          aQuerySelectionEvent.mReply->WritingModeRef());
    }

    bool SetSelection(uint32_t aStart, uint32_t aLength, bool aReversed,
                      const WritingMode& aWritingMode) {
      const bool changed = mACP.isNothing() ||
                           mACP->acpStart != static_cast<LONG>(aStart) ||
                           mACP->acpEnd != static_cast<LONG>(aStart + aLength);
      mACP = Some(
          TS_SELECTION_ACP{.acpStart = static_cast<LONG>(aStart),
                           .acpEnd = static_cast<LONG>(aStart + aLength),
                           .style = {.ase = aReversed ? TS_AE_START : TS_AE_END,
                                     .fInterimChar = FALSE}});
      mWritingMode = aWritingMode;

      return changed;
    }

    bool Collapsed() const {
      return mACP.isNothing() || mACP->acpStart == mACP->acpEnd;
    }

    void Collapse(uint32_t aOffset) {
      // XXX This does not update the selection's mWritingMode.
      // If it is ever used to "collapse" to an entirely new location,
      // we may need to fix that.
      mACP = Some(
          TS_SELECTION_ACP{.acpStart = static_cast<LONG>(aOffset),
                           .acpEnd = static_cast<LONG>(aOffset),
                           .style = {.ase = TS_AE_END, .fInterimChar = FALSE}});
    }

    LONG MinOffset() const {
      MOZ_ASSERT(mACP.isSome());
      LONG min = std::min(mACP->acpStart, mACP->acpEnd);
      MOZ_ASSERT(min >= 0);
      return min;
    }

    LONG MaxOffset() const {
      MOZ_ASSERT(mACP.isSome());
      LONG max = std::max(mACP->acpStart, mACP->acpEnd);
      MOZ_ASSERT(max >= 0);
      return max;
    }

    LONG StartOffset() const {
      MOZ_ASSERT(mACP.isSome());
      MOZ_ASSERT(mACP->acpStart >= 0);
      return mACP->acpStart;
    }

    LONG EndOffset() const {
      MOZ_ASSERT(mACP.isSome());
      MOZ_ASSERT(mACP->acpEnd >= 0);
      return mACP->acpEnd;
    }

    LONG Length() const {
      MOZ_ASSERT_IF(mACP.isSome(), mACP->acpEnd >= mACP->acpStart);
      return mACP.isSome() ? std::abs(mACP->acpEnd - mACP->acpStart) : 0;
    }

    bool IsReversed() const {
      return mACP.isSome() && mACP->style.ase == TS_AE_START;
    }

    TsActiveSelEnd ActiveSelEnd() const {
      return mACP.isSome() ? mACP->style.ase : TS_AE_NONE;
    }

    bool IsInterimChar() const {
      return mACP.isSome() && mACP->style.fInterimChar != FALSE;
    }

    const WritingMode& WritingModeRef() const { return mWritingMode; }

    bool EqualsExceptDirection(const TS_SELECTION_ACP& aACP) const {
      if (mACP.isNothing()) {
        return false;
      }
      if (mACP->style.ase == aACP.style.ase) {
        return mACP->acpStart == aACP.acpStart && mACP->acpEnd == aACP.acpEnd;
      }
      return mACP->acpStart == aACP.acpEnd && mACP->acpEnd == aACP.acpStart;
    }

    bool EqualsExceptDirection(
        const SelectionChangeDataBase& aChangedSelection) const {
      MOZ_ASSERT(aChangedSelection.IsInitialized());
      if (mACP.isNothing()) {
        return aChangedSelection.HasRange();
      }
      return aChangedSelection.Length() == static_cast<uint32_t>(Length()) &&
             aChangedSelection.mOffset == static_cast<uint32_t>(StartOffset());
    }

    friend std::ostream& operator<<(std::ostream& aStream,
                                    const Selection& aSelection) {
      aStream << "{ mACP=" << ToString(aSelection.mACP).c_str()
              << ", mWritingMode=" << ToString(aSelection.mWritingMode).c_str()
              << ",  Collapsed()="
              << (aSelection.Collapsed() ? "true" : "false")
              << ", Length=" << aSelection.Length() << " }";
      return aStream;
    }

   private:
    Maybe<TS_SELECTION_ACP> mACP;  // If Nothing, there is no selection
    WritingMode mWritingMode;
  };
  // Don't access mSelection directly.  Instead, Use SelectionForTSFRef().
  // This is modified immediately when TSF requests to set selection and not
  // updated by selection change in content until mContentForTSF is cleared.
  Maybe<Selection> mSelectionForTSF;

  /**
   * Get the selection expected by TSF.  If mSelectionForTSF is already valid,
   * this just return the reference to it.  Otherwise, this initializes it
   * with eQuerySelectedText.  Please check if the result is valid before
   * actually using it.
   * Note that this is also called by ContentForTSF().
   */
  Maybe<Selection>& SelectionForTSF();

  Maybe<WritingMode> GetWritingMode() final {
    const Maybe<Selection>& selectionForTSF = SelectionForTSF();
    if (selectionForTSF.isNothing()) {
      return Nothing();
    }
    return Some(selectionForTSF->WritingModeRef());
  };

  struct PendingAction final {
    enum class Type : uint8_t {
      CompositionStart,
      CompositionUpdate,
      CompositionEnd,
      SetSelection,
      KeyboardEvent,
    };
    Type mType;
    // For eCompositionStart, eCompositionEnd and eSetSelection
    LONG mSelectionStart;
    // For eCompositionStart and eSetSelection
    LONG mSelectionLength;
    // For eCompositionStart, eCompositionUpdate and eCompositionEnd
    nsString mData;
    // For eCompositionUpdate
    RefPtr<TextRangeArray> mRanges;
    // For eKeyboardEvent
    MSG mKeyMsg;
    // For eSetSelection
    bool mSelectionReversed;
    // For eCompositionUpdate
    bool mIncomplete;
    // For eCompositionStart
    bool mAdjustSelection;
  };
  // Items of mPendingActions are appended when TSF tells us to need to dispatch
  // DOM composition events.  However, we cannot dispatch while the document is
  // locked because it can cause modifying the locked document.  So, the pending
  // actions should be performed when document lock is unlocked.
  nsTArray<PendingAction> mPendingActions;

  PendingAction* LastOrNewPendingCompositionUpdate() {
    if (!mPendingActions.IsEmpty()) {
      PendingAction& lastAction = mPendingActions.LastElement();
      if (lastAction.mType == PendingAction::Type::CompositionUpdate) {
        return &lastAction;
      }
    }
    PendingAction* newAction = mPendingActions.AppendElement();
    newAction->mType = PendingAction::Type::CompositionUpdate;
    newAction->mRanges = new TextRangeArray();
    newAction->mIncomplete = true;
    return newAction;
  }

  /**
   * IsLastPendingActionCompositionEndAt() checks whether the previous pending
   * action is committing composition whose range starts from aStart and its
   * length is aLength.  In other words, this checks whether new composition
   * which will replace same range as previous pending commit can be merged
   * with the previous composition.
   *
   * @param aStart              The inserted offset you expected.
   * @param aLength             The inserted text length you expected.
   * @return                    true if the last pending action is
   *                            eCompositionEnd and it inserted the text
   *                            between aStart and aStart + aLength.
   */
  bool IsLastPendingActionCompositionEndAt(LONG aStart, LONG aLength) const {
    if (mPendingActions.IsEmpty()) {
      return false;
    }
    const PendingAction& pendingLastAction = mPendingActions.LastElement();
    return pendingLastAction.mType == PendingAction::Type::CompositionEnd &&
           pendingLastAction.mSelectionStart == aStart &&
           pendingLastAction.mData.Length() == static_cast<ULONG>(aLength);
  }

  bool IsPendingCompositionUpdateIncomplete() const {
    if (mPendingActions.IsEmpty()) {
      return false;
    }
    const PendingAction& lastAction = mPendingActions.LastElement();
    return lastAction.mType == PendingAction::Type::CompositionUpdate &&
           lastAction.mIncomplete;
  }

  void CompleteLastActionIfStillIncomplete() {
    if (!IsPendingCompositionUpdateIncomplete()) {
      return;
    }
    RecordCompositionUpdateAction();
  }

  void RemoveLastCompositionUpdateActions() {
    while (!mPendingActions.IsEmpty()) {
      const PendingAction& lastAction = mPendingActions.LastElement();
      if (lastAction.mType != PendingAction::Type::CompositionUpdate) {
        break;
      }
      mPendingActions.RemoveLastElement();
    }
  }

  // When On*Composition() is called without document lock, we need to flush
  // the recorded actions at quitting the method.
  // AutoPendingActionAndContentFlusher class is useful for it.
  class MOZ_STACK_CLASS AutoPendingActionAndContentFlusher final {
   public:
    explicit AutoPendingActionAndContentFlusher(TSFTextStore* aTextStore)
        : mTextStore(aTextStore) {
      MOZ_ASSERT(!mTextStore->mIsRecordingActionsWithoutLock);
      if (!mTextStore->IsReadWriteLocked()) {
        mTextStore->mIsRecordingActionsWithoutLock = true;
      }
    }

    ~AutoPendingActionAndContentFlusher() {
      if (!mTextStore->mIsRecordingActionsWithoutLock) {
        return;
      }
      mTextStore->FlushPendingActions();
      mTextStore->mIsRecordingActionsWithoutLock = false;
    }

   private:
    AutoPendingActionAndContentFlusher() {}

    RefPtr<TSFTextStore> mTextStore;
  };

  class Content final {
   public:
    Content(TSFTextStore& aTSFTextStore, const nsAString& aText)
        : mText(aText),
          mLastComposition(aTSFTextStore.mComposition),
          mComposition(aTSFTextStore.mComposition),
          mSelection(aTSFTextStore.mSelectionForTSF) {}

    void OnLayoutChanged() { mMinModifiedOffset.reset(); }

    // OnCompositionEventsHandled() is called when all pending composition
    // events are handled in the focused content which may be in a remote
    // process.
    void OnCompositionEventsHandled() { mLastComposition = mComposition; }

    const nsDependentSubstring GetSelectedText() const;
    const nsDependentSubstring GetSubstring(uint32_t aStart,
                                            uint32_t aLength) const;
    void ReplaceSelectedTextWith(const nsAString& aString);
    void ReplaceTextWith(LONG aStart, LONG aLength,
                         const nsAString& aReplaceString);

    void StartComposition(ITfCompositionView* aCompositionView,
                          const PendingAction& aCompStart,
                          bool aPreserveSelection);
    /**
     * RestoreCommittedComposition() restores the committed string as
     * composing string.  If InsertTextAtSelection() or something is called
     * before a call of OnStartComposition() or previous composition is
     * committed and new composition is restarted to clean up the commited
     * string, there is a pending compositionend.  In this case, we need to
     * cancel the pending compositionend and continue the composition.
     *
     * @param aCompositionView          The composition view.
     * @param aCanceledCompositionEnd   The pending compositionend which is
     *                                  canceled for restarting the composition.
     */
    void RestoreCommittedComposition(
        ITfCompositionView* aCompositionView,
        const PendingAction& aCanceledCompositionEnd);
    void EndComposition(const PendingAction& aCompEnd);

    const nsString& TextRef() const { return mText; }
    const Maybe<OffsetAndData<LONG>>& LastComposition() const {
      return mLastComposition;
    }
    const Maybe<uint32_t>& MinModifiedOffset() const {
      return mMinModifiedOffset;
    }
    const Maybe<StartAndEndOffsets<LONG>>& LatestCompositionRange() const {
      return mLatestCompositionRange;
    }

    // Returns true if layout of the character at the aOffset has not been
    // calculated.
    bool IsLayoutChangedAt(uint32_t aOffset) const {
      return IsLayoutChanged() && (mMinModifiedOffset.value() <= aOffset);
    }
    // Returns true if layout of the content has been changed, i.e., the new
    // layout has not been calculated.
    bool IsLayoutChanged() const { return mMinModifiedOffset.isSome(); }
    bool HasOrHadComposition() const {
      return mLatestCompositionRange.isSome();
    }

    Maybe<TSFTextStore::Composition>& Composition() { return mComposition; }
    Maybe<TSFTextStore::Selection>& Selection() { return mSelection; }

    friend std::ostream& operator<<(std::ostream& aStream,
                                    const Content& aContent) {
      aStream << "{ mText="
              << PrintStringDetail(aContent.mText,
                                   PrintStringDetail::kMaxLengthForEditor)
                     .get()
              << ", mLastComposition=" << aContent.mLastComposition
              << ", mLatestCompositionRange="
              << aContent.mLatestCompositionRange
              << ", mMinModifiedOffset=" << aContent.mMinModifiedOffset << " }";
      return aStream;
    }

   private:
    nsString mText;

    // mLastComposition may store the composition string and its start offset
    // when the document is locked. This is necessary to compute
    // mMinTextModifiedOffset.
    Maybe<OffsetAndData<LONG>> mLastComposition;

    Maybe<TSFTextStore::Composition>& mComposition;
    Maybe<TSFTextStore::Selection>& mSelection;

    // The latest composition's start and end offset.
    Maybe<StartAndEndOffsets<LONG>> mLatestCompositionRange;

    // The minimum offset of modified part of the text.
    Maybe<uint32_t> mMinModifiedOffset;
  };
  // mContentForTSF is cache of content.  The information is expected by TSF
  // and TIP.  Therefore, this is useful for answering the query from TSF or
  // TIP.
  // This is initialized by ContentForTSF() automatically (therefore, don't
  // access this member directly except at calling Clear(), IsInitialized(),
  // IsLayoutChangeAfter() or IsLayoutChanged()).
  // This is cleared when:
  //  - When there is no composition, the document is unlocked.
  //  - When there is a composition, all dispatched events are handled by
  //    the focused editor which may be in a remote process.
  // So, if two compositions are created very quickly, this cache may not be
  // cleared between eCompositionCommit(AsIs) and eCompositionStart.
  Maybe<Content> mContentForTSF;

  Maybe<Content>& ContentForTSF();

  class MOZ_STACK_CLASS AutoNotifyingTSFBatch final {
   public:
    explicit AutoNotifyingTSFBatch(TSFTextStore& aTextStore)
        : mTextStore(aTextStore), mOldValue(aTextStore.mDeferNotifyingTSF) {
      mTextStore.mDeferNotifyingTSF = true;
    }
    ~AutoNotifyingTSFBatch() {
      mTextStore.mDeferNotifyingTSF = mOldValue;
      mTextStore.MaybeFlushPendingNotifications();
    }

   private:
    TSFTextStore& mTextStore;
    bool mOldValue;
  };

  // CanAccessActualContentDirectly() returns true when TSF/TIP can access
  // actual content directly.  In other words, mContentForTSF and/or
  // mSelectionForTSF doesn't cache content or they matches with actual
  // contents due to no pending text/selection change notifications.
  bool CanAccessActualContentDirectly() const;

  // While mContentForTSF is valid, this returns the text stored by it.
  // Otherwise, return the current text content retrieved by eQueryTextContent.
  enum class AllowToFlushLayoutIfNoCache { No, Yes };
  bool GetCurrentText(nsAString& aTextContent,
                      AllowToFlushLayoutIfNoCache aAllowToFlushLayoutIfNoCache);

  class MouseTracker final {
   public:
    static const DWORD kInvalidCookie = static_cast<DWORD>(-1);

    MouseTracker();

    HRESULT Init(TSFTextStore* aTextStore);
    HRESULT AdviseSink(TSFTextStore* aTextStore, ITfRangeACP* aTextRange,
                       ITfMouseSink* aMouseSink);
    void UnadviseSink();

    bool IsUsing() const { return mSink != nullptr; }
    DWORD Cookie() const { return mCookie; }
    bool OnMouseButtonEvent(ULONG aEdge, ULONG aQuadrant, DWORD aButtonStatus);
    Maybe<StartAndEndOffsets<LONG>> Range() const { return mRange; }

   private:
    RefPtr<ITfMouseSink> mSink;
    Maybe<StartAndEndOffsets<LONG>> mRange;
    DWORD mCookie;
  };
  // mMouseTrackers is an array to store each information of installed
  // ITfMouseSink instance.
  nsTArray<MouseTracker> mMouseTrackers;

  // If edit actions are being recorded without document lock, this is true.
  // Otherwise, false.
  bool mIsRecordingActionsWithoutLock = false;
  // If GetTextExt() or GetACPFromPoint() is called and the layout hasn't been
  // calculated yet, these methods return TS_E_NOLAYOUT.  At that time,
  // mHasReturnedNoLayoutError is set to true.
  bool mHasReturnedNoLayoutError = false;
  // When we need to create native caret with the latest selection, but we're
  // initializing selection, this is set to true.
  bool mPendingToCreateNativeCaret = false;
  // If this is false, MaybeFlushPendingNotifications() will clear the
  // mContentForTSF.
  bool mDeferClearingContentForTSF = false;
  // While the document is locked, committing composition always fails since
  // TSF needs another document lock for modifying the composition, selection
  // and etc.  So, committing composition should be performed after the
  // document is unlocked.
  bool mDeferCommittingComposition = false;
  bool mDeferCancellingComposition = false;
  // Debug flag to check whether we're initializing mContentForTSF and
  // mSelectionForTSF.
  bool mIsInitializingContentForTSF = false;
  bool mIsInitializingSelectionForTSF = false;

 private:
  // Handling key message.
  static const MSG* sHandlingKeyMsg;

  // true if an eKeyDown or eKeyUp event for sHandlingKeyMsg has already
  // been dispatched.
  static bool sIsKeyboardEventDispatched;
};

}  // namespace mozilla::widget

#endif  // #ifndef TSFTextStore_h
