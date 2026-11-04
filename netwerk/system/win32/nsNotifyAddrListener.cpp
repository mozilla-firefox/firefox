/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We define this to make our use of inet_ntoa() pass. The "proper" function
// inet_ntop() doesn't exist on Windows XP.
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "nsNotifyAddrListener.h"

// clang-format off
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <ole2.h>
#include <netcon.h>
#include <objbase.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <tcpmib.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <netlistmgr.h>
#include <iprtrmib.h>
// clang-format on

#include <algorithm>
#include <vector>

#include "../LinkServiceCommon.h"
#include "mozilla/Base64.h"
#include "mozilla/Logging.h"
#include "mozilla/SHA1.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/widget/WinRegistry.h"
#include "nsCRT.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsComponentManagerUtils.h"
#include "nsIObserverService.h"
#include "nsNetAddr.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsThreadPool.h"
#include "nsThreadUtils.h"

#if defined(__MINGW32__) || defined(__MINGW64__)
BOOLEAN IN6_IS_ADDR_LOOPBACK(const struct in6_addr* a) {
  auto words = a->s6_words;
  return (words[0] == 0) && (words[1] == 0) && (words[2] == 0) &&
         (words[3] == 0) && (words[4] == 0) && (words[5] == 0) &&
         (words[6] == 0) && (words[7] == 0x0100);
}

#  define IN6_IS_ADDR_LINKLOCAL(_addr)         \
    ((((const uint8_t*)(_addr))[0] == 0xfe) && \
     ((((const uint8_t*)(_addr))[1] & 0xc0) == 0x80))

#endif  // defined(__MINGW32__)  || defined (__MINGW64__)

using namespace mozilla;

static LazyLogModule gNotifyAddrLog("nsNotifyAddr");
#define LOG(args) MOZ_LOG(gNotifyAddrLog, mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() MOZ_LOG_TEST(gNotifyAddrLog, mozilla::LogLevel::Debug)

// period during which to absorb subsequent network change events, in
// milliseconds
static const unsigned int kNetworkChangeCoalescingPeriod = 1000;

NS_IMPL_ISUPPORTS(nsNotifyAddrListener, nsINetworkLinkService, nsIRunnable,
                  nsIObserver)

static Atomic<bool, MemoryOrdering::Relaxed> sHasNonLocalIPv6{true};

nsNotifyAddrListener::nsNotifyAddrListener() = default;

nsNotifyAddrListener::~nsNotifyAddrListener() {
  NS_ASSERTION(!mThread, "nsNotifyAddrListener thread shutdown failed");
}

NS_IMETHODIMP
nsNotifyAddrListener::GetIsLinkUp(bool* aIsUp) {
  if (!mCheckAttempted && !mStatusKnown) {
    mCheckAttempted = true;
    CheckLinkStatus();
  }

  *aIsUp = mLinkUp;
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetLinkStatusKnown(bool* aIsUp) {
  *aIsUp = mStatusKnown;
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetLinkType(uint32_t* aLinkType) {
  NS_ENSURE_ARG_POINTER(aLinkType);

  // XXX This function has not yet been implemented for this platform
  *aLinkType = nsINetworkLinkService::LINK_TYPE_UNKNOWN;
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetNetworkID(nsACString& aNetworkID) {
  MutexAutoLock lock(mMutex);
  aNetworkID = mNetworkId;
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetNetworkIDSeed(nsACString& aSeed) {
  aSeed = GetNetworkIdSeed();
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::MatchNetworkID(const nsACString& aNetworkID,
                                     const nsACString& aSeed,
                                     nsACString& aMatchingNetworkID,
                                     bool* aResult) {
  MutexAutoLock lock(mMutex);
  *aResult = MatchNetworkId(aNetworkID, aSeed, mUnseededNetworkId, mNetworkId,
                            aMatchingNetworkID);
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetDnsSuffixList(nsTArray<nsCString>& aDnsSuffixList) {
  aDnsSuffixList.Clear();
  MutexAutoLock lock(mMutex);
  aDnsSuffixList.AppendElements(mDnsSuffixList);
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetResolvers(nsTArray<RefPtr<nsINetAddr>>& aResolvers) {
  nsTArray<mozilla::net::NetAddr> addresses;
  nsresult rv = GetNativeResolvers(addresses);
  if (NS_FAILED(rv)) {
    return rv;
  }

  for (const auto& addr : addresses) {
    aResolvers.AppendElement(MakeRefPtr<nsNetAddr>(&addr));
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetNativeResolvers(
    nsTArray<mozilla::net::NetAddr>& aResolvers) {
  aResolvers.Clear();
  MutexAutoLock lock(mMutex);
  aResolvers.AppendElements(mDNSResolvers);
  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::GetPlatformDNSIndications(
    uint32_t* aPlatformDNSIndications) {
  *aPlatformDNSIndications = mPlatformDNSIndications;
  return NS_OK;
}

//
// Hash the sorted network ids
//
void nsNotifyAddrListener::HashSortedNetworkIds(std::vector<GUID> nwGUIDS,
                                                SHA1Sum& sha1) {
  std::sort(nwGUIDS.begin(), nwGUIDS.end(), [](const GUID& a, const GUID& b) {
    return memcmp(&a, &b, sizeof(GUID)) < 0;
  });

  for (auto const& nwGUID : nwGUIDS) {
    sha1.update(&nwGUID, sizeof(GUID));

    if (LOG_ENABLED()) {
      nsPrintfCString guid("%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
                           nwGUID.Data1, nwGUID.Data2, nwGUID.Data3,
                           nwGUID.Data4[0], nwGUID.Data4[1], nwGUID.Data4[2],
                           nwGUID.Data4[3], nwGUID.Data4[4], nwGUID.Data4[5],
                           nwGUID.Data4[6], nwGUID.Data4[7]);
      LOG(("calculateNetworkId: interface networkID: %s\n", guid.get()));
    }
  }
}

//
// Figure out the current "network identification" string.
//
void nsNotifyAddrListener::calculateNetworkId(void) {
  MOZ_ASSERT(!NS_IsMainThread(), "Must not be called on the main thread");

  // No need to recompute the networkId if we're shutting down.
  if (mShutdown) {
    return;
  }

  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
    return;
  }

  auto unitialize = MakeScopeExit([]() { CoUninitialize(); });

  RefPtr<INetworkListManager> nlm;
  HRESULT hr = CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL,
                                IID_INetworkListManager, getter_AddRefs(nlm));
  if (NS_WARN_IF(FAILED(hr))) {
    LOG(("CoCreateInstance error: %lX", hr));
    return;
  }
  RefPtr<IEnumNetworks> enumNetworks;
  hr = nlm->GetNetworks(NLM_ENUM_NETWORK_CONNECTED,
                        getter_AddRefs(enumNetworks));
  if (NS_WARN_IF(FAILED(hr))) {
    LOG(("GetNetworks error: %lX", hr));
    return;
  }

  // We will hash the found network ids
  // for privacy reasons
  SHA1Sum sha1;

  // The networks stored in enumNetworks
  // are not ordered. We will sort them
  // To keep a consistent hash
  // regardless of the found networks order.
  std::vector<GUID> nwGUIDS;

  // Consume the found networks iterator
  while (true) {
    RefPtr<INetwork> network;
    hr = enumNetworks->Next(1, getter_AddRefs(network), nullptr);
    if (hr != S_OK) {
      break;
    }

    GUID nwGUID;
    hr = network->GetNetworkId(&nwGUID);
    if (hr != S_OK) {
      continue;
    }
    nwGUIDS.push_back(nwGUID);
  }

  if (nwGUIDS.empty()) {
    bool idChanged = false;
    {
      MutexAutoLock lock(mMutex);
      if (!mNetworkId.IsEmpty()) {
        idChanged = true;
      }
      mNetworkId.Truncate();
      mUnseededNetworkId.reset();
    }
    LOG(("calculateNetworkId: no network ID - no active networks"));
    glean::network::id.AccumulateSingleSample(0);
    if (idChanged) {
      NotifyObservers(NS_NETWORK_ID_CHANGED_TOPIC, nullptr);
    }
    return;
  }

  nsAutoCString output;
  SHA1Sum::Hash digest;
  HashSortedNetworkIds(nwGUIDS, sha1);
  SHA1Sum unseeded = sha1;
  SeedNetworkId(sha1);
  sha1.finish(digest);
  nsCString newString(reinterpret_cast<char*>(digest), SHA1Sum::kHashSize);
  nsresult rv = Base64Encode(newString, output);
  if (NS_FAILED(rv)) {
    {
      MutexAutoLock lock(mMutex);
      mNetworkId.Truncate();
      mUnseededNetworkId.reset();
    }
    glean::network::id.AccumulateSingleSample(0);
    LOG(("calculateNetworkId: no network ID Base64Encode error %X",
         uint32_t(rv)));
    return;
  }

  MutexAutoLock lock(mMutex);
  mUnseededNetworkId = mozilla::Some(unseeded);
  if (output != mNetworkId) {
    mNetworkId = output;
    glean::network::id.AccumulateSingleSample(1);
    LOG(("calculateNetworkId: new NetworkID: %s", output.get()));
    NotifyObservers(NS_NETWORK_ID_CHANGED_TOPIC, nullptr);
  } else {
    glean::network::id.AccumulateSingleSample(2);
    LOG(("calculateNetworkId: same NetworkID: %s", output.get()));
  }
}

// Static Callback function for NotifyIpInterfaceChange API.
static void WINAPI OnInterfaceChange(PVOID callerContext,
                                     PMIB_IPINTERFACE_ROW row,
                                     MIB_NOTIFICATION_TYPE notificationType) {
  nsNotifyAddrListener* notify =
      static_cast<nsNotifyAddrListener*>(callerContext);
  notify->CheckLinkStatus();
}

DWORD
nsNotifyAddrListener::nextCoalesceWaitTime() {
  // check if coalescing period should continue
  double period = (TimeStamp::Now() - mChangeTime).ToMilliseconds();
  if (period >= kNetworkChangeCoalescingPeriod) {
    NotifyObservers(NS_NETWORK_LINK_TOPIC, NS_NETWORK_LINK_DATA_CHANGED);
    mCoalescingActive = false;
    return INFINITE;  // return default
  } else {
    // wait no longer than to the end of the period
    return static_cast<DWORD>(kNetworkChangeCoalescingPeriod - period);
  }
}

NS_IMETHODIMP
nsNotifyAddrListener::Run() {
  mStartTime = TimeStamp::Now();

  calculateNetworkId();

  DWORD waitTime = INFINITE;

  // Windows Vista and newer versions.
  HANDLE interfacechange;
  // The callback will simply invoke CheckLinkStatus()
  DWORD ret = NotifyIpInterfaceChange(
      StaticPrefs::network_notify_IPv6() ? AF_UNSPEC
                                         : AF_INET,  // IPv4 and IPv6
      (PIPINTERFACE_CHANGE_CALLBACK)OnInterfaceChange,
      this,                                        // pass to callback
      StaticPrefs::network_notify_initial_call(),  // initial notification
      &interfacechange);

  if (ret == NO_ERROR) {
    do {
      ret = WaitForSingleObject(mCheckEvent, waitTime);
      if (!mShutdown) {
        waitTime = nextCoalesceWaitTime();
      } else {
        break;
      }
    } while (ret != WAIT_FAILED);
    CancelMibChangeNotify2(interfacechange);
  } else {
    LOG(("Link Monitor: NotifyIpInterfaceChange returned %d\n", (int)ret));
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNotifyAddrListener::Observe(nsISupports* subject, const char* topic,
                              const char16_t* data) {
  if (!strcmp("xpcom-shutdown-threads", topic)) Shutdown();

  return NS_OK;
}

nsresult nsNotifyAddrListener::Init(void) {
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (!observerService) return NS_ERROR_FAILURE;

  nsresult rv =
      observerService->AddObserver(this, "xpcom-shutdown-threads", false);
  NS_ENSURE_SUCCESS(rv, rv);

  mCheckEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  NS_ENSURE_TRUE(mCheckEvent, NS_ERROR_OUT_OF_MEMORY);

  nsCOMPtr<nsIThreadPool> threadPool = new nsThreadPool();
  MOZ_ALWAYS_SUCCEEDS(threadPool->SetThreadLimit(1));
  MOZ_ALWAYS_SUCCEEDS(
      threadPool->SetThreadStackSize(nsIThreadManager::kThreadPoolStackSize));
  MOZ_ALWAYS_SUCCEEDS(threadPool->SetName("Link Monitor"_ns));
  mThread = threadPool.forget();

  return mThread->Dispatch(this, NS_DISPATCH_NORMAL);
}

nsresult nsNotifyAddrListener::Shutdown(void) {
  // remove xpcom shutdown observer
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService)
    observerService->RemoveObserver(this, "xpcom-shutdown-threads");

  if (!mCheckEvent) return NS_OK;

  mShutdown = true;
  SetEvent(mCheckEvent);

  nsresult rv = mThread ? mThread->ShutdownWithTimeout(2000) : NS_OK;

  // Have to break the cycle here, otherwise nsNotifyAddrListener holds
  // onto the thread and the thread holds onto the nsNotifyAddrListener
  // via its mRunnable
  mThread = nullptr;

  CloseHandle(mCheckEvent);
  mCheckEvent = nullptr;

  return rv;
}

/*
 * A network event has been registered. Delay the actual sending of the event
 * for a while and absorb subsequent events in the mean time in an effort to
 * squash potentially many triggers into a single event.
 * Only ever called from the same thread.
 */
nsresult nsNotifyAddrListener::NetworkChanged() {
  if (mCoalescingActive) {
    LOG(("NetworkChanged: absorbed an event (coalescing active)\n"));
  } else {
    // A fresh trigger!
    mChangeTime = TimeStamp::Now();
    mCoalescingActive = true;
    SetEvent(mCheckEvent);
    LOG(("NetworkChanged: coalescing period started\n"));
  }
  return NS_OK;
}

/* Sends the given event.  Assumes aTopic/aData never goes out of scope (static
 * strings are ideal).
 */
nsresult nsNotifyAddrListener::NotifyObservers(const char* aTopic,
                                               const char* aData) {
  LOG(("NotifyObservers: %s=%s\n", aTopic, aData));

  if (mShutdown) {
    LOG(("NotifyObservers call failed when called during shutdown"));
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  auto runnable = [self = RefPtr<nsNotifyAddrListener>(this), aTopic, aData] {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService)
      observerService->NotifyObservers(
          static_cast<nsINetworkLinkService*>(self.get()), aTopic,
          !aData ? nullptr : NS_ConvertASCIItoUTF16(aData).get());
  };
  nsresult rv = NS_DispatchToMainThread(NS_NewRunnableFunction(
      "nsNotifyAddrListener::NotifyObservers", runnable));
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "nsNotifyAddrListener::NotifyObservers Failed to dispatch observer "
        "notification");
  }
  return rv;
}

DWORD
nsNotifyAddrListener::CheckAdaptersAddresses(void) {
  MOZ_ASSERT(!NS_IsMainThread(), "Don't call this on the main thread");
  ULONG len = 16384;

  PIP_ADAPTER_ADDRESSES adapterList = (PIP_ADAPTER_ADDRESSES)moz_xmalloc(len);

  ULONG flags = GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST;
  if (!StaticPrefs::network_notify_resolvers()) {
    flags |= GAA_FLAG_SKIP_DNS_SERVER;
  }

  DWORD ret =
      GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapterList, &len);
  if (ret == ERROR_BUFFER_OVERFLOW) {
    free(adapterList);
    adapterList = static_cast<PIP_ADAPTER_ADDRESSES>(moz_xmalloc(len));

    ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapterList, &len);
  }

  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
    free(adapterList);
    return ERROR_NOT_SUPPORTED;
  }

  //
  // Since NotifyIpInterfaceChange() signals a change more often than we
  // think is a worthy change, we checksum the entire state of all interfaces
  // that are UP. If the checksum is the same as previous check, nothing
  // of interest changed!
  //
  ULONG sumAll = 0;

  nsTArray<nsCString> dnsSuffixList;
  nsTArray<mozilla::net::NetAddr> resolvers;
  uint32_t platformDNSIndications = NONE_DETECTED;
  bool hasNonLocalIPv6 = false;
  if (ret == ERROR_SUCCESS) {
    bool linkUp = false;
    ULONG sum = 0;

    for (PIP_ADAPTER_ADDRESSES adapter = adapterList; adapter;
         adapter = adapter->Next) {
      if (adapter->OperStatus != IfOperStatusUp ||
          !adapter->FirstUnicastAddress ||
          adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
          nsDependentString(adapter->FriendlyName).Find(u"VMnet") !=
              kNotFound ||
          nsDependentString(adapter->Description).Find(u"VMnet") != kNotFound) {
        continue;
      }

      LOG(("Adapter %s type: %lu",
           NS_ConvertUTF16toUTF8(adapter->FriendlyName).get(),
           adapter->IfType));

      if (adapter->IfType == IF_TYPE_PPP ||
          adapter->IfType == IF_TYPE_PROP_VIRTUAL ||
          nsDependentString(adapter->FriendlyName).Find(u"VPN") != kNotFound ||
          nsDependentString(adapter->Description).Find(u"VPN") != kNotFound) {
        LOG(("VPN connection found"));
        platformDNSIndications |= VPN_DETECTED;
      }

      sum <<= 2;
      // Add chars from AdapterName to the checksum.
      for (int i = 0; adapter->AdapterName[i]; ++i) {
        sum += adapter->AdapterName[i];
      }

      // Add bytes from each socket address to the checksum.
      for (PIP_ADAPTER_UNICAST_ADDRESS pip = adapter->FirstUnicastAddress; pip;
           pip = pip->Next) {
        SOCKET_ADDRESS* sockAddr = &pip->Address;
        for (int i = 0; i < sockAddr->iSockaddrLength; ++i) {
          sum += (reinterpret_cast<unsigned char*>(sockAddr->lpSockaddr))[i];
        }

        if (sockAddr->lpSockaddr->sa_family == AF_INET6) {
          sockaddr_in6* ipv6 =
              reinterpret_cast<sockaddr_in6*>(sockAddr->lpSockaddr);
          if (!IN6_IS_ADDR_LINKLOCAL(&ipv6->sin6_addr) &&
              !IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr)) {
            hasNonLocalIPv6 = true;
          }
        }
      }

      for (IP_ADAPTER_DNS_SERVER_ADDRESS* pDnServer =
               adapter->FirstDnsServerAddress;
           pDnServer; pDnServer = pDnServer->Next) {
        mozilla::net::NetAddr addr;
        if (pDnServer->Address.lpSockaddr->sa_family == AF_INET) {
          const struct sockaddr_in* sin =
              (const struct sockaddr_in*)pDnServer->Address.lpSockaddr;
          addr.inet.family = AF_INET;
          addr.inet.ip = sin->sin_addr.s_addr;
          addr.inet.port = sin->sin_port;
        } else if (pDnServer->Address.lpSockaddr->sa_family == AF_INET6) {
          const struct sockaddr_in6* sin6 =
              (const struct sockaddr_in6*)pDnServer->Address.lpSockaddr;
          addr.inet6.family = AF_INET6;
          memcpy(&addr.inet6.ip.u8, &sin6->sin6_addr, sizeof(addr.inet6.ip.u8));
          addr.inet6.port = sin6->sin6_port;
        } else {
          NS_WARNING("Unexpected addr type");
          continue;
        }

        LOG(("Found DNS resolver=%s", addr.ToString().get()));
        resolvers.AppendElement(addr);
      }

      if (StaticPrefs::network_notify_dnsSuffixList()) {
        nsCString suffix = NS_ConvertUTF16toUTF8(adapter->DnsSuffix);
        if (!suffix.IsEmpty()) {
          LOG(("  found DNS suffix=%s\n", suffix.get()));
          dnsSuffixList.AppendElement(suffix);
        }
      }

      linkUp = true;
      sumAll ^= sum;
    }
    mLinkUp = linkUp;
    mStatusKnown = true;
  }
  free(adapterList);
  LOG(("has IPv6: %d", hasNonLocalIPv6));
  sHasNonLocalIPv6 = hasNonLocalIPv6;

  if (mLinkUp) {
    /* Store the checksum only if one or more interfaces are up */
    mIPInterfaceChecksum = sumAll;
  }

  CoUninitialize();

  if (StaticPrefs::network_notify_dnsSuffixList()) {
    // It seems that the only way to retrieve non-connection specific DNS
    // suffixes is via the Windows registry.

    // This function takes a registry path. If aRegPath\\SearchList is
    // found and successfully parsed, then it returns true. Otherwise it
    // returns false.
    auto checkRegistry = [&dnsSuffixList](const nsAString& aRegPath) -> bool {
      nsAutoString wideSuffixString;
      if (!mozilla::widget::WinRegistry::GetString(
              HKEY_LOCAL_MACHINE, nsString(aRegPath), u"SearchList"_ns,
              wideSuffixString)) {
        LOG(("  reading registry key %s failed\n",
             NS_ConvertUTF16toUTF8(aRegPath).get()));
        return false;
      }

      // Normally the key should not contain whitespace, but editing the
      // registry manually or through gpedit doesn't always enforce this.
      nsAutoCString list = NS_ConvertUTF16toUTF8(wideSuffixString);
      list.StripWhitespace();
      for (const nsACString& suffix : list.Split(',')) {
        LOG(("  appending DNS suffix from registry: %s\n",
             PromiseFlatCString(suffix).get()));
        if (!suffix.IsEmpty()) {
          dnsSuffixList.AppendElement(suffix);
        }
      }

      return true;
    };

    // The Local group policy overrides the user set suffix list, so we must
    // first check the registry key that is set by gpedit, and if that fails we
    // fall back to the one that is set by the user.
    if (!checkRegistry(nsLiteralString(
            u"SOFTWARE\\Policies\\Microsoft\\Windows NT\\DNSClient"))) {
      checkRegistry(nsLiteralString(
          u"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters"));
    }
  }

  auto registryChildCount = [](const nsAString& aRegPath) -> uint32_t {
    mozilla::widget::WinRegistry::Key key(
        HKEY_LOCAL_MACHINE, nsString(aRegPath),
        mozilla::widget::WinRegistry::KeyMode::Read);
    if (!key) {
      LOG(("  opening registry key %s failed\n",
           NS_ConvertUTF16toUTF8(aRegPath).get()));
      return 0;
    }
    return key.GetChildCount();
  };

  if (StaticPrefs::network_notify_checkForProxies()) {
    if (registryChildCount(u"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\"
                           "Parameters\\DnsConnections"_ns) > 0 ||
        registryChildCount(u"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\"
                           "Parameters\\DnsConnectionsProxies"_ns) > 0) {
      platformDNSIndications |= PROXY_DETECTED;
    }
  }

  if (StaticPrefs::network_notify_checkForNRPT()) {
    if (registryChildCount(u"SYSTEM\\CurrentControlSet\\Services\\Dnscache\\"
                           "Parameters\\DnsPolicyConfig"_ns) > 0 ||
        registryChildCount(u"SOFTWARE\\Policies\\Microsoft\\Windows NT\\"
                           "DNSClient\\DnsPolicyConfig"_ns) > 0) {
      platformDNSIndications |= NRPT_DETECTED;
    }
  }

  {
    MutexAutoLock lock(mMutex);
    mDnsSuffixList = std::move(dnsSuffixList);
    mDNSResolvers = std::move(resolvers);
    mPlatformDNSIndications = platformDNSIndications;
  }

  NotifyObservers(NS_DNS_SUFFIX_LIST_UPDATED_TOPIC, nullptr);

  calculateNetworkId();

  return ret;
}

/**
 * Checks the status of all network adapters.  If one is up and has a valid IP
 * address, sets mLinkUp to true.  Sets mStatusKnown to true if the link status
 * is definitive.
 */
void nsNotifyAddrListener::CheckLinkStatus(void) {
  DWORD ret;
  const char* event;
  bool prevLinkUp = mLinkUp;
  ULONG prevCsum = mIPInterfaceChecksum;

  LOG(("check status of all network adapters\n"));

  // The CheckAdaptersAddresses call is very expensive (~650 milliseconds),
  // so we don't want to call it synchronously. Instead, we just start up
  // assuming we have a network link, but we'll report that the status is
  // unknown.
  if (NS_IsMainThread()) {
    NS_WARNING(
        "CheckLinkStatus called on main thread! No check "
        "performed. Assuming link is up, status is unknown.");
    mLinkUp = true;

    if (!mStatusKnown) {
      event = NS_NETWORK_LINK_DATA_UNKNOWN;
    } else if (!prevLinkUp) {
      event = NS_NETWORK_LINK_DATA_UP;
    } else {
      // Known status and it was already UP
      event = nullptr;
    }

    if (event) {
      NotifyObservers(NS_NETWORK_LINK_TOPIC, event);
    }
  } else {
    ret = CheckAdaptersAddresses();
    if (ret != ERROR_SUCCESS) {
      mLinkUp = true;
    }

    if (mLinkUp && (prevCsum != mIPInterfaceChecksum)) {
      TimeDuration since = TimeStamp::Now() - mStartTime;

      // Network is online. Topology has changed. Always send CHANGED
      // before UP - if allowed to and having cooled down.
      if (StaticPrefs::network_notify_changed() &&
          (since.ToMilliseconds() > 2000)) {
        NetworkChanged();
      }
    }
    if (prevLinkUp != mLinkUp) {
      // UP/DOWN status changed, send appropriate UP/DOWN event
      NotifyObservers(NS_NETWORK_LINK_TOPIC, mLinkUp
                                                 ? NS_NETWORK_LINK_DATA_UP
                                                 : NS_NETWORK_LINK_DATA_DOWN);
    }
  }
}

// static
bool nsINetworkLinkService::HasNonLocalIPv6Address() {
  return sHasNonLocalIPv6;
}
