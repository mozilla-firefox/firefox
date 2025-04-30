/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TSFTextStore.h"

#include "IMMHandler.h"
#include "KeyboardLayout.h"
#include "TSFStaticSink.h"
#include "TSFUtils.h"
#include "WinIMEHandler.h"
#include "WinMessages.h"
#include "WinUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_intl.h"
#include "mozilla/glean/WidgetWindowsMetrics.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/TextEvents.h"
#include "mozilla/ToString.h"
#include "mozilla/WindowsVersion.h"
#include "nsWindow.h"

#include <algorithm>
#include <comutil.h>  // for _bstr_t
#include <oleauto.h>  // for SysAllocString
#include <olectl.h>

// For collecting other people's log, tell `MOZ_LOG=IMEHandler:4,sync`
// rather than `MOZ_LOG=IMEHandler:5,sync` since using `5` may create too
// big file.
// Therefore you shouldn't use `LogLevel::Verbose` for logging usual behavior.
extern mozilla::LazyLogModule gIMELog;  // defined in TSFUtils.cpp

namespace mozilla::widget {

/**
 * TSF related code should log its behavior even on release build especially
 * in the interface methods.
 *
 * In interface methods, use LogLevel::Info.
 * In internal methods, use LogLevel::Debug for logging normal behavior.
 * For logging error, use LogLevel::Error.
 *
 * When an instance method is called, start with following text:
 *   "0x%p TSFFoo::Bar(", the 0x%p should be the "this" of the nsFoo.
 * after that, start with:
 *   "0x%p   TSFFoo::Bar("
 * In an internal method, start with following text:
 *   "0x%p   TSFFoo::Bar("
 * When a static method is called, start with following text:
 *   "TSFFoo::Bar("
 */

/******************************************************************/
/* TSFTextStore                                                   */
/******************************************************************/

const MSG* TSFTextStore::sHandlingKeyMsg = nullptr;
bool TSFTextStore::sIsKeyboardEventDispatched = false;

TSFTextStore::TSFTextStore() : TSFTextStoreBase(Editable::Yes) {
  // We hope that 5 or more actions don't occur at once.
  mPendingActions.SetCapacity(5);

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::TSFTextStore() SUCCEEDED", this));
}

TSFTextStore::~TSFTextStore() {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore instance is destroyed", this));
}

bool TSFTextStore::Init(nsWindow* aWidget, const InputContext& aContext) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::Init(aWidget=0x%p)", this, aWidget));

  if (NS_WARN_IF(!InitBase(aWidget, aContext))) {
    return false;
  }

  // Create document manager
  const RefPtr<ITfThreadMgr> threadMgr = TSFUtils::GetThreadMgr();
  RefPtr<ITfDocumentMgr> documentMgr;
  HRESULT hr = threadMgr->CreateDocumentMgr(getter_AddRefs(documentMgr));
  if (NS_WARN_IF(FAILED(hr))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::Init() FAILED to create ITfDocumentMgr "
             "(0x%08lX)",
             this, hr));
    return false;
  }
  if (NS_WARN_IF(mDestroyed)) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStore::Init() FAILED to create ITfDocumentMgr due to "
         "TextStore being destroyed during calling "
         "ITfThreadMgr::CreateDocumentMgr()",
         this));
    return false;
  }
  // Create context and add it to document manager
  RefPtr<ITfContext> context;
  hr = documentMgr->CreateContext(TSFUtils::ClientId(), 0,
                                  static_cast<ITextStoreACP*>(this),
                                  getter_AddRefs(context), &mEditCookie);
  if (NS_WARN_IF(FAILED(hr))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::Init() FAILED to create the context "
             "(0x%08lX)",
             this, hr));
    return false;
  }
  if (NS_WARN_IF(mDestroyed)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::Init() FAILED to create ITfContext due to "
             "TextStore being destroyed during calling "
             "ITfDocumentMgr::CreateContext()",
             this));
    return false;
  }

  hr = documentMgr->Push(context);
  if (NS_WARN_IF(FAILED(hr))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::Init() FAILED to push the context (0x%08lX)",
             this, hr));
    return false;
  }
  if (NS_WARN_IF(mDestroyed)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::Init() FAILED to create ITfContext due to "
             "TextStore being destroyed during calling ITfDocumentMgr::Push()",
             this));
    documentMgr->Pop(TF_POPF_ALL);
    return false;
  }

  mDocumentMgr = documentMgr;
  mContext = context;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::Init() succeeded: "
           "mDocumentMgr=0x%p, mContext=0x%p, mEditCookie=0x%08lX",
           this, mDocumentMgr.get(), mContext.get(), mEditCookie));

  return true;
}

void TSFTextStore::Destroy() {
  if (mBeingDestroyed) {
    return;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::Destroy(), mLock=%s, "
           "mComposition=%s, mHandlingKeyMessage=%u",
           this, AutoLockFlagsCString(mLock).get(),
           ToString(mComposition).c_str(), mHandlingKeyMessage));

  mDestroyed = true;

  // Destroy native caret first because it's not directly related to TSF and
  // there may be another textstore which gets focus.  So, we should avoid
  // to destroy caret after the new one recreates caret.
  IMEHandler::MaybeDestroyNativeCaret();

  if (mLock) {
    mPendingDestroy = true;
    return;
  }

  AutoRestore<bool> savedBeingDestroyed(mBeingDestroyed);
  mBeingDestroyed = true;

  // If there is composition, TSF keeps the composition even after the text
  // store destroyed.  So, we should clear the composition here.
  if (mComposition.isSome()) {
    CommitCompositionInternal(false);
  }

  if (mSink) {
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::Destroy(), calling "
             "ITextStoreACPSink::OnLayoutChange(TS_LC_DESTROY)...",
             this));
    RefPtr<ITextStoreACPSink> sink = mSink;
    sink->OnLayoutChange(TS_LC_DESTROY, TSFUtils::sDefaultView);
  }

  // If this is called during handling a keydown or keyup message, we should
  // put off to release TSF objects until it completely finishes since
  // MS-IME for Japanese refers some objects without grabbing them.
  if (!mHandlingKeyMessage) {
    ReleaseTSFObjects();
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::Destroy() succeeded", this));
}

void TSFTextStore::ReleaseTSFObjects() {
  MOZ_ASSERT(!mHandlingKeyMessage);

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::ReleaseTSFObjects()", this));

  mDocumentURL.Truncate();
  mContext = nullptr;
  if (const RefPtr<ITfDocumentMgr> documentMgr = mDocumentMgr.forget()) {
    documentMgr->Pop(TF_POPF_ALL);
  }
  MOZ_ASSERT(!mDocumentMgr);
  mSink = nullptr;
  mWidget = nullptr;
  mDispatcher = nullptr;

  if (!mMouseTrackers.IsEmpty()) {
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::ReleaseTSFObjects(), "
             "removing a mouse tracker...",
             this));
    mMouseTrackers.Clear();
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::ReleaseTSFObjects() completed", this));
}

STDMETHODIMP TSFTextStore::QueryInterface(REFIID riid, void** ppv) {
  HRESULT hr = TSFTextStoreBase::QueryInterface(riid, ppv);
  if (*ppv) {
    return hr;
  }
  MOZ_ASSERT(riid != IID_IUnknown);
  MOZ_ASSERT(riid != IID_ITextStoreACP);
  if (IID_ITfContextOwnerCompositionSink == riid) {
    *ppv = static_cast<ITfContextOwnerCompositionSink*>(this);
  } else if (IID_ITfMouseTrackerACP == riid) {
    *ppv = static_cast<ITfMouseTrackerACP*>(this);
  }
  if (*ppv) {
    AddRef();
    return S_OK;
  }

  MOZ_LOG(gIMELog, LogLevel::Error,
          ("0x%p TSFTextStore::QueryInterface() FAILED, riid=%s", this,
           AutoRiidCString(riid).get()));
  return E_NOINTERFACE;
}

STDMETHODIMP TSFTextStore::RequestLock(DWORD dwLockFlags, HRESULT* phrSession) {
  if (mDestroyed &&
      (mContentForTSF.isNothing() || mSelectionForTSF.isNothing())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RequestLock() FAILED due to "
             "being destroyed and no information of the contents",
             this));
    return E_FAIL;
  }

  const bool maybeFlushPendingNotifications = !mLock;
  HRESULT hr = TSFTextStoreBase::RequestLock(dwLockFlags, phrSession);
  if (SUCCEEDED(hr) && maybeFlushPendingNotifications) {
    MaybeFlushPendingNotifications();
  }
  return hr;
}

void TSFTextStore::DidLockGranted() {
  if (IsReadWriteLocked()) {
    // FreeCJ (TIP for Traditional Chinese) calls SetSelection() to set caret
    // to the start of composition string and insert a full width space for
    // a placeholder with a call of SetText().  After that, it calls
    // OnUpdateComposition() without new range.  Therefore, let's record the
    // composition update information here.
    CompleteLastActionIfStillIncomplete();

    FlushPendingActions();
  }

  // If the widget has gone, we don't need to notify anything.
  if (mDestroyed || !mWidget || mWidget->Destroyed()) {
    mPendingSelectionChangeData.reset();
    mHasReturnedNoLayoutError = false;
  }
}

void TSFTextStore::FlushPendingActions() {
  if (!mWidget || mWidget->Destroyed()) {
    // Note that don't clear mContentForTSF because TIP may try to commit
    // composition with a document lock.  In such case, TSFTextStore needs to
    // behave as expected by TIP.
    mPendingActions.Clear();
    mPendingSelectionChangeData.reset();
    mHasReturnedNoLayoutError = false;
    return;
  }

  // Some TIP may request lock but does nothing during the lock.  In such case,
  // this should do nothing.  For example, when MS-IME for Japanese is active
  // and we're inactivating, this case occurs and causes different behavior
  // from the other TIPs.
  if (mPendingActions.IsEmpty()) {
    return;
  }

  RefPtr<nsWindow> widget(mWidget);
  nsresult rv = mDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::FlushPendingActions() "
             "FAILED due to BeginNativeInputTransaction() failure",
             this));
    return;
  }
  for (uint32_t i = 0; i < mPendingActions.Length(); i++) {
    PendingAction& action = mPendingActions[i];
    switch (action.mType) {
      case PendingAction::Type::KeyboardEvent:
        if (mDestroyed) {
          MOZ_LOG(
              gIMELog, LogLevel::Warning,
              ("0x%p   TSFTextStore::FlushPendingActions() "
               "IGNORED pending KeyboardEvent(%s) due to already destroyed",
               this,
               action.mKeyMsg.message == WM_KEYDOWN ? "eKeyDown" : "eKeyUp"));
        }
        MOZ_DIAGNOSTIC_ASSERT(action.mKeyMsg.message == WM_KEYDOWN ||
                              action.mKeyMsg.message == WM_KEYUP);
        DispatchKeyboardEventAsProcessedByIME(action.mKeyMsg);
        if (!widget || widget->Destroyed()) {
          break;
        }
        break;
      case PendingAction::Type::CompositionStart: {
        MOZ_LOG(gIMELog, LogLevel::Debug,
                ("0x%p   TSFTextStore::FlushPendingActions() "
                 "flushing Type::eCompositionStart={ mSelectionStart=%ld, "
                 "mSelectionLength=%ld }, mDestroyed=%s",
                 this, action.mSelectionStart, action.mSelectionLength,
                 TSFUtils::BoolToChar(mDestroyed)));

        if (mDestroyed) {
          MOZ_LOG(gIMELog, LogLevel::Warning,
                  ("0x%p   TSFTextStore::FlushPendingActions() "
                   "IGNORED pending compositionstart due to already destroyed",
                   this));
          break;
        }

        if (action.mAdjustSelection) {
          // Select composition range so the new composition replaces the range
          WidgetSelectionEvent selectionSet(true, eSetSelection, widget);
          widget->InitEvent(selectionSet);
          selectionSet.mOffset = static_cast<uint32_t>(action.mSelectionStart);
          selectionSet.mLength = static_cast<uint32_t>(action.mSelectionLength);
          selectionSet.mReversed = false;
          selectionSet.mExpandToClusterBoundary =
              TSFStaticSink::ActiveTIP() !=
                  TextInputProcessorID::KeymanDesktop &&
              StaticPrefs::
                  intl_tsf_hack_extend_setting_selection_range_to_cluster_boundaries();
          DispatchEvent(selectionSet);
          if (!selectionSet.mSucceeded) {
            MOZ_LOG(gIMELog, LogLevel::Error,
                    ("0x%p   TSFTextStore::FlushPendingActions() "
                     "FAILED due to eSetSelection failure",
                     this));
            break;
          }
        }

        // eCompositionStart always causes
        // NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED.  Therefore, we should
        // wait to clear mContentForTSF until it's notified.
        mDeferClearingContentForTSF = true;

        MOZ_LOG(gIMELog, LogLevel::Debug,
                ("0x%p   TSFTextStore::FlushPendingActions() "
                 "dispatching compositionstart event...",
                 this));
        WidgetEventTime eventTime = widget->CurrentMessageWidgetEventTime();
        nsEventStatus status;
        rv = mDispatcher->StartComposition(status, &eventTime);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          MOZ_LOG(
              gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::FlushPendingActions() "
               "FAILED to dispatch compositionstart event, "
               "IsHandlingCompositionInContent()=%s",
               this, TSFUtils::BoolToChar(IsHandlingCompositionInContent())));
          // XXX Is this right? If there is a composition in content,
          //     shouldn't we wait NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED?
          mDeferClearingContentForTSF = !IsHandlingCompositionInContent();
        }
        if (!widget || widget->Destroyed()) {
          break;
        }
        break;
      }
      case PendingAction::Type::CompositionUpdate: {
        MOZ_LOG(gIMELog, LogLevel::Debug,
                ("0x%p   TSFTextStore::FlushPendingActions() "
                 "flushing Type::eCompositionUpdate={ mData=\"%s\", "
                 "mRanges=0x%p, mRanges->Length()=%zu }",
                 this, AutoEscapedUTF8String(action.mData).get(),
                 action.mRanges.get(),
                 action.mRanges ? action.mRanges->Length() : 0));

        // eCompositionChange causes a DOM text event, the IME will be notified
        // of NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED.  In this case, we
        // should not clear mContentForTSF until we notify the IME of the
        // composition update.
        mDeferClearingContentForTSF = true;

        rv = mDispatcher->SetPendingComposition(action.mData, action.mRanges);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          MOZ_LOG(
              gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::FlushPendingActions() "
               "FAILED to setting pending composition... "
               "IsHandlingCompositionInContent()=%s",
               this, TSFUtils::BoolToChar(IsHandlingCompositionInContent())));
          // XXX Is this right? If there is a composition in content,
          //     shouldn't we wait NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED?
          mDeferClearingContentForTSF = !IsHandlingCompositionInContent();
        } else {
          MOZ_LOG(gIMELog, LogLevel::Debug,
                  ("0x%p   TSFTextStore::FlushPendingActions() "
                   "dispatching compositionchange event...",
                   this));
          WidgetEventTime eventTime = widget->CurrentMessageWidgetEventTime();
          nsEventStatus status;
          rv = mDispatcher->FlushPendingComposition(status, &eventTime);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            MOZ_LOG(
                gIMELog, LogLevel::Error,
                ("0x%p   TSFTextStore::FlushPendingActions() "
                 "FAILED to dispatch compositionchange event, "
                 "IsHandlingCompositionInContent()=%s",
                 this, TSFUtils::BoolToChar(IsHandlingCompositionInContent())));
            // XXX Is this right? If there is a composition in content,
            //     shouldn't we wait NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED?
            mDeferClearingContentForTSF = !IsHandlingCompositionInContent();
          }
          // Be aware, the mWidget might already have been destroyed.
        }
        break;
      }
      case PendingAction::Type::CompositionEnd: {
        MOZ_LOG(gIMELog, LogLevel::Debug,
                ("0x%p   TSFTextStore::FlushPendingActions() "
                 "flushing Type::eCompositionEnd={ mData=\"%s\" }",
                 this, AutoEscapedUTF8String(action.mData).get()));

        // Dispatching eCompositionCommit causes a DOM text event, then,
        // the IME will be notified of NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED
        // when focused content actually handles the event.  For example,
        // when focused content is in a remote process, it's sent when
        // all dispatched composition events have been handled in the remote
        // process.  So, until then, we don't have newer content information.
        // Therefore, we need to put off to clear mContentForTSF.
        mDeferClearingContentForTSF = true;

        MOZ_LOG(gIMELog, LogLevel::Debug,
                ("0x%p   TSFTextStore::FlushPendingActions(), "
                 "dispatching compositioncommit event...",
                 this));
        WidgetEventTime eventTime = widget->CurrentMessageWidgetEventTime();
        nsEventStatus status;
        rv = mDispatcher->CommitComposition(status, &action.mData, &eventTime);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          MOZ_LOG(
              gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::FlushPendingActions() "
               "FAILED to dispatch compositioncommit event, "
               "IsHandlingCompositionInContent()=%s",
               this, TSFUtils::BoolToChar(IsHandlingCompositionInContent())));
          // XXX Is this right? If there is a composition in content,
          //     shouldn't we wait NOTIFY_IME_OF_COMPOSITION_EVENT_HANDLED?
          mDeferClearingContentForTSF = !IsHandlingCompositionInContent();
        }
        break;
      }
      case PendingAction::Type::SetSelection: {
        MOZ_LOG(gIMELog, LogLevel::Debug,
                ("0x%p   TSFTextStore::FlushPendingActions() "
                 "flushing Type::eSetSelection={ mSelectionStart=%ld, "
                 "mSelectionLength=%ld, mSelectionReversed=%s }, "
                 "mDestroyed=%s",
                 this, action.mSelectionStart, action.mSelectionLength,
                 TSFUtils::BoolToChar(action.mSelectionReversed),
                 TSFUtils::BoolToChar(mDestroyed)));

        if (mDestroyed) {
          MOZ_LOG(gIMELog, LogLevel::Warning,
                  ("0x%p   TSFTextStore::FlushPendingActions() "
                   "IGNORED pending selectionset due to already destroyed",
                   this));
          break;
        }

        WidgetSelectionEvent selectionSet(true, eSetSelection, widget);
        selectionSet.mOffset = static_cast<uint32_t>(action.mSelectionStart);
        selectionSet.mLength = static_cast<uint32_t>(action.mSelectionLength);
        selectionSet.mReversed = action.mSelectionReversed;
        selectionSet.mExpandToClusterBoundary =
            TSFStaticSink::ActiveTIP() != TextInputProcessorID::KeymanDesktop &&
            StaticPrefs::
                intl_tsf_hack_extend_setting_selection_range_to_cluster_boundaries();
        DispatchEvent(selectionSet);
        if (!selectionSet.mSucceeded) {
          MOZ_LOG(gIMELog, LogLevel::Error,
                  ("0x%p   TSFTextStore::FlushPendingActions() "
                   "FAILED due to eSetSelection failure",
                   this));
          break;
        }
        break;
      }
      default:
        MOZ_CRASH("unexpected action type");
    }

    if (widget && !widget->Destroyed()) {
      continue;
    }

    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::FlushPendingActions(), "
             "qutting since the mWidget has gone",
             this));
    break;
  }
  mPendingActions.Clear();
}

void TSFTextStore::MaybeFlushPendingNotifications() {
  if (mDeferNotifyingTSF) {
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
             "putting off flushing pending notifications due to initializing "
             "something...",
             this));
    return;
  }

  if (IsReadLocked()) {
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
             "putting off flushing pending notifications due to being the "
             "document locked...",
             this));
    return;
  }

  if (mDeferCommittingComposition) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
             "calling TSFTextStore::CommitCompositionInternal(false)...",
             this));
    mDeferCommittingComposition = mDeferCancellingComposition = false;
    CommitCompositionInternal(false);
  } else if (mDeferCancellingComposition) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
             "calling TSFTextStore::CommitCompositionInternal(true)...",
             this));
    mDeferCommittingComposition = mDeferCancellingComposition = false;
    CommitCompositionInternal(true);
  }

  if (mDeferNotifyingTSFUntilNextUpdate) {
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
             "putting off flushing pending notifications due to being "
             "dispatching events...",
             this));
    return;
  }

  if (mPendingDestroy) {
    Destroy();
    return;
  }

  if (mDestroyed) {
    // If it's already been destroyed completely, this shouldn't notify TSF of
    // anything anymore.
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
             "does nothing because this has already destroyed completely...",
             this));
    return;
  }

  if (!mDeferClearingContentForTSF && mContentForTSF.isSome()) {
    mContentForTSF.reset();
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
             "mContentForTSF is set to `Nothing`",
             this));
  }

  // When there is no cached content, we can sync actual contents and TSF/TIP
  // expecting contents.
  RefPtr<TSFTextStore> kungFuDeathGrip = this;
  Unused << kungFuDeathGrip;
  if (mContentForTSF.isNothing()) {
    if (mPendingTextChangeData.IsValid()) {
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
               "calling TSFTextStore::NotifyTSFOfTextChange()...",
               this));
      NotifyTSFOfTextChange();
    }
    if (mPendingSelectionChangeData.isSome()) {
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
               "calling TSFTextStore::NotifyTSFOfSelectionChange()...",
               this));
      NotifyTSFOfSelectionChange();
    }
  }

  if (mHasReturnedNoLayoutError) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::MaybeFlushPendingNotifications(), "
             "calling TSFTextStore::NotifyTSFOfLayoutChange()...",
             this));
    NotifyTSFOfLayoutChange();
  }
}

void TSFTextStore::MaybeDispatchKeyboardEventAsProcessedByIME() {
  // If we've already been destroyed, we cannot do anything.
  if (mDestroyed) {
    MOZ_LOG(
        gIMELog, LogLevel::Debug,
        ("0x%p   TSFTextStore::MaybeDispatchKeyboardEventAsProcessedByIME(), "
         "does nothing because it's already been destroyed",
         this));
    return;
  }

  // If we're not handling key message or we've already dispatched a keyboard
  // event for the handling key message, we should do nothing anymore.
  if (!sHandlingKeyMsg || sIsKeyboardEventDispatched) {
    MOZ_LOG(
        gIMELog, LogLevel::Debug,
        ("0x%p   TSFTextStore::MaybeDispatchKeyboardEventAsProcessedByIME(), "
         "does nothing because not necessary to dispatch keyboard event",
         this));
    return;
  }

  sIsKeyboardEventDispatched = true;
  // If the document is locked, just adding the task to dispatching an event
  // to the queue.
  if (IsReadLocked()) {
    MOZ_LOG(
        gIMELog, LogLevel::Debug,
        ("0x%p   TSFTextStore::MaybeDispatchKeyboardEventAsProcessedByIME(), "
         "adding to dispatch a keyboard event into the queue...",
         this));
    PendingAction* action = mPendingActions.AppendElement();
    action->mType = PendingAction::Type::KeyboardEvent;
    memcpy(&action->mKeyMsg, sHandlingKeyMsg, sizeof(MSG));
    return;
  }

  // Otherwise, dispatch a keyboard event.
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::MaybeDispatchKeyboardEventAsProcessedByIME(), "
           "trying to dispatch a keyboard event...",
           this));
  DispatchKeyboardEventAsProcessedByIME(*sHandlingKeyMsg);
}

void TSFTextStore::DispatchKeyboardEventAsProcessedByIME(const MSG& aMsg) {
  MOZ_ASSERT(mWidget);
  MOZ_ASSERT(!mWidget->Destroyed());
  MOZ_ASSERT(!mDestroyed);

  ModifierKeyState modKeyState;
  MSG msg(aMsg);
  msg.wParam = VK_PROCESSKEY;
  NativeKey nativeKey(mWidget, msg, modKeyState);
  switch (aMsg.message) {
    case WM_KEYDOWN:
      MOZ_LOG(gIMELog, LogLevel::Debug,
              ("0x%p   TSFTextStore::DispatchKeyboardEventAsProcessedByIME(), "
               "dispatching an eKeyDown event...",
               this));
      nativeKey.HandleKeyDownMessage();
      break;
    case WM_KEYUP:
      MOZ_LOG(gIMELog, LogLevel::Debug,
              ("0x%p   TSFTextStore::DispatchKeyboardEventAsProcessedByIME(), "
               "dispatching an eKeyUp event...",
               this));
      nativeKey.HandleKeyUpMessage();
      break;
    default:
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::DispatchKeyboardEventAsProcessedByIME(), "
               "ERROR, it doesn't handle the message",
               this));
      break;
  }
}

STDMETHODIMP TSFTextStore::QueryInsert(LONG acpTestStart, LONG acpTestEnd,
                                       ULONG cch, LONG* pacpResultStart,
                                       LONG* pacpResultEnd) {
  HRESULT hr = TSFTextStoreBase::QueryInsert(acpTestStart, acpTestEnd, cch,
                                             pacpResultStart, pacpResultEnd);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  // XXX need to adjust to cluster boundary
  // Assume we are given good offsets for now
  if (mComposition.isNothing() &&
      ((StaticPrefs::
            intl_tsf_hack_ms_traditional_chinese_query_insert_result() &&
        TSFStaticSink::IsMSChangJieOrMSQuickActive()) ||
       (StaticPrefs::
            intl_tsf_hack_ms_simplified_chinese_query_insert_result() &&
        TSFStaticSink::IsMSPinyinOrMSWubiActive()))) {
    MOZ_LOG(gIMELog, LogLevel::Warning,
            ("0x%p   TSFTextStore::QueryInsert() WARNING using different "
             "result for the TIP",
             this));
    // Chinese TIPs of Microsoft assume that QueryInsert() returns selected
    // range which should be removed.
    *pacpResultStart = acpTestStart;
    *pacpResultEnd = acpTestEnd;
  } else {
    *pacpResultStart = acpTestStart;
    *pacpResultEnd = acpTestStart + cch;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p  TSFTextStore::QueryInsert() succeeded: "
           "*pacpResultStart=%ld, *pacpResultEnd=%ld)",
           this, *pacpResultStart, *pacpResultEnd));
  return S_OK;
}

STDMETHODIMP TSFTextStore::GetSelection(ULONG ulIndex, ULONG ulCount,
                                        TS_SELECTION_ACP* pSelection,
                                        ULONG* pcFetched) {
  HRESULT hr =
      TSFTextStoreBase::GetSelection(ulIndex, ulCount, pSelection, pcFetched);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  Maybe<Selection>& selectionForTSF = SelectionForTSF();
  if (selectionForTSF.isNothing()) {
    if (TSFUtils::DoNotReturnErrorFromGetSelection()) {
      *pSelection = TSFUtils::EmptySelectionACP();
      *pcFetched = 1;
      MOZ_LOG(
          gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::GetSelection() returns fake selection range "
           "for avoiding a crash in TSF, *pSelection=%s",
           this, mozilla::ToString(*pSelection).c_str()));
      return S_OK;
    }
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetSelection() FAILED due to "
             "SelectionForTSF() failure",
             this));
    return E_FAIL;
  }
  if (!selectionForTSF->HasRange()) {
    *pSelection = TSFUtils::EmptySelectionACP();
    *pcFetched = 0;
    return TS_E_NOSELECTION;
  }
  *pSelection = selectionForTSF->ACPRef();
  *pcFetched = 1;
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::GetSelection() succeeded, *pSelection=%s",
           this, mozilla::ToString(*pSelection).c_str()));
  return S_OK;
}

Maybe<TSFTextStore::Content>& TSFTextStore::ContentForTSF() {
  // This should be called when the document is locked or the content hasn't
  // been abandoned yet.
  if (NS_WARN_IF(!IsReadLocked() && mContentForTSF.isNothing())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::ContentForTSF(), FAILED, due to "
             "called wrong timing, IsReadLocked()=%s, mContentForTSF=Nothing",
             this, TSFUtils::BoolToChar(IsReadLocked())));
    return mContentForTSF;
  }

  Maybe<Selection>& selectionForTSF = SelectionForTSF();
  if (selectionForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::ContentForTSF(), FAILED, due to "
             "SelectionForTSF() failure",
             this));
    mContentForTSF.reset();
    return mContentForTSF;
  }

  if (mContentForTSF.isNothing()) {
    MOZ_DIAGNOSTIC_ASSERT(
        !mIsInitializingContentForTSF,
        "TSFTextStore::ContentForTSF() shouldn't be called recursively");

    // We may query text content recursively if TSF does something recursively,
    // e.g., with flushing pending layout, an nsWindow may be
    // moved/resized/focused/blured by that.  In the case, we cannot avoid the
    // loop at least first nested call.  For avoiding to make an infinite loop,
    // we should not allow to flush pending layout in the nested query.
    const AllowToFlushLayoutIfNoCache allowToFlushPendingLayout =
        !mIsInitializingSelectionForTSF && !mIsInitializingContentForTSF
            ? AllowToFlushLayoutIfNoCache::Yes
            : AllowToFlushLayoutIfNoCache::No;

    AutoNotifyingTSFBatch deferNotifyingTSF(*this);
    AutoRestore<bool> saveInitializingContetTSF(mIsInitializingContentForTSF);
    mIsInitializingContentForTSF = true;

    nsString text;  // Don't use auto string for avoiding to copy long string.
    if (NS_WARN_IF(!GetCurrentText(text, allowToFlushPendingLayout))) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::ContentForTSF(), FAILED, due to "
               "GetCurrentText() failure",
               this));
      return mContentForTSF;
    }

    // If this is called recursively, the inner one should computed with the
    // latest (flushed) layout because it should not cause flushing layout so
    // that nobody should invalidate the layout after that.  Therefore, let's
    // use first query result.
    if (mContentForTSF.isNothing()) {
      mContentForTSF.emplace(*this, text);
    }
    // Basically, the cached content which is expected by TSF/TIP should be
    // cleared after active composition is committed or the document lock is
    // unlocked.  However, in e10s mode, content will be modified
    // asynchronously.  In such case, mDeferClearingContentForTSF may be
    // true until whole dispatched events are handled by the focused editor.
    mDeferClearingContentForTSF = false;
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::ContentForTSF(): mContentForTSF=%s", this,
           mozilla::ToString(mContentForTSF).c_str()));

  return mContentForTSF;
}

bool TSFTextStore::CanAccessActualContentDirectly() const {
  if (mContentForTSF.isNothing() || mSelectionForTSF.isNothing()) {
    return true;
  }

  // If the cached content has been changed by something except composition,
  // the content cache may be different from actual content.
  if (mPendingTextChangeData.IsValid() &&
      !mPendingTextChangeData.mCausedOnlyByComposition) {
    return false;
  }

  // If the cached selection isn't changed, cached content and actual content
  // should be same.
  if (mPendingSelectionChangeData.isNothing()) {
    return true;
  }

  return mSelectionForTSF->EqualsExceptDirection(*mPendingSelectionChangeData);
}

bool TSFTextStore::GetCurrentText(
    nsAString& aTextContent,
    AllowToFlushLayoutIfNoCache aAllowToFlushLayoutIfNoCache) {
  if (mContentForTSF.isSome()) {
    aTextContent = mContentForTSF->TextRef();
    return true;
  }

  MOZ_ASSERT(!mDestroyed);
  MOZ_ASSERT(mWidget && !mWidget->Destroyed());

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::GetCurrentText(): "
           "retrieving text from the content...",
           this));

  WidgetQueryContentEvent queryTextContentEvent(true, eQueryTextContent,
                                                mWidget);
  queryTextContentEvent.InitForQueryTextContent(0, UINT32_MAX);
  queryTextContentEvent.mNeedsToFlushLayout =
      aAllowToFlushLayoutIfNoCache == AllowToFlushLayoutIfNoCache::Yes;
  mWidget->InitEvent(queryTextContentEvent);
  DispatchEvent(queryTextContentEvent);
  if (NS_WARN_IF(queryTextContentEvent.Failed())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetCurrentText(), FAILED, due to "
             "eQueryTextContent failure",
             this));
    aTextContent.Truncate();
    return false;
  }

  aTextContent = queryTextContentEvent.mReply->DataRef();
  return true;
}

Maybe<TSFTextStore::Selection>& TSFTextStore::SelectionForTSF() {
  if (mSelectionForTSF.isNothing()) {
    MOZ_ASSERT(!mDestroyed);
    // If the window has never been available, we should crash since working
    // with broken values may make TIP confused.
    if (!mWidget || mWidget->Destroyed()) {
      MOZ_ASSERT_UNREACHABLE("There should be non-destroyed widget");
    }

    MOZ_DIAGNOSTIC_ASSERT(
        !mIsInitializingSelectionForTSF,
        "TSFTextStore::SelectionForTSF() shouldn't be called recursively");

    // We may query selection recursively if TSF does something recursively,
    // e.g., with flushing pending layout, an nsWindow may be
    // moved/resized/focused/blured by that.  In the case, we cannot avoid the
    // loop at least first nested call.  For avoiding to make an infinite loop,
    // we should not allow to flush pending layout in the nested query.
    const bool allowToFlushPendingLayout =
        !mIsInitializingSelectionForTSF && !mIsInitializingContentForTSF;

    AutoNotifyingTSFBatch deferNotifyingTSF(*this);
    AutoRestore<bool> saveInitializingSelectionForTSF(
        mIsInitializingSelectionForTSF);
    mIsInitializingSelectionForTSF = true;

    WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                   mWidget);
    querySelectedTextEvent.mNeedsToFlushLayout = allowToFlushPendingLayout;
    mWidget->InitEvent(querySelectedTextEvent);
    DispatchEvent(querySelectedTextEvent);
    if (NS_WARN_IF(querySelectedTextEvent.Failed())) {
      return mSelectionForTSF;
    }
    // If this is called recursively, the inner one should computed with the
    // latest (flushed) layout because it should not cause flushing layout so
    // that nobody should invalidate the layout after that.  Therefore, let's
    // use first query result.
    if (mSelectionForTSF.isNothing()) {
      mSelectionForTSF.emplace(querySelectedTextEvent);
    }
  }

  if (mPendingToCreateNativeCaret) {
    mPendingToCreateNativeCaret = false;
    CreateNativeCaret();
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::SelectionForTSF() succeeded, "
           "mSelectionForTSF=%s",
           this, ToString(mSelectionForTSF).c_str()));

  return mSelectionForTSF;
}

static TextRangeType GetGeckoSelectionValue(TF_DISPLAYATTRIBUTE& aDisplayAttr) {
  switch (aDisplayAttr.bAttr) {
    case TF_ATTR_TARGET_CONVERTED:
      return TextRangeType::eSelectedClause;
    case TF_ATTR_CONVERTED:
      return TextRangeType::eConvertedClause;
    case TF_ATTR_TARGET_NOTCONVERTED:
      return TextRangeType::eSelectedRawClause;
    default:
      return TextRangeType::eRawClause;
  }
}

HRESULT TSFTextStore::GetDisplayAttribute(ITfProperty* aAttrProperty,
                                          ITfRange* aRange,
                                          TF_DISPLAYATTRIBUTE* aResult) {
  NS_ENSURE_TRUE(aAttrProperty, E_FAIL);
  NS_ENSURE_TRUE(aRange, E_FAIL);
  NS_ENSURE_TRUE(aResult, E_FAIL);

  HRESULT hr;

  if (MOZ_LOG_TEST(gIMELog, LogLevel::Debug)) {
    const TSFUtils::AutoRangeExtant rangeExtant(aRange);
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::GetDisplayAttribute(): "
             "aRange=%ld-%ld (hr=%s)",
             this, rangeExtant.mStart - mComposition->StartOffset(),
             rangeExtant.End() - mComposition->StartOffset(),
             TSFUtils::CommonHRESULTToChar(rangeExtant.mHR)));
  }

  VARIANT propValue;
  ::VariantInit(&propValue);
  hr = aAttrProperty->GetValue(TfEditCookie(mEditCookie), aRange, &propValue);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetDisplayAttribute() FAILED due to "
             "ITfProperty::GetValue() failed",
             this));
    return hr;
  }
  if (VT_I4 != propValue.vt) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetDisplayAttribute() FAILED due to "
             "ITfProperty::GetValue() returns non-VT_I4 value",
             this));
    ::VariantClear(&propValue);
    return E_FAIL;
  }

  RefPtr<ITfCategoryMgr> categoryMgr = TSFUtils::GetCategoryMgr();
  if (NS_WARN_IF(!categoryMgr)) {
    return E_FAIL;
  }
  GUID guid;
  hr = categoryMgr->GetGUID(DWORD(propValue.lVal), &guid);
  ::VariantClear(&propValue);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetDisplayAttribute() FAILED due to "
             "ITfCategoryMgr::GetGUID() failed",
             this));
    return hr;
  }

  RefPtr<ITfDisplayAttributeMgr> displayAttrMgr =
      TSFUtils::GetDisplayAttributeMgr();
  if (NS_WARN_IF(!displayAttrMgr)) {
    return E_FAIL;
  }
  RefPtr<ITfDisplayAttributeInfo> info;
  hr = displayAttrMgr->GetDisplayAttributeInfo(guid, getter_AddRefs(info),
                                               nullptr);
  if (FAILED(hr) || !info) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetDisplayAttribute() FAILED due to "
             "ITfDisplayAttributeMgr::GetDisplayAttributeInfo() failed",
             this));
    return hr;
  }

  hr = info->GetAttributeInfo(aResult);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetDisplayAttribute() FAILED due to "
             "ITfDisplayAttributeInfo::GetAttributeInfo() failed",
             this));
    return hr;
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::GetDisplayAttribute() succeeded: "
           "Result={ %s }",
           this, mozilla::ToString(*aResult).c_str()));
  return S_OK;
}

HRESULT TSFTextStore::RestartCompositionIfNecessary(ITfRange* aRangeNew) {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::RestartCompositionIfNecessary("
           "aRangeNew=0x%p), mComposition=%s",
           this, aRangeNew, ToString(mComposition).c_str()));

  if (mComposition.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RestartCompositionIfNecessary() FAILED "
             "due to no composition view",
             this));
    return E_FAIL;
  }

  HRESULT hr;
  RefPtr<ITfCompositionView> pComposition(mComposition->GetView());
  RefPtr<ITfRange> composingRange(aRangeNew);
  if (!composingRange) {
    hr = pComposition->GetRange(getter_AddRefs(composingRange));
    if (FAILED(hr)) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::RestartCompositionIfNecessary() "
               "FAILED due to pComposition->GetRange() failure",
               this));
      return hr;
    }
  }

  // Get starting offset of the composition
  const TSFUtils::AutoRangeExtant compositionRangeExtant(composingRange);
  if (MOZ_UNLIKELY(compositionRangeExtant.isErr())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RestartCompositionIfNecessary() FAILED "
             "due to GetRangeExtent() failure",
             this));
    return hr;
  }

  if (mComposition->StartOffset() == compositionRangeExtant.mStart &&
      mComposition->Length() == compositionRangeExtant.mLength) {
    return S_OK;
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::RestartCompositionIfNecessary(), "
           "restaring composition because of compostion range is changed "
           "(range=%ld-%ld, mComposition=%s)",
           this, compositionRangeExtant.mStart, compositionRangeExtant.End(),
           ToString(mComposition).c_str()));

  // If the queried composition length is different from the length
  // of our composition string, OnUpdateComposition is being called
  // because a part of the original composition was committed.
  hr = RestartComposition(*mComposition, pComposition, composingRange);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RestartCompositionIfNecessary() "
             "FAILED due to RestartComposition() failure",
             this));
    return hr;
  }

  MOZ_LOG(
      gIMELog, LogLevel::Debug,
      ("0x%p   TSFTextStore::RestartCompositionIfNecessary() succeeded", this));
  return S_OK;
}

HRESULT TSFTextStore::RestartComposition(Composition& aCurrentComposition,
                                         ITfCompositionView* aCompositionView,
                                         ITfRange* aNewRange) {
  Maybe<Selection>& selectionForTSF = SelectionForTSF();

  const TSFUtils::AutoRangeExtant newRangeExtant(aNewRange);
  if (selectionForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RestartComposition() FAILED "
             "due to SelectionForTSF() failure",
             this));
    return E_FAIL;
  }

  if (MOZ_UNLIKELY(newRangeExtant.isErr())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RestartComposition() FAILED "
             "due to GetRangeExtent() failure",
             this));
    return newRangeExtant.mHR;
  }

  // If the new range has no overlap with the crrent range, we just commit
  // the composition and restart new composition with the new range but
  // current selection range should be preserved.
  if (newRangeExtant.mStart >= aCurrentComposition.EndOffset() ||
      newRangeExtant.End() <= aCurrentComposition.StartOffset()) {
    RecordCompositionEndAction();
    RecordCompositionStartAction(aCompositionView, newRangeExtant.mStart,
                                 newRangeExtant.mLength, true);
    return S_OK;
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::RestartComposition(aCompositionView=0x%p, "
           "aNewRange=0x%p { newStart=%ld, newLength=%ld }), "
           "aCurrentComposition=%s, "
           "selectionForTSF=%s",
           this, aCompositionView, aNewRange, newRangeExtant.mStart,
           newRangeExtant.mLength, ToString(aCurrentComposition).c_str(),
           ToString(selectionForTSF).c_str()));

  // If the new range has an overlap with the current one, we should not commit
  // the whole current range to avoid creating an odd undo transaction.
  // I.e., the overlapped range which is being composed should not appear in
  // undo transaction.

  // Backup current composition data and selection data.
  Composition oldComposition(aCurrentComposition);  // NOLINT
  Selection oldSelection(*selectionForTSF);         // NOLINT

  // Commit only the part of composition.
  LONG keepComposingStartOffset =
      std::max(oldComposition.StartOffset(), newRangeExtant.mStart);
  LONG keepComposingEndOffset =
      std::min(oldComposition.EndOffset(), newRangeExtant.End());
  MOZ_ASSERT(
      keepComposingStartOffset <= keepComposingEndOffset,
      "Why keepComposingEndOffset is smaller than keepComposingStartOffset?");
  LONG keepComposingLength = keepComposingEndOffset - keepComposingStartOffset;
  // Remove the overlapped part from the commit string.
  nsAutoString commitString(oldComposition.DataRef());
  commitString.Cut(keepComposingStartOffset - oldComposition.StartOffset(),
                   keepComposingLength);
  // Update the composition string.
  Maybe<Content>& contentForTSF = ContentForTSF();
  if (contentForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RestartComposition() FAILED "
             "due to ContentForTSF() failure",
             this));
    return E_FAIL;
  }
  contentForTSF->ReplaceTextWith(oldComposition.StartOffset(),
                                 oldComposition.Length(), commitString);
  MOZ_ASSERT(mComposition.isSome());
  // Record a compositionupdate action for commit the part of composing string.
  PendingAction* action = LastOrNewPendingCompositionUpdate();
  if (mComposition.isSome()) {
    action->mData = mComposition->DataRef();
  }
  action->mRanges->Clear();
  // Note that we shouldn't append ranges when composition string
  // is empty because it may cause TextComposition confused.
  if (!action->mData.IsEmpty()) {
    TextRange caretRange;
    caretRange.mStartOffset = caretRange.mEndOffset = static_cast<uint32_t>(
        oldComposition.StartOffset() + commitString.Length());
    caretRange.mRangeType = TextRangeType::eCaret;
    action->mRanges->AppendElement(caretRange);
  }
  action->mIncomplete = false;

  // Record compositionend action.
  RecordCompositionEndAction();

  // Record compositionstart action only with the new start since this method
  // hasn't restored composing string yet.
  RecordCompositionStartAction(aCompositionView, newRangeExtant.mStart, 0,
                               false);

  // Restore the latest text content and selection.
  contentForTSF->ReplaceSelectedTextWith(nsDependentSubstring(
      oldComposition.DataRef(),
      keepComposingStartOffset - oldComposition.StartOffset(),
      keepComposingLength));
  selectionForTSF = Some(oldSelection);

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::RestartComposition() succeeded, "
           "mComposition=%s, selectionForTSF=%s",
           this, ToString(mComposition).c_str(),
           ToString(selectionForTSF).c_str()));

  return S_OK;
}

HRESULT TSFTextStore::RecordCompositionUpdateAction() {
  MOZ_LOG(
      gIMELog, LogLevel::Debug,
      ("0x%p   TSFTextStore::RecordCompositionUpdateAction(), mComposition=%s",
       this, ToString(mComposition).c_str()));

  if (mComposition.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionUpdateAction() FAILED "
             "due to no composition view",
             this));
    return E_FAIL;
  }

  // Getting display attributes is *really* complicated!
  // We first get the context and the property objects to query for
  // attributes, but since a big range can have a variety of values for
  // the attribute, we have to find out all the ranges that have distinct
  // attribute values. Then we query for what the value represents through
  // the display attribute manager and translate that to TextRange to be
  // sent in eCompositionChange

  RefPtr<ITfProperty> attrProperty;
  HRESULT hr =
      mContext->GetProperty(GUID_PROP_ATTRIBUTE, getter_AddRefs(attrProperty));
  if (FAILED(hr) || !attrProperty) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionUpdateAction() FAILED "
             "due to mContext->GetProperty() failure",
             this));
    return FAILED(hr) ? hr : E_FAIL;
  }

  RefPtr<ITfRange> composingRange;
  hr = mComposition->GetView()->GetRange(getter_AddRefs(composingRange));
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionUpdateAction() "
             "FAILED due to mComposition->GetView()->GetRange() failure",
             this));
    return hr;
  }

  RefPtr<IEnumTfRanges> enumRanges;
  hr = attrProperty->EnumRanges(TfEditCookie(mEditCookie),
                                getter_AddRefs(enumRanges), composingRange);
  if (FAILED(hr) || !enumRanges) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionUpdateAction() FAILED "
             "due to attrProperty->EnumRanges() failure",
             this));
    return FAILED(hr) ? hr : E_FAIL;
  }

  // First, put the log of content and selection here.
  Maybe<Selection>& selectionForTSF = SelectionForTSF();
  if (selectionForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionUpdateAction() FAILED "
             "due to SelectionForTSF() failure",
             this));
    return E_FAIL;
  }

  PendingAction* action = LastOrNewPendingCompositionUpdate();
  action->mData = mComposition->DataRef();
  // The ranges might already have been initialized, however, if this is
  // called again, that means we need to overwrite the ranges with current
  // information.
  action->mRanges->Clear();

  // Note that we shouldn't append ranges when composition string
  // is empty because it may cause TextComposition confused.
  if (!action->mData.IsEmpty()) {
    TextRange newRange;
    // No matter if we have display attribute info or not,
    // we always pass in at least one range to eCompositionChange
    newRange.mStartOffset = 0;
    newRange.mEndOffset = action->mData.Length();
    newRange.mRangeType = TextRangeType::eRawClause;
    action->mRanges->AppendElement(newRange);

    RefPtr<ITfRange> range;
    while (enumRanges->Next(1, getter_AddRefs(range), nullptr) == S_OK) {
      if (NS_WARN_IF(!range)) {
        break;
      }
      const TSFUtils::AutoRangeExtant rangeExtant(range);
      if (MOZ_UNLIKELY(rangeExtant.isErr())) {
        continue;
      }
      // The range may include out of composition string.  We should ignore
      // outside of the composition string.
      LONG start = std::clamp(rangeExtant.mStart, mComposition->StartOffset(),
                              mComposition->EndOffset());
      LONG end = std::clamp(rangeExtant.End(), mComposition->StartOffset(),
                            mComposition->EndOffset());
      LONG length = end - start;
      if (length < 0) {
        MOZ_LOG(gIMELog, LogLevel::Error,
                ("0x%p   TSFTextStore::RecordCompositionUpdateAction() "
                 "ignores invalid range (%ld-%ld)",
                 this, rangeExtant.mStart - mComposition->StartOffset(),
                 rangeExtant.End() - mComposition->StartOffset()));
        continue;
      }
      if (!length) {
        MOZ_LOG(gIMELog, LogLevel::Debug,
                ("0x%p   TSFTextStore::RecordCompositionUpdateAction() "
                 "ignores a range due to outside of the composition or empty "
                 "(%ld-%ld)",
                 this, rangeExtant.mStart - mComposition->StartOffset(),
                 rangeExtant.End() - mComposition->StartOffset()));
        continue;
      }

      TextRange newRange;
      newRange.mStartOffset =
          static_cast<uint32_t>(start - mComposition->StartOffset());
      // The end of the last range in the array is
      // always kept at the end of composition
      newRange.mEndOffset = mComposition->Length();

      TF_DISPLAYATTRIBUTE attr;
      hr = GetDisplayAttribute(attrProperty, range, &attr);
      if (FAILED(hr)) {
        newRange.mRangeType = TextRangeType::eRawClause;
      } else {
        newRange.mRangeType = GetGeckoSelectionValue(attr);
        if (const Maybe<nscolor> foregroundColor =
                TSFUtils::GetColor(attr.crText)) {
          newRange.mRangeStyle.mForegroundColor = *foregroundColor;
          newRange.mRangeStyle.mDefinedStyles |=
              TextRangeStyle::DEFINED_FOREGROUND_COLOR;
        }
        if (const Maybe<nscolor> backgroundColor =
                TSFUtils::GetColor(attr.crBk)) {
          newRange.mRangeStyle.mBackgroundColor = *backgroundColor;
          newRange.mRangeStyle.mDefinedStyles |=
              TextRangeStyle::DEFINED_BACKGROUND_COLOR;
        }
        if (const Maybe<nscolor> underlineColor =
                TSFUtils::GetColor(attr.crLine)) {
          newRange.mRangeStyle.mUnderlineColor = *underlineColor;
          newRange.mRangeStyle.mDefinedStyles |=
              TextRangeStyle::DEFINED_UNDERLINE_COLOR;
        }
        if (const Maybe<TextRangeStyle::LineStyle> lineStyle =
                TSFUtils::GetLineStyle(attr.lsStyle)) {
          newRange.mRangeStyle.mLineStyle = *lineStyle;
          newRange.mRangeStyle.mDefinedStyles |=
              TextRangeStyle::DEFINED_LINESTYLE;
          newRange.mRangeStyle.mIsBoldLine = attr.fBoldLine != 0;
        }
      }

      TextRange& lastRange = action->mRanges->LastElement();
      if (lastRange.mStartOffset == newRange.mStartOffset) {
        // Replace range if last range is the same as this one
        // So that ranges don't overlap and confuse the editor
        lastRange = newRange;
      } else {
        lastRange.mEndOffset = newRange.mStartOffset;
        action->mRanges->AppendElement(newRange);
      }
    }

    // We need to hack for Korean Input System which is Korean standard TIP.
    // It sets no change style to IME selection (the selection is always only
    // one).  So, the composition string looks like normal (or committed)
    // string.  At this time, current selection range is same as the
    // composition string range.  Other applications set a wide caret which
    // covers the composition string,  however, Gecko doesn't support the wide
    // caret drawing now (Gecko doesn't support XOR drawing), unfortunately.
    // For now, we should change the range style to undefined.
    if (!selectionForTSF->Collapsed() && action->mRanges->Length() == 1) {
      TextRange& range = action->mRanges->ElementAt(0);
      LONG start = selectionForTSF->MinOffset();
      LONG end = selectionForTSF->MaxOffset();
      if (static_cast<LONG>(range.mStartOffset) ==
              start - mComposition->StartOffset() &&
          static_cast<LONG>(range.mEndOffset) ==
              end - mComposition->StartOffset() &&
          range.mRangeStyle.IsNoChangeStyle()) {
        range.mRangeStyle.Clear();
        // The looks of selected type is better than others.
        range.mRangeType = TextRangeType::eSelectedRawClause;
      }
    }

    // The caret position has to be collapsed.
    uint32_t caretPosition = static_cast<uint32_t>(
        selectionForTSF->HasRange()
            ? selectionForTSF->MaxOffset() - mComposition->StartOffset()
            : mComposition->StartOffset());

    // If caret is in the target clause and it doesn't have specific style,
    // the target clause will be painted as normal selection range.  Since
    // caret shouldn't be in selection range on Windows, we shouldn't append
    // caret range in such case.
    const TextRange* targetClause = action->mRanges->GetTargetClause();
    if (!targetClause || targetClause->mRangeStyle.IsDefined() ||
        caretPosition < targetClause->mStartOffset ||
        caretPosition > targetClause->mEndOffset) {
      TextRange caretRange;
      caretRange.mStartOffset = caretRange.mEndOffset = caretPosition;
      caretRange.mRangeType = TextRangeType::eCaret;
      action->mRanges->AppendElement(caretRange);
    }
  }

  action->mIncomplete = false;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::RecordCompositionUpdateAction() "
           "succeeded",
           this));

  return S_OK;
}

HRESULT TSFTextStore::SetSelectionInternal(
    const TS_SELECTION_ACP* pSelection, bool aDispatchCompositionChangeEvent) {
  MOZ_LOG(
      gIMELog, LogLevel::Debug,
      ("0x%p   TSFTextStore::SetSelectionInternal(pSelection=%s, "
       "aDispatchCompositionChangeEvent=%s), mComposition=%s",
       this, pSelection ? mozilla::ToString(*pSelection).c_str() : "nullptr",
       TSFUtils::BoolToChar(aDispatchCompositionChangeEvent),
       ToString(mComposition).c_str()));

  MOZ_ASSERT(IsReadWriteLocked());

  Maybe<Selection>& selectionForTSF = SelectionForTSF();
  if (selectionForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::SetSelectionInternal() FAILED due to "
             "SelectionForTSF() failure",
             this));
    return E_FAIL;
  }

  MaybeDispatchKeyboardEventAsProcessedByIME();
  if (mDestroyed) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::SetSelectionInternal() FAILED due to "
             "destroyed during dispatching a keyboard event",
             this));
    return E_FAIL;
  }

  // If actually the range is not changing, we should do nothing.
  // Perhaps, we can ignore the difference change because it must not be
  // important for following edit.
  if (selectionForTSF->EqualsExceptDirection(*pSelection)) {
    MOZ_LOG(gIMELog, LogLevel::Warning,
            ("0x%p   TSFTextStore::SetSelectionInternal() Succeeded but "
             "did nothing because the selection range isn't changing",
             this));
    selectionForTSF->SetSelection(*pSelection);
    return S_OK;
  }

  if (mComposition.isSome()) {
    if (aDispatchCompositionChangeEvent) {
      HRESULT hr = RestartCompositionIfNecessary();
      if (FAILED(hr)) {
        MOZ_LOG(gIMELog, LogLevel::Error,
                ("0x%p   TSFTextStore::SetSelectionInternal() FAILED due to "
                 "RestartCompositionIfNecessary() failure",
                 this));
        return hr;
      }
    }
    if (pSelection->acpStart < mComposition->StartOffset() ||
        pSelection->acpEnd > mComposition->EndOffset()) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::SetSelectionInternal() FAILED due to "
               "the selection being out of the composition string",
               this));
      return TS_E_INVALIDPOS;
    }
    // Emulate selection during compositions
    selectionForTSF->SetSelection(*pSelection);
    if (aDispatchCompositionChangeEvent) {
      HRESULT hr = RecordCompositionUpdateAction();
      if (FAILED(hr)) {
        MOZ_LOG(gIMELog, LogLevel::Error,
                ("0x%p   TSFTextStore::SetSelectionInternal() FAILED due to "
                 "RecordCompositionUpdateAction() failure",
                 this));
        return hr;
      }
    }
    return S_OK;
  }

  TS_SELECTION_ACP selectionInContent(*pSelection);

  // If mContentForTSF caches old contents which is now different from
  // actual contents, we need some complicated hack here...
  // Note that this hack assumes that this is used for reconversion.
  if (mContentForTSF.isSome() && mPendingTextChangeData.IsValid() &&
      !mPendingTextChangeData.mCausedOnlyByComposition) {
    uint32_t startOffset = static_cast<uint32_t>(selectionInContent.acpStart);
    uint32_t endOffset = static_cast<uint32_t>(selectionInContent.acpEnd);
    if (mPendingTextChangeData.mStartOffset >= endOffset) {
      // Setting selection before any changed ranges is fine.
    } else if (mPendingTextChangeData.mRemovedEndOffset <= startOffset) {
      // Setting selection after removed range is fine with following
      // adjustment.
      selectionInContent.acpStart += mPendingTextChangeData.Difference();
      selectionInContent.acpEnd += mPendingTextChangeData.Difference();
    } else if (startOffset == endOffset) {
      // Moving caret position may be fine in most cases even if the insertion
      // point has already gone but in this case, composition will be inserted
      // to unexpected position, though.
      // It seems that moving caret into middle of the new text is odd.
      // Perhaps, end of it is expected by users in most cases.
      selectionInContent.acpStart = mPendingTextChangeData.mAddedEndOffset;
      selectionInContent.acpEnd = selectionInContent.acpStart;
    } else {
      // Otherwise, i.e., setting range has already gone, we cannot set
      // selection properly.
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::SetSelectionInternal() FAILED due to "
               "there is unknown content change",
               this));
      return E_FAIL;
    }
  }

  CompleteLastActionIfStillIncomplete();
  PendingAction* action = mPendingActions.AppendElement();
  action->mType = PendingAction::Type::SetSelection;
  action->mSelectionStart = selectionInContent.acpStart;
  action->mSelectionLength =
      selectionInContent.acpEnd - selectionInContent.acpStart;
  action->mSelectionReversed = (selectionInContent.style.ase == TS_AE_START);

  // Use TSF specified selection for updating mSelectionForTSF.
  selectionForTSF->SetSelection(*pSelection);

  return S_OK;
}

STDMETHODIMP TSFTextStore::SetSelection(ULONG ulCount,
                                        const TS_SELECTION_ACP* pSelection) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::SetSelection(), mComposition=%s", this,
           ToString(mComposition).c_str()));

  HRESULT hr = TSFTextStoreBase::SetSelection(ulCount, pSelection);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  hr = SetSelectionInternal(pSelection, true);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::SetSelection() FAILED due to "
             "SetSelectionInternal() failure",
             this));
  } else {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::SetSelection() succeeded", this));
  }
  return hr;
}

STDMETHODIMP TSFTextStore::GetText(LONG acpStart, LONG acpEnd, WCHAR* pchPlain,
                                   ULONG cchPlainReq, ULONG* pcchPlainOut,
                                   TS_RUNINFO* prgRunInfo, ULONG ulRunInfoReq,
                                   ULONG* pulRunInfoOut, LONG* pacpNext) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::GetText(), mComposition=%s", this,
           ToString(mComposition).c_str()));

  HRESULT hr = TSFTextStoreBase::GetText(acpStart, acpEnd, pchPlain,
                                         cchPlainReq, pcchPlainOut, prgRunInfo,
                                         ulRunInfoReq, pulRunInfoOut, pacpNext);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  Maybe<Content>& contentForTSF = ContentForTSF();
  if (contentForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetText() FAILED due to "
             "ContentForTSF() failure",
             this));
    return E_FAIL;
  }
  if (contentForTSF->TextRef().Length() < static_cast<uint32_t>(acpStart)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetText() FAILED due to "
             "acpStart is larger offset than the actual text length",
             this));
    return TS_E_INVALIDPOS;
  }
  if (acpEnd != -1 &&
      contentForTSF->TextRef().Length() < static_cast<uint32_t>(acpEnd)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetText() FAILED due to "
             "acpEnd is larger offset than the actual text length",
             this));
    return TS_E_INVALIDPOS;
  }
  uint32_t length = (acpEnd == -1) ? contentForTSF->TextRef().Length() -
                                         static_cast<uint32_t>(acpStart)
                                   : static_cast<uint32_t>(acpEnd - acpStart);
  if (cchPlainReq && cchPlainReq - 1 < length) {
    length = cchPlainReq - 1;
  }
  if (length) {
    if (pchPlain && cchPlainReq) {
      const char16_t* startChar =
          contentForTSF->TextRef().BeginReading() + acpStart;
      memcpy(pchPlain, startChar, length * sizeof(*pchPlain));
      pchPlain[length] = 0;
      *pcchPlainOut = length;
    }
    if (prgRunInfo && ulRunInfoReq) {
      prgRunInfo->uCount = length;
      prgRunInfo->type = TS_RT_PLAIN;
      if (pulRunInfoOut) *pulRunInfoOut = 1;
    }
    if (pacpNext) *pacpNext = acpStart + length;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::GetText() succeeded: pcchPlainOut=0x%p, "
           "*prgRunInfo={ uCount=%lu, type=%s }, *pulRunInfoOut=%lu, "
           "*pacpNext=%ld)",
           this, pcchPlainOut, prgRunInfo ? prgRunInfo->uCount : 0,
           prgRunInfo ? mozilla::ToString(prgRunInfo->type).c_str() : "N/A",
           pulRunInfoOut ? *pulRunInfoOut : 0, pacpNext ? *pacpNext : 0));
  return S_OK;
}

STDMETHODIMP TSFTextStore::SetText(DWORD dwFlags, LONG acpStart, LONG acpEnd,
                                   const WCHAR* pchText, ULONG cch,
                                   TS_TEXTCHANGE* pChange) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::GetText(), mComposition=%s", this,
           ToString(mComposition).c_str()));

  HRESULT hr = TSFTextStoreBase::SetText(dwFlags, acpStart, acpEnd, pchText,
                                         cch, pChange);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  TS_SELECTION_ACP selection;
  selection.acpStart = acpStart;
  selection.acpEnd = acpEnd;
  selection.style.ase = TS_AE_END;
  selection.style.fInterimChar = 0;
  // Set selection to desired range
  hr = SetSelectionInternal(&selection);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::SetText() FAILED due to "
             "SetSelectionInternal() failure",
             this));
    return hr;
  }
  // Replace just selected text
  if (!InsertTextAtSelectionInternal(nsDependentSubstring(pchText, cch),
                                     pChange)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::SetText() FAILED due to "
             "InsertTextAtSelectionInternal() failure",
             this));
    return E_FAIL;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::SetText() succeeded: pChange={ "
           "acpStart=%ld, acpOldEnd=%ld, acpNewEnd=%ld }",
           this, pChange ? pChange->acpStart : 0,
           pChange ? pChange->acpOldEnd : 0, pChange ? pChange->acpNewEnd : 0));
  return S_OK;
}

STDMETHODIMP TSFTextStore::RequestSupportedAttrs(
    DWORD dwFlags, ULONG cFilterAttrs, const TS_ATTRID* paFilterAttrs) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::RequestSupportedAttrs(dwFlags=%s, "
           "cFilterAttrs=%lu)",
           this, AutoFindFlagsCString(dwFlags).get(), cFilterAttrs));

  return HandleRequestAttrs(dwFlags, cFilterAttrs, paFilterAttrs,
                            TSFUtils::NUM_OF_SUPPORTED_ATTRS);
}

STDMETHODIMP TSFTextStore::RequestAttrsAtPosition(
    LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID* paFilterAttrs,
    DWORD dwFlags) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::RequestAttrsAtPosition(acpPos=%ld, "
           "cFilterAttrs=%lu, dwFlags=%s)",
           this, acpPos, cFilterAttrs, AutoFindFlagsCString(dwFlags).get()));

  return HandleRequestAttrs(dwFlags | TS_ATTR_FIND_WANT_VALUE, cFilterAttrs,
                            paFilterAttrs, TSFUtils::NUM_OF_SUPPORTED_ATTRS);
}

STDMETHODIMP TSFTextStore::RetrieveRequestedAttrs(ULONG ulCount,
                                                  TS_ATTRVAL* paAttrVals,
                                                  ULONG* pcFetched) {
  HRESULT hr = RetrieveRequestedAttrsInternal(ulCount, paAttrVals, pcFetched,
                                              TSFUtils::NUM_OF_SUPPORTED_ATTRS);
  if (FAILED(hr)) {
    return hr;
  }
  if (*pcFetched) {
    return S_OK;
  }
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::RetrieveRequestedAttrs() called "
           "for unknown TS_ATTRVAL, *pcFetched=0 (S_OK)",
           this));
  return S_OK;
}

STDMETHODIMP TSFTextStore::GetEndACP(LONG* pacp) {
  HRESULT hr = TSFTextStoreBase::GetEndACP(pacp);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }
  Maybe<Content>& contentForTSF = ContentForTSF();
  if (contentForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetEndACP() FAILED due to "
             "ContentForTSF() failure",
             this));
    return E_FAIL;
  }
  *pacp = static_cast<LONG>(contentForTSF->TextRef().Length());
  return S_OK;
}

STDMETHODIMP TSFTextStore::GetACPFromPoint(TsViewCookie vcView, const POINT* pt,
                                           DWORD dwFlags, LONG* pacp) {
  HRESULT hr = TSFTextStoreBase::GetACPFromPoint(vcView, pt, dwFlags, pacp);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  mWaitingQueryLayout = false;

  if (mDestroyed ||
      (mContentForTSF.isSome() && mContentForTSF->IsLayoutChanged())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetACPFromPoint() returned "
             "TS_E_NOLAYOUT",
             this));
    mHasReturnedNoLayoutError = true;
    return TS_E_NOLAYOUT;
  }

  LayoutDeviceIntPoint ourPt(pt->x, pt->y);
  // Convert to widget relative coordinates from screen's.
  ourPt -= mWidget->WidgetToScreenOffset();

  // NOTE: Don't check if the point is in the widget since the point can be
  //       outside of the widget if focused editor is in a XUL <panel>.

  WidgetQueryContentEvent queryCharAtPointEvent(true, eQueryCharacterAtPoint,
                                                mWidget);
  mWidget->InitEvent(queryCharAtPointEvent, &ourPt);

  // FYI: WidgetQueryContentEvent may cause flushing pending layout and it
  //      may cause focus change or something.
  RefPtr<TSFTextStore> kungFuDeathGrip(this);
  DispatchEvent(queryCharAtPointEvent);
  if (!mWidget || mWidget->Destroyed()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetACPFromPoint() FAILED due to "
             "mWidget was destroyed during eQueryCharacterAtPoint",
             this));
    return E_FAIL;
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::GetACPFromPoint(), queryCharAtPointEvent={ "
           "mReply=%s }",
           this, ToString(queryCharAtPointEvent.mReply).c_str()));

  if (NS_WARN_IF(queryCharAtPointEvent.Failed())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetACPFromPoint() FAILED due to "
             "eQueryCharacterAtPoint failure",
             this));
    return E_FAIL;
  }

  // If dwFlags isn't set and the point isn't in any character's bounding box,
  // we should return TS_E_INVALIDPOINT.
  if (!(dwFlags & GXFPF_NEAREST) && queryCharAtPointEvent.DidNotFindChar()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetACPFromPoint() FAILED due to the "
             "point contained by no bounding box",
             this));
    return TS_E_INVALIDPOINT;
  }

  // Although, we're not sure if mTentativeCaretOffset becomes NOT_FOUND,
  // let's assume that there is no content in such case.
  NS_WARNING_ASSERTION(queryCharAtPointEvent.DidNotFindTentativeCaretOffset(),
                       "Tentative caret offset was not found");

  uint32_t offset;

  // If dwFlags includes GXFPF_ROUND_NEAREST, we should return tentative
  // caret offset (MSDN calls it "range position").
  if (dwFlags & GXFPF_ROUND_NEAREST) {
    offset = queryCharAtPointEvent.mReply->mTentativeCaretOffset.valueOr(0);
  } else if (queryCharAtPointEvent.FoundChar()) {
    // Otherwise, we should return character offset whose bounding box contains
    // the point.
    offset = queryCharAtPointEvent.mReply->StartOffset();
  } else {
    // If the point isn't in any character's bounding box but we need to return
    // the nearest character from the point, we should *guess* the character
    // offset since there is no inexpensive API to check it strictly.
    // XXX If we retrieve 2 bounding boxes, one is before the offset and
    //     the other is after the offset, we could resolve the offset.
    //     However, dispatching 2 eQueryTextRect may be expensive.

    // So, use tentative offset for now.
    offset = queryCharAtPointEvent.mReply->mTentativeCaretOffset.valueOr(0);

    // However, if it's after the last character, we need to decrement the
    // offset.
    Maybe<Content>& contentForTSF = ContentForTSF();
    if (contentForTSF.isNothing()) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::GetACPFromPoint() FAILED due to "
               "ContentForTSF() failure",
               this));
      return E_FAIL;
    }
    if (contentForTSF->TextRef().Length() <= offset) {
      // If the tentative caret is after the last character, let's return
      // the last character's offset.
      offset = contentForTSF->TextRef().Length() - 1;
    }
  }

  if (NS_WARN_IF(offset > LONG_MAX)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetACPFromPoint() FAILED due to out of "
             "range of the result",
             this));
    return TS_E_INVALIDPOINT;
  }

  *pacp = static_cast<LONG>(offset);
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::GetACPFromPoint() succeeded: *pacp=%ld", this,
           *pacp));
  return S_OK;
}

STDMETHODIMP TSFTextStore::GetTextExt(TsViewCookie vcView, LONG acpStart,
                                      LONG acpEnd, RECT* prc, BOOL* pfClipped) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::GetTextExt(), mContentForTSF=%s, "
           "mSelectionForTSF=%s, mComposition=%s",
           this, mozilla::ToString(mContentForTSF).c_str(),
           ToString(mSelectionForTSF).c_str(), ToString(mComposition).c_str()));
  HRESULT hr =
      TSFTextStoreBase::GetTextExt(vcView, acpStart, acpEnd, prc, pfClipped);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  mWaitingQueryLayout = false;

  if (IsHandlingCompositionInContent() && mContentForTSF.isSome() &&
      mContentForTSF->HasOrHadComposition() &&
      mContentForTSF->IsLayoutChanged() &&
      mContentForTSF->MinModifiedOffset().value() >
          static_cast<uint32_t>(LONG_MAX)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetTextExt(), FAILED due to the text "
             "is too big for TSF (cannot treat modified offset as LONG), "
             "mContentForTSF=%s",
             this, ToString(mContentForTSF).c_str()));
    return E_FAIL;
  }

  // At Windows 10 build 17643 (an insider preview for RS5), Microsoft fixed
  // the bug of TS_E_NOLAYOUT (even when we returned TS_E_NOLAYOUT, TSF
  // returned E_FAIL to TIP).  However, until we drop to support older Windows
  // and all TIPs are aware of TS_E_NOLAYOUT result, we need to keep returning
  // S_OK and available rectangle only for them.
  if (!MaybeHackNoErrorLayoutBugs(acpStart, acpEnd) &&
      mContentForTSF.isSome() && mContentForTSF->IsLayoutChangedAt(acpEnd)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetTextExt() returned TS_E_NOLAYOUT "
             "(acpEnd=%ld)",
             this, acpEnd));
    mHasReturnedNoLayoutError = true;
    return TS_E_NOLAYOUT;
  }

  if (mDestroyed) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetTextExt() returned TS_E_NOLAYOUT "
             "(acpEnd=%ld) because this has already been destroyed",
             this, acpEnd));
    mHasReturnedNoLayoutError = true;
    return TS_E_NOLAYOUT;
  }

  // use eQueryTextRect to get rect in system, screen coordinates
  WidgetQueryContentEvent queryTextRectEvent(true, eQueryTextRect, mWidget);
  mWidget->InitEvent(queryTextRectEvent);

  WidgetQueryContentEvent::Options options;
  int64_t startOffset = acpStart;
  if (mComposition.isSome()) {
    // If there is a composition, TSF must want character rects related to
    // the composition.  Therefore, we should use insertion point relative
    // query because the composition might be at different position from
    // the position where TSFTextStore believes it at.
    options.mRelativeToInsertionPoint = true;
    startOffset -= mComposition->StartOffset();
  } else if (IsHandlingCompositionInParent() && mContentForTSF.isSome() &&
             mContentForTSF->HasOrHadComposition()) {
    // If there was a composition and its commit event hasn't been dispatched
    // yet, ContentCacheInParent is still open for relative offset query from
    // the latest composition.
    options.mRelativeToInsertionPoint = true;
    startOffset -= mContentForTSF->LatestCompositionRange()->StartOffset();
  } else if (!CanAccessActualContentDirectly() &&
             mSelectionForTSF->HasRange()) {
    // If TSF/TIP cannot access actual content directly, there may be pending
    // text and/or selection changes which have not been notified TSF yet.
    // Therefore, we should use relative to insertion point query since
    // TSF/TIP computes the offset from the cached selection.
    options.mRelativeToInsertionPoint = true;
    startOffset -= mSelectionForTSF->StartOffset();
  }
  // ContentEventHandler and ContentCache return actual caret rect when
  // the queried range is collapsed and selection is collapsed at the
  // queried range.  Then, its height (in horizontal layout, width in vertical
  // layout) may be different from actual font height of the line.  In such
  // case, users see "dancing" of candidate or suggest window of TIP.
  // For preventing it, we should query text rect with at least 1 length.
  uint32_t length = std::max(static_cast<int32_t>(acpEnd - acpStart), 1);
  queryTextRectEvent.InitForQueryTextRect(startOffset, length, options);

  DispatchEvent(queryTextRectEvent);
  if (NS_WARN_IF(queryTextRectEvent.Failed())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetTextExt() FAILED due to "
             "eQueryTextRect failure",
             this));
    return TS_E_INVALIDPOS;  // but unexpected failure, maybe.
  }

  // IMEs don't like empty rects, fix here
  if (queryTextRectEvent.mReply->mRect.Width() <= 0) {
    queryTextRectEvent.mReply->mRect.SetWidth(1);
  }
  if (queryTextRectEvent.mReply->mRect.Height() <= 0) {
    queryTextRectEvent.mReply->mRect.SetHeight(1);
  }

  // convert to unclipped screen rect
  nsWindow* refWindow =
      static_cast<nsWindow*>(!!queryTextRectEvent.mReply->mFocusedWidget
                                 ? queryTextRectEvent.mReply->mFocusedWidget
                                 : static_cast<nsIWidget*>(mWidget.get()));
  // Result rect is in top level widget coordinates
  refWindow = refWindow->GetTopLevelWindow(false);
  if (!refWindow) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetTextExt() FAILED due to "
             "no top level window",
             this));
    return E_FAIL;
  }

  queryTextRectEvent.mReply->mRect.MoveBy(refWindow->WidgetToScreenOffset());

  // get bounding screen rect to test for clipping
  if (!GetScreenExtInternal(*prc)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::GetTextExt() FAILED due to "
             "GetScreenExtInternal() failure",
             this));
    return E_FAIL;
  }

  // clip text rect to bounding rect
  RECT textRect;
  ::SetRect(&textRect, queryTextRectEvent.mReply->mRect.X(),
            queryTextRectEvent.mReply->mRect.Y(),
            queryTextRectEvent.mReply->mRect.XMost(),
            queryTextRectEvent.mReply->mRect.YMost());
  if (!::IntersectRect(prc, prc, &textRect))
    // Text is not visible
    ::SetRectEmpty(prc);

  // not equal if text rect was clipped
  *pfClipped = !::EqualRect(prc, &textRect);

  // ATOK 2011 - 2016 refers native caret position and size on windows whose
  // class name is one of Mozilla's windows for deciding candidate window
  // position.  Additionally, ATOK 2015 and earlier behaves really odd when
  // we don't create native caret.  Therefore, we need to create native caret
  // only when ATOK 2011 - 2015 is active (i.e., not necessary for ATOK 2016).
  // However, if a11y module is handling native caret, we shouldn't touch it.
  // Note that ATOK must require the latest information of the caret.  So,
  // even if we'll create native caret later, we need to creat it here with
  // current information.
  if (!IMEHandler::IsA11yHandlingNativeCaret() &&
      StaticPrefs::intl_tsf_hack_atok_create_native_caret() &&
      TSFStaticSink::IsATOKReferringNativeCaretActive() &&
      mComposition.isSome() &&
      mComposition->IsOffsetInRangeOrEndOffset(acpStart) &&
      mComposition->IsOffsetInRangeOrEndOffset(acpEnd)) {
    CreateNativeCaret();
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::GetTextExt() succeeded: "
           "*prc={ left=%ld, top=%ld, right=%ld, bottom=%ld }, *pfClipped=%s",
           this, prc->left, prc->top, prc->right, prc->bottom,
           TSFUtils::BoolToChar(*pfClipped)));

  return S_OK;
}

bool TSFTextStore::MaybeHackNoErrorLayoutBugs(LONG& aACPStart, LONG& aACPEnd) {
  // When ITextStoreACP::GetTextExt() returns TS_E_NOLAYOUT, TSF returns E_FAIL
  // to its caller (typically, active TIP).  Then, most TIPs abort current job
  // or treat such application as non-GUI apps.  E.g., some of them give up
  // showing candidate window, some others show candidate window at top-left of
  // the screen.  For avoiding this issue, when there is composition (until
  // composition is actually committed in remote content), we should not
  // return TS_E_NOLAYOUT error for TIPs whose some features are broken by
  // this issue.
  // Note that ideally, this issue should be avoided by each TIP since this
  // won't be fixed at least on non-latest Windows.  Actually, Google Japanese
  // Input (based on Mozc) does it.  When GetTextExt() returns E_FAIL, TIPs
  // should try to check result of GetRangeFromPoint() because TSF returns
  // TS_E_NOLAYOUT correctly in this case. See:
  // https://github.com/google/mozc/blob/6b878e31fb6ac4347dc9dfd8ccc1080fe718479f/src/win32/tip/tip_range_util.cc#L237-L257

  if (!IsHandlingCompositionInContent() || mContentForTSF.isNothing() ||
      !mContentForTSF->HasOrHadComposition() ||
      !mContentForTSF->IsLayoutChangedAt(aACPEnd)) {
    return false;
  }

  MOZ_ASSERT(mComposition.isNothing() ||
             mComposition->StartOffset() ==
                 mContentForTSF->LatestCompositionRange()->StartOffset());
  MOZ_ASSERT(mComposition.isNothing() ||
             mComposition->EndOffset() ==
                 mContentForTSF->LatestCompositionRange()->EndOffset());

  // If TSF does not have the bug, we need to hack only with a few TIPs.
  static const bool sAlllowToStopHackingIfFine =
      IsWindows10BuildOrLater(17643) &&
      StaticPrefs::
          intl_tsf_hack_allow_to_stop_hacking_on_build_17643_or_later();

  // We need to compute active TIP now.  This may take a couple of milliseconds,
  // however, it'll be cached, so, must be faster than check active TIP every
  // GetTextExt() calls.
  const Maybe<Selection>& selectionForTSF = SelectionForTSF();
  switch (TSFStaticSink::ActiveTIP()) {
    // MS IME for Japanese doesn't support asynchronous handling at deciding
    // its suggest list window position.  The feature was implemented
    // starting from Windows 8.  And also we may meet same trouble in e10s
    // mode on Win7.  So, we should never return TS_E_NOLAYOUT to MS IME for
    // Japanese.
    case TextInputProcessorID::MicrosoftIMEForJapanese:
      // Basically, MS-IME tries to retrieve whole composition string rect
      // at deciding suggest window immediately after unlocking the document.
      // However, in e10s mode, the content hasn't updated yet in most cases.
      // Therefore, if the first character at the retrieving range rect is
      // available, we should use it as the result.
      // Note that according to bug 1609675, MS-IME for Japanese itself does
      // not handle TS_E_NOLAYOUT correctly at least on Build 18363.657 (1909).
      if (StaticPrefs::
              intl_tsf_hack_ms_japanese_ime_do_not_return_no_layout_error_at_first_char() &&
          aACPStart < aACPEnd) {
        aACPEnd = aACPStart;
        break;
      }
      if (sAlllowToStopHackingIfFine) {
        return false;
      }
      // Although, the condition is not clear, MS-IME sometimes retrieves the
      // caret rect immediately after modifying the composition string but
      // before unlocking the document.  In such case, we should return the
      // nearest character rect.
      // (Let's return true if there is no selection which must be not expected
      // by MS-IME nor TSF.)
      if (StaticPrefs::
              intl_tsf_hack_ms_japanese_ime_do_not_return_no_layout_error_at_caret() &&
          aACPStart == aACPEnd && selectionForTSF.isSome() &&
          (!selectionForTSF->HasRange() ||
           (selectionForTSF->Collapsed() &&
            selectionForTSF->EndOffset() == aACPEnd))) {
        int32_t minOffsetOfLayoutChanged =
            static_cast<int32_t>(mContentForTSF->MinModifiedOffset().value());
        aACPEnd = aACPStart = std::max(minOffsetOfLayoutChanged - 1, 0);
      } else {
        return false;
      }
      break;
    // The bug of Microsoft Office IME 2010 for Japanese is similar to
    // MS-IME for Win 8.1 and Win 10.  Newer version of MS Office IME is not
    // released yet.  So, we can hack it without prefs  because there must be
    // no developers who want to disable this hack for tests.
    // XXX We have not tested with Microsoft Office IME 2010 since it's
    //     installable only with Win7 and Win8 (i.e., cannot install Win8.1
    //     and Win10), and requires upgrade to Win10.
    case TextInputProcessorID::MicrosoftOfficeIME2010ForJapanese:
      // Basically, MS-IME tries to retrieve whole composition string rect
      // at deciding suggest window immediately after unlocking the document.
      // However, in e10s mode, the content hasn't updated yet in most cases.
      // Therefore, if the first character at the retrieving range rect is
      // available, we should use it as the result.
      if (aACPStart < aACPEnd) {
        aACPEnd = aACPStart;
      }
      // Although, the condition is not clear, MS-IME sometimes retrieves the
      // caret rect immediately after modifying the composition string but
      // before unlocking the document.  In such case, we should return the
      // nearest character rect.
      // (Let's return true if there is no selection which must be not expected
      // by MS-IME nor TSF.)
      else if (aACPStart == aACPEnd && selectionForTSF.isSome() &&
               (!selectionForTSF->HasRange() ||
                (selectionForTSF->Collapsed() &&
                 selectionForTSF->EndOffset() == aACPEnd))) {
        int32_t minOffsetOfLayoutChanged =
            static_cast<int32_t>(mContentForTSF->MinModifiedOffset().value());
        aACPEnd = aACPStart = std::max(minOffsetOfLayoutChanged - 1, 0);
      } else {
        return false;
      }
      break;
    // ATOK fails to handle TS_E_NOLAYOUT only when it decides the position of
    // suggest window.  In such case, ATOK tries to query rect of whole or a
    // part of composition string.
    // FYI: ATOK changes their implementation around candidate window and
    //      suggest widget at ATOK 2016.  Therefore, there are some differences
    //      ATOK 2015 (or older) and ATOK 2016 (or newer).
    // FYI: ATOK 2017 stops referring our window class name.  I.e., ATOK 2016
    //      and older may behave differently only on Gecko but this must be
    //      finished from ATOK 2017.
    // FYI: For testing with legacy ATOK, we should hack it even if current ATOK
    //      refers native caret rect on windows whose window class is one of
    //      Mozilla window classes and we stop creating native caret for ATOK
    //      because creating native caret causes ATOK refers caret position
    //      when GetTextExt() returns TS_E_NOLAYOUT.
    case TextInputProcessorID::ATOK2011:
    case TextInputProcessorID::ATOK2012:
    case TextInputProcessorID::ATOK2013:
    case TextInputProcessorID::ATOK2014:
    case TextInputProcessorID::ATOK2015:
      // ATOK 2016 and later may temporarily show candidate window at odd
      // position when you convert a word quickly (e.g., keep pressing
      // space bar).  So, on ATOK 2016 or later, we need to keep hacking the
      // result of GetTextExt().
      if (sAlllowToStopHackingIfFine) {
        return false;
      }
      // If we'll create native caret where we paint our caret.  Then, ATOK
      // will refer native caret.  So, we don't need to hack anything in
      // this case.
      if (StaticPrefs::intl_tsf_hack_atok_create_native_caret()) {
        MOZ_ASSERT(TSFStaticSink::IsATOKReferringNativeCaretActive());
        return false;
      }
      [[fallthrough]];
    case TextInputProcessorID::ATOK2016:
    case TextInputProcessorID::ATOKUnknown:
      if (!StaticPrefs::
              intl_tsf_hack_atok_do_not_return_no_layout_error_of_composition_string()) {
        return false;
      }
      // If the range is in the composition string, we should return rectangle
      // in it as far as possible.
      if (!mContentForTSF->LatestCompositionRange()->IsOffsetInRangeOrEndOffset(
              aACPStart) ||
          !mContentForTSF->LatestCompositionRange()->IsOffsetInRangeOrEndOffset(
              aACPEnd)) {
        return false;
      }
      break;
    // Japanist 10 fails to handle TS_E_NOLAYOUT when it decides the position
    // of candidate window.  In such case, Japanist shows candidate window at
    // top-left of the screen.  So, we should return the nearest caret rect
    // where we know.  This is Japanist's bug.  So, even after build 17643,
    // we need this hack.
    case TextInputProcessorID::Japanist10:
      if (!StaticPrefs::
              intl_tsf_hack_japanist10_do_not_return_no_layout_error_of_composition_string()) {
        return false;
      }
      if (!mContentForTSF->LatestCompositionRange()->IsOffsetInRangeOrEndOffset(
              aACPStart) ||
          !mContentForTSF->LatestCompositionRange()->IsOffsetInRangeOrEndOffset(
              aACPEnd)) {
        return false;
      }
      break;
    // Free ChangJie 2010 doesn't handle ITfContextView::GetTextExt() properly.
    // This must be caused by the bug of TSF since Free ChangJie works fine on
    // build 17643 and later.
    case TextInputProcessorID::FreeChangJie:
      if (sAlllowToStopHackingIfFine) {
        return false;
      }
      if (!StaticPrefs::
              intl_tsf_hack_free_chang_jie_do_not_return_no_layout_error()) {
        return false;
      }
      aACPEnd = mContentForTSF->LatestCompositionRange()->StartOffset();
      aACPStart = std::min(aACPStart, aACPEnd);
      break;
    // Some Traditional Chinese TIPs of Microsoft don't show candidate window
    // in e10s mode on Win8 or later.
    case TextInputProcessorID::MicrosoftQuick:
      if (sAlllowToStopHackingIfFine) {
        return false;  // MS Quick works fine with Win10 build 17643.
      }
      [[fallthrough]];
    case TextInputProcessorID::MicrosoftChangJie:
      if (!StaticPrefs::
              intl_tsf_hack_ms_traditional_chinese_do_not_return_no_layout_error()) {
        return false;
      }
      aACPEnd = mContentForTSF->LatestCompositionRange()->StartOffset();
      aACPStart = std::min(aACPStart, aACPEnd);
      break;
    // Some Simplified Chinese TIPs of Microsoft don't show candidate window
    // in e10s mode on Win8 or later.
    // FYI: Only Simplified Chinese TIPs of Microsoft still require this hack
    //      because they sometimes do not show candidate window when we return
    //      TS_E_NOLAYOUT for first query.  Note that even when they show
    //      candidate window properly, we return TS_E_NOLAYOUT and following
    //      log looks same as when they don't show candidate window.  Perhaps,
    //      there is stateful cause or race in them.
    case TextInputProcessorID::MicrosoftPinyin:
    case TextInputProcessorID::MicrosoftWubi:
      if (!StaticPrefs::
              intl_tsf_hack_ms_simplified_chinese_do_not_return_no_layout_error()) {
        return false;
      }
      aACPEnd = mContentForTSF->LatestCompositionRange()->StartOffset();
      aACPStart = std::min(aACPStart, aACPEnd);
      break;
    default:
      return false;
  }

  // If we hack the queried range for active TIP, that means we should not
  // return TS_E_NOLAYOUT even if hacked offset is still modified.  So, as
  // far as possible, we should adjust the offset.
  MOZ_ASSERT(mContentForTSF->IsLayoutChanged());
  bool collapsed = aACPStart == aACPEnd;
  // Note that even if all characters in the editor or the composition
  // string was modified, 0 or start offset of the composition string is
  // useful because it may return caret rect or old character's rect which
  // the user still see.  That must be useful information for TIP.
  int32_t firstModifiedOffset =
      static_cast<int32_t>(mContentForTSF->MinModifiedOffset().value());
  LONG lastUnmodifiedOffset = std::max(firstModifiedOffset - 1, 0);
  if (mContentForTSF->IsLayoutChangedAt(aACPStart)) {
    if (aACPStart >= mContentForTSF->LatestCompositionRange()->StartOffset()) {
      // If mContentForTSF has last composition string and current
      // composition string, we can assume that ContentCacheInParent has
      // cached rects of composition string at least length of current
      // composition string.  Otherwise, we can assume that rect for
      // first character of composition string is stored since it was
      // selection start or caret position.
      LONG maxCachedOffset =
          mContentForTSF->LatestCompositionRange()->EndOffset();
      if (mContentForTSF->LastComposition().isSome()) {
        maxCachedOffset = std::min(
            maxCachedOffset, mContentForTSF->LastComposition()->EndOffset());
      }
      aACPStart = std::min(aACPStart, maxCachedOffset);
    }
    // Otherwise, we don't know which character rects are cached.  So, we
    // need to use first unmodified character's rect in this case.  Even
    // if there is no character, the query event will return caret rect
    // instead.
    else {
      aACPStart = lastUnmodifiedOffset;
    }
    MOZ_ASSERT(aACPStart <= aACPEnd);
  }

  // If TIP requests caret rect with collapsed range, we should keep
  // collapsing the range.
  if (collapsed) {
    aACPEnd = aACPStart;
  }
  // Let's set aACPEnd to larger offset of last unmodified offset or
  // aACPStart which may be the first character offset of the composition
  // string.  However, some TIPs may want to know the right edge of the
  // range.  Therefore, if aACPEnd is in composition string and active TIP
  // doesn't retrieve caret rect (i.e., the range isn't collapsed), we
  // should keep using the original aACPEnd.  Otherwise, we should set
  // aACPEnd to larger value of aACPStart and lastUnmodifiedOffset.
  else if (mContentForTSF->IsLayoutChangedAt(aACPEnd) &&
           !mContentForTSF->LatestCompositionRange()
                ->IsOffsetInRangeOrEndOffset(aACPEnd)) {
    aACPEnd = std::max(aACPStart, lastUnmodifiedOffset);
  }

  MOZ_LOG(
      gIMELog, LogLevel::Debug,
      ("0x%p   TSFTextStore::HackNoErrorLayoutBugs() hacked the queried range "
       "for not returning TS_E_NOLAYOUT, new values are: "
       "aACPStart=%ld, aACPEnd=%ld",
       this, aACPStart, aACPEnd));

  return true;
}

STDMETHODIMP TSFTextStore::InsertTextAtSelection(DWORD dwFlags,
                                                 const WCHAR* pchText,
                                                 ULONG cch, LONG* pacpStart,
                                                 LONG* pacpEnd,
                                                 TS_TEXTCHANGE* pChange) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::InsertTextAtSelection(), mComposition=%s", this,
           ToString(mComposition).c_str()));

  HRESULT hr = TSFTextStoreBase::InsertTextAtSelection(
      dwFlags, pchText, cch, pacpStart, pacpEnd, pChange);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  if (TS_IAS_QUERYONLY == dwFlags) {
    // Get selection first
    Maybe<Selection>& selectionForTSF = SelectionForTSF();
    if (selectionForTSF.isNothing()) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::InsertTextAtSelection() FAILED due to "
               "SelectionForTSF() failure",
               this));
      return E_FAIL;
    }

    // Simulate text insertion
    if (selectionForTSF->HasRange()) {
      *pacpStart = selectionForTSF->StartOffset();
      *pacpEnd = selectionForTSF->EndOffset();
      if (pChange) {
        *pChange = TS_TEXTCHANGE{.acpStart = selectionForTSF->StartOffset(),
                                 .acpOldEnd = selectionForTSF->EndOffset(),
                                 .acpNewEnd = selectionForTSF->StartOffset() +
                                              static_cast<LONG>(cch)};
      }
    } else {
      // There is no error code to return "no selection" state from this method.
      // This means that TSF/TIP should check `GetSelection` result first and
      // stop using this.  However, this could be called by TIP/TSF if they do
      // not do so.  Therefore, we should use start of editor instead, but
      // notify the caller of nothing will be inserted with pChange->acpNewEnd.
      *pacpStart = *pacpEnd = 0;
      if (pChange) {
        *pChange = TS_TEXTCHANGE{.acpStart = 0, .acpOldEnd = 0, .acpNewEnd = 0};
      }
    }
  } else {
    if (!InsertTextAtSelectionInternal(nsDependentSubstring(pchText, cch),
                                       pChange)) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::InsertTextAtSelection() FAILED due to "
               "InsertTextAtSelectionInternal() failure",
               this));
      return E_FAIL;
    }

    if (TS_IAS_NOQUERY != dwFlags) {
      *pacpStart = pChange->acpStart;
      *pacpEnd = pChange->acpNewEnd;
    }
  }
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::InsertTextAtSelection() succeeded: "
           "*pacpStart=%ld, *pacpEnd=%ld, "
           "*pChange={ acpStart=%ld, acpOldEnd=%ld, acpNewEnd=%ld })",
           this, pacpStart ? *pacpStart : 0, pacpEnd ? *pacpEnd : 0,
           pChange ? pChange->acpStart : 0, pChange ? pChange->acpOldEnd : 0,
           pChange ? pChange->acpNewEnd : 0));
  return S_OK;
}

bool TSFTextStore::InsertTextAtSelectionInternal(const nsAString& aInsertStr,
                                                 TS_TEXTCHANGE* aTextChange) {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::InsertTextAtSelectionInternal("
           "aInsertStr=\"%s\", aTextChange=0x%p), mComposition=%s",
           this, AutoEscapedUTF8String(aInsertStr).get(), aTextChange,
           ToString(mComposition).c_str()));

  Maybe<Content>& contentForTSF = ContentForTSF();
  if (contentForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::InsertTextAtSelectionInternal() failed "
             "due to ContentForTSF() failure()",
             this));
    return false;
  }

  MaybeDispatchKeyboardEventAsProcessedByIME();
  if (mDestroyed) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStore::InsertTextAtSelectionInternal() FAILED due to "
         "destroyed during dispatching a keyboard event",
         this));
    return false;
  }

  const auto numberOfCRLFs = [&]() -> uint32_t {
    const auto* str = aInsertStr.BeginReading();
    uint32_t num = 0;
    for (uint32_t i = 0; i + 1 < aInsertStr.Length(); i++) {
      if (str[i] == '\r' && str[i + 1] == '\n') {
        num++;
        i++;
      }
    }
    return num;
  }();
  if (numberOfCRLFs) {
    nsAutoString key;
    if (TSFStaticSink::GetActiveTIPNameForTelemetry(key)) {
      glean::widget::ime_name_on_windows_inserted_crlf
          .Get(NS_ConvertUTF16toUTF8(key))
          .Set(true);
    }
  }

  TS_SELECTION_ACP oldSelection = contentForTSF->Selection()->ACPRef();
  if (mComposition.isNothing()) {
    // Use a temporary composition to contain the text
    PendingAction* compositionStart = mPendingActions.AppendElements(2);
    PendingAction* compositionEnd = compositionStart + 1;

    compositionStart->mType = PendingAction::Type::CompositionStart;
    compositionStart->mSelectionStart = oldSelection.acpStart;
    compositionStart->mSelectionLength =
        oldSelection.acpEnd - oldSelection.acpStart;
    compositionStart->mAdjustSelection = false;

    compositionEnd->mType = PendingAction::Type::CompositionEnd;
    compositionEnd->mData = aInsertStr;
    compositionEnd->mSelectionStart = compositionStart->mSelectionStart;

    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::InsertTextAtSelectionInternal() "
             "appending pending compositionstart and compositionend... "
             "PendingCompositionStart={ mSelectionStart=%ld, "
             "mSelectionLength=%ld }, PendingCompositionEnd={ mData=\"%s\" "
             "(Length()=%zu), mSelectionStart=%ld }",
             this, compositionStart->mSelectionStart,
             compositionStart->mSelectionLength,
             AutoEscapedUTF8String(compositionEnd->mData).get(),
             compositionEnd->mData.Length(), compositionEnd->mSelectionStart));
  }

  contentForTSF->ReplaceSelectedTextWith(aInsertStr);

  if (aTextChange) {
    aTextChange->acpStart = oldSelection.acpStart;
    aTextChange->acpOldEnd = oldSelection.acpEnd;
    aTextChange->acpNewEnd = contentForTSF->Selection()->EndOffset();
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::InsertTextAtSelectionInternal() "
           "succeeded: mWidget=0x%p, mWidget->Destroyed()=%s, aTextChange={ "
           "acpStart=%ld, acpOldEnd=%ld, acpNewEnd=%ld }",
           this, mWidget.get(),
           TSFUtils::BoolToChar(mWidget ? mWidget->Destroyed() : true),
           aTextChange ? aTextChange->acpStart : 0,
           aTextChange ? aTextChange->acpOldEnd : 0,
           aTextChange ? aTextChange->acpNewEnd : 0));
  return true;
}

HRESULT TSFTextStore::RecordCompositionStartAction(
    ITfCompositionView* aCompositionView, ITfRange* aRange,
    bool aPreserveSelection) {
  MOZ_LOG(
      gIMELog, LogLevel::Debug,
      ("0x%p   TSFTextStore::RecordCompositionStartAction("
       "aCompositionView=0x%p, aRange=0x%p, aPreserveSelection=%s), "
       "mComposition=%s",
       this, aCompositionView, aRange, TSFUtils::BoolToChar(aPreserveSelection),
       ToString(mComposition).c_str()));

  const TSFUtils::AutoRangeExtant rangeExtant(aRange);
  if (MOZ_UNLIKELY(rangeExtant.isErr())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionStartAction() FAILED "
             "due to GetRangeExtent() failure",
             this));
    return rangeExtant.mHR;
  }

  return RecordCompositionStartAction(aCompositionView, rangeExtant.mStart,
                                      rangeExtant.mLength, aPreserveSelection);
}

HRESULT TSFTextStore::RecordCompositionStartAction(
    ITfCompositionView* aCompositionView, LONG aStart, LONG aLength,
    bool aPreserveSelection) {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::RecordCompositionStartAction("
           "aCompositionView=0x%p, aStart=%ld, aLength=%ld, "
           "aPreserveSelection=%s), "
           "mComposition=%s",
           this, aCompositionView, aStart, aLength,
           TSFUtils::BoolToChar(aPreserveSelection),
           ToString(mComposition).c_str()));

  Maybe<Content>& contentForTSF = ContentForTSF();
  if (contentForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionStartAction() FAILED "
             "due to ContentForTSF() failure",
             this));
    return E_FAIL;
  }

  MaybeDispatchKeyboardEventAsProcessedByIME();
  if (mDestroyed) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStore::RecordCompositionStartAction() FAILED due to "
         "destroyed during dispatching a keyboard event",
         this));
    return false;
  }

  CompleteLastActionIfStillIncomplete();

  // TIP may have inserted text at selection before calling
  // OnStartComposition().  In this case, we've already created a pending
  // compositionend.  If new composition replaces all commit string of the
  // pending compositionend, we should cancel the pending compositionend and
  // keep the previous composition normally.
  // On Windows 7, MS-IME for Korean, MS-IME 2010 for Korean and MS Old Hangul
  // may start composition with calling InsertTextAtSelection() and
  // OnStartComposition() with this order (bug 1208043).
  // On Windows 10, MS Pinyin, MS Wubi, MS ChangJie and MS Quick commits
  // last character and replace it with empty string with new composition
  // when user removes last character of composition string with Backspace
  // key (bug 1462257).
  if (!aPreserveSelection &&
      IsLastPendingActionCompositionEndAt(aStart, aLength)) {
    const PendingAction& pendingCompositionEnd = mPendingActions.LastElement();
    contentForTSF->RestoreCommittedComposition(aCompositionView,
                                               pendingCompositionEnd);
    mPendingActions.RemoveLastElement();
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::RecordCompositionStartAction() "
             "succeeded: restoring the committed string as composing string, "
             "mComposition=%s, mSelectionForTSF=%s",
             this, ToString(mComposition).c_str(),
             ToString(mSelectionForTSF).c_str()));
    return S_OK;
  }

  PendingAction* action = mPendingActions.AppendElement();
  action->mType = PendingAction::Type::CompositionStart;
  action->mSelectionStart = aStart;
  action->mSelectionLength = aLength;

  Maybe<Selection>& selectionForTSF = SelectionForTSF();
  if (selectionForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionStartAction() FAILED "
             "due to SelectionForTSF() failure",
             this));
    action->mAdjustSelection = true;
  } else if (!selectionForTSF->HasRange()) {
    // If there is no selection, let's collapse seletion to the insertion point.
    action->mAdjustSelection = true;
  } else if (selectionForTSF->MinOffset() != aStart ||
             selectionForTSF->MaxOffset() != aStart + aLength) {
    // If new composition range is different from current selection range,
    // we need to set selection before dispatching compositionstart event.
    action->mAdjustSelection = true;
  } else {
    // We shouldn't dispatch selection set event before dispatching
    // compositionstart event because it may cause put caret different
    // position in HTML editor since generated flat text content and offset in
    // it are lossy data of HTML contents.
    action->mAdjustSelection = false;
  }

  contentForTSF->StartComposition(aCompositionView, *action,
                                  aPreserveSelection);
  MOZ_ASSERT(mComposition.isSome());
  action->mData = mComposition->DataRef();

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::RecordCompositionStartAction() succeeded: "
           "mComposition=%s, mSelectionForTSF=%s }",
           this, ToString(mComposition).c_str(),
           ToString(mSelectionForTSF).c_str()));
  return S_OK;
}

HRESULT TSFTextStore::RecordCompositionEndAction() {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::RecordCompositionEndAction(), "
           "mComposition=%s",
           this, ToString(mComposition).c_str()));

  MOZ_ASSERT(mComposition.isSome());

  if (mComposition.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionEndAction() FAILED due to "
             "no composition",
             this));
    return false;
  }

  MaybeDispatchKeyboardEventAsProcessedByIME();
  if (mDestroyed) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionEndAction() FAILED due to "
             "destroyed during dispatching a keyboard event",
             this));
    return false;
  }

  // If we're handling incomplete composition update or already handled
  // composition update, we can forget them since composition end will send
  // the latest composition string and it overwrites the composition string
  // even if we dispatch eCompositionChange event before that.  So, let's
  // forget all composition updates now.
  RemoveLastCompositionUpdateActions();
  PendingAction* action = mPendingActions.AppendElement();
  action->mType = PendingAction::Type::CompositionEnd;
  action->mData = mComposition->DataRef();
  action->mSelectionStart = mComposition->StartOffset();

  Maybe<Content>& contentForTSF = ContentForTSF();
  if (contentForTSF.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::RecordCompositionEndAction() FAILED due "
             "to ContentForTSF() failure",
             this));
    return E_FAIL;
  }
  contentForTSF->EndComposition(*action);

  // If this composition was restart but the composition doesn't modify
  // anything, we should remove the pending composition for preventing to
  // dispatch redundant composition events.
  for (size_t i = mPendingActions.Length(), j = 1; i > 0; --i, ++j) {
    PendingAction& pendingAction = mPendingActions[i - 1];
    if (pendingAction.mType == PendingAction::Type::CompositionStart) {
      if (pendingAction.mData != action->mData) {
        break;
      }
      // When only setting selection is necessary, we should append it.
      if (pendingAction.mAdjustSelection) {
        LONG selectionStart = pendingAction.mSelectionStart;
        LONG selectionLength = pendingAction.mSelectionLength;

        PendingAction* setSelection = mPendingActions.AppendElement();
        setSelection->mType = PendingAction::Type::SetSelection;
        setSelection->mSelectionStart = selectionStart;
        setSelection->mSelectionLength = selectionLength;
        setSelection->mSelectionReversed = false;
      }
      // Remove the redundant pending composition.
      mPendingActions.RemoveElementsAt(i - 1, j);
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("0x%p   TSFTextStore::RecordCompositionEndAction(), "
               "succeeded, but the composition was canceled due to redundant",
               this));
      return S_OK;
    }
  }

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("0x%p   TSFTextStore::RecordCompositionEndAction(), succeeded", this));
  return S_OK;
}

STDMETHODIMP TSFTextStore::OnStartComposition(ITfCompositionView* pComposition,
                                              BOOL* pfOk) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::OnStartComposition(pComposition=0x%p, "
           "pfOk=0x%p), mComposition=%s",
           this, pComposition, pfOk, ToString(mComposition).c_str()));

  AutoPendingActionAndContentFlusher flusher(this);

  *pfOk = FALSE;

  // Only one composition at a time
  if (mComposition.isSome()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnStartComposition() FAILED due to "
             "there is another composition already (but returns S_OK)",
             this));
    return S_OK;
  }

  RefPtr<ITfRange> range;
  HRESULT hr = pComposition->GetRange(getter_AddRefs(range));
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnStartComposition() FAILED due to "
             "pComposition->GetRange() failure",
             this));
    return hr;
  }
  hr = RecordCompositionStartAction(pComposition, range, false);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnStartComposition() FAILED due to "
             "RecordCompositionStartAction() failure",
             this));
    return hr;
  }

  *pfOk = TRUE;
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::OnStartComposition() succeeded", this));
  return S_OK;
}

STDMETHODIMP TSFTextStore::OnUpdateComposition(ITfCompositionView* pComposition,
                                               ITfRange* pRangeNew) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::OnUpdateComposition(pComposition=0x%p, "
           "pRangeNew=0x%p), mComposition=%s",
           this, pComposition, pRangeNew, ToString(mComposition).c_str()));

  AutoPendingActionAndContentFlusher flusher(this);

  if (!mDocumentMgr || !mContext) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnUpdateComposition() FAILED due to "
             "not ready for the composition",
             this));
    return E_UNEXPECTED;
  }
  if (mComposition.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnUpdateComposition() FAILED due to "
             "no active composition",
             this));
    return E_UNEXPECTED;
  }
  if (mComposition->GetView() != pComposition) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnUpdateComposition() FAILED due to "
             "different composition view specified",
             this));
    return E_UNEXPECTED;
  }

  // pRangeNew is null when the update is not complete
  if (!pRangeNew) {
    MaybeDispatchKeyboardEventAsProcessedByIME();
    if (mDestroyed) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::OnUpdateComposition() FAILED due to "
               "destroyed during dispatching a keyboard event",
               this));
      return E_FAIL;
    }
    PendingAction* action = LastOrNewPendingCompositionUpdate();
    action->mIncomplete = true;
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::OnUpdateComposition() succeeded but "
             "not complete",
             this));
    return S_OK;
  }

  HRESULT hr = RestartCompositionIfNecessary(pRangeNew);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnUpdateComposition() FAILED due to "
             "RestartCompositionIfNecessary() failure",
             this));
    return hr;
  }

  hr = RecordCompositionUpdateAction();
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnUpdateComposition() FAILED due to "
             "RecordCompositionUpdateAction() failure",
             this));
    return hr;
  }

  if (MOZ_LOG_TEST(gIMELog, LogLevel::Info)) {
    Maybe<Selection>& selectionForTSF = SelectionForTSF();
    if (selectionForTSF.isNothing()) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::OnUpdateComposition() FAILED due to "
               "SelectionForTSF() failure",
               this));
      return S_OK;  // Don't return error only when we're logging.
    }
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::OnUpdateComposition() succeeded: "
             "mComposition=%s, SelectionForTSF()=%s",
             this, ToString(mComposition).c_str(),
             ToString(selectionForTSF).c_str()));
  }
  return S_OK;
}

STDMETHODIMP TSFTextStore::OnEndComposition(ITfCompositionView* pComposition) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::OnEndComposition(pComposition=0x%p), "
           "mComposition=%s",
           this, pComposition, ToString(mComposition).c_str()));

  AutoPendingActionAndContentFlusher flusher(this);

  if (mComposition.isNothing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnEndComposition() FAILED due to "
             "no active composition",
             this));
    return E_UNEXPECTED;
  }

  if (mComposition->GetView() != pComposition) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnEndComposition() FAILED due to "
             "different composition view specified",
             this));
    return E_UNEXPECTED;
  }

  HRESULT hr = RecordCompositionEndAction();
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::OnEndComposition() FAILED due to "
             "RecordCompositionEndAction() failure",
             this));
    return hr;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::OnEndComposition(), succeeded", this));
  return S_OK;
}

STDMETHODIMP TSFTextStore::AdviseMouseSink(ITfRangeACP* range,
                                           ITfMouseSink* pSink,
                                           DWORD* pdwCookie) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStore::AdviseMouseSink(range=0x%p, pSink=0x%p, "
           "pdwCookie=0x%p)",
           this, range, pSink, pdwCookie));

  if (!pdwCookie) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::AdviseMouseSink() FAILED due to the "
             "pdwCookie is null",
             this));
    return E_INVALIDARG;
  }
  // Initialize the result with invalid cookie for safety.
  *pdwCookie = MouseTracker::kInvalidCookie;

  if (!range) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::AdviseMouseSink() FAILED due to the "
             "range is null",
             this));
    return E_INVALIDARG;
  }
  if (!pSink) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::AdviseMouseSink() FAILED due to the "
             "pSink is null",
             this));
    return E_INVALIDARG;
  }

  // Looking for an unusing tracker.
  MouseTracker* tracker = nullptr;
  for (size_t i = 0; i < mMouseTrackers.Length(); i++) {
    if (mMouseTrackers[i].IsUsing()) {
      continue;
    }
    tracker = &mMouseTrackers[i];
  }
  // If there is no unusing tracker, create new one.
  // XXX Should we make limitation of the number of installs?
  if (!tracker) {
    tracker = mMouseTrackers.AppendElement();
    HRESULT hr = tracker->Init(this);
    if (FAILED(hr)) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStore::AdviseMouseSink() FAILED due to "
               "failure of MouseTracker::Init()",
               this));
      return hr;
    }
  }
  HRESULT hr = tracker->AdviseSink(this, range, pSink);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::AdviseMouseSink() FAILED due to failure "
             "of MouseTracker::Init()",
             this));
    return hr;
  }
  *pdwCookie = tracker->Cookie();
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::AdviseMouseSink(), succeeded, "
           "*pdwCookie=%ld",
           this, *pdwCookie));
  return S_OK;
}

STDMETHODIMP TSFTextStore::UnadviseMouseSink(DWORD dwCookie) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("0x%p TSFTextStore::UnadviseMouseSink(dwCookie=%ld)", this, dwCookie));
  if (dwCookie == MouseTracker::kInvalidCookie) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::UnadviseMouseSink() FAILED due to "
             "the cookie is invalid value",
             this));
    return E_INVALIDARG;
  }
  // The cookie value must be an index of mMouseTrackers.
  // We can use this shortcut for now.
  if (static_cast<size_t>(dwCookie) >= mMouseTrackers.Length()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::UnadviseMouseSink() FAILED due to "
             "the cookie is too large value",
             this));
    return E_INVALIDARG;
  }
  MouseTracker& tracker = mMouseTrackers[dwCookie];
  if (!tracker.IsUsing()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::UnadviseMouseSink() FAILED due to "
             "the found tracker uninstalled already",
             this));
    return E_INVALIDARG;
  }
  tracker.UnadviseSink();
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::UnadviseMouseSink(), succeeded", this));
  return S_OK;
}

// static
Result<RefPtr<TSFTextStore>, nsresult> TSFTextStore::CreateAndSetFocus(
    nsWindow* aFocusedWindow, const InputContext& aContext) {
  const RefPtr<TSFTextStore> textStore = new TSFTextStore();
  if (NS_WARN_IF(!textStore->Init(aFocusedWindow, aContext))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::CreateAndSetFocus() FAILED due to "
             "TSFTextStore::Init() failure"));
    textStore->Destroy();
    TSFUtils::ClearStoringTextStoresIf(textStore);
    return Err(NS_ERROR_FAILURE);
  }
  const RefPtr<ITfDocumentMgr> newDocMgr = textStore->mDocumentMgr;
  if (NS_WARN_IF(!newDocMgr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::CreateAndSetFocus() FAILED due to "
             "invalid TSFTextStore::mDocumentMgr"));
    textStore->Destroy();
    TSFUtils::ClearStoringTextStoresIf(textStore);
    return Err(NS_ERROR_FAILURE);
  }
  if (aContext.mIMEState.mEnabled == IMEEnabled::Password) {
    TSFUtils::MarkContextAsKeyboardDisabled(textStore->mContext);
    RefPtr<ITfContext> topContext;
    newDocMgr->GetTop(getter_AddRefs(topContext));
    if (topContext && topContext != textStore->mContext) {
      TSFUtils::MarkContextAsKeyboardDisabled(topContext);
    }
  }
  const RefPtr<ITfThreadMgr> threadMgr = TSFUtils::GetThreadMgr();
  if (NS_WARN_IF(FAILED(threadMgr->SetFocus(newDocMgr)))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::CreateAndSetFocus() FAILED due to "
             "ITfTheadMgr::SetFocus() failure"));
    textStore->Destroy();
    TSFUtils::ClearStoringTextStoresIf(textStore);
    return Err(NS_ERROR_FAILURE);
  }
  if (NS_WARN_IF(!TSFUtils::GetThreadMgr())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::CreateAndSetFocus() FAILED due to "
             "sThreadMgr being destroyed during calling "
             "ITfTheadMgr::SetFocus()"));
    textStore->Destroy();
    TSFUtils::ClearStoringTextStoresIf(textStore);
    return Err(NS_ERROR_FAILURE);
  }
  if (NS_WARN_IF(TSFUtils::GetCurrentTextStore())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::CreateAndSetFocus() FAILED due to "
             "creating TextStore has lost focus during calling "
             "ITfThreadMgr::SetFocus()"));
    textStore->Destroy();
    TSFUtils::ClearStoringTextStoresIf(textStore);
    return Err(NS_ERROR_FAILURE);
  }
  // Use AssociateFocus() for ensuring that any native focus event
  // never steal focus from our documentMgr.
  {
    RefPtr<ITfDocumentMgr> prevFocusedDocumentMgr;
    if (NS_WARN_IF(FAILED(threadMgr->AssociateFocus(
            aFocusedWindow->GetWindowHandle(), newDocMgr,
            getter_AddRefs(prevFocusedDocumentMgr))))) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("  TSFTextStore::CreateAndSetFocus() FAILED due to "
               "ITfTheadMgr::AssociateFocus() failure"));
      textStore->Destroy();
      TSFUtils::ClearStoringTextStoresIf(textStore);
      return Err(NS_ERROR_FAILURE);
    }
  }
  if (NS_WARN_IF(!TSFUtils::GetThreadMgr())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::CreateAndSetFocus() FAILED due to "
             "sThreadMgr being destroyed during calling "
             "ITfTheadMgr::AssociateFocus()"));
    textStore->Destroy();
    TSFUtils::ClearStoringTextStoresIf(textStore);
    return Err(NS_ERROR_FAILURE);
  }
  if (NS_WARN_IF(TSFUtils::GetCurrentTextStore())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::CreateAndSetFocus() FAILED due to "
             "creating TextStore has lost focus during calling "
             "ITfTheadMgr::AssociateFocus()"));
    textStore->Destroy();
    TSFUtils::ClearStoringTextStoresIf(textStore);
    return Err(NS_ERROR_FAILURE);
  }

  if (textStore->mSink) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("  TSFTextStore::CreateAndSetFocus(), calling "
             "ITextStoreACPSink::OnLayoutChange(TS_LC_CREATE) for 0x%p...",
             textStore.get()));
    const RefPtr<ITextStoreACPSink> sink = textStore->mSink;
    sink->OnLayoutChange(TS_LC_CREATE, TSFUtils::sDefaultView);
    if (NS_WARN_IF(TSFUtils::GetCurrentTextStore())) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("  TSFTextStore::CreateAndSetFocus() FAILED due to "
               "creating TextStore has lost focus during calling "
               "ITextStoreACPSink::OnLayoutChange(TS_LC_CREATE)"));
      textStore->Destroy();
      TSFUtils::ClearStoringTextStoresIf(textStore);
      return Err(NS_ERROR_FAILURE);
    }
  }
  return textStore;
}

IMENotificationRequests TSFTextStore::GetIMENotificationRequests() const {
  if (NS_WARN_IF(!mDocumentMgr)) {
    // If there is no active text store, we don't need any notifications
    // since there is no sink which needs notifications.
    return IMENotificationRequests();
  }

  // Otherwise, requests all notifications since even if some of them may not
  // be required by the sink of active TIP, active TIP may be changed and
  // other TIPs may need all notifications.
  // Note that Windows temporarily steal focus from active window if the main
  // process which created the window becomes busy.  In this case, we shouldn't
  // commit composition since user may want to continue to compose the
  // composition after becoming not busy.  Therefore, we need notifications
  // even during deactive.
  // Be aware, we don't need to check actual focused text store.  For example,
  // MS-IME for Japanese handles focus messages by themselves and sets focused
  // text store to nullptr when the process is being inactivated.  However,
  // we still need to reuse sEnabledTextStore if the process is activated and
  // focused element isn't changed.  Therefore, if sEnabledTextStore isn't
  // nullptr, we need to keep notifying the sink even when it is not focused
  // text store for the thread manager.
  return IMENotificationRequests(
      IMENotificationRequests::NOTIFY_TEXT_CHANGE |
      IMENotificationRequests::NOTIFY_POSITION_CHANGE |
      IMENotificationRequests::NOTIFY_MOUSE_BUTTON_EVENT_ON_CHAR |
      IMENotificationRequests::NOTIFY_DURING_DEACTIVE);
}

nsresult TSFTextStore::OnTextChangeInternal(
    const IMENotification& aIMENotification) {
  const TextChangeDataBase& textChangeData = aIMENotification.mTextChangeData;

  MOZ_LOG(
      gIMELog, LogLevel::Debug,
      ("0x%p   TSFTextStore::OnTextChangeInternal(aIMENotification={ "
       "mMessage=0x%08X, mTextChangeData=%s }), "
       "mDestroyed=%s, mSink=0x%p, mSinkMask=%s, "
       "mComposition=%s",
       this, aIMENotification.mMessage,
       mozilla::ToString(textChangeData).c_str(),
       TSFUtils::BoolToChar(mDestroyed), mSink.get(),
       AutoSinkMasksCString(mSinkMask).get(), ToString(mComposition).c_str()));

  if (mDestroyed) {
    // If this instance is already destroyed, we shouldn't notify TSF of any
    // changes.
    return NS_OK;
  }

  mDeferNotifyingTSFUntilNextUpdate = false;

  // Different from selection change, we don't modify anything with text
  // change data.  Therefore, if neither TSF not TIP wants text change
  // notifications, we don't need to store the changes.
  if (!mSink || !(mSinkMask & TS_AS_TEXT_CHANGE)) {
    return NS_OK;
  }

  // Merge any text change data even if it's caused by composition.
  mPendingTextChangeData.MergeWith(textChangeData);

  MaybeFlushPendingNotifications();

  return NS_OK;
}

void TSFTextStore::NotifyTSFOfTextChange() {
  MOZ_ASSERT(!mDestroyed);
  MOZ_ASSERT(!IsReadLocked());
  MOZ_ASSERT(mComposition.isNothing());
  MOZ_ASSERT(mPendingTextChangeData.IsValid());

  // If the text changes are caused only by composition, we don't need to
  // notify TSF of the text changes.
  if (mPendingTextChangeData.mCausedOnlyByComposition) {
    mPendingTextChangeData.Clear();
    return;
  }

  // First, forget cached selection.
  mSelectionForTSF.reset();

  // For making it safer, we should check if there is a valid sink to receive
  // text change notification.
  if (NS_WARN_IF(!mSink) || NS_WARN_IF(!(mSinkMask & TS_AS_TEXT_CHANGE))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::NotifyTSFOfTextChange() FAILED due to "
             "mSink is not ready to call ITextStoreACPSink::OnTextChange()...",
             this));
    mPendingTextChangeData.Clear();
    return;
  }

  if (NS_WARN_IF(!mPendingTextChangeData.IsInInt32Range())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::NotifyTSFOfTextChange() FAILED due to "
             "offset is too big for calling "
             "ITextStoreACPSink::OnTextChange()...",
             this));
    mPendingTextChangeData.Clear();
    return;
  }

  TS_TEXTCHANGE textChange;
  textChange.acpStart = static_cast<LONG>(mPendingTextChangeData.mStartOffset);
  textChange.acpOldEnd =
      static_cast<LONG>(mPendingTextChangeData.mRemovedEndOffset);
  textChange.acpNewEnd =
      static_cast<LONG>(mPendingTextChangeData.mAddedEndOffset);
  mPendingTextChangeData.Clear();

  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("0x%p   TSFTextStore::NotifyTSFOfTextChange(), calling "
       "ITextStoreACPSink::OnTextChange(0, { acpStart=%ld, acpOldEnd=%ld, "
       "acpNewEnd=%ld })...",
       this, textChange.acpStart, textChange.acpOldEnd, textChange.acpNewEnd));
  RefPtr<ITextStoreACPSink> sink = mSink;
  sink->OnTextChange(0, &textChange);
}

nsresult TSFTextStore::OnSelectionChangeInternal(
    const IMENotification& aIMENotification) {
  const SelectionChangeDataBase& selectionChangeData =
      aIMENotification.mSelectionChangeData;
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::OnSelectionChangeInternal("
           "aIMENotification={ mSelectionChangeData=%s }), mDestroyed=%s, "
           "mSink=0x%p, mSinkMask=%s, mIsRecordingActionsWithoutLock=%s, "
           "mComposition=%s",
           this, mozilla::ToString(selectionChangeData).c_str(),
           TSFUtils::BoolToChar(mDestroyed), mSink.get(),
           AutoSinkMasksCString(mSinkMask).get(),
           TSFUtils::BoolToChar(mIsRecordingActionsWithoutLock),
           ToString(mComposition).c_str()));

  if (mDestroyed) {
    // If this instance is already destroyed, we shouldn't notify TSF of any
    // changes.
    return NS_OK;
  }

  mDeferNotifyingTSFUntilNextUpdate = false;

  // Assign the new selection change data to the pending selection change data
  // because only the latest selection data is necessary.
  // Note that this is necessary to update mSelectionForTSF.  Therefore, even if
  // neither TSF nor TIP wants selection change notifications, we need to
  // store the selection information.
  mPendingSelectionChangeData = Some(selectionChangeData);

  // Flush remaining pending notifications here if it's possible.
  MaybeFlushPendingNotifications();

  // If we're available, we should create native caret instead of IMEHandler
  // because we may have some cache to do it.
  // Note that if we have composition, we'll notified composition-updated
  // later so that we don't need to create native caret in such case.
  if (!IsHandlingCompositionInContent() &&
      IMEHandler::NeedsToCreateNativeCaret()) {
    CreateNativeCaret();
  }

  return NS_OK;
}

void TSFTextStore::NotifyTSFOfSelectionChange() {
  MOZ_ASSERT(!mDestroyed);
  MOZ_ASSERT(!IsReadLocked());
  MOZ_ASSERT(mComposition.isNothing());
  MOZ_ASSERT(mPendingSelectionChangeData.isSome());

  // If selection range isn't actually changed, we don't need to notify TSF
  // of this selection change.
  if (mSelectionForTSF.isNothing()) {
    MOZ_DIAGNOSTIC_ASSERT(!mIsInitializingSelectionForTSF,
                          "While mSelectionForTSF is being initialized, this "
                          "should not be called");
    mSelectionForTSF.emplace(*mPendingSelectionChangeData);
  } else if (!mSelectionForTSF->SetSelection(*mPendingSelectionChangeData)) {
    mPendingSelectionChangeData.reset();
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::NotifyTSFOfSelectionChange(), "
             "selection isn't actually changed.",
             this));
    return;
  }

  mPendingSelectionChangeData.reset();

  if (!mSink || !(mSinkMask & TS_AS_SEL_CHANGE)) {
    return;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::NotifyTSFOfSelectionChange(), calling "
           "ITextStoreACPSink::OnSelectionChange()...",
           this));
  RefPtr<ITextStoreACPSink> sink = mSink;
  sink->OnSelectionChange();
}

nsresult TSFTextStore::OnLayoutChangeInternal() {
  if (mDestroyed) {
    // If this instance is already destroyed, we shouldn't notify TSF of any
    // changes.
    return NS_OK;
  }

  NS_ENSURE_TRUE(mContext, NS_ERROR_FAILURE);
  NS_ENSURE_TRUE(mSink, NS_ERROR_FAILURE);

  mDeferNotifyingTSFUntilNextUpdate = false;

  nsresult rv = NS_OK;

  // We need to notify TSF of layout change even if the document is locked.
  // So, don't use MaybeFlushPendingNotifications() for flushing pending
  // layout change.
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::OnLayoutChangeInternal(), calling "
           "NotifyTSFOfLayoutChange()...",
           this));
  if (NS_WARN_IF(!NotifyTSFOfLayoutChange())) {
    rv = NS_ERROR_FAILURE;
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::OnLayoutChangeInternal(), calling "
           "MaybeFlushPendingNotifications()...",
           this));
  MaybeFlushPendingNotifications();

  return rv;
}

bool TSFTextStore::NotifyTSFOfLayoutChange() {
  MOZ_ASSERT(!mDestroyed);

  // If we're waiting a query of layout information from TIP, it means that
  // we've returned TS_E_NOLAYOUT error.
  bool returnedNoLayoutError = mHasReturnedNoLayoutError || mWaitingQueryLayout;

  // If we returned TS_E_NOLAYOUT, TIP should query the computed layout again.
  mWaitingQueryLayout = returnedNoLayoutError;

  // For avoiding to call this method again at unlocking the document during
  // calls of OnLayoutChange(), reset mHasReturnedNoLayoutError.
  mHasReturnedNoLayoutError = false;

  // Now, layout has been computed.  We should notify mContentForTSF for
  // making GetTextExt() and GetACPFromPoint() not return TS_E_NOLAYOUT.
  if (mContentForTSF.isSome()) {
    mContentForTSF->OnLayoutChanged();
  }

  if (IMEHandler::NeedsToCreateNativeCaret()) {
    // If we're available, we should create native caret instead of IMEHandler
    // because we may have some cache to do it.
    CreateNativeCaret();
  } else {
    // Now, the caret position is different from ours.  Destroy the native caret
    // if we've create it only for GetTextExt().
    IMEHandler::MaybeDestroyNativeCaret();
  }

  // This method should return true if either way succeeds.
  bool ret = true;

  if (mSink) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::NotifyTSFOfLayoutChange(), "
             "calling ITextStoreACPSink::OnLayoutChange()...",
             this));
    RefPtr<ITextStoreACPSink> sink = mSink;
    HRESULT hr = sink->OnLayoutChange(TS_LC_CHANGE, TSFUtils::sDefaultView);
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::NotifyTSFOfLayoutChange(), "
             "called ITextStoreACPSink::OnLayoutChange()",
             this));
    ret = SUCCEEDED(hr);
  }

  // The layout change caused by composition string change should cause
  // calling ITfContextOwnerServices::OnLayoutChange() too.
  if (returnedNoLayoutError && mContext) {
    RefPtr<ITfContextOwnerServices> service;
    mContext->QueryInterface(IID_ITfContextOwnerServices,
                             getter_AddRefs(service));
    if (service) {
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("0x%p   TSFTextStore::NotifyTSFOfLayoutChange(), "
               "calling ITfContextOwnerServices::OnLayoutChange()...",
               this));
      HRESULT hr = service->OnLayoutChange();
      ret = ret && SUCCEEDED(hr);
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("0x%p   TSFTextStore::NotifyTSFOfLayoutChange(), "
               "called ITfContextOwnerServices::OnLayoutChange()",
               this));
    }
  }

  if (!mWidget || mWidget->Destroyed()) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::NotifyTSFOfLayoutChange(), "
             "the widget is destroyed during calling OnLayoutChange()",
             this));
    return ret;
  }

  if (mDestroyed) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::NotifyTSFOfLayoutChange(), "
             "the TSFTextStore instance is destroyed during calling "
             "OnLayoutChange()",
             this));
    return ret;
  }

  // If we returned TS_E_NOLAYOUT again, we need another call of
  // OnLayoutChange() later.  So, let's wait a query from TIP.
  if (mHasReturnedNoLayoutError) {
    mWaitingQueryLayout = true;
  }

  if (!mWaitingQueryLayout) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::NotifyTSFOfLayoutChange(), "
             "succeeded notifying TIP of our layout change",
             this));
    return ret;
  }

  // If we believe that TIP needs to retry to retrieve our layout information
  // later, we should call it with ::PostMessage() hack.
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::NotifyTSFOfLayoutChange(), "
           "posting MOZ_WM_NOTIFY_TSF_OF_LAYOUT_CHANGE for calling "
           "OnLayoutChange() again...",
           this));
  ::PostMessage(mWidget->GetWindowHandle(), MOZ_WM_NOTIFY_TSF_OF_LAYOUT_CHANGE,
                reinterpret_cast<WPARAM>(this), 0);

  return true;
}

void TSFTextStore::NotifyTSFOfLayoutChangeAgain() {
  // Don't notify TSF of layout change after destroyed.
  if (mDestroyed) {
    mWaitingQueryLayout = false;
    return;
  }

  // Before preforming this method, TIP has accessed our layout information by
  // itself.  In such case, we don't need to call OnLayoutChange() anymore.
  if (!mWaitingQueryLayout) {
    return;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStore::NotifyTSFOfLayoutChangeAgain(), "
           "calling NotifyTSFOfLayoutChange()...",
           this));
  NotifyTSFOfLayoutChange();

  // If TIP didn't retrieved our layout information during a call of
  // NotifyTSFOfLayoutChange(), it means that the TIP already gave up to
  // retry to retrieve layout information or doesn't necessary it anymore.
  // But don't forget that the call may have caused returning TS_E_NOLAYOUT
  // error again.  In such case we still need to call OnLayoutChange() later.
  if (!mHasReturnedNoLayoutError && mWaitingQueryLayout) {
    mWaitingQueryLayout = false;
    MOZ_LOG(gIMELog, LogLevel::Warning,
            ("0x%p   TSFTextStore::NotifyTSFOfLayoutChangeAgain(), "
             "called NotifyTSFOfLayoutChange() but TIP didn't retry to "
             "retrieve the layout information",
             this));
  } else {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStore::NotifyTSFOfLayoutChangeAgain(), "
             "called NotifyTSFOfLayoutChange()",
             this));
  }
}

nsresult TSFTextStore::OnUpdateCompositionInternal() {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::OnUpdateCompositionInternal(), "
           "mDestroyed=%s, mDeferNotifyingTSFUntilNextUpdate=%s",
           this, TSFUtils::BoolToChar(mDestroyed),
           TSFUtils::BoolToChar(mDeferNotifyingTSFUntilNextUpdate)));

  // There are nothing to do after destroyed.
  if (mDestroyed) {
    return NS_OK;
  }

  // Update cached data now because all pending events have been handled now.
  if (mContentForTSF.isSome()) {
    mContentForTSF->OnCompositionEventsHandled();
  }

  // If composition is completely finished both in TSF/TIP and the focused
  // editor which may be in a remote process, we can clear the cache and don't
  // have it until starting next composition.
  if (mComposition.isNothing() && !IsHandlingCompositionInContent()) {
    mDeferClearingContentForTSF = false;
  }
  mDeferNotifyingTSFUntilNextUpdate = false;
  MaybeFlushPendingNotifications();

  // If we're available, we should create native caret instead of IMEHandler
  // because we may have some cache to do it.
  if (IMEHandler::NeedsToCreateNativeCaret()) {
    CreateNativeCaret();
  }

  return NS_OK;
}

nsresult TSFTextStore::OnMouseButtonEventInternal(
    const IMENotification& aIMENotification) {
  if (mDestroyed) {
    // If this instance is already destroyed, we shouldn't notify TSF of any
    // events.
    return NS_OK;
  }

  if (mMouseTrackers.IsEmpty()) {
    return NS_OK;
  }

  MOZ_LOG(
      gIMELog, LogLevel::Debug,
      ("0x%p   TSFTextStore::OnMouseButtonEventInternal("
       "aIMENotification={ mEventMessage=%s, mOffset=%u, mCursorPos=%s, "
       "mCharRect=%s, mButton=%s, mButtons=%s, mModifiers=%s })",
       this, ToChar(aIMENotification.mMouseButtonEventData.mEventMessage),
       aIMENotification.mMouseButtonEventData.mOffset,
       ToString(aIMENotification.mMouseButtonEventData.mCursorPos).c_str(),
       ToString(aIMENotification.mMouseButtonEventData.mCharRect).c_str(),
       TSFUtils::MouseButtonToChar(
           aIMENotification.mMouseButtonEventData.mButton),
       AutoMouseButtonsCString(aIMENotification.mMouseButtonEventData.mButtons)
           .get(),
       GetModifiersName(aIMENotification.mMouseButtonEventData.mModifiers)
           .get()));

  uint32_t offset = aIMENotification.mMouseButtonEventData.mOffset;
  if (offset > static_cast<uint32_t>(LONG_MAX)) {
    return NS_OK;
  }
  LayoutDeviceIntRect charRect =
      aIMENotification.mMouseButtonEventData.mCharRect;
  LayoutDeviceIntPoint cursorPos =
      aIMENotification.mMouseButtonEventData.mCursorPos;
  ULONG quadrant = 1;
  if (charRect.Width() > 0) {
    int32_t cursorXInChar = cursorPos.x - charRect.X();
    quadrant = cursorXInChar * 4 / charRect.Width();
    quadrant = (quadrant + 2) % 4;
  }
  ULONG edge = quadrant < 2 ? offset + 1 : offset;
  DWORD buttonStatus = 0;
  bool isMouseUp =
      aIMENotification.mMouseButtonEventData.mEventMessage == eMouseUp;
  if (!isMouseUp) {
    switch (aIMENotification.mMouseButtonEventData.mButton) {
      case MouseButton::ePrimary:
        buttonStatus = MK_LBUTTON;
        break;
      case MouseButton::eMiddle:
        buttonStatus = MK_MBUTTON;
        break;
      case MouseButton::eSecondary:
        buttonStatus = MK_RBUTTON;
        break;
    }
  }
  if (aIMENotification.mMouseButtonEventData.mModifiers & MODIFIER_CONTROL) {
    buttonStatus |= MK_CONTROL;
  }
  if (aIMENotification.mMouseButtonEventData.mModifiers & MODIFIER_SHIFT) {
    buttonStatus |= MK_SHIFT;
  }
  for (size_t i = 0; i < mMouseTrackers.Length(); i++) {
    MouseTracker& tracker = mMouseTrackers[i];
    if (!tracker.IsUsing() || tracker.Range().isNothing() ||
        !tracker.Range()->IsOffsetInRange(offset)) {
      continue;
    }
    if (tracker.OnMouseButtonEvent(edge - tracker.Range()->StartOffset(),
                                   quadrant, buttonStatus)) {
      return NS_SUCCESS_EVENT_CONSUMED;
    }
  }
  return NS_OK;
}

void TSFTextStore::CreateNativeCaret() {
  MOZ_ASSERT(!IMEHandler::IsA11yHandlingNativeCaret());

  IMEHandler::MaybeDestroyNativeCaret();

  // Don't create native caret after destroyed or when we need to wait for end
  // of query selection.
  if (mDestroyed) {
    return;
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::CreateNativeCaret(), mComposition=%s, "
           "mPendingToCreateNativeCaret=%s",
           this, ToString(mComposition).c_str(),
           TSFUtils::BoolToChar(mPendingToCreateNativeCaret)));

  // If we're initializing selection, we should create native caret when it's
  // done.
  if (mIsInitializingSelectionForTSF || mPendingToCreateNativeCaret) {
    mPendingToCreateNativeCaret = true;
    return;
  }

  Maybe<Selection>& selectionForTSF = SelectionForTSF();
  if (MOZ_UNLIKELY(selectionForTSF.isNothing())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::CreateNativeCaret() FAILED due to "
             "SelectionForTSF() failure",
             this));
    return;
  }
  if (!selectionForTSF->HasRange() && mComposition.isNothing()) {
    // If there is no selection range nor composition, then, we don't have a
    // good position to show windows of TIP...
    // XXX It seems that storing last caret rect and using it in this case might
    // be better?
    MOZ_LOG(gIMELog, LogLevel::Warning,
            ("0x%p   TSFTextStore::CreateNativeCaret() couludn't create native "
             "caret due to no selection range",
             this));
    return;
  }

  WidgetQueryContentEvent queryCaretRectEvent(true, eQueryCaretRect, mWidget);
  // Don't request flushing pending layout because we must have the lastest
  // layout since we already caches selection above.
  queryCaretRectEvent.mNeedsToFlushLayout = false;
  mWidget->InitEvent(queryCaretRectEvent);

  WidgetQueryContentEvent::Options options;
  // XXX If this is called without composition and the selection isn't
  //     collapsed, is it OK?
  int64_t caretOffset = selectionForTSF->HasRange()
                            ? selectionForTSF->MaxOffset()
                            : mComposition->StartOffset();
  if (mComposition.isSome()) {
    // If there is a composition, use the relative query for deciding caret
    // position because composition might be different place from that
    // TSFTextStore assumes.
    options.mRelativeToInsertionPoint = true;
    caretOffset -= mComposition->StartOffset();
  } else if (!CanAccessActualContentDirectly()) {
    // If TSF/TIP cannot access actual content directly, there may be pending
    // text and/or selection changes which have not been notified TSF yet.
    // Therefore, we should use the relative query from start of selection where
    // TSFTextStore assumes since TSF/TIP computes the offset from our cached
    // selection.
    options.mRelativeToInsertionPoint = true;
    caretOffset -= selectionForTSF->StartOffset();
  }
  queryCaretRectEvent.InitForQueryCaretRect(caretOffset, options);

  DispatchEvent(queryCaretRectEvent);
  if (NS_WARN_IF(queryCaretRectEvent.Failed())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::CreateNativeCaret() FAILED due to "
             "eQueryCaretRect failure (offset=%lld)",
             this, caretOffset));
    return;
  }

  if (!IMEHandler::CreateNativeCaret(static_cast<nsWindow*>(mWidget.get()),
                                     queryCaretRectEvent.mReply->mRect)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::CreateNativeCaret() FAILED due to "
             "IMEHandler::CreateNativeCaret() failure",
             this));
    return;
  }
}

void TSFTextStore::CommitCompositionInternal(bool aDiscard) {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::CommitCompositionInternal(aDiscard=%s), "
           "mSink=0x%p, mContext=0x%p, mComposition=%s",
           this, TSFUtils::BoolToChar(aDiscard), mSink.get(), mContext.get(),
           ToString(mComposition).c_str()));

  // If the document is locked, TSF will fail to commit composition since
  // TSF needs another document lock.  So, let's put off the request.
  // Note that TextComposition will commit composition in the focused editor
  // with the latest composition string for web apps and waits asynchronous
  // committing messages.  Therefore, we can and need to perform this
  // asynchronously.
  if (IsReadLocked()) {
    if (mDeferCommittingComposition || mDeferCancellingComposition) {
      MOZ_LOG(gIMELog, LogLevel::Debug,
              ("0x%p   TSFTextStore::CommitCompositionInternal(), "
               "does nothing because already called and waiting unlock...",
               this));
      return;
    }
    if (aDiscard) {
      mDeferCancellingComposition = true;
    } else {
      mDeferCommittingComposition = true;
    }
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::CommitCompositionInternal(), "
             "putting off to request to %s composition after unlocking the "
             "document",
             this, aDiscard ? "cancel" : "commit"));
    return;
  }

  if (mComposition.isSome() && aDiscard) {
    LONG endOffset = mComposition->EndOffset();
    mComposition->SetData(EmptyString());
    // Note that don't notify TSF of text change after this is destroyed.
    if (mSink && !mDestroyed) {
      TS_TEXTCHANGE textChange;
      textChange.acpStart = mComposition->StartOffset();
      textChange.acpOldEnd = endOffset;
      textChange.acpNewEnd = mComposition->StartOffset();
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("0x%p   TSFTextStore::CommitCompositionInternal(), calling"
               "mSink->OnTextChange(0, { acpStart=%ld, acpOldEnd=%ld, "
               "acpNewEnd=%ld })...",
               this, textChange.acpStart, textChange.acpOldEnd,
               textChange.acpNewEnd));
      RefPtr<ITextStoreACPSink> sink = mSink;
      sink->OnTextChange(0, &textChange);
    }
  }
  // Terminate two contexts, the base context (mContext) and the top if the top
  // context is not the same as the base context.
  // NOTE: that the context might have a hidden composition from our point of
  // view.  Therefore, do this even if we don't have composition.
  RefPtr<ITfContext> baseContext = mContext;
  RefPtr<ITfContext> topContext;
  if (mDocumentMgr) {
    mDocumentMgr->GetTop(getter_AddRefs(topContext));
  }
  const auto TerminateCompositionIn = [this](ITfContext* aContext) {
    if (MOZ_UNLIKELY(!aContext)) {
      return;
    }
    RefPtr<ITfContextOwnerCompositionServices> services;
    aContext->QueryInterface(IID_ITfContextOwnerCompositionServices,
                             getter_AddRefs(services));
    if (MOZ_UNLIKELY(!services)) {
      return;
    }
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("0x%p   TSFTextStore::CommitCompositionInternal(), "
             "requesting TerminateComposition() for the context 0x%p...",
             this, aContext));
    services->TerminateComposition(nullptr);
  };
  TerminateCompositionIn(baseContext);
  if (baseContext != topContext) {
    TerminateCompositionIn(topContext);
  }
}

// static
void TSFTextStore::SetIMEOpenState(bool aState) {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("TSFTextStore::SetIMEOpenState(aState=%s)",
           TSFUtils::BoolToChar(aState)));

  if (!TSFUtils::GetThreadMgr()) {
    return;
  }

  const RefPtr<ITfCompartment> comp = TSFUtils::GetCompartmentForOpenClose();
  if (NS_WARN_IF(!comp)) {
    MOZ_LOG(gIMELog, LogLevel::Debug,
            ("  TSFTextStore::SetIMEOpenState() FAILED due to"
             "no compartment available"));
    return;
  }

  VARIANT variant;
  variant.vt = VT_I4;
  variant.lVal = aState;
  HRESULT hr = comp->SetValue(TSFUtils::ClientId(), &variant);
  if (NS_WARN_IF(FAILED(hr))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::SetIMEOpenState() FAILED due to "
             "ITfCompartment::SetValue() failure, hr=0x%08lX",
             hr));
    return;
  }
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("  TSFTextStore::SetIMEOpenState(), setting "
           "0x%04lX to GUID_COMPARTMENT_KEYBOARD_OPENCLOSE...",
           variant.lVal));
}

// static
bool TSFTextStore::GetIMEOpenState() {
  if (!TSFUtils::GetThreadMgr()) {
    return false;
  }

  const RefPtr<ITfCompartment> comp = TSFUtils::GetCompartmentForOpenClose();
  if (NS_WARN_IF(!comp)) {
    return false;
  }

  VARIANT variant;
  ::VariantInit(&variant);
  HRESULT hr = comp->GetValue(&variant);
  if (NS_WARN_IF(FAILED(hr))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFTextStore::GetIMEOpenState() FAILED due to "
             "ITfCompartment::GetValue() failure, hr=0x%08lX",
             hr));
    return false;
  }
  // Until IME is open in this process, the result may be empty.
  if (variant.vt == VT_EMPTY) {
    return false;
  }
  if (NS_WARN_IF(variant.vt != VT_I4)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFTextStore::GetIMEOpenState() FAILED due to "
             "invalid result of ITfCompartment::GetValue()"));
    ::VariantClear(&variant);
    return false;
  }

  return variant.lVal != 0;
}

// static
bool TSFTextStore::ProcessRawKeyMessage(const MSG& aMsg) {
  if (!TSFUtils::GetThreadMgr()) {
    return false;  // not in TSF mode
  }
  if (aMsg.message == WM_KEYDOWN) {
    RefPtr<TSFTextStore> textStore(TSFUtils::GetActiveTextStore());
    if (textStore) {
      textStore->OnStartToHandleKeyMessage();
      if (NS_WARN_IF(textStore != TSFUtils::GetActiveTextStore())) {
        // Let's handle the key message with new focused TSFTextStore.
        textStore = TSFUtils::GetActiveTextStore();
      }
    }
    AutoRestore<const MSG*> savePreviousKeyMsg(sHandlingKeyMsg);
    AutoRestore<bool> saveKeyEventDispatched(sIsKeyboardEventDispatched);
    sHandlingKeyMsg = &aMsg;
    sIsKeyboardEventDispatched = false;
    BOOL eaten;
    const RefPtr<ITfKeystrokeMgr> keystrokeMgr = TSFUtils::GetKeystrokeMgr();
    HRESULT hr = keystrokeMgr->TestKeyDown(aMsg.wParam, aMsg.lParam, &eaten);
    if (FAILED(hr) || !TSFUtils::GetKeystrokeMgr() || !eaten) {
      return false;
    }
    hr = keystrokeMgr->KeyDown(aMsg.wParam, aMsg.lParam, &eaten);
    if (textStore) {
      textStore->OnEndHandlingKeyMessage(!!eaten);
    }
    return SUCCEEDED(hr) && (eaten || !TSFUtils::GetKeystrokeMgr() ||
                             sIsKeyboardEventDispatched);
  }
  if (aMsg.message == WM_KEYUP) {
    RefPtr<TSFTextStore> textStore(TSFUtils::GetActiveTextStore());
    if (textStore) {
      textStore->OnStartToHandleKeyMessage();
      if (NS_WARN_IF(textStore != TSFUtils::GetActiveTextStore())) {
        // Let's handle the key message with new focused TSFTextStore.
        textStore = TSFUtils::GetActiveTextStore();
      }
    }
    AutoRestore<const MSG*> savePreviousKeyMsg(sHandlingKeyMsg);
    AutoRestore<bool> saveKeyEventDispatched(sIsKeyboardEventDispatched);
    sHandlingKeyMsg = &aMsg;
    sIsKeyboardEventDispatched = false;
    BOOL eaten;
    const RefPtr<ITfKeystrokeMgr> keystrokeMgr = TSFUtils::GetKeystrokeMgr();
    HRESULT hr = keystrokeMgr->TestKeyUp(aMsg.wParam, aMsg.lParam, &eaten);
    if (FAILED(hr) || !TSFUtils::GetKeystrokeMgr() || !eaten) {
      return false;
    }
    hr = keystrokeMgr->KeyUp(aMsg.wParam, aMsg.lParam, &eaten);
    if (textStore) {
      textStore->OnEndHandlingKeyMessage(!!eaten);
    }
    return SUCCEEDED(hr) && (eaten || !TSFUtils::GetKeystrokeMgr() ||
                             sIsKeyboardEventDispatched);
  }
  return false;
}

// static
void TSFTextStore::ProcessMessage(nsWindow* aWindow, UINT aMessage,
                                  WPARAM& aWParam, LPARAM& aLParam,
                                  MSGResult& aResult) {
  switch (aMessage) {
    case WM_IME_SETCONTEXT:
      // If a windowless plugin had focus and IME was handled on it, composition
      // window was set the position.  After that, even in TSF mode, WinXP keeps
      // to use composition window at the position if the active IME is not
      // aware TSF.  For avoiding this issue, we need to hide the composition
      // window here.
      if (aWParam) {
        aLParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
      }
      break;
    case WM_ENTERIDLE:
      // When an modal dialog such as a file picker is open, composition
      // should be committed because IME might be used on it.
      if (!IsComposingOn(aWindow)) {
        break;
      }
      CommitComposition(false);
      break;
    case MOZ_WM_NOTIFY_TSF_OF_LAYOUT_CHANGE: {
      TSFTextStore* const maybeTextStore =
          reinterpret_cast<TSFTextStore*>(aWParam);
      if (maybeTextStore == TSFUtils::GetActiveTextStore()) {
        const RefPtr<TSFTextStore> textStore(maybeTextStore);
        textStore->NotifyTSFOfLayoutChangeAgain();
      }
      break;
    }
  }
}

// static
bool TSFTextStore::IsIMM_IMEActive() {
  return TSFStaticSink::IsIMM_IMEActive();
}

// static
bool TSFTextStore::IsMSJapaneseIMEActive() {
  return TSFStaticSink::IsMSJapaneseIMEActive();
}

// static
bool TSFTextStore::IsGoogleJapaneseInputActive() {
  return TSFStaticSink::IsGoogleJapaneseInputActive();
}

// static
bool TSFTextStore::IsATOKActive() { return TSFStaticSink::IsATOKActive(); }

/******************************************************************************
 *  TSFTextStore::Content
 *****************************************************************************/

const nsDependentSubstring TSFTextStore::Content::GetSelectedText() const {
  if (NS_WARN_IF(mSelection.isNothing())) {
    return nsDependentSubstring();
  }
  return GetSubstring(static_cast<uint32_t>(mSelection->StartOffset()),
                      static_cast<uint32_t>(mSelection->Length()));
}

const nsDependentSubstring TSFTextStore::Content::GetSubstring(
    uint32_t aStart, uint32_t aLength) const {
  return nsDependentSubstring(mText, aStart, aLength);
}

void TSFTextStore::Content::ReplaceSelectedTextWith(const nsAString& aString) {
  if (NS_WARN_IF(mSelection.isNothing())) {
    return;
  }
  ReplaceTextWith(mSelection->StartOffset(), mSelection->Length(), aString);
}

inline uint32_t FirstDifferentCharOffset(const nsAString& aStr1,
                                         const nsAString& aStr2) {
  MOZ_ASSERT(aStr1 != aStr2);
  uint32_t i = 0;
  uint32_t minLength = std::min(aStr1.Length(), aStr2.Length());
  for (; i < minLength && aStr1[i] == aStr2[i]; i++) {
    /* nothing to do */
  }
  return i;
}

void TSFTextStore::Content::ReplaceTextWith(LONG aStart, LONG aLength,
                                            const nsAString& aReplaceString) {
  MOZ_ASSERT(aStart >= 0);
  MOZ_ASSERT(aLength >= 0);
  const nsDependentSubstring replacedString = GetSubstring(
      static_cast<uint32_t>(aStart), static_cast<uint32_t>(aLength));
  if (aReplaceString != replacedString) {
    uint32_t firstDifferentOffset = mMinModifiedOffset.valueOr(UINT32_MAX);
    if (mComposition.isSome()) {
      // Emulate text insertion during compositions, because during a
      // composition, editor expects the whole composition string to
      // be sent in eCompositionChange, not just the inserted part.
      // The actual eCompositionChange will be sent in SetSelection
      // or OnUpdateComposition.
      MOZ_ASSERT(aStart >= mComposition->StartOffset());
      MOZ_ASSERT(aStart + aLength <= mComposition->EndOffset());
      mComposition->ReplaceData(
          static_cast<uint32_t>(aStart - mComposition->StartOffset()),
          static_cast<uint32_t>(aLength), aReplaceString);
      // TIP may set composition string twice or more times during a document
      // lock.  Therefore, we should compute the first difference offset with
      // mLastComposition.
      if (mLastComposition.isNothing()) {
        firstDifferentOffset = mComposition->StartOffset();
      } else if (mComposition->DataRef() != mLastComposition->DataRef()) {
        firstDifferentOffset =
            mComposition->StartOffset() +
            FirstDifferentCharOffset(mComposition->DataRef(),
                                     mLastComposition->DataRef());
        // The previous change to the composition string is canceled.
        if (mMinModifiedOffset.isSome() &&
            mMinModifiedOffset.value() >=
                static_cast<uint32_t>(mComposition->StartOffset()) &&
            mMinModifiedOffset.value() < firstDifferentOffset) {
          mMinModifiedOffset = Some(firstDifferentOffset);
        }
      } else if (mMinModifiedOffset.isSome() &&
                 mMinModifiedOffset.value() < static_cast<uint32_t>(LONG_MAX) &&
                 mComposition->IsOffsetInRange(
                     static_cast<long>(mMinModifiedOffset.value()))) {
        // The previous change to the composition string is canceled.
        firstDifferentOffset = mComposition->EndOffset();
        mMinModifiedOffset = Some(firstDifferentOffset);
      }
      mLatestCompositionRange = Some(mComposition->CreateStartAndEndOffsets());
      MOZ_LOG(
          gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::Content::ReplaceTextWith(aStart=%ld, "
           "aLength=%ld, aReplaceString=\"%s\"), mComposition=%s, "
           "mLastComposition=%s, mMinModifiedOffset=%s, "
           "firstDifferentOffset=%u",
           this, aStart, aLength, AutoEscapedUTF8String(aReplaceString).get(),
           ToString(mComposition).c_str(), ToString(mLastComposition).c_str(),
           ToString(mMinModifiedOffset).c_str(), firstDifferentOffset));
    } else {
      firstDifferentOffset =
          static_cast<uint32_t>(aStart) +
          FirstDifferentCharOffset(aReplaceString, replacedString);
    }
    mMinModifiedOffset =
        mMinModifiedOffset.isNothing()
            ? Some(firstDifferentOffset)
            : Some(std::min(mMinModifiedOffset.value(), firstDifferentOffset));
    mText.Replace(static_cast<uint32_t>(aStart), static_cast<uint32_t>(aLength),
                  aReplaceString);
  }
  // Selection should be collapsed at the end of the inserted string.
  mSelection = Some(TSFTextStore::Selection(static_cast<uint32_t>(aStart) +
                                            aReplaceString.Length()));
}

void TSFTextStore::Content::StartComposition(
    ITfCompositionView* aCompositionView, const PendingAction& aCompStart,
    bool aPreserveSelection) {
  MOZ_ASSERT(aCompositionView);
  MOZ_ASSERT(mComposition.isNothing());
  MOZ_ASSERT(aCompStart.mType == PendingAction::Type::CompositionStart);

  mComposition.reset();  // Avoid new crash in the beta and nightly channels.
  mComposition.emplace(
      aCompositionView, aCompStart.mSelectionStart,
      GetSubstring(static_cast<uint32_t>(aCompStart.mSelectionStart),
                   static_cast<uint32_t>(aCompStart.mSelectionLength)));
  mLatestCompositionRange = Some(mComposition->CreateStartAndEndOffsets());
  if (!aPreserveSelection) {
    // XXX Do we need to set a new writing-mode here when setting a new
    // selection? Currently, we just preserve the existing value.
    WritingMode writingMode =
        mSelection.isNothing() ? WritingMode() : mSelection->WritingModeRef();
    mSelection = Some(TSFTextStore::Selection(mComposition->StartOffset(),
                                              mComposition->Length(), false,
                                              writingMode));
  }
}

void TSFTextStore::Content::RestoreCommittedComposition(
    ITfCompositionView* aCompositionView,
    const PendingAction& aCanceledCompositionEnd) {
  MOZ_ASSERT(aCompositionView);
  MOZ_ASSERT(mComposition.isNothing());
  MOZ_ASSERT(aCanceledCompositionEnd.mType ==
             PendingAction::Type::CompositionEnd);
  MOZ_ASSERT(
      GetSubstring(
          static_cast<uint32_t>(aCanceledCompositionEnd.mSelectionStart),
          static_cast<uint32_t>(aCanceledCompositionEnd.mData.Length())) ==
      aCanceledCompositionEnd.mData);

  // Restore the committed string as composing string.
  mComposition.reset();  // Avoid new crash in the beta and nightly channels.
  mComposition.emplace(aCompositionView,
                       aCanceledCompositionEnd.mSelectionStart,
                       aCanceledCompositionEnd.mData);
  mLatestCompositionRange = Some(mComposition->CreateStartAndEndOffsets());
}

void TSFTextStore::Content::EndComposition(const PendingAction& aCompEnd) {
  MOZ_ASSERT(mComposition.isSome());
  MOZ_ASSERT(aCompEnd.mType == PendingAction::Type::CompositionEnd);

  if (mComposition.isNothing()) {
    return;  // Avoid new crash in the beta and nightly channels.
  }

  mSelection = Some(TSFTextStore::Selection(mComposition->StartOffset() +
                                            aCompEnd.mData.Length()));
  mComposition.reset();
}

/******************************************************************************
 *  TSFTextStore::MouseTracker
 *****************************************************************************/

TSFTextStore::MouseTracker::MouseTracker() : mCookie(kInvalidCookie) {}

HRESULT TSFTextStore::MouseTracker::Init(TSFTextStore* aTextStore) {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::MouseTracker::Init(aTextStore=0x%p), "
           "aTextStore->mMouseTrackers.Length()=%zu",
           this, aTextStore, aTextStore->mMouseTrackers.Length()));

  if (&aTextStore->mMouseTrackers.LastElement() != this) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::MouseTracker::Init() FAILED due to "
             "this is not the last element of mMouseTrackers",
             this));
    return E_FAIL;
  }
  if (aTextStore->mMouseTrackers.Length() > kInvalidCookie) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::MouseTracker::Init() FAILED due to "
             "no new cookie available",
             this));
    return E_FAIL;
  }
  MOZ_ASSERT(!aTextStore->mMouseTrackers.IsEmpty(),
             "This instance must be in TSFTextStore::mMouseTrackers");
  mCookie = static_cast<DWORD>(aTextStore->mMouseTrackers.Length() - 1);
  return S_OK;
}

HRESULT TSFTextStore::MouseTracker::AdviseSink(TSFTextStore* aTextStore,
                                               ITfRangeACP* aTextRange,
                                               ITfMouseSink* aMouseSink) {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::MouseTracker::AdviseSink(aTextStore=0x%p, "
           "aTextRange=0x%p, aMouseSink=0x%p), mCookie=%ld, mSink=0x%p",
           this, aTextStore, aTextRange, aMouseSink, mCookie, mSink.get()));
  MOZ_ASSERT(mCookie != kInvalidCookie, "This hasn't been initalized?");

  if (mSink) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::MouseTracker::AdviseMouseSink() FAILED "
             "due to already being used",
             this));
    return E_FAIL;
  }

  MOZ_ASSERT(mRange.isNothing());

  LONG start = 0, length = 0;
  HRESULT hr = aTextRange->GetExtent(&start, &length);
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::MouseTracker::AdviseMouseSink() FAILED "
             "due to failure of ITfRangeACP::GetExtent()",
             this));
    return hr;
  }

  if (start < 0 || length <= 0 || start + length > LONG_MAX) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::MouseTracker::AdviseMouseSink() FAILED "
             "due to odd result of ITfRangeACP::GetExtent(), "
             "start=%ld, length=%ld",
             this, start, length));
    return E_INVALIDARG;
  }

  nsAutoString textContent;
  if (NS_WARN_IF(!aTextStore->GetCurrentText(
          textContent, AllowToFlushLayoutIfNoCache::Yes))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::MouseTracker::AdviseMouseSink() FAILED "
             "due to failure of TSFTextStore::GetCurrentText()",
             this));
    return E_FAIL;
  }

  if (textContent.Length() <= static_cast<uint32_t>(start) ||
      textContent.Length() < static_cast<uint32_t>(start + length)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStore::MouseTracker::AdviseMouseSink() FAILED "
             "due to out of range, start=%ld, length=%ld, "
             "textContent.Length()=%zu",
             this, start, length, textContent.Length()));
    return E_INVALIDARG;
  }

  mRange.emplace(start, start + length);

  mSink = aMouseSink;

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::MouseTracker::AdviseMouseSink(), "
           "succeeded, mRange=%s, textContent.Length()=%zu",
           this, ToString(mRange).c_str(), textContent.Length()));
  return S_OK;
}

void TSFTextStore::MouseTracker::UnadviseSink() {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::MouseTracker::UnadviseSink(), "
           "mCookie=%ld, mSink=0x%p, mRange=%s",
           this, mCookie, mSink.get(), ToString(mRange).c_str()));
  mSink = nullptr;
  mRange.reset();
}

bool TSFTextStore::MouseTracker::OnMouseButtonEvent(ULONG aEdge,
                                                    ULONG aQuadrant,
                                                    DWORD aButtonStatus) {
  MOZ_ASSERT(IsUsing(), "The caller must check before calling OnMouseEvent()");

  BOOL eaten = FALSE;
  RefPtr<ITfMouseSink> sink = mSink;
  HRESULT hr = sink->OnMouseEvent(aEdge, aQuadrant, aButtonStatus, &eaten);

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStore::MouseTracker::OnMouseEvent(aEdge=%ld, "
           "aQuadrant=%ld, aButtonStatus=0x%08lX), hr=0x%08lX, eaten=%s",
           this, aEdge, aQuadrant, aButtonStatus, hr,
           TSFUtils::BoolToChar(!!eaten)));

  return SUCCEEDED(hr) && eaten;
}

#ifdef DEBUG
// static
bool TSFTextStore::CurrentKeyboardLayoutHasIME() {
  const RefPtr<ITfInputProcessorProfiles> inputProcessorProfiles =
      TSFUtils::GetInputProcessorProfiles();
  if (!inputProcessorProfiles) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("TSFTextStore::CurrentKeyboardLayoutHasIME() FAILED due to "
             "there is no input processor profiles instance"));
    return false;
  }
  RefPtr<ITfInputProcessorProfileMgr> profileMgr;
  HRESULT hr = inputProcessorProfiles->QueryInterface(
      IID_ITfInputProcessorProfileMgr, getter_AddRefs(profileMgr));
  if (FAILED(hr) || !profileMgr) {
    // On Windows Vista or later, ImmIsIME() API always returns true.
    // If we failed to obtain the profile manager, we cannot know if current
    // keyboard layout has IME.
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::CurrentKeyboardLayoutHasIME() FAILED to query "
             "ITfInputProcessorProfileMgr"));
    return false;
  }

  TF_INPUTPROCESSORPROFILE profile;
  hr = profileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &profile);
  if (hr == S_FALSE) {
    return false;  // not found or not active
  }
  if (FAILED(hr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStore::CurrentKeyboardLayoutHasIME() FAILED to retrieve "
             "active profile"));
    return false;
  }
  return (profile.dwProfileType == TF_PROFILETYPE_INPUTPROCESSOR);
}
#endif  // #ifdef DEBUG

}  // namespace mozilla::widget
