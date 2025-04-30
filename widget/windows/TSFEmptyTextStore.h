/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TSFEmptyTextStore_h
#define TSFEmptyTextStore_h

#include "nsIWidget.h"
#include "nsWindow.h"

#include "TSFTextStoreBase.h"
#include "TSFUtils.h"
#include "WinUtils.h"
#include "WritingModes.h"

#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TextEventDispatcher.h"
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

class TSFEmptyTextStore final : public TSFTextStoreBase {
  friend class TSFStaticSink;

 public: /*IUnknown*/
  STDMETHODIMP QueryInterface(REFIID, void**);

  NS_INLINE_DECL_IUNKNOWN_ADDREF_RELEASE(TSFEmptyTextStore);

 public: /*ITextStoreACP*/
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

 public:
  [[nodiscard]] IMENotificationRequests GetIMENotificationRequests()
      const final;

  void Destroy() final;

  [[nodiscard]] static Result<RefPtr<TSFEmptyTextStore>, nsresult>
  CreateAndSetFocus(nsWindow* aFocusedWindow, const InputContext& aContext);

  nsresult SetFocusAndUpdateDocumentURLAndBrowsingMode(
      nsWindow* aFocusedWindow, const InputContext& aContext);

 protected:
  TSFEmptyTextStore();
  virtual ~TSFEmptyTextStore();

  bool Init(nsWindow* aWidget, const InputContext& aContext);
  void ReleaseTSFObjects();

  nsresult SetFocus(nsWindow* aFocusedWindow, const InputContext& aContext);

  // This is called immediately after a call of OnLockGranted() of mSink.
  // Note that mLock isn't cleared yet when this is called.
  void DidLockGranted() final {
    mDeferNotifyingTSF = mDeferNotifyingTSFUntilNextUpdate = false;
  }
};

}  // namespace mozilla::widget

#endif  // #ifndef TSFEmptyTextStore_h
