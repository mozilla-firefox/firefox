/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// See also: docshell/base/nsAboutRedirector.cpp

#include "AboutRedirector.h"
#include "nsNetUtil.h"
#include "nsIAppStartup.h"
#include "nsIChannel.h"
#include "nsIURI.h"
#include "nsIProtocolHandler.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "mozilla/Components.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/browser/NimbusFeatures.h"

#define REFERRALS_ENABLED_PREF "browser.referrals.enabled"
#define PROFILES_ENABLED_PREF "browser.profiles.enabled"
#define ABOUT_WELCOME_CHROME_URL \
  "chrome://browser/content/aboutwelcome/aboutwelcome.html"
#define ABOUT_HOME_URL "about:home"

namespace mozilla {
namespace browser {

NS_IMPL_ISUPPORTS(AboutRedirector, nsIAboutModule)

struct RedirEntry {
  const char* id;
  const char* url;
  uint32_t flags;
};

/*
  Entries which do not have URI_SAFE_FOR_UNTRUSTED_CONTENT will run with chrome
  privileges. This is potentially dangerous. Please use
  URI_SAFE_FOR_UNTRUSTED_CONTENT in the third argument to each map item below
  unless your about: page really needs chrome privileges. Security review is
  required before adding new map entries without
  URI_SAFE_FOR_UNTRUSTED_CONTENT.

  NOTE: changes to this redir map need to be accompanied with changes to
    browser/components/about/components.conf
*/
static const RedirEntry kRedirMap[] = {
    {"aichatcontent", "chrome://browser/content/aiwindow/aiChatContent.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"asrouter", "chrome://browser/content/asrouter/asrouter-admin.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"blocked", "chrome://browser/content/blockedSite.xhtml",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"certerror", "chrome://global/content/aboutNetError.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"unloads", "chrome://browser/content/tabunloader/aboutUnloads.html",
     nsIAboutModule::ALLOW_SCRIPT},
    {"framecrashed", "chrome://browser/content/aboutFrameCrashed.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_CHILD |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"logins", "chrome://browser/content/aboutlogins/aboutLogins.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::IS_SECURE_CHROME_UI},
    {"loginsimportreport",
     "chrome://browser/content/aboutlogins/aboutLoginsImportReport.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::IS_SECURE_CHROME_UI},
    {"firefoxview", "chrome://browser/content/firefoxview/firefoxview.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
    {"funcomputer", "chrome://browser/content/funcomputer-accounts.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS},
    {"funsearch", "https://funsearchapp.netlify.app/",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT},
    {"opentabs", "chrome://browser/content/tabbrowser/opentabs.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"policies", "chrome://browser/content/policies/aboutPolicies.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
    {"privatebrowsing", "chrome://browser/content/aboutPrivateBrowsing.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS},
    {"profiling",
     "chrome://devtools/content/performance-new/aboutprofiling/index.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
    {"referrals", "https://www.firefox.com/invite/",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD},
    {"rights", "https://www.mozilla.org/about/legal/terms/firefox/",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD},
    {"robots", "chrome://browser/content/aboutRobots.xhtml",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::ALLOW_SCRIPT},
    {"sessionrestore", "chrome://browser/content/aboutSessionRestore.xhtml",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::HIDE_FROM_ABOUTABOUT |
         nsIAboutModule::IS_SECURE_CHROME_UI},
    {"tabcrashed", "chrome://browser/content/aboutTabCrashed.xhtml",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"welcomeback", "chrome://browser/content/aboutWelcomeBack.xhtml",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::HIDE_FROM_ABOUTABOUT |
         nsIAboutModule::IS_SECURE_CHROME_UI},
    {"welcome", "about:blank",
     nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::ALLOW_SCRIPT},
    {"messagepreview",
     "chrome://browser/content/messagepreview/messagepreview.html",
     nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"settings", "chrome://browser/content/preferences/preferences.xhtml",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"smartwindowtasks", "chrome://browser/content/aiwindow/tasks.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"preferences", "chrome://browser/content/preferences/preferences.xhtml",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
    {"downloads",
     "chrome://browser/content/downloads/contentAreaDownloadsView.xhtml",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
    {"reader", "chrome://global/content/reader/aboutReader.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"restartrequired", "chrome://browser/content/aboutRestartRequired.xhtml",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"protections", "chrome://browser/content/protections.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::IS_SECURE_CHROME_UI},
#ifdef MOZ_NORMANDY
    {"studies", "resource://normandy-content/about-studies/about-studies.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT},
#endif
#ifdef MOZ_SELECTABLE_PROFILES
    {"profilemanager", "chrome://browser/content/profiles/profiles.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"editprofile", "chrome://browser/content/profiles/edit-profile.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::IS_SECURE_CHROME_UI | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"deleteprofile", "chrome://browser/content/profiles/delete-profile.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::IS_SECURE_CHROME_UI |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"newprofile", "chrome://browser/content/profiles/new-profile.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::IS_SECURE_CHROME_UI | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
#endif
    {"keyboard", "chrome://browser/content/customkeys/customkeys.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::IS_SECURE_CHROME_UI | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS},
};

static nsAutoCString GetAboutModuleName(nsIURI* aURI) {
  nsAutoCString path;
  aURI->GetPathQueryRef(path);

  int32_t f = path.FindChar('#');
  if (f >= 0) path.SetLength(f);

  f = path.FindChar('?');
  if (f >= 0) path.SetLength(f);

  ToLowerCase(path);
  return path;
}

NS_IMETHODIMP
AboutRedirector::NewChannel(nsIURI* aURI, nsILoadInfo* aLoadInfo,
                            nsIChannel** result) {
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(aLoadInfo);

  NS_ASSERTION(result, "must not be null");

  nsAutoCString path = GetAboutModuleName(aURI);

  if (path.EqualsASCII("referrals") &&
      !mozilla::Preferences::GetBool(REFERRALS_ENABLED_PREF, false)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if ((path.EqualsASCII("editprofile") || path.EqualsASCII("deleteprofile") ||
       path.EqualsASCII("newprofile")) &&
      !mozilla::Preferences::GetBool(PROFILES_ENABLED_PREF, false)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (path.EqualsASCII("profilemanager") &&
      !mozilla::Preferences::GetBool(PROFILES_ENABLED_PREF, false)) {
    bool startingUp;
    nsCOMPtr<nsIAppStartup> appStartup(
        mozilla::components::AppStartup::Service());
    if (NS_FAILED(appStartup->GetStartingUp(&startingUp)) || !startingUp) {
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  for (auto& redir : kRedirMap) {
    if (!strcmp(path.get(), redir.id)) {
      nsAutoCString url;

      if (path.EqualsLiteral("welcome")) {
        NimbusFeatures::RecordExposureEvent("aboutwelcome"_ns, true);
        if (NimbusFeatures::GetBool("aboutwelcome"_ns, "enabled"_ns, true)) {
          url.AssignASCII(ABOUT_WELCOME_CHROME_URL);
        } else {
          url.AssignASCII(ABOUT_HOME_URL);
        }
      }

      if (path.EqualsLiteral("referrals")) {
        nsAutoCString query;
        if (nsresult rv = aURI->GetQuery(query); NS_SUCCEEDED(rv)) {
          url.AssignASCII(nsPrintfCString("%s?%s", redir.url, query.get()));
        } else {
          url.AssignASCII(redir.url);
        }
      }

      if (path.EqualsLiteral("funsearch")) {
        nsAutoCString pathQuery;
        if (NS_SUCCEEDED(aURI->GetPathQueryRef(pathQuery))) {
          int32_t queryStart = pathQuery.FindChar('?');
          if (queryStart >= 0) {
            nsAutoCString query(Substring(pathQuery, queryStart + 1));
            int32_t hash = query.FindChar('#');
            if (hash >= 0) {
              query.SetLength(hash);
            }
            if (!query.IsEmpty()) {
              url.AssignASCII(nsPrintfCString("%s?%s", redir.url, query.get()));
            }
          }
        }
      }

      // fall back to the specified url in the map
      if (url.IsEmpty()) {
        url.AssignASCII(redir.url);
      }

      nsCOMPtr<nsIChannel> tempChannel;
      nsCOMPtr<nsIURI> tempURI;
      nsresult rv = NS_NewURI(getter_AddRefs(tempURI), url);
      NS_ENSURE_SUCCESS(rv, rv);

      // If tempURI links to an external URI (i.e. something other than
      // chrome:// or resource://) then set the result principal URI on the
      // load info which forces the channel prncipal to reflect the displayed
      // URL rather then being the systemPrincipal.
      bool isUIResource = false;
      rv = NS_URIChainHasFlags(tempURI, nsIProtocolHandler::URI_IS_UI_RESOURCE,
                               &isUIResource);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = NS_NewChannelInternal(getter_AddRefs(tempChannel), tempURI,
                                 aLoadInfo);
      NS_ENSURE_SUCCESS(rv, rv);

      if (!isUIResource) {
        aLoadInfo->SetResultPrincipalURI(tempURI);
      }
      tempChannel->SetOriginalURI(aURI);

      NS_ADDREF(*result = tempChannel);
      return rv;
    }
  }

  return NS_ERROR_ILLEGAL_VALUE;
}

NS_IMETHODIMP
AboutRedirector::GetURIFlags(nsIURI* aURI, uint32_t* result) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsAutoCString name = GetAboutModuleName(aURI);

  for (auto& redir : kRedirMap) {
    if (name.Equals(redir.id)) {
      *result = redir.flags;
      return NS_OK;
    }
  }

  return NS_ERROR_ILLEGAL_VALUE;
}

NS_IMETHODIMP
AboutRedirector::GetChromeURI(nsIURI* aURI, nsIURI** chromeURI) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsAutoCString name = GetAboutModuleName(aURI);

  for (const auto& redir : kRedirMap) {
    if (name.Equals(redir.id)) {
      return NS_NewURI(chromeURI, redir.url);
    }
  }

  return NS_ERROR_ILLEGAL_VALUE;
}

nsresult AboutRedirector::Create(REFNSIID aIID, void** result) {
  AboutRedirector* about = new AboutRedirector();
  if (about == nullptr) return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(about);
  nsresult rv = about->QueryInterface(aIID, result);
  NS_RELEASE(about);
  return rv;
}

}  // namespace browser
}  // namespace mozilla
