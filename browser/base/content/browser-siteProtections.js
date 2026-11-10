/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

ChromeUtils.defineESModuleGetters(this, {
  ContentBlockingAllowList:
    "moz-src:///toolkit/components/antitracking/ContentBlockingAllowList.sys.mjs",
  ReportBrokenSite:
    "moz-src:///browser/components/reportbrokensite/ReportBrokenSite.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  this,
  "TrackingDBService",
  "@mozilla.org/tracking-db-service;1",
  Ci.nsITrackingDBService
);

/**
 * Represents a protection category shown in the protections UI. For the most
 * common categories we can directly instantiate this category. Some protections
 * categories inherit from this class and overwrite some of its members.
 */
class ProtectionCategory {
  /**
   * Creates a protection category.
   *
   * @param {string} id - Identifier of the category. Used to query the category
   * UI elements in the DOM.
   * @param {object} options - Category options.
   * @param {string} options.prefEnabled - ID of pref which controls the
   * category enabled state.
   * @param {object} flags - Flags for this category to look for in the content
   * blocking event and content blocking log.
   * @param {number} [flags.load] - Load flag for this protection category. If
   * omitted, we will never match a isAllowing check for this category.
   * @param {number} [flags.block] - Block flag for this protection category. If
   * omitted, we will never match a isBlocking check for this category.
   * @param {number} [flags.shim] - Shim flag for this protection category. This
   * flag is set if we replaced tracking content with a non-tracking shim
   * script.
   * @param {number} [flags.allow] - Allow flag for this protection category.
   * This flag is set if we explicitly allow normally blocked tracking content.
   * The webcompat extension can do this if it needs to unblock content on user
   * opt-in.
   */
  constructor(
    id,
    { prefEnabled },
    {
      load,
      block,
      shim = Ci.nsIWebProgressListener.STATE_REPLACED_TRACKING_CONTENT,
      allow = Ci.nsIWebProgressListener.STATE_ALLOWED_TRACKING_CONTENT,
    }
  ) {
    this._id = id;
    this.prefEnabled = prefEnabled;

    this._flags = { load, block, shim, allow };

    if (
      Services.prefs.getPrefType(this.prefEnabled) == Services.prefs.PREF_BOOL
    ) {
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "_enabled",
        this.prefEnabled,
        false,
        this.updateCategoryItem.bind(this)
      );
    }

    MozXULElement.insertFTLIfNeeded("browser/siteProtections.ftl");

    ChromeUtils.defineLazyGetter(this, "subView", () =>
      document.getElementById(`protections-popup-${this._id}View`)
    );

    ChromeUtils.defineLazyGetter(this, "subViewHeading", () =>
      document.getElementById(`protections-popup-${this._id}View-heading`)
    );

    ChromeUtils.defineLazyGetter(this, "subViewList", () =>
      document.getElementById(`protections-popup-${this._id}View-list`)
    );

    ChromeUtils.defineLazyGetter(this, "subViewShimAllowHint", () =>
      document.getElementById(
        `protections-popup-${this._id}View-shim-allow-hint`
      )
    );

    ChromeUtils.defineLazyGetter(this, "isWindowPrivate", () =>
      PrivateBrowsingUtils.isWindowPrivate(window)
    );
  }

  // Child classes may override these to do init / teardown. We expect them to
  // be called when the protections panel is initialized or destroyed.
  init() {}
  uninit() {}

  // Some child classes may overide this getter.
  get enabled() {
    return this._enabled;
  }

  /**
   * Get the category item associated with this protection from the main
   * protections panel.
   *
   * @returns {xul:toolbarbutton|undefined} - Item or undefined if the panel is
   * not yet initialized.
   */
  get categoryItem() {
    // We don't use defineLazyGetter for the category item, since it may be null
    // on first access.
    return (
      this._categoryItem ||
      (this._categoryItem = document.getElementById(
        `protections-popup-category-${this._id}`
      ))
    );
  }

  /**
   * Defaults to enabled state. May be overridden by child classes.
   *
   * @returns {boolean} - Whether the protection is set to block trackers.
   */
  get blockingEnabled() {
    return this.enabled;
  }

  /**
   * The title of the category's subview. Child classes whose title depends on
   * more than the blocking state may override this.
   *
   * @param {boolean} blocking - Whether the category is blocking.
   * @returns {string} - The l10n id of the title.
   */
  subViewTitleL10nId(blocking) {
    return this.l10nKeys.title[blocking ? "blocking" : "not-blocking"];
  }

  /**
   * Update the category item state in the main view of the protections panel.
   * Determines whether the category is set to block trackers.
   *
   * @returns {boolean} - true if the state has been updated, false if the
   * protections popup has not been initialized yet.
   */
  updateCategoryItem() {
    // Can't get `this.categoryItem` without the popup. Using the popup instead
    // of `this.categoryItem` to guard access, because the category item getter
    // can trigger bug 1543537. If there's no popup, we'll be called again the
    // first time the popup shows.
    if (!gProtectionsHandler._protectionsPopup) {
      return false;
    }
    this.categoryItem.classList.toggle("blocked", this.enabled);
    this.categoryItem.classList.toggle("subviewbutton-nav", this.enabled);
    return true;
  }

  /**
   * Update the category sub view that is shown when users click on the category
   * button.
   */
  updateSubView() {
    let { items, anyShimAllowed } = this._generateSubViewListItems();
    this.subViewShimAllowHint.hidden = !anyShimAllowed;

    this.subViewList.textContent = "";
    this.subViewList.append(items);
    const isBlocking =
      this.blockingEnabled && !gProtectionsHandler.hasException;
    let l10nId;
    switch (this._id) {
      case "cryptominers":
        l10nId = isBlocking
          ? "protections-blocking-cryptominers"
          : "protections-not-blocking-cryptominers";
        break;
      case "fingerprinters":
        l10nId = isBlocking
          ? "protections-blocking-fingerprinters"
          : "protections-not-blocking-fingerprinters";
        break;
      case "socialblock":
        l10nId = isBlocking
          ? "protections-blocking-social-media-trackers"
          : "protections-not-blocking-social-media-trackers";
        break;
    }
    if (l10nId) {
      document.l10n.setAttributes(this.subView, l10nId);
    }
  }

  /**
   * Create a list of items, each representing a tracker.
   *
   * @param {object[]} [logs] - Content-blocking logs to build the list from,
   * oldest first. Defaults to the current page's log.
   * @returns {object} result - An object containing the results.
   * @returns {HTMLDivElement[]} result.items - Generated tracker items. May be
   * empty.
   * @returns {boolean} result.anyShimAllowed - Flag indicating if any of the
   * items have been unblocked by a shim script.
   */
  _generateSubViewListItems(logs) {
    logs ??= [JSON.parse(gBrowser.selectedBrowser.getContentBlockingLog())];
    let anyShimAllowed = false;

    let fragment = document.createDocumentFragment();
    for (let [origin, { actions, count }] of this._groupLogsByOrigin(logs)) {
      let { item, shimAllowed } = this._createListItem(origin, actions, count);
      anyShimAllowed = anyShimAllowed || shimAllowed;
      fragment.appendChild(item);
    }

    return {
      items: fragment,
      anyShimAllowed,
    };
  }

  /**
   * Group the origins listed across content-blocking logs, so that a tracker
   * blocked on several pages of the same site is listed once, with a count of
   * the pages it appeared on.
   *
   * @param {object[]} logs - Content-blocking logs for the visited pages of a single site, oldest-visited page first.
   * @returns {Map} Maps each origin to its combined actions and the number of logs it was listed in.
   */
  _groupLogsByOrigin(logs) {
    let grouped = new Map();
    for (let log of logs) {
      for (let [origin, actions] of Object.entries(log)) {
        if (!this._isListed(actions)) {
          continue;
        }
        let entry = grouped.get(origin);
        if (entry) {
          entry.actions.push(...actions);
          entry.count++;
        } else {
          grouped.set(origin, { actions: [...actions], count: 1 });
        }
      }
    }
    return grouped;
  }

  /**
   * Return the number items blocked by this blocker.
   *
   * @param {object[]} [logs] - Content-blocking logs to count items from,
   * oldest first. Defaults to the current page's log.
   * @returns {Integer} count - The number of items blocked.
   */
  getBlockerCount(logs) {
    logs ??= [JSON.parse(gBrowser.selectedBrowser.getContentBlockingLog())];
    let count = 0;
    for (let log of logs) {
      for (let actions of Object.values(log)) {
        if (this._isListed(actions)) {
          count++;
        }
      }
    }
    return count;
  }

  /**
   * Whether an origin's actions from the content-blocking log should be listed
   * by this blocker.
   *
   * @param {Array} actions - Array of [state, blocked] pairs for one origin.
   * @returns {boolean}
   */
  _isListed(actions) {
    return (
      actions.some(
        ([state]) => this.isAllowing(state) && !this.isShimming(state)
      ) || actions.some(([state]) => this.isBlocking(state))
    );
  }

  /**
   * Create a DOM item representing a tracker.
   *
   * @param {string} origin - Origin of the tracker.
   * @param {Array} actions - Array of actions from the content blocking log
   * associated with the tracking origin. Must be listed by this blocker, as
   * determined by `_isListed`.
   * @param {number} [count] - Number of pages the tracker was blocked on.
   * @returns {object} result - An object containing the results.
   * @returns {HTMLDivElement} result.item - Generated item.
   * @returns {boolean} result.shimAllowed - Flag indicating whether the
   * tracking origin was allowed by a shim script.
   */
  _createListItem(origin, actions, count = 1) {
    let isAllowed = actions.some(
      ([state]) => this.isAllowing(state) && !this.isShimming(state)
    );

    // Create an item to hold the origin label and shim allow indicator. Using
    // an html element here, so we can use CSS flex, which handles the label
    // overflow in combination with the icon correctly.
    let listItem = document.createElementNS(
      "http://www.w3.org/1999/xhtml",
      "div"
    );
    listItem.className = "protections-popup-list-item";
    listItem.classList.toggle("allowed", isAllowed);

    let label = document.createXULElement("label");
    // Repeat the host in the tooltip in case it's too long
    // and overflows in our panel.
    label.tooltipText = origin;
    label.value = origin;
    label.className = "protections-popup-list-host-label";
    label.setAttribute("crop", "end");
    listItem.append(label);

    if (count > 1) {
      listItem.classList.add("protections-popup-list-item-with-count");
      listItem.append(this._getCountLabel(count));
    }

    // Determine whether we should show a shim-allow indicator for this item.
    let shimAllowed = actions.some(([flag]) => flag == this._flags.allow);
    if (shimAllowed) {
      listItem.append(this._getShimAllowIndicator());
    }

    return { item: listItem, shimAllowed };
  }

  /**
   * Create a label showing how many pages of the site a tracker was blocked on.
   *
   * @param {number} count - Number of pages the tracker was blocked on.
   * @returns {XULElement} - Created element.
   */
  _getCountLabel(count) {
    let label = document.createXULElement("label");
    label.className = "protections-popup-list-count-label";
    document.l10n.setAttributes(label, "content-blocking-tracker-count", {
      count,
    });
    return label;
  }

  /**
   * Create an indicator icon for marking origins that have been allowed by a
   * shim script.
   *
   * @returns {HTMLImageElement} - Created element.
   */
  _getShimAllowIndicator() {
    let allowIndicator = document.createXULElement("image");
    document.l10n.setAttributes(
      allowIndicator,
      "protections-panel-shim-allowed-indicator"
    );
    allowIndicator.classList.add(
      "protections-popup-list-host-shim-allow-indicator"
    );
    return allowIndicator;
  }

  /**
   * @param {number} state - Content blocking event flags.
   * @returns {boolean} - Whether the protection has blocked a tracker.
   */
  isBlocking(state) {
    return (state & this._flags.block) != 0;
  }

  /**
   * @param {number} state - Content blocking event flags.
   * @returns {boolean} - Whether the protection has allowed a tracker.
   */
  isAllowing(state) {
    return (state & this._flags.load) != 0;
  }

  /**
   * @param {number} state - Content blocking event flags.
   * @returns {boolean} - Whether the protection has detected (blocked or
   * allowed) a tracker.
   */
  isDetected(state) {
    return this.isBlocking(state) || this.isAllowing(state);
  }

  /**
   * @param {number} state - Content blocking event flags.
   * @returns {boolean} - Whether the protections has allowed a tracker that
   * would have normally been blocked.
   */
  isShimming(state) {
    return (state & this._flags.shim) != 0 && this.isAllowing(state);
  }
}

let Fingerprinting =
  new (class FingerprintingProtection extends ProtectionCategory {
    iconSrc = "chrome://browser/skin/fingerprint.svg";
    l10nKeys = {
      content: "fingerprinters",
      general: "fingerprinter",
      title: {
        blocking: "protections-blocking-fingerprinters",
        "not-blocking": "protections-not-blocking-fingerprinters",
      },
    };
    #isInitialized = false;

    constructor() {
      super(
        "fingerprinters",
        {
          prefEnabled: "privacy.trackingprotection.fingerprinting.enabled",
        },
        {
          load: Ci.nsIWebProgressListener.STATE_LOADED_FINGERPRINTING_CONTENT,
          block: Ci.nsIWebProgressListener.STATE_BLOCKED_FINGERPRINTING_CONTENT,
          shim: Ci.nsIWebProgressListener.STATE_REPLACED_FINGERPRINTING_CONTENT,
          allow: Ci.nsIWebProgressListener.STATE_ALLOWED_FINGERPRINTING_CONTENT,
        }
      );

      this.prefFPPEnabled = "privacy.fingerprintingProtection";
      this.prefFPPEnabledInPrivateWindows =
        "privacy.fingerprintingProtection.pbmode";

      this.enabledFPB = false;
      this.enabledFPPGlobally = false;
      this.enabledFPPInPrivateWindows = false;
    }

    init() {
      this.updateEnabled();

      if (!this.#isInitialized) {
        Services.prefs.addObserver(this.prefEnabled, this);
        Services.prefs.addObserver(this.prefFPPEnabled, this);
        Services.prefs.addObserver(this.prefFPPEnabledInPrivateWindows, this);
        this.#isInitialized = true;
      }
    }

    uninit() {
      if (this.#isInitialized) {
        Services.prefs.removeObserver(this.prefEnabled, this);
        Services.prefs.removeObserver(this.prefFPPEnabled, this);
        Services.prefs.removeObserver(
          this.prefFPPEnabledInPrivateWindows,
          this
        );
        this.#isInitialized = false;
      }
    }

    updateEnabled() {
      this.enabledFPB = Services.prefs.getBoolPref(this.prefEnabled);
      this.enabledFPPGlobally = Services.prefs.getBoolPref(this.prefFPPEnabled);
      this.enabledFPPInPrivateWindows = Services.prefs.getBoolPref(
        this.prefFPPEnabledInPrivateWindows
      );
    }

    observe() {
      this.updateEnabled();
      this.updateCategoryItem();
    }

    get enabled() {
      return (
        this.enabledFPB ||
        this.enabledFPPGlobally ||
        (this.isWindowPrivate && this.enabledFPPInPrivateWindows)
      );
    }

    isBlocking(state) {
      let blockFlag = this._flags.block;

      // We only consider the suspicious fingerprinting flag if the
      // fingerprinting protection is enabled in the context.
      if (
        this.enabledFPPGlobally ||
        (this.isWindowPrivate && this.enabledFPPInPrivateWindows)
      ) {
        blockFlag |=
          Ci.nsIWebProgressListener.STATE_BLOCKED_SUSPICIOUS_FINGERPRINTING;
      }

      return (state & blockFlag) != 0;
    }
    // TODO (Bug 1864914): Consider showing suspicious fingerprinting as allowed
    // when the fingerprinting protection is disabled.
  })();

let Cryptomining = new ProtectionCategory(
  "cryptominers",
  {
    prefEnabled: "privacy.trackingprotection.cryptomining.enabled",
  },
  {
    load: Ci.nsIWebProgressListener.STATE_LOADED_CRYPTOMINING_CONTENT,
    block: Ci.nsIWebProgressListener.STATE_BLOCKED_CRYPTOMINING_CONTENT,
  }
);

Cryptomining.l10nId = "trustpanel-cryptomining";
Cryptomining.iconSrc = "chrome://browser/skin/controlcenter/cryptominers.svg";
Cryptomining.l10nKeys = {
  content: "cryptominers",
  general: "cryptominer",
  title: {
    blocking: "protections-blocking-cryptominers",
    "not-blocking": "protections-not-blocking-cryptominers",
  },
};

let TrackingProtection =
  new (class TrackingProtection extends ProtectionCategory {
    iconSrc = "chrome://browser/skin/canvas.svg";
    l10nKeys = {
      content: "tracking-content",
      general: "tracking-content",
      title: {
        blocking: "protections-blocking-tracking-content",
        "not-blocking": "protections-not-blocking-tracking-content",
      },
    };
    #isInitialized = false;

    constructor() {
      super(
        "trackers",
        {
          prefEnabled: "privacy.trackingprotection.enabled",
        },
        {
          load: null,
          block:
            Ci.nsIWebProgressListener.STATE_BLOCKED_TRACKING_CONTENT |
            Ci.nsIWebProgressListener.STATE_BLOCKED_EMAILTRACKING_CONTENT,
        }
      );

      this.prefEnabledInPrivateWindows =
        "privacy.trackingprotection.pbmode.enabled";
      this.prefTrackingTable = "urlclassifier.trackingTable";
      this.prefTrackingAnnotationTable =
        "urlclassifier.trackingAnnotationTable";
      this.prefAnnotationsLevel2Enabled =
        "privacy.annotate_channels.strict_list.enabled";
      this.prefEmailTrackingProtectionEnabled =
        "privacy.trackingprotection.emailtracking.enabled";
      this.prefEmailTrackingProtectionEnabledInPrivateWindows =
        "privacy.trackingprotection.emailtracking.pbmode.enabled";

      this.enabledGlobally = false;
      this.emailTrackingProtectionEnabledGlobally = false;

      this.enabledInPrivateWindows = false;
      this.emailTrackingProtectionEnabledInPrivateWindows = false;

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "trackingTable",
        this.prefTrackingTable,
        ""
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "trackingAnnotationTable",
        this.prefTrackingAnnotationTable,
        ""
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "annotationsLevel2Enabled",
        this.prefAnnotationsLevel2Enabled,
        false
      );
    }

    init() {
      this.updateEnabled();

      if (!this.#isInitialized) {
        Services.prefs.addObserver(this.prefEnabled, this);
        Services.prefs.addObserver(this.prefEnabledInPrivateWindows, this);
        Services.prefs.addObserver(
          this.prefEmailTrackingProtectionEnabled,
          this
        );
        Services.prefs.addObserver(
          this.prefEmailTrackingProtectionEnabledInPrivateWindows,
          this
        );
        this.#isInitialized = true;
      }
    }

    uninit() {
      if (this.#isInitialized) {
        Services.prefs.removeObserver(this.prefEnabled, this);
        Services.prefs.removeObserver(this.prefEnabledInPrivateWindows, this);
        Services.prefs.removeObserver(
          this.prefEmailTrackingProtectionEnabled,
          this
        );
        Services.prefs.removeObserver(
          this.prefEmailTrackingProtectionEnabledInPrivateWindows,
          this
        );
        this.#isInitialized = false;
      }
    }

    observe() {
      this.updateEnabled();
      this.updateCategoryItem();
    }

    get trackingProtectionLevel2Enabled() {
      const CONTENT_TABLE = "content-track-digest256";
      return this.trackingTable.includes(CONTENT_TABLE);
    }

    get enabled() {
      return (
        this.enabledGlobally ||
        this.emailTrackingProtectionEnabledGlobally ||
        (this.isWindowPrivate &&
          (this.enabledInPrivateWindows ||
            this.emailTrackingProtectionEnabledInPrivateWindows))
      );
    }

    updateEnabled() {
      this.enabledGlobally = Services.prefs.getBoolPref(this.prefEnabled);
      this.enabledInPrivateWindows = Services.prefs.getBoolPref(
        this.prefEnabledInPrivateWindows
      );
      this.emailTrackingProtectionEnabledGlobally = Services.prefs.getBoolPref(
        this.prefEmailTrackingProtectionEnabled
      );
      this.emailTrackingProtectionEnabledInPrivateWindows =
        Services.prefs.getBoolPref(
          this.prefEmailTrackingProtectionEnabledInPrivateWindows
        );
    }

    isAllowingLevel1(state) {
      return (
        (state &
          Ci.nsIWebProgressListener.STATE_LOADED_LEVEL_1_TRACKING_CONTENT) !=
        0
      );
    }

    isAllowingLevel2(state) {
      return (
        (state &
          Ci.nsIWebProgressListener.STATE_LOADED_LEVEL_2_TRACKING_CONTENT) !=
        0
      );
    }

    isAllowing(state) {
      return this.isAllowingLevel1(state) || this.isAllowingLevel2(state);
    }

    updateSubView() {
      let { items, anyShimAllowed } = this._generateSubViewListItems();

      // If we don't have trackers we would usually not show the menu item
      // allowing the user to show the sub-panel. However, in the edge case
      // that we annotated trackers on the page using the strict list but did
      // not detect trackers on the page using the basic list, we currently
      // still show the panel. To reduce the confusion, tell the user that we have
      // not detected any tracker.
      if (!items.childNodes.length) {
        let emptyImage = document.createXULElement("image");
        emptyImage.classList.add("protections-popup-trackersView-empty-image");
        emptyImage.classList.add("trackers-icon");

        let emptyLabel = document.createXULElement("label");
        emptyLabel.classList.add("protections-popup-empty-label");
        document.l10n.setAttributes(
          emptyLabel,
          "content-blocking-trackers-view-empty"
        );

        items.appendChild(emptyImage);
        items.appendChild(emptyLabel);

        this.subViewList.classList.add("empty");
      } else {
        this.subViewList.classList.remove("empty");
      }

      this.subViewShimAllowHint.hidden = !anyShimAllowed;

      this.subViewList.textContent = "";
      this.subViewList.append(items);
      const l10nId =
        this.enabled && !gProtectionsHandler.hasException
          ? "protections-blocking-tracking-content"
          : "protections-not-blocking-tracking-content";
      document.l10n.setAttributes(this.subView, l10nId);
    }

    _isListed(actions) {
      if (!super._isListed(actions)) {
        return false;
      }

      // Because we might use different lists for annotation vs. blocking, we
      // need to make sure that this is a tracker that we would actually have blocked
      // before showing it to the user.
      return !(
        this.annotationsLevel2Enabled &&
        !this.trackingProtectionLevel2Enabled &&
        actions.some(
          ([state]) =>
            (state &
              Ci.nsIWebProgressListener
                .STATE_LOADED_LEVEL_2_TRACKING_CONTENT) !=
            0
        )
      );
    }
  })();

let ThirdPartyCookies =
  new (class ThirdPartyCookies extends ProtectionCategory {
    iconSrc = "chrome://browser/skin/controlcenter/3rdpartycookies.svg";
    l10nKeys = {
      content: "cross-site-tracking-cookies",
      general: "tracking-cookies",
      title: {
        blocking: "protections-blocking-cookies-third-party",
        "not-blocking": "protections-not-blocking-cookies-third-party",
      },
    };

    constructor() {
      super(
        "cookies",
        {
          // This would normally expect a boolean pref. However, this category
          // overwrites the enabled getter for custom handling of cookie behavior
          // states.
          prefEnabled: "network.cookie.cookieBehavior",
        },
        {
          // ThirdPartyCookies implements custom flag processing.
          allow: null,
          shim: null,
          load: null,
          block: null,
        }
      );

      ChromeUtils.defineLazyGetter(this, "categoryLabel", () =>
        document.getElementById("protections-popup-cookies-category-label")
      );

      this.prefEnabledValues = [
        // These values match the ones exposed under the Content Blocking section
        // of the Preferences UI.
        Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN, // Block all third-party cookies
        Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER, // Block third-party cookies from trackers
        Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN, // Partition all third-party cookies
        Ci.nsICookieService.BEHAVIOR_REJECT, // Block all cookies
      ];

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "behaviorPref",
        this.prefEnabled,
        Ci.nsICookieService.BEHAVIOR_ACCEPT,
        this.updateCategoryItem.bind(this)
      );
    }

    isBlocking(state) {
      return (
        (state & Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_TRACKER) !=
          0 ||
        (state &
          Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_SOCIALTRACKER) !=
          0 ||
        (state & Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_ALL) != 0 ||
        (state &
          Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_BY_PERMISSION) !=
          0 ||
        (state & Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_FOREIGN) !=
          0 ||
        (state & Ci.nsIWebProgressListener.STATE_COOKIES_PARTITIONED_TRACKER) !=
          0
      );
    }

    isDetected(state) {
      if (this.isBlocking(state)) {
        return true;
      }

      if (
        [
          Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN,
          Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER,
          Ci.nsICookieService.BEHAVIOR_ACCEPT,
        ].includes(this.behaviorPref)
      ) {
        return (
          (state & Ci.nsIWebProgressListener.STATE_COOKIES_LOADED_TRACKER) !=
            0 ||
          (SocialTracking.enabled &&
            (state &
              Ci.nsIWebProgressListener.STATE_COOKIES_LOADED_SOCIALTRACKER) !=
              0)
        );
      }

      // We don't have specific flags for the other cookie behaviors so just
      // fall back to STATE_COOKIES_LOADED.
      return (state & Ci.nsIWebProgressListener.STATE_COOKIES_LOADED) != 0;
    }

    updateCategoryItem() {
      if (!super.updateCategoryItem()) {
        return;
      }

      let l10nId;
      if (!this.enabled) {
        l10nId = "content-blocking-cookies-blocking-trackers-label";
      } else {
        switch (this.behaviorPref) {
          case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
            l10nId = "content-blocking-cookies-blocking-third-party-label";
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT:
            l10nId = "content-blocking-cookies-blocking-all-label";
            break;
          case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
            l10nId = "content-blocking-cookies-blocking-unvisited-label";
            break;
          case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
          case Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN:
            l10nId = "content-blocking-cookies-blocking-trackers-label";
            break;
          default:
            console.error(
              `Error: Unknown cookieBehavior pref observed: ${this.behaviorPref}`
            );
            this.categoryLabel.removeAttribute("data-l10n-id");
            this.categoryLabel.textContent = "";
            return;
        }
      }
      document.l10n.setAttributes(this.categoryLabel, l10nId);
    }

    get enabled() {
      return this.prefEnabledValues.includes(this.behaviorPref);
    }

    _generateSubViewListItems(logs) {
      logs ??= [JSON.parse(gBrowser.selectedBrowser.getContentBlockingLog())];
      let fragment = document.createDocumentFragment();

      // A cookie origin may be blocked on several pages of the same site; group
      // by origin so it is listed once, with a count of the pages it appeared
      // on.
      let grouped = new Map();
      for (let log of logs) {
        let categories = this._processContentBlockingLog(log);
        for (let category of this._categoryNames()) {
          for (let info of categories[category]) {
            let entry = grouped.get(info.origin);
            if (entry) {
              entry.count++;
            } else {
              grouped.set(info.origin, { info, count: 1 });
            }
          }
        }
      }

      for (let { info, count } of grouped.values()) {
        fragment.appendChild(this._createCookieListItem(info, count));
      }
      return { items: fragment };
    }

    getBlockerCount(logs) {
      logs ??= [JSON.parse(gBrowser.selectedBrowser.getContentBlockingLog())];
      let count = 0;
      for (let log of logs) {
        let categories = this._processContentBlockingLog(log);
        for (let category of this._categoryNames()) {
          count += categories[category].length;
        }
      }
      return count;
    }

    _categoryNames() {
      let categoryNames = ["trackers"];
      switch (this.behaviorPref) {
        case Ci.nsICookieService.BEHAVIOR_REJECT:
          categoryNames.push("firstParty");
        // eslint-disable-next-line no-fallthrough
        case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
          categoryNames.push("thirdParty");
      }
      return categoryNames;
    }

    updateSubView() {
      let contentBlockingLog = gBrowser.selectedBrowser.getContentBlockingLog();
      contentBlockingLog = JSON.parse(contentBlockingLog);

      let categories = this._processContentBlockingLog(contentBlockingLog);

      this.subViewList.textContent = "";

      for (let category of this._categoryNames()) {
        let itemsToShow = categories[category];

        if (!itemsToShow.length) {
          continue;
        }

        let box = document.createXULElement("vbox");
        box.className = "protections-popup-cookiesView-list-section";
        let label = document.createXULElement("label");
        label.className = "protections-popup-cookiesView-list-header";
        let l10nId;
        switch (category) {
          case "trackers":
            l10nId = "content-blocking-cookies-view-trackers-label";
            break;
          case "firstParty":
            l10nId = "content-blocking-cookies-view-first-party-label";
            break;
          case "thirdParty":
            l10nId = "content-blocking-cookies-view-third-party-label";
            break;
        }
        if (l10nId) {
          document.l10n.setAttributes(label, l10nId);
        }
        box.appendChild(label);

        for (let info of itemsToShow) {
          box.appendChild(this._createCookieListItem(info));
        }

        this.subViewList.appendChild(box);
      }

      this.subViewHeading.hidden = false;
      if (!this.enabled) {
        document.l10n.setAttributes(
          this.subView,
          "protections-not-blocking-cross-site-tracking-cookies"
        );
        return;
      }

      let l10nId = this.subViewTitleL10nId(!gProtectionsHandler.hasException);
      if (!l10nId) {
        return;
      }

      switch (this.behaviorPref) {
        case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
          this.subViewHeading.hidden = true;
          if (this.subViewHeading.nextSibling.nodeName == "toolbarseparator") {
            this.subViewHeading.nextSibling.hidden = true;
          }
          break;
        case Ci.nsICookieService.BEHAVIOR_REJECT:
          this.subViewHeading.hidden = true;
          if (this.subViewHeading.nextSibling.nodeName == "toolbarseparator") {
            this.subViewHeading.nextSibling.hidden = true;
          }
          break;
        case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
          this.subViewHeading.hidden = true;
          if (this.subViewHeading.nextSibling.nodeName == "toolbarseparator") {
            this.subViewHeading.nextSibling.hidden = true;
          }
          break;
      }

      document.l10n.setAttributes(this.subView, l10nId);
    }

    /**
     * The title depends on which cookies the user chose to block, not only on
     * whether they are being blocked.
     *
     * @param {boolean} blocking - Whether the category is blocking.
     * @returns {?string} - The l10n id of the title, null for an unknown
     *   cookie behavior.
     */
    subViewTitleL10nId(blocking) {
      switch (this.behaviorPref) {
        case Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN:
          return blocking
            ? "protections-blocking-cookies-third-party"
            : "protections-not-blocking-cookies-third-party";
        case Ci.nsICookieService.BEHAVIOR_REJECT:
          return blocking
            ? "protections-blocking-cookies-all"
            : "protections-not-blocking-cookies-all";
        case Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN:
          return "protections-blocking-cookies-unvisited";
        case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
        case Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN:
          return blocking
            ? "protections-blocking-cookies-trackers"
            : "protections-not-blocking-cross-site-tracking-cookies";
        case Ci.nsICookieService.BEHAVIOR_ACCEPT:
          return "protections-not-blocking-cross-site-tracking-cookies";
      }
      console.error(
        `Error: Unknown cookieBehavior pref when updating subview: ${this.behaviorPref}`
      );
      return null;
    }

    _getExceptionState(origin) {
      let thirdPartyStorage = Services.perms.testPermissionFromPrincipal(
        gBrowser.contentPrincipal,
        "3rdPartyStorage^" + origin
      );

      if (thirdPartyStorage != Services.perms.UNKNOWN_ACTION) {
        return thirdPartyStorage;
      }

      let principal =
        Services.scriptSecurityManager.createContentPrincipalFromOrigin(origin);
      // Cookie exceptions get "inherited" from parent- to sub-domain, so we need to
      // make sure to include parent domains in the permission check for "cookie".
      return Services.perms.testPermissionFromPrincipal(principal, "cookie");
    }

    _clearException(origin) {
      for (let perm of Services.perms.getAllForPrincipal(
        gBrowser.contentPrincipal
      )) {
        if (perm.type == "3rdPartyStorage^" + origin) {
          Services.perms.removePermission(perm);
        }
      }

      // OAs don't matter here, so we can just use the hostname.
      let host = Services.io.newURI(origin).host;

      // Cookie exceptions get "inherited" from parent- to sub-domain, so we need to
      // clear any cookie permissions from parent domains as well.
      for (let perm of Services.perms.all) {
        if (
          perm.type == "cookie" &&
          Services.eTLD.hasRootDomain(host, perm.principal.host)
        ) {
          Services.perms.removePermission(perm);
        }
      }
    }

    // Transforms and filters cookie entries in the content blocking log
    // so that we can categorize and display them in the UI.
    _processContentBlockingLog(log) {
      let newLog = {
        firstParty: [],
        trackers: [],
        thirdParty: [],
      };

      let firstPartyDomain = null;
      try {
        firstPartyDomain = Services.eTLD.getBaseDomain(gBrowser.currentURI);
      } catch (e) {
        // There are nasty edge cases here where someone is trying to set a cookie
        // on a public suffix or an IP address. Just categorize those as third party...
        if (
          e.result != Cr.NS_ERROR_HOST_IS_IP_ADDRESS &&
          e.result != Cr.NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS
        ) {
          throw e;
        }
      }

      for (let [origin, actions] of Object.entries(log)) {
        if (!origin.startsWith("http")) {
          continue;
        }

        let info = {
          origin,
          isAllowed: true,
          exceptionState: this._getExceptionState(origin),
        };
        let hasCookie = false;
        let isTracker = false;

        // Extract information from the states entries in the content blocking log.
        // Each state will contain a single state flag from nsIWebProgressListener.
        // Note that we are using the same helper functions that are applied to the
        // bit map passed to onSecurityChange (which contains multiple states), thus
        // not checking exact equality, just presence of bits.
        for (let [state, blocked] of actions) {
          if (this.isDetected(state)) {
            hasCookie = true;
          }
          if (TrackingProtection.isAllowing(state)) {
            isTracker = true;
          }
          // blocked tells us whether the resource was actually blocked
          // (which it may not be in case of an exception).
          if (this.isBlocking(state)) {
            info.isAllowed = !blocked;
          }
        }

        if (!hasCookie) {
          continue;
        }

        let isFirstParty = false;
        try {
          let uri = Services.io.newURI(origin);
          isFirstParty = Services.eTLD.getBaseDomain(uri) == firstPartyDomain;
        } catch (e) {
          if (
            e.result != Cr.NS_ERROR_HOST_IS_IP_ADDRESS &&
            e.result != Cr.NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS
          ) {
            throw e;
          }
        }

        if (isFirstParty) {
          newLog.firstParty.push(info);
        } else if (isTracker) {
          newLog.trackers.push(info);
        } else {
          newLog.thirdParty.push(info);
        }
      }

      return newLog;
    }

    _createCookieListItem({ origin, isAllowed, exceptionState }, count = 1) {
      let listItem = document.createElementNS(
        "http://www.w3.org/1999/xhtml",
        "div"
      );
      listItem.className = "protections-popup-list-item";
      // Repeat the origin in the tooltip in case it's too long
      // and overflows in our panel.
      listItem.tooltipText = origin;

      let label = document.createXULElement("label");
      label.value = origin;
      label.className = "protections-popup-list-host-label";
      label.setAttribute("crop", "end");
      listItem.append(label);

      if (count > 1) {
        listItem.classList.add("protections-popup-list-item-with-count");
        listItem.append(this._getCountLabel(count));
      }

      if (
        (isAllowed && exceptionState == Services.perms.ALLOW_ACTION) ||
        (!isAllowed && exceptionState == Services.perms.DENY_ACTION)
      ) {
        listItem.classList.add("protections-popup-list-item-with-state");

        let stateLabel = document.createXULElement("label");
        stateLabel.className = "protections-popup-list-state-label";
        let l10nId;
        if (isAllowed) {
          l10nId = "content-blocking-cookies-view-allowed-label";
          listItem.classList.toggle("allowed", true);
        } else {
          l10nId = "content-blocking-cookies-view-blocked-label";
        }
        document.l10n.setAttributes(stateLabel, l10nId);

        let removeException = document.createXULElement("button");
        removeException.className = "permission-popup-permission-remove-button";
        document.l10n.setAttributes(
          removeException,
          "content-blocking-cookies-view-remove-button",
          { domain: origin }
        );
        removeException.appendChild(stateLabel);

        removeException.addEventListener(
          "click",
          () => {
            this._clearException(origin);
            removeException.remove();
            listItem.classList.toggle("allowed", !isAllowed);
          },
          { once: true }
        );
        listItem.append(removeException);
      }

      return listItem;
    }
  })();

let SocialTracking =
  new (class SocialTrackingProtection extends ProtectionCategory {
    iconSrc = "chrome://browser/skin/thumb-down.svg";
    l10nKeys = {
      content: "social-media-trackers",
      general: "social-tracking",
      title: {
        blocking: "protections-blocking-social-media-trackers",
        "not-blocking": "protections-not-blocking-social-media-trackers",
      },
    };

    constructor() {
      super(
        "socialblock",
        {
          prefEnabled: "privacy.socialtracking.block_cookies.enabled",
        },
        {
          load: Ci.nsIWebProgressListener.STATE_LOADED_SOCIALTRACKING_CONTENT,
          block: Ci.nsIWebProgressListener.STATE_BLOCKED_SOCIALTRACKING_CONTENT,
        }
      );

      this.prefStpTpEnabled =
        "privacy.trackingprotection.socialtracking.enabled";
      this.prefSTPCookieEnabled = this.prefEnabled;
      this.prefCookieBehavior = "network.cookie.cookieBehavior";

      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "socialTrackingProtectionEnabled",
        this.prefStpTpEnabled,
        false,
        this.updateCategoryItem.bind(this)
      );
      XPCOMUtils.defineLazyPreferenceGetter(
        this,
        "rejectTrackingCookies",
        this.prefCookieBehavior,
        null,
        this.updateCategoryItem.bind(this),
        val =>
          [
            Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER,
            Ci.nsICookieService.BEHAVIOR_PARTITION_FOREIGN,
          ].includes(val)
      );
    }

    get blockingEnabled() {
      return (
        (this.socialTrackingProtectionEnabled || this.rejectTrackingCookies) &&
        this.enabled
      );
    }

    isBlockingCookies(state) {
      return (
        (state &
          Ci.nsIWebProgressListener.STATE_COOKIES_BLOCKED_SOCIALTRACKER) !=
        0
      );
    }

    isBlocking(state) {
      return super.isBlocking(state) || this.isBlockingCookies(state);
    }

    isAllowing(state) {
      if (this.socialTrackingProtectionEnabled) {
        return super.isAllowing(state);
      }

      return (
        (state &
          Ci.nsIWebProgressListener.STATE_COOKIES_LOADED_SOCIALTRACKER) !=
        0
      );
    }

    updateCategoryItem() {
      // Can't get `this.categoryItem` without the popup. Using the popup instead
      // of `this.categoryItem` to guard access, because the category item getter
      // can trigger bug 1543537. If there's no popup, we'll be called again the
      // first time the popup shows.
      if (!gProtectionsHandler._protectionsPopup) {
        return;
      }
      if (this.enabled) {
        this.categoryItem.removeAttribute("uidisabled");
      } else {
        this.categoryItem.setAttribute("uidisabled", true);
      }
      this.categoryItem.classList.toggle("blocked", this.blockingEnabled);
    }
  })();

/**
 * Utility object to handle manipulations of the protections indicators in the UI
 */
var gProtectionsHandler = {
  PREF_CB_CATEGORY: "browser.contentblocking.category",

  /**
   * Contains an array of smartblock compatible sites and information on the corresponding shim
   * sites is a list of compatible sites
   * shimId is the id of the shim blocking content from the origin
   * displayName is the name shown for the toggle used for blocking/unblocking the origin
   */
  smartblockEmbedInfo: [
    {
      matchPatterns: ["https://itisatracker.org/*"],
      shimId: "EmbedTestShim",
      displayName: "Test",
    },
    {
      matchPatterns: [
        "https://www.instagram.com/*",
        "https://platform.instagram.com/*",
      ],
      shimId: "InstagramEmbed",
      displayName: "Instagram",
    },
    {
      matchPatterns: ["https://www.tiktok.com/*"],
      shimId: "TikTokEmbed",
      displayName: "TikTok",
    },
    {
      matchPatterns: [
        "https://platform.twitter.com/*",
        "https://platform.x.com/*",
      ],
      shimId: "TwitterEmbed",
      displayName: "X",
    },
    {
      matchPatterns: ["https://*.disqus.com/*"],
      shimId: "DisqusEmbed",
      displayName: "Disqus",
    },
    {
      matchPatterns: ["https://*.sendtonews.com/*"],
      shimId: "SendToNewsEmbed",
      displayName: "STN Video",
    },
  ],

  /**
   * Keeps track of what was responsible for opening the protections panel popup. Used for
   * telemetry purposes.
   */
  _protectionsPopupOpeningReason: null,

  _protectionsPopup: null,
  _initializePopup() {
    if (!this._protectionsPopup) {
      let wrapper = document.getElementById("template-protections-popup");
      this._protectionsPopup = wrapper.content.firstElementChild;
      this._protectionsPopup.addEventListener("popupshown", this);
      this._protectionsPopup.addEventListener("popuphidden", this);
      wrapper.replaceWith(wrapper.content);

      this.maybeSetMilestoneCounterText();

      for (let blocker of Object.values(this.blockers)) {
        blocker.updateCategoryItem();
      }

      this._protectionsPopup.addEventListener("command", this);
      this._protectionsPopup.addEventListener("popupshown", this);
      this._protectionsPopup.addEventListener("popuphidden", this);

      function openTooltip(event) {
        document.getElementById(event.target.tooltip).openPopup(event.target);
      }
      function closeTooltip(event) {
        document.getElementById(event.target.tooltip).hidePopup();
      }
      let notBlockingWhy = document.getElementById(
        "protections-popup-not-blocking-section-why"
      );
      notBlockingWhy.addEventListener("mouseover", openTooltip);
      notBlockingWhy.addEventListener("focus", openTooltip);
      notBlockingWhy.addEventListener("mouseout", closeTooltip);
      notBlockingWhy.addEventListener("blur", closeTooltip);

      document
        .getElementById(
          "protections-popup-trackers-blocked-counter-description"
        )
        .addEventListener("click", () =>
          gProtectionsHandler.openProtections(true)
        );
    }
  },

  _hidePopup() {
    if (this._protectionsPopup) {
      PanelMultiView.hidePopup(this._protectionsPopup);
    }
  },

  // smart getters
  get iconBox() {
    delete this.iconBox;
    return (this.iconBox = document.getElementById(
      "tracking-protection-icon-box"
    ));
  },
  get _protectionsPopupMultiView() {
    delete this._protectionsPopupMultiView;
    return (this._protectionsPopupMultiView = document.getElementById(
      "protections-popup-multiView"
    ));
  },
  get _protectionsPopupMainView() {
    delete this._protectionsPopupMainView;
    return (this._protectionsPopupMainView = document.getElementById(
      "protections-popup-mainView"
    ));
  },
  get _protectionsPopupMainViewHeaderLabel() {
    delete this._protectionsPopupMainViewHeaderLabel;
    return (this._protectionsPopupMainViewHeaderLabel = document.getElementById(
      "protections-popup-mainView-panel-header-span"
    ));
  },
  get _protectionsPopupTPSwitch() {
    delete this._protectionsPopupTPSwitch;
    return (this._protectionsPopupTPSwitch = document.getElementById(
      "protections-popup-tp-switch"
    ));
  },
  get _protectionsPopupCategoryList() {
    delete this._protectionsPopupCategoryList;
    return (this._protectionsPopupCategoryList = document.getElementById(
      "protections-popup-category-list"
    ));
  },
  get _protectionsPopupBlockingHeader() {
    delete this._protectionsPopupBlockingHeader;
    return (this._protectionsPopupBlockingHeader = document.getElementById(
      "protections-popup-blocking-section-header"
    ));
  },
  get _protectionsPopupNotBlockingHeader() {
    delete this._protectionsPopupNotBlockingHeader;
    return (this._protectionsPopupNotBlockingHeader = document.getElementById(
      "protections-popup-not-blocking-section-header"
    ));
  },
  get _protectionsPopupNotFoundHeader() {
    delete this._protectionsPopupNotFoundHeader;
    return (this._protectionsPopupNotFoundHeader = document.getElementById(
      "protections-popup-not-found-section-header"
    ));
  },
  get _protectionsPopupSmartblockContainer() {
    delete this._protectionsPopupSmartblockContainer;
    return (this._protectionsPopupSmartblockContainer = document.getElementById(
      "protections-popup-smartblock-highlight-container"
    ));
  },
  get _protectionsPopupSmartblockDescription() {
    delete this._protectionsPopupSmartblockDescription;
    return (this._protectionsPopupSmartblockDescription =
      document.getElementById("protections-popup-smartblock-description"));
  },
  get _protectionsPopupSmartblockToggleContainer() {
    delete this._protectionsPopupSmartblockToggleContainer;
    return (this._protectionsPopupSmartblockToggleContainer =
      document.getElementById("protections-popup-smartblock-toggle-container"));
  },
  get _protectionsPopupSettingsButton() {
    delete this._protectionsPopupSettingsButton;
    return (this._protectionsPopupSettingsButton = document.getElementById(
      "protections-popup-settings-button"
    ));
  },
  get _protectionsPopupFooter() {
    delete this._protectionsPopupFooter;
    return (this._protectionsPopupFooter = document.getElementById(
      "protections-popup-footer"
    ));
  },
  get _protectionsPopupTrackersCounterBox() {
    delete this._protectionsPopupTrackersCounterBox;
    return (this._protectionsPopupTrackersCounterBox = document.getElementById(
      "protections-popup-trackers-blocked-counter-box"
    ));
  },
  get _protectionsPopupTrackersCounterDescription() {
    delete this._protectionsPopupTrackersCounterDescription;
    return (this._protectionsPopupTrackersCounterDescription =
      document.getElementById(
        "protections-popup-trackers-blocked-counter-description"
      ));
  },
  get _protectionsPopupFooterProtectionTypeLabel() {
    delete this._protectionsPopupFooterProtectionTypeLabel;
    return (this._protectionsPopupFooterProtectionTypeLabel =
      document.getElementById(
        "protections-popup-footer-protection-type-label"
      ));
  },
  get _trackingProtectionIconTooltipLabel() {
    delete this._trackingProtectionIconTooltipLabel;
    return (this._trackingProtectionIconTooltipLabel = document.getElementById(
      "tracking-protection-icon-tooltip-label"
    ));
  },
  get _trackingProtectionIconContainer() {
    delete this._trackingProtectionIconContainer;
    return (this._trackingProtectionIconContainer = document.getElementById(
      "tracking-protection-icon-container"
    ));
  },

  get noTrackersDetectedDescription() {
    delete this.noTrackersDetectedDescription;
    return (this.noTrackersDetectedDescription = document.getElementById(
      "protections-popup-no-trackers-found-description"
    ));
  },

  get _protectionsPopupMilestonesText() {
    delete this._protectionsPopupMilestonesText;
    return (this._protectionsPopupMilestonesText = document.getElementById(
      "protections-popup-milestones-text"
    ));
  },

  get _notBlockingWhyLink() {
    delete this._notBlockingWhyLink;
    return (this._notBlockingWhyLink = document.getElementById(
      "protections-popup-not-blocking-section-why"
    ));
  },

  // A list of blockers that will be displayed in the categories list
  // when blockable content is detected. A blocker must be an object
  // with at least the following two properties:
  //  - enabled: Whether the blocker is currently turned on.
  //  - isDetected(state): Given a content blocking state, whether the blocker has
  //                       either allowed or blocked elements.
  //  - categoryItem: The DOM item that represents the entry in the category list.
  //
  // It may also contain an init() and uninit() function, which will be called
  // on gProtectionsHandler.init() and gProtectionsHandler.uninit().
  // The buttons in the protections panel will appear in the same order as this array.
  blockers: {
    SocialTracking,
    ThirdPartyCookies,
    TrackingProtection,
    Fingerprinting,
    Cryptomining,
  },

  init() {
    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "_protectionsPopupToastTimeout",
      "browser.protections_panel.toast.timeout",
      3000
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "_protectionsPopupButtonDelay",
      "security.notification_enable_delay",
      500
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "milestoneListPref",
      "browser.contentblocking.cfr-milestone.milestones",
      "[]",
      () => this.maybeSetMilestoneCounterText(),
      val => JSON.parse(val)
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "milestonePref",
      "browser.contentblocking.cfr-milestone.milestone-achieved",
      0,
      () => this.maybeSetMilestoneCounterText()
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "milestoneTimestampPref",
      "browser.contentblocking.cfr-milestone.milestone-shown-time",
      "0",
      null,
      val => parseInt(val)
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "milestonesEnabledPref",
      "browser.contentblocking.cfr-milestone.enabled",
      false,
      () => this.maybeSetMilestoneCounterText()
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "protectionsPanelMessageSeen",
      "browser.protections_panel.infoMessage.seen",
      false
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "smartblockEmbedsEnabledPref",
      "extensions.webcompat.smartblockEmbeds.enabled",
      false
    );

    XPCOMUtils.defineLazyPreferenceGetter(
      this,
      "trustPanelEnabledPref",
      "browser.urlbar.trustPanel.featureGate",
      false
    );

    for (let blocker of Object.values(this.blockers)) {
      if (blocker.init) {
        blocker.init();
      }
    }

    // Add an observer to observe that the history has been cleared.
    Services.obs.addObserver(this, "browser:purge-session-history");
    // Add an observer to listen to requests to open the protections panel
    Services.obs.addObserver(this, "smartblock:open-protections-panel");

    // bind the reset toggle sec delay function to this so we can use it
    // as an event listener without this becoming the event target inside
    // the function
    this._resetToggleSecDelay = this._resetToggleSecDelay.bind(this);
  },

  uninit() {
    for (let blocker of Object.values(this.blockers)) {
      if (blocker.uninit) {
        blocker.uninit();
      }
    }

    Services.obs.removeObserver(this, "browser:purge-session-history");
    Services.obs.removeObserver(this, "smartblock:open-protections-panel");
  },

  getTrackingProtectionLabel() {
    const value = Services.prefs.getStringPref(this.PREF_CB_CATEGORY);

    switch (value) {
      case "strict":
        return "protections-popup-footer-protection-label-strict";
      case "custom":
        return "protections-popup-footer-protection-label-custom";
      case "standard":
      /* fall through */
      default:
        return "protections-popup-footer-protection-label-standard";
    }
  },

  openPreferences(origin) {
    openPreferences("privacy-trackingprotection", { origin });
  },

  openProtections(relatedToCurrent = false) {
    switchToTabHavingURI("about:protections", true, {
      replaceQueryString: true,
      relatedToCurrent,
      triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal(),
    });

    // Don't show the milestones section anymore.
    Services.prefs.clearUserPref(
      "browser.contentblocking.cfr-milestone.milestone-shown-time"
    );
  },

  showTrackersSubview() {
    TrackingProtection.updateSubView();
    this._protectionsPopupMultiView.showSubView(
      "protections-popup-trackersView"
    );
  },

  showSocialblockerSubview() {
    SocialTracking.updateSubView();
    this._protectionsPopupMultiView.showSubView(
      "protections-popup-socialblockView"
    );
  },

  showCookiesSubview() {
    ThirdPartyCookies.updateSubView();
    this._protectionsPopupMultiView.showSubView(
      "protections-popup-cookiesView"
    );
  },

  showFingerprintersSubview() {
    Fingerprinting.updateSubView();
    this._protectionsPopupMultiView.showSubView(
      "protections-popup-fingerprintersView"
    );
  },

  showCryptominersSubview() {
    Cryptomining.updateSubView();
    this._protectionsPopupMultiView.showSubView(
      "protections-popup-cryptominersView"
    );
  },

  shieldHistogramAdd(value) {
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      return;
    }
    Glean.contentblocking.trackingProtectionShield.accumulateSingleSample(
      value
    );
  },

  cryptominersHistogramAdd(value) {
    Glean.contentblocking.cryptominersBlockedCount[value].add(1);
  },

  fingerprintersHistogramAdd(value) {
    Glean.contentblocking.fingerprintersBlockedCount[value].add(1);
  },

  handleProtectionsButtonEvent(event) {
    event.stopPropagation();
    if (
      (event.type == "click" && event.button != 0) ||
      (event.type == "keypress" &&
        event.charCode != KeyEvent.DOM_VK_SPACE &&
        event.keyCode != KeyEvent.DOM_VK_RETURN)
    ) {
      return; // Left click, space or enter only
    }

    this.showProtectionsPopup({ event, openingReason: "shieldButtonClicked" });
  },

  onPopupShown(event) {
    if (event.target == this._protectionsPopup) {
      PopupNotifications.suppressWhileOpen(this._protectionsPopup);

      window.addEventListener("focus", this, true);
      this._protectionsPopupTPSwitch.addEventListener("toggle", this);

      // Insert the info message if needed. This will be shown once and then
      // remain collapsed.
      this._insertProtectionsPanelInfoMessage(event);

      // Record telemetry for open, don't record if the panel open is only a toast
      if (!event.target.hasAttribute("toast")) {
        Glean.securityUiProtectionspopup.openProtectionsPopup.record({
          openingReason: this._protectionsPopupOpeningReason,
          smartblockEmbedTogglesShown:
            !this._protectionsPopupSmartblockContainer.hidden,
        });
      }

      // Add the "open" attribute to the tracking protection icon container
      // for styling.
      // Only set the attribute once the panel is opened to avoid icon being
      // incorrectly highlighted if opening is cancelled. See Bug 1926460.
      this._trackingProtectionIconContainer.setAttribute("open", "true");

      // Disable the toggles for a short time after opening via SmartBlock placeholder button
      // to prevent clickjacking.
      if (this._protectionsPopupOpeningReason == "embedPlaceholderButton") {
        this._disablePopupToggles();
        this._protectionsPopupToggleDelayTimer = setTimeout(() => {
          this._enablePopupToggles();
          delete this._protectionsPopupToggleDelayTimer;
        }, this._protectionsPopupButtonDelay);
      }

      ReportBrokenSite.updateParentMenu(event);
    }
  },

  onPopupHidden(event) {
    if (event.target == this._protectionsPopup) {
      window.removeEventListener("focus", this, true);
      this._protectionsPopupTPSwitch.removeEventListener("toggle", this);

      if (this._protectionsPopupToggleDelayTimer) {
        clearTimeout(this._protectionsPopupToggleDelayTimer);
        this._enablePopupToggles();
        delete this._protectionsPopupToggleDelayTimer;
      }

      this._protectionsPopupOpeningReason = null;
    }
  },

  async onTrackingProtectionIconHoveredOrFocused() {
    // We would try to pre-fetch the data whenever the shield icon is hovered or
    // focused. We check focus event here due to the keyboard navigation.
    if (this._updatingFooter) {
      return;
    }
    this._updatingFooter = true;

    // Take the popup out of its template.
    this._initializePopup();

    // Get the tracker count and set it to the counter in the footer.
    const trackerCount = await TrackingDBService.sumAllEvents();
    this.setTrackersBlockedCounter(trackerCount);

    // Set tracking protection label
    const l10nId = this.getTrackingProtectionLabel();
    const elem = this._protectionsPopupFooterProtectionTypeLabel;
    document.l10n.setAttributes(elem, l10nId);

    // Try to get the earliest recorded date in case that there was no record
    // during the initiation but new records come after that.
    await this.maybeUpdateEarliestRecordedDateTooltip(trackerCount);

    this._updatingFooter = false;
  },

  // This triggers from top level location changes.
  onLocationChange() {
    if (this._showToastAfterRefresh) {
      this._showToastAfterRefresh = false;

      // We only display the toast if we're still on the same page.
      if (
        this._previousURI == gBrowser.currentURI.spec &&
        this._previousOuterWindowID == gBrowser.selectedBrowser.outerWindowID
      ) {
        this.showProtectionsPopup({
          toast: true,
        });
      }
    }

    // Reset blocking and exception status so that we can send telemetry
    this.hadShieldState = false;

    // Don't deal with about:, file: etc.
    if (!ContentBlockingAllowList.canHandle(gBrowser.selectedBrowser)) {
      // We hide the icon and thus avoid showing the doorhanger, since
      // the information contained there would mostly be broken and/or
      // irrelevant anyway.
      this._trackingProtectionIconContainer.hidden = true;
      return;
    }
    this._trackingProtectionIconContainer.hidden = false;

    // Check whether the user has added an exception for this site.
    this.hasException = ContentBlockingAllowList.includes(
      gBrowser.selectedBrowser
    );

    if (this._protectionsPopup) {
      this._protectionsPopup.toggleAttribute("hasException", this.hasException);
    }
    this.iconBox.toggleAttribute("hasException", this.hasException);

    // Add to telemetry per page load as a baseline measurement.
    this.fingerprintersHistogramAdd("pageLoad");
    this.cryptominersHistogramAdd("pageLoad");
    this.shieldHistogramAdd(0);
  },

  notifyContentBlockingEvent(event) {
    // We don't notify observers until the document stops loading, therefore
    // a merged event can be sent, which gives an opportunity to decide the
    // priority by the handler.
    // Content blocking events coming after stopping will not be merged, and are
    // sent directly.
    if (!this._isStoppedState || !this.anyDetected) {
      return;
    }

    let uri = gBrowser.currentURI;
    let uriHost = uri.asciiHost ? uri.host : uri.spec;
    Services.obs.notifyObservers(
      {
        wrappedJSObject: {
          browser: gBrowser.selectedBrowser,
          host: uriHost,
          event,
        },
      },
      "SiteProtection:ContentBlockingEvent"
    );
  },

  onStateChange(aWebProgress, stateFlags) {
    if (!aWebProgress.isTopLevel) {
      return;
    }

    this._isStoppedState = !!(
      stateFlags & Ci.nsIWebProgressListener.STATE_STOP
    );
    this.notifyContentBlockingEvent(
      gBrowser.selectedBrowser.getContentBlockingEvents()
    );
  },

  /**
   * Update the in-panel UI given a blocking event. Called when the popup
   * is being shown, or when the popup is open while a new event comes in.
   */
  updatePanelForBlockingEvent(event) {
    // Update the categories:
    for (let blocker of Object.values(this.blockers)) {
      if (blocker.categoryItem.hasAttribute("uidisabled")) {
        continue;
      }
      blocker.categoryItem.classList.toggle(
        "notFound",
        !blocker.isDetected(event)
      );
      blocker.categoryItem.classList.toggle(
        "subviewbutton-nav",
        blocker.isDetected(event)
      );
    }

    // And the popup attributes:
    this._protectionsPopup.toggleAttribute("detected", this.anyDetected);
    this._protectionsPopup.toggleAttribute("blocking", this.anyBlocking);
    this._protectionsPopup.toggleAttribute("hasException", this.hasException);

    this.noTrackersDetectedDescription.hidden = this.anyDetected;

    if (this.anyDetected) {
      // Reorder categories if any are in use.
      this.reorderCategoryItems();
    }
  },

  reportBlockingEventTelemetry(event, isSimulated, previousState) {
    if (!isSimulated) {
      if (this.hasException && !this.hadShieldState) {
        this.hadShieldState = true;
        this.shieldHistogramAdd(1);
      } else if (
        !this.hasException &&
        this.anyBlocking &&
        !this.hadShieldState
      ) {
        this.hadShieldState = true;
        this.shieldHistogramAdd(2);
      }
    }

    // We report up to one instance of fingerprinting and cryptomining
    // blocking and/or allowing per page load.
    let fingerprintingBlocking =
      Fingerprinting.isBlocking(event) &&
      !Fingerprinting.isBlocking(previousState);
    let fingerprintingAllowing =
      Fingerprinting.isAllowing(event) &&
      !Fingerprinting.isAllowing(previousState);
    let cryptominingBlocking =
      Cryptomining.isBlocking(event) && !Cryptomining.isBlocking(previousState);
    let cryptominingAllowing =
      Cryptomining.isAllowing(event) && !Cryptomining.isAllowing(previousState);

    if (fingerprintingBlocking) {
      this.fingerprintersHistogramAdd("blocked");
    } else if (fingerprintingAllowing) {
      this.fingerprintersHistogramAdd("allowed");
    }

    if (cryptominingBlocking) {
      this.cryptominersHistogramAdd("blocked");
    } else if (cryptominingAllowing) {
      this.cryptominersHistogramAdd("allowed");
    }
  },

  onContentBlockingEvent(event, webProgress, isSimulated, previousState) {
    // Don't deal with about:, file: etc.
    if (!ContentBlockingAllowList.canHandle(gBrowser.selectedBrowser)) {
      this.iconBox.removeAttribute("active");
      this.iconBox.removeAttribute("hasException");
      return;
    }

    // First update all our internal state based on the allowlist and the
    // different blockers:
    this.anyDetected = false;
    this.anyBlocking = false;
    this._lastEvent = event;

    // Check whether the user has added an exception for this site.
    this.hasException = ContentBlockingAllowList.includes(
      gBrowser.selectedBrowser
    );

    // Update blocker state and find if they detected or blocked anything.
    for (let blocker of Object.values(this.blockers)) {
      if (blocker.categoryItem?.hasAttribute("uidisabled")) {
        continue;
      }
      // Store data on whether the blocker is activated for reporting it
      // using the "report breakage" dialog. Under normal circumstances this
      // dialog should only be able to open in the currently selected tab
      // and onSecurityChange runs on tab switch, so we can avoid associating
      // the data with the document directly.
      blocker.activated = blocker.isBlocking(event);
      this.anyDetected = this.anyDetected || blocker.isDetected(event);
      this.anyBlocking = this.anyBlocking || blocker.activated;
    }

    this._categoryItemOrderInvalidated = true;

    // Now, update the icon UI:

    // We consider the shield state "active" when some kind of blocking activity
    // occurs on the page.  Note that merely allowing the loading of content that
    // we could have blocked does not trigger the appearance of the shield.
    // This state will be overriden later if there's an exception set for this site.
    this.iconBox.toggleAttribute("active", this.anyBlocking);
    this.iconBox.toggleAttribute("hasException", this.hasException);

    // Update the icon's tooltip:
    if (this.hasException) {
      this.showDisabledTooltipForTPIcon();
    } else if (this.anyBlocking) {
      this.showActiveTooltipForTPIcon();
    } else {
      this.showNoTrackerTooltipForTPIcon();
    }

    // Update the panel if it's open.
    let isPanelOpen = ["showing", "open"].includes(
      this._protectionsPopup?.state
    );
    if (isPanelOpen) {
      this.updatePanelForBlockingEvent(event);
    }

    // Notify other consumers, like CFR.
    // Don't send a content blocking event to CFR for
    // tab switches since this will already be done via
    // onStateChange.
    if (!isSimulated) {
      this.notifyContentBlockingEvent(event);
    }

    // Finally, report telemetry.
    this.reportBlockingEventTelemetry(event, isSimulated, previousState);
  },

  onCommand(event) {
    switch (event.target.id) {
      case "protections-popup-category-trackers":
        gProtectionsHandler.showTrackersSubview(event);
        Glean.securityUiProtectionspopup.clickTrackers.record();
        break;
      case "protections-popup-category-socialblock":
        gProtectionsHandler.showSocialblockerSubview(event);
        Glean.securityUiProtectionspopup.clickSocial.record();
        break;
      case "protections-popup-category-cookies":
        gProtectionsHandler.showCookiesSubview(event);
        Glean.securityUiProtectionspopup.clickCookies.record();
        break;
      case "protections-popup-category-cryptominers":
        gProtectionsHandler.showCryptominersSubview(event);
        Glean.securityUiProtectionspopup.clickCryptominers.record();
        return;
      case "protections-popup-category-fingerprinters":
        gProtectionsHandler.showFingerprintersSubview(event);
        Glean.securityUiProtectionspopup.clickFingerprinters.record();
        break;
      case "protections-popup-settings-button":
        gProtectionsHandler.openPreferences();
        Glean.securityUiProtectionspopup.clickSettings.record();
        break;
      case "protections-popup-show-report-button":
        gProtectionsHandler.openProtections(true);
        Glean.securityUiProtectionspopup.clickFullReport.record();
        break;
      case "protections-popup-milestones-content":
        gProtectionsHandler.openProtections(true);
        Glean.securityUiProtectionspopup.clickMilestoneMessage.record();
        break;
      case "protections-popup-trackersView-settings-button":
        gProtectionsHandler.openPreferences();
        Glean.securityUiProtectionspopup.clickSubviewSettings.record({
          value: "trackers",
        });
        break;
      case "protections-popup-socialblockView-settings-button":
        gProtectionsHandler.openPreferences();
        Glean.securityUiProtectionspopup.clickSubviewSettings.record({
          value: "social",
        });
        break;
      case "protections-popup-cookiesView-settings-button":
        gProtectionsHandler.openPreferences();
        Glean.securityUiProtectionspopup.clickSubviewSettings.record({
          value: "cookies",
        });
        break;
      case "protections-popup-fingerprintersView-settings-button":
        gProtectionsHandler.openPreferences();
        Glean.securityUiProtectionspopup.clickSubviewSettings.record({
          value: "fingerprinters",
        });
        break;
      case "protections-popup-cryptominersView-settings-button":
        gProtectionsHandler.openPreferences();
        Glean.securityUiProtectionspopup.clickSubviewSettings.record({
          value: "cryptominers",
        });
        break;
      case "protections-popup-toast-panel-tp-on-desc":
      case "protections-popup-toast-panel-tp-off-desc":
        // Hide the toast first.
        PanelMultiView.hidePopup(this._protectionsPopup);

        // Open the full protections panel.
        this.showProtectionsPopup({
          event,
          openingReason: "toastButtonClicked",
        });
        break;
    }
  },

  // We handle focus here when the panel is shown.
  handleEvent(event) {
    switch (event.type) {
      case "command":
        this.onCommand(event);
        break;
      case "focus": {
        let elem = document.activeElement;
        let position = elem.compareDocumentPosition(this._protectionsPopup);

        if (
          !(
            position &
            (Node.DOCUMENT_POSITION_CONTAINS |
              Node.DOCUMENT_POSITION_CONTAINED_BY)
          ) &&
          !this._protectionsPopup.hasAttribute("noautohide")
        ) {
          // Hide the panel when focusing an element that is
          // neither an ancestor nor descendant unless the panel has
          // @noautohide (e.g. for a tour).
          PanelMultiView.hidePopup(this._protectionsPopup);
        }
        break;
      }
      case "popupshown":
        this.onPopupShown(event);
        break;
      case "popuphidden":
        this.onPopupHidden(event);
        break;
      case "toggle": {
        this.onTPSwitchCommand(event);
        break;
      }
    }
  },

  observe(subject, topic) {
    switch (topic) {
      case "browser:purge-session-history":
        // We need to update the earliest recorded date if history has been
        // cleared.
        this._earliestRecordedDate = 0;
        this.maybeUpdateEarliestRecordedDateTooltip();
        break;
      case "smartblock:open-protections-panel":
        if (!this.smartblockEmbedsEnabledPref) {
          // don't react if smartblock disabled by pref
          break;
        }

        if (gBrowser.selectedBrowser.browserId !== subject.browserId) {
          break;
        }

        // Ensure panel is fully hidden before trying to open
        this._hidePopup();

        this.showProtectionsPopup({
          openingReason: "embedPlaceholderButton",
        });
        break;
    }
  },

  /**
   * Update the popup contents. Only called when the popup has been taken
   * out of the template and is shown or about to be shown.
   */
  refreshProtectionsPopup() {
    let host = gIdentityHandler.getHostForDisplay();
    document.l10n.setAttributes(
      this._protectionsPopupMainViewHeaderLabel,
      "protections-header",
      { host }
    );

    let currentlyEnabled = !this.hasException;

    this.updateProtectionsToggle(currentlyEnabled);

    this._notBlockingWhyLink.setAttribute(
      "tooltip",
      currentlyEnabled
        ? "protections-popup-not-blocking-why-etp-on-tooltip"
        : "protections-popup-not-blocking-why-etp-off-tooltip"
    );

    // Update the tooltip of the blocked tracker counter.
    this.maybeUpdateEarliestRecordedDateTooltip();

    let today = Date.now();
    let threeDaysMillis = 72 * 60 * 60 * 1000;
    let expired = today - this.milestoneTimestampPref > threeDaysMillis;

    if (this._milestoneTextSet && !expired) {
      this._protectionsPopup.setAttribute("milestone", this.milestonePref);
      NimbusFeatures.privacySecurityMessaging.recordExposureEvent();
    } else {
      this._protectionsPopup.removeAttribute("milestone");
    }

    this._protectionsPopup.toggleAttribute("detected", this.anyDetected);
    this._protectionsPopup.toggleAttribute("blocking", this.anyBlocking);
    this._protectionsPopup.toggleAttribute("hasException", this.hasException);
  },

  /**
   * Updates the "pressed" state and labels for the toggle
   *
   * @param {boolean} isPressed - Whether or not the toggle should be pressed.
   *  True if ETP is enabled for a given site.
   */
  updateProtectionsToggle(isPressed) {
    let host = gIdentityHandler.getHostForDisplay();
    let toggle = this._protectionsPopupTPSwitch;
    toggle.toggleAttribute("pressed", isPressed);
    toggle.toggleAttribute("disabled", !!this._TPSwitchCommanding);
    document.l10n.setAttributes(
      toggle,
      isPressed
        ? "protections-panel-etp-toggle-on"
        : "protections-panel-etp-toggle-off",
      { host }
    );
  },

  /*
   * This function sorts the category items into the Blocked/Allowed/None Detected
   * sections. It's called immediately in onContentBlockingEvent if the popup
   * is presently open. Otherwise, the next time the popup is shown.
   */
  reorderCategoryItems() {
    if (!this._categoryItemOrderInvalidated) {
      return;
    }

    delete this._categoryItemOrderInvalidated;

    // Hide all the headers to start with.
    this._protectionsPopupBlockingHeader.hidden = true;
    this._protectionsPopupNotBlockingHeader.hidden = true;
    this._protectionsPopupNotFoundHeader.hidden = true;
    this._protectionsPopupSmartblockContainer.hidden = true;

    for (let { categoryItem } of Object.values(this.blockers)) {
      if (
        categoryItem.classList.contains("notFound") ||
        categoryItem.hasAttribute("uidisabled")
      ) {
        // Add the item to the bottom of the list. This will be under
        // the "None Detected" section.
        this._protectionsPopupCategoryList.insertAdjacentElement(
          "beforeend",
          categoryItem
        );
        categoryItem.setAttribute("disabled", true);
        // We have an undetected category, show the header.
        this._protectionsPopupNotFoundHeader.hidden = false;
        continue;
      }

      // Clear the disabled attribute in case we are moving the item out of
      // "None Detected"
      categoryItem.removeAttribute("disabled");

      if (categoryItem.classList.contains("blocked") && !this.hasException) {
        // Add the item just above the Smartblock embeds section - this will be the
        // bottom of the "Blocked" section.
        categoryItem.parentNode.insertBefore(
          categoryItem,
          this._protectionsPopupSmartblockContainer
        );
        // We have a blocking category, show the header.
        this._protectionsPopupBlockingHeader.hidden = false;
        continue;
      }

      // Add the item just above the "None Detected" section - this will be the
      // bottom of the "Allowed" section.
      categoryItem.parentNode.insertBefore(
        categoryItem,
        this._protectionsPopupNotFoundHeader
      );
      // We have an allowing category, show the header.
      this._protectionsPopupNotBlockingHeader.hidden = false;
    }

    // add toggles if required to the Smartblock embed section
    let smartblockEmbedDetected = this._addSmartblockEmbedToggles();

    if (smartblockEmbedDetected) {
      // We have a compatible smartblock toggle, show the smartblock
      // embed section
      this._protectionsPopupSmartblockContainer.hidden = false;
    }
  },

  /**
   * Adds the toggles into the smartblock toggle container. Clears existing toggles first, then
   * searches through the contentBlockingLog for smartblock-compatible content.
   *
   * @returns {boolean} true if a smartblock compatible resource is blocked or shimmed, false otherwise
   */
  _addSmartblockEmbedToggles() {
    if (!this.smartblockEmbedsEnabledPref) {
      // Do not insert toggles if feature is disabled.
      return false;
    }

    let contentBlockingLog = gBrowser.selectedBrowser.getContentBlockingLog();
    contentBlockingLog = JSON.parse(contentBlockingLog);
    let smartBlockEmbedToggleAdded = false;

    // remove all old toggles
    while (this._protectionsPopupSmartblockToggleContainer.lastChild) {
      this._protectionsPopupSmartblockToggleContainer.lastChild.remove();
    }

    // check that there is an allowed or replaced flag present
    let contentBlockingEvents =
      gBrowser.selectedBrowser.getContentBlockingEvents();

    // In the future, we should add a flag specifically for smartblock embeds so that
    // these checks do not trigger when a non-embed-related shim is shimming
    // a smartblock compatible site, see Bug 1926461
    let somethingAllowedOrReplaced =
      contentBlockingEvents &
        Ci.nsIWebProgressListener.STATE_ALLOWED_TRACKING_CONTENT ||
      contentBlockingEvents &
        Ci.nsIWebProgressListener.STATE_REPLACED_TRACKING_CONTENT;

    if (!somethingAllowedOrReplaced) {
      // return early if there is no content that is allowed or replaced
      return smartBlockEmbedToggleAdded;
    }

    // search through content log for compatible blocked origins
    for (let [origin, actions] of Object.entries(contentBlockingLog)) {
      let shimAllowed = actions.some(
        ([flag]) =>
          (flag & Ci.nsIWebProgressListener.STATE_ALLOWED_TRACKING_CONTENT) != 0
      );

      let shimDetected = actions.some(
        ([flag]) =>
          (flag & Ci.nsIWebProgressListener.STATE_REPLACED_TRACKING_CONTENT) !=
          0
      );

      if (!shimAllowed && !shimDetected) {
        // origin is not being shimmed or allowed
        continue;
      }

      let shimInfo = this.smartblockEmbedInfo.find(element => {
        let matchPatternSet = new MatchPatternSet(element.matchPatterns);
        return matchPatternSet.matches(origin);
      });
      if (!shimInfo) {
        // origin not relevant to smartblock
        continue;
      }

      const { shimId, displayName } = shimInfo;
      smartBlockEmbedToggleAdded = true;

      // check that a toggle doesn't already exist
      let existingToggle = document.getElementById(
        `smartblock-${shimId.toLowerCase()}-toggle`
      );
      if (existingToggle) {
        // make sure toggle state is allowed if ANY of the sites are allowed
        if (shimAllowed) {
          existingToggle.setAttribute("pressed", true);
        }
        // skip adding a new toggle
        continue;
      }

      // create the toggle element
      let toggle = document.createElement("moz-toggle");
      toggle.setAttribute("id", `smartblock-${shimId.toLowerCase()}-toggle`);
      toggle.setAttribute("data-l10n-attrs", "label");
      document.l10n.setAttributes(
        toggle,
        "protections-panel-smartblock-blocking-toggle",
        {
          trackername: displayName,
        }
      );

      // set toggle to correct position
      toggle.toggleAttribute("pressed", !!shimAllowed);

      // add functionality to toggle
      toggle.addEventListener("toggle", event => {
        let newToggleState = event.target.pressed;

        if (newToggleState) {
          this._sendUnblockMessageToSmartblock(shimId);
        } else {
          this._sendReblockMessageToSmartblock(shimId);
        }
      });

      this._protectionsPopupSmartblockToggleContainer.insertAdjacentElement(
        "beforeend",
        toggle
      );
    }

    return smartBlockEmbedToggleAdded;
  },

  disableForCurrentPage(shouldReload = true) {
    ContentBlockingAllowList.add(gBrowser.selectedBrowser);
    if (shouldReload) {
      this._hidePopup();
      BrowserCommands.reload();
    }
  },

  enableForCurrentPage(shouldReload = true) {
    ContentBlockingAllowList.remove(gBrowser.selectedBrowser);
    if (shouldReload) {
      this._hidePopup();
      BrowserCommands.reload();
    }
  },

  async onTPSwitchCommand() {
    // When the switch is clicked, we wait 500ms and then disable/enable
    // protections, causing the page to refresh, and close the popup.
    // We need to ensure we don't handle more clicks during the 500ms delay,
    // so we keep track of state and return early if needed.
    if (this._TPSwitchCommanding) {
      return;
    }

    this._TPSwitchCommanding = true;

    // Toggling the 'hasException' on the protections panel in order to do some
    // styling after toggling the TP switch.
    let newExceptionState =
      this._protectionsPopup.toggleAttribute("hasException");

    this.updateProtectionsToggle(!newExceptionState);

    // Change the tooltip of the tracking protection icon.
    if (newExceptionState) {
      this.showDisabledTooltipForTPIcon();
    } else {
      this.showNoTrackerTooltipForTPIcon();
    }

    // Change the state of the tracking protection icon.
    this.iconBox.toggleAttribute("hasException", newExceptionState);

    // Indicating that we need to show a toast after refreshing the page.
    // And caching the current URI and window ID in order to only show the mini
    // panel if it's still on the same page.
    this._showToastAfterRefresh = true;
    this._previousURI = gBrowser.currentURI.spec;
    this._previousOuterWindowID = gBrowser.selectedBrowser.outerWindowID;

    if (newExceptionState) {
      this.disableForCurrentPage(false);
      Glean.securityUiProtectionspopup.clickEtpToggleOff.record();
    } else {
      this.enableForCurrentPage(false);
      Glean.securityUiProtectionspopup.clickEtpToggleOn.record();
    }

    // We need to flush the TP state change immediately without waiting the
    // 500ms delay if the Tab get switched out.
    let targetTab = gBrowser.selectedTab;
    let onTabSelectHandler;
    let tabSelectPromise = new Promise(resolve => {
      onTabSelectHandler = () => resolve();
      gBrowser.tabContainer.addEventListener("TabSelect", onTabSelectHandler);
    });
    let timeoutPromise = new Promise(resolve => setTimeout(resolve, 500));

    await Promise.race([tabSelectPromise, timeoutPromise]);
    gBrowser.tabContainer.removeEventListener("TabSelect", onTabSelectHandler);
    PanelMultiView.hidePopup(this._protectionsPopup);
    gBrowser.reloadTab(targetTab);

    delete this._TPSwitchCommanding;
  },

  setTrackersBlockedCounter(trackerCount) {
    if (this._earliestRecordedDate) {
      document.l10n.setAttributes(
        this._protectionsPopupTrackersCounterDescription,
        "protections-footer-blocked-tracker-counter",
        { trackerCount, date: this._earliestRecordedDate }
      );
    } else {
      document.l10n.setAttributes(
        this._protectionsPopupTrackersCounterDescription,
        "protections-footer-blocked-tracker-counter-no-tooltip",
        { trackerCount }
      );
      this._protectionsPopupTrackersCounterDescription.removeAttribute(
        "tooltiptext"
      );
    }

    // Show the counter if the number of tracker is not zero.
    this._protectionsPopupTrackersCounterBox.toggleAttribute(
      "showing",
      trackerCount != 0
    );
  },

  // Whenever one of the milestone prefs are changed, we attempt to update
  // the milestone section string. This requires us to fetch the earliest
  // recorded date from the Tracking DB, hence this process is async.
  // When completed, we set _milestoneSetText to signal that the section
  // is populated and ready to be shown - which happens next time we call
  // refreshProtectionsPopup.
  _milestoneTextSet: false,
  async maybeSetMilestoneCounterText() {
    if (!this._protectionsPopup) {
      return;
    }
    let trackerCount = this.milestonePref;
    if (
      !this.milestonesEnabledPref ||
      !trackerCount ||
      !this.milestoneListPref.includes(trackerCount)
    ) {
      this._milestoneTextSet = false;
      return;
    }

    let date = await TrackingDBService.getEarliestRecordedDate();
    document.l10n.setAttributes(
      this._protectionsPopupMilestonesText,
      "protections-milestone",
      { date: date ?? 0, trackerCount }
    );
    this._milestoneTextSet = true;
  },

  showDisabledTooltipForTPIcon() {
    document.l10n.setAttributes(
      this._trackingProtectionIconTooltipLabel,
      "tracking-protection-icon-disabled"
    );
    document.l10n.setAttributes(
      this._trackingProtectionIconContainer,
      "tracking-protection-icon-disabled-container"
    );
  },

  showActiveTooltipForTPIcon() {
    document.l10n.setAttributes(
      this._trackingProtectionIconTooltipLabel,
      "tracking-protection-icon-active"
    );
    document.l10n.setAttributes(
      this._trackingProtectionIconContainer,
      "tracking-protection-icon-active-container"
    );
  },

  showNoTrackerTooltipForTPIcon() {
    document.l10n.setAttributes(
      this._trackingProtectionIconTooltipLabel,
      "tracking-protection-icon-no-trackers-detected"
    );
    document.l10n.setAttributes(
      this._trackingProtectionIconContainer,
      "tracking-protection-icon-no-trackers-detected-container"
    );
  },

  /**
   * Showing the protections popup.
   *
   * @param {object} options
   *                 The object could have two properties.
   *                 event:
   *                   The event triggers the protections popup to be opened.
   *                 toast:
   *                   A boolean to indicate if we need to open the protections
   *                   popup as a toast. A toast only has a header section and
   *                   will be hidden after a certain amount of time.
   *                 openingReason:
   *                   A string indicating why the panel was opened. Used for
   *                   telemetry purposes.
   */
  showProtectionsPopup(options = {}) {
    if (this.trustPanelEnabledPref) {
      return;
    }
    const { event, toast, openingReason } = options;

    this._initializePopup();

    // Set opening reason variable for telemetry
    this._protectionsPopupOpeningReason = openingReason;

    // Ensure we've updated category state based on the last blocking event:
    if (this.hasOwnProperty("_lastEvent")) {
      this.updatePanelForBlockingEvent(this._lastEvent);
      delete this._lastEvent;
    }

    // We need to clear the toast timer if it exists before showing the
    // protections popup.
    if (this._toastPanelTimer) {
      clearTimeout(this._toastPanelTimer);
      delete this._toastPanelTimer;
    }

    this._protectionsPopup.toggleAttribute("toast", !!toast);
    if (!toast) {
      // Refresh strings if we want to open it as a standard protections popup.
      this.refreshProtectionsPopup();
    }

    if (toast) {
      this._protectionsPopup.addEventListener(
        "popupshown",
        () => {
          this._toastPanelTimer = setTimeout(() => {
            PanelMultiView.hidePopup(this._protectionsPopup, true);
            delete this._toastPanelTimer;
          }, this._protectionsPopupToastTimeout);
        },
        { once: true }
      );
    }

    // Check the panel state of other panels. Hide them if needed.
    let openPanels = Array.from(document.querySelectorAll("panel[openpanel]"));
    for (let panel of openPanels) {
      PanelMultiView.hidePopup(panel);
    }

    // Now open the popup, anchored off the primary chrome element
    PanelMultiView.openPopup(
      this._protectionsPopup,
      this._trackingProtectionIconContainer,
      {
        position: "bottomleft topleft",
        triggerEvent: event,
      }
    ).catch(console.error);
  },

  async maybeUpdateEarliestRecordedDateTooltip(trackerCount) {
    // If we've already updated or the popup isn't in the DOM yet, don't bother
    // doing this:
    if (this._earliestRecordedDate || !this._protectionsPopup) {
      return;
    }

    let date = await TrackingDBService.getEarliestRecordedDate();

    // If there is no record for any blocked tracker, we don't have to do anything
    // since the tracker counter won't be shown.
    if (date) {
      if (typeof trackerCount !== "number") {
        trackerCount = await TrackingDBService.sumAllEvents();
      }
      document.l10n.setAttributes(
        this._protectionsPopupTrackersCounterDescription,
        "protections-footer-blocked-tracker-counter",
        { trackerCount, date }
      );
      this._earliestRecordedDate = date;
    }
  },

  /**
   * Sends a message to webcompat extension to unblock content and remove placeholders
   *
   * @param {string} shimId - the id of the shim blocking the content
   */
  _sendUnblockMessageToSmartblock(shimId) {
    Services.obs.notifyObservers(
      gBrowser.selectedTab,
      "smartblock:unblock-embed",
      shimId
    );
  },

  /**
   * Sends a message to webcompat extension to reblock content
   *
   * @param {string} shimId - the id of the shim blocking the content
   */
  _sendReblockMessageToSmartblock(shimId) {
    Services.obs.notifyObservers(
      gBrowser.selectedTab,
      "smartblock:reblock-embed",
      shimId
    );
  },

  /**
   * Dispatch the action defined in the message and user telemetry event.
   */
  _dispatchUserAction(message) {
    let url;
    try {
      // Set platform specific path variables for SUMO articles
      url = Services.urlFormatter.formatURL(message.content.cta_url);
    } catch (e) {
      console.error(e);
      url = message.content.cta_url;
    }
    SpecialMessageActions.handleAction(
      {
        type: message.content.cta_type,
        data: {
          args: url,
          where: message.content.cta_where || "tabshifted",
        },
      },
      window.gBrowser.selectedBrowser
    );

    // Only send telemetry for non private browsing windows
    if (!PrivateBrowsingUtils.isWindowPrivate(window)) {
      Glean.securityUiProtectionspopup.clickProtectionspopupCfr.record({
        value: "learn_more_link",
        message: message.id,
      });
    }
  },

  /**
   * Attach event listener to dispatch message defined action.
   */
  _attachCommandListener(element, message) {
    // Add event listener for `mouseup` not to overlap with the
    // `mousedown` & `click` events dispatched from PanelMultiView.sys.mjs
    // https://searchfox.org/mozilla-central/rev/7531325c8660cfa61bf71725f83501028178cbb9/browser/components/customizableui/PanelMultiView.jsm#1830-1837
    element.addEventListener("mouseup", () => {
      this._dispatchUserAction(message);
    });
    element.addEventListener("keyup", e => {
      if (e.key === "Enter" || e.key === " ") {
        this._dispatchUserAction(message);
      }
    });
  },

  /**
   * Inserts a message into the Protections Panel. The message is visible once
   * and afterwards set in a collapsed state. It can be shown again using the
   * info button in the panel header.
   */
  _insertProtectionsPanelInfoMessage(event) {
    // const PROTECTIONS_PANEL_INFOMSG_PREF =
    //   "browser.protections_panel.infoMessage.seen";
    const message = {
      id: "PROTECTIONS_PANEL_1",
      content: {
        title: { string_id: "cfr-protections-panel-header" },
        body: { string_id: "cfr-protections-panel-body" },
        link_text: { string_id: "cfr-protections-panel-link-text" },
        cta_url: `${Services.urlFormatter.formatURLPref(
          "app.support.baseURL"
        )}etp-promotions?as=u&utm_source=inproduct`,
        cta_type: "OPEN_URL",
      },
    };

    const doc = event.target.ownerDocument;
    const container = doc.getElementById("info-message-container");
    const infoButton = doc.getElementById("protections-popup-info-button");
    const panelContainer = doc.getElementById("protections-popup");
    const toggleMessage = () => {
      const learnMoreLink = doc.querySelector(
        "#info-message-container .text-link"
      );
      if (learnMoreLink) {
        container.toggleAttribute("disabled");
        infoButton.toggleAttribute("checked");
        panelContainer.toggleAttribute("infoMessageShowing");
        learnMoreLink.disabled = !learnMoreLink.disabled;
      }
      // If the message panel is opened, send impression telemetry
      // if we are in a non private browsing window.
      if (
        panelContainer.hasAttribute("infoMessageShowing") &&
        !PrivateBrowsingUtils.isWindowPrivate(window)
      ) {
        Glean.securityUiProtectionspopup.openProtectionspopupCfr.record({
          value: "impression",
          message: message.id,
        });
      }
    };
    if (!container.childElementCount) {
      const messageEl = this._createHeroElement(doc, message);
      container.appendChild(messageEl);
      infoButton.addEventListener("click", toggleMessage);
    }
    // Message is collapsed by default. If it was never shown before we want
    // to expand it
    if (
      !this.protectionsPanelMessageSeen &&
      container.hasAttribute("disabled")
    ) {
      toggleMessage(message);
    }
    // Save state that we displayed the message
    if (!this.protectionsPanelMessageSeen) {
      Services.prefs.setBoolPref(
        "browser.protections_panel.infoMessage.seen",
        true
      );
    }
    // Collapse the message after the panel is hidden so we don't get the
    // animation when opening the panel
    panelContainer.addEventListener(
      "popuphidden",
      () => {
        if (
          this.protectionsPanelMessageSeen &&
          !container.hasAttribute("disabled")
        ) {
          toggleMessage(message);
        }
      },
      {
        once: true,
      }
    );
  },

  _createElement(doc, elem, options = {}) {
    const node = doc.createElementNS("http://www.w3.org/1999/xhtml", elem);
    if (options.classList) {
      node.classList.add(options.classList);
    }
    if (options.content) {
      doc.l10n.setAttributes(node, options.content.string_id);
    }
    return node;
  },

  _createHeroElement(doc, message) {
    const messageEl = this._createElement(doc, "div");
    messageEl.setAttribute("id", "protections-popup-message");
    messageEl.classList.add("protections-hero-message");
    const wrapperEl = this._createElement(doc, "div");
    wrapperEl.classList.add("protections-popup-message-body");
    messageEl.appendChild(wrapperEl);

    wrapperEl.appendChild(
      this._createElement(doc, "h2", {
        classList: "protections-popup-message-title",
        content: message.content.title,
      })
    );

    wrapperEl.appendChild(
      this._createElement(doc, "p", { content: message.content.body })
    );

    if (message.content.link_text) {
      let linkEl = this._createElement(doc, "a", {
        classList: "text-link",
        content: message.content.link_text,
      });

      linkEl.disabled = true;
      wrapperEl.appendChild(linkEl);
      this._attachCommandListener(linkEl, message);
    } else {
      this._attachCommandListener(wrapperEl, message);
    }

    return messageEl;
  },

  _resetToggleSecDelay() {
    // Note: `this` is bound to gProtectionsHandler in init.
    clearTimeout(this._protectionsPopupToggleDelayTimer);
    this._protectionsPopupToggleDelayTimer = setTimeout(() => {
      this._enablePopupToggles();
      delete this._protectionsPopupToggleDelayTimer;
    }, this._protectionsPopupButtonDelay);
  },

  _disablePopupToggles() {
    // Disables all toggles in the protections panel
    this._protectionsPopup.querySelectorAll("moz-toggle").forEach(toggle => {
      toggle.setAttribute("disabled", true);
      toggle.addEventListener("pointerdown", this._resetToggleSecDelay);
    });
  },

  _enablePopupToggles() {
    // Enables all toggles in the protections panel
    this._protectionsPopup.querySelectorAll("moz-toggle").forEach(toggle => {
      toggle.removeAttribute("disabled");
      toggle.removeEventListener("pointerdown", this._resetToggleSecDelay);
    });
  },
};
