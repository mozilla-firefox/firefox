/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TSFEmptyTextStore.h"

#include "IMMHandler.h"
#include "KeyboardLayout.h"
#include "TSFInputScope.h"
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
/* TSFEmptyTextStore                                              */
/******************************************************************/

TSFEmptyTextStore::TSFEmptyTextStore() : TSFTextStoreBase(Editable::No) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFEmptyTextStore instance is created", this));
}

TSFEmptyTextStore::~TSFEmptyTextStore() {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFEmptyTextStore instance is destroyed", this));
}

bool TSFEmptyTextStore::Init(nsWindow* aWidget, const InputContext& aContext) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFEmptyTextStore::Init(aWidget=0x%p)", this, aWidget));

  if (NS_WARN_IF(!InitBase(aWidget, aContext))) {
    return false;
  }

  // Create document manager
  const RefPtr<ITfThreadMgr> threadMgr = TSFUtils::GetThreadMgr();
  RefPtr<ITfDocumentMgr> documentMgr;
  HRESULT hr = threadMgr->CreateDocumentMgr(getter_AddRefs(documentMgr));
  if (NS_WARN_IF(FAILED(hr))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFEmptyTextStore::Init() FAILED to create ITfDocumentMgr "
             "(0x%08lX)",
             this, hr));
    return false;
  }
  if (NS_WARN_IF(mDestroyed)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFEmptyTextStore::Init() FAILED to create ITfDocumentMgr "
             "due to TextStore being destroyed during calling "
             "ITfThreadMgr::CreateDocumentMgr()",
             this));
    return false;
  }

  const auto EmptyContextIsSupported = [&]() -> bool {
    // The empty TSF text store support was introduced with Windows 11.
    // If it's supported, QI for GUID_COMPARTMENT_EMPTYCONTEXT will success.
    // Then, we use an empty text store when there is no editable text to expose
    // the content URL and InputScope properties.
    RefPtr<IUnknown> emptyContextCompartment;
    HRESULT hr = threadMgr->QueryInterface(
        GUID_COMPARTMENT_EMPTYCONTEXT, getter_AddRefs(emptyContextCompartment));
    return SUCCEEDED(hr);
  };

  // Create context and add it to document manager
  RefPtr<ITfContext> context;
  hr = documentMgr->CreateContext(
      TSFUtils::ClientId(), 0,
      EmptyContextIsSupported() ? static_cast<ITextStoreACP*>(this) : nullptr,
      getter_AddRefs(context), &mEditCookie);
  if (NS_WARN_IF(FAILED(hr))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFEmptyTextStore::Init() FAILED to create the context "
             "(0x%08lX)",
             this, hr));
    return false;
  }
  if (NS_WARN_IF(mDestroyed)) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFEmptyTextStore::Init() FAILED to create ITfContext due to "
         "TextStore being destroyed during calling "
         "ITfDocumentMgr::CreateContext()",
         this));
    return false;
  }

  // Make the context for this disabled and empty.
  TSFUtils::MarkContextAsKeyboardDisabled(context);
  TSFUtils::MarkContextAsEmpty(context);

  hr = documentMgr->Push(context);
  if (NS_WARN_IF(FAILED(hr))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFEmptyTextStore::Init() FAILED to push the context "
             "(0x%08lX)",
             this, hr));
    return false;
  }
  if (NS_WARN_IF(mDestroyed)) {
    MOZ_LOG(
        gIMELog, LogLevel::Error,
        ("0x%p   TSFEmptyTextStore::Init() FAILED to create ITfContext due to "
         "TextStore being destroyed during calling ITfDocumentMgr::Push()",
         this));
    documentMgr->Pop(TF_POPF_ALL);
    return false;
  }

  mDocumentMgr = documentMgr;
  mContext = context;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFEmptyTextStore::Init() succeeded: "
           "mDocumentMgr=0x%p, mContext=0x%p, mEditCookie=0x%08lX",
           this, mDocumentMgr.get(), mContext.get(), mEditCookie));

  return true;
}

void TSFEmptyTextStore::Destroy() {
  if (mBeingDestroyed) {
    return;
  }

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFEmptyTextStore::Destroy(), mLock=%s", this,
           AutoLockFlagsCString(mLock).get()));

  mDestroyed = true;

  if (mLock) {
    mPendingDestroy = true;
    return;
  }

  AutoRestore<bool> savedBeingDestroyed(mBeingDestroyed);
  mBeingDestroyed = true;

  ReleaseTSFObjects();
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFEmptyTextStore::Destroy() succeeded", this));
}

void TSFEmptyTextStore::ReleaseTSFObjects() {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFEmptyTextStore::ReleaseTSFObjects()", this));

  mDocumentURL.Truncate();
  mContext = nullptr;
  if (const RefPtr<ITfDocumentMgr> documentMgr = mDocumentMgr.forget()) {
    documentMgr->Pop(TF_POPF_ALL);
  }
  MOZ_ASSERT(!mDocumentMgr);
  mSink = nullptr;
  mWidget = nullptr;
  mDispatcher = nullptr;

  MOZ_LOG(gIMELog, LogLevel::Debug,
          ("0x%p   TSFEmptyTextStore::ReleaseTSFObjects() completed", this));
}

STDMETHODIMP TSFEmptyTextStore::QueryInterface(REFIID riid, void** ppv) {
  HRESULT hr = TSFTextStoreBase::QueryInterface(riid, ppv);
  if (*ppv) {
    return hr;
  }
  MOZ_ASSERT(riid != IID_IUnknown);
  MOZ_ASSERT(riid != IID_ITextStoreACP);
  MOZ_LOG(gIMELog, LogLevel::Error,
          ("0x%p TSFEmptyTextStore::QueryInterface() FAILED, riid=%s", this,
           AutoRiidCString(riid).get()));
  return E_NOINTERFACE;
}

STDMETHODIMP TSFEmptyTextStore::QueryInsert(LONG acpTestStart, LONG acpTestEnd,
                                            ULONG cch, LONG* pacpResultStart,
                                            LONG* pacpResultEnd) {
  HRESULT hr = TSFTextStoreBase::QueryInsert(acpTestStart, acpTestEnd, cch,
                                             pacpResultStart, pacpResultEnd);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  if (acpTestStart || acpTestEnd) {
    MOZ_LOG(gIMELog, LogLevel::Info,
            ("0x%p  TSFEmptyTextStore::QueryInsert() FAILED due to non-zero "
             "offsets",
             this));
    return E_INVALIDARG;
  }

  *pacpResultStart = *pacpResultEnd = 0;

  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p  TSFEmptyTextStore::QueryInsert() succeeded: "
           "*pacpResultStart=0, *pacpResultEnd=0)",
           this));
  return S_OK;
}

STDMETHODIMP TSFEmptyTextStore::GetSelection(ULONG ulIndex, ULONG ulCount,
                                             TS_SELECTION_ACP* pSelection,
                                             ULONG* pcFetched) {
  HRESULT hr =
      TSFTextStoreBase::GetSelection(ulIndex, ulCount, pSelection, pcFetched);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  // XXX Should we treat selection as collapsed at the start?
  *pSelection = TSFUtils::EmptySelectionACP();
  *pcFetched = 0;
  return TS_E_NOSELECTION;
}

STDMETHODIMP TSFEmptyTextStore::SetSelection(
    ULONG ulCount, const TS_SELECTION_ACP* pSelection) {
  HRESULT hr = TSFTextStoreBase::SetSelection(ulCount, pSelection);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  if (ulCount == 1 && !pSelection->acpStart && !pSelection->acpEnd) {
    return S_OK;
  }

  MOZ_LOG(gIMELog, LogLevel::Error,
          ("0x%p   TSFEmptyTextStore::SetSelection() FAILED", this));
  return TF_E_INVALIDPOS;
}

STDMETHODIMP TSFEmptyTextStore::GetText(LONG acpStart, LONG acpEnd,
                                        WCHAR* pchPlain, ULONG cchPlainReq,
                                        ULONG* pcchPlainOut,
                                        TS_RUNINFO* prgRunInfo,
                                        ULONG ulRunInfoReq,
                                        ULONG* pulRunInfoOut, LONG* pacpNext) {
  MOZ_LOG(gIMELog, LogLevel::Info, ("0x%p TSFEmptyTextStore::GetText()", this));

  HRESULT hr = TSFTextStoreBase::GetText(acpStart, acpEnd, pchPlain,
                                         cchPlainReq, pcchPlainOut, prgRunInfo,
                                         ulRunInfoReq, pulRunInfoOut, pacpNext);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  if (acpStart || acpEnd > 0) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFEmptyTextStore::GetText() FAILED due to invalid offset",
             this));
    return TS_E_INVALIDPOS;
  }
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFEmptyTextStore::GetText() succeeded: pcchPlainOut=0x%p, "
           "*prgRunInfo={ uCount=%lu, type=%s }, *pulRunInfoOut=%lu, "
           "*pacpNext=%ld)",
           this, pcchPlainOut, prgRunInfo ? prgRunInfo->uCount : 0,
           prgRunInfo ? mozilla::ToString(prgRunInfo->type).c_str() : "N/A",
           pulRunInfoOut ? *pulRunInfoOut : 0, pacpNext ? *pacpNext : 0));
  return S_OK;
}

STDMETHODIMP TSFEmptyTextStore::SetText(DWORD dwFlags, LONG acpStart,
                                        LONG acpEnd, const WCHAR* pchText,
                                        ULONG cch, TS_TEXTCHANGE* pChange) {
  MOZ_LOG(gIMELog, LogLevel::Info, ("0x%p TSFEmptyTextStore::GetText()", this));

  HRESULT hr = TSFTextStoreBase::SetText(dwFlags, acpStart, acpEnd, pchText,
                                         cch, pChange);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }
  MOZ_LOG(gIMELog, LogLevel::Error,
          ("0x%p   TSFEmptyTextStore::SetText() FAILED due to readonly", this));
  return TS_E_READONLY;
}

STDMETHODIMP TSFEmptyTextStore::RequestSupportedAttrs(
    DWORD dwFlags, ULONG cFilterAttrs, const TS_ATTRID* paFilterAttrs) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFEmptyTextStore::RequestSupportedAttrs(dwFlags=%s, "
           "cFilterAttrs=%lu)",
           this, AutoFindFlagsCString(dwFlags).get(), cFilterAttrs));

  return HandleRequestAttrs(
      dwFlags, cFilterAttrs, paFilterAttrs,
      TSFUtils::NUM_OF_SUPPORTED_ATTRS_IN_EMPTY_TEXT_STORE);
}

STDMETHODIMP TSFEmptyTextStore::RequestAttrsAtPosition(
    LONG acpPos, ULONG cFilterAttrs, const TS_ATTRID* paFilterAttrs,
    DWORD dwFlags) {
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p TSFEmptyTextStore::RequestAttrsAtPosition(acpPos=%ld, "
           "cFilterAttrs=%lu, dwFlags=%s)",
           this, acpPos, cFilterAttrs, AutoFindFlagsCString(dwFlags).get()));

  return HandleRequestAttrs(
      dwFlags | TS_ATTR_FIND_WANT_VALUE, cFilterAttrs, paFilterAttrs,
      TSFUtils::NUM_OF_SUPPORTED_ATTRS_IN_EMPTY_TEXT_STORE);
}

STDMETHODIMP TSFEmptyTextStore::RetrieveRequestedAttrs(ULONG ulCount,
                                                       TS_ATTRVAL* paAttrVals,
                                                       ULONG* pcFetched) {
  HRESULT hr = RetrieveRequestedAttrsInternal(
      ulCount, paAttrVals, pcFetched,
      TSFUtils::NUM_OF_SUPPORTED_ATTRS_IN_EMPTY_TEXT_STORE);
  if (FAILED(hr)) {
    return hr;
  }
  if (*pcFetched) {
    return S_OK;
  }
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFEmptyTextStore::RetrieveRequestedAttrs() called "
           "for unknown TS_ATTRVAL, *pcFetched=0 (S_OK)",
           this));
  return S_OK;
}

STDMETHODIMP TSFEmptyTextStore::GetEndACP(LONG* pacp) {
  HRESULT hr = TSFTextStoreBase::GetEndACP(pacp);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }
  *pacp = 0;
  return S_OK;
}

STDMETHODIMP TSFEmptyTextStore::GetACPFromPoint(TsViewCookie vcView,
                                                const POINT* pt, DWORD dwFlags,
                                                LONG* pacp) {
  HRESULT hr = TSFTextStoreBase::GetACPFromPoint(vcView, pt, dwFlags, pacp);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  *pacp = 0;
  MOZ_LOG(gIMELog, LogLevel::Info,
          ("0x%p   TSFEmptyTextStore::GetACPFromPoint() succeeded: *pacp=%ld",
           this, *pacp));
  return S_OK;
}

STDMETHODIMP TSFEmptyTextStore::GetTextExt(TsViewCookie vcView, LONG acpStart,
                                           LONG acpEnd, RECT* prc,
                                           BOOL* pfClipped) {
  HRESULT hr =
      TSFTextStoreBase::GetTextExt(vcView, acpStart, acpEnd, prc, pfClipped);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  if (acpStart || acpEnd) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("0x%p   TSFEmptyTextStore::GetTextExt(), FAILED due to invalid "
             "offset",
             this));
    return TS_E_INVALIDPOS;
  }

  prc->left = prc->top = prc->right = prc->bottom = 0;
  return S_OK;
}

STDMETHODIMP TSFEmptyTextStore::InsertTextAtSelection(
    DWORD dwFlags, const WCHAR* pchText, ULONG cch, LONG* pacpStart,
    LONG* pacpEnd, TS_TEXTCHANGE* pChange) {
  HRESULT hr = TSFTextStoreBase::InsertTextAtSelection(
      dwFlags, pchText, cch, pacpStart, pacpEnd, pChange);
  if (MOZ_UNLIKELY(hr != E_NOTIMPL)) {
    return hr;
  }

  MOZ_LOG(gIMELog, LogLevel::Error,
          ("0x%p   TSFEmptyTextStore::InsertTextAtSelection() FAILED due to "
           "readonly",
           this));
  return TS_E_READONLY;
}

// static
Result<RefPtr<TSFEmptyTextStore>, nsresult>
TSFEmptyTextStore::CreateAndSetFocus(nsWindow* aFocusedWindow,
                                     const InputContext& aContext) {
  const RefPtr<TSFEmptyTextStore> textStore = new TSFEmptyTextStore();
  if (NS_WARN_IF(!textStore->Init(aFocusedWindow, aContext))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFEmptyTextStore::CreateAndSetFocus() FAILED due to "
             "TSFEmptyTextStore::Init() failure"));
    textStore->Destroy();
    TSFUtils::ClearStoringTextStoresIf(textStore);
    return Err(NS_ERROR_FAILURE);
  }
  nsresult rv = textStore->SetFocus(aFocusedWindow, aContext);
  if (NS_FAILED(rv)) {
    textStore->Destroy();
    TSFUtils::ClearStoringTextStoresIf(textStore);
    return Err(rv);
  }
  return textStore;
}

nsresult TSFEmptyTextStore::SetFocus(nsWindow* aFocusedWindow,
                                     const InputContext& aContext) {
  const RefPtr<ITfDocumentMgr> docMgr = mDocumentMgr;
  if (NS_WARN_IF(!docMgr)) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFEmptyTextStore::SetFocus() FAILED due to "
             "invalid TSFEmptyTextStore::mDocumentMgr"));
    return NS_ERROR_FAILURE;
  }
  const RefPtr<ITfThreadMgr> threadMgr = TSFUtils::GetThreadMgr();
  if (NS_WARN_IF(FAILED(threadMgr->SetFocus(docMgr)))) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFEmptyTextStore::SetFocus() FAILED due to "
             "ITfTheadMgr::SetFocus() failure"));
    return NS_ERROR_FAILURE;
  }
  if (NS_WARN_IF(!TSFUtils::GetThreadMgr())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFEmptyTextStore::SetFocus() FAILED due to "
             "sThreadMgr being destroyed during calling "
             "ITfTheadMgr::SetFocus()"));
    return NS_ERROR_FAILURE;
  }
  if (NS_WARN_IF(TSFUtils::GetCurrentTextStore())) {
    MOZ_LOG(gIMELog, LogLevel::Error,
            ("  TSFEmptyTextStore::SetFocus() FAILED due to "
             "creating TextStore has lost focus during calling "
             "ITfThreadMgr::SetFocus()"));
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

nsresult TSFEmptyTextStore::SetFocusAndUpdateDocumentURLAndBrowsingMode(
    nsWindow* aFocusedWindow, const InputContext& aContext) {
  if (NS_WARN_IF(NS_FAILED(
          UpdateDocumentURLAndBrowsingMode(aFocusedWindow, aContext)))) {
    return NS_ERROR_FAILURE;
  }
  if (NS_WARN_IF(NS_FAILED(SetFocus(aFocusedWindow, aContext)))) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

IMENotificationRequests TSFEmptyTextStore::GetIMENotificationRequests() const {
  return IMENotificationRequests();
}

}  // namespace mozilla::widget
