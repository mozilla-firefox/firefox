/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAboutRedirector.h"
#include "nsNetUtil.h"
#include "nsAboutProtocolUtils.h"
#include "nsBaseChannel.h"
#include "mozilla/ArrayUtils.h"
#include "nsIProtocolHandler.h"
#include "nsXULAppAPI.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/RemoteType.h"
#include "mozilla/gfx/GPUProcessManager.h"

#define ABOUT_CONFIG_ENABLED_PREF "general.aboutConfig.enable"

NS_IMPL_ISUPPORTS(nsAboutRedirector, nsIAboutModule)

struct RedirEntry {
  const char* id;
  const char* url;
  uint32_t flags;
};

class CrashChannel final : public nsBaseChannel {
 public:
  explicit CrashChannel(nsIURI* aURI) { SetURI(aURI); }

  nsresult OpenContentStream(bool async, nsIInputStream** stream,
                             nsIChannel** channel) override {
    nsAutoCString spec;
    mURI->GetSpec(spec);

    if (spec.EqualsASCII("about:crashparent") && XRE_IsParentProcess()) {
      MOZ_CRASH("Crash via about:crashparent");
    }

    if (spec.EqualsASCII("about:crashgpu") && XRE_IsParentProcess()) {
      if (auto* gpu = mozilla::gfx::GPUProcessManager::Get()) {
        gpu->CrashProcess();
      }
    }

    if (spec.EqualsASCII("about:crashcontent") && XRE_IsContentProcess()) {
      MOZ_CRASH("Crash via about:crashcontent");
    }

    if (spec.EqualsASCII("about:crashextensions") && XRE_IsParentProcess()) {
      using ContentParent = mozilla::dom::ContentParent;
      nsTArray<RefPtr<ContentParent>> toKill;
      for (auto* cp : ContentParent::AllProcesses(ContentParent::eLive)) {
        if (cp->GetRemoteType() == EXTENSION_REMOTE_TYPE) {
          toKill.AppendElement(cp);
        }
      }
      for (auto& cp : toKill) {
        cp->KillHard("Killed via about:crashextensions");
      }
    }

    NS_WARNING("Unhandled about:crash* URI or wrong process");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

 protected:
  virtual ~CrashChannel() = default;
};

/*
  Entries which do not have URI_SAFE_FOR_UNTRUSTED_CONTENT will run with chrome
  privileges. This is potentially dangerous. Please use
  URI_SAFE_FOR_UNTRUSTED_CONTENT in the third argument to each map item below
  unless your about: page really needs chrome privileges. Security review is
  required before adding new map entries without
  URI_SAFE_FOR_UNTRUSTED_CONTENT.

  URI_SAFE_FOR_UNTRUSTED_CONTENT is not enough to let web pages load that page,
  for that you need MAKE_LINKABLE.

  NOTE: changes to this redir map need to be accompanied with changes to
    docshell/build/components.conf
 */
static const RedirEntry kRedirMap[] = {
    {"about", "chrome://global/content/aboutAbout.html", 0},
#ifndef MOZ_WIDGET_ANDROID
    {"addons", "chrome://mozapps/content/extensions/aboutaddons.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
#endif
    {"buildconfig", "chrome://global/content/buildconfig.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::IS_SECURE_CHROME_UI},
    {"checkerboard", "chrome://global/content/aboutCheckerboard.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::ALLOW_SCRIPT},
#ifndef MOZ_WIDGET_ANDROID
    {"config", "chrome://global/content/aboutconfig/aboutconfig.html",
     nsIAboutModule::IS_SECURE_CHROME_UI},
#else
    {"config", "chrome://geckoview/content/config.xhtml",
     nsIAboutModule::IS_SECURE_CHROME_UI |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
#endif
#ifdef MOZ_CRASHREPORTER
    {"crashes", "chrome://global/content/crashes.html",
     nsIAboutModule::IS_SECURE_CHROME_UI},
#endif
    {"credits", "https://www.mozilla.org/credits/",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD},
    {"fingerprintingprotection",
     "chrome://global/content/usercharacteristics/usercharacteristics.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS},
#ifdef MOZ_WIDGET_ANDROID
    {"home", "about:blank",
         nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD},
#endif
    {"httpsonlyerror", "chrome://global/content/httpsonlyerror/errorpage.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
#if defined(NIGHTLY_BUILD)
    {"inference", "chrome://global/content/aboutInference.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
#endif
    {"license", "chrome://global/content/license.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::IS_SECURE_CHROME_UI},
    {"logging", "chrome://global/content/aboutLogging/aboutLogging.html",
     nsIAboutModule::ALLOW_SCRIPT},
    {"logo", "chrome://branding/content/about.png",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         // Linkable for testing reasons.
         nsIAboutModule::MAKE_LINKABLE},
    {"memory", "chrome://global/content/aboutMemory.xhtml",
     nsIAboutModule::ALLOW_SCRIPT},
    {"certificate", "chrome://global/content/certviewer/certviewer.html",
     nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::IS_SECURE_CHROME_UI},
    {"mozilla", "chrome://global/content/mozilla.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT},
#if !defined(ANDROID) && !defined(XP_WIN)
    {"webauthn", "chrome://global/content/aboutWebauthn.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
#endif
    {"neterror", "chrome://global/content/aboutNetError.html",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_CHILD | nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"networking", "chrome://global/content/aboutNetworking.html",
     nsIAboutModule::ALLOW_SCRIPT},
    {"performance", "about:processes",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"processes", "chrome://global/content/aboutProcesses.html",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
    // about:serviceworkers always wants to load in the parent process because
    // the only place nsIServiceWorkerManager has any data is in the parent
    // process.
    //
    // There is overlap without about:debugging, but about:debugging is not
    // available on mobile at this time, and it's useful to be able to know if
    // a ServiceWorker is registered directly from the mobile browser without
    // having to connect the device to a desktop machine and all that entails.
    {"serviceworkers", "chrome://global/content/aboutServiceWorkers.xhtml",
     nsIAboutModule::ALLOW_SCRIPT},
#ifndef ANDROID
    {"profiles", "chrome://global/content/aboutProfiles.xhtml",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
#endif
    // about:srcdoc is unresolvable by specification.  It is included here
    // because the security manager would disallow srcdoc iframes otherwise.
    {"srcdoc", "about:blank",
     nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT |
         // Needs to be linkable so content can touch its own srcdoc frames
         nsIAboutModule::MAKE_LINKABLE | nsIAboutModule::URI_CAN_LOAD_IN_CHILD},
    {"support", "chrome://global/content/aboutSupport.xhtml",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
#ifdef XP_WIN
    {"third-party", "chrome://global/content/aboutThirdParty.html",
     nsIAboutModule::ALLOW_SCRIPT},
    {"windows-messages", "chrome://global/content/aboutWindowsMessages.html",
     nsIAboutModule::ALLOW_SCRIPT},
#endif
#ifndef MOZ_GLEAN_ANDROID
    {"glean", "chrome://global/content/aboutGlean.html",
#  if !defined(NIGHTLY_BUILD) && defined(MOZILLA_OFFICIAL)
     nsIAboutModule::HIDE_FROM_ABOUTABOUT |
#  endif
         nsIAboutModule::ALLOW_SCRIPT},
#endif
    {"telemetry", "chrome://global/content/aboutTelemetry.xhtml",
     nsIAboutModule::ALLOW_SCRIPT | nsIAboutModule::IS_SECURE_CHROME_UI},
    {"translations", "chrome://global/content/translations/translations.html",
     nsIAboutModule::ALLOW_SCRIPT |
         nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD |
         nsIAboutModule::URI_CAN_LOAD_IN_PRIVILEGEDABOUT_PROCESS |
         nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"url-classifier", "chrome://global/content/aboutUrlClassifier.xhtml",
     nsIAboutModule::ALLOW_SCRIPT},
    {"webrtc", "chrome://global/content/aboutwebrtc/aboutWebrtc.html",
     nsIAboutModule::ALLOW_SCRIPT},
    {"crashparent", "about:blank", nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"crashcontent", "about:blank",
     nsIAboutModule::HIDE_FROM_ABOUTABOUT |
         nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT |
         nsIAboutModule::URI_CAN_LOAD_IN_CHILD |
         nsIAboutModule::URI_MUST_LOAD_IN_CHILD},
    {"crashgpu", "about:blank", nsIAboutModule::HIDE_FROM_ABOUTABOUT},
    {"crashextensions", "about:blank", nsIAboutModule::HIDE_FROM_ABOUTABOUT}};
static const int kRedirTotal = std::size(kRedirMap);

NS_IMETHODIMP
nsAboutRedirector::NewChannel(nsIURI* aURI, nsILoadInfo* aLoadInfo,
                              nsIChannel** aResult) {
  NS_ENSURE_ARG_POINTER(aURI);
  NS_ENSURE_ARG_POINTER(aLoadInfo);
  NS_ASSERTION(aResult, "must not be null");

  nsAutoCString path;
  nsresult rv = NS_GetAboutModuleName(aURI, path);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIIOService> ioService = do_GetIOService(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  if (path.EqualsASCII("crashparent") || path.EqualsASCII("crashcontent") ||
      path.EqualsASCII("crashgpu") || path.EqualsASCII("crashextensions")) {
    bool isExternal;
    aLoadInfo->GetLoadTriggeredFromExternal(&isExternal);
    if (isExternal || !aLoadInfo->TriggeringPrincipal() ||
        !aLoadInfo->TriggeringPrincipal()->IsSystemPrincipal()) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    nsCOMPtr<nsIChannel> channel = new CrashChannel(aURI);
    channel->SetLoadInfo(aLoadInfo);
    channel.forget(aResult);
    return NS_OK;
  }

  if (path.EqualsASCII("config") &&
      !mozilla::Preferences::GetBool(ABOUT_CONFIG_ENABLED_PREF, true)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  for (int i = 0; i < kRedirTotal; i++) {
    if (!strcmp(path.get(), kRedirMap[i].id)) {
      nsCOMPtr<nsIChannel> tempChannel;
      nsCOMPtr<nsIURI> tempURI;
      rv = NS_NewURI(getter_AddRefs(tempURI), kRedirMap[i].url);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = NS_NewChannelInternal(getter_AddRefs(tempChannel), tempURI,
                                 aLoadInfo);
      NS_ENSURE_SUCCESS(rv, rv);

      // If tempURI links to an external URI (i.e. something other than
      // chrome:// or resource://) then set result principal URI on the
      // load info which forces the channel principal to reflect the displayed
      // URL rather then being the systemPrincipal.
      bool isUIResource = false;
      rv = NS_URIChainHasFlags(tempURI, nsIProtocolHandler::URI_IS_UI_RESOURCE,
                               &isUIResource);
      NS_ENSURE_SUCCESS(rv, rv);

      bool isAboutBlank = NS_IsAboutBlank(tempURI);

      if (!isUIResource && !isAboutBlank) {
        aLoadInfo->SetResultPrincipalURI(tempURI);
      }

      tempChannel->SetOriginalURI(aURI);

      tempChannel.forget(aResult);
      return rv;
    }
  }

  NS_ERROR("nsAboutRedirector called for unknown case");
  return NS_ERROR_ILLEGAL_VALUE;
}

NS_IMETHODIMP
nsAboutRedirector::GetURIFlags(nsIURI* aURI, uint32_t* aResult) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsAutoCString name;
  nsresult rv = NS_GetAboutModuleName(aURI, name);
  NS_ENSURE_SUCCESS(rv, rv);

  for (int i = 0; i < kRedirTotal; i++) {
    if (name.EqualsASCII(kRedirMap[i].id)) {
      *aResult = kRedirMap[i].flags;
      return NS_OK;
    }
  }

  NS_ERROR("nsAboutRedirector called for unknown case");
  return NS_ERROR_ILLEGAL_VALUE;
}

NS_IMETHODIMP
nsAboutRedirector::GetChromeURI(nsIURI* aURI, nsIURI** chromeURI) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsAutoCString name;
  nsresult rv = NS_GetAboutModuleName(aURI, name);
  NS_ENSURE_SUCCESS(rv, rv);

  for (const auto& redir : kRedirMap) {
    if (name.EqualsASCII(redir.id)) {
      return NS_NewURI(chromeURI, redir.url);
    }
  }

  NS_ERROR("nsAboutRedirector called for unknown case");
  return NS_ERROR_ILLEGAL_VALUE;
}

nsresult nsAboutRedirector::Create(REFNSIID aIID, void** aResult) {
  RefPtr<nsAboutRedirector> about = new nsAboutRedirector();
  return about->QueryInterface(aIID, aResult);
}
