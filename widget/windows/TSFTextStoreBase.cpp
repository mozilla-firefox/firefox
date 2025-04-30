/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TSFTextStoreBase.h"

#include "IMMHandler.h"
#include "TSFInputScope.h"
#include "TSFTextStore.h"
#include "TSFUtils.h"
#include "WinIMEHandler.h"
#include "WinMessages.h"
#include "WinUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_intl.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/TextEvents.h"
#include "mozilla/ToString.h"
#include "nsWindow.h"

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
/* TSFTextStoreBase                                               */
/******************************************************************/

bool TSFTextStoreBase::InitBase(nsWindow* aWidget,
                                const InputContext& aContext) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::InitBase(aWidget=0x%p, aContext=%s)", this,
           aWidget, mozilla::ToString(aContext).c_str()));

  if (NS_WARN_IF(!aWidget) || NS_WARN_IF(aWidget->Destroyed())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::InitBase() FAILED due to being "
             "initialized with "
             "destroyed widget",
             this));
    return false;
  }

  if (mDocumentMgr) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::InitBase() FAILED due to already "
             "initialized",
             this));
    return false;
  }

  mWidget = aWidget;
  if (NS_WARN_IF(!mWidget)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::InitBase() FAILED "
             "due to aWidget is nullptr ",
             this));
    return false;
  }
  mDispatcher = mWidget->GetTextEventDispatcher();
  if (NS_WARN_IF(!mDispatcher)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::InitBase() FAILED "
             "due to aWidget->GetTextEventDispatcher() failure",
             this));
    return false;
  }

  mInPrivateBrowsing = aContext.mInPrivateBrowsing;
  SetInputScope(aContext.mHTMLInputType, aContext.mHTMLInputMode);

  if (aContext.mURI) {
    // We don't need the document URL if it fails, let's ignore the error.
    nsAutoCString spec;
    if (NS_SUCCEEDED(aContext.mURI->GetSpec(spec))) {
      CopyUTF8toUTF16(spec, mDocumentURL);
    }
  }

  return true;
}

STDMETHODIMP TSFTextStoreBase::QueryInterface(REFIID riid, void** ppv) {
  *ppv = nullptr;
  if ((IID_IUnknown == riid) || (IID_ITextStoreACP == riid)) {
    *ppv = static_cast<ITextStoreACP*>(this);
  }
  if (*ppv) {
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDMETHODIMP TSFTextStoreBase::AdviseSink(REFIID riid, IUnknown* punk,
                                          DWORD dwMask) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::AdviseSink(riid=%s, punk=0x%p, dwMask=%s), "
           "mSink=0x%p, mSinkMask=%s",
           this, AutoRiidCString(riid).get(), punk,
           AutoSinkMasksCString(dwMask).get(), mSink.get(),
           AutoSinkMasksCString(mSinkMask).get()));

  if (!punk) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::AdviseSink() FAILED due to the null punk",
         this));
    return E_UNEXPECTED;
  }

  if (IID_ITextStoreACPSink != riid) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::AdviseSink() FAILED due to "
             "unsupported interface",
             this));
    return E_INVALIDARG;  // means unsupported interface.
  }

  if (!mSink) {
    // Install sink
    punk->QueryInterface(IID_ITextStoreACPSink, getter_AddRefs(mSink));
    if (!mSink) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStoreBase::AdviseSink() FAILED due to "
               "punk not having the interface",
               this));
      return E_UNEXPECTED;
    }
  } else {
    // If sink is already installed we check to see if they are the same
    // Get IUnknown from both sides for comparison
    RefPtr<IUnknown> comparison1, comparison2;
    punk->QueryInterface(IID_IUnknown, getter_AddRefs(comparison1));
    mSink->QueryInterface(IID_IUnknown, getter_AddRefs(comparison2));
    if (comparison1 != comparison2) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStoreBase::AdviseSink() FAILED due to "
               "the sink being different from the stored sink",
               this));
      return CONNECT_E_ADVISELIMIT;
    }
  }
  // Update mask either for a new sink or an existing sink
  mSinkMask = dwMask;
  return S_OK;
}

STDMETHODIMP TSFTextStoreBase::UnadviseSink(IUnknown* punk) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::UnadviseSink(punk=0x%p), mSink=0x%p", this,
           punk, mSink.get()));

  if (!punk) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::UnadviseSink() FAILED due to the null punk",
         this));
    return E_INVALIDARG;
  }
  if (!mSink) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::UnadviseSink() FAILED due to "
             "any sink not stored",
             this));
    return CONNECT_E_NOCONNECTION;
  }
  // Get IUnknown from both sides for comparison
  RefPtr<IUnknown> comparison1, comparison2;
  punk->QueryInterface(IID_IUnknown, getter_AddRefs(comparison1));
  mSink->QueryInterface(IID_IUnknown, getter_AddRefs(comparison2));
  // Unadvise only if sinks are the same
  if (comparison1 != comparison2) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::UnadviseSink() FAILED due to "
             "the sink being different from the stored sink",
             this));
    return CONNECT_E_NOCONNECTION;
  }
  mSink = nullptr;
  mSinkMask = 0;
  return S_OK;
}

STDMETHODIMP TSFTextStoreBase::RequestLock(DWORD dwLockFlags,
                                           HRESULT* phrSession) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("0x%p TSFTextStoreBase::RequestLock(dwLockFlags=%s, phrSession=0x%p), "
       "mLock=%s, mDestroyed=%s",
       this, AutoLockFlagsCString(dwLockFlags).get(), phrSession,
       AutoLockFlagsCString(mLock).get(), TSFUtils::BoolToChar(mDestroyed)));

  if (!mSink) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::RequestLock() FAILED due to "
             "any sink not stored",
             this));
    return E_FAIL;
  }
  if (mDestroyed) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::RequestLock() FAILED due to being destroyed",
         this));
    return E_FAIL;
  }
  if (!phrSession) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::RequestLock() FAILED due to "
             "null phrSession",
             this));
    return E_INVALIDARG;
  }

  if (!mLock) {
    // put on lock
    mLock = dwLockFlags & (~TS_LF_SYNC);
    MOZ_LOG(
        gIMELog, LogLevel::Info,
        ("0x%p   Locking (%s) >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
         ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>",
         this, AutoLockFlagsCString(mLock).get()));
    // Don't release this instance during this lock because this is called by
    // TSF but they don't grab us during this call.
    const RefPtr<TSFTextStoreBase> kungFuDeathGrip(this);
    const RefPtr<ITextStoreACPSink> sink = mSink;
    *phrSession = sink->OnLockGranted(mLock);
    MOZ_LOG(
        gIMELog, LogLevel::Info,
        ("0x%p   Unlocked (%s) <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
         "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<",
         this, AutoLockFlagsCString(mLock).get()));
    DidLockGranted();
    while (mLockQueued) {
      mLock = mLockQueued;
      mLockQueued = 0;
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("0x%p   Locking for the request in the queue (%s) >>>>>>>>>>>>>>"
               ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"
               ">>>>>",
               this, AutoLockFlagsCString(mLock).get()));
      sink->OnLockGranted(mLock);
      MOZ_LOG(gIMELog, LogLevel::Info,
              ("0x%p   Unlocked (%s) <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
               "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<"
               "<<<<<",
               this, AutoLockFlagsCString(mLock).get()));
      DidLockGranted();
    }

    // The document is now completely unlocked.
    mLock = 0;

    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStoreBase::RequestLock() succeeded: *phrSession=%s",
             this, TSFUtils::HRESULTToChar(*phrSession)));
    return S_OK;
  }

  // only time when reentrant lock is allowed is when caller holds a
  // read-only lock and is requesting an async write lock
  if (IsReadLocked() && !IsReadWriteLocked() && IsReadWriteLock(dwLockFlags) &&
      !(dwLockFlags & TS_LF_SYNC)) {
    *phrSession = TS_S_ASYNC;
    mLockQueued = dwLockFlags & (~TS_LF_SYNC);

    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStoreBase::RequestLock() stores the request in the "
             "queue, *phrSession=TS_S_ASYNC",
             this));
    return S_OK;
  }

  // no more locks allowed
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStoreBase::RequestLock() didn't allow to lock, "
           "*phrSession=TS_E_SYNCHRONOUS",
           this));
  *phrSession = TS_E_SYNCHRONOUS;
  return E_FAIL;
}

void TSFTextStoreBase::DispatchEvent(WidgetGUIEvent& aEvent) {
  if (NS_WARN_IF(!mWidget) || NS_WARN_IF(mWidget->Destroyed())) {
    return;
  }
  // If the event isn't a query content event, the event may be handled
  // asynchronously.  So, we should put off to answer from GetTextExt() etc.
  if (!aEvent.AsQueryContentEvent()) {
    mDeferNotifyingTSFUntilNextUpdate = true;
  }
  mWidget->DispatchWindowEvent(aEvent);
}

STDMETHODIMP TSFTextStoreBase::GetStatus(TS_STATUS* pdcs) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::GetStatus(pdcs=0x%p)", this, pdcs));

  if (!pdcs) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::GetStatus() FAILED due to null pdcs", this));
    return E_INVALIDARG;
  }
  // We manage on-screen keyboard by own.
  pdcs->dwDynamicFlags = TS_SD_INPUTPANEMANUALDISPLAYENABLE;
  // we use a "flat" text model for TSF support so no hidden text
  pdcs->dwStaticFlags = TS_SS_NOHIDDENTEXT;
  return S_OK;
}

STDMETHODIMP TSFTextStoreBase::QueryInsert(LONG acpTestStart, LONG acpTestEnd,
                                           ULONG cch, LONG* pacpResultStart,
                                           LONG* pacpResultEnd) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("0x%p TSFTextStoreBase::QueryInsert(acpTestStart=%ld, "
       "acpTestEnd=%ld, cch=%lu, pacpResultStart=0x%p, pacpResultEnd=0x%p)",
       this, acpTestStart, acpTestEnd, cch, pacpResultStart, pacpResultEnd));

  if (!pacpResultStart || !pacpResultEnd) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::QueryInsert() FAILED due to "
             "the null argument",
             this));
    return E_INVALIDARG;
  }

  if (acpTestStart < 0 || acpTestStart > acpTestEnd) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::QueryInsert() FAILED due to "
             "wrong argument",
             this));
    return E_INVALIDARG;
  }

  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::GetSelection(ULONG ulIndex, ULONG ulCount,
                                            TS_SELECTION_ACP* pSelection,
                                            ULONG* pcFetched) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::GetSelection(ulIndex=%lu, ulCount=%lu, "
           "pSelection=0x%p, pcFetched=0x%p)",
           this, ulIndex, ulCount, pSelection, pcFetched));

  if (!IsReadLocked()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetSelection() FAILED due to not locked",
             this));
    return TS_E_NOLOCK;
  }
  if (!ulCount || !pSelection || !pcFetched) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::GetSelection() FAILED due to null argument",
         this));
    return E_INVALIDARG;
  }

  *pcFetched = 0;

  if (ulIndex != static_cast<ULONG>(TS_DEFAULT_SELECTION) && ulIndex != 0) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetSelection() FAILED due to "
             "unsupported selection",
             this));
    return TS_E_NOSELECTION;
  }

  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::SetSelection(
    ULONG ulCount, const TS_SELECTION_ACP* pSelection) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::SetSelection(ulCount=%lu, pSelection=%s })",
           this, ulCount,
           pSelection ? mozilla::ToString(pSelection).c_str() : "nullptr"));

  if (!IsReadWriteLocked()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::SetSelection() FAILED due to "
             "not locked (read-write)",
             this));
    return TS_E_NOLOCK;
  }
  if (ulCount != 1) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::SetSelection() FAILED due to "
             "trying setting multiple selection",
             this));
    return E_INVALIDARG;
  }
  if (!pSelection) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::SetSelection() FAILED due to "
             "null argument",
             this));
    return E_INVALIDARG;
  }

  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::GetText(LONG acpStart, LONG acpEnd,
                                       WCHAR* pchPlain, ULONG cchPlainReq,
                                       ULONG* pcchPlainOut,
                                       TS_RUNINFO* prgRunInfo,
                                       ULONG ulRunInfoReq, ULONG* pulRunInfoOut,
                                       LONG* pacpNext) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("0x%p TSFTextStoreBase::GetText(acpStart=%ld, acpEnd=%ld, "
       "pchPlain=0x%p, cchPlainReq=%lu, pcchPlainOut=0x%p, prgRunInfo=0x%p, "
       "ulRunInfoReq=%lu, pulRunInfoOut=0x%p, pacpNext=0x%p)",
       this, acpStart, acpEnd, pchPlain, cchPlainReq, pcchPlainOut, prgRunInfo,
       ulRunInfoReq, pulRunInfoOut, pacpNext));

  if (!IsReadLocked()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetText() FAILED due to "
             "not locked (read)",
             this));
    return TS_E_NOLOCK;
  }

  if (!pcchPlainOut || (!pchPlain && !prgRunInfo) ||
      !cchPlainReq != !pchPlain || !ulRunInfoReq != !prgRunInfo) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetText() FAILED due to "
             "invalid argument",
             this));
    return E_INVALIDARG;
  }

  if (acpStart < 0 || acpEnd < -1 || (acpEnd != -1 && acpStart > acpEnd)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetText() FAILED due to "
             "invalid position",
             this));
    return TS_E_INVALIDPOS;
  }

  // Making sure to null-terminate string just to be on the safe side
  *pcchPlainOut = 0;
  if (pchPlain && cchPlainReq) {
    *pchPlain = 0;
  }
  if (pulRunInfoOut) {
    *pulRunInfoOut = 0;
  }
  if (pacpNext) {
    *pacpNext = acpStart;
  }
  if (prgRunInfo && ulRunInfoReq) {
    prgRunInfo->uCount = 0;
    prgRunInfo->type = TS_RT_PLAIN;
  }

  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::SetText(DWORD dwFlags, LONG acpStart,
                                       LONG acpEnd, const WCHAR* pchText,
                                       ULONG cch, TS_TEXTCHANGE* pChange) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("0x%p TSFTextStoreBase::SetText(dwFlags=%s, acpStart=%ld, acpEnd=%ld, "
       "pchText=0x%p \"%s\", cch=%lu, pChange=0x%p)",
       this, dwFlags == TS_ST_CORRECTION ? "TS_ST_CORRECTION" : "not-specified",
       acpStart, acpEnd, pchText,
       pchText && cch ? AutoEscapedUTF8String(pchText, cch).get() : "", cch,
       pChange));

  // Per SDK documentation, and since we don't have better
  // ways to do this, this method acts as a helper to
  // call SetSelection followed by InsertTextAtSelection
  if (!IsReadWriteLocked()) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::SetText() FAILED due to not locked (read)",
         this));
    return TS_E_NOLOCK;
  }

  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::GetFormattedText(LONG acpStart, LONG acpEnd,
                                                IDataObject** ppDataObject) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::GetFormattedText() called "
           "but not supported (E_NOTIMPL)",
           this));

  // no support for formatted text
  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::GetEmbedded(LONG acpPos, REFGUID rguidService,
                                           REFIID riid, IUnknown** ppunk) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::GetEmbedded() called "
           "but not supported (E_NOTIMPL)",
           this));

  // embedded objects are not supported
  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::QueryInsertEmbedded(const GUID* pguidService,
                                                   const FORMATETC* pFormatEtc,
                                                   BOOL* pfInsertable) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::QueryInsertEmbedded() called "
           "but not supported, *pfInsertable=FALSE (S_OK)",
           this));

  // embedded objects are not supported
  *pfInsertable = FALSE;
  return S_OK;
}

STDMETHODIMP TSFTextStoreBase::InsertEmbedded(DWORD dwFlags, LONG acpStart,
                                              LONG acpEnd,
                                              IDataObject* pDataObject,
                                              TS_TEXTCHANGE* pChange) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::InsertEmbedded() called "
           "but not supported (E_NOTIMPL)",
           this));

  // embedded objects are not supported
  return E_NOTIMPL;
}

void TSFTextStoreBase::SetInputScope(const nsString& aHTMLInputType,
                                     const nsString& aHTMLInputMode) {
  mInputScopes.Clear();

  // IME may refer only first input scope, but we will append inputmode's
  // input scopes too like Chrome since IME may refer it.
  IMEHandler::AppendInputScopeFromType(aHTMLInputType, mInputScopes);
  IMEHandler::AppendInputScopeFromInputMode(aHTMLInputMode, mInputScopes);

  if (mInPrivateBrowsing) {
    mInputScopes.AppendElement(IS_PRIVATE);
  }
}

STDMETHODIMP TSFTextStoreBase::RequestAttrsTransitioningAtPosition(
    LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID* paFilterAttr,
    DWORD dwFlags) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::RequestAttrsTransitioningAtPosition("
           "acpPos=%ld, cFilterAttrs=%lu, dwFlags=%s) called but not supported "
           "(S_OK)",
           this, acpPos, cFilterAttrs, AutoFindFlagsCString(dwFlags).get()));

  // no per character attributes defined
  return S_OK;
}

STDMETHODIMP TSFTextStoreBase::FindNextAttrTransition(
    LONG acpStart, LONG acpHalt, ULONG cFilterAttrs,
    const TS_ATTRID* paFilterAttrs, DWORD dwFlags, LONG* pacpNext,
    BOOL* pfFound, LONG* plFoundOffset) {
  if (!pacpNext || !pfFound || !plFoundOffset) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  0x%p TSFTextStoreBase::FindNextAttrTransition() FAILED due to "
             "null argument",
             this));
    return E_INVALIDARG;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStoreBase::FindNextAttrTransition() called "
           "but not supported (S_OK)",
           this));

  // no per character attributes defined
  *pacpNext = *plFoundOffset = acpHalt;
  *pfFound = FALSE;
  return S_OK;
}

// To test the document URL result, define this to out put it to the stdout
// #define DEBUG_PRINT_DOCUMENT_URL

BSTR TSFTextStoreBase::GetExposingURL() const {
  const bool allowed =
      StaticPrefs::intl_tsf_expose_url_allowed() &&
      (!mInPrivateBrowsing ||
       StaticPrefs::intl_tsf_expose_url_in_private_browsing_allowed());
  if (!allowed || mDocumentURL.IsEmpty()) {
    BSTR emptyString = ::SysAllocString(L"");
    MOZ_ASSERT(
        emptyString,
        "We need to return valid BSTR pointer to notify TSF of supporting it "
        "with a pointer to empty string");
    return emptyString;
  }
  return ::SysAllocString(mDocumentURL.get());
}

void TSFTextStoreBase::PrintExposingURL(const char* aPrefix) const {
  BSTR exposingURL = GetExposingURL();
  printf("%s: DocumentURL=\"%s\"\n", aPrefix,
         NS_ConvertUTF16toUTF8(static_cast<char16ptr_t>(_bstr_t(exposingURL)))
             .get());
  ::SysFreeString(exposingURL);
}

STDMETHODIMP TSFTextStoreBase::GetEndACP(LONG* pacp) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::GetEndACP(pacp=0x%p)", this, pacp));

  if (!IsReadLocked()) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::GetEndACP() FAILED due to not locked (read)",
         this));
    return TS_E_NOLOCK;
  }

  if (!pacp) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetEndACP() FAILED due to null argument",
             this));
    return E_INVALIDARG;
  }

  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::GetActiveView(TsViewCookie* pvcView) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("0x%p TSFTextStoreBase::GetActiveView(pvcView=0x%p)", this, pvcView));

  if (!pvcView) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetActiveView() FAILED due to "
             "null argument",
             this));
    return E_INVALIDARG;
  }

  *pvcView = TSFUtils::sDefaultView;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStoreBase::GetActiveView() succeeded: *pvcView=%ld",
           this, *pvcView));
  return S_OK;
}

STDMETHODIMP TSFTextStoreBase::GetACPFromPoint(TsViewCookie vcView,
                                               const POINT* pt, DWORD dwFlags,
                                               LONG* pacp) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::GetACPFromPoint(pvcView=%ld, pt=%p (x=%ld, "
           "y=%ld), dwFlags=%s, pacp=%p, mDeferNotifyingTSFUntilNextUpdate=%s, "
           "mWaitingQueryLayout=%s",
           this, vcView, pt, pt ? pt->x : 0, pt ? pt->y : 0,
           AutoACPFromPointFlagsCString(dwFlags).get(), pacp,
           TSFUtils::BoolToChar(mDeferNotifyingTSFUntilNextUpdate),
           TSFUtils::BoolToChar(mWaitingQueryLayout)));

  if (!IsReadLocked()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetACPFromPoint() FAILED due to not "
             "locked (read)",
             this));
    return TS_E_NOLOCK;
  }

  if (vcView != TSFUtils::sDefaultView) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetACPFromPoint() FAILED due to called "
             "with invalid view",
             this));
    return E_INVALIDARG;
  }

  if (!pt) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetACPFromPoint() FAILED due to null pt",
             this));
    return E_INVALIDARG;
  }

  if (!pacp) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::GetACPFromPoint() FAILED due to null pacp",
         this));
    return E_INVALIDARG;
  }

  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::GetTextExt(TsViewCookie vcView, LONG acpStart,
                                          LONG acpEnd, RECT* prc,
                                          BOOL* pfClipped) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::GetTextExt(vcView=%ld, "
           "acpStart=%ld, acpEnd=%ld, prc=0x%p, pfClipped=0x%p), "
           "IsHandlingCompositionInParent()=%s, "
           "IsHandlingCompositionInContent()=%s,"
           "mDeferNotifyingTSFUntilNextUpdate=%s, mWaitingQueryLayout=%s, "
           "IMEHandler::IsA11yHandlingNativeCaret()=%s",
           this, vcView, acpStart, acpEnd, prc, pfClipped,
           TSFUtils::BoolToChar(IsHandlingCompositionInParent()),
           TSFUtils::BoolToChar(IsHandlingCompositionInContent()),
           TSFUtils::BoolToChar(mDeferNotifyingTSFUntilNextUpdate),
           TSFUtils::BoolToChar(mWaitingQueryLayout),
           TSFUtils::BoolToChar(IMEHandler::IsA11yHandlingNativeCaret())));

  if (!IsReadLocked()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetTextExt() FAILED due to not locked "
             "(read)",
             this));
    return TS_E_NOLOCK;
  }

  if (vcView != TSFUtils::sDefaultView) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetTextExt() FAILED due to called with "
             "invalid view",
             this));
    return E_INVALIDARG;
  }

  if (!prc || !pfClipped) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::GetTextExt() FAILED due to null argument",
         this));
    return E_INVALIDARG;
  }

  // According to MSDN, ITextStoreACP::GetTextExt() should return
  // TS_E_INVALIDARG when acpStart and acpEnd are same (i.e., collapsed range).
  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms538435(v=vs.85).aspx
  // > TS_E_INVALIDARG: The specified start and end character positions are
  // >                  equal.
  // However, some TIPs (including Microsoft's Chinese TIPs!) call this with
  // collapsed range and if we return TS_E_INVALIDARG, they stops showing their
  // owning window or shows it but odd position.  So, we should just return
  // error only when acpStart and/or acpEnd are really odd.

  if (acpStart < 0 || acpEnd < acpStart) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::GetTextExt() FAILED due to invalid position",
         this));
    return TS_E_INVALIDPOS;
  }

  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::GetScreenExt(TsViewCookie vcView, RECT* prc) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::GetScreenExt(vcView=%ld, prc=0x%p)", this,
           vcView, prc));

  if (vcView != TSFUtils::sDefaultView) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetScreenExt() FAILED due to called "
             "with invalid view",
             this));
    return E_INVALIDARG;
  }

  if (!prc) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFTextStoreBase::GetScreenExt() FAILED due to null argument",
         this));
    return E_INVALIDARG;
  }

  if (mDestroyed) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetScreenExt() returns empty rect "
             "due to already destroyed",
             this));
    prc->left = prc->top = prc->right = prc->bottom = 0;
    return S_OK;
  }

  if (!GetScreenExtInternal(*prc)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetScreenExt() FAILED due to "
             "GetScreenExtInternal() failure",
             this));
    return E_FAIL;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStoreBase::GetScreenExt() succeeded: "
           "*prc={ left=%ld, top=%ld, right=%ld, bottom=%ld }",
           this, prc->left, prc->top, prc->right, prc->bottom));
  return S_OK;
}

bool TSFTextStoreBase::GetScreenExtInternal(RECT& aScreenExt) {
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStoreBase::GetScreenExtInternal()", this));

  MOZ_ASSERT(!mDestroyed);

  // use NS_QUERY_EDITOR_RECT to get rect in system, screen coordinates
  WidgetQueryContentEvent queryEditorRectEvent(true, eQueryEditorRect, mWidget);
  mWidget->InitEvent(queryEditorRectEvent);
  DispatchEvent(queryEditorRectEvent);
  if (queryEditorRectEvent.Failed()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetScreenExtInternal() FAILED due to "
             "eQueryEditorRect failure",
             this));
    return false;
  }

  nsWindow* refWindow =
      static_cast<nsWindow*>(!!queryEditorRectEvent.mReply->mFocusedWidget
                                 ? queryEditorRectEvent.mReply->mFocusedWidget
                                 : static_cast<nsIWidget*>(mWidget.get()));
  // Result rect is in top level widget coordinates
  refWindow = refWindow->GetTopLevelWindow(false);
  if (!refWindow) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetScreenExtInternal() FAILED due to "
             "no top level window",
             this));
    return false;
  }

  LayoutDeviceIntRect boundRect = refWindow->GetClientBounds();
  boundRect.MoveTo(0, 0);

  // Clip frame rect to window rect
  boundRect.IntersectRect(queryEditorRectEvent.mReply->mRect, boundRect);
  if (!boundRect.IsEmpty()) {
    boundRect.MoveBy(refWindow->WidgetToScreenOffset());
    ::SetRect(&aScreenExt, boundRect.X(), boundRect.Y(), boundRect.XMost(),
              boundRect.YMost());
  } else {
    ::SetRectEmpty(&aScreenExt);
  }

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFTextStoreBase::GetScreenExtInternal() succeeded: "
           "aScreenExt={ left=%ld, top=%ld, right=%ld, bottom=%ld }",
           this, aScreenExt.left, aScreenExt.top, aScreenExt.right,
           aScreenExt.bottom));
  return true;
}

STDMETHODIMP TSFTextStoreBase::GetWnd(TsViewCookie vcView, HWND* phwnd) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::GetWnd(vcView=%ld, phwnd=0x%p), "
           "mWidget=0x%p",
           this, vcView, phwnd, mWidget.get()));

  if (vcView != TSFUtils::sDefaultView) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetWnd() FAILED due to "
             "called with invalid view",
             this));
    return E_INVALIDARG;
  }

  if (!phwnd) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::GetScreenExt() FAILED due to "
             "null argument",
             this));
    return E_INVALIDARG;
  }

  *phwnd = mWidget ? mWidget->GetWindowHandle() : nullptr;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFTextStoreBase::GetWnd() succeeded: *phwnd=0x%p", this,
           static_cast<void*>(*phwnd)));
  return S_OK;
}

STDMETHODIMP TSFTextStoreBase::InsertTextAtSelection(DWORD dwFlags,
                                                     const WCHAR* pchText,
                                                     ULONG cch, LONG* pacpStart,
                                                     LONG* pacpEnd,
                                                     TS_TEXTCHANGE* pChange) {
  MOZ_LOG(
      gIMELog, LogLevel::Info,
      ("0x%p TSFTextStoreBase::InsertTextAtSelection(dwFlags=%s, "
       "pchText=0x%p \"%s\", cch=%lu, pacpStart=0x%p, pacpEnd=0x%p, "
       "pChange=0x%p)",
       this,
       dwFlags == 0                  ? "0"
       : dwFlags == TF_IAS_NOQUERY   ? "TF_IAS_NOQUERY"
       : dwFlags == TF_IAS_QUERYONLY ? "TF_IAS_QUERYONLY"
                                     : "Unknown",
       pchText, pchText && cch ? AutoEscapedUTF8String(pchText, cch).get() : "",
       cch, pacpStart, pacpEnd, pChange));

  if (cch && !pchText) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::InsertTextAtSelection() FAILED due to "
             "null pchText",
             this));
    return E_INVALIDARG;
  }

  if (TS_IAS_QUERYONLY == dwFlags) {
    if (!IsReadLocked()) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStoreBase::InsertTextAtSelection() FAILED due to "
               "not locked (read)",
               this));
      return TS_E_NOLOCK;
    }

    if (!pacpStart || !pacpEnd) {
      MOZ_LOG(gIMELog, LogLevel::Error,
              ("0x%p   TSFTextStoreBase::InsertTextAtSelection() FAILED due to "
               "null argument",
               this));
      return E_INVALIDARG;
    }

    return E_NOTIMPL;
  }

  if (!IsReadWriteLocked()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::InsertTextAtSelection() FAILED due to "
             "not locked (read-write)",
             this));
    return TS_E_NOLOCK;
  }

  if (!pChange) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::InsertTextAtSelection() FAILED due to "
             "null pChange",
             this));
    return E_INVALIDARG;
  }

  if (TS_IAS_NOQUERY != dwFlags && (!pacpStart || !pacpEnd)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFTextStoreBase::InsertTextAtSelection() FAILED due to "
             "null argument",
             this));
    return E_INVALIDARG;
  }

  return E_NOTIMPL;
}

STDMETHODIMP TSFTextStoreBase::InsertEmbeddedAtSelection(
    DWORD dwFlags, IDataObject* pDataObject, LONG* pacpStart, LONG* pacpEnd,
    TS_TEXTCHANGE* pChange) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::InsertEmbeddedAtSelection() called "
           "but not supported (E_NOTIMPL)",
           this));

  // embedded objects are not supported
  return E_NOTIMPL;
}

HRESULT TSFTextStoreBase::HandleRequestAttrs(DWORD aFlags, ULONG aFilterCount,
                                             const TS_ATTRID* aFilterAttrs,
                                             int32_t aNumOfSupportedAttrs) {
  MOZ_ASSERT(aNumOfSupportedAttrs == TSFUtils::NUM_OF_SUPPORTED_ATTRS ||
             aNumOfSupportedAttrs ==
                 TSFUtils::NUM_OF_SUPPORTED_ATTRS_IN_EMPTY_TEXT_STORE);
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::HandleRequestAttrs(aFlags=%s, "
           "aFilterCount=%lu, aNumOfSupportedAttrs=%d)",
           this, AutoFindFlagsCString(aFlags).get(), aFilterCount,
           aNumOfSupportedAttrs));

  // This is a little weird! RequestSupportedAttrs gives us advanced notice
  // of a support query via RetrieveRequestedAttrs for a specific attribute.
  // RetrieveRequestedAttrs needs to return valid data for all attributes we
  // support, but the text service will only want the input scope object
  // returned in RetrieveRequestedAttrs if the dwFlags passed in here contains
  // TS_ATTR_FIND_WANT_VALUE.
  for (const int32_t i : IntegerRange(aNumOfSupportedAttrs)) {
    mRequestedAttrs[i] = false;
  }
  mRequestedAttrValues = !!(aFlags & TS_ATTR_FIND_WANT_VALUE);

  for (uint32_t i : IntegerRange(aFilterCount)) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFEmptyTextStore::HandleRequestAttrs(), "
             "requested attr=%s",
             this, AutoGuidCString(aFilterAttrs[i]).get()));
    TSFUtils::AttrIndex index =
        TSFUtils::GetRequestedAttrIndex(aFilterAttrs[i]);
    if (index != TSFUtils::AttrIndex::NotSupported) {
      mRequestedAttrs[index] = true;
    }
  }
  return S_OK;
}

// To test the document URL result, define this to out put it to the stdout
// #define DEBUG_PRINT_DOCUMENT_URL

HRESULT TSFTextStoreBase::RetrieveRequestedAttrsInternal(
    ULONG ulCount, TS_ATTRVAL* paAttrVals, ULONG* pcFetched,
    int32_t aNumOfSupportedAttrs) {
  MOZ_ASSERT(aNumOfSupportedAttrs == TSFUtils::NUM_OF_SUPPORTED_ATTRS ||
             aNumOfSupportedAttrs ==
                 TSFUtils::NUM_OF_SUPPORTED_ATTRS_IN_EMPTY_TEXT_STORE);

  if (!pcFetched || !paAttrVals) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p TSFTextStoreBase::RetrieveRequestedAttrs() FAILED due to "
             "null argument",
             this));
    return E_INVALIDARG;
  }

  const ULONG expectedCount = [&]() {
    ULONG count = 0;
    for (int32_t i : IntegerRange(aNumOfSupportedAttrs)) {
      if (mRequestedAttrs[i]) {
        count++;
      }
    }
    return count;
  }();
  if (ulCount < expectedCount) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p TSFTextStoreBase::RetrieveRequestedAttrs() FAILED due to "
             "not enough count ulCount=%lu, expectedCount=%lu",
             this, ulCount, expectedCount));
    return E_INVALIDARG;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFTextStoreBase::RetrieveRequestedAttrs() called "
           "ulCount=%lu, mRequestedAttrValues=%s",
           this, ulCount, TSFUtils::BoolToChar(mRequestedAttrValues)));

#ifdef DEBUG_PRINT_DOCUMENT_URL
  PrintExposingURL("TSFTextStoreBase::RetrieveRequestedAttrs");
#endif  // #ifdef DEBUG_PRINT_DOCUMENT_URL

  int32_t count = 0;
  for (int32_t i = 0; i < TSFUtils::NUM_OF_SUPPORTED_ATTRS; i++) {
    if (!mRequestedAttrs[i]) {
      continue;
    }
    mRequestedAttrs[i] = false;

    TS_ATTRID attrID = TSFUtils::GetAttrID(static_cast<TSFUtils::AttrIndex>(i));

    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p   TSFTextStoreBase::RetrieveRequestedAttrs() for %s", this,
             AutoGuidCString(attrID).get()));

    paAttrVals[count].idAttr = attrID;
    paAttrVals[count].dwOverlapId = 0;

    if (!mRequestedAttrValues) {
      paAttrVals[count].varValue.vt = VT_EMPTY;
    } else {
      switch (i) {
        case TSFUtils::AttrIndex::InputScope: {
          paAttrVals[count].varValue.vt = VT_UNKNOWN;
          RefPtr<IUnknown> inputScope = new TSFInputScope(mInputScopes);
          paAttrVals[count].varValue.punkVal = inputScope.forget().take();
          break;
        }
        case TSFUtils::AttrIndex::DocumentURL: {
          paAttrVals[count].varValue.vt = VT_BSTR;
          paAttrVals[count].varValue.bstrVal = GetExposingURL();
          break;
        }
        case TSFUtils::AttrIndex::TextVerticalWriting: {
          const Maybe<WritingMode> writingMode = GetWritingMode();
          paAttrVals[count].varValue.vt = VT_BOOL;
          paAttrVals[count].varValue.boolVal =
              writingMode.isSome() && writingMode->IsVertical() ? VARIANT_TRUE
                                                                : VARIANT_FALSE;
          break;
        }
        case TSFUtils::AttrIndex::TextOrientation: {
          const Maybe<WritingMode> writingMode = GetWritingMode();
          paAttrVals[count].varValue.vt = VT_I4;
          paAttrVals[count].varValue.lVal =
              writingMode.isSome() && writingMode->IsVertical() ? 2700 : 0;
          break;
        }
        default:
          MOZ_CRASH("Invalid index? Or not implemented yet?");
          break;
      }
    }
    count++;
  }

  mRequestedAttrValues = false;

  if (count) {
    *pcFetched = count;
    return S_OK;
  }

  paAttrVals->dwOverlapId = 0;
  paAttrVals->varValue.vt = VT_EMPTY;
  *pcFetched = 0;
  return S_OK;
}

#undef DEBUG_PRINT_DOCUMENT_URL

// static
void TSFTextStoreBase::SetInputContext(nsWindow* aWindow,
                                       const InputContext& aContext,
                                       const InputContextAction& aAction) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("TSFTextStoreBase::OnSetInputContext(aWidget=%p, "
           "aContext=%s, aAction.mFocusChange=%s), "
           "CurrentTextStore(0x%p)={ mWidget=0x%p, mContext=0x%p }",
           aWindow, mozilla::ToString(aContext).c_str(),
           mozilla::ToString(aAction.mFocusChange).c_str(),
           TSFUtils::GetCurrentTextStore(),
           TSFUtils::GetCurrentTextStore()
               ? TSFUtils::GetCurrentTextStore()->GetWindow()
               : nullptr,
           TSFUtils::GetCurrentTextStore()
               ? TSFUtils::GetCurrentTextStore()->GetContext()
               : nullptr));

  const bool actuallyEditable =
      aContext.mIMEState.IsEditable() && !aWindow->Destroyed();
  switch (aAction.mFocusChange) {
    case InputContextAction::WIDGET_CREATED:
      // If this is called when the widget is created, there is nothing to do.
      return;
    case InputContextAction::FOCUS_NOT_CHANGED:
    case InputContextAction::MENU_LOST_PSEUDO_FOCUS:
      // In these cases, `NOTIFY_IME_OF_FOCUS` won't be sent.  Therefore,
      // we need to reset text store for new state right now.
      break;
    default: {
      const RefPtr<TSFTextStoreBase> textStore =
          TSFUtils::GetCurrentTextStore();
      if (!textStore) {
        break;
      }
      if (NS_SUCCEEDED(
              textStore->UpdateDocumentURLAndBrowsingMode(aWindow, aContext))) {
        return;
      }
    }
  }

  const bool alreadyEditable = TSFUtils::GetCurrentTextStore() &&
                               TSFUtils::GetCurrentTextStore()->IsEditable() &&
                               TSFUtils::GetCurrentTextStore()->MaybeHasFocus();
  if (alreadyEditable == actuallyEditable) {
    if (const RefPtr<TSFTextStoreBase> textStore =
            TSFUtils::GetCurrentTextStore()) {
      textStore->UpdateDocumentURLAndBrowsingMode(aWindow, aContext);
    }
    return;
  }
  // If focus isn't actually changed but the enabled state is changed, we beed
  // to emulate the focus move.
  if (!alreadyEditable && !IMEHandler::GetFocusedWindow()) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFTextStoreBase::SetInputContent() gets called to enable IME, "
             "but IMEHandler has not received focus notification"));
    if (const RefPtr<TSFTextStoreBase> textStore =
            TSFUtils::GetCurrentTextStore()) {
      textStore->UpdateDocumentURLAndBrowsingMode(aWindow, aContext);
    }
    return;
  }
  TSFUtils::OnFocusChange(
      actuallyEditable ? TSFUtils::GotFocus::Yes : TSFUtils::GotFocus::No,
      aWindow, aContext);
}

nsresult TSFTextStoreBase::UpdateDocumentURLAndBrowsingMode(
    nsWindow* aWindow, const InputContext& aContext) {
  MOZ_ASSERT(aWindow);
  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p TSFTextStoreBase::UpdateDocumentURLAndBrowsingMode(aWindow=%p "
           "(Destroyed()=%s), aContext=%s), "
           "mIsEditable=%s",
           this, aWindow, aWindow->Destroyed() ? "true" : "false",
           mozilla::ToString(aContext).c_str(),
           mozilla::ToString(mIsEditable).c_str()));

  const bool isEditable =
      aContext.mIMEState.IsEditable() && !aWindow->Destroyed();
  // If IME enabled state is changed, we need to recreate proper
  // TSFTextStoreBase instance.
  if (isEditable != IsEditable()) {
    return NS_ERROR_FAILURE;
  }
  // If the editable state is not changed, we can just update the input
  // scopes and the document URL.
  nsAutoString oldURL(mDocumentURL);
  CopyableAutoTArray<InputScope, 5> oldInputScopes(mInputScopes);
  mInPrivateBrowsing = aContext.mInPrivateBrowsing;
  SetInputScope(aContext.mHTMLInputType, aContext.mHTMLInputMode);
  if (aContext.mURI) {
    nsAutoCString spec;
    if (NS_SUCCEEDED(aContext.mURI->GetSpec(spec))) {
      CopyUTF8toUTF16(spec, mDocumentURL);
    } else {
      mDocumentURL.Truncate();
    }
  } else {
    mDocumentURL.Truncate();
  }
  // Notify TSF of the URL and InputScope changes.
  const auto& changedThings = [&]() -> AttrIndices {
    const bool URLChanged = mDocumentURL != oldURL;
    const bool inputScopeChanged = mInputScopes != oldInputScopes;
    if (URLChanged && inputScopeChanged) {
      return URLAndInputScopeChanged;
    }
    if (URLChanged) {
      return OnlyURLChanged;
    }
    return inputScopeChanged ? OnlyURLChanged : NothingChanged;
  }();
  if (changedThings != NothingChanged) {
    NotifyTSFOfInputContextChange(changedThings);
  }
  return NS_OK;
}

void TSFTextStoreBase::NotifyTSFOfInputContextChange(AttrIndices aAttrIndices) {
  MOZ_LOG(
      gIMELog, LogLevel::Debug,
      ("0x%p TSFTextStoreBase::NotifyTSFOfInputContextChange(), mSink=0x%p, "
       "DocumentURL is changed=%s, InputScope is changed=%s",
       this, mSink.get(),
       aAttrIndices.contains(TSFUtils::AttrIndex::DocumentURL) ? "Yes" : "No",
       aAttrIndices.contains(TSFUtils::AttrIndex::InputScope) ? "Yes" : "No"));

  if (!mSink) {
    return;
  }

  AutoTArray<TS_ATTRID, 2> attrIDs;
  if (aAttrIndices.contains(TSFUtils::AttrIndex::DocumentURL)) {
    attrIDs.AppendElement(
        TSFUtils::GetAttrID(TSFUtils::AttrIndex::DocumentURL));
  }
  if (aAttrIndices.contains(TSFUtils::AttrIndex::InputScope)) {
    attrIDs.AppendElement(TSFUtils::GetAttrID(TSFUtils::AttrIndex::InputScope));
  }
  RefPtr<ITextStoreACPSink> sink(mSink);
  sink->OnAttrsChange(0, 0, attrIDs.Length(), attrIDs.Elements());
}

}  // namespace mozilla::widget
