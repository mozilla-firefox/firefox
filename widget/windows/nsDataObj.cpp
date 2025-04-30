/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"
#include "mozilla/TextUtils.h"

#include <ole2.h>
#include <shlobj.h>

#include "nsComponentManagerUtils.h"
#include "nsDataObj.h"
#include "nsArrayUtils.h"
#include "nsClipboard.h"
#include "nsReadableUtils.h"
#include "nsICookieJarSettings.h"
#include "nsIHttpChannel.h"
#include "nsISupportsPrimitives.h"
#include "nsITransferable.h"
#include "IEnumFE.h"
#include "nsPrimitiveHelpers.h"
#include "nsString.h"
#include "nsCRT.h"
#include "nsPrintfCString.h"
#include "nsIStringBundle.h"
#include "nsEscape.h"
#include "nsIURL.h"
#include "nsNetUtil.h"
#include "mozilla/Components.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPrefs_clipboard.h"
#include "mozilla/Unused.h"
#include "nsProxyRelease.h"
#include "nsIObserverService.h"
#include "nsIOutputStream.h"
#include "nscore.h"
#include "nsDirectoryServiceDefs.h"
#include "nsITimer.h"
#include "nsThreadUtils.h"
#include "mozilla/Preferences.h"
#include "nsContentUtils.h"
#include "nsIPrincipal.h"
#include "nsNativeCharsetUtils.h"
#include "nsMimeTypes.h"
#include "nsIMIMEService.h"
#include "imgIEncoder.h"
#include "imgITools.h"
#include "WinOLELock.h"
#include "WinUtils.h"
#include "nsLocalFile.h"

#include "mozilla/LazyIdleThread.h"
#include <algorithm>

using namespace mozilla;
using namespace mozilla::glue;
using namespace mozilla::widget;

#define BFH_LENGTH 14
#define DEFAULT_THREAD_TIMEOUT_MS 30000

//-----------------------------------------------------------------------------
// CStreamBase implementation
nsDataObj::CStreamBase::CStreamBase() : mStreamRead(0) {}

//-----------------------------------------------------------------------------
nsDataObj::CStreamBase::~CStreamBase() {}

NS_IMPL_ISUPPORTS(nsDataObj::CStream, nsIStreamListener)

//-----------------------------------------------------------------------------
// CStream implementation
nsDataObj::CStream::CStream() : mChannelRead(false) {}

//-----------------------------------------------------------------------------
nsDataObj::CStream::~CStream() {}

//-----------------------------------------------------------------------------
// helper - initializes the stream
nsresult nsDataObj::CStream::Init(nsIURI* pSourceURI,
                                  nsContentPolicyType aContentPolicyType,
                                  nsIPrincipal* aRequestingPrincipal,
                                  nsICookieJarSettings* aCookieJarSettings,
                                  nsIReferrerInfo* aReferrerInfo) {
  // we can not create a channel without a requestingPrincipal
  if (!aRequestingPrincipal) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv;
  rv = NS_NewChannel(getter_AddRefs(mChannel), pSourceURI, aRequestingPrincipal,
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT,
                     aContentPolicyType, aCookieJarSettings,
                     nullptr,  // PerformanceStorage
                     nullptr,  // loadGroup
                     nullptr,  // aCallbacks
                     nsIRequest::LOAD_FROM_CACHE);
  NS_ENSURE_SUCCESS(rv, rv);

  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel)) {
    rv = httpChannel->SetReferrerInfo(aReferrerInfo);
    Unused << NS_WARN_IF(NS_FAILED(rv));
  }

  // Do not HTTPS-Only/-First upgrade this request. If we reach this point, any
  // potential upgrades should have already happened, or the URI may have
  // already been exempt.
  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
  loadInfo->SetHttpsOnlyStatus(nsILoadInfo::HTTPS_ONLY_EXEMPT);

  rv = mChannel->AsyncOpen(this);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// IUnknown's QueryInterface, nsISupport's AddRef and Release are shared by
// IUnknown and nsIStreamListener.
STDMETHODIMP nsDataObj::CStream::QueryInterface(REFIID refiid,
                                                void** ppvResult) {
  *ppvResult = nullptr;
  if (IID_IUnknown == refiid || refiid == IID_IStream)

  {
    *ppvResult = this;
  }

  if (nullptr != *ppvResult) {
    ((LPUNKNOWN)*ppvResult)->AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

// nsIStreamListener implementation
NS_IMETHODIMP
nsDataObj::CStream::OnDataAvailable(
    nsIRequest* aRequest, nsIInputStream* aInputStream,
    uint64_t aOffset,  // offset within the stream
    uint32_t aCount)   // bytes available on this call
{
  // If we've been asked to read zero bytes, call `Read` once, just to ensure
  // any side-effects take place, and return immediately.
  if (aCount == 0) {
    char buffer[1] = {0};
    uint32_t bytesReadByCall = 0;
    nsresult rv = aInputStream->Read(buffer, 0, &bytesReadByCall);
    MOZ_ASSERT(bytesReadByCall == 0);
    return rv;
  }

  // Extend the write buffer for the incoming data.
  size_t oldLength = mChannelData.Length();
  char* buffer =
      reinterpret_cast<char*>(mChannelData.AppendElements(aCount, fallible));
  if (!buffer) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  MOZ_ASSERT(mChannelData.Length() == (aOffset + aCount),
             "stream length mismatch w/write buffer");

  // Read() may not return aCount on a single call, so loop until we've
  // accumulated all the data OnDataAvailable has promised.
  uint32_t bytesRead = 0;
  while (bytesRead < aCount) {
    uint32_t bytesReadByCall = 0;
    nsresult rv = aInputStream->Read(buffer + bytesRead, aCount - bytesRead,
                                     &bytesReadByCall);
    bytesRead += bytesReadByCall;

    if (bytesReadByCall == 0) {
      // A `bytesReadByCall` of zero indicates EOF without failure... but we
      // were promised `aCount` elements and haven't gotten them. Return a
      // generic failure.
      rv = NS_ERROR_FAILURE;
    }

    if (NS_FAILED(rv)) {
      // Drop any trailing uninitialized elements before erroring out.
      mChannelData.RemoveElementsAt(oldLength + bytesRead, aCount - bytesRead);
      return rv;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP nsDataObj::CStream::OnStartRequest(nsIRequest* aRequest) {
  mChannelResult = NS_OK;
  return NS_OK;
}

NS_IMETHODIMP nsDataObj::CStream::OnStopRequest(nsIRequest* aRequest,
                                                nsresult aStatusCode) {
  mChannelRead = true;
  mChannelResult = aStatusCode;
  return NS_OK;
}

// Pumps thread messages while waiting for the async listener operation to
// complete. Failing this call will fail the stream incall from Windows
// and cancel the operation.
nsresult nsDataObj::CStream::WaitForCompletion() {
  // We are guaranteed OnStopRequest will get called, so this should be ok.
  SpinEventLoopUntil("widget:nsDataObj::CStream::WaitForCompletion"_ns,
                     [&]() { return mChannelRead; });

  if (!mChannelData.Length()) mChannelResult = NS_ERROR_FAILURE;

  return mChannelResult;
}

//-----------------------------------------------------------------------------
// IStream
STDMETHODIMP nsDataObj::CStreamBase::Clone(IStream** ppStream) {
  return E_NOTIMPL;
}

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStreamBase::Commit(DWORD dwFrags) { return E_NOTIMPL; }

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStreamBase::CopyTo(IStream* pDestStream,
                                            ULARGE_INTEGER nBytesToCopy,
                                            ULARGE_INTEGER* nBytesRead,
                                            ULARGE_INTEGER* nBytesWritten) {
  return E_NOTIMPL;
}

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStreamBase::LockRegion(ULARGE_INTEGER nStart,
                                                ULARGE_INTEGER nBytes,
                                                DWORD dwFlags) {
  return E_NOTIMPL;
}

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStream::Read(void* pvBuffer, ULONG nBytesToRead,
                                      ULONG* nBytesRead) {
  // Wait for the write into our buffer to complete via the stream listener.
  // We can't respond to this by saying "call us back later".
  if (NS_FAILED(WaitForCompletion())) return E_FAIL;

  // Bytes left for Windows to read out of our buffer
  ULONG bytesLeft = mChannelData.Length() - mStreamRead;
  // Let Windows know what we will hand back, usually this is the entire buffer
  *nBytesRead = std::min(bytesLeft, nBytesToRead);
  // Copy the buffer data over
  memcpy(pvBuffer, ((char*)mChannelData.Elements() + mStreamRead), *nBytesRead);
  // Update our bytes read tracking
  mStreamRead += *nBytesRead;
  return S_OK;
}

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStreamBase::Revert(void) { return E_NOTIMPL; }

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStreamBase::Seek(LARGE_INTEGER nMove, DWORD dwOrigin,
                                          ULARGE_INTEGER* nNewPos) {
  if (nNewPos == nullptr) return STG_E_INVALIDPOINTER;

  if (nMove.LowPart == 0 && nMove.HighPart == 0 &&
      (dwOrigin == STREAM_SEEK_SET || dwOrigin == STREAM_SEEK_CUR)) {
    nNewPos->LowPart = 0;
    nNewPos->HighPart = 0;
    return S_OK;
  }

  return E_NOTIMPL;
}

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStreamBase::SetSize(ULARGE_INTEGER nNewSize) {
  return E_NOTIMPL;
}

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStream::Stat(STATSTG* statstg, DWORD dwFlags) {
  if (statstg == nullptr) return STG_E_INVALIDPOINTER;

  if (!mChannel || NS_FAILED(WaitForCompletion())) return E_FAIL;

  memset((void*)statstg, 0, sizeof(STATSTG));

  if (dwFlags != STATFLAG_NONAME) {
    nsCOMPtr<nsIURI> sourceURI;
    if (NS_FAILED(mChannel->GetURI(getter_AddRefs(sourceURI)))) {
      return E_FAIL;
    }

    nsAutoCString strFileName;
    nsCOMPtr<nsIURL> sourceURL = do_QueryInterface(sourceURI);
    sourceURL->GetFileName(strFileName);

    if (strFileName.IsEmpty()) return E_FAIL;

    NS_UnescapeURL(strFileName);
    NS_ConvertUTF8toUTF16 wideFileName(strFileName);

    uint32_t nMaxNameLength = (wideFileName.Length() * 2) + 2;
    void* retBuf = CoTaskMemAlloc(nMaxNameLength);  // freed by caller
    if (!retBuf) return STG_E_INSUFFICIENTMEMORY;

    ZeroMemory(retBuf, nMaxNameLength);
    memcpy(retBuf, wideFileName.get(), wideFileName.Length() * 2);
    statstg->pwcsName = (LPOLESTR)retBuf;
  }

  SYSTEMTIME st;

  statstg->type = STGTY_STREAM;

  GetSystemTime(&st);
  SystemTimeToFileTime((const SYSTEMTIME*)&st, (LPFILETIME)&statstg->mtime);
  statstg->ctime = statstg->atime = statstg->mtime;

  statstg->cbSize.QuadPart = mChannelData.Length();
  statstg->grfMode = STGM_READ;
  statstg->grfLocksSupported = LOCK_ONLYONCE;
  statstg->clsid = CLSID_NULL;

  return S_OK;
}

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStreamBase::UnlockRegion(ULARGE_INTEGER nStart,
                                                  ULARGE_INTEGER nBytes,
                                                  DWORD dwFlags) {
  return E_NOTIMPL;
}

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CStreamBase::Write(const void* pvBuffer,
                                           ULONG nBytesToRead,
                                           ULONG* nBytesRead) {
  return E_NOTIMPL;
}

//-----------------------------------------------------------------------------
HRESULT nsDataObj::CreateStream(IStream** outStream) {
  NS_ENSURE_TRUE(outStream, E_INVALIDARG);

  nsresult rv = NS_ERROR_FAILURE;
  nsAutoString wideFileName;
  nsCOMPtr<nsIURI> sourceURI;
  HRESULT res;

  res = GetDownloadDetails(getter_AddRefs(sourceURI), wideFileName);
  if (FAILED(res)) return res;

  nsDataObj::CStream* pStream = new nsDataObj::CStream();
  NS_ENSURE_TRUE(pStream, E_OUTOFMEMORY);

  pStream->AddRef();

  // query the dataPrincipal from the transferable and add it to the new
  // channel.
  nsCOMPtr<nsIPrincipal> requestingPrincipal =
      mTransferable->GetDataPrincipal();
  MOZ_ASSERT(requestingPrincipal, "can not create channel without a principal");

  // Note that the cookieJarSettings could be null if the data object is for the
  // image copy. We will fix this in Bug 1690532.
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      mTransferable->GetCookieJarSettings();

  // The referrer is optional.
  nsCOMPtr<nsIReferrerInfo> referrerInfo = mTransferable->GetReferrerInfo();

  nsContentPolicyType contentPolicyType = mTransferable->GetContentPolicyType();
  rv = pStream->Init(sourceURI, contentPolicyType, requestingPrincipal,
                     cookieJarSettings, referrerInfo);
  if (NS_FAILED(rv)) {
    pStream->Release();
    return E_FAIL;
  }
  *outStream = pStream;

  return S_OK;
}

//-----------------------------------------------------------------------------
// AutoCloseEvent implementation
nsDataObj::AutoCloseEvent::AutoCloseEvent()
    : mEvent(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

bool nsDataObj::AutoCloseEvent::IsInited() const { return !!mEvent; }

void nsDataObj::AutoCloseEvent::Signal() const { ::SetEvent(mEvent); }

DWORD nsDataObj::AutoCloseEvent::Wait(DWORD aMillisec) const {
  return ::WaitForSingleObject(mEvent, aMillisec);
}

//-----------------------------------------------------------------------------
// AutoSetEvent implementation
nsDataObj::AutoSetEvent::AutoSetEvent(NotNull<AutoCloseEvent*> aEvent)
    : mEvent(aEvent) {}

nsDataObj::AutoSetEvent::~AutoSetEvent() { Signal(); }

void nsDataObj::AutoSetEvent::Signal() const { mEvent->Signal(); }

bool nsDataObj::AutoSetEvent::IsWaiting() const {
  return mEvent->Wait(0) == WAIT_TIMEOUT;
}

//-----------------------------------------------------------------------------
// CMemStream implementation
Win32SRWLock nsDataObj::CMemStream::mLock;

//-----------------------------------------------------------------------------
nsDataObj::CMemStream::CMemStream(nsHGLOBAL aGlobalMem, uint32_t aTotalLength,
                                  already_AddRefed<AutoCloseEvent> aEvent)
    : mGlobalMem(aGlobalMem), mEvent(aEvent), mTotalLength(aTotalLength) {
  ::CoCreateFreeThreadedMarshaler(this, getter_AddRefs(mMarshaler));
}

//-----------------------------------------------------------------------------
nsDataObj::CMemStream::~CMemStream() {}

//-----------------------------------------------------------------------------
// IUnknown
STDMETHODIMP nsDataObj::CMemStream::QueryInterface(REFIID refiid,
                                                   void** ppvResult) {
  *ppvResult = nullptr;
  if (refiid == IID_IUnknown || refiid == IID_IStream ||
      refiid == IID_IAgileObject) {
    *ppvResult = this;
  } else if (refiid == IID_IMarshal && mMarshaler) {
    return mMarshaler->QueryInterface(refiid, ppvResult);
  }

  if (nullptr != *ppvResult) {
    ((LPUNKNOWN)*ppvResult)->AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

void nsDataObj::CMemStream::WaitForCompletion() {
  if (!mEvent) {
    // We are not waiting for obtaining the icon cache.
    return;
  }
  if (!NS_IsMainThread()) {
    mEvent->Wait(INFINITE);
  } else {
    // We should not block the main thread.
    mEvent->Signal();
  }
  // mEvent will always be in the signaled state here.
}

//-----------------------------------------------------------------------------
// IStream
STDMETHODIMP nsDataObj::CMemStream::Read(void* pvBuffer, ULONG nBytesToRead,
                                         ULONG* nBytesRead) {
  // Wait until the event is signaled.
  WaitForCompletion();

  AutoExclusiveLock lock(mLock);
  char* contents = reinterpret_cast<char*>(GlobalLock(mGlobalMem.get()));
  if (!contents) {
    return E_OUTOFMEMORY;
  }

  // Bytes left for Windows to read out of our buffer
  ULONG bytesLeft = mTotalLength - mStreamRead;
  // Let Windows know what we will hand back, usually this is the entire buffer
  *nBytesRead = std::min(bytesLeft, nBytesToRead);
  // Copy the buffer data over
  memcpy(pvBuffer, contents + mStreamRead, *nBytesRead);
  // Update our bytes read tracking
  mStreamRead += *nBytesRead;

  GlobalUnlock(mGlobalMem.get());
  return S_OK;
}

//-----------------------------------------------------------------------------
STDMETHODIMP nsDataObj::CMemStream::Stat(STATSTG* statstg, DWORD dwFlags) {
  if (statstg == nullptr) return STG_E_INVALIDPOINTER;

  memset((void*)statstg, 0, sizeof(STATSTG));

  if (dwFlags != STATFLAG_NONAME) {
    constexpr size_t kMaxNameLength = sizeof(wchar_t);
    void* retBuf = CoTaskMemAlloc(kMaxNameLength);  // freed by caller
    if (!retBuf) return STG_E_INSUFFICIENTMEMORY;

    ZeroMemory(retBuf, kMaxNameLength);
    statstg->pwcsName = (LPOLESTR)retBuf;
  }

  SYSTEMTIME st;

  statstg->type = STGTY_STREAM;

  GetSystemTime(&st);
  SystemTimeToFileTime((const SYSTEMTIME*)&st, (LPFILETIME)&statstg->mtime);
  statstg->ctime = statstg->atime = statstg->mtime;

  statstg->cbSize.QuadPart = mTotalLength;
  statstg->grfMode = STGM_READ;
  statstg->grfLocksSupported = LOCK_ONLYONCE;
  statstg->clsid = CLSID_NULL;

  return S_OK;
}

/*
 * Class nsDataObj
 */

//-----------------------------------------------------
// construction
//-----------------------------------------------------
nsDataObj::nsDataObj(nsIURI* uri)
    : m_cRef(0),
      mTransferable(nullptr),
      mIsAsyncMode(FALSE),
      mIsInOperation(FALSE) {
  mIOThread = new LazyIdleThread(DEFAULT_THREAD_TIMEOUT_MS, "nsDataObj",
                                 LazyIdleThread::ManualShutdown);
  m_enumFE = new CEnumFormatEtc();
  m_enumFE->AddRef();

  if (uri) {
    // A URI was obtained, so pass this through to the DataObject
    // so it can create a SourceURL for CF_HTML flavour
    uri->GetSpec(mSourceURL);
  }
}
//-----------------------------------------------------
// destruction
//-----------------------------------------------------
nsDataObj::~nsDataObj() {
  mTransferable = nullptr;

  mDataFlavors.Clear();

  m_enumFE->Release();

  // Free arbitrary system formats
  for (uint32_t idx = 0; idx < mDataEntryList.Length(); idx++) {
    CoTaskMemFree(mDataEntryList[idx]->fe.ptd);
    ReleaseStgMedium(&mDataEntryList[idx]->stgm);
    CoTaskMemFree(mDataEntryList[idx]);
  }
}

//-----------------------------------------------------
// IUnknown interface methods - see inknown.h for documentation
//-----------------------------------------------------
STDMETHODIMP nsDataObj::QueryInterface(REFIID riid, void** ppv) {
  *ppv = nullptr;

  if ((IID_IUnknown == riid) || (IID_IDataObject == riid)) {
    *ppv = this;
    AddRef();
    return S_OK;
  } else if (IID_IDataObjectAsyncCapability == riid) {
    *ppv = static_cast<IDataObjectAsyncCapability*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

//-----------------------------------------------------
STDMETHODIMP_(ULONG) nsDataObj::AddRef() {
  ++m_cRef;
  NS_LOG_ADDREF(this, m_cRef, "nsDataObj", sizeof(*this));

  // When the first reference is taken, hold our own internal reference.
  if (m_cRef == 1) {
    mKeepAlive = this;
  }

  return m_cRef;
}

namespace {
class RemoveTempFileHelper final : public nsIObserver, public nsINamed {
 public:
  explicit RemoveTempFileHelper(nsIFile* aTempFile) : mTempFile(aTempFile) {
    MOZ_ASSERT(mTempFile);
  }

  // The attach method is seperate from the constructor as we may be addref-ing
  // ourself, and we want to be sure someone has a strong reference to us.
  void Attach() {
    // We need to listen to both the xpcom shutdown message and our timer, and
    // fire when the first of either of these two messages is received.
    nsresult rv;
    rv = NS_NewTimerWithObserver(getter_AddRefs(mTimer), this, 500,
                                 nsITimer::TYPE_ONE_SHOT);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    nsCOMPtr<nsIObserverService> observerService =
        do_GetService("@mozilla.org/observer-service;1");
    if (NS_WARN_IF(!observerService)) {
      mTimer->Cancel();
      mTimer = nullptr;
      return;
    }
    observerService->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSINAMED

 private:
  ~RemoveTempFileHelper() {
    if (mTempFile) {
      mTempFile->Remove(false);
    }
  }

  nsCOMPtr<nsIFile> mTempFile;
  nsCOMPtr<nsITimer> mTimer;
};

NS_IMPL_ISUPPORTS(RemoveTempFileHelper, nsIObserver, nsINamed);

NS_IMETHODIMP
RemoveTempFileHelper::Observe(nsISupports* aSubject, const char* aTopic,
                              const char16_t* aData) {
  // Let's be careful and make sure that we don't die immediately
  RefPtr<RemoveTempFileHelper> grip = this;

  // Make sure that we aren't called again by destroying references to ourself.
  nsCOMPtr<nsIObserverService> observerService =
      do_GetService("@mozilla.org/observer-service;1");
  if (observerService) {
    observerService->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
  }

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }

  // Remove the tempfile
  if (mTempFile) {
    mTempFile->Remove(false);
    mTempFile = nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP
RemoveTempFileHelper::GetName(nsACString& aName) {
  aName.AssignLiteral("RemoveTempFileHelper");
  return NS_OK;
}
}  // namespace

//-----------------------------------------------------
STDMETHODIMP_(ULONG) nsDataObj::Release() {
  --m_cRef;

  NS_LOG_RELEASE(this, m_cRef, "nsDataObj");

  // If we hold the last reference, submit release of it to the main thread.
  if (m_cRef == 1 && mKeepAlive) {
    NS_ReleaseOnMainThread("nsDataObj release", mKeepAlive.forget(), true);
  }

  if (0 != m_cRef) return m_cRef;

  // We have released our last ref on this object and need to delete the
  // temp file. External app acting as drop target may still need to open the
  // temp file. Addref a timer so it can delay deleting file and destroying
  // this object.
  if (mCachedTempFile) {
    RefPtr<RemoveTempFileHelper> helper =
        new RemoveTempFileHelper(mCachedTempFile);
    mCachedTempFile = nullptr;
    helper->Attach();
  }

  // In case the destructor ever AddRef/Releases, ensure we don't delete twice
  // or take mKeepAlive as another reference.
  m_cRef = 1;

  delete this;

  return 0;
}

//-----------------------------------------------------
BOOL nsDataObj::FormatsMatch(const FORMATETC& source,
                             const FORMATETC& target) const {
  if ((source.cfFormat == target.cfFormat) &&
      (source.dwAspect & target.dwAspect) && (source.tymed & target.tymed)) {
    return TRUE;
  } else {
    return FALSE;
  }
}

//-----------------------------------------------------
// IDataObject methods
//-----------------------------------------------------
STDMETHODIMP nsDataObj::GetData(LPFORMATETC aFormat, LPSTGMEDIUM pSTM) {
  if (!mTransferable) return DV_E_FORMATETC;

  // Hold an extra reference in case we end up spinning the event loop.
  RefPtr<nsDataObj> keepAliveDuringGetData(this);

  uint32_t dfInx = 0;

  static CLIPFORMAT fileDescriptorFlavorA =
      ::RegisterClipboardFormat(CFSTR_FILEDESCRIPTORA);
  static CLIPFORMAT fileDescriptorFlavorW =
      ::RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
  static CLIPFORMAT uniformResourceLocatorA =
      ::RegisterClipboardFormat(CFSTR_INETURLA);
  static CLIPFORMAT uniformResourceLocatorW =
      ::RegisterClipboardFormat(CFSTR_INETURLW);
  static CLIPFORMAT fileFlavor = ::RegisterClipboardFormat(CFSTR_FILECONTENTS);
  static CLIPFORMAT PreferredDropEffect =
      ::RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
  static CLIPFORMAT imagePNGFormat = ::RegisterClipboardFormat(TEXT("PNG"));

  // Arbitrary system formats are used for image feedback during drag
  // and drop. We are responsible for storing these internally during
  // drag operations.
  LPDATAENTRY pde;
  if (LookupArbitraryFormat(aFormat, &pde, FALSE)) {
    return CopyMediumData(pSTM, &pde->stgm, aFormat, FALSE) ? S_OK
                                                            : E_UNEXPECTED;
  }

  // Firefox internal formats
  ULONG count;
  FORMATETC fe;
  m_enumFE->Reset();
  while (NOERROR == m_enumFE->Next(1, &fe, &count) &&
         dfInx < mDataFlavors.Length()) {
    nsCString const& df = mDataFlavors.ElementAt(dfInx);
    if (FormatsMatch(fe, *aFormat)) {
      pSTM->pUnkForRelease =
          nullptr;  // caller is responsible for deleting this data
      CLIPFORMAT const format = aFormat->cfFormat;

      // compile-time-constant format indicators:
      switch (format) {
        // Someone is asking for plain or unicode text
        case CF_TEXT:
        case CF_UNICODETEXT:
          return GetText(df, *aFormat, *pSTM);

        // Some 3rd party apps that receive drag and drop files from the browser
        // window require support for this.
        case CF_HDROP:
          return GetFile(*aFormat, *pSTM);

        // Someone is asking for an image
        case CF_DIBV5:
        case CF_DIB:
          return GetDib(df, *aFormat, *pSTM, DibType::Bmp);

        default: /* fallthrough */;
      }  // switch

      // non-compile-time-constant format indicators:
      if (format == imagePNGFormat)
        return GetDib(df, *aFormat, *pSTM, DibType::Png);
      if (format == fileDescriptorFlavorA)
        return GetFileDescriptor(*aFormat, *pSTM, false);
      if (format == fileDescriptorFlavorW)
        return GetFileDescriptor(*aFormat, *pSTM, true);
      if (format == uniformResourceLocatorA)
        return GetUniformResourceLocator(*aFormat, *pSTM, false);
      if (format == uniformResourceLocatorW)
        return GetUniformResourceLocator(*aFormat, *pSTM, true);
      if (format == fileFlavor) return GetFileContents(*aFormat, *pSTM);
      if (format == PreferredDropEffect)
        return GetPreferredDropEffect(*aFormat, *pSTM);
      // MOZ_LOG(gWindowsLog, LogLevel::Info,
      //       ("***** nsDataObj::GetData - Unknown format %u\n", format));
      return GetText(df, *aFormat, *pSTM);
    }  // if
    dfInx++;
  }  // while

  return DATA_E_FORMATETC;
}

//-----------------------------------------------------
STDMETHODIMP nsDataObj::GetDataHere(LPFORMATETC pFE, LPSTGMEDIUM pSTM) {
  return E_FAIL;
}

//-----------------------------------------------------
// Other objects querying to see if we support a
// particular format
//-----------------------------------------------------
STDMETHODIMP nsDataObj::QueryGetData(LPFORMATETC pFE) {
  // Arbitrary system formats are used for image feedback during drag
  // and drop. We are responsible for storing these internally during
  // drag operations.
  LPDATAENTRY pde;
  if (LookupArbitraryFormat(pFE, &pde, FALSE)) return S_OK;

  // Firefox internal formats
  ULONG count;
  FORMATETC fe;
  m_enumFE->Reset();
  while (NOERROR == m_enumFE->Next(1, &fe, &count)) {
    if (fe.cfFormat == pFE->cfFormat) {
      return S_OK;
    }
  }
  return E_FAIL;
}

//-----------------------------------------------------
STDMETHODIMP nsDataObj::GetCanonicalFormatEtc(LPFORMATETC pFEIn,
                                              LPFORMATETC pFEOut) {
  return E_NOTIMPL;
}

//-----------------------------------------------------
STDMETHODIMP nsDataObj::SetData(LPFORMATETC aFormat, LPSTGMEDIUM aMedium,
                                BOOL shouldRel) {
  // Arbitrary system formats are used for image feedback during drag
  // and drop. We are responsible for storing these internally during
  // drag operations.
  LPDATAENTRY pde;
  if (LookupArbitraryFormat(aFormat, &pde, TRUE)) {
    // Release the old data the lookup handed us for this format. This
    // may have been set in CopyMediumData when we originally stored the
    // data.
    if (pde->stgm.tymed) {
      ReleaseStgMedium(&pde->stgm);
      memset(&pde->stgm, 0, sizeof(STGMEDIUM));
    }

    bool result = true;
    if (shouldRel) {
      // If shouldRel is TRUE, the data object called owns the storage medium
      // after the call returns. Store the incoming data in our data array for
      // release when we are destroyed. This is the common case with arbitrary
      // data from explorer.
      pde->stgm = *aMedium;
    } else {
      // Copy the incoming data into our data array. (AFAICT, this never gets
      // called with arbitrary formats for drag images.)
      result = CopyMediumData(&pde->stgm, aMedium, aFormat, TRUE);
    }
    pde->fe.tymed = pde->stgm.tymed;

    return result ? S_OK : DV_E_TYMED;
  }

  if (shouldRel) ReleaseStgMedium(aMedium);

  return S_OK;
}

bool nsDataObj::LookupArbitraryFormat(FORMATETC* aFormat,
                                      LPDATAENTRY* aDataEntry,
                                      BOOL aAddorUpdate) {
  *aDataEntry = nullptr;

  if (aFormat->ptd != nullptr) return false;

  // See if it's already in our list. If so return the data entry.
  for (uint32_t idx = 0; idx < mDataEntryList.Length(); idx++) {
    if (mDataEntryList[idx]->fe.cfFormat == aFormat->cfFormat &&
        mDataEntryList[idx]->fe.dwAspect == aFormat->dwAspect &&
        mDataEntryList[idx]->fe.lindex == aFormat->lindex) {
      if (aAddorUpdate || (mDataEntryList[idx]->fe.tymed & aFormat->tymed)) {
        // If the caller requests we update, or if the
        // medium type matches, return the entry.
        *aDataEntry = mDataEntryList[idx];
        return true;
      } else {
        // Medium does not match, not found.
        return false;
      }
    }
  }

  if (!aAddorUpdate) return false;

  // Add another entry to mDataEntryList
  LPDATAENTRY dataEntry = (LPDATAENTRY)CoTaskMemAlloc(sizeof(DATAENTRY));
  if (!dataEntry) return false;

  dataEntry->fe = *aFormat;
  *aDataEntry = dataEntry;
  memset(&dataEntry->stgm, 0, sizeof(STGMEDIUM));

  // Add this to our IEnumFORMATETC impl. so we can return it when
  // it's requested.
  m_enumFE->AddFormatEtc(aFormat);

  // Store a copy internally in the arbitrary formats array.
  mDataEntryList.AppendElement(dataEntry);

  return true;
}

bool nsDataObj::CopyMediumData(STGMEDIUM* aMediumDst, STGMEDIUM* aMediumSrc,
                               LPFORMATETC aFormat, BOOL aSetData) {
  STGMEDIUM stgmOut = *aMediumSrc;

  switch (stgmOut.tymed) {
    case TYMED_ISTREAM:
      stgmOut.pstm->AddRef();
      break;
    case TYMED_ISTORAGE:
      stgmOut.pstg->AddRef();
      break;
    case TYMED_HGLOBAL:
      if (!aMediumSrc->pUnkForRelease) {
        if (aSetData) {
          if (aMediumSrc->tymed != TYMED_HGLOBAL) return false;
          stgmOut.hGlobal =
              OleDuplicateData(aMediumSrc->hGlobal, aFormat->cfFormat, 0);
          if (!stgmOut.hGlobal) return false;
        } else {
          // We are returning this data from LookupArbitraryFormat, indicate to
          // the shell we hold it and will free it.
          stgmOut.pUnkForRelease = static_cast<IDataObject*>(this);
        }
      }
      break;
    default:
      return false;
  }

  if (stgmOut.pUnkForRelease) stgmOut.pUnkForRelease->AddRef();

  *aMediumDst = stgmOut;

  return true;
}

//-----------------------------------------------------
STDMETHODIMP nsDataObj::EnumFormatEtc(DWORD dwDir, LPENUMFORMATETC* ppEnum) {
  switch (dwDir) {
    case DATADIR_GET:
      m_enumFE->Clone(ppEnum);
      break;
    case DATADIR_SET:
      // fall through
    default:
      *ppEnum = nullptr;
  }  // switch

  if (nullptr == *ppEnum) return E_FAIL;

  (*ppEnum)->Reset();
  // Clone already AddRefed the result so don't addref it again.
  return NOERROR;
}

//-----------------------------------------------------
STDMETHODIMP nsDataObj::DAdvise(LPFORMATETC pFE, DWORD dwFlags,
                                LPADVISESINK pIAdviseSink, DWORD* pdwConn) {
  return OLE_E_ADVISENOTSUPPORTED;
}

//-----------------------------------------------------
STDMETHODIMP nsDataObj::DUnadvise(DWORD dwConn) {
  return OLE_E_ADVISENOTSUPPORTED;
}

//-----------------------------------------------------
STDMETHODIMP nsDataObj::EnumDAdvise(LPENUMSTATDATA* ppEnum) {
  return OLE_E_ADVISENOTSUPPORTED;
}

// IDataObjectAsyncCapability methods
STDMETHODIMP nsDataObj::EndOperation(HRESULT hResult, IBindCtx* pbcReserved,
                                     DWORD dwEffects) {
  mIsInOperation = FALSE;
  return S_OK;
}

STDMETHODIMP nsDataObj::GetAsyncMode(BOOL* pfIsOpAsync) {
  *pfIsOpAsync = mIsAsyncMode;

  return S_OK;
}

STDMETHODIMP nsDataObj::InOperation(BOOL* pfInAsyncOp) {
  *pfInAsyncOp = mIsInOperation;

  return S_OK;
}

STDMETHODIMP nsDataObj::SetAsyncMode(BOOL fDoOpAsync) {
  mIsAsyncMode = fDoOpAsync;
  return S_OK;
}

STDMETHODIMP nsDataObj::StartOperation(IBindCtx* pbcReserved) {
  mIsInOperation = TRUE;
  return S_OK;
}

//
// GetDIB
//
// Someone is asking for a bitmap. The data in the transferable will be a
// straight imgIContainer, so just QI it.
//
HRESULT
nsDataObj::GetDib(const nsACString& inFlavor, FORMATETC& aFormat,
                  STGMEDIUM& aSTG, DibType aDibType) {
  nsCOMPtr<nsISupports> genericDataWrapper;
  if (NS_FAILED(
          mTransferable->GetTransferData(PromiseFlatCString(inFlavor).get(),
                                         getter_AddRefs(genericDataWrapper)))) {
    return E_FAIL;
  }

  nsCOMPtr<imgIContainer> image = do_QueryInterface(genericDataWrapper);
  if (!image) {
    return E_FAIL;
  }

  nsCOMPtr<imgITools> imgTools =
      do_CreateInstance("@mozilla.org/image/tools;1");

  nsAutoString options(u""_ns);
  if (aDibType == DibType::Bmp) {
    if (aFormat.cfFormat == CF_DIBV5) {
      options.AssignLiteral("version=5");
    } else {
      options.AssignLiteral("version=3");
    }
  }

  const nsLiteralCString mimeType = aDibType == DibType::Bmp
                                        ? nsLiteralCString(IMAGE_BMP)
                                        : nsLiteralCString(IMAGE_PNG);
  nsCOMPtr<nsIInputStream> inputStream;
  nsresult rv = imgTools->EncodeImage(image, mimeType, options,
                                      getter_AddRefs(inputStream));

  if (NS_FAILED(rv) || !inputStream) {
    return E_FAIL;
  }

  nsCOMPtr<imgIEncoder> encoder = do_QueryInterface(inputStream);
  if (!encoder) {
    return E_FAIL;
  }

  uint32_t size = 0;
  rv = encoder->GetImageBufferUsed(&size);
  if (NS_FAILED(rv) || size <= BFH_LENGTH) {
    return E_FAIL;
  }

  char* src = nullptr;
  rv = encoder->GetImageBuffer(&src);
  if (NS_FAILED(rv) || !src) {
    return E_FAIL;
  }

  if (aDibType == DibType::Bmp) {
    // We don't want the BMP file-header for CF_DIB; it only exists in files.
    src += BFH_LENGTH;
    size -= BFH_LENGTH;
  }

  ScopedOLEMemory<char[]> glob(size);
  if (!glob) {
    return E_FAIL;
  }

  {
    auto lock = glob.lock();
    ::CopyMemory(lock.begin(), src, size);
  }

  aSTG.tymed = TYMED_HGLOBAL;
  aSTG.hGlobal = glob.forget();
  return S_OK;
}

//
// GetFileDescriptor
//

HRESULT
nsDataObj ::GetFileDescriptor(FORMATETC& aFE, STGMEDIUM& aSTG,
                              bool aIsUnicode) {
  HRESULT res = S_OK;

  // How we handle this depends on if we're dealing with an internet
  // shortcut, since those are done under the covers.
  if (IsFlavourPresent(kFilePromiseMime) || IsFlavourPresent(kFileMime)) {
    if (aIsUnicode)
      return GetFileDescriptor_IStreamW(aFE, aSTG);
    else
      return GetFileDescriptor_IStreamA(aFE, aSTG);
  } else if (IsFlavourPresent(kURLMime)) {
    if (aIsUnicode)
      res = GetFileDescriptorInternetShortcutW(aFE, aSTG);
    else
      res = GetFileDescriptorInternetShortcutA(aFE, aSTG);
  } else
    NS_WARNING("Not yet implemented\n");

  return res;
}  // GetFileDescriptor

//
HRESULT
nsDataObj ::GetFileContents(FORMATETC& aFE, STGMEDIUM& aSTG) {
  HRESULT res = S_OK;

  // How we handle this depends on if we're dealing with an internet
  // shortcut, since those are done under the covers.
  if (IsFlavourPresent(kFilePromiseMime) || IsFlavourPresent(kFileMime))
    return GetFileContents_IStream(aFE, aSTG);
  else if (IsFlavourPresent(kURLMime))
    return GetFileContentsInternetShortcut(aFE, aSTG);
  else
    NS_WARNING("Not yet implemented\n");

  return res;

}  // GetFileContents

// Ensure that the supplied name doesn't have invalid characters.
static void ValidateFilename(nsString& aFilename, bool isShortcut) {
  nsCOMPtr<nsIMIMEService> mimeService = do_GetService("@mozilla.org/mime;1");
  if (NS_WARN_IF(!mimeService)) {
    aFilename.Truncate();
    return;
  }

  uint32_t flags = nsIMIMEService::VALIDATE_SANITIZE_ONLY;
  if (isShortcut) {
    flags |= nsIMIMEService::VALIDATE_ALLOW_INVALID_FILENAMES;
  }

  nsAutoString outFilename;
  mimeService->ValidateFileNameForSaving(aFilename, EmptyCString(), flags,
                                         outFilename);
  aFilename = outFilename;
}

//
// Given a unicode string, convert it to a valid local charset filename
// and append the .url extension to be used for a shortcut file.
// This ensures that we do not cut MBCS characters in the middle.
//
// It would seem that this is more functionality suited to being in nsIFile.
//
static bool CreateURLFilenameFromTextA(nsAutoString& aText, char* aFilename) {
  if (aText.IsEmpty()) {
    return false;
  }
  aText.AppendLiteral(".url");
  ValidateFilename(aText, true);
  if (aText.IsEmpty()) {
    return false;
  }

  // ValidateFilename should already be checking the filename length, but do
  // an extra check to verify for the local code page that the converted text
  // doesn't go over MAX_PATH and just return false if it does.
  char defaultChar = '_';
  int currLen = WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK | WC_DEFAULTCHAR,
                                    aText.get(), -1, aFilename, MAX_PATH,
                                    &defaultChar, nullptr);
  return currLen != 0;
}

// Wide character version of CreateURLFilenameFromTextA
static bool CreateURLFilenameFromTextW(nsAutoString& aText,
                                       wchar_t* aFilename) {
  if (aText.IsEmpty()) {
    return false;
  }
  aText.AppendLiteral(".url");
  ValidateFilename(aText, true);
  if (aText.IsEmpty() || aText.Length() >= MAX_PATH) {
    return false;
  }

  wcscpy(&aFilename[0], aText.get());
  return true;
}

#define PAGEINFO_PROPERTIES "chrome://navigator/locale/pageInfo.properties"

static bool GetLocalizedString(const char* aName, nsAString& aString) {
  nsCOMPtr<nsIStringBundleService> stringService =
      mozilla::components::StringBundle::Service();
  if (!stringService) return false;

  nsCOMPtr<nsIStringBundle> stringBundle;
  nsresult rv = stringService->CreateBundle(PAGEINFO_PROPERTIES,
                                            getter_AddRefs(stringBundle));
  if (NS_FAILED(rv)) return false;

  rv = stringBundle->GetStringFromName(aName, aString);
  return NS_SUCCEEDED(rv);
}

//
// GetFileDescriptorInternetShortcut
//
// Create the special format for an internet shortcut and build up the data
// structures the shell is expecting.
//
HRESULT
nsDataObj ::GetFileDescriptorInternetShortcutA(FORMATETC& aFE,
                                               STGMEDIUM& aSTG) {
  // get the title of the shortcut
  nsAutoString title;
  if (NS_FAILED(ExtractShortcutTitle(title))) return E_OUTOFMEMORY;

  HGLOBAL fileGroupDescHandle =
      ::GlobalAlloc(GMEM_ZEROINIT | GMEM_SHARE, sizeof(FILEGROUPDESCRIPTORA));
  if (!fileGroupDescHandle) return E_OUTOFMEMORY;

  LPFILEGROUPDESCRIPTORA fileGroupDescA =
      reinterpret_cast<LPFILEGROUPDESCRIPTORA>(
          ::GlobalLock(fileGroupDescHandle));
  if (!fileGroupDescA) {
    ::GlobalFree(fileGroupDescHandle);
    return E_OUTOFMEMORY;
  }

  // get a valid filename in the following order: 1) from the page title,
  // 2) localized string for an untitled page, 3) just use "Untitled.url"
  if (!CreateURLFilenameFromTextA(title, fileGroupDescA->fgd[0].cFileName)) {
    nsAutoString untitled;
    if (!GetLocalizedString("noPageTitle", untitled) ||
        !CreateURLFilenameFromTextA(untitled,
                                    fileGroupDescA->fgd[0].cFileName)) {
      strcpy(fileGroupDescA->fgd[0].cFileName, "Untitled.url");
    }
  }

  // one file in the file block
  fileGroupDescA->cItems = 1;
  fileGroupDescA->fgd[0].dwFlags = FD_LINKUI;

  ::GlobalUnlock(fileGroupDescHandle);
  aSTG.hGlobal = fileGroupDescHandle;
  aSTG.tymed = TYMED_HGLOBAL;

  return S_OK;
}  // GetFileDescriptorInternetShortcutA

HRESULT
nsDataObj ::GetFileDescriptorInternetShortcutW(FORMATETC& aFE,
                                               STGMEDIUM& aSTG) {
  // get the title of the shortcut
  nsAutoString title;
  if (NS_FAILED(ExtractShortcutTitle(title))) return E_OUTOFMEMORY;

  HGLOBAL fileGroupDescHandle =
      ::GlobalAlloc(GMEM_ZEROINIT | GMEM_SHARE, sizeof(FILEGROUPDESCRIPTORW));
  if (!fileGroupDescHandle) return E_OUTOFMEMORY;

  LPFILEGROUPDESCRIPTORW fileGroupDescW =
      reinterpret_cast<LPFILEGROUPDESCRIPTORW>(
          ::GlobalLock(fileGroupDescHandle));
  if (!fileGroupDescW) {
    ::GlobalFree(fileGroupDescHandle);
    return E_OUTOFMEMORY;
  }

  // get a valid filename in the following order: 1) from the page title,
  // 2) localized string for an untitled page, 3) just use "Untitled.url"
  if (!CreateURLFilenameFromTextW(title, fileGroupDescW->fgd[0].cFileName)) {
    nsAutoString untitled;
    if (!GetLocalizedString("noPageTitle", untitled) ||
        !CreateURLFilenameFromTextW(untitled,
                                    fileGroupDescW->fgd[0].cFileName)) {
      wcscpy(fileGroupDescW->fgd[0].cFileName, L"Untitled.url");
    }
  }

  // one file in the file block
  fileGroupDescW->cItems = 1;
  fileGroupDescW->fgd[0].dwFlags = FD_LINKUI;

  ::GlobalUnlock(fileGroupDescHandle);
  aSTG.hGlobal = fileGroupDescHandle;
  aSTG.tymed = TYMED_HGLOBAL;

  return S_OK;
}  // GetFileDescriptorInternetShortcutW

//
// GetFileContentsInternetShortcut
//
// Create the special format for an internet shortcut and build up the data
// structures the shell is expecting.
//
HRESULT
nsDataObj ::GetFileContentsInternetShortcut(FORMATETC& aFE, STGMEDIUM& aSTG) {
  static const char* kShellIconPref = "browser.shell.shortcutFavicons";
  nsAutoString url;
  if (NS_FAILED(ExtractShortcutURL(url))) return E_OUTOFMEMORY;

  nsCOMPtr<nsIURI> aUri;
  nsresult rv = NS_NewURI(getter_AddRefs(aUri), url);
  if (NS_FAILED(rv)) {
    return E_FAIL;
  }

  nsAutoCString asciiUrl;
  rv = aUri->GetAsciiSpec(asciiUrl);
  if (NS_FAILED(rv)) {
    return E_FAIL;
  }

  RefPtr<AutoCloseEvent> event;

  const char* shortcutFormatStr;
  int totalLen;
  nsCString asciiPath;
  if (!Preferences::GetBool(kShellIconPref, true)) {
    shortcutFormatStr = "[InternetShortcut]\r\nURL=%s\r\n";
    const int formatLen = strlen(shortcutFormatStr) - 2;  // don't include %s
    totalLen = formatLen + asciiUrl.Length();  // don't include null character
  } else {
    nsCOMPtr<nsIFile> icoFile;

    nsAutoString aUriHash;

    event = new AutoCloseEvent();
    if (!event->IsInited()) {
      return E_FAIL;
    }

    RefPtr<AutoSetEvent> e = new AutoSetEvent(WrapNotNull(event));
    mozilla::widget::FaviconHelper::ObtainCachedIconFile(
        aUri, aUriHash, mIOThread, true,
        NS_NewRunnableFunction(
            "FaviconHelper::RefreshDesktop", [e = std::move(e)] {
              if (e->IsWaiting()) {
                // Unblock IStream:::Read.
                e->Signal();
              } else {
                // We could not wait until the favicon was available. We have
                // to refresh to refect the favicon.
                SendNotifyMessage(HWND_BROADCAST, WM_SETTINGCHANGE,
                                  SPI_SETNONCLIENTMETRICS, 0);
              }
            }));

    rv = mozilla::widget::FaviconHelper::GetOutputIconPath(aUri, icoFile, true);
    NS_ENSURE_SUCCESS(rv, E_FAIL);
    nsString path;
    rv = icoFile->GetPath(path);
    NS_ENSURE_SUCCESS(rv, E_FAIL);

    if (IsAsciiNullTerminated(static_cast<const char16_t*>(path.get()))) {
      LossyCopyUTF16toASCII(path, asciiPath);
      shortcutFormatStr =
          "[InternetShortcut]\r\nURL=%s\r\n"
          "IDList=\r\nHotKey=0\r\nIconFile=%s\r\n"
          "IconIndex=0\r\n";
    } else {
      int len =
          WideCharToMultiByte(CP_UTF7, 0, char16ptr_t(path.BeginReading()),
                              path.Length(), nullptr, 0, nullptr, nullptr);
      NS_ENSURE_TRUE(len > 0, E_FAIL);
      asciiPath.SetLength(len);
      WideCharToMultiByte(CP_UTF7, 0, char16ptr_t(path.BeginReading()),
                          path.Length(), asciiPath.BeginWriting(), len, nullptr,
                          nullptr);
      shortcutFormatStr =
          "[InternetShortcut]\r\nURL=%s\r\n"
          "IDList=\r\nHotKey=0\r\nIconIndex=0\r\n"
          "[InternetShortcut.W]\r\nIconFile=%s\r\n";
    }
    const int formatLen = strlen(shortcutFormatStr) - 2 * 2;  // no %s twice
    totalLen = formatLen + asciiUrl.Length() +
               asciiPath.Length();  // we don't want a null character on the end
  }

  // create a global memory area and build up the file contents w/in it
  nsAutoGlobalMem globalMem(nsHGLOBAL(::GlobalAlloc(GMEM_SHARE, totalLen)));
  if (!globalMem) return E_OUTOFMEMORY;

  char* contents = reinterpret_cast<char*>(::GlobalLock(globalMem.get()));
  if (!contents) {
    return E_OUTOFMEMORY;
  }

  // NOTE: we intentionally use the Microsoft version of snprintf here because
  // it does NOT null
  // terminate strings which reach the maximum size of the buffer. Since we know
  // that the formatted length here is totalLen, this call to _snprintf will
  // format the string into the buffer without appending the null character.

  if (!Preferences::GetBool(kShellIconPref, true)) {
    _snprintf(contents, totalLen, shortcutFormatStr, asciiUrl.get());
  } else {
    _snprintf(contents, totalLen, shortcutFormatStr, asciiUrl.get(),
              asciiPath.get());
  }

  ::GlobalUnlock(globalMem.get());

  if (aFE.tymed & TYMED_ISTREAM) {
    if (!mIsInOperation) {
      // The drop target didn't initiate an async operation.
      // We can't block CMemStream::Read.
      event = nullptr;
    }
    RefPtr<IStream> stream =
        new CMemStream(globalMem.disown(), totalLen, event.forget());
    stream.forget(&aSTG.pstm);
    aSTG.tymed = TYMED_ISTREAM;
  } else {
    if (event && event->IsInited()) {
      event->Signal();  // We can't block reading the global memory
    }
    aSTG.hGlobal = globalMem.disown();
    aSTG.tymed = TYMED_HGLOBAL;
  }

  return S_OK;
}  // GetFileContentsInternetShortcut

// check if specified flavour is present in the transferable
bool nsDataObj ::IsFlavourPresent(const char* inFlavour) {
  bool retval = false;
  NS_ENSURE_TRUE(mTransferable, false);

  // get the list of flavors available in the transferable
  nsTArray<nsCString> flavors;
  nsresult rv = mTransferable->FlavorsTransferableCanExport(flavors);
  NS_ENSURE_SUCCESS(rv, false);

  // try to find requested flavour
  for (uint32_t i = 0; i < flavors.Length(); ++i) {
    if (flavors[i].Equals(inFlavour)) {
      retval = true;  // found it!
      break;
    }
  }  // for each flavor

  return retval;
}

HRESULT nsDataObj::GetPreferredDropEffect(FORMATETC& aFE, STGMEDIUM& aSTG) {
  HRESULT res = S_OK;
  aSTG.tymed = TYMED_HGLOBAL;
  aSTG.pUnkForRelease = nullptr;

  ScopedOLEMemory<DWORD> hGlobalMemory;
  if (hGlobalMemory) {
    // The PreferredDropEffect clipboard format is only registered if a
    // drag/drop of an image happens from Mozilla to the desktop.  We want its
    // value to be DROPEFFECT_MOVE in that case so that the file is moved from
    // the temporary location, not copied. This value should, ideally, be set on
    // the data object via SetData() but our IDataObject implementation doesn't
    // implement SetData.  It adds data to the data object lazily only when the
    // drop target asks for it.
    *hGlobalMemory.lock() = (DWORD)DROPEFFECT_MOVE;
  } else {
    res = E_OUTOFMEMORY;
  }
  aSTG.hGlobal = hGlobalMemory.forget();
  return res;
}

//-----------------------------------------------------
HRESULT nsDataObj::GetText(const nsACString& aDataFlavor, FORMATETC& aFE,
                           STGMEDIUM& aSTG) {
  // assignDataToStg
  //
  // Helper function to fill the STG with a block of data.
  auto const assignDataToStg = [&aSTG](void* data, size_t extent) -> HRESULT {
    aSTG.tymed = TYMED_HGLOBAL;
    aSTG.pUnkForRelease = nullptr;

    ScopedOLEMemory<char[]> hGlobalMemory(extent);
    if (hGlobalMemory) {
      auto dest = hGlobalMemory.lock();
      memcpy(dest.get(), data, extent);
    }

    aSTG.hGlobal = hGlobalMemory.forget();

    return S_OK;
  };

  const nsPromiseFlatCString& flavorStr = PromiseFlatCString(aDataFlavor);

  nsCOMPtr<nsISupports> genericDataWrapper;
  nsresult rv = mTransferable->GetTransferData(
      flavorStr.get(), getter_AddRefs(genericDataWrapper));
  if (NS_FAILED(rv) || !genericDataWrapper) {
    return E_FAIL;
  }

  // data is a possibly-wide NUL-terminated string. len is its strlen() -- not
  // its allocation length!
  auto const [data, len] = [&]() {
    void* data = nullptr;
    uint32_t len;
    nsPrimitiveHelpers::CreateDataFromPrimitive(
        nsDependentCString(flavorStr.get()), genericDataWrapper, &data, &len);
    return std::tuple{data, size_t(len)};
  }();
  if (!data) return E_FAIL;

  // CreateDataFromPrimitive allocates memory; free it on exit.
  auto const _release_data =
      mozilla::MakeScopeExit([data = data]() { ::free(data); });

  // We play games under the hood and advertise flavors that we know we can
  // support, only they require a bit of conversion or munging of the data. Do
  // that here.
  //
  // The transferable gives us data that is null-terminated, but this isn't
  // reflected in the |len| parameter. Windows apps expect this null to be there
  // so bump our data buffer by the appropriate size to account for the null
  // (one char for CF_TEXT, one char16_t for CF_UNICODETEXT).

  if (aFE.cfFormat == CF_TEXT) {
    // Someone is asking for text/plain; convert the unicode (assuming it's
    // present) to text with the correct platform encoding.
    size_t bufferSize = sizeof(char) * (len + 2);
    char* plainTextData = static_cast<char*>(moz_xmalloc(bufferSize));
    auto const _release =
        mozilla::MakeScopeExit([plainTextData]() { ::free(plainTextData); });

    char16_t* castedUnicode = reinterpret_cast<char16_t*>(data);
    int32_t plainTextLen =
        WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)castedUnicode, len / 2 + 1,
                            plainTextData, bufferSize, NULL, NULL);

    if (plainTextLen) {
      return assignDataToStg(plainTextData, plainTextLen);
    }

    NS_WARNING("Oh no, couldn't convert unicode to plain text");
    return S_OK;
  }

  if (aFE.cfFormat == nsClipboard::GetHtmlClipboardFormat()) {
    // Someone is asking for win32's HTML flavor. Convert our html fragment
    // from unicode to UTF-8 then put it into a format specified by msft.
    NS_ConvertUTF16toUTF8 converter(reinterpret_cast<char16_t*>(data));
    char* utf8HTML = nullptr;
    nsresult rv =
        BuildPlatformHTML(converter.get(), &utf8HTML);  // null terminates
    auto const _release =
        mozilla::MakeScopeExit([utf8HTML]() { ::free(utf8HTML); });

    if (NS_SUCCEEDED(rv) && utf8HTML) {
      // return our HTML data. Don't forget the null.
      return assignDataToStg(utf8HTML, strlen(utf8HTML) + sizeof(char));
    }

    NS_WARNING("Oh no, couldn't convert to HTML");
    return S_OK;
  }

  // We assume that any data-format that isn't caught above can be satisfied by
  // Unicode text. (This may be an erroneous assumption, but seems to have been
  // true so far.)
  bool const excludeNull =
      aFE.cfFormat == nsClipboard::GetCustomClipboardFormat();

  return assignDataToStg(data, len + (excludeNull ? 0 : sizeof(char16_t)));
}

//-----------------------------------------------------
HRESULT nsDataObj::GetFile(FORMATETC& aFE, STGMEDIUM& aSTG) {
  uint32_t dfInx = 0;
  ULONG count;
  FORMATETC fe;
  m_enumFE->Reset();
  while (NOERROR == m_enumFE->Next(1, &fe, &count) &&
         dfInx < mDataFlavors.Length()) {
    if (mDataFlavors[dfInx].EqualsLiteral(kNativeImageMime))
      return DropImage(aFE, aSTG);
    if (mDataFlavors[dfInx].EqualsLiteral(kFileMime))
      return DropFile(aFE, aSTG);
    if (mDataFlavors[dfInx].EqualsLiteral(kFilePromiseMime))
      return DropTempFile(aFE, aSTG);
    dfInx++;
  }
  return E_FAIL;
}

static HRESULT AssignDropfile(STGMEDIUM& aSTG, nsAString const& aPath) {
  // Struct describing the data in the OLE memory space. Marginally cleaner than
  // completely-manual pointer manipulation.
  struct DFWithPaths {
    DROPFILES dropfiles;
    // An epsilon-terminated list of NUL-terminated strings.
    // See the CF_HDROP shell clipboard format for more info.
    WCHAR paths[];
  };

  // C++ doesn't have a dependent-type system robust enough for ScopedOLEMemory
  // to handle a struct-with-flexible-array-member well, so we eschew it here
  // for the less-strongly-typed (and slightly more awkward) nsAutoGlobalMem.
  size_t const allocSize =
      // Size of the initial header block...
      sizeof(DFWithPaths) +
      // ... size of the first path...
      ((aPath.Length() + 1) * sizeof(WCHAR)) +
      // ... and size of the terminating empty string.
      sizeof(L"");

  nsAutoGlobalMem hGlobalMemory(
      nsHGLOBAL(::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, allocSize)));

  if (!hGlobalMemory) {
    return E_FAIL;
  }

  {
    ScopedOLELock<DFWithPaths*> pDFWithPaths(hGlobalMemory.get());

    // First, populate the dropfile structure...
    DROPFILES* pDropFile = &pDFWithPaths->dropfiles;
    pDropFile->pFiles =
        offsetof(DFWithPaths, paths) - offsetof(DFWithPaths, dropfiles);
    pDropFile->fNC = 0;
    pDropFile->pt.x = 0;
    pDropFile->pt.y = 0;
    pDropFile->fWide = TRUE;

    // ... then copy the filename into `paths`.
    WCHAR* dest = pDFWithPaths->paths;
    WCHAR* after_dest [[maybe_unused]] =
        std::copy_n(aPath.BeginReading(), aPath.Length(), dest);

    // Two NULs are needed after the file name; the GMEM_ZEROINIT above should
    // provide them.
    size_t const offset [[maybe_unused]] =
        (char*)after_dest - (char*)pDFWithPaths.get();
    MOZ_ASSERT(allocSize - offset == sizeof(WCHAR) * 2);
    MOZ_ASSERT(after_dest[0] == L'\0');
    MOZ_ASSERT(after_dest[1] == L'\0');
  }

  aSTG = {
      .tymed = TYMED_HGLOBAL,
      .hGlobal = hGlobalMemory.disown(),
      .pUnkForRelease = nullptr,
  };

  return S_OK;
}

HRESULT nsDataObj::DropFile(FORMATETC& aFE, STGMEDIUM& aSTG) {
  nsresult rv;
  nsCOMPtr<nsISupports> genericDataWrapper;

  if (NS_FAILED(mTransferable->GetTransferData(
          kFileMime, getter_AddRefs(genericDataWrapper)))) {
    return E_FAIL;
  }
  nsCOMPtr<nsIFile> file(do_QueryInterface(genericDataWrapper));
  if (!file) return E_FAIL;

  nsAutoString path;
  rv = file->GetPath(path);
  if (NS_FAILED(rv)) return E_FAIL;

  return AssignDropfile(aSTG, path);
}

HRESULT nsDataObj::DropImage(FORMATETC& /* aFE */, STGMEDIUM& aSTG) {
  nsresult rv;
  if (!mCachedTempFile) {
    nsCOMPtr<nsISupports> genericDataWrapper;

    if (NS_FAILED(mTransferable->GetTransferData(
            kNativeImageMime, getter_AddRefs(genericDataWrapper)))) {
      return E_FAIL;
    }
    nsCOMPtr<imgIContainer> image(do_QueryInterface(genericDataWrapper));
    if (!image) return E_FAIL;

    nsCOMPtr<imgITools> imgTools =
        do_CreateInstance("@mozilla.org/image/tools;1");
    nsCOMPtr<nsIInputStream> inputStream;

    // Select the image encoding.
    //
    // If we get here, the negotiation phase selected "CF_HDROP"... which
    // unfortunately means "file", rather than something more useful like "file
    // of type XYZ". As of 2024, though, it seems that pretty much everything in
    // the ecosystem understands PNG, so we just default to that (with a config
    // pref to enable fallback to BMP for older recipients).
    nsCString extension;
    if (StaticPrefs::clipboard_copy_image_file_as_png()) {
      extension = ".png"_ns;
      rv = imgTools->EncodeImage(image, nsLiteralCString(IMAGE_PNG), u""_ns,
                                 getter_AddRefs(inputStream));
    } else {
      extension = ".bmp"_ns;
      rv = imgTools->EncodeImage(image, nsLiteralCString(IMAGE_BMP),
                                 u"bpp=32;version=3"_ns,
                                 getter_AddRefs(inputStream));
    }
    if (NS_FAILED(rv) || !inputStream) {
      return E_FAIL;
    }

    nsCOMPtr<imgIEncoder> encoder = do_QueryInterface(inputStream);
    if (!encoder) {
      return E_FAIL;
    }

    uint32_t size = 0;
    rv = encoder->GetImageBufferUsed(&size);
    if (NS_FAILED(rv)) {
      return E_FAIL;
    }

    char* src = nullptr;
    rv = encoder->GetImageBuffer(&src);
    if (NS_FAILED(rv) || !src) {
      return E_FAIL;
    }

    // Save the bitmap to a temporary location.
    nsCOMPtr<nsIFile> dropFile;
    rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(dropFile));
    if (!dropFile) {
      return E_FAIL;
    }

    // Filename must be random so as not to confuse apps like
    // Photoshop which handle multiple drags into a single window.
    char buf[9];
    NS_MakeRandomString(buf, 8);
    nsCString filename{buf};
    filename.Append(extension);
    dropFile->AppendNative(filename);
    rv = dropFile->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0660);
    if (NS_FAILED(rv)) {
      return E_FAIL;
    }

    // Cache the temp file so we can delete it later and so
    // it doesn't get recreated over and over on multiple calls
    // which does occur from windows shell.
    dropFile->Clone(getter_AddRefs(mCachedTempFile));

    // Write the data to disk.
    nsCOMPtr<nsIOutputStream> outStream;
    rv = NS_NewLocalFileOutputStream(getter_AddRefs(outStream), dropFile);
    if (NS_FAILED(rv)) {
      return E_FAIL;
    }

    uint32_t written = 0;
    rv = outStream->Write(src, size, &written);
    if (NS_FAILED(rv) || written != size) {
      return E_FAIL;
    }

    outStream->Close();
  }

  // Pass the file name back to the drop target so that it can access the file.
  nsAutoString path;
  rv = mCachedTempFile->GetPath(path);
  if (NS_FAILED(rv)) return E_FAIL;

  return AssignDropfile(aSTG, path);
}

HRESULT nsDataObj::DropTempFile(FORMATETC& aFE, STGMEDIUM& aSTG) {
  nsresult rv;
  if (!mCachedTempFile) {
    // Tempfile will need a temporary location.
    nsCOMPtr<nsIFile> dropFile;
    rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(dropFile));
    if (!dropFile) return E_FAIL;

    // Filename must be random
    nsCString filename;
    nsAutoString wideFileName;
    nsCOMPtr<nsIURI> sourceURI;
    HRESULT res;
    res = GetDownloadDetails(getter_AddRefs(sourceURI), wideFileName);
    if (FAILED(res)) return res;
    NS_CopyUnicodeToNative(wideFileName, filename);

    dropFile->AppendNative(filename);
    rv = dropFile->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0660);
    if (NS_FAILED(rv)) return E_FAIL;

    // Cache the temp file so we can delete it later and so
    // it doesn't get recreated over and over on multiple calls
    // which does occur from windows shell.
    dropFile->Clone(getter_AddRefs(mCachedTempFile));

    // Write the data to disk.
    nsCOMPtr<nsIOutputStream> outStream;
    rv = NS_NewLocalFileOutputStream(getter_AddRefs(outStream), dropFile);
    if (NS_FAILED(rv)) return E_FAIL;

    IStream* pStream = nullptr;
    nsDataObj::CreateStream(&pStream);
    NS_ENSURE_TRUE(pStream, E_FAIL);

    char buffer[512];
    ULONG readCount = 0;
    uint32_t writeCount = 0;
    while (1) {
      HRESULT hres = pStream->Read(buffer, sizeof(buffer), &readCount);
      if (FAILED(hres)) return E_FAIL;
      if (readCount == 0) break;
      rv = outStream->Write(buffer, readCount, &writeCount);
      if (NS_FAILED(rv)) return E_FAIL;
    }
    outStream->Close();
    pStream->Release();
  }

  // Pass the file name back to the drop target so that it can access the file.
  nsAutoString path;
  rv = mCachedTempFile->GetPath(path);
  if (NS_FAILED(rv)) return E_FAIL;

  return AssignDropfile(aSTG, path);
}

//-----------------------------------------------------
// Registers the DataFlavor/FE pair.
//-----------------------------------------------------
void nsDataObj::AddDataFlavor(const char* aDataFlavor, LPFORMATETC aFE) {
  // These two lists are the mapping to and from data flavors and FEs.
  // Later, OLE will tell us it needs a certain type of FORMATETC (text,
  // unicode, etc) unicode, etc), so we will look up the data flavor that
  // corresponds to the FE and then ask the transferable for that type of data.
  mDataFlavors.AppendElement(aDataFlavor);
  m_enumFE->AddFormatEtc(aFE);
}

//-----------------------------------------------------
// Sets the transferable object
//-----------------------------------------------------
void nsDataObj::SetTransferable(nsITransferable* aTransferable) {
  mTransferable = aTransferable;
}

//
// ExtractURL
//
// Roots around in the transferable for the appropriate flavor that indicates
// a url and pulls out the url portion of the data. Used mostly for creating
// internet shortcuts on the desktop. The url flavor is of the format:
//
//   <url> <linefeed> <page title>
//
nsresult nsDataObj ::ExtractShortcutURL(nsString& outURL) {
  NS_ASSERTION(mTransferable, "We don't have a good transferable");
  nsresult rv = NS_ERROR_FAILURE;

  nsCOMPtr<nsISupports> genericURL;
  if (NS_SUCCEEDED(mTransferable->GetTransferData(
          kURLMime, getter_AddRefs(genericURL)))) {
    nsCOMPtr<nsISupportsString> urlObject(do_QueryInterface(genericURL));
    if (urlObject) {
      nsAutoString url;
      urlObject->GetData(url);
      outURL = url;

      // find the first linefeed in the data, that's where the url ends. trunc
      // the result string at that point.
      int32_t lineIndex = outURL.FindChar('\n');
      NS_ASSERTION(lineIndex > 0,
                   "Format for url flavor is <url> <linefeed> <page title>");
      if (lineIndex > 0) {
        outURL.Truncate(lineIndex);
        rv = NS_OK;
      }
    }
  } else if (NS_SUCCEEDED(mTransferable->GetTransferData(
                 kURLDataMime, getter_AddRefs(genericURL))) ||
             NS_SUCCEEDED(mTransferable->GetTransferData(
                 kURLPrivateMime, getter_AddRefs(genericURL)))) {
    nsCOMPtr<nsISupportsString> urlObject(do_QueryInterface(genericURL));
    if (urlObject) {
      nsAutoString url;
      urlObject->GetData(url);
      outURL = url;

      rv = NS_OK;
    }

  }  // if found flavor

  return rv;

}  // ExtractShortcutURL

//
// ExtractShortcutTitle
//
// Roots around in the transferable for the appropriate flavor that indicates
// a url and pulls out the title portion of the data. Used mostly for creating
// internet shortcuts on the desktop. The url flavor is of the format:
//
//   <url> <linefeed> <page title>
//
nsresult nsDataObj ::ExtractShortcutTitle(nsString& outTitle) {
  NS_ASSERTION(mTransferable, "We'd don't have a good transferable");
  nsresult rv = NS_ERROR_FAILURE;

  nsCOMPtr<nsISupports> genericURL;
  if (NS_SUCCEEDED(mTransferable->GetTransferData(
          kURLMime, getter_AddRefs(genericURL)))) {
    nsCOMPtr<nsISupportsString> urlObject(do_QueryInterface(genericURL));
    if (urlObject) {
      nsAutoString url;
      urlObject->GetData(url);

      // find the first linefeed in the data, that's where the url ends. we want
      // everything after that linefeed. FindChar() returns -1 if we can't find
      int32_t lineIndex = url.FindChar('\n');
      NS_ASSERTION(lineIndex != -1,
                   "Format for url flavor is <url> <linefeed> <page title>");
      if (lineIndex != -1) {
        url.Mid(outTitle, lineIndex + 1, url.Length() - (lineIndex + 1));
        rv = NS_OK;
      }
    }
  }  // if found flavor

  return rv;

}  // ExtractShortcutTitle

//
// BuildPlatformHTML
//
// Munge our HTML data to win32's CF_HTML spec. Basically, put the requisite
// header information on it. This will null-terminate |outPlatformHTML|. See
//  https://docs.microsoft.com/en-us/windows/win32/dataxchg/html-clipboard-format
// for details.
//
// We assume that |inOurHTML| is already a fragment (ie, doesn't have <HTML>
// or <BODY> tags). We'll wrap the fragment with them to make other apps
// happy.
//
nsresult nsDataObj ::BuildPlatformHTML(const char* inOurHTML,
                                       char** outPlatformHTML) {
  *outPlatformHTML = nullptr;
  nsDependentCString inHTMLString(inOurHTML);

  // Do we already have mSourceURL from a drag?
  if (mSourceURL.IsEmpty()) {
    nsAutoString url;
    ExtractShortcutURL(url);

    AppendUTF16toUTF8(url, mSourceURL);
  }

  constexpr auto kStartHTMLPrefix = "Version:0.9\r\nStartHTML:"_ns;
  constexpr auto kEndHTMLPrefix = "\r\nEndHTML:"_ns;
  constexpr auto kStartFragPrefix = "\r\nStartFragment:"_ns;
  constexpr auto kEndFragPrefix = "\r\nEndFragment:"_ns;
  constexpr auto kStartSourceURLPrefix = "\r\nSourceURL:"_ns;
  constexpr auto kEndFragTrailer = "\r\n"_ns;

  // The CF_HTML's size is embedded in the fragment, in such a way that the
  // number of digits in the size is part of the size itself. While it _is_
  // technically possible to compute the necessary size of the size-field
  // precisely -- by trial and error, if nothing else -- it's simpler just to
  // pick a rough but generous estimate and zero-pad it. (Zero-padding is
  // explicitly permitted by the format definition.)
  //
  // Originally, in 2001, the "rough but generous estimate" was 8 digits. While
  // a maximum size of (10**9 - 1) bytes probably would have covered all
  // possible use-cases at the time, it's somewhat more likely to overflow
  // nowadays. Nonetheless, for the sake of backwards compatibility with any
  // misbehaving consumers of our existing CF_HTML output, we retain exactly
  // that padding for (most) fragments where it suffices. (No such misbehaving
  // consumers are actually known, so this is arguably paranoia.)
  //
  // It is now 2022. A padding size of 16 will cover up to about 8.8 petabytes,
  // which should be enough for at least the next few years or so.
  const size_t numberLength = inHTMLString.Length() < 9999'0000 ? 8 : 16;

  const size_t sourceURLLength = mSourceURL.Length();

  const size_t fixedHeaderLen =
      kStartHTMLPrefix.Length() + kEndHTMLPrefix.Length() +
      kStartFragPrefix.Length() + kEndFragPrefix.Length() +
      kEndFragTrailer.Length() + (4 * numberLength);

  const size_t totalHeaderLen =
      fixedHeaderLen + (sourceURLLength > 0
                            ? kStartSourceURLPrefix.Length() + sourceURLLength
                            : 0);

  constexpr auto kHeaderString = "<html><body>\r\n<!--StartFragment-->"_ns;
  constexpr auto kTrailingString =
      "<!--EndFragment-->\r\n"
      "</body>\r\n"
      "</html>"_ns;

  // calculate the offsets
  size_t startHTMLOffset = totalHeaderLen;
  size_t startFragOffset = startHTMLOffset + kHeaderString.Length();

  size_t endFragOffset = startFragOffset + inHTMLString.Length();
  size_t endHTMLOffset = endFragOffset + kTrailingString.Length();

  // now build the final version
  nsCString clipboardString;
  clipboardString.SetCapacity(endHTMLOffset);

  const int numberLengthInt = static_cast<int>(numberLength);
  clipboardString.Append(kStartHTMLPrefix);
  clipboardString.AppendPrintf("%0*zu", numberLengthInt, startHTMLOffset);

  clipboardString.Append(kEndHTMLPrefix);
  clipboardString.AppendPrintf("%0*zu", numberLengthInt, endHTMLOffset);

  clipboardString.Append(kStartFragPrefix);
  clipboardString.AppendPrintf("%0*zu", numberLengthInt, startFragOffset);

  clipboardString.Append(kEndFragPrefix);
  clipboardString.AppendPrintf("%0*zu", numberLengthInt, endFragOffset);

  if (sourceURLLength > 0) {
    clipboardString.Append(kStartSourceURLPrefix);
    clipboardString.Append(mSourceURL);
  }

  clipboardString.Append(kEndFragTrailer);

  // Assert that the positional values were correct as we pass by their
  // corresponding positions.
  MOZ_ASSERT(clipboardString.Length() == startHTMLOffset);
  clipboardString.Append(kHeaderString);
  MOZ_ASSERT(clipboardString.Length() == startFragOffset);
  clipboardString.Append(inHTMLString);
  MOZ_ASSERT(clipboardString.Length() == endFragOffset);
  clipboardString.Append(kTrailingString);
  MOZ_ASSERT(clipboardString.Length() == endHTMLOffset);

  *outPlatformHTML = ToNewCString(clipboardString, mozilla::fallible);
  if (!*outPlatformHTML) return NS_ERROR_OUT_OF_MEMORY;

  return NS_OK;
}

HRESULT
nsDataObj ::GetUniformResourceLocator(FORMATETC& aFE, STGMEDIUM& aSTG,
                                      bool aIsUnicode) {
  HRESULT res = S_OK;
  if (IsFlavourPresent(kURLMime)) {
    if (aIsUnicode)
      res = ExtractUniformResourceLocatorW(aFE, aSTG);
    else
      res = ExtractUniformResourceLocatorA(aFE, aSTG);
  } else
    NS_WARNING("Not yet implemented\n");
  return res;
}

HRESULT
nsDataObj::ExtractUniformResourceLocatorA(FORMATETC& aFE, STGMEDIUM& aSTG) {
  HRESULT result = S_OK;

  nsAutoString url;
  if (NS_FAILED(ExtractShortcutURL(url))) return E_OUTOFMEMORY;

  NS_LossyConvertUTF16toASCII asciiUrl(url);
  const int totalLen = asciiUrl.Length() + 1;
  HGLOBAL hGlobalMemory = GlobalAlloc(GMEM_ZEROINIT | GMEM_SHARE, totalLen);
  if (!hGlobalMemory) return E_OUTOFMEMORY;

  char* contents = reinterpret_cast<char*>(GlobalLock(hGlobalMemory));
  if (!contents) {
    GlobalFree(hGlobalMemory);
    return E_OUTOFMEMORY;
  }

  strcpy(contents, asciiUrl.get());
  GlobalUnlock(hGlobalMemory);
  aSTG.hGlobal = hGlobalMemory;
  aSTG.tymed = TYMED_HGLOBAL;

  return result;
}

HRESULT
nsDataObj::ExtractUniformResourceLocatorW(FORMATETC& aFE, STGMEDIUM& aSTG) {
  HRESULT result = S_OK;

  nsAutoString url;
  if (NS_FAILED(ExtractShortcutURL(url))) return E_OUTOFMEMORY;

  const int totalLen = (url.Length() + 1) * sizeof(char16_t);
  HGLOBAL hGlobalMemory = GlobalAlloc(GMEM_ZEROINIT | GMEM_SHARE, totalLen);
  if (!hGlobalMemory) return E_OUTOFMEMORY;

  wchar_t* contents = reinterpret_cast<wchar_t*>(GlobalLock(hGlobalMemory));
  if (!contents) {
    GlobalFree(hGlobalMemory);
    return E_OUTOFMEMORY;
  }

  wcscpy(contents, url.get());
  GlobalUnlock(hGlobalMemory);
  aSTG.hGlobal = hGlobalMemory;
  aSTG.tymed = TYMED_HGLOBAL;

  return result;
}

// Gets the filename from the kFilePromiseURLMime flavour
HRESULT nsDataObj::GetDownloadDetails(nsIURI** aSourceURI,
                                      nsAString& aFilename) {
  *aSourceURI = nullptr;

  NS_ENSURE_TRUE(mTransferable, E_FAIL);

  // get the URI from the kFilePromiseURLMime flavor
  nsCOMPtr<nsISupports> urlPrimitive;
  nsresult rv = mTransferable->GetTransferData(kFilePromiseURLMime,
                                               getter_AddRefs(urlPrimitive));
  NS_ENSURE_SUCCESS(rv, E_FAIL);
  nsCOMPtr<nsISupportsString> srcUrlPrimitive = do_QueryInterface(urlPrimitive);
  NS_ENSURE_TRUE(srcUrlPrimitive, E_FAIL);

  nsAutoString srcUri;
  srcUrlPrimitive->GetData(srcUri);
  if (srcUri.IsEmpty()) return E_FAIL;
  nsCOMPtr<nsIURI> sourceURI;
  NS_NewURI(getter_AddRefs(sourceURI), srcUri);

  nsAutoString srcFileName;
  nsCOMPtr<nsISupports> fileNamePrimitive;
  Unused << mTransferable->GetTransferData(kFilePromiseDestFilename,
                                           getter_AddRefs(fileNamePrimitive));
  nsCOMPtr<nsISupportsString> srcFileNamePrimitive =
      do_QueryInterface(fileNamePrimitive);
  if (srcFileNamePrimitive) {
    srcFileNamePrimitive->GetData(srcFileName);
  } else {
    nsCOMPtr<nsIURL> sourceURL = do_QueryInterface(sourceURI);
    if (!sourceURL) return E_FAIL;

    nsAutoCString urlFileName;
    sourceURL->GetFileName(urlFileName);
    NS_UnescapeURL(urlFileName);
    CopyUTF8toUTF16(urlFileName, srcFileName);
  }

  // make the name safe for the filesystem
  ValidateFilename(srcFileName, false);
  if (srcFileName.IsEmpty()) return E_FAIL;

  sourceURI.swap(*aSourceURI);
  aFilename = srcFileName;
  return S_OK;
}

HRESULT nsDataObj::GetFileDescriptor_IStreamA(FORMATETC& aFE, STGMEDIUM& aSTG) {
  HGLOBAL fileGroupDescHandle =
      ::GlobalAlloc(GMEM_ZEROINIT | GMEM_SHARE, sizeof(FILEGROUPDESCRIPTORW));
  NS_ENSURE_TRUE(fileGroupDescHandle, E_OUTOFMEMORY);

  LPFILEGROUPDESCRIPTORA fileGroupDescA =
      reinterpret_cast<LPFILEGROUPDESCRIPTORA>(GlobalLock(fileGroupDescHandle));
  if (!fileGroupDescA) {
    ::GlobalFree(fileGroupDescHandle);
    return E_OUTOFMEMORY;
  }

  nsAutoString wideFileName;
  HRESULT res;
  nsCOMPtr<nsIURI> sourceURI;
  res = GetDownloadDetails(getter_AddRefs(sourceURI), wideFileName);
  if (FAILED(res)) {
    ::GlobalFree(fileGroupDescHandle);
    return res;
  }

  nsAutoCString nativeFileName;
  NS_CopyUnicodeToNative(wideFileName, nativeFileName);

  strncpy(fileGroupDescA->fgd[0].cFileName, nativeFileName.get(), MAX_PATH - 1);
  fileGroupDescA->fgd[0].cFileName[MAX_PATH - 1] = '\0';

  // one file in the file block
  fileGroupDescA->cItems = 1;
  fileGroupDescA->fgd[0].dwFlags = FD_PROGRESSUI;

  GlobalUnlock(fileGroupDescHandle);
  aSTG.hGlobal = fileGroupDescHandle;
  aSTG.tymed = TYMED_HGLOBAL;

  return S_OK;
}

HRESULT nsDataObj::GetFileDescriptor_IStreamW(FORMATETC& aFE, STGMEDIUM& aSTG) {
  HGLOBAL fileGroupDescHandle =
      ::GlobalAlloc(GMEM_ZEROINIT | GMEM_SHARE, sizeof(FILEGROUPDESCRIPTORW));
  NS_ENSURE_TRUE(fileGroupDescHandle, E_OUTOFMEMORY);

  LPFILEGROUPDESCRIPTORW fileGroupDescW =
      reinterpret_cast<LPFILEGROUPDESCRIPTORW>(GlobalLock(fileGroupDescHandle));
  if (!fileGroupDescW) {
    ::GlobalFree(fileGroupDescHandle);
    return E_OUTOFMEMORY;
  }

  nsAutoString wideFileName;
  HRESULT res;
  nsCOMPtr<nsIURI> sourceURI;
  res = GetDownloadDetails(getter_AddRefs(sourceURI), wideFileName);
  if (FAILED(res)) {
    ::GlobalFree(fileGroupDescHandle);
    return res;
  }

  wcsncpy(fileGroupDescW->fgd[0].cFileName, wideFileName.get(), MAX_PATH - 1);
  fileGroupDescW->fgd[0].cFileName[MAX_PATH - 1] = '\0';
  // one file in the file block
  fileGroupDescW->cItems = 1;
  fileGroupDescW->fgd[0].dwFlags = FD_PROGRESSUI;

  GlobalUnlock(fileGroupDescHandle);
  aSTG.hGlobal = fileGroupDescHandle;
  aSTG.tymed = TYMED_HGLOBAL;

  return S_OK;
}

HRESULT nsDataObj::GetFileContents_IStream(FORMATETC& aFE, STGMEDIUM& aSTG) {
  IStream* pStream = nullptr;

  nsDataObj::CreateStream(&pStream);
  NS_ENSURE_TRUE(pStream, E_FAIL);

  aSTG.tymed = TYMED_ISTREAM;
  aSTG.pstm = pStream;
  aSTG.pUnkForRelease = nullptr;

  return S_OK;
}
