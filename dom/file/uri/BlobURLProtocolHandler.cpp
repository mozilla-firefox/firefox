/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BlobURLProtocolHandler.h"

#include "BlobURLChannel.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Maybe.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/BlobURL.h"
#include "mozilla/dom/ChromeUtils.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Exceptions.h"
#include "mozilla/dom/IPCBlobUtils.h"
#include "mozilla/dom/MediaSource.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "nsClassHashtable.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsIAsyncShutdown.h"
#include "nsIDUtils.h"
#include "nsIException.h"  // for nsIStackFrame
#include "nsIMemoryReporter.h"
#include "nsIPrincipal.h"
#include "nsIUUIDGenerator.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsReadableUtils.h"

#define RELEASING_TIMER 5000

namespace mozilla {

using namespace ipc;

namespace dom {

// -----------------------------------------------------------------------
// Hash table
struct DataInfo {
  DataInfo(mozilla::dom::BlobImpl* aBlobImpl, nsIPrincipal* aPrincipal,
           const nsCString& aPartitionKey,
           const Maybe<ContentParentId>& aContentParentId)
      : mBlobImpl(aBlobImpl),
        mPrincipal(aPrincipal),
        mPartitionKey(aPartitionKey),
        mRevokeId(0),
        mContentParentId(aContentParentId) {
    MOZ_ASSERT(aPrincipal);
  }

  // NOTE: This may not be present within content processes, if the BlobURL was
  // created within a different process.
  RefPtr<BlobImpl> mBlobImpl;

  nsCOMPtr<nsIPrincipal> mPrincipal;

  nsCString mPartitionKey;

  nsCString mStack;

  // When a blobURL is revoked, we keep it alive for RELEASING_TIMER
  // milliseconds in order to support pending operations such as navigation,
  // download and so on.
  // ReleasingTimerHolder will look for this ID.
  uint64_t mRevokeId;

  Maybe<ContentParentId> mContentParentId;
};

// The mutex is locked whenever gDataTable is changed, or if gDataTable
// is accessed off-main-thread.
static StaticMutex sMutex MOZ_UNANNOTATED;

// All changes to gDataTable must happen on the main thread, while locking
// sMutex. Reading from gDataTable on the main thread may happen without
// locking, since no changes are possible. Reading it from another thread
// must also lock sMutex to prevent data races.
static nsClassHashtable<nsCStringHashKey, mozilla::dom::DataInfo>* gDataTable;

static mozilla::dom::DataInfo* GetDataInfo(const nsACString& aUri,
                                           bool aAlsoIfRevoked = false) {
  if (!gDataTable) {
    return nullptr;
  }

  // Let's remove any fragment from this URI.
  int32_t fragmentPos = aUri.FindChar('#');

  mozilla::dom::DataInfo* res;
  if (fragmentPos < 0) {
    res = gDataTable->Get(aUri);
  } else {
    res = gDataTable->Get(StringHead(aUri, fragmentPos));
  }

  if (!aAlsoIfRevoked && res && res->mRevokeId) {
    return nullptr;
  }

  return res;
}

// Memory reporting for the hash table.
void BroadcastBlobURLRegistration(const nsACString& aURI,
                                  mozilla::dom::BlobImpl* aBlobImpl,
                                  nsIPrincipal* aPrincipal,
                                  const nsCString& aPartitionKey) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aBlobImpl);
  MOZ_ASSERT(aPrincipal);

  if (XRE_IsParentProcess()) {
    dom::ContentParent::BroadcastBlobURLRegistration(aURI, aBlobImpl,
                                                     aPrincipal, aPartitionKey);
    return;
  }

  IPCBlob ipcBlob;
  nsresult rv = IPCBlobUtils::Serialize(aBlobImpl, ipcBlob);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  dom::ContentChild* cc = dom::ContentChild::GetSingleton();
  (void)NS_WARN_IF(!cc->SendStoreAndBroadcastBlobURLRegistration(
      nsCString(aURI), ipcBlob, aPrincipal, aPartitionKey));
}

void BroadcastBlobURLUnregistration(
    const nsTArray<BroadcastBlobURLUnregistrationRequest>& aRequests) {
  if (XRE_IsParentProcess()) {
    dom::ContentParent::BroadcastBlobURLUnregistration(aRequests);
    return;
  }

  dom::ContentChild* cc = dom::ContentChild::GetSingleton();
  if (cc) {
    (void)NS_WARN_IF(
        !cc->SendUnstoreAndBroadcastBlobURLUnregistration(aRequests));
  }
}

class BlobURLsReporter final : public nsIMemoryReporter {
 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD CollectReports(nsIHandleReportCallback* aCallback,
                            nsISupports* aData, bool aAnonymize) override {
    MOZ_ASSERT(NS_IsMainThread(),
               "without locking gDataTable is main-thread only");
    if (!gDataTable) {
      return NS_OK;
    }

    nsTHashMap<nsPtrHashKey<mozilla::dom::BlobImpl>, uint32_t> refCounts;

    // Determine number of URLs per mozilla::dom::BlobImpl, to handle the case
    // where it's > 1.
    for (const auto& entry : *gDataTable) {
      if (mozilla::dom::BlobImpl* blobImpl = entry.GetWeak()->mBlobImpl) {
        refCounts.LookupOrInsert(blobImpl, 0) += 1;
      }
    }

    for (const auto& entry : *gDataTable) {
      nsCStringHashKey::KeyType key = entry.GetKey();
      mozilla::dom::DataInfo* info = entry.GetWeak();
      mozilla::dom::BlobImpl* blobImpl = info->mBlobImpl;
      if (!blobImpl) {
        continue;
      }

      constexpr auto desc =
          "A blob URL allocated with URL.createObjectURL; the referenced "
          "blob cannot be freed until all URLs for it have been explicitly "
          "invalidated with URL.revokeObjectURL."_ns;
      nsAutoCString path, url, owner, specialDesc;
      uint64_t size = 0;
      uint32_t refCount = 1;
      DebugOnly<bool> blobImplWasCounted;

      blobImplWasCounted = refCounts.Get(blobImpl, &refCount);
      MOZ_ASSERT(blobImplWasCounted);
      MOZ_ASSERT(refCount > 0);

      bool isMemoryFile = blobImpl->IsMemoryFile();

      if (isMemoryFile) {
        ErrorResult rv;
        size = blobImpl->GetSize(rv);
        if (NS_WARN_IF(rv.Failed())) {
          rv.SuppressException();
          size = 0;
        }
      }

      path = isMemoryFile ? "memory-blob-urls/" : "file-blob-urls/";
      BuildPath(path, key, info, aAnonymize);

      if (refCount > 1) {
        nsAutoCString addrStr;

        addrStr = "0x";
        addrStr.AppendInt((uint64_t)(mozilla::dom::BlobImpl*)blobImpl, 16);

        path += " ";
        path.AppendInt(refCount);
        path += "@";
        path += addrStr;

        specialDesc = desc;
        specialDesc += "\n\nNOTE: This blob (address ";
        specialDesc += addrStr;
        specialDesc += ") has ";
        specialDesc.AppendInt(refCount);
        specialDesc += " URLs.";
        if (isMemoryFile) {
          specialDesc += " Its size is divided ";
          specialDesc += refCount > 2 ? "among" : "between";
          specialDesc += " them in this report.";
        }
      }

      const nsACString& descString =
          specialDesc.IsEmpty() ? static_cast<const nsACString&>(desc)
                                : static_cast<const nsACString&>(specialDesc);
      if (isMemoryFile) {
        aCallback->Callback(""_ns, path, KIND_OTHER, UNITS_BYTES,
                            size / refCount, descString, aData);
      } else {
        aCallback->Callback(""_ns, path, KIND_OTHER, UNITS_COUNT, 1, descString,
                            aData);
      }
    }

    return NS_OK;
  }

  // Initialize info->mStack to record JS stack info, if enabled.
  // The string generated here is used in ReportCallback, below.
  static void GetJSStackForBlob(mozilla::dom::DataInfo* aInfo) {
    nsCString& stack = aInfo->mStack;
    MOZ_ASSERT(stack.IsEmpty());
    const uint32_t maxFrames =
        Preferences::GetUint("memory.blob_report.stack_frames");

    if (maxFrames == 0) {
      return;
    }

    nsCOMPtr<nsIStackFrame> frame = dom::GetCurrentJSStack(maxFrames);

    nsAutoCString origin;

    aInfo->mPrincipal->GetPrePath(origin);

    // If we got a frame, we better have a current JSContext.  This is cheating
    // a bit; ideally we'd have our caller pass in a JSContext, or have
    // GetCurrentJSStack() hand out the JSContext it found.
    JSContext* cx = frame ? nsContentUtils::GetCurrentJSContext() : nullptr;

    while (frame) {
      nsCString fileName;
      frame->GetFilename(cx, fileName);

      int32_t lineNumber = frame->GetLineNumber(cx);

      if (!fileName.IsEmpty()) {
        stack += "js(";
        if (!origin.IsEmpty()) {
          // Make the file name root-relative for conciseness if possible.
          const char* originData;
          uint32_t originLen;

          originLen = origin.GetData(&originData);
          // If fileName starts with origin + "/", cut up to that "/".
          if (fileName.Length() >= originLen + 1 &&
              memcmp(fileName.get(), originData, originLen) == 0 &&
              fileName[originLen] == '/') {
            fileName.Cut(0, originLen);
          }
        }
        fileName.ReplaceChar('/', '\\');
        stack += fileName;
        if (lineNumber > 0) {
          stack += ", line=";
          stack.AppendInt(lineNumber);
        }
        stack += ")/";
      }

      frame = frame->GetCaller(cx);
    }
  }

 private:
  ~BlobURLsReporter() = default;

  static void BuildPath(nsAutoCString& path, nsCStringHashKey::KeyType aKey,
                        mozilla::dom::DataInfo* aInfo, bool anonymize) {
    nsAutoCString url, owner;
    aInfo->mPrincipal->GetAsciiSpec(owner);
    if (!owner.IsEmpty()) {
      owner.ReplaceChar('/', '\\');
      path += "owner(";
      if (anonymize) {
        path += "<anonymized>";
      } else {
        path += owner;
      }
      path += ")";
    } else {
      path += "owner unknown";
    }
    path += "/";
    if (anonymize) {
      path += "<anonymized-stack>";
    } else {
      path += aInfo->mStack;
    }
    url = aKey;
    url.ReplaceChar('/', '\\');
    if (anonymize) {
      path += "<anonymized-url>";
    } else {
      path += url;
    }
  }
};

NS_IMPL_ISUPPORTS(BlobURLsReporter, nsIMemoryReporter)

class ReleasingTimerHolder final : public Runnable,
                                   public nsITimerCallback,
                                   public nsIAsyncShutdownBlocker {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  static uint64_t NextRevokeId() {
    MOZ_ASSERT(NS_IsMainThread());

    static uint64_t sRevokeId = 0;
    return ++sRevokeId;
  }

  static void Create(uint64_t aRevokeId) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aRevokeId > 0);

    RefPtr<ReleasingTimerHolder> holder = new ReleasingTimerHolder(aRevokeId);

    // BlobURLProtocolHandler::RemoveDataEntry potentially happens late. We are
    // prepared to RevokeUri synchronously if we run after XPCOMWillShutdown,
    // but we need at least to be able to dispatch to the main thread here.
    auto raii = MakeScopeExit([holder] { holder->CancelTimerAndRevokeURIs(); });

    nsresult rv = SchedulerGroup::Dispatch(holder.forget());
    NS_ENSURE_SUCCESS_VOID(rv);

    raii.release();
  }

  // Runnable interface

  NS_IMETHOD
  Run() override {
    RefPtr<ReleasingTimerHolder> self = this;
    auto raii = MakeScopeExit([self] { self->CancelTimerAndRevokeURIs(); });

    nsresult rv = NS_NewTimerWithCallback(
        getter_AddRefs(mTimer), this, RELEASING_TIMER, nsITimer::TYPE_ONE_SHOT);
    NS_ENSURE_SUCCESS(rv, NS_OK);

    nsCOMPtr<nsIAsyncShutdownClient> phase = GetShutdownPhase();
    NS_ENSURE_TRUE(!!phase, NS_OK);

    rv = phase->AddBlocker(this, NS_LITERAL_STRING_FROM_CSTRING(__FILE__),
                           __LINE__, u"ReleasingTimerHolder shutdown"_ns);
    NS_ENSURE_SUCCESS(rv, NS_OK);

    raii.release();
    return NS_OK;
  }

  // nsITimerCallback interface

  NS_IMETHOD
  Notify(nsITimer* aTimer) override {
    RevokeURIs();
    return NS_OK;
  }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  using nsINamed::GetName;
#endif

  // nsIAsyncShutdownBlocker interface

  NS_IMETHOD
  GetName(nsAString& aName) override {
    aName.AssignLiteral("ReleasingTimerHolder for revokeID ");
    aName.AppendInt(mRevokeId);
    return NS_OK;
  }

  NS_IMETHOD
  BlockShutdown(nsIAsyncShutdownClient* aClient) override {
    CancelTimerAndRevokeURIs();
    return NS_OK;
  }

  NS_IMETHOD
  GetState(nsIPropertyBag**) override { return NS_OK; }

 private:
  explicit ReleasingTimerHolder(uint64_t aRevokeId)
      : Runnable("ReleasingTimerHolder"), mRevokeId(aRevokeId) {}

  ~ReleasingTimerHolder() override = default;

  void RevokeURIs() {
    MOZ_ASSERT(NS_IsMainThread());

    // Remove the shutting down blocker
    nsCOMPtr<nsIAsyncShutdownClient> phase = GetShutdownPhase();
    if (phase) {
      phase->RemoveBlocker(this);
    }

    {
      StaticMutexAutoLock lock(sMutex);

      if (!gDataTable) {
        return;
      }

      for (auto iter = gDataTable->Iter(); !iter.Done(); iter.Next()) {
        mozilla::dom::DataInfo* info = iter.UserData();
        MOZ_ASSERT(info);

        if (info->mRevokeId != mRevokeId) {
          // This entry does not match the current revoking operation
          continue;
        }

        iter.Remove();
      }

      if (gDataTable->Count() == 0) {
        delete gDataTable;
        gDataTable = nullptr;
      }
    }
  }

  void CancelTimerAndRevokeURIs() {
    if (mTimer) {
      mTimer->Cancel();
      mTimer = nullptr;
    }

    RevokeURIs();
  }

  static nsCOMPtr<nsIAsyncShutdownClient> GetShutdownPhase() {
    nsCOMPtr<nsIAsyncShutdownService> svc = services::GetAsyncShutdownService();
    NS_ENSURE_TRUE(!!svc, nullptr);

    nsCOMPtr<nsIAsyncShutdownClient> phase;
    nsresult rv = svc->GetXpcomWillShutdown(getter_AddRefs(phase));
    NS_ENSURE_SUCCESS(rv, nullptr);

    return phase;
  }

  uint64_t mRevokeId;
  nsCOMPtr<nsITimer> mTimer;
};

NS_IMPL_ISUPPORTS_INHERITED(ReleasingTimerHolder, Runnable, nsITimerCallback,
                            nsIAsyncShutdownBlocker)

static void AddDataEntryInternal(
    const nsACString& aURI, BlobImpl* aBlobImpl, nsIPrincipal* aPrincipal,
    const nsCString& aPartitionKey,
    Maybe<ContentParentId> aContentParentId = Nothing()) {
  MOZ_ASSERT(NS_IsMainThread(), "changing gDataTable is main-thread only");
  StaticMutexAutoLock lock(sMutex);
  if (!gDataTable) {
    gDataTable = new nsClassHashtable<nsCStringHashKey, mozilla::dom::DataInfo>;
  }

  mozilla::UniquePtr<mozilla::dom::DataInfo> info =
      mozilla::MakeUnique<mozilla::dom::DataInfo>(
          aBlobImpl, aPrincipal, aPartitionKey, aContentParentId);
  BlobURLsReporter::GetJSStackForBlob(info.get());

  gDataTable->InsertOrUpdate(aURI, std::move(info));
}

void BlobURLProtocolHandler::Init(void) {
  static bool initialized = false;

  if (!initialized) {
    initialized = true;
    RegisterStrongMemoryReporter(MakeAndAddRef<BlobURLsReporter>());
  }
}

BlobURLProtocolHandler::BlobURLProtocolHandler() { Init(); }

BlobURLProtocolHandler::~BlobURLProtocolHandler() = default;

/* static */
nsresult BlobURLProtocolHandler::AddDataEntry(mozilla::dom::BlobImpl* aBlobImpl,
                                              nsIPrincipal* aPrincipal,
                                              const nsCString& aPartitionKey,
                                              nsACString& aUri) {
  MOZ_ASSERT(aBlobImpl);
  MOZ_ASSERT(aPrincipal);

  Init();

  nsresult rv = GenerateURIString(aPrincipal, aUri);
  NS_ENSURE_SUCCESS(rv, rv);

  AddDataEntryInternal(aUri, aBlobImpl, aPrincipal, aPartitionKey);

  BroadcastBlobURLRegistration(aUri, aBlobImpl, aPrincipal, aPartitionKey);
  return NS_OK;
}

/* static */
void BlobURLProtocolHandler::AddDataEntryParent(
    const nsACString& aURI, nsIPrincipal* aPrincipal,
    const nsCString& aPartitionKey, mozilla::dom::BlobImpl* aBlobImpl,
    const ContentParentId& aContentParentId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aBlobImpl);
  AddDataEntryInternal(aURI, aBlobImpl, aPrincipal, aPartitionKey,
                       Some(aContentParentId));
}

/* static */
void BlobURLProtocolHandler::AddDataEntryChild(const nsACString& aURI,
                                               nsIPrincipal* aPrincipal,
                                               const nsCString& aPartitionKey) {
  MOZ_ASSERT(XRE_IsContentProcess());
  MOZ_ASSERT(aPrincipal);
  AddDataEntryInternal(aURI, nullptr, aPrincipal, aPartitionKey);
}

/* static */
bool BlobURLProtocolHandler::ForEachBlobURL(
    std::function<bool(mozilla::dom::BlobImpl*, nsIPrincipal*, const nsCString&,
                       const nsACString&, bool aRevoked)>&& aCb) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  if (!gDataTable) {
    return false;
  }

  for (const auto& entry : *gDataTable) {
    mozilla::dom::DataInfo* info = entry.GetWeak();
    MOZ_ASSERT(info);

    MOZ_ASSERT(info->mBlobImpl);
    if (!aCb(info->mBlobImpl, info->mPrincipal, info->mPartitionKey,
             entry.GetKey(), !!info->mRevokeId)) {
      return false;
    }
  }

  return true;
}

/*static */
void BlobURLProtocolHandler::RemoveDataEntries(
    const nsTArray<nsCString>& aURIs, bool aBroadcastToOtherProcesses) {
  MOZ_ASSERT(NS_IsMainThread(), "changing gDataTable is main-thread only");
  if (!gDataTable) {
    return;
  }

  uint64_t revokeId = ReleasingTimerHolder::NextRevokeId();
  MOZ_ASSERT(revokeId > 0);

  nsTArray<BroadcastBlobURLUnregistrationRequest> requests(aURIs.Length());
  bool revokeNeeded = false;

  {
    StaticMutexAutoLock lock(sMutex);

    for (const nsCString& uri : aURIs) {
      mozilla::dom::DataInfo* info = GetDataInfo(uri);
      if (!info) {
        continue;
      }

      info->mRevokeId = revokeId;
      revokeNeeded = true;

      if (aBroadcastToOtherProcesses) {
        requests.AppendElement(
            BroadcastBlobURLUnregistrationRequest{uri, info->mPrincipal});
      }
    }
  }

  // The timer will take care of removing the entry for real after
  // RELEASING_TIMER milliseconds. In the meantime, the
  // mozilla::dom::DataInfo, marked as revoked, will not be exposed.
  if (revokeNeeded) {
    ReleasingTimerHolder::Create(revokeId);
  }

  if (!requests.IsEmpty()) {
    BroadcastBlobURLUnregistration(requests);
  }
}

// static
void BlobURLProtocolHandler::RemoveDataEntriesPerContentParent(
    const ContentParentId& aContentParentId) {
  MOZ_ASSERT(NS_IsMainThread(), "changing gDataTable is main-thread only");
  if (!gDataTable) {
    return;
  }

  uint64_t revokeId = ReleasingTimerHolder::NextRevokeId();
  MOZ_ASSERT(revokeId > 0);
  bool revokeNeeded = false;

  {
    StaticMutexAutoLock lock(sMutex);

    for (const auto& entry : *gDataTable) {
      mozilla::dom::DataInfo* info = entry.GetWeak();
      MOZ_ASSERT(info);

      if (!info->mContentParentId.isSome() ||
          info->mContentParentId.value() != aContentParentId) {
        continue;
      }

      info->mRevokeId = revokeId;
      revokeNeeded = true;
    }
  }

  // The timer will take care of removing the entry for real after
  // RELEASING_TIMER milliseconds. In the meantime, the
  // mozilla::dom::DataInfo, marked as revoked, will not be exposed.
  if (revokeNeeded) {
    ReleasingTimerHolder::Create(revokeId);
  }
}

/*static */
bool BlobURLProtocolHandler::RemoveDataEntry(const nsACString& aUri,
                                             nsIPrincipal* aPrincipal,
                                             const nsCString& aPartitionKey) {
  MOZ_ASSERT(NS_IsMainThread(), "changing gDataTable is main-thread only");
  if (!gDataTable) {
    return false;
  }

  mozilla::dom::DataInfo* info = GetDataInfo(aUri);
  if (!info) {
    return false;
  }

  if (!aPrincipal || !aPrincipal->Subsumes(info->mPrincipal)) {
    return false;
  }

  if (StaticPrefs::privacy_partition_bloburl_per_partition_key() &&
      !aPartitionKey.IsEmpty() && !info->mPartitionKey.IsEmpty() &&
      !aPartitionKey.Equals(info->mPartitionKey)) {
    return false;
  }

  RemoveDataEntries(nsTArray{nsCString(aUri)}, true);
  return true;
}

/* static */
void BlobURLProtocolHandler::RemoveDataEntries() {
  MOZ_ASSERT(NS_IsMainThread(), "changing gDataTable is main-thread only");
  StaticMutexAutoLock lock(sMutex);
  if (!gDataTable) {
    return;
  }

  gDataTable->Clear();
  delete gDataTable;
  gDataTable = nullptr;
}

/* static */
bool BlobURLProtocolHandler::HasDataEntryTypeBlob(const nsACString& aUri) {
  MOZ_ASSERT(NS_IsMainThread(),
             "without locking gDataTable is main-thread only");
  DataInfo* info = GetDataInfo(aUri);
  return info != nullptr;
}

/* static */
nsresult BlobURLProtocolHandler::GenerateURIString(nsIPrincipal* aPrincipal,
                                                   nsACString& aUri) {
  NS_ENSURE_ARG(aPrincipal);

  nsID id{};
  nsresult rv = nsID::GenerateUUIDInPlace(id);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsAutoCString origin;
  if (NS_FAILED(aPrincipal->GetWebExposedOriginSerialization(origin))) {
    // Special case the system principal to have a "system" origin part, so that
    // the system principal can be recovered from the URI.
    // See bug 2006467 for removing system principal Blob URLs in the future.
    origin = aPrincipal->IsSystemPrincipal() ? "system"_ns : "null"_ns;
  }

  aUri = BLOBURI_SCHEME ":"_ns + origin + "/"_ns + NSID_TrimBracketsASCII(id);

  return NS_OK;
}

/* static */
bool BlobURLProtocolHandler::GetDataEntry(
    const nsACString& aUri, mozilla::dom::BlobImpl** aBlobImpl,
    nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
    const OriginAttributes& aOriginAttributes, uint64_t aInnerWindowId,
    const nsCString& aPartitionKey, bool aAlsoIfRevoked) {
  MOZ_ASSERT(NS_IsMainThread(),
             "without locking gDataTable is main-thread only");
  MOZ_ASSERT(aTriggeringPrincipal);

  if (!gDataTable) {
    return false;
  }

  mozilla::dom::DataInfo* info = GetDataInfo(aUri, aAlsoIfRevoked);
  if (!info) {
    return false;
  }

  // We want to be sure that we stop the creation of the channel if the blob
  // URL is copy-and-pasted on a different context (ex. private browsing or
  // containers).
  //
  // We also allow the system principal to create the channel regardless of
  // the OriginAttributes.  This is primarily for the benefit of mechanisms
  // like the Download API that explicitly create a channel with the system
  // principal and which is never mutated to have a non-zero
  // mPrivateBrowsingId or container.

  if ((NS_WARN_IF(!aLoadingPrincipal) ||
       !aLoadingPrincipal->IsSystemPrincipal()) &&
      NS_WARN_IF(!ChromeUtils::IsOriginAttributesEqualIgnoringFPD(
          aOriginAttributes,
          BasePrincipal::Cast(info->mPrincipal)->OriginAttributesRef()))) {
    return false;
  }

  if (NS_WARN_IF(!aTriggeringPrincipal->Subsumes(info->mPrincipal))) {
    return false;
  }

  if (StaticPrefs::privacy_partition_bloburl_per_partition_key() &&
      !aPartitionKey.IsEmpty() && !info->mPartitionKey.IsEmpty() &&
      !aPartitionKey.Equals(info->mPartitionKey)) {
    nsAutoString localizedMsg;
    AutoTArray<nsString, 1> param;
    CopyUTF8toUTF16(aUri, *param.AppendElement());
    nsresult rv = nsContentUtils::FormatLocalizedString(
        PropertiesFile::DOM_PROPERTIES, "PartitionKeyDifferentError", param,
        localizedMsg);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }

    nsContentUtils::ReportToConsoleByWindowID(
        localizedMsg, nsIScriptError::errorFlag, "DOM"_ns, aInnerWindowId);
    return false;
  }

  RefPtr<mozilla::dom::BlobImpl> blobImpl = info->mBlobImpl;
  blobImpl.forget(aBlobImpl);

  return true;
}

NS_IMPL_ISUPPORTS(BlobURLProtocolHandler, nsIProtocolHandler,
                  nsISupportsWeakReference)

/* static */ nsresult BlobURLProtocolHandler::CreateNewURI(
    const nsACString& aSpec, const char* aCharset, nsIURI* aBaseURI,
    nsIURI** aResult) {
  *aResult = nullptr;

  // This method can be called on any thread, which is why we lock the mutex
  // for read access to gDataTable.
  bool revoked = true;
  nsCOMPtr<nsIPrincipal> principal;
  {
    StaticMutexAutoLock lock(sMutex);
    mozilla::dom::DataInfo* info = GetDataInfo(aSpec);
    revoked = !info || info->mRevokeId != 0;
    principal = info ? info->mPrincipal : nullptr;
  }

  return NS_MutateURI(new BlobURL::Mutator())
      .SetSpec(aSpec)
      .Apply(&nsIBlobURLMutator::SetRevoked, revoked)
      .Apply(&nsIBlobURLMutator::MaybeSetNullPrincipal, principal)
      .Finalize(aResult);
}

NS_IMETHODIMP
BlobURLProtocolHandler::NewChannel(nsIURI* aURI, nsILoadInfo* aLoadInfo,
                                   nsIChannel** aResult) {
  auto channel = MakeRefPtr<BlobURLChannel>(aURI, aLoadInfo);
  channel.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
BlobURLProtocolHandler::AllowPort(int32_t port, const char* scheme,
                                  bool* _retval) {
  // don't override anything.
  *_retval = false;
  return NS_OK;
}

NS_IMETHODIMP
BlobURLProtocolHandler::GetScheme(nsACString& result) {
  result.AssignLiteral(BLOBURI_SCHEME);
  return NS_OK;
}

/* static */
bool BlobURLProtocolHandler::GetBlobURLPrincipal(nsIURI* aURI,
                                                 const OriginAttributes& aAttrs,
                                                 nsIPrincipal** aPrincipal) {
  MOZ_ASSERT(aURI);
  MOZ_ASSERT(aPrincipal);

  RefPtr<BlobURL> blobURL = do_QueryObject(aURI);
  if (!blobURL) {
    return false;
  }

  nsCOMPtr<nsIPrincipal> principal;

  nsDependentCSubstring originPart = blobURL->OriginPart();
  if (originPart == "system"_ns) {
    principal = nsContentUtils::GetSystemPrincipal();
  } else if (originPart == "null"_ns) {
    // If the origin part is the string "null", the principal should have been
    // cached on the URI when it was parsed.
    principal = blobURL->GetNullPrincipal();

    // If the principal has the "null" origin part, it cannot be a
    // BlobURLBroadcastPrincipal, as in the future we will not broadcast
    // registrations, so the principal would not be available.
    MOZ_DIAGNOSTIC_ASSERT(!principal ||
                          !IsBlobURLBroadcastPrincipal(principal));
  } else {
    // The web-exposed origin serialization is present, we have an origin URI.
    nsCOMPtr<nsIURI> uri;
    nsresult rv = NS_NewURI(getter_AddRefs(uri), originPart);
    NS_ENSURE_SUCCESS(rv, false);

    principal = BasePrincipal::CreateContentPrincipal(uri, aAttrs);

    // Ensure the principal's origin serialization matches the origin part,
    // otherwise we'll treat the origin part as invalid.
    nsAutoCString serialization;
    rv = principal->GetWebExposedOriginSerialization(serialization);
    if (NS_WARN_IF(NS_FAILED(rv)) || NS_WARN_IF(originPart != serialization)) {
      return false;
    }
  }
  if (!principal) {
    return false;
  }

  principal.forget(aPrincipal);
  return true;
}

bool BlobURLProtocolHandler::IsBlobURLBroadcastPrincipal(
    nsIPrincipal* aPrincipal) {
  return aPrincipal->IsSystemPrincipal() ||
         aPrincipal->GetIsAddonOrExpandedAddonPrincipal();
}

}  // namespace dom
}  // namespace mozilla

namespace mozilla::dom {

bool IsBlobURI(nsIURI* aUri) {
  RefPtr<BlobURL> blobURL = do_QueryObject(aUri);
  return blobURL != nullptr;
}

}  // namespace mozilla::dom
