/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"
#include "mozilla/TextUtils.h"

#include "nspr.h"

#include "nsIFileStreams.h"  // New Necko file streams
#include <algorithm>

#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsIClassOfService.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPrivateBrowsingChannel.h"
#include "nsComponentManagerUtils.h"
#include "nsIStorageStream.h"
#include "nsISeekableStream.h"
#include "nsIHttpChannel.h"
#include "nsIEncodedChannel.h"
#include "nsIUploadChannel.h"
#include "nsICacheInfoChannel.h"
#include "nsIFileChannel.h"
#include "nsEscape.h"
#include "nsIStringEnumerator.h"
#include "nsStreamUtils.h"

#include "nsCExternalHandlerService.h"

#include "nsIURL.h"
#include "nsIFileURL.h"
#include "nsIWebProgressListener.h"
#include "nsIAuthPrompt.h"
#include "nsIPrompt.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsContentUtils.h"

#include "nsIStringBundle.h"
#include "nsIProtocolHandler.h"

#include "nsWebBrowserPersist.h"
#include "WebBrowserPersistLocalDocument.h"

#include "nsIContent.h"
#include "nsIMIMEInfo.h"
#include "mozilla/dom/Document.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/Mutex.h"
#include "mozilla/Printf.h"
#include "ReferrerInfo.h"
#include "nsIURIMutator.h"
#include "mozilla/WebBrowserPersistDocumentParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/PContentParent.h"
#include "mozilla/dom/BrowserParent.h"
#include "nsIDocumentEncoder.h"

using namespace mozilla;
using namespace mozilla::dom;

// Buffer file writes in 32kb chunks
#define BUFFERED_OUTPUT_SIZE (1024 * 32)

struct nsWebBrowserPersist::WalkData {
  nsCOMPtr<nsIWebBrowserPersistDocument> mDocument;
  nsCOMPtr<nsIURI> mFile;
  nsCOMPtr<nsIURI> mDataPath;
};

// Information about a DOM document
struct nsWebBrowserPersist::DocData {
  nsCOMPtr<nsIURI> mBaseURI;
  nsCOMPtr<nsIWebBrowserPersistDocument> mDocument;
  nsCOMPtr<nsIURI> mFile;
  nsCString mCharset;
};

// Information about a URI
struct nsWebBrowserPersist::URIData {
  bool mNeedsPersisting;
  bool mSaved;
  bool mIsSubFrame;
  bool mDataPathIsRelative;
  bool mNeedsFixup;
  nsString mFilename;
  nsString mSubFrameExt;
  nsCOMPtr<nsIURI> mFile;
  nsCOMPtr<nsIURI> mDataPath;
  nsCOMPtr<nsIURI> mRelativeDocumentURI;
  nsCOMPtr<nsIPrincipal> mTriggeringPrincipal;
  nsCOMPtr<nsICookieJarSettings> mCookieJarSettings;
  nsContentPolicyType mContentPolicyType;
  nsCString mRelativePathToData;
  nsCString mCharset;

  nsresult GetLocalURI(nsIURI* targetBaseURI, nsCString& aSpecOut);
};

// Information about the output stream
// Note that this data structure (and the map that nsWebBrowserPersist keeps,
// where these are values) is used from two threads: the main thread,
// and the background task thread.
// The background thread only writes to mStream (from OnDataAvailable), and
// this access is guarded using mStreamMutex. It reads the mFile member, which
// is only written to on the main thread when the object is constructed and
// from OnStartRequest (if mCalcFileExt), both guaranteed to happen before
// OnDataAvailable is fired.
// The main thread gets OnStartRequest, OnStopRequest, and progress sink events,
// and accesses the other members.
struct nsWebBrowserPersist::OutputData {
  nsCOMPtr<nsIURI> mFile;
  nsCOMPtr<nsIURI> mOriginalLocation;
  nsCOMPtr<nsIOutputStream> mStream;
  Mutex mStreamMutex MOZ_UNANNOTATED;
  int64_t mSelfProgress;
  int64_t mSelfProgressMax;
  bool mCalcFileExt;

  OutputData(nsIURI* aFile, nsIURI* aOriginalLocation, bool aCalcFileExt)
      : mFile(aFile),
        mOriginalLocation(aOriginalLocation),
        mStreamMutex("nsWebBrowserPersist::OutputData::mStreamMutex"),
        mSelfProgress(0),
        mSelfProgressMax(10000),
        mCalcFileExt(aCalcFileExt) {}
  ~OutputData() {
    // Gaining this lock in the destructor is pretty icky. It should be OK
    // because the only other place we lock the mutex is in OnDataAvailable,
    // which will never itself cause the OutputData instance to be
    // destroyed.
    MutexAutoLock lock(mStreamMutex);
    if (mStream) {
      mStream->Close();
    }
  }
};

struct nsWebBrowserPersist::UploadData {
  nsCOMPtr<nsIURI> mFile;
  int64_t mSelfProgress;
  int64_t mSelfProgressMax;

  explicit UploadData(nsIURI* aFile)
      : mFile(aFile), mSelfProgress(0), mSelfProgressMax(10000) {}
};

struct nsWebBrowserPersist::CleanupData {
  nsCOMPtr<nsIFile> mFile;
  // Snapshot of what the file actually is at the time of creation so that if
  // it transmutes into something else later on it can be ignored. For example,
  // catch files that turn into dirs or vice versa.
  bool mIsDirectory;
};

class nsWebBrowserPersist::OnWalk final
    : public nsIWebBrowserPersistResourceVisitor {
 public:
  OnWalk(nsWebBrowserPersist* aParent, nsIURI* aFile, nsIFile* aDataPath)
      : mParent(aParent),
        mFile(aFile),
        mDataPath(aDataPath),
        mPendingDocuments(1),
        mStatus(NS_OK) {}

  NS_DECL_NSIWEBBROWSERPERSISTRESOURCEVISITOR
  NS_DECL_ISUPPORTS
 private:
  RefPtr<nsWebBrowserPersist> mParent;
  nsCOMPtr<nsIURI> mFile;
  nsCOMPtr<nsIFile> mDataPath;

  uint32_t mPendingDocuments;
  nsresult mStatus;

  virtual ~OnWalk() = default;
};

NS_IMPL_ISUPPORTS(nsWebBrowserPersist::OnWalk,
                  nsIWebBrowserPersistResourceVisitor)

class nsWebBrowserPersist::OnRemoteWalk final
    : public nsIWebBrowserPersistDocumentReceiver {
 public:
  OnRemoteWalk(nsIWebBrowserPersistResourceVisitor* aVisitor,
               nsIWebBrowserPersistDocument* aDocument)
      : mVisitor(aVisitor), mDocument(aDocument) {}

  NS_DECL_NSIWEBBROWSERPERSISTDOCUMENTRECEIVER
  NS_DECL_ISUPPORTS
 private:
  nsCOMPtr<nsIWebBrowserPersistResourceVisitor> mVisitor;
  nsCOMPtr<nsIWebBrowserPersistDocument> mDocument;

  virtual ~OnRemoteWalk() = default;
};

NS_IMPL_ISUPPORTS(nsWebBrowserPersist::OnRemoteWalk,
                  nsIWebBrowserPersistDocumentReceiver)

class nsWebBrowserPersist::OnWrite final
    : public nsIWebBrowserPersistWriteCompletion {
 public:
  OnWrite(nsWebBrowserPersist* aParent, nsIURI* aFile, nsIFile* aLocalFile)
      : mParent(aParent), mFile(aFile), mLocalFile(aLocalFile) {}

  NS_DECL_NSIWEBBROWSERPERSISTWRITECOMPLETION
  NS_DECL_ISUPPORTS
 private:
  RefPtr<nsWebBrowserPersist> mParent;
  nsCOMPtr<nsIURI> mFile;
  nsCOMPtr<nsIFile> mLocalFile;

  virtual ~OnWrite() = default;
};

NS_IMPL_ISUPPORTS(nsWebBrowserPersist::OnWrite,
                  nsIWebBrowserPersistWriteCompletion)

class nsWebBrowserPersist::FlatURIMap final
    : public nsIWebBrowserPersistURIMap {
 public:
  explicit FlatURIMap(const nsACString& aTargetBase)
      : mTargetBase(aTargetBase) {}

  void Add(const nsACString& aMapFrom, const nsACString& aMapTo) {
    mMapFrom.AppendElement(aMapFrom);
    mMapTo.AppendElement(aMapTo);
  }

  NS_DECL_NSIWEBBROWSERPERSISTURIMAP
  NS_DECL_ISUPPORTS

 private:
  nsTArray<nsCString> mMapFrom;
  nsTArray<nsCString> mMapTo;
  nsCString mTargetBase;

  virtual ~FlatURIMap() = default;
};

NS_IMPL_ISUPPORTS(nsWebBrowserPersist::FlatURIMap, nsIWebBrowserPersistURIMap)

NS_IMETHODIMP
nsWebBrowserPersist::FlatURIMap::GetNumMappedURIs(uint32_t* aNum) {
  MOZ_ASSERT(mMapFrom.Length() == mMapTo.Length());
  *aNum = mMapTo.Length();
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersist::FlatURIMap::GetTargetBaseURI(nsACString& aTargetBase) {
  aTargetBase = mTargetBase;
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersist::FlatURIMap::GetURIMapping(uint32_t aIndex,
                                               nsACString& aMapFrom,
                                               nsACString& aMapTo) {
  MOZ_ASSERT(mMapFrom.Length() == mMapTo.Length());
  if (aIndex >= mMapTo.Length()) {
    return NS_ERROR_INVALID_ARG;
  }
  aMapFrom = mMapFrom[aIndex];
  aMapTo = mMapTo[aIndex];
  return NS_OK;
}

// Maximum file length constant. The max file name length is
// volume / server dependent but it is difficult to obtain
// that information. Instead this constant is a reasonable value that
// modern systems should able to cope with.
const uint32_t kDefaultMaxFilenameLength = 64;

// Default flags for persistence
const uint32_t kDefaultPersistFlags =
    nsIWebBrowserPersist::PERSIST_FLAGS_NO_CONVERSION |
    nsIWebBrowserPersist::PERSIST_FLAGS_REPLACE_EXISTING_FILES;

// String bundle where error messages come from
const char* kWebBrowserPersistStringBundle =
    "chrome://global/locale/nsWebBrowserPersist.properties";

nsWebBrowserPersist::nsWebBrowserPersist()
    : mCurrentDataPathIsRelative(false),
      mCurrentThingsToPersist(0),
      mOutputMapMutex("nsWebBrowserPersist::mOutputMapMutex"),
      mFirstAndOnlyUse(true),
      mSavingDocument(false),
      mCancel(false),
      mEndCalled(false),
      mCompleted(false),
      mStartSaving(false),
      mReplaceExisting(true),
      mSerializingOutput(false),
      mIsPrivate(false),
      mPersistFlags(kDefaultPersistFlags),
      mPersistResult(NS_OK),
      mTotalCurrentProgress(0),
      mTotalMaxProgress(0),
      mWrapColumn(72),
      mEncodingFlags(0) {}

nsWebBrowserPersist::~nsWebBrowserPersist() { Cleanup(); }

//*****************************************************************************
// nsWebBrowserPersist::nsISupports
//*****************************************************************************

NS_IMPL_ADDREF(nsWebBrowserPersist)
NS_IMPL_RELEASE(nsWebBrowserPersist)

NS_INTERFACE_MAP_BEGIN(nsWebBrowserPersist)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIWebBrowserPersist)
  NS_INTERFACE_MAP_ENTRY(nsIWebBrowserPersist)
  NS_INTERFACE_MAP_ENTRY(nsICancelable)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIThreadRetargetableStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
  NS_INTERFACE_MAP_ENTRY(nsIProgressEventSink)
NS_INTERFACE_MAP_END

//*****************************************************************************
// nsWebBrowserPersist::nsIInterfaceRequestor
//*****************************************************************************

NS_IMETHODIMP nsWebBrowserPersist::GetInterface(const nsIID& aIID,
                                                void** aIFace) {
  NS_ENSURE_ARG_POINTER(aIFace);

  *aIFace = nullptr;

  nsresult rv = QueryInterface(aIID, aIFace);
  if (NS_SUCCEEDED(rv)) {
    return rv;
  }

  if (mProgressListener && (aIID.Equals(NS_GET_IID(nsIAuthPrompt)) ||
                            aIID.Equals(NS_GET_IID(nsIPrompt)))) {
    mProgressListener->QueryInterface(aIID, aIFace);
    if (*aIFace) return NS_OK;
  }

  nsCOMPtr<nsIInterfaceRequestor> req = do_QueryInterface(mProgressListener);
  if (req) {
    return req->GetInterface(aIID, aIFace);
  }

  return NS_ERROR_NO_INTERFACE;
}

//*****************************************************************************
// nsWebBrowserPersist::nsIWebBrowserPersist
//*****************************************************************************

NS_IMETHODIMP nsWebBrowserPersist::GetPersistFlags(uint32_t* aPersistFlags) {
  NS_ENSURE_ARG_POINTER(aPersistFlags);
  *aPersistFlags = mPersistFlags;
  return NS_OK;
}
NS_IMETHODIMP nsWebBrowserPersist::SetPersistFlags(uint32_t aPersistFlags) {
  mPersistFlags = aPersistFlags;
  mReplaceExisting = (mPersistFlags & PERSIST_FLAGS_REPLACE_EXISTING_FILES);
  mSerializingOutput = (mPersistFlags & PERSIST_FLAGS_SERIALIZE_OUTPUT);
  return NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::GetCurrentState(uint32_t* aCurrentState) {
  NS_ENSURE_ARG_POINTER(aCurrentState);
  if (mCompleted) {
    *aCurrentState = PERSIST_STATE_FINISHED;
  } else if (mFirstAndOnlyUse) {
    *aCurrentState = PERSIST_STATE_SAVING;
  } else {
    *aCurrentState = PERSIST_STATE_READY;
  }
  return NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::GetResult(nsresult* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = mPersistResult;
  return NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::GetProgressListener(
    nsIWebProgressListener** aProgressListener) {
  NS_ENSURE_ARG_POINTER(aProgressListener);
  *aProgressListener = mProgressListener;
  NS_IF_ADDREF(*aProgressListener);
  return NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::SetProgressListener(
    nsIWebProgressListener* aProgressListener) {
  mProgressListener = aProgressListener;
  mProgressListener2 = do_QueryInterface(aProgressListener);
  mEventSink = do_GetInterface(aProgressListener);
  return NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::SaveURI(
    nsIURI* aURI, nsIPrincipal* aPrincipal, uint32_t aCacheKey,
    nsIReferrerInfo* aReferrerInfo, nsICookieJarSettings* aCookieJarSettings,
    nsIInputStream* aPostData, const char* aExtraHeaders, nsISupports* aFile,
    nsContentPolicyType aContentPolicy, bool aIsPrivate) {
  NS_ENSURE_TRUE(mFirstAndOnlyUse, NS_ERROR_FAILURE);
  mFirstAndOnlyUse = false;  // Stop people from reusing this object!

  nsCOMPtr<nsIURI> fileAsURI;
  nsresult rv;
  rv = GetValidURIFromObject(aFile, getter_AddRefs(fileAsURI));
  NS_ENSURE_SUCCESS(rv, NS_ERROR_INVALID_ARG);

  // SaveURIInternal doesn't like broken uris.
  mPersistFlags |= PERSIST_FLAGS_FAIL_ON_BROKEN_LINKS;
  rv = SaveURIInternal(aURI, aPrincipal, aContentPolicy, aCacheKey,
                       aReferrerInfo, aCookieJarSettings, aPostData,
                       aExtraHeaders, fileAsURI, false, aIsPrivate);
  return NS_FAILED(rv) ? rv : NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::SaveChannel(nsIChannel* aChannel,
                                               nsISupports* aFile) {
  NS_ENSURE_TRUE(mFirstAndOnlyUse, NS_ERROR_FAILURE);
  mFirstAndOnlyUse = false;  // Stop people from reusing this object!

  nsCOMPtr<nsIURI> fileAsURI;
  nsresult rv;
  rv = GetValidURIFromObject(aFile, getter_AddRefs(fileAsURI));
  NS_ENSURE_SUCCESS(rv, NS_ERROR_INVALID_ARG);

  rv = aChannel->GetURI(getter_AddRefs(mURI));
  NS_ENSURE_SUCCESS(rv, rv);

  // SaveChannelInternal doesn't like broken uris.
  mPersistFlags |= PERSIST_FLAGS_FAIL_ON_BROKEN_LINKS;
  rv = SaveChannelInternal(aChannel, fileAsURI, false);
  return NS_FAILED(rv) ? rv : NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::SaveDocument(nsISupports* aDocument,
                                                nsISupports* aFile,
                                                nsISupports* aDataPath,
                                                const char* aOutputContentType,
                                                uint32_t aEncodingFlags,
                                                uint32_t aWrapColumn) {
  NS_ENSURE_TRUE(mFirstAndOnlyUse, NS_ERROR_FAILURE);
  mFirstAndOnlyUse = false;  // Stop people from reusing this object!

  // We need a STATE_IS_NETWORK start/stop pair to bracket the
  // notification callbacks.  For a whole document we generate those
  // here and in EndDownload(), but for the single-request methods
  // that's done in On{Start,Stop}Request instead.
  mSavingDocument = true;

  NS_ENSURE_ARG_POINTER(aDocument);
  NS_ENSURE_ARG_POINTER(aFile);

  nsCOMPtr<nsIURI> fileAsURI;
  nsCOMPtr<nsIURI> datapathAsURI;
  nsresult rv;

  rv = GetValidURIFromObject(aFile, getter_AddRefs(fileAsURI));
  NS_ENSURE_SUCCESS(rv, NS_ERROR_INVALID_ARG);
  if (aDataPath) {
    rv = GetValidURIFromObject(aDataPath, getter_AddRefs(datapathAsURI));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_INVALID_ARG);
  }

  mWrapColumn = aWrapColumn;
  mEncodingFlags = aEncodingFlags;

  if (aOutputContentType) {
    mContentType.AssignASCII(aOutputContentType);
  }

  // State start notification
  if (mProgressListener) {
    mProgressListener->OnStateChange(
        nullptr, nullptr,
        nsIWebProgressListener::STATE_START |
            nsIWebProgressListener::STATE_IS_NETWORK,
        NS_OK);
  }

  nsCOMPtr<nsIWebBrowserPersistDocument> doc = do_QueryInterface(aDocument);
  if (!doc) {
    nsCOMPtr<Document> localDoc = do_QueryInterface(aDocument);
    if (localDoc) {
      doc = new mozilla::WebBrowserPersistLocalDocument(localDoc);
    } else {
      rv = NS_ERROR_NO_INTERFACE;
    }
  }

  bool closed = false;
  if (doc && NS_SUCCEEDED(doc->GetIsClosed(&closed)) && !closed) {
    rv = SaveDocumentInternal(doc, fileAsURI, datapathAsURI);
  }

  if (NS_FAILED(rv) || closed) {
    SendErrorStatusChange(true, rv, nullptr, mURI);
    EndDownload(rv);
  }
  return rv;
}

NS_IMETHODIMP nsWebBrowserPersist::Cancel(nsresult aReason) {
  // No point cancelling if we're already complete.
  if (mEndCalled) {
    return NS_OK;
  }
  mCancel = true;
  EndDownload(aReason);
  return NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::CancelSave() {
  return Cancel(NS_BINDING_ABORTED);
}

nsresult nsWebBrowserPersist::StartUpload(nsIStorageStream* storStream,
                                          nsIURI* aDestinationURI,
                                          const nsACString& aContentType) {
  // setup the upload channel if the destination is not local
  nsCOMPtr<nsIInputStream> inputstream;
  nsresult rv = storStream->NewInputStream(0, getter_AddRefs(inputstream));
  NS_ENSURE_TRUE(inputstream, NS_ERROR_FAILURE);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
  return StartUpload(inputstream, aDestinationURI, aContentType);
}

nsresult nsWebBrowserPersist::StartUpload(nsIInputStream* aInputStream,
                                          nsIURI* aDestinationURI,
                                          const nsACString& aContentType) {
  nsCOMPtr<nsIChannel> destChannel;
  CreateChannelFromURI(aDestinationURI, getter_AddRefs(destChannel));
  nsCOMPtr<nsIUploadChannel> uploadChannel(do_QueryInterface(destChannel));
  NS_ENSURE_TRUE(uploadChannel, NS_ERROR_FAILURE);

  // Set the upload stream
  // NOTE: ALL data must be available in "inputstream"
  nsresult rv = uploadChannel->SetUploadStream(aInputStream, aContentType, -1);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
  rv = destChannel->AsyncOpen(this);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  // add this to the upload list
  nsCOMPtr<nsISupports> keyPtr = do_QueryInterface(destChannel);
  mUploadList.InsertOrUpdate(keyPtr, MakeUnique<UploadData>(aDestinationURI));

  return NS_OK;
}

void nsWebBrowserPersist::SerializeNextFile() {
  nsresult rv = NS_OK;
  MOZ_ASSERT(mWalkStack.Length() == 0);

  // First, handle gathered URIs.
  // This is potentially O(n^2), when taking into account the
  // number of times this method is called.  If it becomes a
  // bottleneck, the count of not-yet-persisted URIs could be
  // maintained separately, and we can skip iterating mURIMap if there are none.

  // Persist each file in the uri map. The document(s)
  // will be saved after the last one of these is saved.
  for (const auto& entry : mURIMap) {
    URIData* data = entry.GetWeak();

    if (!data->mNeedsPersisting || data->mSaved) {
      continue;
    }

    // Create a URI from the key.
    nsCOMPtr<nsIURI> uri;
    rv = NS_NewURI(getter_AddRefs(uri), entry.GetKey(), data->mCharset.get());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      break;
    }

    // Make a URI to save the data to.
    nsCOMPtr<nsIURI> fileAsURI = data->mDataPath;
    rv = AppendPathToURI(fileAsURI, data->mFilename, fileAsURI);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      break;
    }

    rv = SaveURIInternal(uri, data->mTriggeringPrincipal,
                         data->mContentPolicyType, 0, nullptr,
                         data->mCookieJarSettings, nullptr, nullptr, fileAsURI,
                         true, mIsPrivate);
    // If SaveURIInternal fails, then it will have called EndDownload,
    // which means that |data| is no longer valid memory. We MUST bail.
    if (NS_WARN_IF(NS_FAILED(rv))) {
      break;
    }

    if (rv == NS_OK) {
      // URIData.mFile will be updated to point to the correct
      // URI object when it is fixed up with the right file extension
      // in OnStartRequest
      data->mFile = fileAsURI;
      data->mSaved = true;
    } else {
      data->mNeedsFixup = false;
    }

    if (mSerializingOutput) {
      break;
    }
  }

  // If there are downloads happening, wait until they're done; the
  // OnStopRequest handler will call this method again.
  if (mOutputMap.Count() > 0) {
    return;
  }

  // If serializing, also wait until last upload is done.
  if (mSerializingOutput && mUploadList.Count() > 0) {
    return;
  }

  // If there are also no more documents, then we're done.
  if (mDocList.Length() == 0) {
    // ...or not quite done, if there are still uploads.
    if (mUploadList.Count() > 0) {
      return;
    }
    // Finish and clean things up.  Defer this because the caller
    // may have been expecting to use the listeners that that
    // method will clear.
    NS_DispatchToCurrentThread(
        NewRunnableMethod("nsWebBrowserPersist::FinishDownload", this,
                          &nsWebBrowserPersist::FinishDownload));
    return;
  }

  // There are no URIs to save, so just save the next document.
  mStartSaving = true;
  mozilla::UniquePtr<DocData> docData(mDocList.ElementAt(0));
  mDocList.RemoveElementAt(0);  // O(n^2) but probably doesn't matter.
  MOZ_ASSERT(docData);
  if (!docData) {
    EndDownload(NS_ERROR_FAILURE);
    return;
  }

  mCurrentBaseURI = docData->mBaseURI;
  mCurrentCharset = docData->mCharset;
  mTargetBaseURI = docData->mFile;

  // Save the document, fixing it up with the new URIs as we do

  nsAutoCString targetBaseSpec;
  if (mTargetBaseURI) {
    rv = mTargetBaseURI->GetSpec(targetBaseSpec);
    if (NS_FAILED(rv)) {
      SendErrorStatusChange(true, rv, nullptr, nullptr);
      EndDownload(rv);
      return;
    }
  }

  // mFlatURIMap must be rebuilt each time through SerializeNextFile, as
  // mTargetBaseURI is used to create the relative URLs and will be different
  // with each serialized document.
  RefPtr<FlatURIMap> flatMap = new FlatURIMap(targetBaseSpec);
  for (const auto& uriEntry : mURIMap) {
    nsAutoCString mapTo;
    nsresult rv = uriEntry.GetWeak()->GetLocalURI(mTargetBaseURI, mapTo);
    if (NS_SUCCEEDED(rv) || !mapTo.IsVoid()) {
      flatMap->Add(uriEntry.GetKey(), mapTo);
    }
  }
  mFlatURIMap = std::move(flatMap);

  nsCOMPtr<nsIFile> localFile;
  GetLocalFileFromURI(docData->mFile, getter_AddRefs(localFile));
  if (localFile) {
    // if we're not replacing an existing file but the file
    // exists, something is wrong
    bool fileExists = false;
    rv = localFile->Exists(&fileExists);
    if (NS_SUCCEEDED(rv) && !mReplaceExisting && fileExists) {
      rv = NS_ERROR_FILE_ALREADY_EXISTS;
    }
    if (NS_FAILED(rv)) {
      SendErrorStatusChange(false, rv, nullptr, docData->mFile);
      EndDownload(rv);
      return;
    }
  }
  nsCOMPtr<nsIOutputStream> outputStream;
  rv = MakeOutputStream(docData->mFile, getter_AddRefs(outputStream));
  if (NS_SUCCEEDED(rv) && !outputStream) {
    rv = NS_ERROR_FAILURE;
  }
  if (NS_FAILED(rv)) {
    SendErrorStatusChange(false, rv, nullptr, docData->mFile);
    EndDownload(rv);
    return;
  }

  RefPtr<OnWrite> finish = new OnWrite(this, docData->mFile, localFile);
  rv = docData->mDocument->WriteContent(outputStream, mFlatURIMap,
                                        NS_ConvertUTF16toUTF8(mContentType),
                                        mEncodingFlags, mWrapColumn, finish);
  if (NS_FAILED(rv)) {
    SendErrorStatusChange(false, rv, nullptr, docData->mFile);
    EndDownload(rv);
  }
}

NS_IMETHODIMP
nsWebBrowserPersist::OnWrite::OnFinish(nsIWebBrowserPersistDocument* aDoc,
                                       nsIOutputStream* aStream,
                                       const nsACString& aContentType,
                                       nsresult aStatus) {
  nsresult rv = aStatus;

  if (NS_FAILED(rv)) {
    mParent->SendErrorStatusChange(false, rv, nullptr, mFile);
    mParent->EndDownload(rv);
    return NS_OK;
  }
  if (!mLocalFile) {
    nsCOMPtr<nsIStorageStream> storStream(do_QueryInterface(aStream));
    if (storStream) {
      aStream->Close();
      rv = mParent->StartUpload(storStream, mFile, aContentType);
      if (NS_FAILED(rv)) {
        mParent->SendErrorStatusChange(false, rv, nullptr, mFile);
        mParent->EndDownload(rv);
      }
      // Either we failed and we're done, or we're uploading and
      // the OnStopRequest callback is responsible for the next
      // SerializeNextFile().
      return NS_OK;
    }
  }
  NS_DispatchToCurrentThread(
      NewRunnableMethod("nsWebBrowserPersist::SerializeNextFile", mParent,
                        &nsWebBrowserPersist::SerializeNextFile));
  return NS_OK;
}

//*****************************************************************************
// nsWebBrowserPersist::nsIRequestObserver
//*****************************************************************************

NS_IMETHODIMP nsWebBrowserPersist::OnStartRequest(nsIRequest* request) {
  if (mProgressListener) {
    uint32_t stateFlags = nsIWebProgressListener::STATE_START |
                          nsIWebProgressListener::STATE_IS_REQUEST;
    if (!mSavingDocument) {
      stateFlags |= nsIWebProgressListener::STATE_IS_NETWORK;
    }
    mProgressListener->OnStateChange(nullptr, request, stateFlags, NS_OK);
  }

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  NS_ENSURE_TRUE(channel, NS_ERROR_FAILURE);

  nsCOMPtr<nsISupports> keyPtr = do_QueryInterface(request);
  OutputData* data = mOutputMap.Get(keyPtr);

  // NOTE: This code uses the channel as a hash key so it will not
  //       recognize redirected channels because the key is not the same.
  //       When that happens we remove and add the data entry to use the
  //       new channel as the hash key.
  if (!data) {
    UploadData* upData = mUploadList.Get(keyPtr);
    if (!upData) {
      // Redirect? Try and fixup the output table
      nsresult rv = FixRedirectedChannelEntry(channel);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

      // Should be able to find the data after fixup unless redirects
      // are disabled.
      data = mOutputMap.Get(keyPtr);
      if (!data) {
        return NS_ERROR_FAILURE;
      }
    }
  }

  if (data && data->mFile) {
    nsCOMPtr<nsIThreadRetargetableRequest> r = do_QueryInterface(request);
    // Determine if we're uploading. Only use OMT onDataAvailable if not.
    nsCOMPtr<nsIFile> localFile;
    GetLocalFileFromURI(data->mFile, getter_AddRefs(localFile));
    if (r && localFile) {
      if (!mBackgroundQueue) {
        NS_CreateBackgroundTaskQueue("WebBrowserPersist",
                                     getter_AddRefs(mBackgroundQueue));
      }
      if (mBackgroundQueue) {
        r->RetargetDeliveryTo(mBackgroundQueue);
      }
    }

    // If PERSIST_FLAGS_AUTODETECT_APPLY_CONVERSION is set in mPersistFlags,
    // try to determine whether this channel needs to apply Content-Encoding
    // conversions.
    NS_ASSERTION(
        !((mPersistFlags & PERSIST_FLAGS_AUTODETECT_APPLY_CONVERSION) &&
          (mPersistFlags & PERSIST_FLAGS_NO_CONVERSION)),
        "Conflict in persist flags: both AUTODETECT and NO_CONVERSION set");
    if (mPersistFlags & PERSIST_FLAGS_AUTODETECT_APPLY_CONVERSION)
      SetApplyConversionIfNeeded(channel);

    if (data->mCalcFileExt &&
        !(mPersistFlags & PERSIST_FLAGS_DONT_CHANGE_FILENAMES)) {
      nsCOMPtr<nsIURI> uriWithExt;
      // this is the first point at which the server can tell us the mimetype
      nsresult rv = CalculateAndAppendFileExt(
          data->mFile, channel, data->mOriginalLocation, uriWithExt);
      if (NS_SUCCEEDED(rv)) {
        data->mFile = uriWithExt;
      }

      // now make filename conformant and unique
      nsCOMPtr<nsIURI> uniqueFilenameURI;
      rv = CalculateUniqueFilename(data->mFile, uniqueFilenameURI);
      if (NS_SUCCEEDED(rv)) {
        data->mFile = uniqueFilenameURI;
      }

      // The URIData entry is pointing to the old unfixed URI, so we need
      // to update it.
      nsCOMPtr<nsIURI> chanURI;
      rv = channel->GetOriginalURI(getter_AddRefs(chanURI));
      if (NS_SUCCEEDED(rv)) {
        nsAutoCString spec;
        chanURI->GetSpec(spec);
        URIData* uridata;
        if (mURIMap.Get(spec, &uridata)) {
          uridata->mFile = data->mFile;
        }
      }
    }

    // compare uris and bail before we add to output map if they are equal
    bool isEqual = false;
    if (NS_SUCCEEDED(data->mFile->Equals(data->mOriginalLocation, &isEqual)) &&
        isEqual) {
      {
        MutexAutoLock lock(mOutputMapMutex);
        // remove from output map
        mOutputMap.Remove(keyPtr);
      }

      // cancel; we don't need to know any more
      // stop request will get called
      request->Cancel(NS_BINDING_ABORTED);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::OnStopRequest(nsIRequest* request,
                                                 nsresult status) {
  nsCOMPtr<nsISupports> keyPtr = do_QueryInterface(request);
  OutputData* data = mOutputMap.Get(keyPtr);
  if (data) {
    if (NS_SUCCEEDED(mPersistResult) && NS_FAILED(status)) {
      SendErrorStatusChange(true, status, request, data->mFile);
    }

    // If there is a stream ref and we weren't canceled,
    // close it away from the main thread.
    // We don't do this when there's an error/cancelation,
    // because our consumer may try to delete the file, which will error
    // if we're still holding on to it, so we have to close it pronto.
    {
      MutexAutoLock lock(data->mStreamMutex);
      if (data->mStream && NS_SUCCEEDED(status) && !mCancel) {
        if (!mBackgroundQueue) {
          nsresult rv = NS_CreateBackgroundTaskQueue(
              "WebBrowserPersist", getter_AddRefs(mBackgroundQueue));
          if (NS_FAILED(rv)) {
            return rv;
          }
        }
        // Now steal the stream ref and close it away from the main thread,
        // keeping the promise around so we don't finish before all files
        // are flushed and closed.
        mFileClosePromises.AppendElement(InvokeAsync(
            mBackgroundQueue, __func__, [stream = std::move(data->mStream)]() {
              nsresult rv = stream->Close();
              // We don't care if closing failed; we don't care in the
              // destructor either...
              return ClosePromise::CreateAndResolve(rv, __func__);
            }));
      }
    }
    MutexAutoLock lock(mOutputMapMutex);
    mOutputMap.Remove(keyPtr);
  } else {
    // if we didn't find the data in mOutputMap, try mUploadList
    UploadData* upData = mUploadList.Get(keyPtr);
    if (upData) {
      mUploadList.Remove(keyPtr);
    }
  }

  // Do more work.
  SerializeNextFile();

  if (mProgressListener) {
    uint32_t stateFlags = nsIWebProgressListener::STATE_STOP |
                          nsIWebProgressListener::STATE_IS_REQUEST;
    if (!mSavingDocument) {
      stateFlags |= nsIWebProgressListener::STATE_IS_NETWORK;
    }
    mProgressListener->OnStateChange(nullptr, request, stateFlags, status);
  }

  return NS_OK;
}

//*****************************************************************************
// nsWebBrowserPersist::nsIStreamListener
//*****************************************************************************

// Note: this is supposed to (but not guaranteed to) fire on a background
// thread when used to save to local disk (channels not using local files will
// use the main thread).
// (Read) Access to mOutputMap is guarded via mOutputMapMutex.
// Access to individual OutputData::mStream is guarded via its mStreamMutex.
// mCancel is atomic, as is mPersistFlags (accessed via MakeOutputStream).
// If you end up touching this method and needing other member access, bear
// this in mind.
NS_IMETHODIMP
nsWebBrowserPersist::OnDataAvailable(nsIRequest* request,
                                     nsIInputStream* aIStream, uint64_t aOffset,
                                     uint32_t aLength) {
  // MOZ_ASSERT(!NS_IsMainThread()); // no guarantees, but it's likely.

  bool cancel = mCancel;
  if (!cancel) {
    nsresult rv = NS_OK;
    uint32_t bytesRemaining = aLength;

    nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
    NS_ENSURE_TRUE(channel, NS_ERROR_FAILURE);

    MutexAutoLock lock(mOutputMapMutex);
    nsCOMPtr<nsISupports> keyPtr = do_QueryInterface(request);
    OutputData* data = mOutputMap.Get(keyPtr);
    if (!data) {
      // might be uploadData; consume necko's buffer and bail...
      uint32_t n;
      return aIStream->ReadSegments(NS_DiscardSegment, nullptr, aLength, &n);
    }

    bool readError = true;

    MutexAutoLock streamLock(data->mStreamMutex);
    // Make the output stream
    if (!data->mStream) {
      rv = MakeOutputStream(data->mFile, getter_AddRefs(data->mStream));
      if (NS_FAILED(rv)) {
        readError = false;
        cancel = true;
      }
    }

    // Read data from the input and write to the output
    char buffer[8192];
    uint32_t bytesRead;
    while (!cancel && bytesRemaining) {
      readError = true;
      rv = aIStream->Read(buffer,
                          std::min(uint32_t(sizeof(buffer)), bytesRemaining),
                          &bytesRead);
      if (NS_SUCCEEDED(rv)) {
        readError = false;
        // Write out the data until something goes wrong, or, it is
        // all written.  We loop because for some errors (e.g., disk
        // full), we get NS_OK with some bytes written, then an error.
        // So, we want to write again in that case to get the actual
        // error code.
        const char* bufPtr = buffer;  // Where to write from.
        while (NS_SUCCEEDED(rv) && bytesRead) {
          uint32_t bytesWritten = 0;
          rv = data->mStream->Write(bufPtr, bytesRead, &bytesWritten);
          if (NS_SUCCEEDED(rv)) {
            bytesRead -= bytesWritten;
            bufPtr += bytesWritten;
            bytesRemaining -= bytesWritten;
            // Force an error if (for some reason) we get NS_OK but
            // no bytes written.
            if (!bytesWritten) {
              rv = NS_ERROR_FAILURE;
              cancel = true;
            }
          } else {
            // Disaster - can't write out the bytes - disk full / permission?
            cancel = true;
          }
        }
      } else {
        // Disaster - can't read the bytes - broken link / file error?
        cancel = true;
      }
    }

    int64_t channelContentLength = -1;
    if (!cancel &&
        NS_SUCCEEDED(channel->GetContentLength(&channelContentLength))) {
      // if we get -1 at this point, we didn't get content-length header
      // assume that we got all of the data and push what we have;
      // that's the best we can do now
      if ((-1 == channelContentLength) ||
          ((channelContentLength - (aOffset + aLength)) == 0)) {
        NS_WARNING_ASSERTION(
            channelContentLength != -1,
            "nsWebBrowserPersist::OnDataAvailable() no content length "
            "header, pushing what we have");
        // we're done with this pass; see if we need to do upload
        nsAutoCString contentType;
        channel->GetContentType(contentType);
        // if we don't have the right type of output stream then it's a local
        // file
        nsCOMPtr<nsIStorageStream> storStream(do_QueryInterface(data->mStream));
        if (storStream) {
          data->mStream->Close();
          data->mStream =
              nullptr;  // null out stream so we don't close it later
          MOZ_ASSERT(NS_IsMainThread(),
                     "Uploads should be on the main thread.");
          rv = StartUpload(storStream, data->mFile, contentType);
          if (NS_FAILED(rv)) {
            readError = false;
            cancel = true;
          }
        }
      }
    }

    // Notify listener if an error occurred.
    if (cancel) {
      RefPtr<nsIRequest> req = readError ? request : nullptr;
      nsCOMPtr<nsIURI> file = data->mFile;
      RefPtr<Runnable> errorOnMainThread = NS_NewRunnableFunction(
          "nsWebBrowserPersist::SendErrorStatusChange",
          [self = RefPtr{this}, req, file, readError, rv]() {
            self->SendErrorStatusChange(readError, rv, req, file);
          });
      NS_DispatchToMainThread(errorOnMainThread);

      // And end the download on the main thread.
      nsCOMPtr<nsIRunnable> endOnMainThread = NewRunnableMethod<nsresult>(
          "nsWebBrowserPersist::EndDownload", this,
          &nsWebBrowserPersist::EndDownload, NS_BINDING_ABORTED);
      NS_DispatchToMainThread(endOnMainThread);
    }
  }

  return cancel ? NS_BINDING_ABORTED : NS_OK;
}

//*****************************************************************************
// nsWebBrowserPersist::nsIThreadRetargetableStreamListener
//*****************************************************************************

NS_IMETHODIMP nsWebBrowserPersist::CheckListenerChain() { return NS_OK; }

NS_IMETHODIMP
nsWebBrowserPersist::OnDataFinished(nsresult) { return NS_OK; }

//*****************************************************************************
// nsWebBrowserPersist::nsIProgressEventSink
//*****************************************************************************

NS_IMETHODIMP nsWebBrowserPersist::OnProgress(nsIRequest* request,
                                              int64_t aProgress,
                                              int64_t aProgressMax) {
  if (!mProgressListener) {
    return NS_OK;
  }

  // Store the progress of this request
  nsCOMPtr<nsISupports> keyPtr = do_QueryInterface(request);
  OutputData* data = mOutputMap.Get(keyPtr);
  if (data) {
    data->mSelfProgress = aProgress;
    data->mSelfProgressMax = aProgressMax;
  } else {
    UploadData* upData = mUploadList.Get(keyPtr);
    if (upData) {
      upData->mSelfProgress = aProgress;
      upData->mSelfProgressMax = aProgressMax;
    }
  }

  // Notify listener of total progress
  CalcTotalProgress();
  if (mProgressListener2) {
    mProgressListener2->OnProgressChange64(nullptr, request, aProgress,
                                           aProgressMax, mTotalCurrentProgress,
                                           mTotalMaxProgress);
  } else {
    // have to truncate 64-bit to 32bit
    mProgressListener->OnProgressChange(
        nullptr, request, uint64_t(aProgress), uint64_t(aProgressMax),
        mTotalCurrentProgress, mTotalMaxProgress);
  }

  // If our progress listener implements nsIProgressEventSink,
  // forward the notification
  if (mEventSink) {
    mEventSink->OnProgress(request, aProgress, aProgressMax);
  }

  return NS_OK;
}

NS_IMETHODIMP nsWebBrowserPersist::OnStatus(nsIRequest* request,
                                            nsresult status,
                                            const char16_t* statusArg) {
  if (mProgressListener) {
    // We need to filter out non-error error codes.
    // Is the only NS_SUCCEEDED value NS_OK?
    switch (status) {
      case NS_NET_STATUS_RESOLVING_HOST:
      case NS_NET_STATUS_RESOLVED_HOST:
      case NS_NET_STATUS_CONNECTING_TO:
      case NS_NET_STATUS_CONNECTED_TO:
      case NS_NET_STATUS_TLS_HANDSHAKE_STARTING:
      case NS_NET_STATUS_TLS_HANDSHAKE_ENDED:
      case NS_NET_STATUS_SENDING_TO:
      case NS_NET_STATUS_RECEIVING_FROM:
      case NS_NET_STATUS_WAITING_FOR:
      case NS_NET_STATUS_READING:
      case NS_NET_STATUS_WRITING:
        break;

      default:
        // Pass other notifications (for legitimate errors) along.
        mProgressListener->OnStatusChange(nullptr, request, status, statusArg);
        break;
    }
  }

  // If our progress listener implements nsIProgressEventSink,
  // forward the notification
  if (mEventSink) {
    mEventSink->OnStatus(request, status, statusArg);
  }

  return NS_OK;
}

//*****************************************************************************
// nsWebBrowserPersist private methods
//*****************************************************************************

// Convert error info into proper message text and send OnStatusChange
// notification to the web progress listener.
nsresult nsWebBrowserPersist::SendErrorStatusChange(bool aIsReadError,
                                                    nsresult aResult,
                                                    nsIRequest* aRequest,
                                                    nsIURI* aURI) {
  NS_ENSURE_ARG_POINTER(aURI);

  if (!mProgressListener) {
    // Do nothing
    return NS_OK;
  }

  // Get the file path or spec from the supplied URI
  nsCOMPtr<nsIFile> file;
  GetLocalFileFromURI(aURI, getter_AddRefs(file));
  AutoTArray<nsString, 1> strings;
  nsresult rv;
  if (file) {
    file->GetPath(*strings.AppendElement());
  } else {
    nsAutoCString fileurl;
    rv = aURI->GetSpec(fileurl);
    NS_ENSURE_SUCCESS(rv, rv);
    CopyUTF8toUTF16(fileurl, *strings.AppendElement());
  }

  const char* msgId;
  switch (aResult) {
    case NS_ERROR_FILE_NAME_TOO_LONG:
      // File name too long.
      msgId = "fileNameTooLongError";
      break;
    case NS_ERROR_FILE_ALREADY_EXISTS:
      // File exists with same name as directory.
      msgId = "fileAlreadyExistsError";
      break;
    case NS_ERROR_FILE_NO_DEVICE_SPACE:
      // Out of space on target volume.
      msgId = "diskFull";
      break;

    case NS_ERROR_FILE_READ_ONLY:
      // Attempt to write to read/only file.
      msgId = "readOnly";
      break;

    case NS_ERROR_FILE_ACCESS_DENIED:
      // Attempt to write without sufficient permissions.
      msgId = "accessError";
      break;

    default:
      // Generic read/write error message.
      if (aIsReadError)
        msgId = "readError";
      else
        msgId = "writeError";
      break;
  }
  // Get properties file bundle and extract status string.
  nsCOMPtr<nsIStringBundleService> s =
      do_GetService(NS_STRINGBUNDLE_CONTRACTID, &rv);
  NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && s, NS_ERROR_FAILURE);

  nsCOMPtr<nsIStringBundle> bundle;
  rv = s->CreateBundle(kWebBrowserPersistStringBundle, getter_AddRefs(bundle));
  NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && bundle, NS_ERROR_FAILURE);

  nsAutoString msgText;
  rv = bundle->FormatStringFromName(msgId, strings, msgText);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  mProgressListener->OnStatusChange(nullptr, aRequest, aResult, msgText.get());

  return NS_OK;
}

nsresult nsWebBrowserPersist::GetValidURIFromObject(nsISupports* aObject,
                                                    nsIURI** aURI) const {
  NS_ENSURE_ARG_POINTER(aObject);
  NS_ENSURE_ARG_POINTER(aURI);

  nsCOMPtr<nsIFile> objAsFile = do_QueryInterface(aObject);
  if (objAsFile) {
    return NS_NewFileURI(aURI, objAsFile);
  }
  nsCOMPtr<nsIURI> objAsURI = do_QueryInterface(aObject);
  if (objAsURI) {
    *aURI = objAsURI;
    NS_ADDREF(*aURI);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

/* static */
nsresult nsWebBrowserPersist::GetLocalFileFromURI(nsIURI* aURI,
                                                  nsIFile** aLocalFile) {
  nsresult rv;

  nsCOMPtr<nsIFileURL> fileURL = do_QueryInterface(aURI, &rv);
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIFile> file;
  rv = fileURL->GetFile(getter_AddRefs(file));
  if (NS_FAILED(rv)) {
    return rv;
  }

  file.forget(aLocalFile);
  return NS_OK;
}

/* static */
nsresult nsWebBrowserPersist::AppendPathToURI(nsIURI* aURI,
                                              const nsAString& aPath,
                                              nsCOMPtr<nsIURI>& aOutURI) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsAutoCString newPath;
  nsresult rv = aURI->GetPathQueryRef(newPath);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  // Append a forward slash if necessary
  int32_t len = newPath.Length();
  if (len > 0 && newPath.CharAt(len - 1) != '/') {
    newPath.Append('/');
  }

  // Store the path back on the URI
  AppendUTF16toUTF8(aPath, newPath);

  return NS_MutateURI(aURI).SetPathQueryRef(newPath).Finalize(aOutURI);
}

nsresult nsWebBrowserPersist::SaveURIInternal(
    nsIURI* aURI, nsIPrincipal* aTriggeringPrincipal,
    nsContentPolicyType aContentPolicyType, uint32_t aCacheKey,
    nsIReferrerInfo* aReferrerInfo, nsICookieJarSettings* aCookieJarSettings,
    nsIInputStream* aPostData, const char* aExtraHeaders, nsIURI* aFile,
    bool aCalcFileExt, bool aIsPrivate) {
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(aFile);
  NS_ENSURE_ARG_POINTER(aTriggeringPrincipal);

  nsresult rv = NS_OK;

  mURI = aURI;

  nsLoadFlags loadFlags = nsIRequest::LOAD_NORMAL;
  if (mPersistFlags & PERSIST_FLAGS_BYPASS_CACHE) {
    loadFlags |= nsIRequest::LOAD_BYPASS_CACHE;
  } else if (mPersistFlags & PERSIST_FLAGS_FROM_CACHE) {
    loadFlags |= nsIRequest::LOAD_FROM_CACHE;
  }

  // If there is no cookieJarSetting given, we need to create a new
  // cookieJarSettings for this download in order to send cookies based on the
  // current state of the prefs/permissions.
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings = aCookieJarSettings;
  if (!cookieJarSettings) {
    // Although the variable is called 'triggering principal', it is used as the
    // loading principal in the download channel, so we treat it as a loading
    // principal also.
    bool shouldResistFingerprinting =
        nsContentUtils::ShouldResistFingerprinting_dangerous(
            aTriggeringPrincipal,
            "We are creating a new CookieJar Settings, so none exists "
            "currently. Although the variable is called 'triggering principal',"
            "it is used as the loading principal in the download channel, so we"
            "treat it as a loading principal also.",
            RFPTarget::IsAlwaysEnabledForPrecompute);
    cookieJarSettings =
        aIsPrivate
            ? net::CookieJarSettings::Create(net::CookieJarSettings::ePrivate,
                                             shouldResistFingerprinting)
            : net::CookieJarSettings::Create(net::CookieJarSettings::eRegular,
                                             shouldResistFingerprinting);
  }

  // Open a channel to the URI
  nsCOMPtr<nsIChannel> inputChannel;
  rv = NS_NewChannel(getter_AddRefs(inputChannel), aURI, aTriggeringPrincipal,
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     aContentPolicyType, cookieJarSettings,
                     nullptr,  // aPerformanceStorage
                     nullptr,  // aLoadGroup
                     static_cast<nsIInterfaceRequestor*>(this), loadFlags);

  nsCOMPtr<nsIPrivateBrowsingChannel> pbChannel =
      do_QueryInterface(inputChannel);
  if (pbChannel) {
    pbChannel->SetPrivate(aIsPrivate);
  }

  if (NS_FAILED(rv) || inputChannel == nullptr) {
    EndDownload(NS_ERROR_FAILURE);
    return NS_ERROR_FAILURE;
  }

  // Disable content conversion
  if (mPersistFlags & PERSIST_FLAGS_NO_CONVERSION) {
    nsCOMPtr<nsIEncodedChannel> encodedChannel(do_QueryInterface(inputChannel));
    if (encodedChannel) {
      encodedChannel->SetApplyConversion(false);
    }
  }

  nsCOMPtr<nsILoadInfo> loadInfo = inputChannel->LoadInfo();
  loadInfo->SetIsUserTriggeredSave(true);
  if (mPersistFlags & nsIWebBrowserPersist::PERSIST_FLAGS_DISABLE_HTTPS_ONLY) {
    loadInfo->SetHttpsOnlyStatus(nsILoadInfo::HTTPS_ONLY_EXEMPT);
  }

  // Set the referrer, post data and headers if any
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(inputChannel));
  if (httpChannel) {
    if (aReferrerInfo) {
      DebugOnly<nsresult> success = httpChannel->SetReferrerInfo(aReferrerInfo);
      MOZ_ASSERT(NS_SUCCEEDED(success));
    }

    // Post data
    if (aPostData) {
      nsCOMPtr<nsISeekableStream> stream(do_QueryInterface(aPostData));
      if (stream) {
        // Rewind the postdata stream
        stream->Seek(nsISeekableStream::NS_SEEK_SET, 0);
        nsCOMPtr<nsIUploadChannel> uploadChannel(
            do_QueryInterface(httpChannel));
        NS_ASSERTION(uploadChannel, "http must support nsIUploadChannel");
        // Attach the postdata to the http channel
        uploadChannel->SetUploadStream(aPostData, ""_ns, -1);
      }
    }

    // Cache key
    nsCOMPtr<nsICacheInfoChannel> cacheChannel(do_QueryInterface(httpChannel));
    if (cacheChannel && aCacheKey != 0) {
      cacheChannel->SetCacheKey(aCacheKey);
    }

    // Headers
    if (aExtraHeaders) {
      rv = mozilla::net::AddExtraHeaders(httpChannel,
                                         nsDependentCString(aExtraHeaders));
      if (NS_FAILED(rv)) {
        EndDownload(NS_ERROR_FAILURE);
        return NS_ERROR_FAILURE;
      }
    }
  }
  return SaveChannelInternal(inputChannel, aFile, aCalcFileExt);
}

nsresult nsWebBrowserPersist::SaveChannelInternal(nsIChannel* aChannel,
                                                  nsIURI* aFile,
                                                  bool aCalcFileExt) {
  NS_ENSURE_ARG_POINTER(aChannel);
  NS_ENSURE_ARG_POINTER(aFile);

  // The default behaviour of SaveChannelInternal is to download the source
  // into a storage stream and upload that to the target. MakeOutputStream
  // special-cases a file target and creates a file output stream directly.
  // We want to special-case a file source and create a file input stream,
  // but we don't need to do this in the case of a file target.
  nsCOMPtr<nsIFileChannel> fc(do_QueryInterface(aChannel));
  nsCOMPtr<nsIFileURL> fu(do_QueryInterface(aFile));

  if (fc && !fu) {
    nsCOMPtr<nsIInputStream> fileInputStream, bufferedInputStream;
    nsresult rv = aChannel->Open(getter_AddRefs(fileInputStream));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = NS_NewBufferedInputStream(getter_AddRefs(bufferedInputStream),
                                   fileInputStream.forget(),
                                   BUFFERED_OUTPUT_SIZE);
    NS_ENSURE_SUCCESS(rv, rv);
    nsAutoCString contentType;
    aChannel->GetContentType(contentType);
    return StartUpload(bufferedInputStream, aFile, contentType);
  }

  // Mark save channel as throttleable.
  nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(aChannel));
  if (cos) {
    cos->AddClassFlags(nsIClassOfService::Throttleable);
  }

  // Read from the input channel
  nsresult rv = aChannel->AsyncOpen(this);
  if (rv == NS_ERROR_NO_CONTENT) {
    // Assume this is a protocol such as mailto: which does not feed out
    // data and just ignore it.
    return NS_SUCCESS_DONT_FIXUP;
  }

  if (NS_FAILED(rv)) {
    // Opening failed, but do we care?
    if (mPersistFlags & PERSIST_FLAGS_FAIL_ON_BROKEN_LINKS) {
      SendErrorStatusChange(true, rv, aChannel, aFile);
      EndDownload(NS_ERROR_FAILURE);
      return NS_ERROR_FAILURE;
    }
    return NS_SUCCESS_DONT_FIXUP;
  }

  MutexAutoLock lock(mOutputMapMutex);
  // Add the output transport to the output map with the channel as the key
  nsCOMPtr<nsISupports> keyPtr = do_QueryInterface(aChannel);
  mOutputMap.InsertOrUpdate(keyPtr,
                            MakeUnique<OutputData>(aFile, mURI, aCalcFileExt));

  return NS_OK;
}

nsresult nsWebBrowserPersist::GetExtensionForContentType(
    const char16_t* aContentType, char16_t** aExt) {
  NS_ENSURE_ARG_POINTER(aContentType);
  NS_ENSURE_ARG_POINTER(aExt);

  *aExt = nullptr;

  nsresult rv;
  if (!mMIMEService) {
    mMIMEService = do_GetService(NS_MIMESERVICE_CONTRACTID, &rv);
    NS_ENSURE_TRUE(mMIMEService, NS_ERROR_FAILURE);
  }

  nsAutoCString contentType;
  LossyCopyUTF16toASCII(MakeStringSpan(aContentType), contentType);
  nsAutoCString ext;
  rv = mMIMEService->GetPrimaryExtension(contentType, ""_ns, ext);
  if (NS_SUCCEEDED(rv)) {
    *aExt = UTF8ToNewUnicode(ext);
    NS_ENSURE_TRUE(*aExt, NS_ERROR_OUT_OF_MEMORY);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

nsresult nsWebBrowserPersist::SaveDocumentDeferred(
    mozilla::UniquePtr<WalkData>&& aData) {
  nsresult rv =
      SaveDocumentInternal(aData->mDocument, aData->mFile, aData->mDataPath);
  if (NS_FAILED(rv)) {
    SendErrorStatusChange(true, rv, nullptr, mURI);
    EndDownload(rv);
  }
  return rv;
}

nsresult nsWebBrowserPersist::SaveDocumentInternal(
    nsIWebBrowserPersistDocument* aDocument, nsIURI* aFile, nsIURI* aDataPath) {
  mURI = nullptr;
  NS_ENSURE_ARG_POINTER(aDocument);
  NS_ENSURE_ARG_POINTER(aFile);

  nsresult rv = aDocument->SetPersistFlags(mPersistFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aDocument->GetIsPrivate(&mIsPrivate);
  NS_ENSURE_SUCCESS(rv, rv);

  // See if we can get the local file representation of this URI
  nsCOMPtr<nsIFile> localFile;
  rv = GetLocalFileFromURI(aFile, getter_AddRefs(localFile));

  nsCOMPtr<nsIFile> localDataPath;
  if (NS_SUCCEEDED(rv) && aDataPath) {
    // See if we can get the local file representation of this URI
    rv = GetLocalFileFromURI(aDataPath, getter_AddRefs(localDataPath));
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
  }

  // Persist the main document
  rv = aDocument->GetCharacterSet(mCurrentCharset);
  NS_ENSURE_SUCCESS(rv, rv);
  nsAutoCString uriSpec;
  rv = aDocument->GetDocumentURI(uriSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = NS_NewURI(getter_AddRefs(mURI), uriSpec, mCurrentCharset.get());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aDocument->GetBaseURI(uriSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = NS_NewURI(getter_AddRefs(mCurrentBaseURI), uriSpec,
                 mCurrentCharset.get());
  NS_ENSURE_SUCCESS(rv, rv);

  // Does the caller want to fixup the referenced URIs and save those too?
  if (aDataPath) {
    // Basic steps are these.
    //
    // 1. Iterate through the document (and subdocuments) building a list
    //    of unique URIs.
    // 2. For each URI create an OutputData entry and open a channel to save
    //    it. As each URI is saved, discover the mime type and fix up the
    //    local filename with the correct extension.
    // 3. Store the document in a list and wait for URI persistence to finish
    // 4. After URI persistence completes save the list of documents,
    //    fixing it up as it goes out to file.

    mCurrentDataPathIsRelative = false;
    mCurrentDataPath = aDataPath;
    mCurrentRelativePathToData = "";
    mCurrentThingsToPersist = 0;
    mTargetBaseURI = aFile;

    // Determine if the specified data path is relative to the
    // specified file, (e.g. c:\docs\htmldata is relative to
    // c:\docs\myfile.htm, but not to d:\foo\data.

    // Starting with the data dir work back through its parents
    // checking if one of them matches the base directory.

    if (localDataPath && localFile) {
      nsCOMPtr<nsIFile> baseDir;
      localFile->GetParent(getter_AddRefs(baseDir));

      nsAutoCString relativePathToData;
      nsCOMPtr<nsIFile> dataDirParent;
      dataDirParent = localDataPath;
      while (dataDirParent) {
        bool sameDir = false;
        dataDirParent->Equals(baseDir, &sameDir);
        if (sameDir) {
          mCurrentRelativePathToData = relativePathToData;
          mCurrentDataPathIsRelative = true;
          break;
        }

        nsAutoString dirName;
        dataDirParent->GetLeafName(dirName);

        nsAutoCString newRelativePathToData;
        newRelativePathToData =
            NS_ConvertUTF16toUTF8(dirName) + "/"_ns + relativePathToData;
        relativePathToData = newRelativePathToData;

        nsCOMPtr<nsIFile> newDataDirParent;
        rv = dataDirParent->GetParent(getter_AddRefs(newDataDirParent));
        dataDirParent = newDataDirParent;
      }
    } else {
      // generate a relative path if possible
      nsCOMPtr<nsIURL> pathToBaseURL(do_QueryInterface(aFile));
      if (pathToBaseURL) {
        nsAutoCString relativePath;  // nsACString
        if (NS_SUCCEEDED(
                pathToBaseURL->GetRelativeSpec(aDataPath, relativePath))) {
          mCurrentDataPathIsRelative = true;
          mCurrentRelativePathToData = relativePath;
        }
      }
    }

    // Store the document in a list so when URI persistence is done and the
    // filenames of saved URIs are known, the documents can be fixed up and
    // saved

    auto* docData = new DocData;
    docData->mBaseURI = mCurrentBaseURI;
    docData->mCharset = mCurrentCharset;
    docData->mDocument = aDocument;
    docData->mFile = aFile;
    mDocList.AppendElement(docData);

    // Walk the DOM gathering a list of externally referenced URIs in the uri
    // map
    nsCOMPtr<nsIWebBrowserPersistResourceVisitor> visit =
        new OnWalk(this, aFile, localDataPath);
    return aDocument->ReadResources(visit);
  } else {
    auto* docData = new DocData;
    docData->mBaseURI = mCurrentBaseURI;
    docData->mCharset = mCurrentCharset;
    docData->mDocument = aDocument;
    docData->mFile = aFile;
    mDocList.AppendElement(docData);

    // Not walking DOMs, so go directly to serialization.
    SerializeNextFile();
    return NS_OK;
  }
}

NS_IMETHODIMP
nsWebBrowserPersist::OnWalk::VisitResource(
    nsIWebBrowserPersistDocument* aDoc, const nsACString& aURI,
    nsContentPolicyType aContentPolicyType) {
  return mParent->StoreURI(aURI, aDoc, aContentPolicyType);
}

NS_IMETHODIMP
nsWebBrowserPersist::OnWalk::VisitDocument(
    nsIWebBrowserPersistDocument* aDoc, nsIWebBrowserPersistDocument* aSubDoc) {
  URIData* data = nullptr;
  nsAutoCString uriSpec;
  nsresult rv = aSubDoc->GetDocumentURI(uriSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mParent->StoreURI(uriSpec, aDoc, nsIContentPolicy::TYPE_SUBDOCUMENT,
                         false, &data);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!data) {
    // If the URI scheme isn't persistable, then don't persist.
    return NS_OK;
  }
  data->mIsSubFrame = true;
  return mParent->SaveSubframeContent(aSubDoc, aDoc, uriSpec, data);
}

NS_IMETHODIMP
nsWebBrowserPersist::OnWalk::VisitBrowsingContext(
    nsIWebBrowserPersistDocument* aDoc, BrowsingContext* aContext) {
  RefPtr<dom::CanonicalBrowsingContext> context = aContext->Canonical();

  if (NS_WARN_IF(!context->GetCurrentWindowGlobal())) {
    EndVisit(nullptr, NS_ERROR_FAILURE);
    return NS_ERROR_FAILURE;
  }

  RefPtr<WebBrowserPersistDocumentParent> actor(
      new WebBrowserPersistDocumentParent());

  nsCOMPtr<nsIWebBrowserPersistDocumentReceiver> receiver =
      new OnRemoteWalk(this, aDoc);
  actor->SetOnReady(receiver);

  RefPtr<dom::BrowserParent> browserParent =
      context->GetCurrentWindowGlobal()->GetBrowserParent();

  bool ok =
      context->GetContentParent()->SendPWebBrowserPersistDocumentConstructor(
          actor, browserParent, context);

  if (NS_WARN_IF(!ok)) {
    // (The actor will be destroyed on constructor failure.)
    EndVisit(nullptr, NS_ERROR_FAILURE);
    return NS_ERROR_FAILURE;
  }

  ++mPendingDocuments;

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersist::OnWalk::EndVisit(nsIWebBrowserPersistDocument* aDoc,
                                      nsresult aStatus) {
  if (NS_FAILED(mStatus)) {
    return mStatus;
  }

  if (NS_FAILED(aStatus)) {
    mStatus = aStatus;
    mParent->SendErrorStatusChange(true, aStatus, nullptr, mFile);
    mParent->EndDownload(aStatus);
    return aStatus;
  }

  if (--mPendingDocuments) {
    // We're not done yet, wait for more.
    return NS_OK;
  }

  mParent->FinishSaveDocumentInternal(mFile, mDataPath);
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersist::OnRemoteWalk::OnDocumentReady(
    nsIWebBrowserPersistDocument* aSubDocument) {
  mVisitor->VisitDocument(mDocument, aSubDocument);
  mVisitor->EndVisit(mDocument, NS_OK);
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowserPersist::OnRemoteWalk::OnError(nsresult aFailure) {
  mVisitor->EndVisit(nullptr, aFailure);
  return NS_OK;
}

void nsWebBrowserPersist::FinishSaveDocumentInternal(nsIURI* aFile,
                                                     nsIFile* aDataPath) {
  // If there are things to persist, create a directory to hold them
  if (mCurrentThingsToPersist > 0) {
    if (aDataPath) {
      bool exists = false;
      bool haveDir = false;

      aDataPath->Exists(&exists);
      if (exists) {
        aDataPath->IsDirectory(&haveDir);
      }
      if (!haveDir) {
        nsresult rv = aDataPath->Create(nsIFile::DIRECTORY_TYPE, 0755);
        if (NS_SUCCEEDED(rv)) {
          haveDir = true;
        } else {
          SendErrorStatusChange(false, rv, nullptr, aFile);
        }
      }
      if (!haveDir) {
        EndDownload(NS_ERROR_FAILURE);
        return;
      }
      if (mPersistFlags & PERSIST_FLAGS_CLEANUP_ON_FAILURE) {
        // Add to list of things to delete later if all goes wrong
        auto* cleanupData = new CleanupData;
        cleanupData->mFile = aDataPath;
        cleanupData->mIsDirectory = true;
        mCleanupList.AppendElement(cleanupData);
      }
    }
  }

  if (mWalkStack.Length() > 0) {
    mozilla::UniquePtr<WalkData> toWalk = mWalkStack.PopLastElement();
    // Bounce this off the event loop to avoid stack overflow.
    using WalkStorage = StoreCopyPassByRRef<decltype(toWalk)>;
    auto saveMethod = &nsWebBrowserPersist::SaveDocumentDeferred;
    nsCOMPtr<nsIRunnable> saveLater = NewRunnableMethod<WalkStorage>(
        "nsWebBrowserPersist::FinishSaveDocumentInternal", this, saveMethod,
        std::move(toWalk));
    NS_DispatchToCurrentThread(saveLater);
  } else {
    // Done walking DOMs; on to the serialization phase.
    SerializeNextFile();
  }
}

void nsWebBrowserPersist::Cleanup() {
  mURIMap.Clear();
  nsClassHashtable<nsISupportsHashKey, OutputData> outputMapCopy;
  {
    MutexAutoLock lock(mOutputMapMutex);
    mOutputMap.SwapElements(outputMapCopy);
  }
  for (const auto& key : outputMapCopy.Keys()) {
    nsCOMPtr<nsIChannel> channel = do_QueryInterface(key);
    if (channel) {
      channel->Cancel(NS_BINDING_ABORTED);
    }
  }
  outputMapCopy.Clear();

  for (const auto& key : mUploadList.Keys()) {
    nsCOMPtr<nsIChannel> channel = do_QueryInterface(key);
    if (channel) {
      channel->Cancel(NS_BINDING_ABORTED);
    }
  }
  mUploadList.Clear();

  uint32_t i;
  for (i = 0; i < mDocList.Length(); i++) {
    DocData* docData = mDocList.ElementAt(i);
    delete docData;
  }
  mDocList.Clear();

  for (i = 0; i < mCleanupList.Length(); i++) {
    CleanupData* cleanupData = mCleanupList.ElementAt(i);
    delete cleanupData;
  }
  mCleanupList.Clear();

  mFilenameList.Clear();
}

void nsWebBrowserPersist::CleanupLocalFiles() {
  // Two passes, the first pass cleans up files, the second pass tests
  // for and then deletes empty directories. Directories that are not
  // empty after the first pass must contain files from something else
  // and are not deleted.
  int pass;
  for (pass = 0; pass < 2; pass++) {
    uint32_t i;
    for (i = 0; i < mCleanupList.Length(); i++) {
      CleanupData* cleanupData = mCleanupList.ElementAt(i);
      nsCOMPtr<nsIFile> file = cleanupData->mFile;

      // Test if the dir / file exists (something in an earlier loop
      // may have already removed it)
      bool exists = false;
      file->Exists(&exists);
      if (!exists) continue;

      // Test if the file has changed in between creation and deletion
      // in some way that means it should be ignored
      bool isDirectory = false;
      file->IsDirectory(&isDirectory);
      if (isDirectory != cleanupData->mIsDirectory)
        continue;  // A file has become a dir or vice versa !

      if (pass == 0 && !isDirectory) {
        file->Remove(false);
      } else if (pass == 1 && isDirectory)  // Directory
      {
        // Directories are more complicated. Enumerate through
        // children looking for files. Any files created by the
        // persist object would have been deleted by the first
        // pass so if there are any there at this stage, the dir
        // cannot be deleted because it has someone else's files
        // in it. Empty child dirs are deleted but they must be
        // recursed through to ensure they are actually empty.

        bool isEmptyDirectory = true;
        nsCOMArray<nsIDirectoryEnumerator> dirStack;
        int32_t stackSize = 0;

        // Push the top level enum onto the stack
        nsCOMPtr<nsIDirectoryEnumerator> pos;
        if (NS_SUCCEEDED(file->GetDirectoryEntries(getter_AddRefs(pos))))
          dirStack.AppendObject(pos);

        while (isEmptyDirectory && (stackSize = dirStack.Count())) {
          // Pop the last element
          nsCOMPtr<nsIDirectoryEnumerator> curPos;
          curPos = dirStack[stackSize - 1];
          dirStack.RemoveObjectAt(stackSize - 1);

          nsCOMPtr<nsIFile> child;
          if (NS_FAILED(curPos->GetNextFile(getter_AddRefs(child))) || !child) {
            continue;
          }

          bool childIsSymlink = false;
          child->IsSymlink(&childIsSymlink);
          bool childIsDir = false;
          child->IsDirectory(&childIsDir);
          if (!childIsDir || childIsSymlink) {
            // Some kind of file or symlink which means dir
            // is not empty so just drop out.
            isEmptyDirectory = false;
            break;
          }
          // Push parent enumerator followed by child enumerator
          nsCOMPtr<nsIDirectoryEnumerator> childPos;
          child->GetDirectoryEntries(getter_AddRefs(childPos));
          dirStack.AppendObject(curPos);
          if (childPos) dirStack.AppendObject(childPos);
        }
        dirStack.Clear();

        // If after all that walking the dir is deemed empty, delete it
        if (isEmptyDirectory) {
          file->Remove(true);
        }
      }
    }
  }
}

nsresult nsWebBrowserPersist::CalculateUniqueFilename(
    nsIURI* aURI, nsCOMPtr<nsIURI>& aOutURI) {
  nsCOMPtr<nsIURL> url(do_QueryInterface(aURI));
  NS_ENSURE_TRUE(url, NS_ERROR_FAILURE);

  bool nameHasChanged = false;
  nsresult rv;

  // Get the old filename
  nsAutoCString filename;
  rv = url->GetFileName(filename);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
  nsAutoCString directory;
  rv = url->GetDirectory(directory);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  // Split the filename into a base and an extension.
  // e.g. "foo.html" becomes "foo" & ".html"
  //
  // The nsIURL methods GetFileBaseName & GetFileExtension don't
  // preserve the dot whereas this code does to save some effort
  // later when everything is put back together.
  int32_t lastDot = filename.RFind(".");
  nsAutoCString base;
  nsAutoCString ext;
  if (lastDot >= 0) {
    filename.Mid(base, 0, lastDot);
    filename.Mid(ext, lastDot, filename.Length() - lastDot);  // includes dot
  } else {
    // filename contains no dot
    base = filename;
  }

  // Test if the filename is longer than allowed by the OS
  int32_t needToChop = filename.Length() - kDefaultMaxFilenameLength;
  if (needToChop > 0) {
    // Truncate the base first and then the ext if necessary
    if (base.Length() > (uint32_t)needToChop) {
      base.Truncate(base.Length() - needToChop);
    } else {
      needToChop -= base.Length() - 1;
      base.Truncate(1);
      if (ext.Length() > (uint32_t)needToChop) {
        ext.Truncate(ext.Length() - needToChop);
      } else {
        ext.Truncate(0);
      }
      // If kDefaultMaxFilenameLength were 1 we'd be in trouble here,
      // but that won't happen because it will be set to a sensible
      // value.
    }

    filename.Assign(base);
    filename.Append(ext);
    nameHasChanged = true;
  }

  // Ensure the filename is unique
  // Create a filename if it's empty, or if the filename / datapath is
  // already taken by another URI and create an alternate name.

  if (base.IsEmpty() || !mFilenameList.IsEmpty()) {
    nsAutoCString tmpPath;
    nsAutoCString tmpBase;
    uint32_t duplicateCounter = 1;
    while (true) {
      // Make a file name,
      // Foo become foo_001, foo_002, etc.
      // Empty files become _001, _002 etc.

      if (base.IsEmpty() || duplicateCounter > 1) {
        SmprintfPointer tmp = mozilla::Smprintf("_%03d", duplicateCounter);
        NS_ENSURE_TRUE(tmp, NS_ERROR_OUT_OF_MEMORY);
        if (filename.Length() < kDefaultMaxFilenameLength - 4) {
          tmpBase = base;
        } else {
          base.Mid(tmpBase, 0, base.Length() - 4);
        }
        tmpBase.Append(tmp.get());
      } else {
        tmpBase = base;
      }

      tmpPath.Assign(directory);
      tmpPath.Append(tmpBase);
      tmpPath.Append(ext);

      // Test if the name is a duplicate
      if (!mFilenameList.Contains(tmpPath)) {
        if (!base.Equals(tmpBase)) {
          filename.Assign(tmpBase);
          filename.Append(ext);
          nameHasChanged = true;
        }
        break;
      }
      duplicateCounter++;
    }
  }

  // Add name to list of those already used
  nsAutoCString newFilepath(directory);
  newFilepath.Append(filename);
  mFilenameList.AppendElement(newFilepath);

  // Update the uri accordingly if the filename actually changed
  if (nameHasChanged) {
    // Final sanity test
    if (filename.Length() > kDefaultMaxFilenameLength) {
      NS_WARNING(
          "Filename wasn't truncated less than the max file length - how can "
          "that be?");
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIFile> localFile;
    GetLocalFileFromURI(aURI, getter_AddRefs(localFile));

    if (localFile) {
      nsAutoString filenameAsUnichar;
      CopyASCIItoUTF16(filename, filenameAsUnichar);
      localFile->SetLeafName(filenameAsUnichar);

      // Resync the URI with the file after the extension has been appended
      return NS_MutateURI(aURI)
          .Apply(&nsIFileURLMutator::SetFile, localFile)
          .Finalize(aOutURI);
    }
    return NS_MutateURI(url)
        .Apply(&nsIURLMutator::SetFileName, filename, nullptr)
        .Finalize(aOutURI);
  }

  // TODO (:valentin) This method should always clone aURI
  aOutURI = aURI;
  return NS_OK;
}

nsresult nsWebBrowserPersist::MakeFilenameFromURI(nsIURI* aURI,
                                                  nsString& aFilename) {
  // Try to get filename from the URI.
  nsAutoString fileName;

  // Get a suggested file name from the URL but strip it of characters
  // likely to cause the name to be illegal.

  nsCOMPtr<nsIURL> url(do_QueryInterface(aURI));
  if (url) {
    nsAutoCString nameFromURL;
    url->GetFileName(nameFromURL);
    if (mPersistFlags & PERSIST_FLAGS_DONT_CHANGE_FILENAMES) {
      CopyASCIItoUTF16(NS_UnescapeURL(nameFromURL), fileName);
      aFilename = fileName;
      return NS_OK;
    }
    if (!nameFromURL.IsEmpty()) {
      // Unescape the file name (GetFileName escapes it)
      NS_UnescapeURL(nameFromURL);
      uint32_t nameLength = 0;
      const char* p = nameFromURL.get();
      for (; *p && *p != ';' && *p != '?' && *p != '#' && *p != '.'; p++) {
        if (IsAsciiAlpha(*p) || IsAsciiDigit(*p) || *p == '.' || *p == '-' ||
            *p == '_' || (*p == ' ')) {
          fileName.Append(char16_t(*p));
          if (++nameLength == kDefaultMaxFilenameLength) {
            // Note:
            // There is no point going any further since it will be
            // truncated in CalculateUniqueFilename anyway.
            // More importantly, certain implementations of
            // nsIFile (e.g. the Mac impl) might truncate
            // names in undesirable ways, such as truncating from
            // the middle, inserting ellipsis and so on.
            break;
          }
        }
      }
    }
  }

  // Empty filenames can confuse the local file object later
  // when it attempts to set the leaf name in CalculateUniqueFilename
  // for duplicates and ends up replacing the parent dir. To avoid
  // the problem, all filenames are made at least one character long.
  if (fileName.IsEmpty()) {
    fileName.Append(char16_t('a'));  // 'a' is for arbitrary
  }

  aFilename = fileName;
  return NS_OK;
}

nsresult nsWebBrowserPersist::CalculateAndAppendFileExt(
    nsIURI* aURI, nsIChannel* aChannel, nsIURI* aOriginalURIWithExtension,
    nsCOMPtr<nsIURI>& aOutURI) {
  nsresult rv = NS_OK;

  if (!mMIMEService) {
    mMIMEService = do_GetService(NS_MIMESERVICE_CONTRACTID, &rv);
    NS_ENSURE_TRUE(mMIMEService, NS_ERROR_FAILURE);
  }

  nsAutoCString contentType;

  // Get the content type from the channel
  aChannel->GetContentType(contentType);

  // Get the content type from the MIME service
  if (contentType.IsEmpty()) {
    nsCOMPtr<nsIURI> uri;
    aChannel->GetOriginalURI(getter_AddRefs(uri));
    mMIMEService->GetTypeFromURI(uri, contentType);
  }

  // Validate the filename
  if (!contentType.IsEmpty()) {
    nsAutoString newFileName;
    if (NS_SUCCEEDED(mMIMEService->GetValidFileName(
            aChannel, contentType, aOriginalURIWithExtension,
            nsIMIMEService::VALIDATE_DEFAULT, newFileName))) {
      nsCOMPtr<nsIFile> localFile;
      GetLocalFileFromURI(aURI, getter_AddRefs(localFile));
      if (localFile) {
        localFile->SetLeafName(newFileName);

        // Resync the URI with the file after the extension has been appended
        return NS_MutateURI(aURI)
            .Apply(&nsIFileURLMutator::SetFile, localFile)
            .Finalize(aOutURI);
      }
      return NS_MutateURI(aURI)
          .Apply(&nsIURLMutator::SetFileName,
                 NS_ConvertUTF16toUTF8(newFileName), nullptr)
          .Finalize(aOutURI);
    }
  }

  // TODO (:valentin) This method should always clone aURI
  aOutURI = aURI;
  return NS_OK;
}

// Note: the MakeOutputStream helpers can be called from a background thread.
nsresult nsWebBrowserPersist::MakeOutputStream(
    nsIURI* aURI, nsIOutputStream** aOutputStream) {
  nsresult rv;

  nsCOMPtr<nsIFile> localFile;
  GetLocalFileFromURI(aURI, getter_AddRefs(localFile));
  if (localFile)
    rv = MakeOutputStreamFromFile(localFile, aOutputStream);
  else
    rv = MakeOutputStreamFromURI(aURI, aOutputStream);

  return rv;
}

nsresult nsWebBrowserPersist::MakeOutputStreamFromFile(
    nsIFile* aFile, nsIOutputStream** aOutputStream) {
  nsresult rv = NS_OK;

  nsCOMPtr<nsIFileOutputStream> fileOutputStream =
      do_CreateInstance(NS_LOCALFILEOUTPUTSTREAM_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  // XXX brade:  get the right flags here!
  int32_t ioFlags = -1;
  if (mPersistFlags & nsIWebBrowserPersist::PERSIST_FLAGS_APPEND_TO_FILE)
    ioFlags = PR_APPEND | PR_CREATE_FILE | PR_WRONLY;
  rv = fileOutputStream->Init(aFile, ioFlags, -1, 0);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = NS_NewBufferedOutputStream(aOutputStream, fileOutputStream.forget(),
                                  BUFFERED_OUTPUT_SIZE);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mPersistFlags & PERSIST_FLAGS_CLEANUP_ON_FAILURE) {
    // Add to cleanup list in event of failure
    auto* cleanupData = new CleanupData;
    cleanupData->mFile = aFile;
    cleanupData->mIsDirectory = false;
    if (NS_IsMainThread()) {
      mCleanupList.AppendElement(cleanupData);
    } else {
      // If we're on a background thread, add the cleanup back on the main
      // thread.
      RefPtr<Runnable> addCleanup = NS_NewRunnableFunction(
          "nsWebBrowserPersist::AddCleanupToList",
          [self = RefPtr{this}, cleanup = std::move(cleanupData)]() {
            self->mCleanupList.AppendElement(cleanup);
          });
      NS_DispatchToMainThread(addCleanup);
    }
  }

  return NS_OK;
}

nsresult nsWebBrowserPersist::MakeOutputStreamFromURI(
    nsIURI* aURI, nsIOutputStream** aOutputStream) {
  uint32_t segsize = 8192;
  uint32_t maxsize = uint32_t(-1);
  nsCOMPtr<nsIStorageStream> storStream;
  nsresult rv =
      NS_NewStorageStream(segsize, maxsize, getter_AddRefs(storStream));
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ENSURE_SUCCESS(CallQueryInterface(storStream, aOutputStream),
                    NS_ERROR_FAILURE);
  return NS_OK;
}

void nsWebBrowserPersist::FinishDownload() {
  // We call FinishDownload when we run out of things to download for this
  // persist operation, by dispatching this method to the main thread. By now,
  // it's possible that we have been canceled or encountered an error earlier
  // in the download, or something else called EndDownload. In that case, don't
  // re-run EndDownload.
  if (mEndCalled) {
    return;
  }
  EndDownload(NS_OK);
}

void nsWebBrowserPersist::EndDownload(nsresult aResult) {
  MOZ_ASSERT(NS_IsMainThread(), "Should end download on the main thread.");

  // Really this should just never happen, but if it does, at least avoid
  // no-op notifications or pretending we succeeded if we already failed.
  if (mEndCalled && (NS_SUCCEEDED(aResult) || mPersistResult == aResult)) {
    return;
  }

  // Store the error code in the result if it is an error
  if (NS_SUCCEEDED(mPersistResult) && NS_FAILED(aResult)) {
    mPersistResult = aResult;
  }

  if (mEndCalled) {
    MOZ_ASSERT(!mEndCalled, "Should only end the download once.");
    return;
  }
  mEndCalled = true;

  ClosePromise::All(GetCurrentSerialEventTarget(), mFileClosePromises)
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}, aResult]() {
               self->EndDownloadInternal(aResult);
             });
}

void nsWebBrowserPersist::EndDownloadInternal(nsresult aResult) {
  // mCompleted needs to be set before issuing the stop notification.
  // (Bug 1224437)
  mCompleted = true;
  // State stop notification
  if (mProgressListener) {
    mProgressListener->OnStateChange(
        nullptr, nullptr,
        nsIWebProgressListener::STATE_STOP |
            nsIWebProgressListener::STATE_IS_NETWORK,
        mPersistResult);
  }

  // Do file cleanup if required
  if (NS_FAILED(aResult) &&
      (mPersistFlags & PERSIST_FLAGS_CLEANUP_ON_FAILURE)) {
    CleanupLocalFiles();
  }

  // Cleanup the channels
  Cleanup();

  mProgressListener = nullptr;
  mProgressListener2 = nullptr;
  mEventSink = nullptr;
}

nsresult nsWebBrowserPersist::FixRedirectedChannelEntry(
    nsIChannel* aNewChannel) {
  NS_ENSURE_ARG_POINTER(aNewChannel);

  // Iterate through existing open channels looking for one with a URI
  // matching the one specified.
  nsCOMPtr<nsIURI> originalURI;
  aNewChannel->GetOriginalURI(getter_AddRefs(originalURI));
  nsISupports* matchingKey = nullptr;
  for (nsISupports* key : mOutputMap.Keys()) {
    nsCOMPtr<nsIChannel> thisChannel = do_QueryInterface(key);
    nsCOMPtr<nsIURI> thisURI;

    thisChannel->GetOriginalURI(getter_AddRefs(thisURI));

    // Compare this channel's URI to the one passed in.
    bool matchingURI = false;
    thisURI->Equals(originalURI, &matchingURI);
    if (matchingURI) {
      matchingKey = key;
      break;
    }
  }

  if (matchingKey) {
    // We only get called from OnStartRequest, so this is always on the
    // main thread. Make sure we don't pull the rug from under anything else.
    MutexAutoLock lock(mOutputMapMutex);
    // If a match was found, remove the data entry with the old channel
    // key and re-add it with the new channel key.
    mozilla::UniquePtr<OutputData> outputData;
    mOutputMap.Remove(matchingKey, &outputData);
    NS_ENSURE_TRUE(outputData, NS_ERROR_FAILURE);

    // Store data again with new channel unless told to ignore redirects.
    if (!(mPersistFlags & PERSIST_FLAGS_IGNORE_REDIRECTED_DATA)) {
      nsCOMPtr<nsISupports> keyPtr = do_QueryInterface(aNewChannel);
      mOutputMap.InsertOrUpdate(keyPtr, std::move(outputData));
    }
  }

  return NS_OK;
}

void nsWebBrowserPersist::CalcTotalProgress() {
  mTotalCurrentProgress = 0;
  mTotalMaxProgress = 0;

  if (mOutputMap.Count() > 0) {
    // Total up the progress of each output stream
    for (const auto& data : mOutputMap.Values()) {
      // Only count toward total progress if destination file is local.
      nsCOMPtr<nsIFileURL> fileURL = do_QueryInterface(data->mFile);
      if (fileURL) {
        mTotalCurrentProgress += data->mSelfProgress;
        mTotalMaxProgress += data->mSelfProgressMax;
      }
    }
  }

  if (mUploadList.Count() > 0) {
    // Total up the progress of each upload
    for (const auto& data : mUploadList.Values()) {
      if (data) {
        mTotalCurrentProgress += data->mSelfProgress;
        mTotalMaxProgress += data->mSelfProgressMax;
      }
    }
  }

  // XXX this code seems pretty bogus and pointless
  if (mTotalCurrentProgress == 0 && mTotalMaxProgress == 0) {
    // No output streams so we must be complete
    mTotalCurrentProgress = 10000;
    mTotalMaxProgress = 10000;
  }
}

nsresult nsWebBrowserPersist::StoreURI(const nsACString& aURI,
                                       nsIWebBrowserPersistDocument* aDoc,
                                       nsContentPolicyType aContentPolicyType,
                                       bool aNeedsPersisting, URIData** aData) {
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aURI, mCurrentCharset.get(),
                          mCurrentBaseURI);
  NS_ENSURE_SUCCESS(rv, rv);

  return StoreURI(uri, aDoc, aContentPolicyType, aNeedsPersisting, aData);
}

nsresult nsWebBrowserPersist::StoreURI(nsIURI* aURI,
                                       nsIWebBrowserPersistDocument* aDoc,
                                       nsContentPolicyType aContentPolicyType,
                                       bool aNeedsPersisting, URIData** aData) {
  NS_ENSURE_ARG_POINTER(aURI);
  if (aData) {
    *aData = nullptr;
  }

  // Test if this URI should be persisted. By default
  // we should assume the URI  is persistable.
  bool doNotPersistURI;
  nsresult rv = NS_URIChainHasFlags(
      aURI, nsIProtocolHandler::URI_NON_PERSISTABLE, &doNotPersistURI);
  if (NS_FAILED(rv)) {
    doNotPersistURI = false;
  }

  if (doNotPersistURI) {
    return NS_OK;
  }

  URIData* data = nullptr;
  MakeAndStoreLocalFilenameInURIMap(aURI, aDoc, aContentPolicyType,
                                    aNeedsPersisting, &data);
  if (aData) {
    *aData = data;
  }

  return NS_OK;
}

nsresult nsWebBrowserPersist::URIData::GetLocalURI(nsIURI* targetBaseURI,
                                                   nsCString& aSpecOut) {
  aSpecOut.SetIsVoid(true);
  if (!mNeedsFixup) {
    return NS_OK;
  }
  nsresult rv;
  nsCOMPtr<nsIURI> fileAsURI;
  if (mFile) {
    fileAsURI = mFile;
  } else {
    fileAsURI = mDataPath;
    rv = AppendPathToURI(fileAsURI, mFilename, fileAsURI);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // remove username/password if present
  Unused << NS_MutateURI(fileAsURI).SetUserPass(""_ns).Finalize(fileAsURI);

  // reset node attribute
  // Use relative or absolute links
  if (mDataPathIsRelative) {
    bool isEqual = false;
    if (NS_SUCCEEDED(mRelativeDocumentURI->Equals(targetBaseURI, &isEqual)) &&
        isEqual) {
      nsCOMPtr<nsIURL> url(do_QueryInterface(fileAsURI));
      if (!url) {
        return NS_ERROR_FAILURE;
      }

      nsAutoCString filename;
      url->GetFileName(filename);

      nsAutoCString rawPathURL(mRelativePathToData);
      rawPathURL.Append(filename);

      rv = NS_EscapeURL(rawPathURL, esc_FilePath, aSpecOut, fallible);
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      nsAutoCString rawPathURL;

      nsCOMPtr<nsIFile> dataFile;
      rv = GetLocalFileFromURI(mFile, getter_AddRefs(dataFile));
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIFile> docFile;
      rv = GetLocalFileFromURI(targetBaseURI, getter_AddRefs(docFile));
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsIFile> parentDir;
      rv = docFile->GetParent(getter_AddRefs(parentDir));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = dataFile->GetRelativePath(parentDir, rawPathURL);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = NS_EscapeURL(rawPathURL, esc_FilePath, aSpecOut, fallible);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  } else {
    fileAsURI->GetSpec(aSpecOut);
  }
  if (mIsSubFrame) {
    AppendUTF16toUTF8(mSubFrameExt, aSpecOut);
  }

  return NS_OK;
}

bool nsWebBrowserPersist::DocumentEncoderExists(const char* aContentType) {
  return do_getDocumentTypeSupportedForEncoding(aContentType);
}

nsresult nsWebBrowserPersist::SaveSubframeContent(
    nsIWebBrowserPersistDocument* aFrameContent,
    nsIWebBrowserPersistDocument* aParentDocument, const nsCString& aURISpec,
    URIData* aData) {
  NS_ENSURE_ARG_POINTER(aData);

  // Extract the content type for the frame's contents.
  nsAutoCString contentType;
  nsresult rv = aFrameContent->GetContentType(contentType);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString ext;
  GetExtensionForContentType(NS_ConvertASCIItoUTF16(contentType).get(),
                             getter_Copies(ext));

  // We must always have an extension so we will try to re-assign
  // the original extension if GetExtensionForContentType fails.
  if (ext.IsEmpty()) {
    nsCOMPtr<nsIURI> docURI;
    rv = NS_NewURI(getter_AddRefs(docURI), aURISpec, mCurrentCharset.get());
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIURL> url(do_QueryInterface(docURI, &rv));
    nsAutoCString extension;
    if (NS_SUCCEEDED(rv)) {
      url->GetFileExtension(extension);
    } else {
      extension.AssignLiteral("htm");
    }
    aData->mSubFrameExt.Assign(char16_t('.'));
    AppendUTF8toUTF16(extension, aData->mSubFrameExt);
  } else {
    aData->mSubFrameExt.Assign(char16_t('.'));
    aData->mSubFrameExt.Append(ext);
  }

  nsString filenameWithExt = aData->mFilename;
  filenameWithExt.Append(aData->mSubFrameExt);

  // Work out the path for the subframe
  nsCOMPtr<nsIURI> frameURI = mCurrentDataPath;
  rv = AppendPathToURI(frameURI, filenameWithExt, frameURI);
  NS_ENSURE_SUCCESS(rv, rv);

  // Work out the path for the subframe data
  nsCOMPtr<nsIURI> frameDataURI = mCurrentDataPath;
  nsAutoString newFrameDataPath(aData->mFilename);

  // Append _data
  newFrameDataPath.AppendLiteral("_data");
  rv = AppendPathToURI(frameDataURI, newFrameDataPath, frameDataURI);
  NS_ENSURE_SUCCESS(rv, rv);

  // Make frame document & data path conformant and unique
  nsCOMPtr<nsIURI> out;
  rv = CalculateUniqueFilename(frameURI, out);
  NS_ENSURE_SUCCESS(rv, rv);
  frameURI = out;

  rv = CalculateUniqueFilename(frameDataURI, out);
  NS_ENSURE_SUCCESS(rv, rv);
  frameDataURI = out;

  mCurrentThingsToPersist++;

  // We shouldn't use SaveDocumentInternal for the contents
  // of frames that are not documents, e.g. images.
  if (DocumentEncoderExists(contentType.get())) {
    auto toWalk = mozilla::MakeUnique<WalkData>();
    toWalk->mDocument = aFrameContent;
    toWalk->mFile = frameURI;
    toWalk->mDataPath = frameDataURI;
    mWalkStack.AppendElement(std::move(toWalk));
  } else {
    nsContentPolicyType policyType = nsIContentPolicy::TYPE_OTHER;
    if (StringBeginsWith(contentType, "image/"_ns)) {
      policyType = nsIContentPolicy::TYPE_IMAGE;
    } else if (StringBeginsWith(contentType, "audio/"_ns) ||
               StringBeginsWith(contentType, "video/"_ns)) {
      policyType = nsIContentPolicy::TYPE_MEDIA;
    }
    rv = StoreURI(aURISpec, aParentDocument, policyType);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // Store the updated uri to the frame
  aData->mFile = frameURI;
  aData->mSubFrameExt.Truncate();  // we already put this in frameURI

  return NS_OK;
}

nsresult nsWebBrowserPersist::CreateChannelFromURI(nsIURI* aURI,
                                                   nsIChannel** aChannel) {
  nsresult rv = NS_OK;
  *aChannel = nullptr;

  rv = NS_NewChannel(aChannel, aURI, nsContentUtils::GetSystemPrincipal(),
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     nsIContentPolicy::TYPE_OTHER);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_ARG_POINTER(*aChannel);

  rv = (*aChannel)->SetNotificationCallbacks(
      static_cast<nsIInterfaceRequestor*>(this));
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

// we store the current location as the key (absolutized version of domnode's
// attribute's value)
nsresult nsWebBrowserPersist::MakeAndStoreLocalFilenameInURIMap(
    nsIURI* aURI, nsIWebBrowserPersistDocument* aDoc,
    nsContentPolicyType aContentPolicyType, bool aNeedsPersisting,
    URIData** aData) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsAutoCString spec;
  nsresult rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  // Create a sensibly named filename for the URI and store in the URI map
  URIData* data;
  if (mURIMap.Get(spec, &data)) {
    if (aNeedsPersisting) {
      data->mNeedsPersisting = true;
    }
    if (aData) {
      *aData = data;
    }
    return NS_OK;
  }

  // Create a unique file name for the uri
  nsString filename;
  rv = MakeFilenameFromURI(aURI, filename);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

  // Store the file name
  data = new URIData;

  data->mContentPolicyType = aContentPolicyType;
  data->mNeedsPersisting = aNeedsPersisting;
  data->mNeedsFixup = true;
  data->mFilename = filename;
  data->mSaved = false;
  data->mIsSubFrame = false;
  data->mDataPath = mCurrentDataPath;
  data->mDataPathIsRelative = mCurrentDataPathIsRelative;
  data->mRelativePathToData = mCurrentRelativePathToData;
  data->mRelativeDocumentURI = mTargetBaseURI;
  data->mCharset = mCurrentCharset;

  aDoc->GetPrincipal(getter_AddRefs(data->mTriggeringPrincipal));
  aDoc->GetCookieJarSettings(getter_AddRefs(data->mCookieJarSettings));

  if (aNeedsPersisting) mCurrentThingsToPersist++;

  mURIMap.InsertOrUpdate(spec, UniquePtr<URIData>(data));
  if (aData) {
    *aData = data;
  }

  return NS_OK;
}

// Decide if we need to apply conversion to the passed channel.
void nsWebBrowserPersist::SetApplyConversionIfNeeded(nsIChannel* aChannel) {
  nsresult rv = NS_OK;
  nsCOMPtr<nsIEncodedChannel> encChannel = do_QueryInterface(aChannel, &rv);
  if (NS_FAILED(rv)) return;

  // Set the default conversion preference:
  encChannel->SetApplyConversion(false);

  nsCOMPtr<nsIURI> thisURI;
  aChannel->GetURI(getter_AddRefs(thisURI));
  nsCOMPtr<nsIURL> sourceURL(do_QueryInterface(thisURI));
  if (!sourceURL) return;
  nsAutoCString extension;
  sourceURL->GetFileExtension(extension);

  nsCOMPtr<nsIUTF8StringEnumerator> encEnum;
  encChannel->GetContentEncodings(getter_AddRefs(encEnum));
  if (!encEnum) return;
  nsCOMPtr<nsIExternalHelperAppService> helperAppService =
      do_GetService(NS_EXTERNALHELPERAPPSERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) return;
  bool hasMore;
  rv = encEnum->HasMore(&hasMore);
  if (NS_SUCCEEDED(rv) && hasMore) {
    nsAutoCString encType;
    rv = encEnum->GetNext(encType);
    if (NS_SUCCEEDED(rv)) {
      bool applyConversion = false;
      rv = helperAppService->ApplyDecodingForExtension(extension, encType,
                                                       &applyConversion);
      if (NS_SUCCEEDED(rv)) encChannel->SetApplyConversion(applyConversion);
    }
  }
}
