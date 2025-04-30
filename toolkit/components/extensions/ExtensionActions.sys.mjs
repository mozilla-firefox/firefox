/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const { ExtensionError } = ExtensionUtils;

import { ExtensionParent } from "resource://gre/modules/ExtensionParent.sys.mjs";

const { IconDetails, StartupCache } = ExtensionParent;

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  MV2_ACTION_POPURL_RESTRICTED: {
    pref: "extensions.manifestV2.actionsPopupURLRestricted",
    default: false,
  },
});

const PERSONAL_TOOLBAR_VISIBILITY_PREF =
  "browser.toolbars.bookmarks.visibility";
const VERTICAL_TABS_PREF = "sidebar.verticalTabs";

function parseColor(color, kind) {
  if (typeof color == "string") {
    let rgba = InspectorUtils.colorToRGBA(color);
    if (!rgba) {
      throw new ExtensionError(`Invalid badge ${kind} color: "${color}"`);
    }
    color = [rgba.r, rgba.g, rgba.b, Math.round(rgba.a * 255)];
  }
  return color;
}

/** Common base class for Page and Browser actions. */
class PanelActionBase {
  constructor(options, tabContext, extension) {
    this.tabContext = tabContext;
    this.extension = extension;

    // These are always defined on the action
    this.defaults = {
      enabled: true,
      title: options.default_title || extension.name,
      popup: options.default_popup || "",
      icon: null,
    };
    this.globals = Object.create(this.defaults);

    // eslint-disable-next-line mozilla/balanced-listeners
    this.tabContext.on("location-change", this.handleLocationChange.bind(this));

    // eslint-disable-next-line mozilla/balanced-listeners
    this.tabContext.on("tab-select", (evt, tab) => {
      this.updateOnChange(tab);
    });

    // When preloading a popup we temporarily grant active tab permissions to
    // the preloaded popup. If we don't end up opening we need to clear this
    // permission when clearing the popup.
    this.activeTabForPreload = null;
  }

  onShutdown() {
    this.tabContext.shutdown();
  }

  setPropertyFromDetails(details, prop, value) {
    return this.setProperty(this.getTargetFromDetails(details), prop, value);
  }

  /**
   * Set a global, window specific or tab specific property.
   *
   * @param {NativeTab|ChromeWindow|null} target
   *        A NativeTab tab, a ChromeWindow, or null for the global data.
   * @param {string} prop
   *        String property to set. Should should be one of "icon", "title", "badgeText",
   *        "popup", "badgeBackgroundColor", "badgeTextColor" or "enabled".
   * @param {string} value
   *        Value for prop.
   * @returns {object}
   *        The object to which the property has been set.
   */
  setProperty(target, prop, value) {
    let values = this.getContextData(target);
    if (value === null) {
      delete values[prop];
    } else {
      values[prop] = value;
    }

    this.updateOnChange(target);
    return values;
  }

  /**
   * Gets the data associated with a tab, window, or the global one.
   *
   * @param {NativeTab|ChromeWindow|null} target
   *        A NativeTab tab, a ChromeWindow, or null for the global data.
   * @returns {object}
   *        The icon, title, badge, etc. associated with the target.
   */
  getContextData(target) {
    if (target) {
      return this.tabContext.get(target);
    }
    return this.globals;
  }

  /**
   * Retrieve the value of a global, window specific or tab specific property.
   *
   * @param {NativeTab|ChromeWindow|null} target
   *        A NativeTab tab, a ChromeWindow, or null for the global data.
   * @param {string} prop
   *        Name of property to retrieve. Should should be one of "icon",
   *        "title", "badgeText", "popup", "badgeBackgroundColor" or "enabled".
   * @returns {any} value
   *          Value of prop.
   */
  getProperty(target, prop) {
    return this.getContextData(target)[prop];
  }

  getPropertyFromDetails(details, prop) {
    return this.getProperty(this.getTargetFromDetails(details), prop);
  }

  enable(tabId) {
    this.setPropertyFromDetails({ tabId }, "enabled", true);
  }

  disable(tabId) {
    this.setPropertyFromDetails({ tabId }, "enabled", false);
  }

  getIcon(details = {}) {
    return this.getPropertyFromDetails(details, "icon");
  }

  normalizeIcon(details, extension, context) {
    let icon = IconDetails.normalize(details, extension, context);
    if (!Object.keys(icon).length) {
      return null;
    }
    return icon;
  }

  /**
   * Updates the `tabData` for any location change, however it only updates the button
   * when the selected tab has a location change, or the selected tab has changed.
   *
   * @param {string} eventType
   *        The type of the event, should be "location-change".
   * @param {NativeTab} tab
   *        The tab whose location changed, or which has become selected.
   * @param {boolean} [fromBrowse]
   *        - `true` if navigation occurred in `tab`.
   *        - `false` if the location changed but no navigation occurred, e.g. due to
               a hash change or `history.pushState`.
   *        - Omitted if TabSelect has occurred, tabData does not need to be updated.
   */
  handleLocationChange(eventType, tab, fromBrowse) {
    if (fromBrowse) {
      this.tabContext.clear(tab);
    }
  }

  /**
   * Gets the popup url for a given tab.
   *
   * @param {NativeTab} tab
   *        The tab the popup refers to.
   * @param {boolean} strict
   *        If errors should be thrown if a URL is not available.
   * @returns {string}
   *        The popup URL if a popup is present, undefined otherwise.
   */
  getPopupUrl(tab, strict = false) {
    if (!this.isShownForTab(tab)) {
      if (strict) {
        throw new ExtensionError("Popup is disabled");
      }

      return undefined;
    }
    let popupUrl = this.getProperty(tab, "popup");

    if (strict && !popupUrl) {
      throw new ExtensionError("No popup URL is set");
    }

    return popupUrl;
  }

  /**
   * Grants activeTab permission for a tab when preloading the popup.
   *
   * Will clear any existing activeTab permissions previously granted for any
   * other tab.
   *
   * @param {NativeTab} tab
   *        The tab that should be granted activeTab permission for. Set to
   *        null to clear previously granted activeTab permission.
   */
  setActiveTabForPreload(tab = null) {
    let oldTab = this.activeTabForPreload;
    if (oldTab === tab) {
      return;
    }
    this.activeTabForPreload = tab;
    if (tab) {
      this.extension.tabManager.addActiveTabPermission(tab);
    }
    if (oldTab) {
      this.extension.tabManager.revokeActiveTabPermission(oldTab);
    }
  }

  /**
   * Triggers this action and sends the appropriate event if needed.
   *
   * @param {NativeTab} tab
   *        The tab on which the action was fired.
   * @param {object} clickInfo
   *        Extra data passed to the second parameter to the action API's
   *        onClicked event.
   * @returns {string}
   *        the popup URL if a popup should be open, undefined otherwise.
   */
  triggerClickOrPopup(tab, clickInfo = undefined) {
    if (!this.isShownForTab(tab)) {
      return null;
    }

    // Now that the action is actually being triggered we can clear any
    // existing preloaded activeTab permission.
    this.setActiveTabForPreload(null);
    this.extension.tabManager.addActiveTabPermission(tab);
    this.extension.tabManager.activateScripts(tab);

    let popupUrl = this.getProperty(tab, "popup");
    // The "click" event is only dispatched when the popup is not shown. This
    // is done for compatibility with the Google Chrome onClicked extension
    // API.
    if (!popupUrl) {
      this.dispatchClick(tab, clickInfo);
    }
    this.updateOnChange(tab);
    return popupUrl;
  }

  api(context) {
    let { extension } = context;
    return {
      setTitle: details => {
        this.setPropertyFromDetails(details, "title", details.title);
      },
      getTitle: details => {
        return this.getPropertyFromDetails(details, "title");
      },
      setIcon: details => {
        details.iconType = "browserAction";
        this.setPropertyFromDetails(
          details,
          "icon",
          this.normalizeIcon(details, extension, context)
        );
      },
      setPopup: details => {
        // Note: Chrome resolves arguments to setIcon relative to the calling
        // context, but resolves arguments to setPopup relative to the extension
        // root.
        // For internal consistency, we currently resolve both relative to the
        // calling context.
        let url = details.popup && context.uri.resolve(details.popup);

        if (url && !context.checkLoadURL(url)) {
          return Promise.reject({ message: `Access denied for URL ${url}` });
        }

        // On manifest_version 3 is mandatory for the resolved URI to belong to the
        // current extension (see Bug 1760608).
        //
        // The same restriction is extended  extend to MV2 extensions if the
        // "extensions.manifestV2.actionsPopupURLRestricted" preference is set to true.
        //
        // (Currently set to true by default on GeckoView builds, where the set of
        // extensions supported is limited to a small set and so less risks of
        // unexpected regressions for the existing extensions).
        if (
          url &&
          !url.startsWith(extension.baseURI.spec) &&
          (context.extension.manifestVersion >= 3 ||
            lazy.MV2_ACTION_POPURL_RESTRICTED)
        ) {
          return Promise.reject({ message: `Access denied for URL ${url}` });
        }

        this.setPropertyFromDetails(details, "popup", url);
      },
      getPopup: details => {
        return this.getPropertyFromDetails(details, "popup");
      },
    };
  }

  // Override these

  /**
   * Update the toolbar button when the extension changes the icon, title, url, etc.
   * If it only changes a parameter for a single tab, `target` will be that tab.
   * If it only changes a parameter for a single window, `target` will be that window.
   * Otherwise `target` will be null.
   *
   * @param {NativeTab|ChromeWindow} [_target]
   *        Browser tab or browser chrome window, may be null.
   */
  updateOnChange(_target) {}

  /**
   * Get tab object from tabId.
   *
   * @param {string} _tabId
   *        Internal id of the tab to get.
   * @returns {NativeTab}
   */
  getTab(_tabId) {
    throw new Error("Not implemented.");
  }

  /**
   * Get window object from windowId
   *
   * @param {string} _windowId
   *        Internal id of the window to get.
   * @returns {ChromeWindow}
   */
  getWindow(_windowId) {
    throw new Error("Not implemented.");
  }

  /**
   * Gets the target object corresponding to the `details` parameter of the various
   * get* and set* API methods.
   *
   * @param {object} _details
   *        An object with optional `tabId` or `windowId` properties.
   * @param {number} [_details.tabId]
   * @param {number} [_details.windowId]
   * @throws if both `tabId` and `windowId` are specified, or if they are invalid.
   * @returns {NativeTab|ChromeWindow|null}
   *        If a `tabId` was specified, the corresponding NativeTab tab.
   *        If a `windowId` was specified, the corresponding ChromeWindow.
   *        Otherwise, `null`.
   */
  getTargetFromDetails(_details) {
    throw new Error("Not Implemented");
  }

  /**
   * Triggers a click event.
   *
   * @param {NativeTab} _tab
   *        The tab where this event should be fired.
   * @param {object} _clickInfo
   *        Extra data passed to the second parameter to the action API's
   *        onClicked event.
   */
  dispatchClick(_tab, _clickInfo) {}

  /**
   * Checks whether this action is shown.
   *
   * @param {NativeTab} _tab
   *        The tab to be checked
   * @returns {boolean}
   */
  isShownForTab(_tab) {
    return false;
  }
}

export class PageActionBase extends PanelActionBase {
  constructor(tabContext, extension) {
    const options = extension.manifest.page_action;
    super(options, tabContext, extension);

    // `enabled` can have three different values:
    // - `false`. This means the page action is not shown.
    //   It's set as default if show_matches is empty. Can also be set in a tab via
    //   `pageAction.hide(tabId)`, e.g. in order to override show_matches.
    // - `true`. This means the page action is shown.
    //   It's never set as default because <all_urls> doesn't really match all URLs
    //   (e.g. "about:" URLs). But can be set in a tab via `pageAction.show(tabId)`.
    // - `undefined`.
    //   This is the default value when there are some patterns in show_matches.
    //   Can't be set as a tab-specific value.
    let enabled, showMatches, hideMatches;
    let show_matches = options.show_matches || [];
    let hide_matches = options.hide_matches || [];
    if (!show_matches.length) {
      // Always hide by default. No need to do any pattern matching.
      enabled = false;
    } else {
      // Might show or hide depending on the URL. Enable pattern matching.
      const { restrictSchemes } = extension;
      showMatches = new MatchPatternSet(show_matches, { restrictSchemes });
      hideMatches = new MatchPatternSet(hide_matches, { restrictSchemes });
    }

    this.defaults = {
      ...this.defaults,
      enabled,
      showMatches,
      hideMatches,
      pinned: options.pinned,
    };
    this.globals = Object.create(this.defaults);
  }

  handleLocationChange(eventType, tab, fromBrowse) {
    super.handleLocationChange(eventType, tab, fromBrowse);
    if (fromBrowse === false) {
      // Clear pattern matching cache when URL changes.
      let tabData = this.tabContext.get(tab);
      if (tabData.patternMatching !== undefined) {
        tabData.patternMatching = undefined;
      }
    }

    if (tab.selected) {
      // isShownForTab will do pattern matching (if necessary) and store the result
      // so that updateButton knows whether the page action should be shown.
      this.isShownForTab(tab);
      this.updateOnChange(tab);
    }
  }

  // Checks whether the tab action is shown when the specified tab becomes active.
  // Does pattern matching if necessary, and caches the result as a tab-specific value.
  // @param {NativeTab} tab
  //        The tab to be checked
  // @return boolean
  isShownForTab(tab) {
    let tabData = this.getContextData(tab);

    // If there is a "show" value, return it. Can be due to show(), hide() or empty show_matches.
    if (tabData.enabled !== undefined) {
      return tabData.enabled;
    }

    // Otherwise pattern matching must have been configured. Do it, caching the result.
    if (tabData.patternMatching === undefined) {
      let uri = tab.linkedBrowser.currentURI;
      tabData.patternMatching =
        tabData.showMatches.matches(uri) && !tabData.hideMatches.matches(uri);
    }
    return tabData.patternMatching;
  }

  async loadIconData() {
    const { extension } = this;
    const options = extension.manifest.page_action;
    this.defaults.icon = await StartupCache.get(
      extension,
      ["pageAction", "default_icon"],
      () =>
        this.normalizeIcon(
          { path: options.default_icon || "" },
          extension,
          null
        )
    );
  }

  getPinned() {
    return this.globals.pinned;
  }

  getTargetFromDetails({ tabId }) {
    // PageActionBase doesn't support |windowId|
    if (tabId != null) {
      return this.getTab(tabId);
    }
    return null;
  }

  api(context) {
    return {
      ...super.api(context),
      show: (...args) => this.enable(...args),
      hide: (...args) => this.disable(...args),
      isShown: ({ tabId }) => {
        let tab = this.getTab(tabId);
        return this.isShownForTab(tab);
      },
    };
  }
}

export class BrowserActionBase extends PanelActionBase {
  constructor(tabContext, extension) {
    const options =
      extension.manifest.browser_action || extension.manifest.action;
    super(options, tabContext, extension);

    let default_area =
      Services.policies?.getExtensionSettings(extension.id)?.default_area ||
      options.default_area ||
      "menupanel";

    // When the personal toolbar (bookmarks) is never visible OR vertical tabs
    // are enabled, we want to place the extension action (widget) in the panel
    // and not in one of these areas, otherwise it would be invisible to the
    // user.
    if (
      (default_area === "tabstrip" &&
        Services.prefs.getBoolPref(VERTICAL_TABS_PREF)) ||
      (default_area === "personaltoolbar" &&
        Services.prefs.getStringPref(PERSONAL_TOOLBAR_VISIBILITY_PREF) ===
          "never")
    ) {
      default_area = "menupanel";
    }

    this.defaults = {
      ...this.defaults,
      badgeText: "",
      badgeBackgroundColor: [0xd9, 0, 0, 255],
      badgeDefaultColor: [255, 255, 255, 255],
      badgeTextColor: null,
      default_area,
    };
    this.globals = Object.create(this.defaults);

    // eslint-disable-next-line mozilla/balanced-listeners
    extension.on("add-permissions", () => this.updateOnChange());
    // eslint-disable-next-line mozilla/balanced-listeners
    extension.on("remove-permissions", () => this.updateOnChange());
    // eslint-disable-next-line mozilla/balanced-listeners
    extension.on("update-ignore-quarantine", () => this.updateOnChange());
    // eslint-disable-next-line mozilla/balanced-listeners
    extension.on("update-blocklist-state", () => this.updateOnChange());
  }

  async loadIconData() {
    const { extension } = this;
    const options =
      extension.manifest.browser_action || extension.manifest.action;
    this.defaults.icon = await StartupCache.get(
      extension,
      ["browserAction", "default_icon"],
      () =>
        IconDetails.normalize(
          {
            path: options.default_icon || extension.manifest.icons,
            iconType: "browserAction",
            themeIcons: options.theme_icons,
          },
          extension
        )
    );
  }

  handleLocationChange(eventType, tab, fromBrowse) {
    super.handleLocationChange(eventType, tab, fromBrowse);
    if (fromBrowse) {
      this.updateOnChange(tab);
    }
  }

  getTargetFromDetails({ tabId, windowId }) {
    if (tabId != null && windowId != null) {
      throw new ExtensionError(
        "Only one of tabId and windowId can be specified."
      );
    }
    if (tabId != null) {
      return this.getTab(tabId);
    } else if (windowId != null) {
      return this.getWindow(windowId);
    }
    return null;
  }

  getDefaultArea() {
    return this.globals.default_area;
  }

  /**
   * Determines the text badge color to be used in a tab, window, or globally.
   *
   * @typedef {number[]} ColorArray from schemas/browser_action.json.
   *
   * @param {object} values
   *        The values associated with the tab or window, or global values.
   * @returns {ColorArray}
   */
  getTextColor(values) {
    // If a text color has been explicitly provided, use it.
    let { badgeTextColor } = values;
    if (badgeTextColor) {
      return badgeTextColor;
    }

    // Otherwise, check if the default color to be used has been cached previously.
    let { badgeDefaultColor } = values;
    if (badgeDefaultColor) {
      return badgeDefaultColor;
    }

    // Choose a color among white and black, maximizing contrast with background
    // according to https://www.w3.org/TR/WCAG20-TECHS/G18.html#G18-procedure
    let [r, g, b] = values.badgeBackgroundColor
      .slice(0, 3)
      .map(function (channel) {
        channel /= 255;
        if (channel <= 0.03928) {
          return channel / 12.92;
        }
        return ((channel + 0.055) / 1.055) ** 2.4;
      });
    let lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;

    // The luminance is 0 for black, 1 for white, and `lum` for the background color.
    // Since `0 <= lum`, the contrast ratio for black is `c0 = (lum + 0.05) / 0.05`.
    // Since `lum <= 1`, the contrast ratio for white is `c1 = 1.05 / (lum + 0.05)`.
    // We want to maximize contrast, so black is chosen if `c1 < c0`, that is, if
    // `1.05 * 0.05 < (L + 0.05) ** 2`. Otherwise white is chosen.
    let channel = 1.05 * 0.05 < (lum + 0.05) ** 2 ? 0 : 255;
    let result = [channel, channel, channel, 255];

    // Cache the result as high as possible in the prototype chain
    while (!Object.getOwnPropertyDescriptor(values, "badgeDefaultColor")) {
      values = Object.getPrototypeOf(values);
    }
    values.badgeDefaultColor = result;
    return result;
  }

  isShownForTab(tab) {
    return this.getProperty(tab, "enabled");
  }

  api(context) {
    return {
      ...super.api(context),
      enable: (...args) => this.enable(...args),
      disable: (...args) => this.disable(...args),
      isEnabled: details => {
        return this.getPropertyFromDetails(details, "enabled");
      },
      setBadgeText: details => {
        this.setPropertyFromDetails(details, "badgeText", details.text);
      },
      getBadgeText: details => {
        return this.getPropertyFromDetails(details, "badgeText");
      },
      setBadgeBackgroundColor: details => {
        let color = parseColor(details.color, "background");
        let values = this.setPropertyFromDetails(
          details,
          "badgeBackgroundColor",
          color
        );
        if (color === null) {
          // Let the default text color inherit after removing background color
          delete values.badgeDefaultColor;
        } else {
          // Invalidate a cached default color calculated with the old background
          values.badgeDefaultColor = null;
        }
      },
      getBadgeBackgroundColor: details => {
        return this.getPropertyFromDetails(details, "badgeBackgroundColor");
      },
      setBadgeTextColor: details => {
        let color = parseColor(details.color, "text");
        this.setPropertyFromDetails(details, "badgeTextColor", color);
      },
      getBadgeTextColor: details => {
        let target = this.getTargetFromDetails(details);
        let values = this.getContextData(target);
        return this.getTextColor(values);
      },
    };
  }
}
