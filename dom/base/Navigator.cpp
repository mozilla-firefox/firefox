/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Needs to be first.
#include "Navigator.h"

#include "Geolocation.h"
#include "base/basictypes.h"
#include "mozilla/Components.h"
#include "mozilla/ContentBlockingNotifier.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_beacon.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BodyExtractor.h"
#include "mozilla/dom/FetchBinding.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/Serial.h"
#include "mozilla/glean/DomMediaMetrics.h"
#include "mozilla/glean/DomMetrics.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsIClassOfService.h"
#include "nsIContentPolicy.h"
#include "nsIHttpProtocolHandler.h"
#include "nsISupportsPriority.h"
#include "nsITimedChannel.h"
#include "nsIWebProtocolHandlerRegistrar.h"
#include "nsIXULAppInfo.h"
#include "nsMimeTypeArray.h"
#include "nsPluginArray.h"
#include "nsUnicharUtils.h"
#ifdef FUZZING
#  include "mozilla/StaticPrefs_fuzzing.h"
#endif
#include "BatteryManager.h"
#include "Connection.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Hal.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_pdfjs.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/dom/AudioSession.h"
#include "mozilla/dom/Clipboard.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/CredentialsContainer.h"
#include "mozilla/dom/Event.h"  // for Event
#include "mozilla/dom/GamepadServiceTest.h"
#include "mozilla/dom/LockManager.h"
#include "mozilla/dom/MIDIAccessManager.h"
#include "mozilla/dom/MIDIOptionsBinding.h"
#include "mozilla/dom/MediaCapabilities.h"
#include "mozilla/dom/MediaSession.h"
#include "mozilla/dom/ModelContext.h"
#include "mozilla/dom/NavigatorLogin.h"
#include "mozilla/dom/Permissions.h"
#include "mozilla/dom/PermissionsPolicyUtils.h"
#include "mozilla/dom/ServiceWorkerContainer.h"
#include "mozilla/dom/StorageManager.h"
#include "mozilla/dom/TCPSocket.h"
#include "mozilla/dom/URLSearchParams.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/VRDisplay.h"
#include "mozilla/dom/VRDisplayEvent.h"
#include "mozilla/dom/VRServiceTest.h"
#include "mozilla/dom/WakeLockJS.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/XRSystem.h"
#include "mozilla/dom/power/PowerManagerService.h"
#include "mozilla/dom/workerinternals/RuntimeService.h"
#include "nsComponentManagerUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsICookieManager.h"
#include "nsICookieService.h"
#include "nsIHttpChannel.h"
#include "nsIPermissionManager.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsRFPService.h"
#include "nsStringStream.h"
#ifdef ENABLE_WEBDRIVER
#  include "nsIMarionette.h"
#  include "nsIRemoteAgent.h"
#endif
#include "BrowserChild.h"
#include "MediaManager.h"
#include "ReferrerInfo.h"
#include "WidgetUtils.h"
#include "mozilla/PermissionDelegateHandler.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/MediaDevices.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/ipc/URIUtils.h"
#include "nsIDocShell.h"
#include "nsIExternalProtocolHandler.h"
#include "nsIScriptError.h"
#include "nsIUploadChannel2.h"
#include "nsJSUtils.h"
#include "nsStreamUtils.h"

#if defined(XP_WIN)
#  include "mozilla/WindowsVersion.h"
#endif

#include "AutoplayPolicy.h"
#include "mozilla/DetailedPromise.h"
#include "mozilla/EMEUtils.h"
#include "mozilla/dom/AudioContext.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/webgpu/Instance.h"

namespace mozilla::dom {

static const nsLiteralCString kVibrationPermissionType = "vibration"_ns;

Navigator::Navigator(nsPIDOMWindowInner* aWindow) : mWindow(aWindow) {}

Navigator::~Navigator() { Invalidate(); }

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Navigator)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(Navigator)
NS_IMPL_CYCLE_COLLECTING_RELEASE(Navigator)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(Navigator)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Navigator)
  tmp->Invalidate();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWindow)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSharePromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Navigator)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPlugins)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPermissions)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGeolocation)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSerial)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBatteryManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBatteryPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mConnection)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStorageManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCredentials)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMediaDevices)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mServiceWorkerContainer)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMediaCapabilities)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMediaSession)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAudioSession)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAddonManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWebGpu)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLocks)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLogin)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mModelContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mUserActivation)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWakeLock)

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWindow)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMediaKeySystemAccessManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGamepadServiceTest)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mVRGetDisplaysPromises)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mVRServiceTest)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSharePromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mXRSystem)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mClipboard)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

void Navigator::Invalidate() {
  // Don't clear mWindow here so we know we've got a non-null mWindow
  // until we're unlinked.

  mPlugins = nullptr;

  mPermissions = nullptr;

  if (mStorageManager) {
    mStorageManager->Shutdown();
    mStorageManager = nullptr;
  }

  // If there is a page transition, make sure delete the geolocation object.
  if (mGeolocation) {
    mGeolocation->Shutdown();
    mGeolocation = nullptr;
  }

  if (mSerial) {
    mSerial->Shutdown();
    mSerial = nullptr;
  }

  if (mBatteryManager) {
    mBatteryManager->Shutdown();
    mBatteryManager = nullptr;
  }

  mBatteryPromise = nullptr;

  if (mConnection) {
    mConnection->Shutdown();
    mConnection = nullptr;
  }

  mMediaDevices = nullptr;

  mServiceWorkerContainer = nullptr;

  if (mMediaKeySystemAccessManager) {
    mMediaKeySystemAccessManager->Shutdown();
    mMediaKeySystemAccessManager = nullptr;
  }

  if (mGamepadServiceTest) {
    mGamepadServiceTest->Shutdown();
    mGamepadServiceTest = nullptr;
  }

  mVRGetDisplaysPromises.Clear();

  if (mVRServiceTest) {
    mVRServiceTest->Shutdown();
    mVRServiceTest = nullptr;
  }

  if (mXRSystem) {
    mXRSystem->Shutdown();
    mXRSystem = nullptr;
  }

  mMediaCapabilities = nullptr;

  if (mMediaSession) {
    mMediaSession->Shutdown();
    mMediaSession = nullptr;
  }

  mAudioSession = nullptr;

  mAddonManager = nullptr;

  mWebGpu = nullptr;

  if (mLocks) {
    // Unloading a page does not immediately destruct the lock manager actor,
    // but we want to abort the lock requests as soon as possible. Explicitly
    // call Shutdown() to do that.
    mLocks->Shutdown();
    mLocks = nullptr;
  }

  mLogin = nullptr;

  mModelContext = nullptr;

  mUserActivation = nullptr;

  mSharePromise = nullptr;

  mWakeLock = nullptr;

  mClipboard = nullptr;
}

void Navigator::GetUserAgent(nsACString& aUserAgent, CallerType aCallerType,
                             ErrorResult& aRv) const {
  if (nsIDocShell* docshell = mWindow->GetDocShell()) {
    docshell->GetBrowsingContext()->GetCustomUserAgent(aUserAgent);
    if (!aUserAgent.IsEmpty()) {
      return;
    }
  }

  nsCOMPtr<Document> doc = mWindow->GetExtantDoc();
  nsresult rv = GetUserAgent(
      mWindow, doc, aCallerType == CallerType::System ? Some(false) : Nothing(),
      aUserAgent);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
  }
}

void Navigator::GetAppCodeName(nsAString& aAppCodeName, ErrorResult& aRv) {
  nsresult rv;

  nsCOMPtr<nsIHttpProtocolHandler> service(
      do_GetService(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "http", &rv));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  nsAutoCString appName;
  rv = service->GetAppName(appName);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  CopyASCIItoUTF16(appName, aAppCodeName);
}

void Navigator::GetAppVersion(nsAString& aAppVersion, CallerType aCallerType,
                              ErrorResult& aRv) const {
  nsCOMPtr<Document> doc = mWindow->GetExtantDoc();

  nsresult rv = GetAppVersion(
      aAppVersion, doc,
      /* aUsePrefOverriddenValue = */ aCallerType != CallerType::System);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
  }
}

void Navigator::GetAppName(nsAString& aAppName) const {
  aAppName.AssignLiteral("Netscape");
}

/**
 * Returns the value of Accept-Languages (HTTP header) as a nsTArray of
 * languages. The value is set in the preference by the user ("Content
 * Languages").
 *
 * "en", "en-US" and "i-cherokee" and "" are valid languages tokens.
 *
 * If there is no valid language, the value of getWebExposedLocales is
 * used to ensure that locale spoofing is honored and to reduce
 * fingerprinting.
 *
 * See RFC 7231, Section 9.7 "Browser Fingerprinting" and
 * RFC 2616, Section 15.1.4 "Privacy Issues Connected to Accept Headers"
 * for more detail.
 */
/* static */
void Navigator::GetAcceptLanguages(nsTArray<nsString>& aLanguages,
                                   const nsCString* aLanguageOverride) {
  MOZ_ASSERT(NS_IsMainThread());

  aLanguages.Clear();

  // E.g. "de-de, en-us,en".
  nsAutoCString acceptLang;
  if (aLanguageOverride) {
    acceptLang.Assign(aLanguageOverride->get());
  } else {
    intl::LocaleService::GetInstance()->GetAcceptLanguages(acceptLang);
  }

  // Split values on commas.
  for (nsDependentCSubstring lang :
       nsCCharSeparatedTokenizer(acceptLang, ',').ToRange()) {
    // Replace "_" with "-" to avoid POSIX/Windows "en_US" notation.
    // NOTE: we should probably rely on the pref being set correctly.
    if (lang.Length() > 2 && lang[2] == '_') {
      lang.Replace(2, 1, '-');
    }

    // Use uppercase for country part, e.g. "en-US", not "en-us", see BCP47
    // only uppercase 2-letter country codes, not "zh-Hant", "de-DE-x-goethe".
    // NOTE: we should probably rely on the pref being set correctly.
    if (lang.Length() > 2) {
      int32_t pos = 0;
      bool first = true;
      for (const nsACString& code :
           nsCCharSeparatedTokenizer(lang, '-').ToRange()) {
        if (code.Length() == 2 && !first) {
          nsAutoCString upper(code);
          ToUpperCase(upper);
          lang.Replace(pos, code.Length(), upper);
        }

        pos += code.Length() + 1;  // 1 is the separator
        first = false;
      }
    }

    aLanguages.AppendElement(NS_ConvertUTF8toUTF16(lang));
  }
  if (aLanguages.Length() == 0) {
    nsTArray<nsCString> locales;
    mozilla::intl::LocaleService::GetInstance()->GetWebExposedLocales(locales);
    aLanguages.AppendElement(NS_ConvertUTF8toUTF16(locales[0]));
  }
}

/**
 * Returns the first language from GetAcceptLanguages.
 *
 * Full details above in GetAcceptLanguages.
 */
void Navigator::GetLanguage(nsAString& aLanguage) {
  nsTArray<nsString> languages;
  GetLanguages(languages);
  MOZ_ASSERT(languages.Length() >= 1);
  aLanguage.Assign(languages[0]);
}

void Navigator::GetLanguages(nsTArray<nsString>& aLanguages) {
  BrowsingContext* bc = mWindow ? mWindow->GetBrowsingContext() : nullptr;
  if (bc) {
    const nsCString& languageOverride = bc->Top()->GetLanguageOverride();

    if (!languageOverride.IsEmpty()) {
      GetAcceptLanguages(aLanguages, &languageOverride);

      return;
    }
  }

  GetAcceptLanguages(aLanguages, nullptr);

  // The returned value is cached by the binding code. The window listens to the
  // accept languages change and will clear the cache when needed. It has to
  // take care of dispatching the DOM event already and the invalidation and the
  // event has to be timed correctly.
}

void Navigator::GetPlatform(nsAString& aPlatform, CallerType aCallerType,
                            ErrorResult& aRv) const {
  if (mWindow) {
    BrowsingContext* bc = mWindow->GetBrowsingContext();
    nsString customPlatform;
    if (bc) {
      bc->GetCustomPlatform(customPlatform);

      if (!customPlatform.IsEmpty()) {
        aPlatform = std::move(customPlatform);
        return;
      }
    }
  }

  nsCOMPtr<Document> doc = mWindow->GetExtantDoc();

  nsresult rv = GetPlatform(
      aPlatform, doc,
      /* aUsePrefOverriddenValue = */ aCallerType != CallerType::System);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
  }
}

void Navigator::GetOscpu(nsAString& aOSCPU, CallerType aCallerType,
                         ErrorResult& aRv) const {
  if (aCallerType != CallerType::System) {
    // If fingerprinting resistance is on, we will spoof this value. See
    // nsRFPService.h for details about spoofed values.
    if (nsContentUtils::ShouldResistFingerprinting(GetDocShell(),
                                                   RFPTarget::NavigatorOscpu)) {
      aOSCPU.AssignLiteral(SPOOFED_OSCPU);
      return;
    }

    nsAutoString override;
    nsresult rv = Preferences::GetString("general.oscpu.override", override);
    if (NS_SUCCEEDED(rv)) {
      aOSCPU = std::move(override);
      return;
    }
  }

  nsresult rv;
  nsCOMPtr<nsIHttpProtocolHandler> service(
      do_GetService(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "http", &rv));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  nsAutoCString oscpu;
  rv = service->GetOscpu(oscpu);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  CopyASCIItoUTF16(oscpu, aOSCPU);
}

void Navigator::GetVendor(nsAString& aVendor) { aVendor.Truncate(); }

void Navigator::GetVendorSub(nsAString& aVendorSub) { aVendorSub.Truncate(); }

void Navigator::GetProduct(nsAString& aProduct) {
  aProduct.AssignLiteral("Gecko");
}

void Navigator::GetProductSub(nsAString& aProductSub) {
  // Legacy build date hardcoded for backward compatibility (bug 776376)
  aProductSub.AssignLiteral(LEGACY_UA_GECKO_TRAIL);
}

nsMimeTypeArray* Navigator::GetMimeTypes(ErrorResult& aRv) {
  auto* plugins = GetPlugins(aRv);
  if (!plugins) {
    return nullptr;
  }

  return plugins->MimeTypeArray();
}

nsPluginArray* Navigator::GetPlugins(ErrorResult& aRv) {
  if (!mPlugins) {
    if (!mWindow) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }
    mPlugins = MakeRefPtr<nsPluginArray>(mWindow);
  }

  return mPlugins;
}

bool Navigator::PdfViewerEnabled() {
  return !StaticPrefs::pdfjs_disabled() ||
         nsContentUtils::ShouldResistFingerprinting(GetDocShell(),
                                                    RFPTarget::PdfjsSpoof);
}

Permissions* Navigator::GetPermissions(ErrorResult& aRv) {
  if (!mWindow) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  if (!mPermissions) {
    mPermissions = new Permissions(mWindow->AsGlobal());
  }

  return mPermissions;
}

StorageManager* Navigator::Storage() {
  MOZ_ASSERT(mWindow);

  if (!mStorageManager) {
    mStorageManager = new StorageManager(mWindow->AsGlobal());
  }

  return mStorageManager;
}

bool Navigator::CookieEnabled() {
  // Check whether an exception overrides the global cookie behavior
  // Note that the code for getting the URI here matches that in
  // nsHTMLDocument::SetCookie.
  if (!mWindow || !mWindow->GetDocShell()) {
    return nsICookieManager::GetCookieBehavior(false) !=
           nsICookieService::BEHAVIOR_REJECT;
  }

  nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(mWindow);
  uint32_t cookieBehavior = loadContext
                                ? nsICookieManager::GetCookieBehavior(
                                      loadContext->UsePrivateBrowsing())
                                : nsICookieManager::GetCookieBehavior(false);
  bool cookieEnabled = cookieBehavior != nsICookieService::BEHAVIOR_REJECT;

  nsCOMPtr<Document> doc = mWindow->GetExtantDoc();
  if (!doc) {
    return cookieEnabled;
  }

  uint32_t rejectedReason = 0;
  bool granted = false;
  nsresult rv = doc->NodePrincipal()->HasFirstpartyStorageAccess(
      mWindow, &rejectedReason, &granted);
  if (NS_FAILED(rv)) {
    // Not a content, so technically can't set cookies, but let's
    // just return the default value.
    return cookieEnabled;
  }

  // We should return true if the cookie is partitioned because the cookie is
  // still available in this case.
  if (!granted &&
      StoragePartitioningEnabled(rejectedReason, doc->CookieJarSettings())) {
    granted = true;
  }

  ContentBlockingNotifier::OnDecision(
      mWindow,
      granted ? ContentBlockingNotifier::BlockingDecision::eAllow
              : ContentBlockingNotifier::BlockingDecision::eBlock,
      rejectedReason);
  return granted;
}

bool Navigator::OnLine() {
  if (nsContentUtils::ShouldResistFingerprinting(
          GetDocShell(), RFPTarget::NetworkConnection)) {
    return true;
  }

  if (mWindow) {
    // Check if this tab is set to be offline.
    BrowsingContext* bc = mWindow->GetBrowsingContext();
    if (bc && bc->Top()->GetForceOffline()) {
      return false;
    }
  }
  // Return the default browser value
  return !NS_IsOffline();
}

void Navigator::GetBuildID(nsAString& aBuildID, CallerType aCallerType,
                           ErrorResult& aRv) const {
  if (aCallerType != CallerType::System) {
    // If fingerprinting resistance is on, we will spoof this value. See
    // nsRFPService.h for details about spoofed values.
    if (nsContentUtils::ShouldResistFingerprinting(
            GetDocShell(), RFPTarget::NavigatorBuildID)) {
      aBuildID.AssignLiteral(LEGACY_BUILD_ID);
      return;
    }

    nsAutoString override;
    nsresult rv = Preferences::GetString("general.buildID.override", override);
    if (NS_SUCCEEDED(rv)) {
      aBuildID = std::move(override);
      return;
    }

    nsAutoCString host;
    bool isHTTPS = false;
    if (mWindow) {
      nsCOMPtr<Document> doc = mWindow->GetDoc();
      if (doc) {
        nsIURI* uri = doc->GetDocumentURI();
        if (uri) {
          isHTTPS = uri->SchemeIs("https");
          if (isHTTPS) {
            MOZ_ALWAYS_SUCCEEDS(uri->GetHost(host));
          }
        }
      }
    }

    // Spoof the buildID on pages not loaded from "https://*.mozilla.org".
    if (!isHTTPS || !StringEndsWith(host, ".mozilla.org"_ns)) {
      aBuildID.AssignLiteral(LEGACY_BUILD_ID);
      return;
    }
  }

  nsCOMPtr<nsIXULAppInfo> appInfo =
      do_GetService("@mozilla.org/xre/app-info;1");
  if (!appInfo) {
    aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
    return;
  }

  nsAutoCString buildID;
  nsresult rv = appInfo->GetAppBuildID(buildID);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  aBuildID.Truncate();
  AppendASCIItoUTF16(buildID, aBuildID);
}

void Navigator::GetDoNotTrack(nsAString& aResult) {
  if (StaticPrefs::privacy_donottrackheader_enabled()) {
    aResult.AssignLiteral("1");
  } else {
    aResult.AssignLiteral("unspecified");
  }
}

bool Navigator::GlobalPrivacyControl() {
  bool gpcStatus = StaticPrefs::privacy_globalprivacycontrol_enabled();
  if (!gpcStatus) {
    nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(mWindow);
    gpcStatus = loadContext && loadContext->UsePrivateBrowsing() &&
                StaticPrefs::privacy_globalprivacycontrol_pbmode_enabled();
  }
  return gpcStatus;
}

uint64_t Navigator::HardwareConcurrency() {
  workerinternals::RuntimeService* rts =
      workerinternals::RuntimeService::GetOrCreateService();
  if (!rts) {
    return 1;
  }

  return rts->ClampedHardwareConcurrency(
      nsGlobalWindowInner::Cast(mWindow)->ShouldResistFingerprinting(
          RFPTarget::NavigatorHWConcurrency),
      nsGlobalWindowInner::Cast(mWindow)->ShouldResistFingerprinting(
          RFPTarget::NavigatorHWConcurrencyTiered));
}

namespace {

class VibrateWindowListener : public nsIDOMEventListener {
 public:
  VibrateWindowListener(nsPIDOMWindowInner* aWindow, Document* aDocument)
      : mWindow(do_GetWeakReference(aWindow)), mDocument(aDocument) {
    constexpr auto visibilitychange = u"visibilitychange"_ns;
    aDocument->AddSystemEventListener(visibilitychange, this, /* listener */
                                      true,                   /* use capture */
                                      false /* wants untrusted */);
  }

  void RemoveListener();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMEVENTLISTENER

 private:
  virtual ~VibrateWindowListener() = default;

  nsWeakPtr mWindow;
  WeakPtr<Document> mDocument;
};

NS_IMPL_ISUPPORTS(VibrateWindowListener, nsIDOMEventListener)

StaticRefPtr<VibrateWindowListener> gVibrateWindowListener;

static bool MayVibrate(Document* doc) {
  // Hidden documents cannot start or stop a vibration.
  return (doc && !doc->Hidden());
}

NS_IMETHODIMP
VibrateWindowListener::HandleEvent(Event* aEvent) {
  nsCOMPtr<Document> doc = do_QueryInterface(aEvent->GetTarget());

  if (!MayVibrate(doc)) {
    // It's important that we call CancelVibrate(), not Vibrate() with an
    // empty list, because Vibrate() will fail if we're no longer focused, but
    // CancelVibrate() will succeed, so long as nobody else has started a new
    // vibration pattern.
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryReferent(mWindow);
    hal::CancelVibrate(window);
    RemoveListener();
    gVibrateWindowListener = nullptr;
    // Careful: The line above might have deleted |this|!
  }

  return NS_OK;
}

void VibrateWindowListener::RemoveListener() {
  nsCOMPtr<Document> target(mDocument);
  if (!target) {
    return;
  }
  constexpr auto visibilitychange = u"visibilitychange"_ns;
  target->RemoveSystemEventListener(visibilitychange, this,
                                    true /* use capture */);
}

}  // namespace

void Navigator::SetVibrationPermission(bool aPermitted, bool aPersistent) {
  MOZ_ASSERT(NS_IsMainThread());

  nsTArray<uint32_t> pattern = std::move(mRequestedVibrationPattern);

  if (!mWindow) {
    return;
  }

  nsCOMPtr<Document> doc = mWindow->GetExtantDoc();

  if (!MayVibrate(doc)) {
    return;
  }

  if (aPermitted) {
    // Add a listener to cancel the vibration if the document becomes hidden,
    // and remove the old visibility listener, if there was one.
    if (!gVibrateWindowListener) {
      // If gVibrateWindowListener is null, this is the first time we've
      // vibrated, and we need to register a listener to clear
      // gVibrateWindowListener on shutdown.
      ClearOnShutdown(&gVibrateWindowListener);
    } else {
      gVibrateWindowListener->RemoveListener();
    }
    gVibrateWindowListener = new VibrateWindowListener(mWindow, doc);
    hal::Vibrate(pattern, mWindow);
  }

  if (aPersistent) {
    nsCOMPtr<nsIPermissionManager> permMgr =
        components::PermissionManager::Service();
    if (!permMgr) {
      return;
    }
    permMgr->AddFromPrincipal(doc->NodePrincipal(), kVibrationPermissionType,
                              aPermitted ? nsIPermissionManager::ALLOW_ACTION
                                         : nsIPermissionManager::DENY_ACTION,
                              nsIPermissionManager::EXPIRE_SESSION, 0);
  }
}

bool Navigator::Vibrate(uint32_t aDuration) {
  AutoTArray<uint32_t, 1> pattern;
  pattern.AppendElement(aDuration);
  return Vibrate(pattern);
}

nsTArray<uint32_t> SanitizeVibratePattern(const nsTArray<uint32_t>& aPattern) {
  nsTArray<uint32_t> pattern(aPattern.Clone());

  if (pattern.Length() > StaticPrefs::dom_vibrator_max_vibrate_list_len()) {
    pattern.SetLength(StaticPrefs::dom_vibrator_max_vibrate_list_len());
  }

  for (size_t i = 0; i < pattern.Length(); ++i) {
    pattern[i] =
        std::min(StaticPrefs::dom_vibrator_max_vibrate_ms(), pattern[i]);
  }

  return pattern;
}

bool Navigator::Vibrate(const nsTArray<uint32_t>& aPattern) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mWindow) {
    return false;
  }

  nsCOMPtr<Document> doc = mWindow->GetExtantDoc();

  if (!MayVibrate(doc)) {
    return false;
  }

  nsTArray<uint32_t> pattern = SanitizeVibratePattern(aPattern);

  mRequestedVibrationPattern = std::move(pattern);

  PermissionDelegateHandler* permissionHandler =
      doc->GetPermissionDelegateHandler();
  if (NS_WARN_IF(!permissionHandler)) {
    return false;
  }

  uint32_t permission = nsIPermissionManager::UNKNOWN_ACTION;

  permissionHandler->GetPermission(kVibrationPermissionType, &permission,
                                   false);

  if (permission == nsIPermissionManager::DENY_ACTION) {
    // Abort without observer service or on denied session permission.
    SetVibrationPermission(false /* permitted */, false /* persistent */);
    return false;
  }

  if (permission == nsIPermissionManager::ALLOW_ACTION ||
      mRequestedVibrationPattern.IsEmpty() ||
      (mRequestedVibrationPattern.Length() == 1 &&
       mRequestedVibrationPattern[0] == 0)) {
    // Always allow cancelling vibration and respect session permissions.
    SetVibrationPermission(true /* permitted */, false /* persistent */);
    return true;
  }

  // Request user permission.
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    return true;
  }

  obs->NotifyObservers(ToSupports(this), "Vibration:Request", nullptr);

  return true;
}

//*****************************************************************************
//  Pointer Events interface
//*****************************************************************************

uint32_t Navigator::MaxTouchPoints(CallerType aCallerType) {
  nsIDocShell* docshell = GetDocShell();
  BrowsingContext* bc = docshell ? docshell->GetBrowsingContext() : nullptr;

  // Responsive Design Mode overrides the maxTouchPoints property when
  // touch simulation is enabled.
  if (bc && bc->Top()->InRDMPane()) {
    return bc->Top()->GetMaxTouchPointsOverride();
  }

  // The maxTouchPoints is going to reveal the detail of users' hardware. So,
  // we will spoof it into 0 if fingerprinting resistance is on.
  if (aCallerType != CallerType::System &&
      nsContentUtils::ShouldResistFingerprinting(GetDocShell(),
                                                 RFPTarget::MaxTouchPoints)) {
    return SPOOFED_MAX_TOUCH_POINTS;
  }

  nsCOMPtr<nsIWidget> widget =
      widget::WidgetUtils::DOMWindowToWidget(mWindow->GetOuterWindow());

  NS_ENSURE_TRUE(widget, 0);
  uint32_t maxTouchPoints = widget->GetMaxTouchPoints();

  if (aCallerType != CallerType::System &&
      nsContentUtils::ShouldResistFingerprinting(
          GetDocShell(), RFPTarget::MaxTouchPointsCollapse)) {
    return nsRFPService::CollapseMaxTouchPoints(maxTouchPoints);
  }
  return maxTouchPoints;
}

//*****************************************************************************
//    Navigator::nsIDOMClientInformation
//*****************************************************************************

// This list should be kept up-to-date with the spec:
// https://html.spec.whatwg.org/multipage/system-state.html#custom-handlers
// If you change this list, please also update the copy in E10SUtils.sys.mjs.
static const char* const kSafeSchemes[] = {
    // clang-format off
    "bitcoin",
    "ftp",
    "ftps",
    "geo",
    "im",
    "irc",
    "ircs",
    "magnet",
    "mailto",
    "matrix",
    "mms",
    "news",
    "nntp",
    "openpgp4fpr",
    "sftp",
    "sip",
    "sms",
    "smsto",
    "ssh",
    "tel",
    "urn",
    "webcal",
    "wtai",
    "xmpp",
    // clang-format on
};

void Navigator::CheckProtocolHandlerAllowed(const nsAString& aScheme,
                                            nsIURI* aHandlerURI,
                                            nsIURI* aDocumentURI,
                                            ErrorResult& aRv) {
  auto raisePermissionDeniedHandler = [&] {
    nsAutoCString spec;
    aHandlerURI->GetSpec(spec);
    nsPrintfCString message("Permission denied to add %s as a protocol handler",
                            spec.get());
    aRv.ThrowSecurityError(message);
  };

  auto raisePermissionDeniedScheme = [&] {
    nsPrintfCString message(
        "Permission denied to add a protocol handler for %s",
        NS_ConvertUTF16toUTF8(aScheme).get());
    aRv.ThrowSecurityError(message);
  };

  if (!aDocumentURI || !aHandlerURI) {
    aRv.Throw(NS_ERROR_DOM_SYNTAX_ERR);
    return;
  }

  nsCString spec;
  aHandlerURI->GetSpec(spec);
  // If the uri doesn't contain '%s', it won't be a good handler - the %s
  // gets replaced with the handled URI.
  if (!FindInReadable("%s"_ns, spec)) {
    aRv.ThrowSyntaxError("Handler URI does not contain \"%s\".");
    return;
  }

  // For security reasons we reject non-http(s) urls (see bug 354316),
  nsAutoCString docScheme;
  nsAutoCString handlerScheme;
  aDocumentURI->GetScheme(docScheme);
  aHandlerURI->GetScheme(handlerScheme);
  if ((!docScheme.EqualsLiteral("https") && !docScheme.EqualsLiteral("http")) ||
      (!handlerScheme.EqualsLiteral("https") &&
       !handlerScheme.EqualsLiteral("http"))) {
    raisePermissionDeniedHandler();
    return;
  }

  // Should be same-origin:
  nsAutoCString handlerHost;
  aHandlerURI->GetHostPort(handlerHost);
  nsAutoCString documentHost;
  aDocumentURI->GetHostPort(documentHost);
  if (!handlerHost.Equals(documentHost) || !handlerScheme.Equals(docScheme)) {
    raisePermissionDeniedHandler();
    return;
  }

  // Having checked the handler URI, check the scheme:
  nsAutoCString scheme;
  ToLowerCase(NS_ConvertUTF16toUTF8(aScheme), scheme);
  if (StringBeginsWith(scheme, "web+"_ns)) {
    // Check for non-ascii
    nsReadingIterator<char> iter;
    nsReadingIterator<char> iterEnd;
    auto remainingScheme = Substring(scheme, 4 /* web+ */);
    remainingScheme.BeginReading(iter);
    remainingScheme.EndReading(iterEnd);
    // Scheme suffix must be non-empty
    if (iter == iterEnd) {
      raisePermissionDeniedScheme();
      return;
    }
    for (; iter != iterEnd; iter++) {
      if (*iter < 'a' || *iter > 'z') {
        raisePermissionDeniedScheme();
        return;
      }
    }
  } else {
    bool matches = false;
    for (const char* safeScheme : kSafeSchemes) {
      if (scheme.Equals(safeScheme)) {
        matches = true;
        break;
      }
    }
    if (!matches) {
      raisePermissionDeniedScheme();
      return;
    }
  }

  nsCOMPtr<nsIProtocolHandler> handler;
  nsCOMPtr<nsIIOService> io = components::IO::Service();
  if (NS_FAILED(
          io->GetProtocolHandler(scheme.get(), getter_AddRefs(handler)))) {
    raisePermissionDeniedScheme();
    return;
  }

  // check if we have prefs set saying not to add this.
  bool defaultExternal =
      Preferences::GetBool("network.protocol-handler.external-default");
  nsPrintfCString specificPref("network.protocol-handler.external.%s",
                               scheme.get());
  if (!Preferences::GetBool(specificPref.get(), defaultExternal)) {
    raisePermissionDeniedScheme();
    return;
  }

  // Check to make sure this isn't already handled internally (we don't
  // want to let them take over, say "chrome"). In theory, the checks above
  // should have already taken care of this.
  nsCOMPtr<nsIExternalProtocolHandler> externalHandler =
      do_QueryInterface(handler);
  MOZ_RELEASE_ASSERT(
      externalHandler,
      "We should never allow overriding a builtin protocol handler");
}

void Navigator::RegisterProtocolHandler(const nsAString& aScheme,
                                        const nsAString& aURI,
                                        ErrorResult& aRv) {
  if (!mWindow || !mWindow->GetOuterWindow() || !mWindow->GetDocShell() ||
      !mWindow->GetDoc()) {
    return;
  }
  nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(mWindow);
  if (loadContext->UsePrivateBrowsing()) {
    // If we're a private window, don't alert the user or webpage. We log to the
    // console so that web developers have some way to tell what's going wrong.
    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "DOM"_ns, mWindow->GetDoc(),
        PropertiesFile::DOM_PROPERTIES,
        "RegisterProtocolHandlerPrivateBrowsingWarning");
    return;
  }

  nsCOMPtr<Document> doc = mWindow->GetDoc();

  // Determine if doc is allowed to assign this handler
  nsIURI* docURI = doc->GetDocumentURIObject();
  nsCOMPtr<nsIURI> handlerURI;
  NS_NewURI(getter_AddRefs(handlerURI), NS_ConvertUTF16toUTF8(aURI),
            doc->GetDocumentCharacterSet(), docURI);
  CheckProtocolHandlerAllowed(aScheme, handlerURI, docURI, aRv);
  if (aRv.Failed()) {
    return;
  }

  if (WindowContext* wc = mWindow->GetWindowContext()) {
    nsLiteralCString frame =
        wc->IsTop() ? "top_level"_ns
        : CredentialsContainer::IsSameOriginWithAncestors(mWindow)
            ? "same_origin_iframe"_ns
            : "cross_origin_iframe"_ns;
    glean::dom::register_protocol_handler_calls
        .Get(frame, wc->HasValidTransientUserGestureActivation()
                        ? "activated"_ns
                        : "not_activated"_ns)
        .Add(1);
  }

  // Determine a title from the document URI.
  nsAutoCString docDisplayHostPort;
  docURI->GetDisplayHostPort(docDisplayHostPort);
  NS_ConvertASCIItoUTF16 title(docDisplayHostPort);

  if (XRE_IsContentProcess()) {
    nsAutoString scheme(aScheme);
    RefPtr<BrowserChild> browserChild = BrowserChild::GetFrom(mWindow);
    browserChild->SendRegisterProtocolHandler(scheme, handlerURI, title,
                                              docURI);
    return;
  }

  nsCOMPtr<nsIWebProtocolHandlerRegistrar> registrar =
      do_GetService(NS_WEBPROTOCOLHANDLERREGISTRAR_CONTRACTID);
  if (registrar) {
    aRv = registrar->RegisterProtocolHandler(aScheme, handlerURI, title, docURI,
                                             mWindow->GetOuterWindow());
  }
}

Geolocation* Navigator::GetGeolocation(ErrorResult& aRv) {
  if (mGeolocation) {
    return mGeolocation;
  }

  if (!mWindow || !mWindow->GetOuterWindow() || !mWindow->GetDocShell()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  mGeolocation = new Geolocation();
  if (NS_FAILED(mGeolocation->Init(mWindow))) {
    mGeolocation = nullptr;
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return mGeolocation;
}

dom::Serial* Navigator::GetSerial(ErrorResult& aRv) {
  if (mSerial) {
    return mSerial;
  }

  if (!mWindow) {
    aRv.ThrowInvalidStateError("Navigator no longer has an associated window");
    return nullptr;
  }

  mSerial = MakeRefPtr<dom::Serial>(mWindow);
  return mSerial;
}

class BeaconStreamListener final : public nsIStreamListener {
  ~BeaconStreamListener() = default;

 public:
  BeaconStreamListener() = default;

  void SetLoadGroup(nsILoadGroup* aLoadGroup) { mLoadGroup = aLoadGroup; }

  NS_DECL_ISUPPORTS
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIREQUESTOBSERVER

 private:
  nsCOMPtr<nsILoadGroup> mLoadGroup{};
};

NS_IMPL_ISUPPORTS(BeaconStreamListener, nsIStreamListener, nsIRequestObserver)

NS_IMETHODIMP
BeaconStreamListener::OnStartRequest(nsIRequest* aRequest) {
  // release the loadgroup first
  mLoadGroup = nullptr;

  return NS_ERROR_ABORT;
}

NS_IMETHODIMP
BeaconStreamListener::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  return NS_OK;
}

NS_IMETHODIMP
BeaconStreamListener::OnDataAvailable(nsIRequest* aRequest,
                                      nsIInputStream* inStr,
                                      uint64_t sourceOffset, uint32_t count) {
  MOZ_ASSERT(false);
  return NS_OK;
}

bool Navigator::SendBeacon(const nsAString& aUrl,
                           const Nullable<fetch::BodyInit>& aData,
                           ErrorResult& aRv) {
  if (aData.IsNull()) {
    return SendBeaconInternal(aUrl, nullptr, eBeaconTypeOther, aRv);
  }

  // A beacon request has keepalive set, and extracting a body from a
  // ReadableStream with keepalive throws.
  // https://fetch.spec.whatwg.org/#concept-bodyinit-extract step 10
  if (StaticPrefs::dom_fetch_streaming_upload()) {
    if (aData.Value().IsReadableStream()) {
      aRv.ThrowTypeError("sendBeacon cannot send a ReadableStream body");
      return false;
    }
  } else if (aData.Value().IsReadableStream()) {
    // Preserve previous behaviour when the pref is false.
    nsAutoString stringified(u"[object ReadableStream]"_ns);
    BodyExtractor<const nsAString> body(&stringified);
    return SendBeaconInternal(aUrl, &body, eBeaconTypeOther, aRv);
  }

  if (aData.Value().IsArrayBuffer()) {
    BodyExtractor<const ArrayBuffer> body(&aData.Value().GetAsArrayBuffer());
    return SendBeaconInternal(aUrl, &body, eBeaconTypeArrayBuffer, aRv);
  }

  if (aData.Value().IsArrayBufferView()) {
    BodyExtractor<const ArrayBufferView> body(
        &aData.Value().GetAsArrayBufferView());
    return SendBeaconInternal(aUrl, &body, eBeaconTypeArrayBuffer, aRv);
  }

  if (aData.Value().IsBlob()) {
    BodyExtractor<const Blob> body(&aData.Value().GetAsBlob());
    return SendBeaconInternal(aUrl, &body, eBeaconTypeBlob, aRv);
  }

  if (aData.Value().IsFormData()) {
    BodyExtractor<const FormData> body(&aData.Value().GetAsFormData());
    return SendBeaconInternal(aUrl, &body, eBeaconTypeOther, aRv);
  }

  if (aData.Value().IsUSVString()) {
    BodyExtractor<const nsAString> body(&aData.Value().GetAsUSVString());
    return SendBeaconInternal(aUrl, &body, eBeaconTypeOther, aRv);
  }

  if (aData.Value().IsURLSearchParams()) {
    BodyExtractor<const URLSearchParams> body(
        &aData.Value().GetAsURLSearchParams());
    return SendBeaconInternal(aUrl, &body, eBeaconTypeOther, aRv);
  }

  MOZ_CRASH("Invalid data type.");
  return false;
}

bool Navigator::SendBeaconInternal(const nsAString& aUrl,
                                   BodyExtractorBase* aBody, BeaconType aType,
                                   ErrorResult& aRv) {
  if (!mWindow) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return false;
  }

  nsCOMPtr<Document> doc = mWindow->GetDoc();
  if (!doc) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return false;
  }

  nsIURI* documentURI = doc->GetDocumentURI();
  if (!documentURI) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return false;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = nsContentUtils::NewURIWithDocumentCharset(
      getter_AddRefs(uri), aUrl, doc, doc->GetDocBaseURI());
  if (NS_FAILED(rv)) {
    aRv.ThrowTypeError<MSG_INVALID_URL>(NS_ConvertUTF16toUTF8(aUrl));
    return false;
  }

  // Spec disallows any schemes save for HTTP/HTTPs
  if (!net::SchemeIsHttpOrHttps(uri)) {
    aRv.ThrowTypeError<MSG_INVALID_URL_SCHEME>("Beacon",
                                               uri->GetSpecOrDefault());
    return false;
  }

  nsCOMPtr<nsIInputStream> in;
  nsAutoCString contentTypeWithCharset;
  nsAutoCString charset;
  uint64_t length = 0;
  if (aBody) {
    aRv = aBody->GetAsStream(getter_AddRefs(in), &length,
                             contentTypeWithCharset, charset);
    if (NS_WARN_IF(aRv.Failed())) {
      return false;
    }
  }

  nsSecurityFlags securityFlags = nsILoadInfo::SEC_COOKIES_INCLUDE;
  // Ensure that only streams with content types that are safelisted ignore CORS
  // rules
  if (aBody && !contentTypeWithCharset.IsVoid() &&
      !nsContentUtils::IsCORSSafelistedRequestHeader("content-type"_ns,
                                                     contentTypeWithCharset)) {
    securityFlags |= nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT;
  } else {
    securityFlags |= nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT;
  }

  // Bail out only after the spec-mandated validation above, so that a disabled
  // beacon is indistinguishable from a successful one to content.
  if (!StaticPrefs::beacon_enabled()) {
    return true;
  }

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel), uri, doc, securityFlags,
                     nsIContentPolicy::TYPE_BEACON);

  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
    return false;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(channel);
  if (!httpChannel) {
    // Beacon spec only supports HTTP requests at this time
    aRv.Throw(NS_ERROR_DOM_BAD_URI);
    return false;
  }

  auto referrerInfo = MakeRefPtr<ReferrerInfo>(*doc);
  rv = httpChannel->SetReferrerInfoWithoutClone(referrerInfo);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  if (aBody) {
    nsCOMPtr<nsIUploadChannel2> uploadChannel = do_QueryInterface(channel);
    if (!uploadChannel) {
      aRv.Throw(NS_ERROR_FAILURE);
      return false;
    }

    uploadChannel->ExplicitSetUploadStream(in, contentTypeWithCharset, length,
                                           "POST"_ns);
  } else {
    rv = httpChannel->SetRequestMethod("POST"_ns);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  nsCOMPtr<nsISupportsPriority> p = do_QueryInterface(channel);
  if (p) {
    p->SetPriority(nsISupportsPriority::PRIORITY_LOWEST);
  }

  nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(channel));
  if (cos) {
    cos->AddClassFlags(nsIClassOfService::Background);
  }

  if (nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(channel)) {
    timedChannel->SetInitiatorType(u"beacon"_ns);
  }

  // The channel needs to have a loadgroup associated with it, so that we can
  // cancel the channel and any redirected channels it may create.
  nsCOMPtr<nsILoadGroup> loadGroup = do_CreateInstance(NS_LOADGROUP_CONTRACTID);
  nsCOMPtr<nsIInterfaceRequestor> callbacks =
      do_QueryInterface(mWindow->GetDocShell());
  loadGroup->SetNotificationCallbacks(callbacks);
  channel->SetLoadGroup(loadGroup);

  RefPtr<BeaconStreamListener> beaconListener = new BeaconStreamListener();
  rv = channel->AsyncOpen(beaconListener);
  // do not throw if security checks fail within asyncOpen
  NS_ENSURE_SUCCESS(rv, false);

  // make the beaconListener hold a strong reference to the loadgroup
  // which is released in ::OnStartRequest
  beaconListener->SetLoadGroup(loadGroup);

  return true;
}

MediaDevices* Navigator::GetMediaDevices(ErrorResult& aRv) {
  if (!mMediaDevices) {
    if (!mWindow || !mWindow->GetOuterWindow() ||
        mWindow->GetOuterWindow()->GetCurrentInnerWindow() != mWindow) {
      aRv.Throw(NS_ERROR_NOT_AVAILABLE);
      return nullptr;
    }
    mMediaDevices = new MediaDevices(mWindow);
  }
  return mMediaDevices;
}

void Navigator::MozGetUserMedia(const MediaStreamConstraints& aConstraints,
                                NavigatorUserMediaSuccessCallback& aOnSuccess,
                                NavigatorUserMediaErrorCallback& aOnError,
                                CallerType aCallerType, ErrorResult& aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mWindow || !mWindow->IsFullyActive()) {
    aRv.ThrowInvalidStateError("The document is not fully active.");
    return;
  }
  GetMediaDevices(aRv);
  if (aRv.Failed()) {
    return;
  }
  MOZ_ASSERT(mMediaDevices);
  RefPtr<MediaManager::StreamPromise> sp;
  if (!MediaManager::IsOn(aConstraints.mVideo) &&
      !MediaManager::IsOn(aConstraints.mAudio)) {
    sp = MediaManager::StreamPromise::CreateAndReject(
        MakeRefPtr<MediaMgrError>(MediaMgrError::Name::TypeError,
                                  "audio and/or video is required"),
        __func__);
  } else {
    sp = mMediaDevices->GetUserMedia(mWindow, aConstraints, aCallerType);
  }
  RefPtr<NavigatorUserMediaSuccessCallback> onsuccess(&aOnSuccess);
  RefPtr<NavigatorUserMediaErrorCallback> onerror(&aOnError);

  nsWeakPtr weakWindow = nsWeakPtr(do_GetWeakReference(mWindow));
  sp->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [weakWindow, onsuccess = std::move(onsuccess)](
          const RefPtr<DOMMediaStream>& aStream) MOZ_CAN_RUN_SCRIPT {
        nsCOMPtr<nsPIDOMWindowInner> window = do_QueryReferent(weakWindow);
        if (!window || !window->GetOuterWindow() ||
            window->GetOuterWindow()->GetCurrentInnerWindow() != window) {
          return;  // Leave Promise pending after navigation by design.
        }
        MediaManager::CallOnSuccess(*onsuccess, *aStream);
      },
      [weakWindow, onerror = std::move(onerror)](
          const RefPtr<MediaMgrError>& aError) MOZ_CAN_RUN_SCRIPT {
        nsCOMPtr<nsPIDOMWindowInner> window = do_QueryReferent(weakWindow);
        if (!window || !window->GetOuterWindow() ||
            window->GetOuterWindow()->GetCurrentInnerWindow() != window) {
          return;  // Leave Promise pending after navigation by design.
        }
        auto error = MakeRefPtr<MediaStreamError>(window, *aError);
        MediaManager::CallOnError(*onerror, *error);
      });
}

//*****************************************************************************
//    Navigator::nsINavigatorBattery
//*****************************************************************************

Promise* Navigator::GetBattery(ErrorResult& aRv) {
  if (mBatteryPromise) {
    return mBatteryPromise;
  }

  if (!mWindow || !mWindow->GetDocShell()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  RefPtr<Promise> batteryPromise = Promise::Create(mWindow->AsGlobal(), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  mBatteryPromise = batteryPromise;

  if (!mBatteryManager) {
    mBatteryManager = new battery::BatteryManager(mWindow);
    mBatteryManager->Init();
  }

  mBatteryPromise->MaybeResolve(mBatteryManager);

  return mBatteryPromise;
}

//*****************************************************************************
//    Navigator::Share() - Web Share API
//*****************************************************************************

already_AddRefed<Promise> Navigator::Share(const ShareData& aData,
                                           ErrorResult& aRv) {
  if (!mWindow || !mWindow->IsFullyActive()) {
    aRv.ThrowInvalidStateError("The document is not fully active.");
    return nullptr;
  }

  if (NS_WARN_IF(!mWindow->GetDocShell() || !mWindow->GetExtantDoc())) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  if (!PermissionsPolicyUtils::IsFeatureAllowed(mWindow->GetExtantDoc(),
                                                u"web-share"_ns)) {
    aRv.ThrowNotAllowedError(
        "Document's Permissions Policy does not allow calling "
        "share() from this context.");
    return nullptr;
  }

  if (mSharePromise) {
    NS_WARNING("Only one share picker at a time per navigator instance");
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  // null checked above
  Document* doc = mWindow->GetExtantDoc();

  if (StaticPrefs::dom_webshare_requireinteraction() &&
      !doc->ConsumeTransientUserGestureActivation()) {
    aRv.ThrowNotAllowedError(
        "User activation was already consumed "
        "or share() was not activated by a user gesture.");
    return nullptr;
  }

  ValidateShareData(aData, aRv);

  if (aRv.Failed()) {
    return nullptr;
  }

  // TODO: Process file member, which we don't currently support.

  // If data's url member is present, try to resolve it...
  nsCOMPtr<nsIURI> url;
  if (aData.mUrl.WasPassed()) {
    auto result = doc->ResolveWithBaseURI(aData.mUrl.Value());
    url = result.unwrap();
    MOZ_ASSERT(url);
  }

  // Process the title member...
  nsCString title;
  if (aData.mTitle.WasPassed()) {
    title.Assign(NS_ConvertUTF16toUTF8(aData.mTitle.Value()));
  } else {
    title.SetIsVoid(true);
  }

  // Process the text member...
  nsCString text;
  if (aData.mText.WasPassed()) {
    text.Assign(NS_ConvertUTF16toUTF8(aData.mText.Value()));
  } else {
    text.SetIsVoid(true);
  }

  // Let mSharePromise be a new promise.
  mSharePromise = Promise::Create(mWindow->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  IPCWebShareData data(title, text, url);
  auto wgc = mWindow->GetWindowGlobalChild();
  if (!wgc) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  // Do the share
  wgc->SendShare(data)->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr{this}](
          PWindowGlobalChild::SharePromise::ResolveOrRejectValue&& aResult) {
        if (!self->mSharePromise) {
          // Navigator is deactivated, no one to report to.
          return;
        }
        if (aResult.IsResolve()) {
          if (NS_SUCCEEDED(aResult.ResolveValue())) {
            self->mSharePromise->MaybeResolveWithUndefined();
          } else {
            self->mSharePromise->MaybeReject(aResult.ResolveValue());
          }
        }
        {
          // IPC died
          self->mSharePromise->MaybeReject(NS_BINDING_ABORTED);
        }
        self->mSharePromise = nullptr;
      });
  return do_AddRef(mSharePromise);
}

//*****************************************************************************
//    Navigator::CanShare() - Web Share API
//*****************************************************************************
bool Navigator::CanShare(const ShareData& aData) {
  if (!mWindow || !mWindow->IsFullyActive()) {
    return false;
  }

  if (!PermissionsPolicyUtils::IsFeatureAllowed(mWindow->GetExtantDoc(),
                                                u"web-share"_ns)) {
    return false;
  }

  IgnoredErrorResult rv;
  ValidateShareData(aData, rv);
  return !rv.Failed();
}

void Navigator::ValidateShareData(const ShareData& aData, ErrorResult& aRv) {
  // TODO: remove this check when we support files share.
  if (aData.mFiles.WasPassed() && !aData.mFiles.Value().IsEmpty()) {
    aRv.ThrowTypeError("Passing files is currently not supported.");
    return;
  }

  bool titleTextOrUrlPassed = aData.mTitle.WasPassed() ||
                              aData.mText.WasPassed() || aData.mUrl.WasPassed();

  // At least one member must be present.
  if (!titleTextOrUrlPassed) {
    aRv.ThrowTypeError(
        "Must have a title, text, or url member in the ShareData dictionary");
    return;
  }

  // If data's url member is present, try to resolve it...
  nsCOMPtr<nsIURI> url;
  if (aData.mUrl.WasPassed()) {
    Document* doc = mWindow->GetExtantDoc();
    Result<OwningNonNull<nsIURI>, nsresult> result =
        doc->ResolveWithBaseURI(aData.mUrl.Value());
    if (NS_WARN_IF(result.isErr())) {
      aRv.ThrowTypeError<MSG_INVALID_URL>(
          NS_ConvertUTF16toUTF8(aData.mUrl.Value()));
      return;
    }
    url = result.unwrap();
    // Check that we only share loadable URLs (e.g., http/https).
    // we also exclude blobs, as it doesn't make sense to share those outside
    // the context of the browser.
    const uint32_t flags =
        nsIScriptSecurityManager::DISALLOW_INHERIT_PRINCIPAL |
        nsIScriptSecurityManager::DISALLOW_SCRIPT;
    if (NS_FAILED(
            nsContentUtils::GetSecurityManager()->CheckLoadURIWithPrincipal(
                doc->NodePrincipal(), url, flags, doc->InnerWindowID())) ||
        url->SchemeIs("blob")) {
      aRv.ThrowTypeError<MSG_INVALID_URL_SCHEME>("Share",
                                                 url->GetSpecOrDefault());
      return;
    }
  }
}

static bool ShouldResistFingerprinting(const Document* aDoc,
                                       RFPTarget aTarget) {
  return aDoc ? aDoc->ShouldResistFingerprinting(aTarget)
              : nsContentUtils::ShouldResistFingerprinting("Fallback", aTarget);
}

already_AddRefed<LegacyMozTCPSocket> Navigator::MozTCPSocket() {
  RefPtr<LegacyMozTCPSocket> socket = new LegacyMozTCPSocket(GetWindow());
  return socket.forget();
}

void Navigator::GetGamepads(nsTArray<RefPtr<Gamepad>>& aGamepads,
                            ErrorResult& aRv) {
  if (!mWindow || !mWindow->IsFullyActive()) {
    return;
  }
  NS_ENSURE_TRUE_VOID(mWindow->GetDocShell());
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);

  if (!PermissionsPolicyUtils::IsFeatureAllowed(win->GetExtantDoc(),
                                                u"gamepad"_ns)) {
    aRv.ThrowSecurityError(
        "Document's Permission Policy does not allow calling "
        "getGamepads() from this context.");
    return;
  }

  win->SetHasGamepadEventListener(true);
  win->GetGamepads(aGamepads);
}

GamepadServiceTest* Navigator::RequestGamepadServiceTest(ErrorResult& aRv) {
#ifdef FUZZING
  if (!StaticPrefs::fuzzing_enabled()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }
#else
  if (!xpc::IsInAutomation()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }
#endif

  if (!mGamepadServiceTest) {
    mGamepadServiceTest = GamepadServiceTest::CreateTestService(mWindow);
  }
  return mGamepadServiceTest;
}

already_AddRefed<Promise> Navigator::RequestAllGamepads(ErrorResult& aRv) {
  if (!mWindow || !mWindow->IsFullyActive()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);

  // We need to set the flag to trigger the parent process to start monitoring
  // gamepads. Otherwise, we cannot get any gamepad information.
  win->SetHasGamepadEventListener(true);
  return win->RequestAllGamepads(aRv);
}

already_AddRefed<Promise> Navigator::GetVRDisplays(ErrorResult& aRv) {
  if (!mWindow || !mWindow->GetDocShell() || !mWindow->GetExtantDoc()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  if (!PermissionsPolicyUtils::IsFeatureAllowed(mWindow->GetExtantDoc(),
                                                u"vr"_ns)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return nullptr;
  }

  RefPtr<Promise> p = Promise::Create(mWindow->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  RefPtr<BrowserChild> browser(BrowserChild::GetFrom(mWindow));
  if (!browser) {
    MOZ_ASSERT(XRE_IsParentProcess());
    FinishGetVRDisplays(true, p);
  } else {
    RefPtr<Navigator> self(this);
    int browserID = browser->ChromeOuterWindowID();

    browser->SendIsWindowSupportingWebVR(browserID)->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self, p](bool isSupported) {
          self->FinishGetVRDisplays(isSupported, p);
        },
        [p](const mozilla::ipc::ResponseRejectReason) {
          p->MaybeRejectWithTypeError("Unable to start display enumeration");
        });
  }

  return p.forget();
}

void Navigator::FinishGetVRDisplays(bool isWebVRSupportedInwindow, Promise* p) {
  if (!isWebVRSupportedInwindow) {
    // WebVR in this window is not supported, so resolve the promise
    // with no displays available
    nsTArray<RefPtr<VRDisplay>> vrDisplaysEmpty;
    p->MaybeResolve(vrDisplaysEmpty);
    return;
  }

  // Since FinishGetVRDisplays can be called asynchronously after an IPC
  // response, it's possible that the Window can be torn down before this
  // call. In that case, the Window's cyclic references to VR objects are
  // also torn down and should not be recreated via
  // NotifyHasXRSession.
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);
  if (win->IsDying()) {
    // The Window has been torn down, so there is no further work that can
    // be done.
    p->MaybeRejectWithTypeError(
        "Unable to return VRDisplays for a closed window.");
    return;
  }

  mVRGetDisplaysPromises.AppendElement(p);
  win->RequestXRPermission();
}

void Navigator::OnXRPermissionRequestAllow() {
  // The permission request that results in this callback could have
  // been instantiated by WebVR, WebXR, or both.
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);
  bool usingWebXR = false;

  if (mXRSystem) {
    usingWebXR = mXRSystem->OnXRPermissionRequestAllow();
  }

  bool rejectWebVR = true;
  // If WebVR and WebXR both requested permission, only grant it to
  // WebXR, which takes priority.
  if (!usingWebXR) {
    // We pass mWindow's id to RefreshVRDisplays, so NotifyVRDisplaysUpdated
    // will be called asynchronously, resolving the promises in
    // mVRGetDisplaysPromises.
    rejectWebVR = !VRDisplay::RefreshVRDisplays(win->WindowID());
  }
  // Even if WebXR took priority, reject requests for WebVR in case they were
  // made simultaneously and coelesced into a single permission prompt.
  if (rejectWebVR) {
    for (auto& p : mVRGetDisplaysPromises) {
      // Failed to refresh, reject the promise now
      p->MaybeRejectWithTypeError("Failed to find attached VR displays.");
    }
    mVRGetDisplaysPromises.Clear();
  }
}

void Navigator::OnXRPermissionRequestCancel() {
  if (mXRSystem) {
    mXRSystem->OnXRPermissionRequestCancel();
  }

  nsTArray<RefPtr<VRDisplay>> vrDisplays;
  for (auto& p : mVRGetDisplaysPromises) {
    // Resolve the promise with no vr displays when
    // the user blocks access.
    p->MaybeResolve(vrDisplays);
  }
  mVRGetDisplaysPromises.Clear();
}

void Navigator::GetActiveVRDisplays(
    nsTArray<RefPtr<VRDisplay>>& aDisplays) const {
  /**
   * Get only the active VR displays.
   * GetActiveVRDisplays should only enumerate displays that
   * are already active without causing any other hardware to be
   * activated.
   * We must not call nsGlobalWindowInner::NotifyHasXRSession here,
   * as that would cause enumeration and activation of other VR hardware.
   * Activating VR hardware is intrusive to the end user, as it may
   * involve physically powering on devices that the user did not
   * intend to use.
   */
  if (!mWindow || !mWindow->GetDocShell()) {
    return;
  }
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);
  nsTArray<RefPtr<VRDisplay>> displays;
  if (win->UpdateVRDisplays(displays)) {
    for (auto display : displays) {
      if (display->IsPresenting()) {
        aDisplays.AppendElement(display);
      }
    }
  }
}

void Navigator::NotifyVRDisplaysUpdated() {
  // Synchronize the VR devices and resolve the promises in
  // mVRGetDisplaysPromises
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);

  nsTArray<RefPtr<VRDisplay>> vrDisplays;
  if (win->UpdateVRDisplays(vrDisplays)) {
    for (auto p : mVRGetDisplaysPromises) {
      p->MaybeResolve(vrDisplays);
    }
  } else {
    for (auto p : mVRGetDisplaysPromises) {
      p->MaybeReject(NS_ERROR_FAILURE);
    }
  }
  mVRGetDisplaysPromises.Clear();
}

void Navigator::NotifyActiveVRDisplaysChanged() {
  Navigator_Binding::ClearCachedActiveVRDisplaysValue(this);
}

VRServiceTest* Navigator::RequestVRServiceTest(ErrorResult& aRv) {
  if (!xpc::IsInAutomation()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  // Ensure that the Mock VR devices are not released prematurely
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);
  win->NotifyHasXRSession();

  if (!mVRServiceTest) {
    mVRServiceTest = VRServiceTest::CreateTestService(mWindow);
  }
  return mVRServiceTest;
}

XRSystem* Navigator::GetXr(ErrorResult& aRv) {
  if (!mWindow) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }
  if (!mXRSystem) {
    mXRSystem = XRSystem::Create(mWindow);
  }
  return mXRSystem;
}

bool Navigator::IsWebVRContentDetected() const {
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);
  return win->IsVRContentDetected();
}

bool Navigator::IsWebVRContentPresenting() const {
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);
  return win->IsVRContentPresenting();
}

void Navigator::RequestVRPresentation(VRDisplay& aDisplay) {
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(mWindow);
  win->DispatchVRDisplayActivate(aDisplay.DisplayId(),
                                 VRDisplayEventReason::Requested);
}

already_AddRefed<Promise> Navigator::RequestMIDIAccess(
    const MIDIOptions& aOptions, ErrorResult& aRv) {
  if (!mWindow) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }
  MIDIAccessManager* accessMgr = MIDIAccessManager::Get();
  return accessMgr->RequestMIDIAccess(mWindow, aOptions, aRv);
}

network::Connection* Navigator::GetConnection(ErrorResult& aRv) {
  if (!mConnection) {
    if (!mWindow) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }
    mConnection = network::Connection::CreateForWindow(
        mWindow, nsGlobalWindowInner::Cast(mWindow)->ShouldResistFingerprinting(
                     RFPTarget::NetworkConnection));
  }

  return mConnection;
}

already_AddRefed<ServiceWorkerContainer> Navigator::ServiceWorker() {
  MOZ_ASSERT(mWindow);

  if (!mServiceWorkerContainer) {
    mServiceWorkerContainer =
        ServiceWorkerContainer::Create(mWindow->AsGlobal());
  }

  RefPtr<ServiceWorkerContainer> ref = mServiceWorkerContainer;
  return ref.forget();
}

already_AddRefed<ServiceWorkerContainer> Navigator::ServiceWorkerJS() {
  if (mWindow->AsGlobal()->GetStorageAccess() ==
      StorageAccess::ePrivateBrowsing) {
    SetUseCounter(mWindow->AsGlobal()->GetGlobalJSObject(),
                  eUseCounter_custom_PrivateBrowsingNavigatorServiceWorker);
  }

  return ServiceWorker();
}

size_t Navigator::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);

  // TODO: add SizeOfIncludingThis() to nsMimeTypeArray, bug 674113.
  // TODO: add SizeOfIncludingThis() to nsPluginArray, bug 674114.
  // TODO: add SizeOfIncludingThis() to Geolocation, bug 674115.
  // TODO: add SizeOfIncludingThis() to DesktopNotificationCenter, bug 674116.

  return n;
}

void Navigator::OnNavigation() {
  if (!mWindow) {
    return;
  }

  // If MediaManager is open let it inform any live streams or pending callbacks
  MediaManager* manager = MediaManager::GetIfExists();
  if (manager) {
    manager->OnNavigation(mWindow->WindowID());
  }
}

JSObject* Navigator::WrapObject(JSContext* cx,
                                JS::Handle<JSObject*> aGivenProto) {
  return Navigator_Binding::Wrap(cx, this, aGivenProto);
}

/* static */
bool Navigator::HasUserMediaSupport(JSContext* cx, JSObject* obj) {
  // Make enabling peerconnection enable getUserMedia() as well.
  // Emulate [SecureContext] unless media.devices.insecure.enabled=true
  return (StaticPrefs::media_navigator_enabled() ||
          StaticPrefs::media_peerconnection_enabled()) &&
         (IsSecureContextOrObjectIsFromSecureContext(cx, obj) ||
          StaticPrefs::media_devices_insecure_enabled());
}

/* static */
bool Navigator::MozGetUserMediaSupport(JSContext* aCx, JSObject* aObj) {
  return StaticPrefs::media_navigator_mozgetusermedia_enabled() &&
         Navigator::HasUserMediaSupport(aCx, aObj);
}

/* static */
bool Navigator::HasShareSupport(JSContext* cx, JSObject* obj) {
  if (!StaticPrefs::dom_webshare_enabled()) {
    return false;
  }
#if defined(XP_WIN) && !defined(__MINGW32__)
  // The first public build that supports ShareCanceled API
  return IsWindows10BuildOrLater(18956);
#else
  return true;
#endif
}

/* static */
bool Navigator::HasMidiSupport(JSContext* cx, JSObject* obj) {
  nsIPrincipal* principal = nsContentUtils::SubjectPrincipal(cx);

  // Enable on secure contexts but exclude file schemes.
  return StaticPrefs::dom_webmidi_enabled() &&
         IsSecureContextOrObjectIsFromSecureContext(cx, obj) &&
         !principal->SchemeIs("file");
}

/* static */
already_AddRefed<nsPIDOMWindowInner> Navigator::GetWindowFromGlobal(
    JSObject* aGlobal) {
  nsCOMPtr<nsPIDOMWindowInner> win = xpc::WindowOrNull(aGlobal);
  return win.forget();
}

void Navigator::ClearPlatformCache() {
  Navigator_Binding::ClearCachedPlatformValue(this);
}

void Navigator::ClearLanguageCache() {
  Navigator_Binding::ClearCachedLanguageValue(this);
  Navigator_Binding::ClearCachedLanguagesValue(this);
}

nsresult Navigator::GetPlatform(nsAString& aPlatform, Document* aCallerDoc,
                                bool aUsePrefOverriddenValue) {
  MOZ_ASSERT(NS_IsMainThread());

  // navigator.platform is the same for default and spoofed values. The
  // "general.platform.override" pref should override the default platform,
  // but the spoofed platform should override the pref.
  if (aUsePrefOverriddenValue &&
      !ShouldResistFingerprinting(aCallerDoc, RFPTarget::NavigatorPlatform)) {
    nsAutoString override;
    nsresult rv =
        mozilla::Preferences::GetString("general.platform.override", override);

    if (NS_SUCCEEDED(rv)) {
      aPlatform = std::move(override);
      return NS_OK;
    }
  }

#if defined(WIN32)
  aPlatform.AssignLiteral("Win32");
#elif defined(XP_MACOSX)
  // Always return "MacIntel", even on ARM64 macOS like Safari does.
  aPlatform.AssignLiteral("MacIntel");
#elif defined(ANDROID)
  aPlatform.AssignLiteral("Linux armv81");
#else
  aPlatform.AssignLiteral("Linux x86_64");
#endif

  return NS_OK;
}

/* static */
nsresult Navigator::GetAppVersion(nsAString& aAppVersion, Document* aCallerDoc,
                                  bool aUsePrefOverriddenValue) {
  MOZ_ASSERT(NS_IsMainThread());

  if (aUsePrefOverriddenValue) {
    // If fingerprinting resistance is on, we will spoof this value. See
    // nsRFPService.h for details about spoofed values.
    if (ShouldResistFingerprinting(aCallerDoc,
                                   RFPTarget::NavigatorAppVersion)) {
      aAppVersion.AssignLiteral(SPOOFED_APPVERSION);
      return NS_OK;
    }
    nsAutoString override;
    nsresult rv = mozilla::Preferences::GetString("general.appversion.override",
                                                  override);

    if (NS_SUCCEEDED(rv)) {
      aAppVersion = std::move(override);
      return NS_OK;
    }
  }

  nsresult rv;

  nsCOMPtr<nsIHttpProtocolHandler> service(
      do_GetService(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "http", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString str;
  rv = service->GetAppVersion(str);
  CopyASCIItoUTF16(str, aAppVersion);
  NS_ENSURE_SUCCESS(rv, rv);

  aAppVersion.AppendLiteral(" (");

  rv = service->GetPlatform(str);
  NS_ENSURE_SUCCESS(rv, rv);

  AppendASCIItoUTF16(str, aAppVersion);
  aAppVersion.Append(char16_t(')'));

  return rv;
}

void Navigator::ClearUserAgentCache() {
  Navigator_Binding::ClearCachedUserAgentValue(this);
}

nsresult Navigator::GetUserAgent(nsPIDOMWindowInner* aWindow,
                                 Document* aCallerDoc,
                                 Maybe<bool> aShouldResistFingerprinting,
                                 nsACString& aUserAgent) {
  MOZ_ASSERT(NS_IsMainThread());

  /*
    ResistFingerprinting is migrating to fine-grained control based off
    either a channel or Principal+OriginAttributes

    This function can be called from Workers, Main Thread, and at least one
    other (unusual) case.

    For Main Thread, we will generally have a window and an associated
    Document, for Workers we will not.

    If aShouldResistFingerprinting is provided, we should respect it.
    If it is not provided, we will use aCallerDoc to determine our behavior.
  */

  bool shouldResistFingerprinting =
      aShouldResistFingerprinting.isSome()
          ? aShouldResistFingerprinting.value()
          : ShouldResistFingerprinting(aCallerDoc,
                                       RFPTarget::NavigatorUserAgent);

  // We will skip the override and pass to httpHandler to get spoofed userAgent
  // when 'privacy.resistFingerprinting' is true.
  if (!shouldResistFingerprinting) {
    nsAutoCString override;
    nsresult rv = mozilla::Preferences::GetCString("general.useragent.override",
                                                   override);

    if (NS_SUCCEEDED(rv)) {
      aUserAgent = std::move(override);
      return NS_OK;
    }
  }

  // When the caller is content and 'privacy.resistFingerprinting' is true,
  // return a spoofed userAgent which reveals the platform but not the
  // specific OS version, etc.
  if (shouldResistFingerprinting) {
    nsRFPService::GetSpoofedUserAgent(aUserAgent);
    return NS_OK;
  }

  nsresult rv;
  nsCOMPtr<nsIHttpProtocolHandler> service(
      do_GetService(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "http", &rv));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = service->GetUserAgent(aUserAgent);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!aWindow) {
    return NS_OK;
  }

  // Copy the User-Agent header from the document channel which has already been
  // subject to UA overrides.
  nsCOMPtr<Document> doc = aWindow->GetExtantDoc();
  if (!doc) {
    return NS_OK;
  }
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(doc->GetChannel());
  if (httpChannel) {
    bool IsUserAgentHeaderOutdated;
    (void)httpChannel->GetIsUserAgentHeaderOutdated(&IsUserAgentHeaderOutdated);

    // Do not return user agent from the request
    // if the user agent of the channel is outdated.
    if (!IsUserAgentHeaderOutdated) {
      rv = httpChannel->GetRequestHeader("User-Agent"_ns, aUserAgent);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }
  return NS_OK;
}

static nsCString RequestKeySystemAccessLogString(
    const nsAString& aKeySystem,
    const Sequence<MediaKeySystemConfiguration>& aConfigs,
    bool aIsSecureContext) {
  nsCString str;
  str.AppendPrintf(
      "Navigator::RequestMediaKeySystemAccess(keySystem='%s' options=",
      NS_ConvertUTF16toUTF8(aKeySystem).get());
  str.Append(MediaKeySystemAccess::ToCString(aConfigs));
  str.AppendLiteral(") secureContext=");
  str.AppendInt(aIsSecureContext);
  return str;
}

already_AddRefed<Promise> Navigator::RequestMediaKeySystemAccess(
    const nsAString& aKeySystem,
    const Sequence<MediaKeySystemConfiguration>& aConfigs, ErrorResult& aRv) {
  EME_LOG("%s", RequestKeySystemAccessLogString(aKeySystem, aConfigs,
                                                mWindow->IsSecureContext())
                    .get());

  if (!mWindow->IsSecureContext()) {
    Document* doc = mWindow->GetExtantDoc();
    AutoTArray<nsString, 1> params;
    nsString* uri = params.AppendElement();
    if (doc) {
      (void)doc->GetDocumentURI(*uri);
    }
    nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "Media"_ns,
                                    doc, PropertiesFile::DOM_PROPERTIES,
                                    "MediaEMEInsecureContextDeprecatedWarning",
                                    params);
  }

  Document* doc = mWindow->GetExtantDoc();
  if (doc &&
      !PermissionsPolicyUtils::IsFeatureAllowed(doc, u"encrypted-media"_ns)) {
    aRv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return nullptr;
  }

  RefPtr<DetailedPromise> promise = DetailedPromise::Create(
      mWindow->AsGlobal(), aRv, "navigator.requestMediaKeySystemAccess"_ns);
  if (aRv.Failed()) {
    return nullptr;
  }

  GetOrCreateMediaKeySystemAccessManager()->Request(promise, aKeySystem,
                                                    aConfigs);
  return promise.forget();
}

MediaKeySystemAccessManager*
Navigator::GetOrCreateMediaKeySystemAccessManager() {
  if (!mMediaKeySystemAccessManager) {
    mMediaKeySystemAccessManager = new MediaKeySystemAccessManager(mWindow);
  }
  return mMediaKeySystemAccessManager;
}

CredentialsContainer* Navigator::Credentials() {
  if (!mCredentials) {
    mCredentials = new CredentialsContainer(GetWindow());
  }
  return mCredentials;
}

dom::MediaCapabilities* Navigator::MediaCapabilities() {
  if (!mMediaCapabilities) {
    mMediaCapabilities = new dom::MediaCapabilities(GetWindow()->AsGlobal());
  }
  return mMediaCapabilities;
}

dom::MediaSession* Navigator::MediaSession() {
  if (!mMediaSession) {
    mMediaSession = new dom::MediaSession(GetWindow());
  }
  return mMediaSession;
}

dom::AudioSession* Navigator::AudioSession() {
  if (!mAudioSession) {
    mAudioSession = new dom::AudioSession(GetWindow());
    glean::media_audio_session::api_used.Add(1);
  }
  return mAudioSession;
}

bool Navigator::HasCreatedMediaSession() const {
  return mMediaSession != nullptr;
}

Clipboard* Navigator::Clipboard() {
  if (!mClipboard) {
    mClipboard = new dom::Clipboard(GetWindow());
  }
  return mClipboard;
}

AddonManager* Navigator::GetMozAddonManager(ErrorResult& aRv) {
  if (!mAddonManager) {
    nsPIDOMWindowInner* win = GetWindow();
    if (!win) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }

    mAddonManager = ConstructJSImplementation<AddonManager>(
        "@mozilla.org/addon-web-api/manager;1", win->AsGlobal(), aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
  }

  return mAddonManager;
}

webgpu::Instance* Navigator::Gpu() {
  if (!mWebGpu) {
    mWebGpu = webgpu::Instance::Create(GetWindow()->AsGlobal());
  }
  return mWebGpu;
}

dom::LockManager* Navigator::Locks() {
  if (!mLocks) {
    mLocks = dom::LockManager::Create(*GetWindow()->AsGlobal());
  }
  return mLocks;
}

NavigatorLogin* Navigator::Login() {
  if (!mLogin) {
    mLogin = new NavigatorLogin(GetWindow());
  }
  return mLogin;
}

dom::ModelContext* Navigator::ModelContext() {
  if (!mModelContext) {
    mModelContext = new dom::ModelContext(GetWindow());
  }
  return mModelContext;
}

/* static */
bool Navigator::Webdriver() {
#ifdef ENABLE_WEBDRIVER
  nsCOMPtr<nsIMarionette> marionette = do_GetService(NS_MARIONETTE_CONTRACTID);
  if (marionette) {
    bool isBrowserAutomationRunning = false;
    marionette->GetIsBrowserAutomationRunning(&isBrowserAutomationRunning);
    if (isBrowserAutomationRunning) {
      return true;
    }
  }

  nsCOMPtr<nsIRemoteAgent> agent = do_GetService(NS_REMOTEAGENT_CONTRACTID);
  if (agent) {
    bool isBrowserAutomationRunning = false;
    agent->GetIsBrowserAutomationRunning(&isBrowserAutomationRunning);
    if (isBrowserAutomationRunning) {
      return true;
    }
  }
#endif

  return false;
}

AutoplayPolicy Navigator::GetAutoplayPolicy(AutoplayPolicyMediaType aType) {
  if (!mWindow) {
    return AutoplayPolicy::Disallowed;
  }
  nsCOMPtr<Document> doc = mWindow->GetExtantDoc();
  if (!doc) {
    return AutoplayPolicy::Disallowed;
  }
  return media::AutoplayPolicy::GetAutoplayPolicy(aType, *doc);
}

AutoplayPolicy Navigator::GetAutoplayPolicy(HTMLMediaElement& aElement) {
  return media::AutoplayPolicy::GetAutoplayPolicy(aElement);
}

AutoplayPolicy Navigator::GetAutoplayPolicy(AudioContext& aContext) {
  return media::AutoplayPolicy::GetAutoplayPolicy(aContext);
}

already_AddRefed<dom::UserActivation> Navigator::UserActivation() {
  if (!mUserActivation) {
    mUserActivation = new dom::UserActivation(GetWindow());
  }
  return do_AddRef(mUserActivation);
}

dom::WakeLockJS* Navigator::WakeLock() {
  if (!mWakeLock) {
    mWakeLock = new WakeLockJS(mWindow);
  }
  return mWakeLock;
}

}  // namespace mozilla::dom
