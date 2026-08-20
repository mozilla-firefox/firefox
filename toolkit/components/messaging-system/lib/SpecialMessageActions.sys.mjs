/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const DOH_DOORHANGER_DECISION_PREF = "doh-rollout.doorhanger-decision";
const NETWORK_TRR_MODE_PREF = "network.trr.mode";

// Allowlist of about page IDs that OPEN_ABOUT_PAGE is permitted to open.
// Sourced from pages listened in AboutRedirector.
const ALLOWED_ABOUT_PAGES = new Set([
  "addons",
  "profiles",
  "translations",
  "keyboard",
  "logins",
  "preferences",
  "privatebrowsing",
  "protections",
  "referrals",
  "settings",
  "welcome",
  "newtab",
  "home",
  "robots",
]);

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AIWindow:
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  AIWindowUI:
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  CustomIconManager:
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    "moz-src:///browser/components/shell/CustomIconManager.sys.mjs",
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  ExperimentAPI: "resource://nimbus/ExperimentAPI.sys.mjs",
  FxAccounts: "resource://gre/modules/FxAccounts.sys.mjs",
  GenAI: "resource:///modules/GenAI.sys.mjs",
  ICON_CATALOG:
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    "moz-src:///browser/components/shell/CustomIconManager.sys.mjs",
  IPProtection:
    // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  MessagingSystemAllowlists:
    "resource://messaging-system/lib/MessagingSystemAllowlists.sys.mjs",
  MigrationUtils: "resource:///modules/MigrationUtils.sys.mjs",
  ON_SERVICE_ENABLED_NOTIFICATION:
    "resource://gre/modules/FxAccountsCommon.sys.mjs",
  PlacesTransactions: "resource://gre/modules/PlacesTransactions.sys.mjs",
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  PlacesUIUtils: "moz-src:///browser/components/places/PlacesUIUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  Referrals: "resource:///modules/referrals/Referrals.sys.mjs",
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  SelectableProfileService:
    "resource:///modules/profiles/SelectableProfileService.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  Spotlight: "resource:///modules/asrouter/Spotlight.sys.mjs",
  // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
  TaskbarTabs: "resource:///modules/taskbartabs/TaskbarTabs.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
  UITour: "moz-src:///browser/components/uitour/UITour.sys.mjs",
  LaunchOnLogin: "resource://gre/modules/LaunchOnLogin.sys.mjs",
});

export const SpecialMessageActions = {
  // This is overridden by ASRouter.init
  blockMessageById() {
    throw new Error("ASRouter not intialized yet");
  },

  /**
   * loadAddonIconInURLBar - load addons-notification icon by displaying
   * box containing addons icon in urlbar. See Bug 1513882
   *
   * @param  {Browser} browser browser element for showing addons icon
   */
  loadAddonIconInURLBar(browser) {
    if (!browser) {
      return;
    }
    const chromeDoc = browser.ownerDocument;
    let notificationPopupBox = chromeDoc.getElementById(
      "notification-popup-box"
    );
    if (!notificationPopupBox) {
      return;
    }
    if (
      notificationPopupBox.style.display === "none" ||
      notificationPopupBox.style.display === ""
    ) {
      notificationPopupBox.style.display = "block";
    }
  },

  /**
   *
   * @param {Browser} browser The revelant Browser
   * @param {string} url URL to look up install location
   * @param {string} telemetrySource Telemetry information to pass to getInstallForURL
   */
  async installAddonFromURL(browser, url, telemetrySource = "amo") {
    try {
      this.loadAddonIconInURLBar(browser);
      const aUri = Services.io.newURI(url);
      const systemPrincipal =
        Services.scriptSecurityManager.getSystemPrincipal();

      // AddonManager installation source associated to the addons installed from activitystream's CFR
      // and RTAMO (source is going to be "amo" if not configured explicitly in the message provider).
      const telemetryInfo = { source: telemetrySource };
      const install = await lazy.AddonManager.getInstallForURL(aUri.spec, {
        telemetryInfo,
      });
      await lazy.AddonManager.installAddonFromWebpage(
        "application/x-xpinstall",
        browser,
        systemPrincipal,
        install
      );
    } catch (e) {
      console.error(e);
    }
  },

  /**
   * Pin Firefox to taskbar.
   *
   * @param {Window} window
   * @param {object} options
   * @param {boolean} options.privatePin Whether the pinned launcher should open
   *   a private window. Defaults to false.
   * @param {boolean} options.fireAndForget User confirmation is often necessary
   *   due to OS restrictions. This means an OS notification appears asking for
   *   confirmation. By default, this API will only resolve when the user has
   *   confirmed the action, so it will hang until the prompt is dismissed. This
   *   parameter makes the API resolve after the prompt is shown, but before the
   *   user has confirmed or rejected the action. Defaults to false.
   */
  pinFirefoxToTaskbar(window, { privatePin, fireAndForget } = {}) {
    return window.getShellService().pinToTaskbar(privatePin, fireAndForget);
  },

  /**
   * Pin current application to Windows start menu.
   */
  pinToStartMenu(window) {
    return window.getShellService().pinToStartMenu();
  },

  /**
   * Pin a web app (Taskbar Tab) to the taskbar by manifest data, without
   * requiring the site to be open or previously visited.
   *
   * Returns true if the tab was newly created and pinned, null if a Taskbar
   * Tab for this URL already existed, or false if an error occurred.
   *
   * NOTE: findOrCreateTaskbarTab resolves once the taskbar tab is registered,
   * not once it has actually been pinned. The internal pin step (and the
   * Windows 11 OS pin dialog) runs after the promise settles, so we return
   * before the user has accepted or rejected the OS prompt.
   *
   * @param {object} data
   * @param {string} data.url - The URL of the web app (HTTP/HTTPS only).
   * @param {string} data.name - Display name for the web app.
   * @param {string} data.iconUrl - URL of the icon (256x256 PNG recommended).
   * @returns {Promise<boolean|null>}
   */
  async pinTaskbarTab({ url, name, iconUrl }) {
    let uri;
    try {
      uri = Services.io.newURI(url);
    } catch (e) {
      return false;
    }
    if (uri.scheme !== "https" && uri.scheme !== "http") {
      return false;
    }

    const manifest = {
      name,
      start_url: url,
      scope: uri.prePath + "/",
      // NOTE: Manifest icon support (bug 1979462) is not yet implemented.
      // Until that lands, the icon may fall back to the favicon service.
      icons: [{ src: iconUrl, sizes: "256x256", type: "image/png" }],
    };

    try {
      await lazy.TaskbarTabs.findOrCreateTaskbarTab(uri, 0, {
        manifest,
        ensurePinned: true,
      });
      return true;
    } catch (e) {
      console.error("Failed to pin Taskbar Tab:", e);
      return false;
    }
  },

  /**
   *  Set browser as the operating system default browser.
   *
   *  @param {Window} window Reference to a window object
   */
  async setDefaultBrowser(window) {
    await window.getShellService().setAsDefault();
  },

  /**
   * Set browser as the default PDF handler.
   *
   * @param {Window} window Reference to a window object
   */
  async setDefaultPDFHandler(
    window,
    onlyIfKnownBrowser = false,
    openInFirefox = false
  ) {
    await window
      .getShellService()
      .setAsDefaultPDFHandler(onlyIfKnownBrowser, openInFirefox);
  },

  /**
   * Set browser as the default handler for a protocol (scheme).
   *
   * @param {Window} window Reference to a window object
   * @param {string} protocol The protocol to claim, e.g. "mailto"
   * @param {string} [url] URL passed to the OS default-app picker
   * @param {boolean} [openInFirefox] Whether to open the protocol's default URL
   *   in Firefox after the user picks it
   */
  async setDefaultProtocolHandler(
    window,
    protocol,
    url,
    openInFirefox = false
  ) {
    await window
      .getShellService()
      .setAsDefaultProtocolHandler(protocol, url, openInFirefox);
  },

  /**
   * Reset browser homepage and newtab to default with a certain section configuration
   *
   * @param {"default"|null} home Value to set for browser homepage
   * @param {"default"|null} newtab Value to set for browser newtab
   * @param {obj} layout Configuration options for newtab sections
   * @returns {undefined}
   */
  configureHomepage({ homePage = null, newtab = null, layout = null }) {
    // Homepage can be default, blank or a custom url
    if (homePage === "default") {
      Services.prefs.clearUserPref("browser.startup.homepage");
    }
    // Newtab page can only be default or blank
    if (newtab === "default") {
      Services.prefs.clearUserPref("browser.newtabpage.enabled");
    }
    if (layout) {
      // Existing prefs that interact with the newtab page layout, we default to true
      // or payload configuration
      let newtabConfigurations = [
        [
          // controls the search bar
          "browser.newtabpage.activity-stream.showSearch",
          layout.search,
        ],
        [
          // controls the topsites
          "browser.newtabpage.activity-stream.feeds.topsites",
          layout.topsites,
          // User can control number of topsite rows
          ["browser.newtabpage.activity-stream.topSitesRows"],
        ],
        [
          // controls the highlights section
          "browser.newtabpage.activity-stream.feeds.section.highlights",
          layout.highlights,
          // User can control number of rows and highlight sources
          [
            "browser.newtabpage.activity-stream.section.highlights.rows",
            "browser.newtabpage.activity-stream.section.highlights.includeVisited",
            "browser.newtabpage.activity-stream.section.highlights.includeDownloads",
            "browser.newtabpage.activity-stream.section.highlights.includeBookmarks",
          ],
        ],
        [
          // controls the topstories section
          "browser.newtabpage.activity-stream.feeds.system.topstories",
          layout.topstories,
        ],
      ].filter(
        // If a section has configs that the user changed we will skip that section
        ([, , sectionConfigs]) =>
          !sectionConfigs ||
          sectionConfigs.every(
            prefName => !Services.prefs.prefHasUserValue(prefName)
          )
      );

      for (let [prefName, prefValue] of newtabConfigurations) {
        Services.prefs.setBoolPref(prefName, prefValue);
      }
    }
  },

  /**
   * Set a target pref's value to the value of a source pref.
   *
   * @param {string} targetPref - The name of a pref to be updated.
   * @param {string} sourcePref - The name of the source pref whose value will be copied to the target pref.
   */
  copyPrefValue(targetPref, sourcePref) {
    const sourceType = Services.prefs.getPrefType(sourcePref);
    const targetType = Services.prefs.getPrefType(targetPref);
    if (
      targetType !== Services.prefs.PREF_INVALID &&
      targetType !== sourceType
    ) {
      throw new Error(
        `Special message action with type SET_PREF(copyFromPref), target pref "${targetPref}" has type ${targetType} which does not match source pref "${sourcePref}" type ${sourceType}.`
      );
    }
    switch (sourceType) {
      case Services.prefs.PREF_STRING:
        Services.prefs.setStringPref(
          targetPref,
          Services.prefs.getStringPref(sourcePref)
        );
        break;
      case Services.prefs.PREF_INT:
        Services.prefs.setIntPref(
          targetPref,
          Services.prefs.getIntPref(sourcePref)
        );
        break;
      case Services.prefs.PREF_BOOL:
        Services.prefs.setBoolPref(
          targetPref,
          Services.prefs.getBoolPref(sourcePref)
        );
        break;
      default:
        throw new Error(
          `Special message action with type SET_PREF(copyFromPref), pref of "${sourcePref}" is invalid or not a supported type.`
        );
    }
  },

  /**
   * Set prefs with special message actions
   *
   * @param {object} pref - A pref to be updated.
   * @param {string} pref.name - The name of the pref to be updated
   * @param {string} [pref.value] - The value of the pref to be updated. If not included, the pref will be reset.
   * @param {boolean} onImpression - Whether the setPref action was triggered on
   * message impression and not by direct user interaction.
   */
  setPref(pref, onImpression = false) {
    // Array of prefs that are allowed to be edited by SET_PREF
    const allowedPrefs = [
      "browser.aboutwelcome.didSeeFinalScreen",
      "browser.sessionstore.newTabOnRestore",
      "browser.smartwindow.enabled",
      "browser.smartwindow.firstrun.hasCompleted",
      "browser.smartwindow.firstrun.modelChoice",
      "browser.smartwindow.isDefaultWindow",
      "browser.smartwindow.sidebar.openByDefault",
      "browser.smartwindow.memories.generateFromConversation",
      "browser.smartwindow.memories.generateFromHistory",
      "browser.crashReports.unsubmittedCheck.autoSubmit2",
      "browser.dataFeatureRecommendations.enabled",
      "browser.ipProtection.bandwidth.enabled",
      "browser.ipProtection.blockIPProtectionCallouts",
      "browser.ipProtection.enabled",
      "browser.ipProtection.optedOut",
      "browser.migrate.content-modal.about-welcome-behavior",
      "browser.migrate.content-modal.import-all.enabled",
      "browser.migrate.preferences-entrypoint.enabled",
      "browser.shell.checkDefaultBrowser",
      "browser.shell.setDefaultGuidanceNotifications",
      "browser.startup.homepage",
      "browser.startup.page",
      "browser.privateWindowSeparation.enabled",
      "browser.firefox-view.feature-tour",
      "browser.pdfjs.feature-tour",
      "browser.newtab.feature-tour",
      "datareporting.healthreport.uploadEnabled",
      "datareporting.policy.currentPolicyVersion",
      "datareporting.policy.dataSubmissionPolicyAcceptedVersion",
      "datareporting.policy.dataSubmissionPolicyNotifiedTime",
      "datareporting.policy.minimumPolicyVersion",
      "messaging-system.askForFeedback",
      "browser.toolbars.bookmarks.visibility",
      "sidebar.verticalTabs",
      "sidebar.revamp",
      "sidebar.visibility",
      "termsofuse.acceptedVersion",
      "termsofuse.acceptedDate",
      "termsofuse.firstAcceptedDate",
      "termsofuse.currentVersion",
      "termsofuse.minimumVersion",
      "privacy.trackingprotection.allow_list.baseline.enabled",
      "privacy.trackingprotection.allow_list.convenience.enabled",
    ];

    // Array of prefs that are allowed to be edited when SET_PREF is called on
    // message impression, rather than by an explicit user action. Currently,
    // only prefs created on the fly are allowed. This is to ensure that adding
    // the abililty to set any in-tree prefs with this feature undergoes code
    // review.
    const allowedSetOnImpressionPrefs = [
      "termsofuse.firstAcceptedDate",
      "termsofuse.acceptedDate",
    ];

    // allowedPrefs above is the in-tree baseline. It can be extended off-train
    // via Remote Settings, but not for onImpression prefs, which stay
    // deliberately restricted to prefs reviewed in-tree, and not for the prefs
    // in MessagingSystemBlocklists.sys.mjs, which are filtered out before they
    // reach this getter. MessagingSystemAllowlists.sys.mjs documents how the
    // two in-tree lists and the collection resolve against each other. This
    // check is synchronous and does not wait on the Remote Settings collection
    // to load (see MessagingSystemAllowlists.ensureInit). Callers that dispatch
    // SET_PREF outside of ASRouter's own message routing, namely about:welcome
    // and Spotlight, do not await ASRouter's init sequence, so a pref granted
    // only through Remote Settings may not be recognized yet if it fires before
    // the collection has loaded for this session. If that happens the pref is
    // simply namespaced like any other unlisted pref rather than being set
    // under its real name. Note this case is unlikely outside of automated
    // scenarios since user action is required to fire a SET_PREF action in
    // these scenarios (onImpression prefs are not extendable via this method).
    const allowedPrefsSet = onImpression
      ? new Set(allowedSetOnImpressionPrefs)
      : new Set([
          ...allowedPrefs,
          ...lazy.MessagingSystemAllowlists.getAllowedPrefs(),
        ]);

    if (
      !allowedPrefsSet.has(pref.name) &&
      !pref.name.startsWith("messaging-system-action.")
    ) {
      pref.name = `messaging-system-action.${pref.name}`;
    }
    // If pref has no value, reset it, otherwise set it to desired value
    switch (typeof pref.value) {
      case "object":
        if (pref.value.timestamp) {
          Services.prefs.setStringPref(pref.name, Date.now().toString());
        } else if (pref.value.copyFromPref) {
          this.copyPrefValue(pref.name, pref.value.copyFromPref);
        } else {
          Services.prefs.clearUserPref(pref.name);
        }
        break;
      case "undefined":
        Services.prefs.clearUserPref(pref.name);
        break;
      case "string":
        Services.prefs.setStringPref(pref.name, pref.value);
        break;
      case "number":
        Services.prefs.setIntPref(pref.name, pref.value);
        break;
      case "boolean":
        Services.prefs.setBoolPref(pref.name, pref.value);
        break;
      default:
        throw new Error(
          `Special message action with type SET_PREF, pref of "${pref.name}" is an unsupported type.`
        );
    }
  },

  /**
   * Destroy UI widgets with special message actions
   *
   * @param {string} widgetId - The ID of the widget to be destroyed.
   */
  destroyUIWidget(widgetId) {
    const allowedWidgetIds = ["fxms-bmb-button"];

    if (!allowedWidgetIds.includes(widgetId)) {
      return;
    }

    lazy.CustomizableUI.destroyWidget(widgetId);
  },

  /**
   * Open an FxA sign-in page and automatically close it once sign-in
   * completes.
   *
   * @param {any=} data
   * @param {Browser} browser the xul:browser rendering the page
   * @returns {Promise<boolean>} true if the user signed in, false otherwise
   */
  async fxaSignInFlow(data, browser) {
    if (!(await lazy.FxAccounts.canConnectAccount())) {
      return false;
    }
    // In practice, all FxA signin flows will have a "service", because that param dictates the
    // UI shown by FxA. But to be extra cautious, this code treats it as optional.
    let extraParams = data?.extraParams;
    let neededService = extraParams?.service;
    if (neededService) {
      delete extraParams.service;
    }
    const url = await lazy.FxAccounts.config.promiseConnectAccountURI(
      neededService || "sync",
      data?.entrypoint || "activity-stream-firstrun",
      extraParams || {}
    );

    let window = browser.documentGlobal;

    let fxaBrowser = await new Promise(resolve => {
      window.openLinkIn(url, data?.where || "tab", {
        private: false,
        triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
          {}
        ),
        resolveOnContentBrowserCreated: resolve,
        forceForeground: true,
      });
    });

    let gBrowser = fxaBrowser.getTabBrowser();
    let fxaTab = gBrowser.getTabForBrowser(fxaBrowser);

    let sawNeededService = false;
    let didSignIn = await new Promise(resolve => {
      // We're going to be setting up a listener and an observer for this
      // mechanism.
      //
      // 1. An event listener for the TabClose event, to detect if the user
      //    closes the tab before completing sign-in
      // 2. An nsIObserver that listens for an FxA "service" being enabled.
      // 3. An nsIObserver that listens for the UIState for FxA to reach
      //    STATUS_SIGNED_IN.
      //
      // We want to clean up both the listener and observer when all of this
      // is done.
      //
      // We use an AbortController to make it easier to manage the cleanup.
      let controller = new AbortController();
      let { signal } = controller;

      // This nsIObserver will listen for the UIState status to change to
      // STATUS_SIGNED_IN as our definitive signal that FxA sign-in has
      // completed. It will then resolve the outer Promise to `true`.
      let fxaObserver = {
        QueryInterface: ChromeUtils.generateQI([
          Ci.nsIObserver,
          Ci.nsISupportsWeakReference,
        ]),

        observe(aSubject, aTopic, aData) {
          switch (aTopic) {
            case lazy.UIState.ON_UPDATE: {
              let state = lazy.UIState.get();
              if (
                (!neededService || sawNeededService) &&
                state.status === lazy.UIState.STATUS_SIGNED_IN
              ) {
                // We completed sign-in, so tear down our listener / observer and resolve
                // didSignIn to true.
                controller.abort();
                resolve(true);
              }
              break;
            }
            case lazy.ON_SERVICE_ENABLED_NOTIFICATION: {
              if (aData === neededService) {
                sawNeededService = true;
              }
              break;
            }
          }
        },
      };

      // The TabClose event listener _can_ accept the AbortController signal,
      // which will then remove the event listener after controller.abort is
      // called.
      fxaTab.addEventListener(
        "TabClose",
        () => {
          // If the TabClose event was fired before the event handler was
          // removed, this means that the tab was closed and sign-in was
          // not completed, which means we should resolve didSignIn to false.
          controller.abort();
          resolve(false);
        },
        { once: true, signal }
      );

      fxaTab.documentGlobal.addEventListener("unload", () => {
        // If the hosting window unload event was fired before the event handler
        // was removed, this means that the window was closed and sign-in was
        // not completed, which means we should resolve didSignIn to false.
        controller.abort();
        resolve(false);
      });

      Services.obs.addObserver(
        fxaObserver,
        lazy.ON_SERVICE_ENABLED_NOTIFICATION
      );
      Services.obs.addObserver(fxaObserver, lazy.UIState.ON_UPDATE);

      // Unfortunately, nsIObserverService.addObserver does not accept an
      // AbortController signal as a parameter, so instead we listen for the
      // abort event on the signal to remove the observers.
      signal.addEventListener(
        "abort",
        () => {
          Services.obs.removeObserver(
            fxaObserver,
            lazy.ON_SERVICE_ENABLED_NOTIFICATION
          );
          Services.obs.removeObserver(fxaObserver, lazy.UIState.ON_UPDATE);
        },
        { once: true }
      );
    });

    // If the user completed sign-in, we'll close the fxaBrowser tab for
    // them to bring them back to the about:welcome flow.
    //
    // If the sign-in page was loaded in a new window, this will close the
    // tab for that window. That will close the window as well if it's the
    // last tab in that window.
    if (didSignIn && data?.autoClose !== false) {
      gBrowser.removeTab(fxaTab);
    }

    return didSignIn;
  },

  /**
   * Sets the visibility of bookmarks toolbar.
   *
   * @param {Window} window Reference to a window object
   * @param {string} visibility Visibility options which can be "always", "newtab", or "never"
   */
  setBookmarksToolbarVisibility(window, visibility) {
    Services.prefs.setCharPref(
      "browser.toolbars.bookmarks.visibility",
      visibility
    );

    lazy.CustomizableUI.setToolbarVisibility(
      window.document.getElementById("PersonalToolbar").id,
      visibility,
      false
    );
  },

  /**
   * Bookmarks the current tab.
   *
   * @param {Window} window Reference to a window object
   * @param {boolean} shouldHideDialog True if bookmark dialog should be hidden
   * @param {boolean} shouldHideConfirmationHint True if bookmark confirmation hint should be hidden
   */
  async bookmarkCurrentTab(
    window,
    shouldHideDialog = false,
    shouldHideConfirmationHint = false
  ) {
    if (!window.top.gBrowser) {
      return;
    }

    // Bookmark current tab without showing the bookmark dialog
    if (shouldHideDialog) {
      let browser = window.top.gBrowser.selectedBrowser;
      let url = URL.fromURI(Services.io.createExposableURI(browser.currentURI));

      let info = await lazy.PlacesUtils.bookmarks.fetch({ url });
      let parentGuid = await lazy.PlacesUIUtils.defaultParentGuid;
      info = { url, parentGuid };
      let charset = null;
      let isErrorPage = false;

      // Check if the current tab is an error page,
      // if so, attempt to get the title from the history entry
      if (browser.documentURI) {
        isErrorPage = /^about:(neterror|certerror|blocked)/.test(
          browser.documentURI.spec
        );
      }
      try {
        if (isErrorPage) {
          let entry = await lazy.PlacesUtils.history.fetch(browser.currentURI);
          if (entry) {
            info.title = entry.title;
          }
        } else {
          info.title = browser.contentTitle;
        }
        info.title = info.title || url.href;
        charset = browser.characterSet;
      } catch (e) {
        console.error(e);
      }

      // Creates new bookmark
      info.guid = await lazy.PlacesTransactions.NewBookmark(info).transact();

      if (charset) {
        lazy.PlacesUIUtils.setCharsetForPage(url, charset, window).catch(
          console.error
        );
      }
      window.gURLBar.handleRevert();

      if (!shouldHideConfirmationHint) {
        window.StarUI.showConfirmation();
      }
    } else {
      // Bookmarks current tab with bookmark dialog shown
      window.top.PlacesCommandHook.bookmarkTabs([
        window.top.gBrowser.selectedTab,
      ]);
    }
  },

  /**
   * Change the browser icon to the one identified by `id` via
   * CustomIconManager. Icon IDs that are not in the catalog are ignored. The
   * "default" id reverts to the browser's own icon (the no-override state).
   *
   * CustomIconManager is only packaged on Windows, so this is a no-op on other
   * platforms to avoid importing a module that does not exist.
   *
   * @param {string} id A key in ICON_CATALOG.
   */
  async setBrowserIcon(id) {
    if (AppConstants.platform !== "win") {
      return;
    }
    if (!lazy.ICON_CATALOG[id]) {
      return;
    }
    if (id === "default") {
      await lazy.CustomIconManager.revert();
      return;
    }
    await lazy.CustomIconManager.apply(id);
  },

  async createAndOpenProfile() {
    await lazy.SelectableProfileService.createNewProfile(
      true,
      null,
      "asrouter"
    );
  },

  async submitOnboardingOptOutPing() {
    // `onboarding-opt-out` pings can always be sent.
    GleanPings.onboardingOptOut.setEnabled(true);

    // The `onboarding-opt-out` ping does not include any info sections, and
    // therefore needs to capture experiments and rollouts independently.  This
    // data layout agrees with the `nimbus-targeting-context` ping for ease of
    // analysis.
    let ctx = lazy.ExperimentAPI.manager.createTargetingContext();

    Glean.onboardingOptOut.activeExperiments.set(await ctx.activeExperiments);
    Glean.onboardingOptOut.activeRollouts.set(await ctx.activeRollouts);
    Glean.onboardingOptOut.enrollmentsMap.set(
      Object.entries(await ctx.enrollmentsMap).map(
        ([experimentSlug, branchSlug]) => ({
          experimentSlug,
          branchSlug,
        })
      )
    );

    GleanPings.onboardingOptOut.submit("set_upload_enabled");
  },

  async handleMultiAction(actions, browser, orderedExecution) {
    if (orderedExecution) {
      let hasFailed = false;
      for (const action of actions) {
        try {
          // If action requires previous actions to succeed, check to see if a previous action has failed
          if (action.requiresPrevious && hasFailed) {
            continue; // Skip this action if previous actions did not succeed
          }
          let result = await this.handleAction(action, browser);
          if (result === false) {
            hasFailed = true;
          }
        } catch (err) {
          console.error("Error in MULTI_ACTION event:", err);
          throw err;
        }
      }
      return;
    }
    // If order doesn't matter, allow actions to run concurrently
    await Promise.all(
      actions.map(async action => {
        try {
          await this.handleAction(action, browser);
        } catch (err) {
          throw new Error(`Error in MULTI_ACTION event: ${err.message}`);
        }
      })
    );
  },

  /**
   * Processes "Special Message Actions", which are definitions of behaviors such as opening tabs
   * installing add-ons, or focusing the awesome bar that are allowed to can be triggered from
   * Messaging System interactions.
   *
   * @param {{type: string, data?: any}} action User action defined in message JSON.
   * @param browser {Browser} The browser most relevant to the message.
   * @returns {Promise<unknown>} Type depends on action type. See cases below.
   */
  /* eslint-disable-next-line complexity */
  async handleAction(action, browser) {
    const window = browser.documentGlobal;
    switch (action.type) {
      case "SHOW_MIGRATION_WIZARD":
        lazy.MigrationUtils.showMigrationWizard(window, {
          entrypoint: lazy.MigrationUtils.MIGRATION_ENTRYPOINTS.NEWTAB,
          migratorKey: action.data?.source,
        });
        break;
      case "OPEN_PRIVATE_BROWSER_WINDOW":
        // Forcefully open about:privatebrowsing
        window.OpenBrowserWindow({ private: true });
        break;
      case "OPEN_SIDEBAR":
        window.SidebarController.show(action.data);
        break;
      case "OPEN_URL":
        window.openLinkIn(
          Services.urlFormatter.formatURL(action.data.args),
          action.data.where || "current",
          {
            private: false,
            triggeringPrincipal:
              Services.scriptSecurityManager.createNullPrincipal({}),
            width: action.data.width,
            height: action.data.height,
          }
        );
        break;
      case "OPEN_ABOUT_PAGE": {
        let aboutPageURL = new URL(`about:${action.data.args}`);
        if (!ALLOWED_ABOUT_PAGES.has(aboutPageURL.pathname)) {
          throw new Error(
            `SpecialMessageActions: OPEN_ABOUT_PAGE disallows about:${action.data.args}`
          );
        }
        if (action.data.entrypoint) {
          aboutPageURL.search = action.data.entrypoint;
        }
        window.openTrustedLinkIn(
          aboutPageURL.toString(),
          action.data.where || "tab"
        );
        break;
      }
      case "OPEN_FIREFOX_VIEW":
        window.FirefoxViewHandler.openTab();
        break;
      case "OPEN_TAB_IN_SPLITVIEW": {
        Services.prefs.setBoolPref("browser.tabs.splitView.enabled", true);
        let newTab = window.gBrowser.addTrustedTab("about:opentabs");
        window.gBrowser.addTabSplitView([window.gBrowser.selectedTab, newTab]);
        break;
      }
      case "OPEN_PREFERENCES_PAGE":
        window.openPreferences(
          action.data.category || action.data.args,
          action.data.entrypoint && {
            urlParams: { entrypoint: action.data.entrypoint },
          }
        );
        break;
      case "OPEN_APPLICATIONS_MENU":
        lazy.UITour.showMenu(window, action.data.args);
        break;
      case "HIGHLIGHT_FEATURE": {
        const highlight = await lazy.UITour.getTarget(window, action.data.args);
        if (highlight) {
          await lazy.UITour.showHighlight(window, highlight, "none", {
            autohide: true,
          });
        }
        break;
      }
      case "INSTALL_ADDON_FROM_URL":
        await this.installAddonFromURL(
          browser,
          action.data.url,
          action.data.telemetrySource
        );
        break;
      case "PIN_FIREFOX_TO_TASKBAR":
        await this.pinFirefoxToTaskbar(window, action.data);
        break;
      case "PIN_TASKBAR_TAB":
        return this.pinTaskbarTab(action.data);
      case "PIN_FIREFOX_TO_START_MENU":
        await this.pinToStartMenu(window);
        break;
      case "PIN_AND_DEFAULT":
        // If setDefaultBrowser is called before the pinning action, the OS will
        // focus the Settings window, preventing the pinning confirmation toast
        // from appearing. So we must pin first and await the action. There are
        // two ways of doing this, however. By default, the set default prompt
        // and Settings window will not appear until the user has accepted or
        // rejected the pinning toast. The `fireAndForget` param will wait only
        // for the pinning toast to be shown, so both toasts will appear at
        // approximately the same time, but timed to avoid the race.
        await this.pinFirefoxToTaskbar(window, action.data);
        await this.setDefaultBrowser(window);
        break;
      case "SET_DEFAULT_BROWSER":
        await this.setDefaultBrowser(window);
        break;
      case "SET_DEFAULT_PDF_HANDLER":
        await this.setDefaultPDFHandler(
          window,
          action.data?.onlyIfKnownBrowser ?? false,
          action.data?.openInFirefox ?? false
        );
        break;
      case "DECLINE_DEFAULT_PDF_HANDLER":
        Services.prefs.setBoolPref(
          "browser.shell.checkDefaultPDF.silencedByUser",
          true
        );
        break;
      case "SET_DEFAULT_PROTOCOL_HANDLER":
        await this.setDefaultProtocolHandler(
          window,
          action.data?.protocol,
          action.data?.url,
          action.data?.openInFirefox ?? false
        );
        break;
      case "CONFIRM_LAUNCH_ON_LOGIN":
        await lazy.LaunchOnLogin.enable();
        break;
      case "REMOVE_LAUNCH_ON_LOGIN":
        await lazy.LaunchOnLogin.disable();
        break;
      case "CREATE_GROUP_FROM_CURRENT_TAB": {
        let tab =
          window.gBrowser.getTabForBrowser(browser) ??
          window.gBrowser.selectedTab;
        if (tab.group) {
          // Create a new tab after the current tab's group, add the new tab to
          // a new tab group.
          /** @type {Extract<nsIObserver, Function>} */
          async function observer(aSubject) {
            Services.obs.removeObserver(observer, "browser-open-newtab-start");
            /** @type {nsIBrowser} */
            let newBrowser = await aSubject.wrappedJSObject;
            let newTab = window.gBrowser.getTabForBrowser(newBrowser);
            window.gBrowser.addTabGroup([newTab], {
              insertBefore: tab.group.nextElementSibling,
              metricsContext: window.gBrowser.TabMetrics.userTriggeredContext(
                window.gBrowser.TabMetrics.METRIC_SOURCE.MESSAGING
              ),
            });
          }
          Services.obs.addObserver(observer, "browser-open-newtab-start");
          window.gBrowser.addAdjacentNewTab(tab);
        } else {
          // Add the current tab to a new tab group in place.
          window.gBrowser.addTabGroup([tab], {
            insertBefore: tab,
            metricsContext: window.gBrowser.TabMetrics.userTriggeredContext(
              window.gBrowser.TabMetrics.METRIC_SOURCE.MESSAGING
            ),
          });
        }
        break;
      }
      case "PIN_CURRENT_TAB": {
        let tab = window.gBrowser.selectedTab;
        window.gBrowser.pinTab(tab);
        window.ConfirmationHint.show(tab, "confirmation-hint-pin-tab", {
          descriptionId: "confirmation-hint-pin-tab-description",
        });
        break;
      }
      case "SHOW_FIREFOX_ACCOUNTS": {
        if (!(await lazy.FxAccounts.canConnectAccount())) {
          break;
        }
        const data = action.data;
        const service = data?.extraParams?.service;
        if (service) {
          delete data.extraParams.service;
        }
        const url = await lazy.FxAccounts.config.promiseConnectAccountURI(
          service || "sync",
          data && data.entrypoint,
          (data && data.extraParams) || {}
        );
        // Use location provided; if not specified, replace the current tab.
        window.openLinkIn(url, data.where || "current", {
          private: false,
          triggeringPrincipal:
            Services.scriptSecurityManager.createNullPrincipal({}),
        });
        break;
      }
      case "FXA_SIGNIN_FLOW":
        /** @returns {Promise<boolean>} */
        return this.fxaSignInFlow(action.data, browser);
      case "FXA_AIWINDOW_SIGNIN_FLOW":
        /** @returns {Promise<boolean>} */
        return lazy.AIWindow.launchWindow(browser);
      case "OPEN_PROTECTION_PANEL": {
        let { gProtectionsHandler } = window;
        gProtectionsHandler.showProtectionsPopup({});
        break;
      }
      case "OPEN_PROTECTION_REPORT":
        window.gProtectionsHandler.openProtections();
        break;
      case "OPEN_AWESOME_BAR":
        window.gURLBar.search("");
        break;
      case "DISABLE_STP_DOORHANGERS":
        await this.blockMessageById([
          "SOCIAL_TRACKING_PROTECTION",
          "FINGERPRINTERS_PROTECTION",
          "CRYPTOMINERS_PROTECTION",
        ]);
        break;
      case "DISABLE_DOH":
        Services.prefs.setStringPref(
          DOH_DOORHANGER_DECISION_PREF,
          "UIDisabled"
        );
        Services.prefs.setIntPref(NETWORK_TRR_MODE_PREF, 5);
        break;
      case "ACCEPT_DOH":
        Services.prefs.setStringPref(DOH_DOORHANGER_DECISION_PREF, "UIOk");
        break;
      case "CANCEL":
        // A no-op used by CFRs that minimizes the notification but does not
        // trigger a dismiss or block (it keeps the notification around)
        break;
      case "CONFIGURE_HOMEPAGE":
        this.configureHomepage(action.data);
        break;
      case "SHOW_SPOTLIGHT":
        lazy.Spotlight.showSpotlightDialog(browser, action.data);
        break;
      case "BLOCK_MESSAGE":
        await this.blockMessageById(action.data.id);
        break;
      case "DESTROY_UIWIDGET":
        this.destroyUIWidget(action.data.widget_id);
        break;
      case "SET_PREF":
        this.setPref(action.data.pref, action.data.onImpression);
        break;
      case "MULTI_ACTION":
        await this.handleMultiAction(
          action.data.actions,
          browser,
          action.data.orderedExecution
        );
        break;
      case "RELOAD_BROWSER":
        browser.reload();
        break;
      case "FOCUS_URLBAR":
        window.gURLBar.focus();
        window.gURLBar.select();
        break;
      case "BOOKMARK_CURRENT_TAB":
        this.bookmarkCurrentTab(
          window,
          action.data?.shouldHideDialog,
          action.data?.shouldHideConfirmationHint
        );
        break;
      case "SET_BOOKMARKS_TOOLBAR_VISIBILITY":
        this.setBookmarksToolbarVisibility(window, action.data?.visibility);
        break;
      case "SET_TERMS_OF_USE_INTERACTED":
        Services.obs.notifyObservers(null, "termsofuse:interacted");
        break;
      case "CREATE_NEW_SELECTABLE_PROFILE":
        this.createAndOpenProfile();
        break;
      case "SUBMIT_ONBOARDING_OPT_OUT_PING":
        this.submitOnboardingOptOutPing();
        break;
      case "SET_SEARCH_MODE":
        window.gURLBar.searchMode = action.data;
        window.gURLBar.focus();
        break;
      case "SUMMARIZE_PAGE": {
        const entry = action.data ?? "message";
        await lazy.GenAI.summarizeCurrentPage(window, entry);
        break;
      }
      case "OPEN_ORGANIZE_TABS_PANEL":
        lazy.AIWindowUI.toggleGroupTabsPanel(window, {
          source: action.data?.source ?? "message",
        });
        break;
      case "OPEN_PANEL": {
        let { anchor_id, widget_id, panel_id, fallback_to_app_menu } =
          action.data;
        let anchor;
        if (anchor_id) {
          anchor = window.document.getElementById(anchor_id);
        } else if (widget_id) {
          let widget = lazy.CustomizableUI.getWidget(widget_id);
          anchor = widget?.forWindow(window)?.anchor;
        }
        if (!anchor && fallback_to_app_menu) {
          anchor = window.document.getElementById("PanelUI-menu-button");
        }
        await window.PanelUI.showSubView(panel_id, anchor);
        break;
      }
      case "CREATE_TASKBAR_TAB": {
        let currentTab = window.gBrowser.selectedTab;
        await lazy.TaskbarTabs.moveTabIntoTaskbarTab(currentTab);
        break;
      }
      case "RESTORE_SESSION": {
        if (
          lazy.SessionStore.canRestoreLastSession &&
          !lazy.PrivateBrowsingUtils.isWindowPrivate(window)
        ) {
          await lazy.SessionStore.restoreLastSession();
        }
        break;
      }
      case "IPPROTECTION_ENROLL": {
        await lazy.IPProtection.getPanel(window)?.enroll();
        break;
      }
      case "SET_BROWSER_ICON": {
        await this.setBrowserIcon(action.data.id);
        break;
      }
      case "GET_REFERRAL_CODE": {
        let referralsEnabled = Services.prefs.getBoolPref(
          "browser.referrals.enabled"
        );
        let referralCode = lazy.Referrals.getReferralCode();
        let aboutPageURL = new URL(`about:referrals`);

        if (!referralsEnabled) {
          throw new Error(
            "Cannot generate referral code; referrals disabled by pref"
          );
        }

        if (action.data.entrypoint) {
          aboutPageURL.searchParams.set("entrypoint", action.data.entrypoint);
        }

        aboutPageURL.searchParams.set("ref_key", referralCode);

        window.openTrustedLinkIn(
          aboutPageURL.toString(),
          action.data.where || "tab"
        );
        break;
      }
      default:
        throw new Error(
          `Special message action with type ${action.type} is unsupported.`
        );
    }
    return undefined;
  },
};
