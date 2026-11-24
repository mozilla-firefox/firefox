/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWifiMonitor.h"

#include "mozilla/Components.h"
#include "mozilla/DelayedRunnable.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_network.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsINetworkLinkService.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsNetCID.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsWifiAccessPoint.h"
#include "nsXPCOM.h"
#include "nsXPCOMCID.h"

#if defined(XP_WIN)
#  include "WinWifiScanner.h"
#endif

#if defined(XP_MACOSX)
#  include "MacWifiScanner.h"
#endif

#if defined(__DragonFly__) || defined(__FreeBSD__)
#  include "FreeBsdWifiScanner.h"
#endif

#if defined(XP_SOLARIS)
#  include "SolarisWifiScanner.h"
#endif

#if defined(NECKO_WIFI_DBUS)
#  include "DbusWifiScanner.h"
#endif

using namespace mozilla;

LazyLogModule gWifiMonitorLog("WifiMonitor");
#define LOG(args) MOZ_LOG(gWifiMonitorLog, mozilla::LogLevel::Debug, args)

NS_IMPL_ISUPPORTS(nsWifiMonitor, nsIObserver, nsIWifiMonitor)

// Main thread only.
static uint64_t sNextPollingIndex = 1;

static uint64_t NextPollingIndex() {
  MOZ_ASSERT(NS_IsMainThread());
  ++sNextPollingIndex;

  // Any non-zero value is valid and we don't care about overflow beyond
  // that we never want the index to be zero.
  if (sNextPollingIndex == 0) {
    ++sNextPollingIndex;
  }
  return sNextPollingIndex;
}

// Should we poll wifi or just check it when our network changes?
// We poll when we are on a network where the wifi environment
// could reasonably be expected to change much -- so, on mobile.
static bool ShouldPollForNetworkType(const char16_t* aLinkType) {
  auto linkTypeU8 = NS_ConvertUTF16toUTF8(aLinkType);
  return linkTypeU8 == NS_NETWORK_LINK_TYPE_WIMAX ||
         linkTypeU8 == NS_NETWORK_LINK_TYPE_MOBILE ||
         linkTypeU8 == NS_NETWORK_LINK_TYPE_UNKNOWN;
}

// Enum value version.
static bool ShouldPollForNetworkType(uint32_t aLinkType) {
  return aLinkType == nsINetworkLinkService::LINK_TYPE_WIMAX ||
         aLinkType == nsINetworkLinkService::LINK_TYPE_MOBILE ||
         aLinkType == nsINetworkLinkService::LINK_TYPE_UNKNOWN;
}

nsWifiMonitor::nsWifiMonitor(UniquePtr<mozilla::WifiScanner>&& aScanner)
    : mWifiScanner(std::move(aScanner)) {
  LOG(("Creating nsWifiMonitor"));
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obsSvc = mozilla::services::GetObserverService();
  if (obsSvc) {
    obsSvc->AddObserver(this, NS_NETWORK_LINK_TOPIC, false);
    obsSvc->AddObserver(this, NS_NETWORK_LINK_TYPE_TOPIC, false);
    obsSvc->AddObserver(this, "xpcom-shutdown", false);
  }

  nsresult rv;
  nsCOMPtr<nsINetworkLinkService> nls;
  nls = do_GetService(NS_NETWORK_LINK_SERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv) && nls) {
    uint32_t linkType = nsINetworkLinkService::LINK_TYPE_UNKNOWN;
    rv = nls->GetLinkType(&linkType);
    if (NS_SUCCEEDED(rv)) {
      mShouldPollForCurrentNetwork = ShouldPollForNetworkType(linkType);
      if (ShouldPoll()) {
        mPollingId = NextPollingIndex();
        DispatchScanToBackgroundThread(mPollingId);
      }
      LOG(("nsWifiMonitor network type: %u | shouldPoll: %s", linkType,
           mShouldPollForCurrentNetwork ? "true" : "false"));
    }
  }
}
nsWifiMonitor::~nsWifiMonitor() { LOG(("Destroying nsWifiMonitor")); }

void nsWifiMonitor::Close() {
  nsCOMPtr<nsIObserverService> obsSvc = mozilla::services::GetObserverService();
  if (obsSvc) {
    obsSvc->RemoveObserver(this, NS_NETWORK_LINK_TOPIC);
    obsSvc->RemoveObserver(this, NS_NETWORK_LINK_TYPE_TOPIC);
    obsSvc->RemoveObserver(this, "xpcom-shutdown");
  }

  mPollingId = 0;
  if (mThread) {
    mThread->Shutdown();
  }
}

NS_IMETHODIMP
nsWifiMonitor::Observe(nsISupports* subject, const char* topic,
                       const char16_t* data) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!strcmp(topic, "xpcom-shutdown")) {
    // Make sure any wifi-polling stops.
    LOG(("nsWifiMonitor received shutdown"));
    Close();
  } else if (!strcmp(topic, NS_NETWORK_LINK_TOPIC)) {
    // Network connectivity has either been gained, lost, or changed (e.g.
    // by changing Wifi network).  Issue an immediate one-time scan.
    // If we were polling, keep polling.
    LOG(("nsWifiMonitor %p | mPollingId %" PRIu64
         " | received: " NS_NETWORK_LINK_TOPIC " with status %s",
         this, mPollingId.load(), NS_ConvertUTF16toUTF8(data).get()));
    DispatchScanToBackgroundThread(0);
  } else if (!strcmp(topic, NS_NETWORK_LINK_TYPE_TOPIC)) {
    // Network type has changed (e.g. from wifi to mobile).  When on some
    // network types, we poll wifi.  This event does not indicate that a
    // new scan would be beneficial right now, so we only issue one if
    // we need to begin polling.
    // Use IDs to make sure only one task is polling at a time.
    LOG(("nsWifiMonitor %p | mPollingId %" PRIu64
         " | received: " NS_NETWORK_LINK_TYPE_TOPIC " with status %s",
         this, mPollingId.load(), NS_ConvertUTF16toUTF8(data).get()));

    bool wasPolling = ShouldPoll();
    MOZ_ASSERT(wasPolling || mPollingId == 0);

    mShouldPollForCurrentNetwork = ShouldPollForNetworkType(data);
    if (!wasPolling && ShouldPoll()) {
      // We weren't polling, so start now.
      mPollingId = NextPollingIndex();
      DispatchScanToBackgroundThread(mPollingId);
    } else if (!ShouldPoll()) {
      // Stop polling if we were.
      mPollingId = 0;
    }
  }

  return NS_OK;
}

void nsWifiMonitor::EnsureWifiScanner() {
  if (mWifiScanner) {
    return;
  }

  LOG(("Constructing WifiScanner"));
  mWifiScanner = std::make_unique<mozilla::WifiScannerImpl>();
}

NS_IMETHODIMP nsWifiMonitor::StartWatching(nsIWifiListener* aListener,
                                           bool aForcePolling) {
  LOG(("nsWifiMonitor::StartWatching %p | listener %p | mPollingId %" PRIu64
       " | aForcePolling %s",
       this, aListener, mPollingId.load(), aForcePolling ? "true" : "false"));
  MOZ_ASSERT(NS_IsMainThread());

  if (!aListener) {
    return NS_ERROR_NULL_POINTER;
  }

  if (!mListeners.InsertOrUpdate(aListener, WifiListenerData(aForcePolling),
                                 mozilla::fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Run a new scan to update the new listener.  If we were polling then
  // stop that polling and start a new polling interval now.
  MOZ_ASSERT(mPollingId == 0 || ShouldPoll());
  if (aForcePolling) {
    ++mNumPollingListeners;
  }
  if (ShouldPoll()) {
    mPollingId = NextPollingIndex();
  }
  return DispatchScanToBackgroundThread(mPollingId);
}

NS_IMETHODIMP nsWifiMonitor::StopWatching(nsIWifiListener* aListener) {
  LOG(("nsWifiMonitor::StopWatching %p | listener %p | mPollingId %" PRIu64,
       this, aListener, mPollingId.load()));
  MOZ_ASSERT(NS_IsMainThread());

  if (!aListener) {
    return NS_ERROR_NULL_POINTER;
  }

  auto maybeData = mListeners.MaybeGet(aListener);
  if (!maybeData) {
    return NS_ERROR_INVALID_ARG;
  }

  if (maybeData->mShouldPoll) {
    --mNumPollingListeners;
  }

  mListeners.Remove(aListener);

  if (!ShouldPoll()) {
    // Stop polling (if we were).
    LOG(("nsWifiMonitor::StopWatching clearing polling ID"));
    mPollingId = 0;
  }

  return NS_OK;
}

nsresult nsWifiMonitor::DispatchScanToBackgroundThread(uint64_t aPollingId,
                                                       uint32_t aWaitMs) {
  RefPtr<Runnable> runnable = NewRunnableMethod<uint64_t>(
      "WifiScannerThread", this, &nsWifiMonitor::Scan, aPollingId);

  if (!mThread) {
    MOZ_ASSERT(NS_IsMainThread());

#ifndef XP_MACOSX
    nsIThreadManager::ThreadCreationOptions options = {};
#else
    // If this ASSERT fails, we've increased our default stack size and
    // may no longer need to special-case the stack size on macOS.
    static_assert(kMacOSWifiMonitorStackSize >
                  nsIThreadManager::DEFAULT_STACK_SIZE);

    // Mac needs a stack size larger than the default for CoreWLAN.
    nsIThreadManager::ThreadCreationOptions options = {
        .stackSize = kMacOSWifiMonitorStackSize};
#endif

    nsresult rv = NS_NewNamedThread("Wifi Monitor", getter_AddRefs(mThread),
                                    nullptr, options);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aWaitMs) {
    return mThread->DelayedDispatch(runnable.forget(), aWaitMs);
  }

  return mThread->Dispatch(runnable.forget());
}

bool nsWifiMonitor::IsBackgroundThread() {
  return NS_GetCurrentThread() == mThread;
}

void nsWifiMonitor::Scan(uint64_t aPollingId) {
  MOZ_ASSERT(IsBackgroundThread());
  LOG(("nsWifiMonitor::Scan aPollingId: %" PRIu64 " | mPollingId: %" PRIu64,
       aPollingId, mPollingId.load()));

  // If we are using a stale polling ID then stop.  If this request to
  // Scan is not for polling (aPollingId is 0) then always allow it.
  if (aPollingId && mPollingId != aPollingId) {
    LOG(("nsWifiMonitor::Scan stopping polling"));
    return;
  }

  LOG(("nsWifiMonitor::Scan starting DoScan with id: %" PRIu64, aPollingId));
  nsresult rv = DoScan();
  LOG(("nsWifiMonitor::Scan DoScan complete | rv = %d",
       static_cast<uint32_t>(rv)));

  if (NS_FAILED(rv)) {
    rv = NS_DispatchToMainThread(NewRunnableMethod<nsresult>(
        "PassErrorToWifiListeners", this,
        &nsWifiMonitor::PassErrorToWifiListeners, rv));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  // If we are polling then we re-issue Scan after a delay.
  // We re-check the polling IDs since mPollingId may have changed.
  if (aPollingId && aPollingId == mPollingId) {
    uint32_t periodMs = StaticPrefs::network_wifi_scanning_period();
    if (periodMs) {
      LOG(("nsWifiMonitor::Scan requesting future scan with id: %" PRIu64
           " | periodMs: %u",
           aPollingId, periodMs));
      DispatchScanToBackgroundThread(aPollingId, periodMs);
    } else {
      // Polling for wifi-scans is disabled.
      mPollingId = 0;
    }
  }

  LOG(("nsWifiMonitor::Scan complete"));
}

nsresult nsWifiMonitor::DoScan() {
  MOZ_ASSERT(IsBackgroundThread());

  EnsureWifiScanner();
  MOZ_ASSERT(mWifiScanner);

  LOG(("Scanning Wifi for access points"));
  nsTArray<RefPtr<nsIWifiAccessPoint>> accessPoints;
  nsresult rv = mWifiScanner->GetAccessPointsFromWLAN(accessPoints);
  if (NS_FAILED(rv)) {
    return rv;
  }

  LOG(("Sorting wifi access points"));
  accessPoints.Sort([](const RefPtr<nsIWifiAccessPoint>& ia,
                       const RefPtr<nsIWifiAccessPoint>& ib) {
    const auto& a = static_cast<const nsWifiAccessPoint&>(*ia);
    const auto& b = static_cast<const nsWifiAccessPoint&>(*ib);
    return a.Compare(b);
  });

  // Sorted compare to see if access point list has changed.
  LOG(("Checking for new access points"));
  bool accessPointsChanged =
      accessPoints.Length() != mLastAccessPoints.Length();
  if (!accessPointsChanged) {
    auto itAp = accessPoints.begin();
    auto itLastAp = mLastAccessPoints.begin();
    while (itAp != accessPoints.end()) {
      const auto& a = static_cast<const nsWifiAccessPoint&>(**itAp);
      const auto& b = static_cast<const nsWifiAccessPoint&>(**itLastAp);
      if (a != b) {
        accessPointsChanged = true;
        break;
      }
      ++itAp;
      ++itLastAp;
    }
  }

  mLastAccessPoints = std::move(accessPoints);

  LOG(("Sending Wifi access points to the main thread"));
  auto* mainThread = GetMainThreadSerialEventTarget();
  if (!mainThread) {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_DispatchToMainThread(
      NewRunnableMethod<nsTArray<RefPtr<nsIWifiAccessPoint>>, bool>(
          "CallWifiListeners", this, &nsWifiMonitor::CallWifiListeners,
          mLastAccessPoints.Clone(), accessPointsChanged));
}

template <typename CallbackFn>
nsresult nsWifiMonitor::NotifyListeners(CallbackFn&& aCallback) {
  // Listeners may (un)register other listeners while we iterate,
  // so we iterate over a copy and re-check membership as we go.
  // Iteration order is not important.
  auto listenersCopy(mListeners.Clone());
  for (auto iter = listenersCopy.begin(); iter != listenersCopy.end(); ++iter) {
    auto maybeIter = mListeners.MaybeGet(iter->GetKey());
    if (maybeIter) {
      aCallback(iter->GetKey(), *maybeIter);
    }
  }
  return NS_OK;
}

nsresult nsWifiMonitor::CallWifiListeners(
    const nsTArray<RefPtr<nsIWifiAccessPoint>>& aAccessPoints,
    bool aAccessPointsChanged) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG(("Sending wifi access points to the listeners"));
  return NotifyListeners(
      [&](nsIWifiListener* aListener, WifiListenerData& aListenerData) {
        if (!aListenerData.mHasSentData || aAccessPointsChanged) {
          aListenerData.mHasSentData = true;
          aListener->OnChange(aAccessPoints);
        }
      });
}

nsresult nsWifiMonitor::PassErrorToWifiListeners(nsresult rv) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG(("About to send error to the wifi listeners"));
  return NotifyListeners([&](nsIWifiListener* aListener, WifiListenerData&) {
    aListener->OnError(rv);
  });
}

bool nsWifiMonitor::GetHasWifiAdapter() {
#ifdef XP_WIN
  EnsureWifiScanner();
  MOZ_ASSERT(mWifiScanner);
  return static_cast<WifiScannerImpl*>(mWifiScanner.get())->HasWifiAdapter();
#else
  MOZ_ASSERT_UNREACHABLE(
      "nsWifiMonitor::HasWifiAdapter is not available on this platform");
  return false;
#endif
}
