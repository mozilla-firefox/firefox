/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSHistory.h"

#include <algorithm>

#include "nsContentUtils.h"
#include "nsCOMArray.h"
#include "nsComponentManagerUtils.h"
#include "nsDocShell.h"
#include "nsFrameLoaderOwner.h"
#include "nsHashKeys.h"
#include "nsIDocShell.h"
#include "nsIDocumentViewer.h"
#include "nsDocShellLoadState.h"
#include "nsIDocShellTreeItem.h"
#include "nsILayoutHistoryState.h"
#include "nsIObserverService.h"
#include "nsISHEntry.h"
#include "nsISHistoryListener.h"
#include "nsIURI.h"
#include "nsIXULRuntime.h"
#include "nsNetUtil.h"
#include "nsTHashMap.h"
#include "nsSHEntry.h"
#include "SessionHistoryEntry.h"
#include "nsTArray.h"
#include "prsystem.h"

#include "mozilla/Attributes.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/RemoteWebProgressRequest.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/LinkedList.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProcessPriorityManager.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "nsIWebNavigation.h"
#include "nsDocShellLoadTypes.h"
#include "base/process.h"

using namespace mozilla;
using namespace mozilla::dom;

#define PREF_SHISTORY_SIZE "browser.sessionhistory.max_entries"
#define PREF_SHISTORY_MAX_TOTAL_VIEWERS \
  "browser.sessionhistory.max_total_viewers"
#define CONTENT_VIEWER_TIMEOUT_SECONDS \
  "browser.sessionhistory.contentViewerTimeout"
// Observe fission.bfcacheInParent so that BFCache can be enabled/disabled when
// the pref is changed.
#define PREF_FISSION_BFCACHEINPARENT "fission.bfcacheInParent"

// Default this to time out unused content viewers after 30 minutes
#define CONTENT_VIEWER_TIMEOUT_SECONDS_DEFAULT (30 * 60)

static constexpr const char* kObservedPrefs[] = {
    PREF_SHISTORY_SIZE, PREF_SHISTORY_MAX_TOTAL_VIEWERS,
    PREF_FISSION_BFCACHEINPARENT, nullptr};

static int32_t gHistoryMaxSize = 50;

// List of all SHistory objects, used for content viewer cache eviction.
// When being destroyed, this helper removes everything from the list to avoid
// assertions when we leak.
struct ListHelper {
#ifdef DEBUG
  ~ListHelper() { mList.clear(); }
#endif  // DEBUG

  LinkedList<nsSHistory> mList;
};

MOZ_RUNINIT static ListHelper gSHistoryList;
// Max viewers allowed total, across all SHistory objects - negative default
// means we will calculate how many viewers to cache based on total memory
int32_t nsSHistory::sHistoryMaxTotalViewers = -1;

// A counter that is used to be able to know the order in which
// entries were touched, so that we can evict older entries first.
static uint32_t gTouchCounter = 0;

extern mozilla::LazyLogModule gSHLog;

LazyLogModule gSHistoryLog("nsSHistory");

#define LOG(format) MOZ_LOG(gSHistoryLog, mozilla::LogLevel::Debug, format)

extern mozilla::LazyLogModule gPageCacheLog;
extern mozilla::LazyLogModule gNavigationLog;
extern mozilla::LazyLogModule gSHIPBFCacheLog;

// This macro makes it easier to print a log message which includes a URI's
// spec.  Example use:
//
//  nsIURI *uri = [...];
//  LOG_SPEC(("The URI is %s.", _spec), uri);
//
#define LOG_SPEC(format, uri)                        \
  PR_BEGIN_MACRO                                     \
  if (MOZ_LOG_TEST(gSHistoryLog, LogLevel::Debug)) { \
    nsAutoCString _specStr("(null)"_ns);             \
    if (uri) {                                       \
      _specStr = uri->GetSpecOrDefault();            \
    }                                                \
    const char* _spec = _specStr.get();              \
    LOG(format);                                     \
  }                                                  \
  PR_END_MACRO

// This macro makes it easy to log a message including an SHEntry's URI.
// For example:
//
//  nsCOMPtr<nsISHEntry> shentry = [...];
//  LOG_SHENTRY_SPEC(("shentry %p has uri %s.", shentry.get(), _spec), shentry);
//
#define LOG_SHENTRY_SPEC(format, shentry)            \
  PR_BEGIN_MACRO                                     \
  if (MOZ_LOG_TEST(gSHistoryLog, LogLevel::Debug)) { \
    nsCOMPtr<nsIURI> uri = shentry->GetURI();        \
    LOG_SPEC(format, uri);                           \
  }                                                  \
  PR_END_MACRO

// Calls a F on all registered session history listeners.
template <typename F>
static void NotifyListeners(nsAutoTObserverArray<nsWeakPtr, 2>& aListeners,
                            F&& f) {
  for (const nsWeakPtr& weakPtr : aListeners.EndLimitedRange()) {
    nsCOMPtr<nsISHistoryListener> listener = do_QueryReferent(weakPtr);
    if (listener) {
      f(listener);
    }
  }
}

class MOZ_STACK_CLASS SHistoryChangeNotifier {
 public:
  explicit SHistoryChangeNotifier(nsSHistory* aHistory) {
    // If we're already in an update, the outermost change notifier will
    // update browsing context in the destructor.
    if (!aHistory->HasOngoingUpdate()) {
      aHistory->SetHasOngoingUpdate(true);
      mSHistory = aHistory;
    }
  }

  ~SHistoryChangeNotifier() {
    if (mSHistory) {
      MOZ_ASSERT(mSHistory->HasOngoingUpdate());
      mSHistory->SetHasOngoingUpdate(false);

      RefPtr<BrowsingContext> rootBC = mSHistory->GetBrowsingContext();
      if (mozilla::SessionHistoryInParent() && rootBC) {
        rootBC->Canonical()->HistoryCommitIndexAndLength();
      }
    }
  }

  RefPtr<nsSHistory> mSHistory;
};

enum HistCmd { HIST_CMD_GOTOINDEX, HIST_CMD_RELOAD };

class nsSHistoryObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  nsSHistoryObserver() {}

  static void PrefChanged(const char* aPref, void* aSelf);
  void PrefChanged(const char* aPref);

 protected:
  ~nsSHistoryObserver() {}
};

StaticRefPtr<nsSHistoryObserver> gObserver;

NS_IMPL_ISUPPORTS(nsSHistoryObserver, nsIObserver)

// static
void nsSHistoryObserver::PrefChanged(const char* aPref, void* aSelf) {
  static_cast<nsSHistoryObserver*>(aSelf)->PrefChanged(aPref);
}

void nsSHistoryObserver::PrefChanged(const char* aPref) {
  nsSHistory::UpdatePrefs();
  nsSHistory::GloballyEvictDocumentViewers();
}

NS_IMETHODIMP
nsSHistoryObserver::Observe(nsISupports* aSubject, const char* aTopic,
                            const char16_t* aData) {
  if (!strcmp(aTopic, "cacheservice:empty-cache") ||
      !strcmp(aTopic, "memory-pressure")) {
    nsSHistory::GloballyEvictAllDocumentViewers();
  }

  return NS_OK;
}

void nsSHistory::EvictDocumentViewerForEntry(nsISHEntry* aEntry) {
  nsCOMPtr<nsIDocumentViewer> viewer = aEntry->GetDocumentViewer();
  if (viewer) {
    LOG_SHENTRY_SPEC(("Evicting content viewer 0x%p for "
                      "owning SHEntry 0x%p at %s.",
                      viewer.get(), aEntry, _spec),
                     aEntry);

    // Drop the presentation state before destroying the viewer, so that
    // document teardown is able to correctly persist the state.
    NotifyListenersDocumentViewerEvicted(1);
    aEntry->SetDocumentViewer(nullptr);
    aEntry->SyncPresentationState();
    viewer->Destroy();
  } else if (nsCOMPtr<SessionHistoryEntry> she = do_QueryInterface(aEntry)) {
    if (RefPtr<nsFrameLoader> frameLoader = she->GetFrameLoader()) {
      nsCOMPtr<nsFrameLoaderOwner> owner =
          do_QueryInterface(frameLoader->GetOwnerContent());
      RefPtr<nsFrameLoader> currentFrameLoader;
      if (owner) {
        currentFrameLoader = owner->GetFrameLoader();
      }

      // Only destroy non-current frameloader when evicting from the bfcache.
      if (currentFrameLoader != frameLoader) {
        MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
                ("nsSHistory::EvictDocumentViewerForEntry "
                 "destroying an nsFrameLoader."));
        NotifyListenersDocumentViewerEvicted(1);
        she->SetFrameLoader(nullptr);
        frameLoader->Destroy();
      }
    }
  }

  // When dropping bfcache, we have to remove associated dynamic entries as
  // well.
  int32_t index = GetIndexOfEntry(aEntry);
  if (index != -1) {
    RemoveDynEntries(index, aEntry);
  }
}

nsSHistory::nsSHistory(BrowsingContext* aRootBC)
    : mRootBC(aRootBC->Id()),
      mHasOngoingUpdate(false),
      mIndex(-1),
      mRequestedIndex(-1),
      mRootDocShellID(aRootBC->GetHistoryID()) {
  static bool sCalledStartup = false;
  if (!sCalledStartup) {
    Startup();
    sCalledStartup = true;
  }

  // Add this new SHistory object to the list
  gSHistoryList.mList.insertBack(this);

  // Init mHistoryTracker on setting mRootBC so we can bind its event
  // target to the tabGroup.
  mHistoryTracker = mozilla::MakeUnique<HistoryTracker>(
      this,
      mozilla::Preferences::GetUint(CONTENT_VIEWER_TIMEOUT_SECONDS,
                                    CONTENT_VIEWER_TIMEOUT_SECONDS_DEFAULT),
      GetCurrentSerialEventTarget());
}

nsSHistory::~nsSHistory() {
  // Clear mEntries explicitly here so that the destructor of the entries
  // can still access nsSHistory in a reasonable way.
  mEntries.Clear();
}

NS_IMPL_ADDREF(nsSHistory)
NS_IMPL_RELEASE(nsSHistory)

NS_INTERFACE_MAP_BEGIN(nsSHistory)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsISHistory)
  NS_INTERFACE_MAP_ENTRY(nsISHistory)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

// static
uint32_t nsSHistory::CalcMaxTotalViewers() {
// This value allows tweaking how fast the allowed amount of content viewers
// grows with increasing amounts of memory. Larger values mean slower growth.
#ifdef ANDROID
#  define MAX_TOTAL_VIEWERS_BIAS 15.9
#else
#  define MAX_TOTAL_VIEWERS_BIAS 14
#endif

  // Calculate an estimate of how many DocumentViewers we should cache based
  // on RAM.  This assumes that the average DocumentViewer is 4MB (conservative)
  // and caps the max at 8 DocumentViewers
  //
  // TODO: Should we split the cache memory betw. DocumentViewer caching and
  // nsCacheService?
  //
  // RAM    | DocumentViewers | on Android
  // -------------------------------------
  // 32   Mb       0                0
  // 64   Mb       1                0
  // 128  Mb       2                0
  // 256  Mb       3                1
  // 512  Mb       5                2
  // 768  Mb       6                2
  // 1024 Mb       8                3
  // 2048 Mb       8                5
  // 3072 Mb       8                7
  // 4096 Mb       8                8
  uint64_t bytes = PR_GetPhysicalMemorySize();

  if (bytes == 0) {
    return 0;
  }

  // Conversion from unsigned int64_t to double doesn't work on all platforms.
  // We need to truncate the value at INT64_MAX to make sure we don't
  // overflow.
  if (bytes > INT64_MAX) {
    bytes = INT64_MAX;
  }

  double kBytesD = (double)(bytes >> 10);

  // This is essentially the same calculation as for nsCacheService,
  // except that we divide the final memory calculation by 4, since
  // we assume each DocumentViewer takes on average 4MB
  uint32_t viewers = 0;
  double x = std::log(kBytesD) / std::log(2.0) - MAX_TOTAL_VIEWERS_BIAS;
  if (x > 0) {
    viewers = (uint32_t)(x * x - x + 2.001);  // add .001 for rounding
    viewers /= 4;
  }

  // Cap it off at 8 max
  if (viewers > 8) {
    viewers = 8;
  }
  return viewers;
}

// static
void nsSHistory::UpdatePrefs() {
  Preferences::GetInt(PREF_SHISTORY_SIZE, &gHistoryMaxSize);
  if (mozilla::SessionHistoryInParent() && !mozilla::BFCacheInParent()) {
    sHistoryMaxTotalViewers = 0;
    return;
  }

  Preferences::GetInt(PREF_SHISTORY_MAX_TOTAL_VIEWERS,
                      &sHistoryMaxTotalViewers);
  // If the pref is negative, that means we calculate how many viewers
  // we think we should cache, based on total memory
  if (sHistoryMaxTotalViewers < 0) {
    sHistoryMaxTotalViewers = CalcMaxTotalViewers();
  }
}

// static
nsresult nsSHistory::Startup() {
  UpdatePrefs();

  // The goal of this is to unbreak users who have inadvertently set their
  // session history size to less than the default value.
  int32_t defaultHistoryMaxSize =
      Preferences::GetInt(PREF_SHISTORY_SIZE, 50, PrefValueKind::Default);
  if (gHistoryMaxSize < defaultHistoryMaxSize) {
    gHistoryMaxSize = defaultHistoryMaxSize;
  }

  // Allow the user to override the max total number of cached viewers,
  // but keep the per SHistory cached viewer limit constant
  if (!gObserver) {
    gObserver = new nsSHistoryObserver();
    Preferences::RegisterCallbacks(nsSHistoryObserver::PrefChanged,
                                   kObservedPrefs, gObserver.get());

    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    if (obsSvc) {
      // Observe empty-cache notifications so tahat clearing the disk/memory
      // cache will also evict all content viewers.
      obsSvc->AddObserver(gObserver, "cacheservice:empty-cache", false);

      // Same for memory-pressure notifications
      obsSvc->AddObserver(gObserver, "memory-pressure", false);
    }
  }

  return NS_OK;
}

// static
void nsSHistory::Shutdown() {
  if (gObserver) {
    Preferences::UnregisterCallbacks(nsSHistoryObserver::PrefChanged,
                                     kObservedPrefs, gObserver.get());

    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    if (obsSvc) {
      obsSvc->RemoveObserver(gObserver, "cacheservice:empty-cache");
      obsSvc->RemoveObserver(gObserver, "memory-pressure");
    }
    gObserver = nullptr;
  }
}

// static
already_AddRefed<nsISHEntry> nsSHistory::GetRootSHEntry(nsISHEntry* aEntry) {
  nsCOMPtr<nsISHEntry> rootEntry = aEntry;
  nsCOMPtr<nsISHEntry> result = nullptr;
  while (rootEntry) {
    result = rootEntry;
    rootEntry = result->GetParent();
  }

  return result.forget();
}

// static
nsresult nsSHistory::WalkHistoryEntries(nsISHEntry* aRootEntry,
                                        BrowsingContext* aBC,
                                        WalkHistoryEntriesFunc aCallback,
                                        void* aData) {
  NS_ENSURE_TRUE(aRootEntry, NS_ERROR_FAILURE);

  int32_t childCount = aRootEntry->GetChildCount();
  for (int32_t i = 0; i < childCount; i++) {
    nsCOMPtr<nsISHEntry> childEntry;
    aRootEntry->GetChildAt(i, getter_AddRefs(childEntry));
    if (!childEntry) {
      // childEntry can be null for valid reasons, for example if the
      // docshell at index i never loaded anything useful.
      // Remember to clone also nulls in the child array (bug 464064).
      aCallback(nullptr, nullptr, i, aData);
      continue;
    }

    BrowsingContext* childBC = nullptr;
    if (aBC) {
      for (BrowsingContext* child : aBC->Children()) {
        // If the SH pref is on and we are in the parent process, update
        // canonical BC directly
        bool foundChild = false;
        if (mozilla::SessionHistoryInParent() && XRE_IsParentProcess()) {
          if (child->Canonical()->HasHistoryEntry(childEntry)) {
            childBC = child;
            foundChild = true;
          }
        }

        nsDocShell* docshell = static_cast<nsDocShell*>(child->GetDocShell());
        if (docshell && docshell->HasHistoryEntry(childEntry)) {
          childBC = docshell->GetBrowsingContext();
          foundChild = true;
        }

        // XXX Simplify this once the old and new session history
        // implementations don't run at the same time.
        if (foundChild) {
          break;
        }
      }
    }

    nsresult rv = aCallback(childEntry, childBC, i, aData);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

// callback data for WalkHistoryEntries
struct MOZ_STACK_CLASS CloneAndReplaceData {
  CloneAndReplaceData(uint32_t aCloneID, nsISHEntry* aReplaceEntry,
                      bool aCloneChildren, nsISHEntry* aDestTreeParent)
      : cloneID(aCloneID),
        cloneChildren(aCloneChildren),
        replaceEntry(aReplaceEntry),
        destTreeParent(aDestTreeParent) {}

  uint32_t cloneID;
  bool cloneChildren;
  nsISHEntry* replaceEntry;
  nsISHEntry* destTreeParent;
  nsCOMPtr<nsISHEntry> resultEntry;
};

nsresult nsSHistory::CloneAndReplaceChild(nsISHEntry* aEntry,
                                          BrowsingContext* aOwnerBC,
                                          int32_t aChildIndex, void* aData) {
  nsCOMPtr<nsISHEntry> dest;

  CloneAndReplaceData* data = static_cast<CloneAndReplaceData*>(aData);
  uint32_t cloneID = data->cloneID;
  nsISHEntry* replaceEntry = data->replaceEntry;

  if (!aEntry) {
    if (data->destTreeParent) {
      data->destTreeParent->AddChild(nullptr, aChildIndex);
    }
    return NS_OK;
  }

  uint32_t srcID = aEntry->GetID();

  nsresult rv = NS_OK;
  if (srcID == cloneID) {
    // Replace the entry
    dest = replaceEntry;
  } else {
    // Clone the SHEntry...
    rv = aEntry->Clone(getter_AddRefs(dest));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  dest->SetIsSubFrame(true);

  if (srcID != cloneID || data->cloneChildren) {
    // Walk the children
    CloneAndReplaceData childData(cloneID, replaceEntry, data->cloneChildren,
                                  dest);
    rv = nsSHistory::WalkHistoryEntries(aEntry, aOwnerBC, CloneAndReplaceChild,
                                        &childData);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (srcID != cloneID && aOwnerBC) {
    nsSHistory::HandleEntriesToSwapInDocShell(aOwnerBC, aEntry, dest);
  }

  if (data->destTreeParent) {
    data->destTreeParent->AddChild(dest, aChildIndex);
  }
  data->resultEntry = dest;
  return rv;
}

// static
nsresult nsSHistory::CloneAndReplace(
    nsISHEntry* aSrcEntry, BrowsingContext* aOwnerBC, uint32_t aCloneID,
    nsISHEntry* aReplaceEntry, bool aCloneChildren, nsISHEntry** aDestEntry) {
  NS_ENSURE_ARG_POINTER(aDestEntry);
  NS_ENSURE_TRUE(aReplaceEntry, NS_ERROR_FAILURE);
  CloneAndReplaceData data(aCloneID, aReplaceEntry, aCloneChildren, nullptr);
  nsresult rv = CloneAndReplaceChild(aSrcEntry, aOwnerBC, 0, &data);
  data.resultEntry.swap(*aDestEntry);
  return rv;
}

// static
void nsSHistory::WalkContiguousEntries(
    nsISHEntry* aEntry, const std::function<void(nsISHEntry*)>& aCallback) {
  MOZ_ASSERT(aEntry);

  nsCOMPtr<nsISHistory> shistory = aEntry->GetShistory();
  if (!shistory) {
    // If there is no session history in the entry, it means this is not a root
    // entry. So, we can return from here.
    return;
  }

  int32_t index = shistory->GetIndexOfEntry(aEntry);
  int32_t count = shistory->GetCount();

  nsCOMPtr<nsIURI> targetURI = aEntry->GetURI();

  // First, call the callback on the input entry.
  aCallback(aEntry);

  // Walk backward to find the entries that have the same origin as the
  // input entry.
  for (int32_t i = index - 1; i >= 0; i--) {
    RefPtr<nsISHEntry> entry;
    shistory->GetEntryAtIndex(i, getter_AddRefs(entry));
    if (entry) {
      nsCOMPtr<nsIURI> uri = entry->GetURI();
      if (NS_FAILED(nsContentUtils::GetSecurityManager()->CheckSameOriginURI(
              targetURI, uri, false, false))) {
        break;
      }

      aCallback(entry);
    }
  }

  // Then, Walk forward.
  for (int32_t i = index + 1; i < count; i++) {
    RefPtr<nsISHEntry> entry;
    shistory->GetEntryAtIndex(i, getter_AddRefs(entry));
    if (entry) {
      nsCOMPtr<nsIURI> uri = entry->GetURI();
      if (NS_FAILED(nsContentUtils::GetSecurityManager()->CheckSameOriginURI(
              targetURI, uri, false, false))) {
        break;
      }

      aCallback(entry);
    }
  }
}

// static
void nsSHistory::WalkContiguousEntriesInOrder(
    nsISHEntry* aEntry, const std::function<void(nsISHEntry*)>& aCallback) {
  MOZ_ASSERT(aEntry);

  nsCOMPtr<nsISHistory> shistory = aEntry->GetShistory();
  if (!shistory) {
    return;
  }

  int32_t index = shistory->GetIndexOfEntry(aEntry);
  int32_t count = shistory->GetCount();

  nsCOMPtr<nsIURI> targetURI = aEntry->GetURI();

  // Walk backward to find the entries that have the same origin as the
  // input entry.
  int32_t lowerBound = index;
  for (int32_t i = index - 1; i >= 0; i--) {
    RefPtr<nsISHEntry> entry;
    shistory->GetEntryAtIndex(i, getter_AddRefs(entry));
    if (!entry) {
      continue;
    }
    nsCOMPtr<nsIURI> uri = entry->GetURI();
    if (NS_FAILED(nsContentUtils::GetSecurityManager()->CheckSameOriginURI(
            targetURI, uri, false, false))) {
      break;
    }
    lowerBound = i;
  }
  for (int32_t i = lowerBound; i < index; i++) {
    RefPtr<nsISHEntry> entry;
    shistory->GetEntryAtIndex(i, getter_AddRefs(entry));
    MOZ_ASSERT(entry);
    aCallback(entry);
  }

  // Then, call the callback on the input entry.
  aCallback(aEntry);

  // Then, Walk forward.
  for (int32_t i = index + 1; i < count; i++) {
    RefPtr<nsISHEntry> entry;
    shistory->GetEntryAtIndex(i, getter_AddRefs(entry));
    if (!entry) {
      continue;
    }
    nsCOMPtr<nsIURI> uri = entry->GetURI();
    if (NS_FAILED(nsContentUtils::GetSecurityManager()->CheckSameOriginURI(
            targetURI, uri, false, false))) {
      break;
    }
    aCallback(entry);
  }
}

NS_IMETHODIMP
nsSHistory::AddChildSHEntryHelper(nsISHEntry* aCloneRef, nsISHEntry* aNewEntry,
                                  BrowsingContext* aRootBC,
                                  bool aCloneChildren) {
  MOZ_ASSERT(aRootBC->IsTop());

  /* You are currently in the rootDocShell.
   * You will get here when a subframe has a new url
   * to load and you have walked up the tree all the
   * way to the top to clone the current SHEntry hierarchy
   * and replace the subframe where a new url was loaded with
   * a new entry.
   */
  nsCOMPtr<nsISHEntry> child;
  nsCOMPtr<nsISHEntry> currentHE;
  int32_t index = mIndex;
  if (index < 0) {
    return NS_ERROR_FAILURE;
  }

  GetEntryAtIndex(index, getter_AddRefs(currentHE));
  NS_ENSURE_TRUE(currentHE, NS_ERROR_FAILURE);

  nsresult rv = NS_OK;
  uint32_t cloneID = aCloneRef->GetID();
  rv = nsSHistory::CloneAndReplace(currentHE, aRootBC, cloneID, aNewEntry,
                                   aCloneChildren, getter_AddRefs(child));

  if (NS_SUCCEEDED(rv)) {
    rv = AddEntry(child, true);
    if (NS_SUCCEEDED(rv)) {
      child->SetDocshellID(aRootBC->GetHistoryID());
    }
  }

  return rv;
}

nsresult nsSHistory::SetChildHistoryEntry(nsISHEntry* aEntry,
                                          BrowsingContext* aBC,
                                          int32_t aEntryIndex, void* aData) {
  SwapEntriesData* data = static_cast<SwapEntriesData*>(aData);
  if (!aBC || aBC == data->ignoreBC) {
    return NS_OK;
  }

  nsISHEntry* destTreeRoot = data->destTreeRoot;

  nsCOMPtr<nsISHEntry> destEntry;

  if (data->destTreeParent) {
    // aEntry is a clone of some child of destTreeParent, but since the
    // trees aren't necessarily in sync, we'll have to locate it.
    // Note that we could set aShell's entry to null if we don't find a
    // corresponding entry under destTreeParent.

    uint32_t targetID = aEntry->GetID();

    // First look at the given index, since this is the common case.
    nsCOMPtr<nsISHEntry> entry;
    data->destTreeParent->GetChildAt(aEntryIndex, getter_AddRefs(entry));
    if (entry && entry->GetID() == targetID) {
      destEntry.swap(entry);
    } else {
      int32_t childCount;
      data->destTreeParent->GetChildCount(&childCount);
      for (int32_t i = 0; i < childCount; ++i) {
        data->destTreeParent->GetChildAt(i, getter_AddRefs(entry));
        if (!entry) {
          continue;
        }

        if (entry->GetID() == targetID) {
          destEntry.swap(entry);
          break;
        }
      }
    }
  } else {
    destEntry = destTreeRoot;
  }

  nsSHistory::HandleEntriesToSwapInDocShell(aBC, aEntry, destEntry);
  // Now handle the children of aEntry.
  SwapEntriesData childData = {data->ignoreBC, destTreeRoot, destEntry};
  return nsSHistory::WalkHistoryEntries(aEntry, aBC, SetChildHistoryEntry,
                                        &childData);
}

// static
void nsSHistory::HandleEntriesToSwapInDocShell(
    mozilla::dom::BrowsingContext* aBC, nsISHEntry* aOldEntry,
    nsISHEntry* aNewEntry) {
  bool shPref = mozilla::SessionHistoryInParent();
  if (aBC->IsInProcess() || !shPref) {
    nsDocShell* docshell = static_cast<nsDocShell*>(aBC->GetDocShell());
    if (docshell) {
      docshell->SwapHistoryEntries(aOldEntry, aNewEntry);
    }
  } else {
    // FIXME Bug 1633988: Need to update entries?
  }

  // XXX Simplify this once the old and new session history implementations
  // don't run at the same time.
  if (shPref && XRE_IsParentProcess()) {
    aBC->Canonical()->SwapHistoryEntries(aOldEntry, aNewEntry);
  }
}

NS_IMETHODIMP
nsSHistory::AddToRootSessionHistory(bool aCloneChildren, nsISHEntry* aOSHE,
                                    BrowsingContext* aRootBC,
                                    nsISHEntry* aEntry, uint32_t aLoadType,
                                    bool aShouldPersist,
                                    Maybe<int32_t>* aPreviousEntryIndex,
                                    Maybe<int32_t>* aLoadedEntryIndex) {
  MOZ_ASSERT(aRootBC->IsTop());

  nsresult rv = NS_OK;

  // If we need to clone our children onto the new session
  // history entry, do so now.
  if (aCloneChildren && aOSHE) {
    uint32_t cloneID = aOSHE->GetID();
    nsCOMPtr<nsISHEntry> newEntry;
    nsSHistory::CloneAndReplace(aOSHE, aRootBC, cloneID, aEntry, true,
                                getter_AddRefs(newEntry));
    NS_ASSERTION(aEntry == newEntry,
                 "The new session history should be in the new entry");
  }
  // This is the root docshell
  bool addToSHistory = !LOAD_TYPE_HAS_FLAGS(
      aLoadType, nsIWebNavigation::LOAD_FLAGS_REPLACE_HISTORY);
  if (!addToSHistory) {
    // Replace current entry in session history; If the requested index is
    // valid, it indicates the loading was triggered by a history load, and
    // we should replace the entry at requested index instead.
    int32_t index = GetIndexForReplace();

    // Replace the current entry with the new entry
    if (index >= 0) {
      rv = ReplaceEntry(index, aEntry);
    } else {
      // If we're trying to replace an inexistant shistory entry, append.
      addToSHistory = true;
    }
  }
  if (addToSHistory) {
    // Add to session history
    *aPreviousEntryIndex = Some(mIndex);
    rv = AddEntry(aEntry, aShouldPersist);
    *aLoadedEntryIndex = Some(mIndex);
    MOZ_LOG(gPageCacheLog, LogLevel::Verbose,
            ("Previous index: %d, Loaded index: %d",
             aPreviousEntryIndex->value(), aLoadedEntryIndex->value()));
  }
  if (NS_SUCCEEDED(rv)) {
    aEntry->SetDocshellID(aRootBC->GetHistoryID());
  }
  return rv;
}

/* Add an entry to the History list at mIndex and
 * increment the index to point to the new entry
 */
NS_IMETHODIMP
nsSHistory::AddEntry(nsISHEntry* aSHEntry, bool aPersist) {
  NS_ENSURE_ARG(aSHEntry);

  nsCOMPtr<nsISHistory> shistoryOfEntry = aSHEntry->GetShistory();
  if (shistoryOfEntry && shistoryOfEntry != this) {
    NS_WARNING(
        "The entry has been associated to another nsISHistory instance. "
        "Try nsISHEntry.clone() and nsISHEntry.abandonBFCacheEntry() "
        "first if you're copying an entry from another nsISHistory.");
    return NS_ERROR_FAILURE;
  }

  aSHEntry->SetShistory(this);

  // If we have a root docshell, update the docshell id of the root shentry to
  // match the id of that docshell
  RefPtr<BrowsingContext> rootBC = GetBrowsingContext();
  if (rootBC) {
    aSHEntry->SetDocshellID(mRootDocShellID);
  }

  if (mIndex >= 0) {
    MOZ_ASSERT(mIndex < Length(), "Index out of range!");
    if (mIndex >= Length()) {
      return NS_ERROR_FAILURE;
    }

    if (mEntries[mIndex] && !mEntries[mIndex]->GetPersist()) {
      NotifyListeners(mListeners, [](auto l) { l->OnHistoryReplaceEntry(); });
      aSHEntry->SetPersist(aPersist);
      mEntries[mIndex] = aSHEntry;
      return NS_OK;
    }
  }
  SHistoryChangeNotifier change(this);

  int32_t truncating = Length() - 1 - mIndex;
  if (truncating > 0) {
    NotifyListeners(mListeners,
                    [truncating](auto l) { l->OnHistoryTruncate(truncating); });
  }

  nsCOMPtr<nsIURI> uri = aSHEntry->GetURI();
  NotifyListeners(mListeners,
                  [&uri, this](auto l) { l->OnHistoryNewEntry(uri, mIndex); });

  // Remove all entries after the current one, add the new one, and set the
  // new one as the current one.
  MOZ_ASSERT(mIndex >= -1);
  aSHEntry->SetPersist(aPersist);
  mEntries.TruncateLength(mIndex + 1);
  mEntries.AppendElement(aSHEntry);
  mIndex++;
  if (mIndex > 0) {
    UpdateEntryLength(mEntries[mIndex - 1], mEntries[mIndex], false);
  }

  // Purge History list if it is too long
  if (gHistoryMaxSize >= 0 && Length() > gHistoryMaxSize) {
    PurgeHistory(Length() - gHistoryMaxSize);
  }

  return NS_OK;
}

void nsSHistory::NotifyOnHistoryReplaceEntry() {
  NotifyListeners(mListeners, [](auto l) { l->OnHistoryReplaceEntry(); });
}

/* Get size of the history list */
NS_IMETHODIMP
nsSHistory::GetCount(int32_t* aResult) {
  MOZ_ASSERT(aResult, "null out param?");
  *aResult = Length();
  return NS_OK;
}

NS_IMETHODIMP
nsSHistory::GetIndex(int32_t* aResult) {
  MOZ_ASSERT(aResult, "null out param?");
  *aResult = mIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsSHistory::SetIndex(int32_t aIndex) {
  if (aIndex < 0 || aIndex >= Length()) {
    return NS_ERROR_FAILURE;
  }

  mIndex = aIndex;
  return NS_OK;
}

/* Get the requestedIndex */
NS_IMETHODIMP
nsSHistory::GetRequestedIndex(int32_t* aResult) {
  MOZ_ASSERT(aResult, "null out param?");
  *aResult = mRequestedIndex;
  return NS_OK;
}

NS_IMETHODIMP_(void)
nsSHistory::InternalSetRequestedIndex(int32_t aRequestedIndex) {
  MOZ_ASSERT(aRequestedIndex >= -1 && aRequestedIndex < Length());
  mRequestedIndex = aRequestedIndex;
}

NS_IMETHODIMP
nsSHistory::GetEntryAtIndex(int32_t aIndex, nsISHEntry** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);

  if (aIndex < 0 || aIndex >= Length()) {
    return NS_ERROR_FAILURE;
  }

  *aResult = mEntries[aIndex];
  NS_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP_(int32_t)
nsSHistory::GetIndexOfEntry(nsISHEntry* aSHEntry) {
  for (int32_t i = 0; i < Length(); i++) {
    if (aSHEntry == mEntries[i]) {
      return i;
    }
  }

  return -1;
}

static void LogEntry(nsISHEntry* aEntry, int32_t aIndex, int32_t aTotal,
                     const nsCString& aPrefix, bool aIsCurrent) {
  if (!aEntry) {
    MOZ_LOG(gSHLog, LogLevel::Debug,
            (" %s+- %i SH Entry null\n", aPrefix.get(), aIndex));
    return;
  }

  nsCOMPtr<nsIURI> uri = aEntry->GetURI();
  nsAutoString title, name;
  aEntry->GetTitle(title);
  aEntry->GetName(name);

  SHEntrySharedParentState* shared;
  if (mozilla::SessionHistoryInParent()) {
    shared = static_cast<SessionHistoryEntry*>(aEntry)->SharedInfo();
  } else {
    shared = static_cast<nsSHEntry*>(aEntry)->GetState();
  }

  nsID docShellId;
  aEntry->GetDocshellID(docShellId);

  int32_t childCount = aEntry->GetChildCount();

  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("%s%s+- %i SH Entry %p %" PRIu64 " %s\n", aIsCurrent ? ">" : " ",
           aPrefix.get(), aIndex, aEntry, shared->GetId(),
           nsIDToCString(docShellId).get()));

  nsCString prefix(aPrefix);
  if (aIndex < aTotal - 1) {
    prefix.AppendLiteral("|   ");
  } else {
    prefix.AppendLiteral("    ");
  }

  MOZ_LOG(gSHLog, LogLevel::Debug,
          (" %s%s  URL = %s\n", prefix.get(), childCount > 0 ? "|" : " ",
           uri->GetSpecOrDefault().get()));
  MOZ_LOG(gSHLog, LogLevel::Debug,
          (" %s%s  Title = %s\n", prefix.get(), childCount > 0 ? "|" : " ",
           NS_LossyConvertUTF16toASCII(title).get()));
  MOZ_LOG(gSHLog, LogLevel::Debug,
          (" %s%s  Name = %s\n", prefix.get(), childCount > 0 ? "|" : " ",
           NS_LossyConvertUTF16toASCII(name).get()));
  MOZ_LOG(
      gSHLog, LogLevel::Debug,
      (" %s%s  Is in BFCache = %s\n", prefix.get(), childCount > 0 ? "|" : " ",
       aEntry->GetIsInBFCache() ? "true" : "false"));
  MOZ_LOG(gSHLog, LogLevel::Debug,
          (" %s%s  Has User Interaction = %s\n", prefix.get(),
           childCount > 0 ? "|" : " ",
           aEntry->GetHasUserInteraction() ? "true" : "false"));

  nsCOMPtr<nsISHEntry> prevChild;
  for (int32_t i = 0; i < childCount; ++i) {
    nsCOMPtr<nsISHEntry> child;
    aEntry->GetChildAt(i, getter_AddRefs(child));
    LogEntry(child, i, childCount, prefix, false);
    child.swap(prevChild);
  }
}

void nsSHistory::LogHistory() {
  if (!MOZ_LOG_TEST(gSHLog, LogLevel::Debug)) {
    return;
  }

  MOZ_LOG(gSHLog, LogLevel::Debug, ("nsSHistory %p\n", this));
  int32_t length = Length();
  for (int32_t i = 0; i < length; i++) {
    LogEntry(mEntries[i], i, length, EmptyCString(), i == mIndex);
  }
}

void nsSHistory::WindowIndices(int32_t aIndex, int32_t* aOutStartIndex,
                               int32_t* aOutEndIndex) {
  *aOutStartIndex = std::max(0, aIndex - nsSHistory::VIEWER_WINDOW);
  *aOutEndIndex = std::min(Length() - 1, aIndex + nsSHistory::VIEWER_WINDOW);
}

static void MarkAsInitialEntry(
    SessionHistoryEntry* aEntry,
    nsTHashMap<nsIDHashKey, SessionHistoryEntry*>& aHashtable) {
  if (!aEntry->BCHistoryLength().Modified()) {
    ++(aEntry->BCHistoryLength());
  }
  aHashtable.InsertOrUpdate(aEntry->DocshellID(), aEntry);
  for (const RefPtr<SessionHistoryEntry>& entry : aEntry->Children()) {
    if (entry) {
      MarkAsInitialEntry(entry, aHashtable);
    }
  }
}

static void ClearEntries(SessionHistoryEntry* aEntry) {
  aEntry->ClearBCHistoryLength();
  for (const RefPtr<SessionHistoryEntry>& entry : aEntry->Children()) {
    if (entry) {
      ClearEntries(entry);
    }
  }
}

NS_IMETHODIMP
nsSHistory::PurgeHistory(int32_t aNumEntries) {
  if (Length() <= 0 || aNumEntries <= 0) {
    return NS_ERROR_FAILURE;
  }

  SHistoryChangeNotifier change(this);

  aNumEntries = std::min(aNumEntries, Length());

  NotifyListeners(mListeners,
                  [aNumEntries](auto l) { l->OnHistoryPurge(aNumEntries); });

  // Set all the entries hanging of the first entry that we keep
  // (mEntries[aNumEntries]) as being created as the result of a load
  // (so contributing one to their BCHistoryLength).
  nsTHashMap<nsIDHashKey, SessionHistoryEntry*> docshellIDToEntry;
  if (aNumEntries != Length()) {
    nsCOMPtr<SessionHistoryEntry> she =
        do_QueryInterface(mEntries[aNumEntries]);
    if (she) {
      MarkAsInitialEntry(she, docshellIDToEntry);
    }
  }

  // Reset the BCHistoryLength of all the entries that we're removing to a new
  // counter with value 0 while decreasing their contribution to a shared
  // BCHistoryLength. The end result is that they don't contribute to the
  // BCHistoryLength of any other entry anymore.
  for (int32_t i = 0; i < aNumEntries; ++i) {
    nsCOMPtr<SessionHistoryEntry> she = do_QueryInterface(mEntries[i]);
    if (she) {
      ClearEntries(she);
    }
  }

  RefPtr<BrowsingContext> rootBC = GetBrowsingContext();
  if (rootBC) {
    rootBC->PreOrderWalk([&docshellIDToEntry](BrowsingContext* aBC) {
      SessionHistoryEntry* entry = docshellIDToEntry.Get(aBC->GetHistoryID());
      Unused << aBC->SetHistoryEntryCount(
          entry ? uint32_t(entry->BCHistoryLength()) : 0);
    });
  }

  // Remove the first `aNumEntries` entries.
  mEntries.RemoveElementsAt(0, aNumEntries);

  // Adjust the indices, but don't let them go below -1.
  mIndex -= aNumEntries;
  mIndex = std::max(mIndex, -1);
  mRequestedIndex -= aNumEntries;
  mRequestedIndex = std::max(mRequestedIndex, -1);

  if (rootBC && rootBC->GetDocShell()) {
    rootBC->GetDocShell()->HistoryPurged(aNumEntries);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSHistory::AddSHistoryListener(nsISHistoryListener* aListener) {
  NS_ENSURE_ARG_POINTER(aListener);

  // Check if the listener supports Weak Reference. This is a must.
  // This listener functionality is used by embedders and we want to
  // have the right ownership with who ever listens to SHistory
  nsWeakPtr listener = do_GetWeakReference(aListener);
  if (!listener) {
    return NS_ERROR_FAILURE;
  }

  mListeners.AppendElementUnlessExists(listener);
  return NS_OK;
}

void nsSHistory::NotifyListenersDocumentViewerEvicted(uint32_t aNumEvicted) {
  NotifyListeners(mListeners, [aNumEvicted](auto l) {
    l->OnDocumentViewerEvicted(aNumEvicted);
  });
}

NS_IMETHODIMP
nsSHistory::RemoveSHistoryListener(nsISHistoryListener* aListener) {
  // Make sure the listener that wants to be removed is the
  // one we have in store.
  nsWeakPtr listener = do_GetWeakReference(aListener);
  mListeners.RemoveElement(listener);
  return NS_OK;
}

/* Replace an entry in the History list at a particular index.
 * Do not update index or count.
 */
NS_IMETHODIMP
nsSHistory::ReplaceEntry(int32_t aIndex, nsISHEntry* aReplaceEntry) {
  NS_ENSURE_ARG(aReplaceEntry);

  if (aIndex < 0 || aIndex >= Length()) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsISHistory> shistoryOfEntry = aReplaceEntry->GetShistory();
  if (shistoryOfEntry && shistoryOfEntry != this) {
    NS_WARNING(
        "The entry has been associated to another nsISHistory instance. "
        "Try nsISHEntry.clone() and nsISHEntry.abandonBFCacheEntry() "
        "first if you're copying an entry from another nsISHistory.");
    return NS_ERROR_FAILURE;
  }

  aReplaceEntry->SetShistory(this);

  NotifyListeners(mListeners, [](auto l) { l->OnHistoryReplaceEntry(); });

  aReplaceEntry->SetPersist(true);
  mEntries[aIndex] = aReplaceEntry;

  return NS_OK;
}

// Calls OnHistoryReload on all registered session history listeners.
// Listeners may return 'false' to cancel an action so make sure that we
// set the return value to 'false' if one of the listeners wants to cancel.
NS_IMETHODIMP
nsSHistory::NotifyOnHistoryReload(bool* aCanReload) {
  *aCanReload = true;

  for (const nsWeakPtr& weakPtr : mListeners.EndLimitedRange()) {
    nsCOMPtr<nsISHistoryListener> listener = do_QueryReferent(weakPtr);
    if (listener) {
      bool retval = true;

      if (NS_SUCCEEDED(listener->OnHistoryReload(&retval)) && !retval) {
        *aCanReload = false;
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSHistory::EvictOutOfRangeDocumentViewers(int32_t aIndex) {
  MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
          ("nsSHistory::EvictOutOfRangeDocumentViewers %i", aIndex));

  // Check our per SHistory object limit in the currently navigated SHistory
  EvictOutOfRangeWindowDocumentViewers(aIndex);
  // Check our total limit across all SHistory objects
  GloballyEvictDocumentViewers();
  return NS_OK;
}

NS_IMETHODIMP_(void)
nsSHistory::EvictDocumentViewersOrReplaceEntry(nsISHEntry* aNewSHEntry,
                                               bool aReplace) {
  if (!aReplace) {
    int32_t curIndex;
    GetIndex(&curIndex);
    if (curIndex > -1) {
      EvictOutOfRangeDocumentViewers(curIndex);
    }
  } else {
    nsCOMPtr<nsISHEntry> rootSHEntry = nsSHistory::GetRootSHEntry(aNewSHEntry);

    int32_t index = GetIndexOfEntry(rootSHEntry);
    if (index > -1) {
      ReplaceEntry(index, rootSHEntry);
    }
  }
}

NS_IMETHODIMP
nsSHistory::EvictAllDocumentViewers() {
  // XXXbz we don't actually do a good job of evicting things as we should, so
  // we might have viewers quite far from mIndex.  So just evict everything.
  for (int32_t i = 0; i < Length(); i++) {
    EvictDocumentViewerForEntry(mEntries[i]);
  }

  return NS_OK;
}

MOZ_CAN_RUN_SCRIPT
static void FinishRestore(CanonicalBrowsingContext* aBrowsingContext,
                          nsDocShellLoadState* aLoadState,
                          SessionHistoryEntry* aEntry,
                          nsFrameLoader* aFrameLoader, bool aCanSave) {
  MOZ_ASSERT(aEntry);
  MOZ_ASSERT(aFrameLoader);

  aEntry->SetFrameLoader(nullptr);

  nsCOMPtr<nsISHistory> shistory = aEntry->GetShistory();
  int32_t indexOfHistoryLoad =
      shistory ? shistory->GetIndexOfEntry(aEntry) : -1;

  nsCOMPtr<nsFrameLoaderOwner> frameLoaderOwner =
      do_QueryInterface(aBrowsingContext->GetEmbedderElement());
  if (frameLoaderOwner && aFrameLoader->GetMaybePendingBrowsingContext() &&
      indexOfHistoryLoad >= 0) {
    RefPtr<BrowsingContextWebProgress> webProgress =
        aBrowsingContext->GetWebProgress();
    if (webProgress) {
      // Synthesize a STATE_START WebProgress state change event from here
      // in order to ensure emitting it on the BrowsingContext we navigate
      // *from* instead of the BrowsingContext we navigate *to*. This will fire
      // before and the next one will be ignored by BrowsingContextWebProgress:
      // https://searchfox.org/mozilla-central/rev/77f0b36028b2368e342c982ea47609040b399d89/docshell/base/BrowsingContextWebProgress.cpp#196-203
      nsCOMPtr<nsIURI> nextURI = aEntry->GetURI();
      nsCOMPtr<nsIURI> nextOriginalURI = aEntry->GetOriginalURI();
      nsCOMPtr<nsIRequest> request = MakeAndAddRef<RemoteWebProgressRequest>(
          nextURI, nextOriginalURI ? nextOriginalURI : nextURI,
          ""_ns /* aMatchedList */);
      webProgress->OnStateChange(webProgress, request,
                                 nsIWebProgressListener::STATE_START |
                                     nsIWebProgressListener::STATE_IS_DOCUMENT |
                                     nsIWebProgressListener::STATE_IS_REQUEST |
                                     nsIWebProgressListener::STATE_IS_WINDOW |
                                     nsIWebProgressListener::STATE_IS_NETWORK,
                                 NS_OK);
    }

    RefPtr<CanonicalBrowsingContext> loadingBC =
        aFrameLoader->GetMaybePendingBrowsingContext()->Canonical();
    RefPtr<nsFrameLoader> currentFrameLoader =
        frameLoaderOwner->GetFrameLoader();
    // The current page can be bfcached, store the
    // nsFrameLoader in the current SessionHistoryEntry.
    RefPtr<SessionHistoryEntry> currentSHEntry =
        aBrowsingContext->GetActiveSessionHistoryEntry();
    if (currentSHEntry) {
      // Update layout history state now, before we change the IsInBFCache flag
      // and the active session history entry.
      aBrowsingContext->SynchronizeLayoutHistoryState();

      if (aCanSave) {
        currentSHEntry->SetFrameLoader(currentFrameLoader);
        Unused << aBrowsingContext->SetIsInBFCache(true);
      }
    }

    // Ensure browser priority to matches `IsPriorityActive` after restoring.
    if (BrowserParent* bp = loadingBC->GetBrowserParent()) {
      bp->VisitAll([&](BrowserParent* aBp) {
        ProcessPriorityManager::BrowserPriorityChanged(
            aBp, aBrowsingContext->IsPriorityActive());
      });
    }

    if (aEntry) {
      aEntry->SetWireframe(Nothing());
    }

    // ReplacedBy will swap the entry back.
    aBrowsingContext->SetActiveSessionHistoryEntry(aEntry);
    loadingBC->SetActiveSessionHistoryEntry(nullptr);
    NavigationIsolationOptions options;
    aBrowsingContext->ReplacedBy(loadingBC, options);

    // Assuming we still have the session history, update the index.
    if (loadingBC->GetSessionHistory()) {
      shistory->InternalSetRequestedIndex(indexOfHistoryLoad);
      shistory->UpdateIndex();
    }
    loadingBC->HistoryCommitIndexAndLength();

    // ResetSHEntryHasUserInteractionCache(); ?
    // browser.navigation.requireUserInteraction is still
    // disabled everywhere.

    frameLoaderOwner->RestoreFrameLoaderFromBFCache(aFrameLoader);
    // EvictOutOfRangeDocumentViewers is called here explicitly to
    // possibly evict the now in the bfcache document.
    // HistoryCommitIndexAndLength might not have evicted that before the
    // FrameLoader swap.
    shistory->EvictOutOfRangeDocumentViewers(indexOfHistoryLoad);

    // The old page can't be stored in the bfcache,
    // destroy the nsFrameLoader.
    if (!aCanSave && currentFrameLoader) {
      currentFrameLoader->Destroy();
    }

    Unused << loadingBC->SetIsInBFCache(false);

    // We need to call this after we've restored the page from BFCache (see
    // SetIsInBFCache(false) above), so that the page is not frozen anymore and
    // the right focus events are fired.
    frameLoaderOwner->UpdateFocusAndMouseEnterStateAfterFrameLoaderChange();

    return;
  }

  aFrameLoader->Destroy();

  // Fall back to do a normal load.
  aBrowsingContext->LoadURI(aLoadState, false);
}

/* static */
void nsSHistory::LoadURIOrBFCache(LoadEntryResult& aLoadEntry) {
  if (mozilla::BFCacheInParent() && aLoadEntry.mBrowsingContext->IsTop()) {
    MOZ_ASSERT(XRE_IsParentProcess());
    RefPtr<nsDocShellLoadState> loadState = aLoadEntry.mLoadState;
    RefPtr<CanonicalBrowsingContext> canonicalBC =
        aLoadEntry.mBrowsingContext->Canonical();
    nsCOMPtr<SessionHistoryEntry> she = do_QueryInterface(loadState->SHEntry());
    nsCOMPtr<SessionHistoryEntry> currentShe =
        canonicalBC->GetActiveSessionHistoryEntry();
    MOZ_ASSERT(she);
    RefPtr<nsFrameLoader> frameLoader = she->GetFrameLoader();
    if (frameLoader && canonicalBC->Group()->Toplevels().Length() == 1 &&
        (!currentShe || (she->SharedInfo() != currentShe->SharedInfo() &&
                         !currentShe->GetFrameLoader()))) {
      bool canSave = (!currentShe || currentShe->GetSaveLayoutStateFlag()) &&
                     canonicalBC->AllowedInBFCache(Nothing(), nullptr);

      MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
              ("nsSHistory::LoadURIOrBFCache "
               "saving presentation=%i",
               canSave));

      nsCOMPtr<nsFrameLoaderOwner> frameLoaderOwner =
          do_QueryInterface(canonicalBC->GetEmbedderElement());
      if (frameLoaderOwner) {
        RefPtr<nsFrameLoader> currentFrameLoader =
            frameLoaderOwner->GetFrameLoader();
        if (currentFrameLoader &&
            currentFrameLoader->GetMaybePendingBrowsingContext()) {
          if (WindowGlobalParent* wgp =
                  currentFrameLoader->GetMaybePendingBrowsingContext()
                      ->Canonical()
                      ->GetCurrentWindowGlobal()) {
            wgp->PermitUnload(
                [canonicalBC, loadState, she, frameLoader, currentFrameLoader,
                 canSave](bool aAllow) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                  if (aAllow && !canonicalBC->IsReplaced()) {
                    FinishRestore(canonicalBC, loadState, she, frameLoader,
                                  canSave && canonicalBC->AllowedInBFCache(
                                                 Nothing(), nullptr));
                  } else if (currentFrameLoader
                                 ->GetMaybePendingBrowsingContext()) {
                    nsISHistory* shistory =
                        currentFrameLoader->GetMaybePendingBrowsingContext()
                            ->Canonical()
                            ->GetSessionHistory();
                    if (shistory) {
                      shistory->InternalSetRequestedIndex(-1);
                    }
                  }
                });
            return;
          }
        }
      }

      FinishRestore(canonicalBC, loadState, she, frameLoader, canSave);
      return;
    }
    if (frameLoader) {
      she->SetFrameLoader(nullptr);
      frameLoader->Destroy();
    }
  }

  RefPtr<BrowsingContext> bc = aLoadEntry.mBrowsingContext;
  RefPtr<nsDocShellLoadState> loadState = aLoadEntry.mLoadState;
  bc->LoadURI(loadState, false);
}

/* static */
void nsSHistory::LoadURIs(nsTArray<LoadEntryResult>& aLoadResults) {
  for (LoadEntryResult& loadEntry : aLoadResults) {
    LoadURIOrBFCache(loadEntry);
  }
}

NS_IMETHODIMP
nsSHistory::Reload(uint32_t aReloadFlags) {
  nsTArray<LoadEntryResult> loadResults;
  nsresult rv = Reload(aReloadFlags, loadResults);
  NS_ENSURE_SUCCESS(rv, rv);

  if (loadResults.IsEmpty()) {
    return NS_OK;
  }

  LoadURIs(loadResults);
  return NS_OK;
}

nsresult nsSHistory::Reload(uint32_t aReloadFlags,
                            nsTArray<LoadEntryResult>& aLoadResults) {
  MOZ_ASSERT(aLoadResults.IsEmpty());

  uint32_t loadType;
  if (aReloadFlags & nsIWebNavigation::LOAD_FLAGS_BYPASS_PROXY &&
      aReloadFlags & nsIWebNavigation::LOAD_FLAGS_BYPASS_CACHE) {
    loadType = LOAD_RELOAD_BYPASS_PROXY_AND_CACHE;
  } else if (aReloadFlags & nsIWebNavigation::LOAD_FLAGS_BYPASS_PROXY) {
    loadType = LOAD_RELOAD_BYPASS_PROXY;
  } else if (aReloadFlags & nsIWebNavigation::LOAD_FLAGS_BYPASS_CACHE) {
    loadType = LOAD_RELOAD_BYPASS_CACHE;
  } else if (aReloadFlags & nsIWebNavigation::LOAD_FLAGS_CHARSET_CHANGE) {
    loadType = LOAD_RELOAD_CHARSET_CHANGE;
  } else {
    loadType = LOAD_RELOAD_NORMAL;
  }

  // We are reloading. Send Reload notifications.
  // nsDocShellLoadFlagType is not public, where as nsIWebNavigation
  // is public. So send the reload notifications with the
  // nsIWebNavigation flags.
  bool canNavigate = true;
  MOZ_ALWAYS_SUCCEEDS(NotifyOnHistoryReload(&canNavigate));
  if (!canNavigate) {
    return NS_OK;
  }

  nsresult rv = LoadEntry(
      mIndex, loadType, HIST_CMD_RELOAD, aLoadResults, /* aSameEpoch */ false,
      /* aLoadCurrentEntry */ true,
      aReloadFlags & nsIWebNavigation::LOAD_FLAGS_USER_ACTIVATION);
  if (NS_FAILED(rv)) {
    aLoadResults.Clear();
    return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSHistory::ReloadCurrentEntry() {
  nsTArray<LoadEntryResult> loadResults;
  nsresult rv = ReloadCurrentEntry(loadResults);
  NS_ENSURE_SUCCESS(rv, rv);

  LoadURIs(loadResults);
  return NS_OK;
}

nsresult nsSHistory::ReloadCurrentEntry(
    nsTArray<LoadEntryResult>& aLoadResults) {
  // Notify listeners
  NotifyListeners(mListeners, [](auto l) { l->OnHistoryGotoIndex(); });

  return LoadEntry(mIndex, LOAD_HISTORY, HIST_CMD_RELOAD, aLoadResults,
                   /* aSameEpoch */ false, /* aLoadCurrentEntry */ true,
                   /* aUserActivation */ false);
}

void nsSHistory::EvictOutOfRangeWindowDocumentViewers(int32_t aIndex) {
  // XXX rename method to EvictDocumentViewersExceptAroundIndex, or something.

  // We need to release all content viewers that are no longer in the range
  //
  //  aIndex - VIEWER_WINDOW to aIndex + VIEWER_WINDOW
  //
  // to ensure that this SHistory object isn't responsible for more than
  // VIEWER_WINDOW content viewers.  But our job is complicated by the
  // fact that two entries which are related by either hash navigations or
  // history.pushState will have the same content viewer.
  //
  // To illustrate the issue, suppose VIEWER_WINDOW = 3 and we have four
  // linked entries in our history.  Suppose we then add a new content
  // viewer and call into this function.  So the history looks like:
  //
  //   A A A A B
  //     +     *
  //
  // where the letters are content viewers and + and * denote the beginning and
  // end of the range aIndex +/- VIEWER_WINDOW.
  //
  // Although one copy of the content viewer A exists outside the range, we
  // don't want to evict A, because it has other copies in range!
  //
  // We therefore adjust our eviction strategy to read:
  //
  //   Evict each content viewer outside the range aIndex -/+
  //   VIEWER_WINDOW, unless that content viewer also appears within the
  //   range.
  //
  // (Note that it's entirely legal to have two copies of one content viewer
  // separated by a different content viewer -- call pushState twice, go back
  // once, and refresh -- so we can't rely on identical viewers only appearing
  // adjacent to one another.)

  if (aIndex < 0) {
    return;
  }
  NS_ENSURE_TRUE_VOID(aIndex < Length());

  // Calculate the range that's safe from eviction.
  int32_t startSafeIndex, endSafeIndex;
  WindowIndices(aIndex, &startSafeIndex, &endSafeIndex);

  LOG(
      ("EvictOutOfRangeWindowDocumentViewers(index=%d), "
       "Length()=%d. Safe range [%d, %d]",
       aIndex, Length(), startSafeIndex, endSafeIndex));

  // The content viewers in range aIndex -/+ VIEWER_WINDOW will not be
  // evicted.  Collect a set of them so we don't accidentally evict one of them
  // if it appears outside this range.
  nsCOMArray<nsIDocumentViewer> safeViewers;
  nsTArray<RefPtr<nsFrameLoader>> safeFrameLoaders;
  for (int32_t i = startSafeIndex; i <= endSafeIndex; i++) {
    nsCOMPtr<nsIDocumentViewer> viewer = mEntries[i]->GetDocumentViewer();
    if (viewer) {
      safeViewers.AppendObject(viewer);
    } else if (nsCOMPtr<SessionHistoryEntry> she =
                   do_QueryInterface(mEntries[i])) {
      nsFrameLoader* frameLoader = she->GetFrameLoader();
      if (frameLoader) {
        safeFrameLoaders.AppendElement(frameLoader);
      }
    }
  }

  // Walk the SHistory list and evict any content viewers that aren't safe.
  // (It's important that the condition checks Length(), rather than a cached
  // copy of Length(), because the length might change between iterations.)
  for (int32_t i = 0; i < Length(); i++) {
    nsCOMPtr<nsISHEntry> entry = mEntries[i];
    nsCOMPtr<nsIDocumentViewer> viewer = entry->GetDocumentViewer();
    if (viewer) {
      if (safeViewers.IndexOf(viewer) == -1) {
        EvictDocumentViewerForEntry(entry);
      }
    } else if (nsCOMPtr<SessionHistoryEntry> she =
                   do_QueryInterface(mEntries[i])) {
      nsFrameLoader* frameLoader = she->GetFrameLoader();
      if (frameLoader) {
        if (!safeFrameLoaders.Contains(frameLoader)) {
          EvictDocumentViewerForEntry(entry);
        }
      }
    }
  }
}

namespace {

class EntryAndDistance {
 public:
  EntryAndDistance(nsSHistory* aSHistory, nsISHEntry* aEntry, uint32_t aDist)
      : mSHistory(aSHistory),
        mEntry(aEntry),
        mViewer(aEntry->GetDocumentViewer()),
        mLastTouched(mEntry->GetLastTouched()),
        mDistance(aDist) {
    nsCOMPtr<SessionHistoryEntry> she = do_QueryInterface(aEntry);
    if (she) {
      mFrameLoader = she->GetFrameLoader();
    }
    NS_ASSERTION(mViewer || mFrameLoader,
                 "Entry should have a content viewer or frame loader.");
  }

  bool operator<(const EntryAndDistance& aOther) const {
    // Compare distances first, and fall back to last-accessed times.
    if (aOther.mDistance != this->mDistance) {
      return this->mDistance < aOther.mDistance;
    }

    return this->mLastTouched < aOther.mLastTouched;
  }

  bool operator==(const EntryAndDistance& aOther) const {
    // This is a little silly; we need == so the default comaprator can be
    // instantiated, but this function is never actually called when we sort
    // the list of EntryAndDistance objects.
    return aOther.mDistance == this->mDistance &&
           aOther.mLastTouched == this->mLastTouched;
  }

  RefPtr<nsSHistory> mSHistory;
  nsCOMPtr<nsISHEntry> mEntry;
  nsCOMPtr<nsIDocumentViewer> mViewer;
  RefPtr<nsFrameLoader> mFrameLoader;
  uint32_t mLastTouched;
  int32_t mDistance;
};

}  // namespace

// static
void nsSHistory::GloballyEvictDocumentViewers() {
  // First, collect from each SHistory object the entries which have a cached
  // content viewer. Associate with each entry its distance from its SHistory's
  // current index.

  nsTArray<EntryAndDistance> entries;

  for (auto shist : gSHistoryList.mList) {
    // Maintain a list of the entries which have viewers and belong to
    // this particular shist object.  We'll add this list to the global list,
    // |entries|, eventually.
    nsTArray<EntryAndDistance> shEntries;

    // Content viewers are likely to exist only within shist->mIndex -/+
    // VIEWER_WINDOW, so only search within that range.
    //
    // A content viewer might exist outside that range due to either:
    //
    //   * history.pushState or hash navigations, in which case a copy of the
    //     content viewer should exist within the range, or
    //
    //   * bugs which cause us not to call nsSHistory::EvictDocumentViewers()
    //     often enough.  Once we do call EvictDocumentViewers() for the
    //     SHistory object in question, we'll do a full search of its history
    //     and evict the out-of-range content viewers, so we don't bother here.
    //
    int32_t startIndex, endIndex;
    shist->WindowIndices(shist->mIndex, &startIndex, &endIndex);
    for (int32_t i = startIndex; i <= endIndex; i++) {
      nsCOMPtr<nsISHEntry> entry = shist->mEntries[i];
      nsCOMPtr<nsIDocumentViewer> viewer = entry->GetDocumentViewer();

      bool found = false;
      bool hasDocumentViewerOrFrameLoader = false;
      if (viewer) {
        hasDocumentViewerOrFrameLoader = true;
        // Because one content viewer might belong to multiple SHEntries, we
        // have to search through shEntries to see if we already know
        // about this content viewer.  If we find the viewer, update its
        // distance from the SHistory's index and continue.
        for (uint32_t j = 0; j < shEntries.Length(); j++) {
          EntryAndDistance& container = shEntries[j];
          if (container.mViewer == viewer) {
            container.mDistance =
                std::min(container.mDistance, DeprecatedAbs(i - shist->mIndex));
            found = true;
            break;
          }
        }
      } else if (nsCOMPtr<SessionHistoryEntry> she = do_QueryInterface(entry)) {
        if (RefPtr<nsFrameLoader> frameLoader = she->GetFrameLoader()) {
          hasDocumentViewerOrFrameLoader = true;
          // Similar search as above but using frameloader.
          for (uint32_t j = 0; j < shEntries.Length(); j++) {
            EntryAndDistance& container = shEntries[j];
            if (container.mFrameLoader == frameLoader) {
              container.mDistance = std::min(container.mDistance,
                                             DeprecatedAbs(i - shist->mIndex));
              found = true;
              break;
            }
          }
        }
      }

      // If we didn't find a EntryAndDistance for this content viewer /
      // frameloader, make a new one.
      if (hasDocumentViewerOrFrameLoader && !found) {
        EntryAndDistance container(shist, entry,
                                   DeprecatedAbs(i - shist->mIndex));
        shEntries.AppendElement(container);
      }
    }

    // We've found all the entries belonging to shist which have viewers.
    // Add those entries to our global list and move on.
    entries.AppendElements(shEntries);
  }

  // We now have collected all cached content viewers.  First check that we
  // have enough that we actually need to evict some.
  if ((int32_t)entries.Length() <= sHistoryMaxTotalViewers) {
    return;
  }

  // If we need to evict, sort our list of entries and evict the largest
  // ones.  (We could of course get better algorithmic complexity here by using
  // a heap or something more clever.  But sHistoryMaxTotalViewers isn't large,
  // so let's not worry about it.)
  entries.Sort();

  for (int32_t i = entries.Length() - 1; i >= sHistoryMaxTotalViewers; --i) {
    (entries[i].mSHistory)->EvictDocumentViewerForEntry(entries[i].mEntry);
  }
}

nsresult nsSHistory::FindEntryForBFCache(SHEntrySharedParentState* aEntry,
                                         nsISHEntry** aResult,
                                         int32_t* aResultIndex) {
  *aResult = nullptr;
  *aResultIndex = -1;

  int32_t startIndex, endIndex;
  WindowIndices(mIndex, &startIndex, &endIndex);

  for (int32_t i = startIndex; i <= endIndex; ++i) {
    nsCOMPtr<nsISHEntry> shEntry = mEntries[i];

    // Does shEntry have the same BFCacheEntry as the argument to this method?
    if (shEntry->HasBFCacheEntry(aEntry)) {
      shEntry.forget(aResult);
      *aResultIndex = i;
      return NS_OK;
    }
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP_(void)
nsSHistory::EvictExpiredDocumentViewerForEntry(
    SHEntrySharedParentState* aEntry) {
  int32_t index;
  nsCOMPtr<nsISHEntry> shEntry;
  FindEntryForBFCache(aEntry, getter_AddRefs(shEntry), &index);

  if (index == mIndex) {
    NS_WARNING("How did the current SHEntry expire?");
  }

  if (shEntry) {
    EvictDocumentViewerForEntry(shEntry);
  }
}

NS_IMETHODIMP_(void)
nsSHistory::AddToExpirationTracker(SHEntrySharedParentState* aEntry) {
  RefPtr<SHEntrySharedParentState> entry = aEntry;
  if (!mHistoryTracker || !entry) {
    return;
  }

  mHistoryTracker->AddObject(entry);
  return;
}

NS_IMETHODIMP_(void)
nsSHistory::RemoveFromExpirationTracker(SHEntrySharedParentState* aEntry) {
  RefPtr<SHEntrySharedParentState> entry = aEntry;
  MOZ_ASSERT(mHistoryTracker && !mHistoryTracker->IsEmpty());
  if (!mHistoryTracker || !entry) {
    return;
  }

  mHistoryTracker->RemoveObject(entry);
}

// Evicts all content viewers in all history objects.  This is very
// inefficient, because it requires a linear search through all SHistory
// objects for each viewer to be evicted.  However, this method is called
// infrequently -- only when the disk or memory cache is cleared.

// static
void nsSHistory::GloballyEvictAllDocumentViewers() {
  int32_t maxViewers = sHistoryMaxTotalViewers;
  sHistoryMaxTotalViewers = 0;
  GloballyEvictDocumentViewers();
  sHistoryMaxTotalViewers = maxViewers;
}

void GetDynamicChildren(nsISHEntry* aEntry, nsTArray<nsID>& aDocshellIDs) {
  int32_t count = aEntry->GetChildCount();
  for (int32_t i = 0; i < count; ++i) {
    nsCOMPtr<nsISHEntry> child;
    aEntry->GetChildAt(i, getter_AddRefs(child));
    if (child) {
      if (child->IsDynamicallyAdded()) {
        child->GetDocshellID(*aDocshellIDs.AppendElement());
      } else {
        GetDynamicChildren(child, aDocshellIDs);
      }
    }
  }
}

bool RemoveFromSessionHistoryEntry(nsISHEntry* aRoot,
                                   nsTArray<nsID>& aDocshellIDs) {
  bool didRemove = false;
  int32_t childCount = aRoot->GetChildCount();
  for (int32_t i = childCount - 1; i >= 0; --i) {
    nsCOMPtr<nsISHEntry> child;
    aRoot->GetChildAt(i, getter_AddRefs(child));
    if (child) {
      nsID docshelldID;
      child->GetDocshellID(docshelldID);
      if (aDocshellIDs.Contains(docshelldID)) {
        didRemove = true;
        aRoot->RemoveChild(child);
      } else if (RemoveFromSessionHistoryEntry(child, aDocshellIDs)) {
        didRemove = true;
      }
    }
  }
  return didRemove;
}

bool RemoveChildEntries(nsISHistory* aHistory, int32_t aIndex,
                        nsTArray<nsID>& aEntryIDs) {
  nsCOMPtr<nsISHEntry> root;
  aHistory->GetEntryAtIndex(aIndex, getter_AddRefs(root));
  return root ? RemoveFromSessionHistoryEntry(root, aEntryIDs) : false;
}

bool IsSameTree(nsISHEntry* aEntry1, nsISHEntry* aEntry2) {
  if (!aEntry1 && !aEntry2) {
    return true;
  }
  if ((!aEntry1 && aEntry2) || (aEntry1 && !aEntry2)) {
    return false;
  }
  uint32_t id1 = aEntry1->GetID();
  uint32_t id2 = aEntry2->GetID();
  if (id1 != id2) {
    return false;
  }

  int32_t count1 = aEntry1->GetChildCount();
  int32_t count2 = aEntry2->GetChildCount();
  // We allow null entries in the end of the child list.
  int32_t count = std::max(count1, count2);
  for (int32_t i = 0; i < count; ++i) {
    nsCOMPtr<nsISHEntry> child1, child2;
    aEntry1->GetChildAt(i, getter_AddRefs(child1));
    aEntry2->GetChildAt(i, getter_AddRefs(child2));
    if (!IsSameTree(child1, child2)) {
      return false;
    }
  }

  return true;
}

bool nsSHistory::RemoveDuplicate(int32_t aIndex, bool aKeepNext) {
  NS_ASSERTION(aIndex >= 0, "aIndex must be >= 0!");
  NS_ASSERTION(aIndex != 0 || aKeepNext,
               "If we're removing index 0 we must be keeping the next");
  NS_ASSERTION(aIndex != mIndex, "Shouldn't remove mIndex!");

  int32_t compareIndex = aKeepNext ? aIndex + 1 : aIndex - 1;

  nsresult rv;
  nsCOMPtr<nsISHEntry> root1, root2;
  rv = GetEntryAtIndex(aIndex, getter_AddRefs(root1));
  if (NS_FAILED(rv)) {
    return false;
  }
  rv = GetEntryAtIndex(compareIndex, getter_AddRefs(root2));
  if (NS_FAILED(rv)) {
    return false;
  }

  SHistoryChangeNotifier change(this);

  if (IsSameTree(root1, root2)) {
    if (aIndex < compareIndex) {
      // If we're removing the entry with the lower index we need to move its
      // BCHistoryLength to the entry we're keeping. If we're removing the entry
      // with the higher index then it shouldn't have a modified
      // BCHistoryLength.
      UpdateEntryLength(root1, root2, true);
    }
    nsCOMPtr<SessionHistoryEntry> she = do_QueryInterface(root1);
    if (she) {
      ClearEntries(she);
    }
    mEntries.RemoveElementAt(aIndex);

    // FIXME Bug 1546350: Reimplement history listeners.
    // if (mRootBC && mRootBC->GetDocShell()) {
    //  static_cast<nsDocShell*>(mRootBC->GetDocShell())
    //      ->HistoryEntryRemoved(aIndex);
    //}

    // Adjust our indices to reflect the removed entry.
    if (mIndex > aIndex) {
      mIndex = mIndex - 1;
    }

    // NB: If the entry we are removing is the entry currently
    // being navigated to (mRequestedIndex) then we adjust the index
    // only if we're not keeping the next entry (because if we are keeping
    // the next entry (because the current is a duplicate of the next), then
    // that entry slides into the spot that we're currently pointing to.
    // We don't do this adjustment for mIndex because mIndex cannot equal
    // aIndex.

    // NB: We don't need to guard on mRequestedIndex being nonzero here,
    // because either they're strictly greater than aIndex which is at least
    // zero, or they are equal to aIndex in which case aKeepNext must be true
    // if aIndex is zero.
    if (mRequestedIndex > aIndex || (mRequestedIndex == aIndex && !aKeepNext)) {
      mRequestedIndex = mRequestedIndex - 1;
    }

    return true;
  }
  return false;
}

NS_IMETHODIMP_(void)
nsSHistory::RemoveEntries(nsTArray<nsID>& aIDs, int32_t aStartIndex) {
  bool didRemove;
  RemoveEntries(aIDs, aStartIndex, &didRemove);
  if (didRemove) {
    RefPtr<BrowsingContext> rootBC = GetBrowsingContext();
    if (rootBC && rootBC->GetDocShell()) {
      rootBC->GetDocShell()->DispatchLocationChangeEvent();
    }
  }
}

void nsSHistory::RemoveEntries(nsTArray<nsID>& aIDs, int32_t aStartIndex,
                               bool* aDidRemove) {
  SHistoryChangeNotifier change(this);

  int32_t index = aStartIndex;
  while (index >= 0 && RemoveChildEntries(this, --index, aIDs)) {
  }
  int32_t minIndex = index;
  index = aStartIndex;
  while (index >= 0 && RemoveChildEntries(this, index++, aIDs)) {
  }

  // We need to remove duplicate nsSHEntry trees.
  *aDidRemove = false;
  while (index > minIndex) {
    if (index != mIndex && RemoveDuplicate(index, index < mIndex)) {
      *aDidRemove = true;
    }
    --index;
  }
}

void nsSHistory::RemoveFrameEntries(nsISHEntry* aEntry) {
  int32_t count = aEntry->GetChildCount();
  AutoTArray<nsID, 16> ids;
  for (int32_t i = 0; i < count; ++i) {
    nsCOMPtr<nsISHEntry> child;
    aEntry->GetChildAt(i, getter_AddRefs(child));
    if (child) {
      child->GetDocshellID(*ids.AppendElement());
    }
  }
  RemoveEntries(ids, mIndex);
}

void nsSHistory::RemoveDynEntries(int32_t aIndex, nsISHEntry* aEntry) {
  // Remove dynamic entries which are at the index and belongs to the container.
  nsCOMPtr<nsISHEntry> entry(aEntry);
  if (!entry) {
    GetEntryAtIndex(aIndex, getter_AddRefs(entry));
  }

  if (entry) {
    AutoTArray<nsID, 16> toBeRemovedEntries;
    GetDynamicChildren(entry, toBeRemovedEntries);
    if (toBeRemovedEntries.Length()) {
      RemoveEntries(toBeRemovedEntries, aIndex);
    }
  }
}

void nsSHistory::RemoveDynEntriesForBFCacheEntry(nsIBFCacheEntry* aBFEntry) {
  int32_t index;
  nsCOMPtr<nsISHEntry> shEntry;
  FindEntryForBFCache(static_cast<nsSHEntryShared*>(aBFEntry),
                      getter_AddRefs(shEntry), &index);
  if (shEntry) {
    RemoveDynEntries(index, shEntry);
  }
}

NS_IMETHODIMP
nsSHistory::UpdateIndex() {
  SHistoryChangeNotifier change(this);

  // Update the actual index with the right value.
  if (mIndex != mRequestedIndex && mRequestedIndex != -1) {
    mIndex = mRequestedIndex;
  }

  mRequestedIndex = -1;
  return NS_OK;
}

NS_IMETHODIMP
nsSHistory::GotoIndex(int32_t aIndex, bool aUserActivation) {
  nsTArray<LoadEntryResult> loadResults;
  nsresult rv = GotoIndex(aIndex, loadResults, /*aSameEpoch*/ false,
                          aIndex == mIndex, aUserActivation);
  NS_ENSURE_SUCCESS(rv, rv);

  LoadURIs(loadResults);
  return NS_OK;
}

NS_IMETHODIMP_(void)
nsSHistory::EnsureCorrectEntryAtCurrIndex(nsISHEntry* aEntry) {
  int index = mRequestedIndex == -1 ? mIndex : mRequestedIndex;
  if (index > -1 && (mEntries[index] != aEntry)) {
    ReplaceEntry(index, aEntry);
  }
}

nsresult nsSHistory::GotoIndex(int32_t aIndex,
                               nsTArray<LoadEntryResult>& aLoadResults,
                               bool aSameEpoch, bool aLoadCurrentEntry,
                               bool aUserActivation) {
  return LoadEntry(aIndex, LOAD_HISTORY, HIST_CMD_GOTOINDEX, aLoadResults,
                   aSameEpoch, aLoadCurrentEntry, aUserActivation);
}

NS_IMETHODIMP_(bool)
nsSHistory::HasUserInteractionAtIndex(int32_t aIndex) {
  nsCOMPtr<nsISHEntry> entry;
  GetEntryAtIndex(aIndex, getter_AddRefs(entry));
  if (!entry) {
    return false;
  }
  return entry->GetHasUserInteraction();
}

NS_IMETHODIMP
nsSHistory::CanGoBackFromEntryAtIndex(int32_t aIndex, bool* aCanGoBack) {
  *aCanGoBack = false;
  if (!StaticPrefs::browser_navigation_requireUserInteraction()) {
    *aCanGoBack = aIndex > 0;
    return NS_OK;
  }

  for (int32_t i = aIndex - 1; i >= 0; i--) {
    if (HasUserInteractionAtIndex(i)) {
      *aCanGoBack = true;
      break;
    }
  }

  return NS_OK;
}

nsresult nsSHistory::LoadNextPossibleEntry(
    int32_t aNewIndex, long aLoadType, uint32_t aHistCmd,
    nsTArray<LoadEntryResult>& aLoadResults, bool aLoadCurrentEntry,
    bool aUserActivation) {
  mRequestedIndex = -1;
  if (aNewIndex < mIndex) {
    return LoadEntry(aNewIndex - 1, aLoadType, aHistCmd, aLoadResults,
                     /*aSameEpoch*/ false, aLoadCurrentEntry, aUserActivation);
  }
  if (aNewIndex > mIndex) {
    return LoadEntry(aNewIndex + 1, aLoadType, aHistCmd, aLoadResults,
                     /*aSameEpoch*/ false, aLoadCurrentEntry, aUserActivation);
  }
  return NS_ERROR_FAILURE;
}

nsresult nsSHistory::LoadEntry(int32_t aIndex, long aLoadType,
                               uint32_t aHistCmd,
                               nsTArray<LoadEntryResult>& aLoadResults,
                               bool aSameEpoch, bool aLoadCurrentEntry,
                               bool aUserActivation) {
  MOZ_LOG(gSHistoryLog, LogLevel::Debug,
          ("LoadEntry(%d, 0x%lx, %u)", aIndex, aLoadType, aHistCmd));
  RefPtr<BrowsingContext> rootBC = GetBrowsingContext();
  if (!rootBC) {
    return NS_ERROR_FAILURE;
  }

  if (aIndex < 0 || aIndex >= Length()) {
    MOZ_LOG(gSHistoryLog, LogLevel::Debug, ("Index out of range"));
    // The index is out of range.
    // Clear the requested index in case it had bogus value. This way the next
    // load succeeds if the offset is reasonable.
    mRequestedIndex = -1;

    return NS_ERROR_FAILURE;
  }

  int32_t originalRequestedIndex = mRequestedIndex;
  int32_t previousRequest = mRequestedIndex > -1 ? mRequestedIndex : mIndex;
  int32_t requestedOffset = aIndex - previousRequest;

  // This is a normal local history navigation.

  nsCOMPtr<nsISHEntry> prevEntry;
  nsCOMPtr<nsISHEntry> nextEntry;
  GetEntryAtIndex(mIndex, getter_AddRefs(prevEntry));
  GetEntryAtIndex(aIndex, getter_AddRefs(nextEntry));
  if (!nextEntry || !prevEntry) {
    mRequestedIndex = -1;
    return NS_ERROR_FAILURE;
  }

  if (mozilla::SessionHistoryInParent()) {
    if (aHistCmd == HIST_CMD_GOTOINDEX) {
      // https://html.spec.whatwg.org/#history-traversal:
      // To traverse the history
      // "If entry has a different Document object than the current entry, then
      // run the following substeps: Remove any tasks queued by the history
      // traversal task source..."
      //
      // aSameEpoch is true only if the navigations would have been
      // generated in the same spin of the event loop (i.e. history.go(-2);
      // history.go(-1)) and from the same content process.
      if (aSameEpoch) {
        bool same_doc = false;
        prevEntry->SharesDocumentWith(nextEntry, &same_doc);
        if (!same_doc) {
          MOZ_LOG(
              gSHistoryLog, LogLevel::Debug,
              ("Aborting GotoIndex %d - same epoch and not same doc", aIndex));
          // Ignore this load. Before SessionHistoryInParent, this would
          // have been dropped in InternalLoad after we filter out SameDoc
          // loads.
          return NS_ERROR_FAILURE;
        }
      }
    }
  }
  // Keep note of requested history index in mRequestedIndex; after all bailouts
  mRequestedIndex = aIndex;

  // Remember that this entry is getting loaded at this point in the sequence

  nextEntry->SetLastTouched(++gTouchCounter);

  // Get the uri for the entry we are about to visit
  nsCOMPtr<nsIURI> nextURI = nextEntry->GetURI();

  MOZ_ASSERT(nextURI, "nextURI can't be null");

  // Send appropriate listener notifications.
  if (aHistCmd == HIST_CMD_GOTOINDEX) {
    // We are going somewhere else. This is not reload either
    NotifyListeners(mListeners, [](auto l) { l->OnHistoryGotoIndex(); });
  }

  if (mRequestedIndex == mIndex) {
    // Possibly a reload case
    InitiateLoad(nextEntry, rootBC, aLoadType, aLoadResults, aLoadCurrentEntry,
                 aUserActivation, requestedOffset);
    return NS_OK;
  }

  // Going back or forward.
  bool differenceFound = LoadDifferingEntries(
      prevEntry, nextEntry, rootBC, aLoadType, aLoadResults, aLoadCurrentEntry,
      aUserActivation, requestedOffset);
  if (!differenceFound) {
    // LoadNextPossibleEntry will change the offset by one, and in order
    // to keep track of the requestedOffset, need to reset mRequestedIndex to
    // the value it had initially.
    mRequestedIndex = originalRequestedIndex;
    // We did not find any differences. Go further in the history.
    return LoadNextPossibleEntry(aIndex, aLoadType, aHistCmd, aLoadResults,
                                 aLoadCurrentEntry, aUserActivation);
  }

  return NS_OK;
}

bool nsSHistory::LoadDifferingEntries(nsISHEntry* aPrevEntry,
                                      nsISHEntry* aNextEntry,
                                      BrowsingContext* aParent, long aLoadType,
                                      nsTArray<LoadEntryResult>& aLoadResults,
                                      bool aLoadCurrentEntry,
                                      bool aUserActivation, int32_t aOffset) {
  MOZ_ASSERT(aPrevEntry && aNextEntry && aParent);

  uint32_t prevID = aPrevEntry->GetID();
  uint32_t nextID = aNextEntry->GetID();

  // Check the IDs to verify if the pages are different.
  if (prevID != nextID) {
    // Set the Subframe flag if not navigating the root docshell.
    aNextEntry->SetIsSubFrame(aParent->Id() != mRootBC);
    InitiateLoad(aNextEntry, aParent, aLoadType, aLoadResults,
                 aLoadCurrentEntry, aUserActivation, aOffset);
    return true;
  }

  // The entries are the same, so compare any child frames
  int32_t pcnt = aPrevEntry->GetChildCount();
  int32_t ncnt = aNextEntry->GetChildCount();

  // Create an array for child browsing contexts.
  nsTArray<RefPtr<BrowsingContext>> browsingContexts;
  aParent->GetChildren(browsingContexts);

  // Search for something to load next.
  bool differenceFound = false;
  for (int32_t i = 0; i < ncnt; ++i) {
    // First get an entry which may cause a new page to be loaded.
    nsCOMPtr<nsISHEntry> nChild;
    aNextEntry->GetChildAt(i, getter_AddRefs(nChild));
    if (!nChild) {
      continue;
    }
    nsID docshellID;
    nChild->GetDocshellID(docshellID);

    // Then find the associated docshell.
    RefPtr<BrowsingContext> bcChild;
    for (const RefPtr<BrowsingContext>& bc : browsingContexts) {
      if (bc->GetHistoryID() == docshellID) {
        bcChild = bc;
        break;
      }
    }
    if (!bcChild) {
      continue;
    }

    // Then look at the previous entries to see if there was
    // an entry for the docshell.
    nsCOMPtr<nsISHEntry> pChild;
    for (int32_t k = 0; k < pcnt; ++k) {
      nsCOMPtr<nsISHEntry> child;
      aPrevEntry->GetChildAt(k, getter_AddRefs(child));
      if (child) {
        nsID dID;
        child->GetDocshellID(dID);
        if (dID == docshellID) {
          pChild = child;
          break;
        }
      }
    }
    if (!pChild) {
      continue;
    }

    // Finally recursively call this method.
    // This will either load a new page to shell or some subshell or
    // do nothing.
    if (LoadDifferingEntries(pChild, nChild, bcChild, aLoadType, aLoadResults,
                             aLoadCurrentEntry, aUserActivation, aOffset)) {
      differenceFound = true;
    }
  }
  return differenceFound;
}

void nsSHistory::InitiateLoad(nsISHEntry* aFrameEntry,
                              BrowsingContext* aFrameBC, long aLoadType,
                              nsTArray<LoadEntryResult>& aLoadResults,
                              bool aLoadCurrentEntry, bool aUserActivation,
                              int32_t aOffset) {
  MOZ_ASSERT(aFrameBC && aFrameEntry);

  LoadEntryResult* loadResult = aLoadResults.AppendElement();
  loadResult->mBrowsingContext = aFrameBC;

  nsCOMPtr<nsIURI> newURI = aFrameEntry->GetURI();
  RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(newURI);

  loadState->SetHasValidUserGestureActivation(aUserActivation);

  // At the time we initiate a history entry load we already know if https-first
  // was able to upgrade the request from http to https. There is no point in
  // re-retrying to upgrade.
  loadState->SetIsExemptFromHTTPSFirstMode(true);

  /* Set the loadType in the SHEntry too to  what was passed on.
   * This will be passed on to child subframes later in nsDocShell,
   * so that proper loadType is maintained through out a frameset
   */
  aFrameEntry->SetLoadType(aLoadType);

  loadState->SetLoadType(aLoadType);

  loadState->SetSHEntry(aFrameEntry);

  // If we're loading the current entry we want to treat it as not a
  // same-document navigation (see nsDocShell::IsSameDocumentNavigation), so
  // record that here in the LoadingSessionHistoryEntry.
  bool loadingCurrentEntry;
  if (mozilla::SessionHistoryInParent()) {
    loadingCurrentEntry = aLoadCurrentEntry;
  } else {
    loadingCurrentEntry =
        aFrameBC->GetDocShell() &&
        nsDocShell::Cast(aFrameBC->GetDocShell())->IsOSHE(aFrameEntry);
  }
  loadState->SetLoadIsFromSessionHistory(aOffset, loadingCurrentEntry);

  if (mozilla::SessionHistoryInParent()) {
    nsCOMPtr<SessionHistoryEntry> she = do_QueryInterface(aFrameEntry);
    aFrameBC->Canonical()->AddLoadingSessionHistoryEntry(
        loadState->GetLoadingSessionHistoryInfo()->mLoadId, she);
  }

  nsCOMPtr<nsIURI> originalURI = aFrameEntry->GetOriginalURI();
  loadState->SetOriginalURI(originalURI);

  loadState->SetLoadReplace(aFrameEntry->GetLoadReplace());

  loadState->SetLoadFlags(nsIWebNavigation::LOAD_FLAGS_NONE);
  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      aFrameEntry->GetTriggeringPrincipal();
  loadState->SetTriggeringPrincipal(triggeringPrincipal);
  loadState->SetFirstParty(false);
  nsCOMPtr<nsIContentSecurityPolicy> csp = aFrameEntry->GetCsp();
  loadState->SetCsp(csp);

  loadResult->mLoadState = std::move(loadState);
}

NS_IMETHODIMP
nsSHistory::CreateEntry(nsISHEntry** aEntry) {
  nsCOMPtr<nsISHEntry> entry;
  if (XRE_IsParentProcess() && mozilla::SessionHistoryInParent()) {
    entry = new SessionHistoryEntry();
  } else {
    entry = new nsSHEntry();
  }
  entry.forget(aEntry);
  return NS_OK;
}

static void CollectEntries(
    nsTHashMap<nsIDHashKey, SessionHistoryEntry*>& aHashtable,
    SessionHistoryEntry* aEntry) {
  aHashtable.InsertOrUpdate(aEntry->DocshellID(), aEntry);
  for (const RefPtr<SessionHistoryEntry>& entry : aEntry->Children()) {
    if (entry) {
      CollectEntries(aHashtable, entry);
    }
  }
}

static void UpdateEntryLength(
    nsTHashMap<nsIDHashKey, SessionHistoryEntry*>& aHashtable,
    SessionHistoryEntry* aNewEntry, bool aMove) {
  SessionHistoryEntry* oldEntry = aHashtable.Get(aNewEntry->DocshellID());
  if (oldEntry) {
    MOZ_ASSERT(oldEntry->GetID() != aNewEntry->GetID() || !aMove ||
               !aNewEntry->BCHistoryLength().Modified());
    aNewEntry->SetBCHistoryLength(oldEntry->BCHistoryLength());
    if (oldEntry->GetID() != aNewEntry->GetID()) {
      MOZ_ASSERT(!aMove);
      // If we have a new id then aNewEntry was created for a new load, so
      // record that in BCHistoryLength.
      ++aNewEntry->BCHistoryLength();
    } else if (aMove) {
      // We're moving the BCHistoryLength from the old entry to the new entry,
      // so we need to let the old entry know that it's not contributing to its
      // BCHistoryLength, and the new one that it does if the old one was
      // before.
      aNewEntry->BCHistoryLength().SetModified(
          oldEntry->BCHistoryLength().Modified());
      oldEntry->BCHistoryLength().SetModified(false);
    }
  }

  for (const RefPtr<SessionHistoryEntry>& entry : aNewEntry->Children()) {
    if (entry) {
      UpdateEntryLength(aHashtable, entry, aMove);
    }
  }
}

void nsSHistory::UpdateEntryLength(nsISHEntry* aOldEntry, nsISHEntry* aNewEntry,
                                   bool aMove) {
  nsCOMPtr<SessionHistoryEntry> oldSHE = do_QueryInterface(aOldEntry);
  nsCOMPtr<SessionHistoryEntry> newSHE = do_QueryInterface(aNewEntry);

  if (!oldSHE || !newSHE) {
    return;
  }

  nsTHashMap<nsIDHashKey, SessionHistoryEntry*> docshellIDToEntry;
  CollectEntries(docshellIDToEntry, oldSHE);

  ::UpdateEntryLength(docshellIDToEntry, newSHE, aMove);
}
