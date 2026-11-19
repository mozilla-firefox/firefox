/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsReadConfig.h"
#include "nsJSConfigTriggers.h"

#include "mozilla/Logging.h"
#include "mozilla/Components.h"
#include "mozilla/HelperMacros.h"
#include "mozilla/Preferences.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsIAppStartup.h"
#include "nsIChannel.h"
#include "nsContentUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsIInputStream.h"
#include "nsIObserverService.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsIPromptService.h"
#include "nsIStringBundle.h"
#include "nsNetUtil.h"
#include "nsString.h"
#include "nsCRT.h"
#include "nspr.h"
#include "nsXULAppAPI.h"

using namespace mozilla;

extern bool sandboxEnabled;

extern mozilla::LazyLogModule MCD;

extern nsresult CentralizedAdminPrefManagerInit(bool aSandboxEnabled);
extern nsresult CentralizedAdminPrefManagerFinish();

static nsresult DisplayError(void) {
  nsresult rv;

  nsCOMPtr<nsIPromptService> promptService =
      do_GetService("@mozilla.org/prompter;1");
  if (!promptService) return NS_ERROR_FAILURE;

  nsCOMPtr<nsIStringBundleService> bundleService =
      do_GetService(NS_STRINGBUNDLE_CONTRACTID);
  if (!bundleService) return NS_ERROR_FAILURE;

  nsCOMPtr<nsIStringBundle> bundle;
  bundleService->CreateBundle(
      "chrome://autoconfig/locale/autoconfig.properties",
      getter_AddRefs(bundle));
  if (!bundle) return NS_ERROR_FAILURE;

  nsAutoString title;
  rv = bundle->GetStringFromName("readConfigTitle", title);
  if (NS_FAILED(rv)) return rv;

  nsAutoString err;
  rv = bundle->GetStringFromName("readConfigMsg", err);
  if (NS_FAILED(rv)) return rv;

  return promptService->Alert(nullptr, title.get(), err.get());
}

// nsISupports Implementation

NS_IMPL_ISUPPORTS(nsReadConfig, nsIObserver)

nsReadConfig::nsReadConfig() : mRead(false) {}

nsresult nsReadConfig::Init() {
  nsresult rv;

  nsCOMPtr<nsIObserverService> observerService =
      do_GetService("@mozilla.org/observer-service;1", &rv);

  if (observerService) {
    rv =
        observerService->AddObserver(this, NS_PREFSERVICE_READ_TOPIC_ID, false);
  }
  return (rv);
}

nsReadConfig::~nsReadConfig() { CentralizedAdminPrefManagerFinish(); }

NS_IMETHODIMP nsReadConfig::Observe(nsISupports* /*aSubject*/,
                                    const char* aTopic,
                                    const char16_t* /*someData*/) {
  nsresult rv = NS_OK;

  if (!nsCRT::strcmp(aTopic, NS_PREFSERVICE_READ_TOPIC_ID)) {
    rv = readConfigFile();
    // Don't show error alerts if the sandbox is enabled, just show
    // sandbox warning.
    if (NS_FAILED(rv)) {
      if (sandboxEnabled) {
        nsContentUtils::ReportToConsoleNonLocalized(
            u"Autoconfig is sandboxed by default. See "
            "https://support.mozilla.org/products/"
            "firefox-enterprise for more information."_ns,
            nsIScriptError::warningFlag, "autoconfig"_ns, nullptr);
      } else {
        rv = DisplayError();
        if (NS_FAILED(rv)) {
          nsCOMPtr<nsIAppStartup> appStartup =
              components::AppStartup::Service();
          if (appStartup) {
            appStartup->Quit(nsIAppStartup::eAttemptQuit, 0);
          }
        }
      }
    }
  }
  return rv;
}

/**
 * This is the blocklist for known bad autoconfig files.
 */
static const char* gBlockedConfigs[] = {"dsengine.cfg"};

/**
 * AutoConfig reads its inputs from the installation, not from the profile, so
 * the user values of these prefs are cleared before the .cfg is evaluated.
 * prefs.js and user.js have already been parsed at this point (see the
 * InitializeUserPrefs, UpdateCurrentProfile, InitializeJSContext and
 * FinishInitializingUserPrefs call chain in XREMain::XRE_mainRun). The .cfg
 * can still set them with pref().
 */
static const char* const gAutoConfigInputPrefs[] = {
    "general.config.filename",    "autoadmin.global_config_url",
    "autoadmin.offline_failover", "autoadmin.append_emailaddr",
    "autoadmin.refresh_interval", "autoadmin.failover_to_cached",
};

nsresult nsReadConfig::readConfigFile() {
  nsresult rv = NS_OK;
  nsAutoCString lockFileName;

  nsCOMPtr<nsIPrefBranch> defaultPrefBranch;
  nsCOMPtr<nsIPrefService> prefService =
      do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) return rv;

  rv =
      prefService->GetDefaultBranch(nullptr, getter_AddRefs(defaultPrefBranch));
  if (NS_FAILED(rv)) return rv;

  constexpr auto channel = nsLiteralCString{MOZ_STRINGIFY(MOZ_UPDATE_CHANNEL)};

  bool sandboxEnabled =
      channel.EqualsLiteral("beta") || channel.EqualsLiteral("release");

  (void)defaultPrefBranch->GetBoolPref("general.config.sandbox_enabled",
                                       &sandboxEnabled);

  rv = defaultPrefBranch->GetCharPref("general.config.filename", lockFileName);

  if (NS_FAILED(rv)) return rv;

  MOZ_LOG(MCD, LogLevel::Debug,
          ("general.config.filename = %s\n", lockFileName.get()));

  for (const char* prefName : gAutoConfigInputPrefs) {
    Preferences::ClearUser(prefName);
  }

  for (size_t index = 0, len = std::size(gBlockedConfigs); index < len;
       ++index) {
    if (lockFileName == gBlockedConfigs[index]) {
      // This is NS_OK because we don't want to show an error to the user
      return rv;
    }
  }

  // This needs to be read only once.
  //
  if (!mRead) {
    // Initiate the new JS Context for Preference management

    rv = CentralizedAdminPrefManagerInit(sandboxEnabled);
    if (NS_FAILED(rv)) return rv;

    // Open and evaluate function calls to set/lock/unlock prefs
    rv = openAndEvaluateJSFile("prefcalls.js", 0, false, false);
    if (NS_FAILED(rv)) return rv;

    mRead = true;
  }
  // If the lockFileName is nullptr return ok, because no lockFile will be used

  nsCOMPtr<nsIPrefBranch> prefBranch;
  rv = prefService->GetBranch(nullptr, getter_AddRefs(prefBranch));
  NS_ENSURE_SUCCESS(rv, rv);

  int32_t obscureValue = 0;
  (void)defaultPrefBranch->GetIntPref("general.config.obscure_value",
                                      &obscureValue);
  MOZ_LOG(MCD, LogLevel::Debug,
          ("evaluating .cfg file %s with obscureValue %d\n", lockFileName.get(),
           obscureValue));
  rv = openAndEvaluateJSFile(lockFileName.get(), obscureValue, true, true);
  if (NS_FAILED(rv)) {
    MOZ_LOG(MCD, LogLevel::Debug,
            ("error evaluating .cfg file %s %" PRIx32 "\n", lockFileName.get(),
             static_cast<uint32_t>(rv)));
    return rv;
  }

  // get the value of the autoconfig url
  nsAutoCString urlName;
  rv = prefBranch->GetCharPref("autoadmin.global_config_url", urlName);
  if (NS_SUCCEEDED(rv) && !urlName.IsEmpty()) {
    // Instantiating nsAutoConfig object if the pref is present
    mAutoConfig = new nsAutoConfig();

    rv = mAutoConfig->Init();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    mAutoConfig->SetConfigURL(urlName.get());
  }

  return NS_OK;
}  // ReadConfigFile

nsresult nsReadConfig::openAndEvaluateJSFile(const char* aFileName,
                                             int32_t obscureValue,
                                             bool isEncoded, bool isBinDir) {
  nsresult rv;

  nsCOMPtr<nsIInputStream> inStr;
  if (isBinDir) {
    nsCOMPtr<nsIFile> jsFile;
#if defined(MOZ_WIDGET_GTK) && defined(MOZ_SYSTEM_PREFERENCES)
    bool exists;

    rv =
        NS_GetSpecialDirectory(NS_OS_SYSTEM_CONFIG_DIR, getter_AddRefs(jsFile));
    if (NS_FAILED(rv)) return rv;

    rv = jsFile->AppendNative(nsDependentCString(aFileName));
    if (NS_FAILED(rv)) return rv;

    rv = jsFile->Exists(&exists);
    if (NS_FAILED(rv)) return rv;

    if (!exists) {
#endif  // defined(MOZ_WIDGET_GTK) && defined(MOZ_SYSTEM_PREFERENCES)
      rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(jsFile));
      if (NS_FAILED(rv)) return rv;

      rv = jsFile->AppendNative(nsDependentCString(aFileName));
      if (NS_FAILED(rv)) return rv;
#if defined(MOZ_WIDGET_GTK) && defined(MOZ_SYSTEM_PREFERENCES)
    }
#endif  // defined(MOZ_WIDGET_GTK) && defined(MOZ_SYSTEM_PREFERENCES)

    rv = NS_NewLocalFileInputStream(getter_AddRefs(inStr), jsFile);
    if (NS_FAILED(rv)) return rv;

  } else {
    nsAutoCString location("resource://gre/defaults/autoconfig/");
    location += aFileName;

    nsCOMPtr<nsIURI> uri;
    rv = NS_NewURI(getter_AddRefs(uri), location);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIChannel> channel;
    rv = NS_NewChannel(getter_AddRefs(channel), uri,
                       nsContentUtils::GetSystemPrincipal(),
                       nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                       nsIContentPolicy::TYPE_OTHER);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = channel->Open(getter_AddRefs(inStr));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  uint64_t fs64;
  uint32_t amt = 0;
  rv = inStr->Available(&fs64);
  if (NS_FAILED(rv)) return rv;
  // This used to use PR_Malloc(), which doesn't support over 4GB.
  if (fs64 > UINT32_MAX) return NS_ERROR_FILE_TOO_BIG;
  uint32_t fs = (uint32_t)fs64;

  char* buf = (char*)malloc(fs * sizeof(char));
  if (!buf) return NS_ERROR_OUT_OF_MEMORY;

  rv = inStr->Read(buf, (uint32_t)fs, &amt);
  NS_ASSERTION((amt == fs), "failed to read the entire configuration file!!");
  if (NS_SUCCEEDED(rv)) {
    if (obscureValue > 0) {
      // Unobscure file by subtracting some value from every char.
      for (uint32_t i = 0; i < amt; i++) buf[i] -= obscureValue;
    }
    rv = EvaluateAdminConfigScript(buf, amt, aFileName, false, true, isEncoded,
                                   !isBinDir);
  }
  inStr->Close();
  free(buf);

  return rv;
}
