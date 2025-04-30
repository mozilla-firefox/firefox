/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * RemotePageAccessManager determines which RPM functions a given
 * about page is allowed to access. It does this based on a map from about
 * page URLs to allowed functions for that page/URL.
 *
 * An RPM function will be exported into the page only if it appears
 * in the access managers's accessMap for that page's uri.
 *
 * This module may be used from both the child and parent process.
 *
 * Please note that prefs that one wants to update need to be
 * explicitly allowed within AsyncPrefs.sys.mjs.
 */
export let RemotePageAccessManager = {
  /* The accessMap lists the permissions that are allowed per page.
   * The structure should be of the following form:
   *   <URL> : {
   *     <function name>: [<keys>],
   *     ...
   *   }
   * For the page with given URL, permission is allowed for each
   * listed function with a matching key. The first argument to the
   * function must match one of the keys. If keys is an array with a
   * single asterisk element ["*"], then all values are permitted.
   */
  accessMap: {
    "about:certerror": {
      RPMSendAsyncMessage: [
        "Browser:EnableOnlineMode",
        "Browser:ResetSSLPreferences",
        "GetChangedCertPrefs",
        "Browser:OpenCaptivePortalPage",
        "Browser:SSLErrorGoBack",
        "Browser:PrimeMitm",
        "Browser:ResetEnterpriseRootsPref",
        "DisplayOfflineSupportPage",
      ],
      RPMRecordGleanEvent: ["*"],
      RPMAddMessageListener: ["*"],
      RPMRemoveMessageListener: ["*"],
      RPMGetFormatURLPref: ["app.support.baseURL"],
      RPMGetBoolPref: [
        "security.certerrors.mitm.priming.enabled",
        "security.certerrors.permanentOverride",
        "security.enterprise_roots.auto-enabled",
        "security.certerror.hideAddException",
        "security.certerrors.felt-privacy-v1",
      ],
      RPMGetIntPref: [
        "security.dialog_enable_delay",
        "services.settings.clock_skew_seconds",
        "services.settings.last_update_seconds",
      ],
      RPMGetAppBuildID: ["*"],
      RPMGetInnerMostURI: ["*"],
      RPMIsWindowPrivate: ["*"],
    },
    "about:home": {
      RPMSendAsyncMessage: ["ActivityStream:ContentToMain"],
      RPMAddMessageListener: ["ActivityStream:MainToContent"],
    },
    "about:httpsonlyerror": {
      RPMGetFormatURLPref: ["app.support.baseURL"],
      RPMGetIntPref: ["security.dialog_enable_delay"],
      RPMSendAsyncMessage: ["goBack", "openInsecure"],
      RPMAddMessageListener: ["WWWReachable"],
      RPMTryPingSecureWWWLink: ["*"],
      RPMOpenSecureWWWLink: ["*"],
    },
    "about:certificate": {
      RPMSendQuery: ["getCertificates"],
    },
    "about:neterror": {
      RPMSendAsyncMessage: [
        "Browser:EnableOnlineMode",
        "Browser:ResetSSLPreferences",
        "GetChangedCertPrefs",
        "Browser:OpenCaptivePortalPage",
        "Browser:SSLErrorGoBack",
        "Browser:PrimeMitm",
        "Browser:ResetEnterpriseRootsPref",
        "DisplayOfflineSupportPage",
        "OpenTRRPreferences",
      ],
      RPMCheckAlternateHostAvailable: ["*"],
      RPMRecordGleanEvent: ["securityDohNeterror", "securityUiTlserror"],
      RPMAddMessageListener: ["*"],
      RPMRemoveMessageListener: ["*"],
      RPMGetFormatURLPref: [
        "app.support.baseURL",
        "network.trr_ui.skip_reason_learn_more_url",
      ],
      RPMGetBoolPref: [
        "security.certerror.hideAddException",
        "security.certerrors.felt-privacy-v1",
      ],
      RPMGetInnerMostURI: ["*"],
      RPMGetHttpResponseHeader: ["*"],
      RPMIsTRROnlyFailure: ["*"],
      RPMIsFirefox: ["*"],
      RPMHasConnectivity: ["*"],
      RPMGetTRRSkipReason: ["*"],
      RPMGetTRRDomain: ["*"],
      RPMIsSiteSpecificTRRError: ["*"],
      RPMSetTRRDisabledLoadFlags: ["*"],
      RPMShowOSXLocalNetworkPermissionWarning: ["*"],
      RPMSendQuery: ["Browser:AddTRRExcludedDomain"],
      RPMGetIntPref: ["network.trr.mode"],
    },
    "about:newtab": {
      RPMSendAsyncMessage: ["ActivityStream:ContentToMain"],
      RPMAddMessageListener: ["ActivityStream:MainToContent"],
    },
    "about:pocket-saved": {
      RPMSendAsyncMessage: ["*"],
      RPMAddMessageListener: ["*"],
      RPMRemoveMessageListener: ["*"],
      RPMGetStringPref: ["extensions.pocket.site"],
    },
    "about:pocket-signup": {
      RPMSendAsyncMessage: ["*"],
      RPMAddMessageListener: ["*"],
      RPMRemoveMessageListener: ["*"],
      RPMGetStringPref: ["extensions.pocket.site"],
    },
    "about:pocket-home": {
      RPMSendAsyncMessage: ["*"],
      RPMAddMessageListener: ["*"],
      RPMRemoveMessageListener: ["*"],
      RPMGetStringPref: ["extensions.pocket.site"],
    },
    "about:pocket-style-guide": {
      RPMSendAsyncMessage: ["*"],
      RPMAddMessageListener: ["*"],
      RPMRemoveMessageListener: ["*"],
    },
    "about:privatebrowsing": {
      RPMSendAsyncMessage: [
        "OpenPrivateWindow",
        "SearchBannerDismissed",
        "OpenSearchPreferences",
        "SearchHandoff",
      ],
      RPMSendQuery: [
        "IsPromoBlocked",
        "ShouldShowSearchBanner",
        "ShouldShowPromo",
        "SpecialMessageActionDispatch",
      ],
      RPMAddMessageListener: ["*"],
      RPMRemoveMessageListener: ["*"],
      RPMGetFormatURLPref: [
        "app.support.baseURL",
        "browser.privatebrowsing.vpnpromourl",
      ],
      RPMIsWindowPrivate: ["*"],
      RPMGetBoolPref: ["browser.privatebrowsing.felt-privacy-v1"],
    },
    "about:deleteprofile": {
      RPMSendQuery: ["Profiles:GetDeleteProfileContent"],
      RPMSendAsyncMessage: ["Profiles:CancelDelete", "Profiles:DeleteProfile"],
    },
    "about:editprofile": {
      RPMSendQuery: [
        "Profiles:GetEditProfileContent",
        "Profiles:UpdateProfileTheme",
      ],
      RPMSendAsyncMessage: [
        "Profiles:UpdateProfileName",
        "Profiles:UpdateProfileAvatar",
        "Profiles:OpenDeletePage",
        "Profiles:CloseProfileTab",
        "Profiles:MoreThemes",
        "Profiles:PageHide",
      ],
    },
    "about:newprofile": {
      RPMSendQuery: [
        "Profiles:GetNewProfileContent",
        "Profiles:UpdateProfileTheme",
      ],
      RPMSendAsyncMessage: [
        "Profiles:UpdateProfileName",
        "Profiles:UpdateProfileAvatar",
        "Profiles:DeleteProfile",
        "Profiles:CloseProfileTab",
        "Profiles:MoreThemes",
        "Profiles:PageHide",
      ],
      RPMGetBoolPref: ["browser.profiles.profile-name.updated"],
      RPMGetFormatURLPref: ["app.support.baseURL"],
    },
    "about:protections": {
      RPMSendAsyncMessage: [
        "OpenContentBlockingPreferences",
        "OpenAboutLogins",
        "OpenSyncPreferences",
        "ClearMonitorCache",
        "RecordEntryPoint",
      ],
      RPMSendQuery: [
        "FetchUserLoginsData",
        "FetchMonitorData",
        "FetchContentBlockingEvents",
        "FetchMobileDeviceConnected",
        "GetShowProxyCard",
        "FetchEntryPoint",
        "FetchVPNSubStatus",
        "FetchShowVPNCard",
      ],
      RPMAddMessageListener: ["*"],
      RPMRemoveMessageListener: ["*"],
      RPMSetPref: [
        "browser.contentblocking.report.show_mobile_app",
        "browser.contentblocking.report.hide_vpn_banner",
      ],
      RPMGetBoolPref: [
        "browser.contentblocking.report.lockwise.enabled",
        "browser.contentblocking.report.monitor.enabled",
        "privacy.fingerprintingProtection",
        "privacy.socialtracking.block_cookies.enabled",
        "browser.contentblocking.report.proxy.enabled",
        "privacy.trackingprotection.cryptomining.enabled",
        "privacy.trackingprotection.fingerprinting.enabled",
        "privacy.trackingprotection.enabled",
        "privacy.trackingprotection.socialtracking.enabled",
        "browser.contentblocking.report.show_mobile_app",
        "browser.contentblocking.report.hide_vpn_banner",
        "browser.vpn_promo.enabled",
      ],
      RPMGetStringPref: [
        "browser.contentblocking.category",
        "browser.contentblocking.report.monitor.url",
        "browser.contentblocking.report.monitor.sign_in_url",
        "browser.contentblocking.report.manage_devices.url",
        "browser.contentblocking.report.proxy_extension.url",
        "browser.contentblocking.report.lockwise.mobile-android.url",
        "browser.contentblocking.report.lockwise.mobile-ios.url",
        "browser.contentblocking.report.mobile-ios.url",
        "browser.contentblocking.report.mobile-android.url",
        "browser.contentblocking.report.vpn.url",
        "browser.contentblocking.report.vpn-promo.url",
        "browser.contentblocking.report.vpn-android.url",
        "browser.contentblocking.report.vpn-ios.url",
      ],
      RPMGetIntPref: ["network.cookie.cookieBehavior"],
      RPMGetFormatURLPref: [
        "browser.contentblocking.report.monitor.how_it_works.url",
        "browser.contentblocking.report.lockwise.how_it_works.url",
        "browser.contentblocking.report.monitor.preferences_url",
        "browser.contentblocking.report.monitor.home_page_url",
        "browser.contentblocking.report.social.url",
        "browser.contentblocking.report.cookie.url",
        "browser.contentblocking.report.tracker.url",
        "browser.contentblocking.report.fingerprinter.url",
        "browser.contentblocking.report.cryptominer.url",
      ],
      RPMRecordGleanEvent: ["securityUiProtections"],
    },
    "about:shoppingsidebar": {
      RPMSetPref: [
        "browser.shopping.experience2023.optedIn",
        "browser.shopping.experience2023.active",
        "browser.shopping.experience2023.ads.userEnabled",
        "browser.shopping.experience2023.autoOpen.userEnabled",
        "browser.shopping.experience2023.newPositionCard.hasSeen",
        "browser.shopping.experience2023.sidebarClosedCount",
        "browser.shopping.experience2023.showKeepSidebarClosedMessage",
      ],
      RPMGetFormatURLPref: ["app.support.baseURL"],
      RPMGetIntPref: ["browser.shopping.experience2023.sidebarClosedCount"],
      RPMGetBoolPref: [
        "browser.shopping.experience2023.showKeepSidebarClosedMessage",
        "sidebar.revamp",
        "browser.shopping.experience2023.integratedSidebar",
        "toolkit.shopping.experience2023.defr",
        "browser.shopping.experience2023.newPositionCard.hasSeen",
      ],
    },
    "about:tabcrashed": {
      RPMSendAsyncMessage: ["Load", "closeTab", "restoreTab", "restoreAll"],
      RPMAddMessageListener: ["*"],
      RPMRemoveMessageListener: ["*"],
    },
    "about:welcome": {
      RPMSendAsyncMessage: ["ActivityStream:ContentToMain"],
      RPMAddMessageListener: ["ActivityStream:MainToContent"],
    },
  },

  /**
   * Check if access is allowed to the given feature for a given document.
   * This should be called from within the child process.
   *
   * The feature within the accessMap must list the given aValue, for access to
   * be granted.
   *
   * @param aDocument child process document to call from
   * @param aFeature to feature to check access to
   * @param aValue value that must be included with that feature's allow list
   * @returns true if access is allowed or false otherwise
   */
  checkAllowAccess(aDocument, aFeature, aValue) {
    let principal = aDocument.nodePrincipal;
    // if there is no content principal; deny access
    if (!principal) {
      return false;
    }

    return this.checkAllowAccessWithPrincipal(
      principal,
      aFeature,
      aValue,
      aDocument
    );
  },

  /**
   * Check if access is allowed to the given feature for a given principal.
   * This may be called from within the child or parent process.
   *
   * The feature within the accessMap must list the given aValue, for access to
   * be granted.
   *
   * In the parent process, the passed-in document is expected to be null.
   *
   * @param aPrincipal principal being called from
   * @param aFeature to feature to check access to
   * @param aValue value that must be included with that feature's allow list
   * @param aDocument optional child process document to call from
   * @returns true if access is allowed or false otherwise
   */
  checkAllowAccessWithPrincipal(aPrincipal, aFeature, aValue, aDocument) {
    let accessMapForFeature = this.checkAllowAccessToFeature(
      aPrincipal,
      aFeature,
      aDocument
    );
    if (!accessMapForFeature) {
      console.error(
        "RemotePageAccessManager does not allow access to Feature: ",
        aFeature,
        " for: ",
        aDocument.location
      );

      return false;
    }

    // If the actual value is in the allow list for that feature;
    // allow access
    if (accessMapForFeature.includes(aValue) || accessMapForFeature[0] == "*") {
      return true;
    }

    return false;
  },

  /**
   * Check if a particular feature can be accessed without checking for a
   * specific feature value.
   *
   * @param aPrincipal principal being called from
   * @param aFeature to feature to check access to
   * @param aDocument optional child process document to call from
   * @returns non-null allow list if access is allowed or null otherwise
   */
  checkAllowAccessToFeature(aPrincipal, aFeature, aDocument) {
    let spec;
    if (!aPrincipal.isContentPrincipal) {
      // For the sake of remote pages, when the principal has no uri,
      // we want to access the "real" document URI directly, e.g. if the
      // about: page is sandboxed.
      if (!aDocument) {
        return null;
      }
      if (!aDocument.documentURIObject.schemeIs("about")) {
        return null;
      }
      spec =
        aDocument.documentURIObject.prePath +
        aDocument.documentURIObject.filePath;
    } else {
      if (!aPrincipal.schemeIs("about")) {
        return null;
      }
      spec = aPrincipal.prePath + aPrincipal.filePath;
    }

    // Check if there is an entry for that requestying URI in the accessMap;
    // if not, deny access.
    let accessMapForURI = this.accessMap[spec];
    if (!accessMapForURI) {
      return null;
    }

    // Check if the feature is allowed to be accessed for that URI;
    // if not, deny access.
    return accessMapForURI[aFeature];
  },

  /**
   * This function adds a new page to the access map, but can only
   * be used in a test environment.
   */
  addPage(aUrl, aFunctionMap) {
    if (!Cu.isInAutomation) {
      throw new Error("Cannot only modify privileges during testing");
    }

    if (aUrl in this.accessMap) {
      throw new Error("Cannot modify privileges of existing page");
    }

    this.accessMap[aUrl] = aFunctionMap;
  },
};
