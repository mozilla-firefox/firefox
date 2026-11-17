/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWebBrowserPersist_h_
#define nsWebBrowserPersist_h_

#include "mozilla/MozPromise.h"
#include "mozilla/Mutex.h"
#include "mozilla/UniquePtr.h"
#include "nsCOMPtr.h"
#include "nsClassHashtable.h"
#include "nsHashKeys.h"
#include "nsIChannel.h"
#include "nsIFile.h"
#include "nsIInputStream.h"
#include "nsIInterfaceRequestor.h"
#include "nsIMIMEService.h"
#include "nsIOutputStream.h"
#include "nsIProgressEventSink.h"
#include "nsIStreamListener.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsIWebBrowserPersist.h"
#include "nsIWebBrowserPersistDocument.h"
#include "nsIWebProgressListener2.h"
#include "nsTArray.h"
#include "nsWeakReference.h"

class nsIReferrerInfo;
class nsIStorageStream;
class nsIWebBrowserPersistDocument;

using ClosePromise = mozilla::MozPromise<nsresult, nsresult, true>;

class nsWebBrowserPersist final : public nsIInterfaceRequestor,
                                  public nsIWebBrowserPersist,
                                  public nsIThreadRetargetableStreamListener,
                                  public nsIProgressEventSink,
                                  public nsSupportsWeakReference {
  friend class nsEncoderNodeFixup;

  // Public members
 public:
  nsWebBrowserPersist();

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSICANCELABLE
  NS_DECL_NSIWEBBROWSERPERSIST
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSITHREADRETARGETABLESTREAMLISTENER
  NS_DECL_NSIPROGRESSEVENTSINK

  // Private members
 private:
  virtual ~nsWebBrowserPersist();
  nsresult SaveURIInternal(nsIURI* aURI, nsIPrincipal* aTriggeringPrincipal,
                           nsContentPolicyType aContentPolicyType,
                           uint32_t aCacheKey, nsIReferrerInfo* aReferrerInfo,
                           nsICookieJarSettings* aCookieJarSettings,
                           nsIInputStream* aPostData, const char* aExtraHeaders,
                           nsIURI* aFile, bool aCalcFileExt, bool aIsPrivate);
  nsresult SaveChannelInternal(nsIChannel* aChannel, nsIURI* aFile,
                               bool aCalcFileExt);
  nsresult SaveDocumentInternal(nsIWebBrowserPersistDocument* aDocument,
                                nsIURI* aFile, nsIURI* aDataPath);
  nsresult SaveDocuments();
  void FinishSaveDocumentInternal(nsIURI* aFile, nsIFile* aDataPath);
  nsresult GetExtensionForContentType(const char16_t* aContentType,
                                      char16_t** aExt);

  struct CleanupData;
  struct DocData;
  struct OutputData;
  struct UploadData;
  struct URIData;
  struct WalkData;

  class OnWalk;
  class OnRemoteWalk;
  class OnWrite;
  class FlatURIMap;
  friend class OnWalk;
  friend class OnWrite;

  nsresult SaveDocumentDeferred(mozilla::UniquePtr<WalkData>&& aData);
  void Cleanup();
  void CleanupLocalFiles();
  nsresult GetValidURIFromObject(nsISupports* aObject, nsIURI** aURI) const;
  static nsresult GetLocalFileFromURI(nsIURI* aURI, nsIFile** aLocalFile);
  static nsresult AppendPathToURI(nsIURI* aURI, const nsAString& aPath,
                                  nsCOMPtr<nsIURI>& aOutURI);
  nsresult MakeAndStoreLocalFilenameInURIMap(
      nsIURI* aURI, nsIWebBrowserPersistDocument* aDoc,
      nsContentPolicyType aContentPolicyType, bool aNeedsPersisting,
      URIData** aData);
  nsresult MakeOutputStream(nsIURI* aFile, nsIURI* aSourceURI,
                            nsIOutputStream** aOutputStream);
  nsresult MakeOutputStreamFromFile(nsIFile* aFile, nsIURI* aSourceURI,
                                    nsIOutputStream** aOutputStream);
  nsresult MakeOutputStreamFromURI(nsIURI* aURI, nsIOutputStream** aOutStream);
  nsresult CreateChannelFromURI(nsIURI* aURI, nsIChannel** aChannel);
  nsresult StartUpload(nsIStorageStream* aOutStream, nsIURI* aDestinationURI,
                       const nsACString& aContentType);
  nsresult StartUpload(nsIInputStream* aInputStream, nsIURI* aDestinationURI,
                       const nsACString& aContentType);
  nsresult CalculateAndAppendFileExt(nsIURI* aURI, nsIChannel* aChannel,
                                     nsIURI* aOriginalURIWithExtension,
                                     nsCOMPtr<nsIURI>& aOutURI);
  nsresult CalculateUniqueFilename(nsIURI* aURI, nsCOMPtr<nsIURI>& aOutURI);
  nsresult MakeFilenameFromURI(nsIURI* aURI, nsString& aFilename);
  nsresult StoreURI(const nsACString& aURI, nsIWebBrowserPersistDocument* aDoc,
                    nsContentPolicyType aContentPolicyType,
                    bool aNeedsPersisting = true, URIData** aData = nullptr);
  nsresult StoreURI(nsIURI* aURI, nsIWebBrowserPersistDocument* aDoc,
                    nsContentPolicyType aContentPolicyType,
                    bool aNeedsPersisting = true, URIData** aData = nullptr);
  bool DocumentEncoderExists(const char* aContentType);

  nsresult SaveSubframeContent(nsIWebBrowserPersistDocument* aFrameContent,
                               nsIWebBrowserPersistDocument* aParentDocument,
                               const nsCString& aURISpec, URIData* aData);
  nsresult SendErrorStatusChange(bool aIsReadError, nsresult aResult,
                                 nsIRequest* aRequest, nsIURI* aURI);

  nsresult FixRedirectedChannelEntry(nsIChannel* aNewChannel);

  void FinishDownload();
  void EndDownload(nsresult aResult);
  void EndDownloadInternal(nsresult aResult);
  void SerializeNextFile();
  void CalcTotalProgress();

  void SetApplyConversionIfNeeded(nsIChannel* aChannel);

  nsCOMPtr<nsIURI> mCurrentDataPath;
  bool mCurrentDataPathIsRelative;
  nsCString mCurrentRelativePathToData;
  nsCOMPtr<nsIURI> mCurrentBaseURI;
  nsCString mCurrentCharset;
  nsCOMPtr<nsIURI> mTargetBaseURI;
  uint32_t mCurrentThingsToPersist;

  nsCOMPtr<nsIMIMEService> mMIMEService;
  nsCOMPtr<nsIURI> mURI;
  nsCOMPtr<nsIReferrerInfo> mReferrerInfo;
  nsCOMPtr<nsIWebProgressListener> mProgressListener;
  /**
   * Progress listener for 64-bit values; this is the same object as
   * mProgressListener, but is a member to avoid having to qi it for each
   * progress notification.
   */
  nsCOMPtr<nsIWebProgressListener2> mProgressListener2;
  nsCOMPtr<nsIProgressEventSink> mEventSink;
  mozilla::Mutex mOutputMapMutex MOZ_UNANNOTATED;
  nsClassHashtable<nsISupportsHashKey, OutputData> mOutputMap;
  nsClassHashtable<nsISupportsHashKey, UploadData> mUploadList;
  nsCOMPtr<nsISerialEventTarget> mBackgroundQueue;
  nsTArray<RefPtr<ClosePromise>> mFileClosePromises;
  nsClassHashtable<nsCStringHashKey, URIData> mURIMap;
  nsCOMPtr<nsIWebBrowserPersistURIMap> mFlatURIMap;
  nsTArray<mozilla::UniquePtr<WalkData>> mWalkStack;
  nsTArray<DocData*> mDocList;
  nsTArray<CleanupData*> mCleanupList;
  nsTArray<nsCString> mFilenameList;
  bool mFirstAndOnlyUse;
  bool mSavingDocument;
  // mCancel is used from both the main thread, and (inside OnDataAvailable)
  // from a background thread.
  mozilla::Atomic<bool> mCancel;
  bool mEndCalled;
  bool mCompleted;
  bool mStartSaving;
  bool mReplaceExisting;
  bool mSerializingOutput;
  bool mIsPrivate;
  // mPersistFlags can be modified on the main thread, and can be read from
  // a background thread when OnDataAvailable calls MakeOutputStreamFromFile.
  mozilla::Atomic<uint32_t> mPersistFlags;
  nsresult mPersistResult;
  // Bytes transferred by finished requests, kept so that the total progress
  // doesn't decrease once they are removed from mOutputMap and mUploadList.
  int64_t mCompletedProgress;
  int64_t mTotalCurrentProgress;
  int64_t mTotalMaxProgress;
  int16_t mWrapColumn;
  uint32_t mEncodingFlags;
  nsString mContentType;
  // Random suffix added to downloaded filenames to make them less predictable.
  nsCString mFilenameRandomSeed;
};

#endif
