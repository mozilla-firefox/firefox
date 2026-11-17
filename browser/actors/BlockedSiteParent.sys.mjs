/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { EscapablePageParent } from "moz-src:///toolkit/actors/NetErrorParent.sys.mjs";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  SafeBrowsing: "resource://gre/modules/SafeBrowsing.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});

class SafeBrowsingNotificationBox {
  _currentURIBaseDomain = null;

  browser = null;

  constructor(browser, title, buttons) {
    this.browser = browser;

    let uri = browser.currentURI;
    // start tracking host so that we know when we leave the domain
    this._currentURIBaseDomain = this.#getDomainForComparison(uri);

    browser.addProgressListener(this, Ci.nsIWebProgress.NOTIFY_LOCATION);

    this.show(title, buttons);
  }

  async show(title, buttons) {
    let gBrowser = this.browser.getTabBrowser();
    let notificationBox = gBrowser.getNotificationBox(this.browser);
    let value = "blocked-badware-page";

    let previousNotification = notificationBox.getNotificationWithValue(value);
    if (previousNotification) {
      notificationBox.removeNotification(previousNotification);
    }

    let notification = await notificationBox.appendNotification(
      value,
      {
        label: title,
        image: "chrome://global/skin/icons/blocked.svg",
        priority: notificationBox.PRIORITY_CRITICAL_HIGH,
      },
      buttons
    );
    // Persist the notification until the user removes so it
    // doesn't get removed on redirects.
    notification.persistence = -1;
  }

  onLocationChange(webProgress, request, newURI) {
    if (webProgress && !webProgress.isTopLevel) {
      return;
    }
    let newURIBaseDomain = this.#getDomainForComparison(newURI);

    if (
      !this._currentURIBaseDomain ||
      newURIBaseDomain !== this._currentURIBaseDomain
    ) {
      this.cleanup();
    }
  }

  cleanup() {
    if (this.browser) {
      let gBrowser = this.browser.getTabBrowser();
      let notificationBox = gBrowser.getNotificationBox(this.browser);
      let notification = notificationBox.getNotificationWithValue(
        "blocked-badware-page"
      );
      if (notification) {
        notificationBox.removeNotification(notification, false);
      }
      this.browser.removeProgressListener(
        this,
        Ci.nsIWebProgress.NOTIFY_LOCATION
      );
      this.browser.safeBrowsingNotification = null;
      this.browser = null;
    }
    this._currentURIBaseDomain = null;
  }

  #getDomainForComparison(uri) {
    try {
      return Services.eTLD.getBaseDomain(uri);
    } catch (e) {
      // If we can't get the base domain, fallback to use host instead. However,
      // host is sometimes empty when the scheme is file. In this case, just use
      // spec.
      return uri.asciiHost || uri.asciiSpec;
    }
  }
}

SafeBrowsingNotificationBox.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsIWebProgressListener",
  "nsISupportsWeakReference",
]);

export class BlockedSiteParent extends EscapablePageParent {
  receiveMessage(msg) {
    switch (msg.name) {
      case "Browser:SiteBlockedError":
        this._onAboutBlocked(
          msg.data.elementId,
          msg.data.reason,
          this.browsingContext === this.browsingContext.top,
          msg.data.blockedInfo
        );
        break;
    }
  }

  _onAboutBlocked(elementId, reason, isTopFrame, blockedInfo) {
    let browser = this.browsingContext.top.embedderElement;
    if (!browser) {
      return;
    }
    // Depending on what page we are displaying here (malware/phishing/unwanted)
    // use the right strings and links for each.
    let bucketName = "";
    let sendTelemetry = false;
    if (reason === "malware") {
      sendTelemetry = true;
      bucketName = "WARNING_MALWARE_PAGE_";
    } else if (reason === "phishing") {
      sendTelemetry = true;
      bucketName = "WARNING_PHISHING_PAGE_";
    } else if (reason === "unwanted") {
      sendTelemetry = true;
      bucketName = "WARNING_UNWANTED_PAGE_";
    } else if (reason === "harmful") {
      sendTelemetry = true;
      bucketName = "WARNING_HARMFUL_PAGE_";
    }
    let nsISecTel = Ci.IUrlClassifierUITelemetry;
    bucketName += isTopFrame ? "TOP_" : "FRAME_";

    switch (elementId) {
      case "goBackButton":
        if (sendTelemetry) {
          Glean.urlclassifier.uiEvents.accumulateSingleSample(
            nsISecTel[bucketName + "GET_ME_OUT_OF_HERE"]
          );
        }
        this.leaveErrorPage(browser, /* Never go back */ false);
        break;
      case "ignore_warning_link":
        if (Services.prefs.getBoolPref("browser.safebrowsing.allowOverride")) {
          if (sendTelemetry) {
            Glean.urlclassifier.uiEvents.accumulateSingleSample(
              nsISecTel[bucketName + "IGNORE_WARNING"]
            );
          }
          this.ignoreWarningLink(reason, blockedInfo);
        }
        break;
    }
  }

  ignoreWarningLink(reason, blockedInfo) {
    let { browsingContext } = this;
    // Add a notify bar before allowing the user to continue through to the
    // site, so that they don't lose track after, e.g., tab switching.
    // We can't use browser.contentPrincipal which is principal of about:blocked
    // Create one from uri with current principal origin attributes

    // Remove the query to avoid leaking sensitive data
    let uri = browsingContext.currentURI.mutate().setQuery("").finalize();
    let principal = Services.scriptSecurityManager.createContentPrincipal(
      uri,
      browsingContext.currentWindowGlobal.documentPrincipal.originAttributes
    );
    Services.perms.addFromPrincipal(
      principal,
      "safe-browsing",
      Ci.nsIPermissionManager.ALLOW_ACTION,
      Ci.nsIPermissionManager.EXPIRE_SESSION
    );

    let buttons = [
      {
        "l10n-id": "safebrowsing-get-me-out-of-here-button",
        callback: () => {
          let browser = browsingContext.top.embedderElement;
          this.leaveErrorPage(browser, /* Never go back */ false);
        },
      },
    ];

    let title;
    let chromeWin = browsingContext.topChromeWindow;
    if (reason === "malware") {
      let reportUrl = lazy.SafeBrowsing.getReportURL("MalwareMistake", {
        ...blockedInfo,
        uri: uri.asciiSpec,
      });
      title = { "l10n-id": "safebrowsing-reported-attack-site-message" };
      // There's no button if we can not get report url, for example if the provider
      // of blockedInfo is not Google
      if (reportUrl) {
        buttons[1] = {
          "l10n-id": "safebrowsing-not-an-attack-site-button",
          callback() {
            lazy.URILoadingHelper.openTrustedLinkIn(
              chromeWin,
              reportUrl,
              "tab"
            );
          },
        };
      }
    } else if (reason === "phishing") {
      let reportUrl = lazy.SafeBrowsing.getReportURL("PhishMistake", {
        ...blockedInfo,
        uri: uri.asciiSpec,
      });
      title = { "l10n-id": "safebrowsing-deceptive-site-message" };
      // There's no button if we can not get report url, for example if the provider
      // of blockedInfo is not Google
      if (reportUrl) {
        buttons[1] = {
          "l10n-id": "safebrowsing-not-a-deceptive-site-button",
          callback() {
            lazy.URILoadingHelper.openTrustedLinkIn(
              chromeWin,
              reportUrl,
              "tab"
            );
          },
        };
      }
    } else if (reason === "unwanted") {
      title = { "l10n-id": "safebrowsing-reported-unwanted-site-message" };
      // There is no button for reporting errors since Google doesn't currently
      // provide a URL endpoint for these reports.
    } else if (reason === "harmful") {
      title = { "l10n-id": "safebrowsing-reported-harmful-site-message" };
      // There is no button for reporting errors since Google doesn't currently
      // provide a URL endpoint for these reports.
    }

    let browser = browsingContext.top.embedderElement;
    browser.safeBrowsingNotification?.cleanup();
    browser.safeBrowsingNotification = new SafeBrowsingNotificationBox(
      browser,
      title,
      buttons
    );

    // Allow users to override and continue through to the site.
    browsingContext.reload(Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CLASSIFIER);
  }
}
