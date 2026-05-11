/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  UrlbarTokenizer:
    "moz-src:///browser/components/urlbar/UrlbarTokenizer.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "touchBarUpdater",
  "@mozilla.org/widget/touchbarupdater;1",
  Ci.nsITouchBarUpdater
);

// For accessing TouchBarHelper methods from static contexts in this file.
XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "touchBarHelper",
  "@mozilla.org/widget/touchbarhelper;1",
  Ci.nsITouchBarHelper
);

/**
 * Executes a XUL command on the top window. Called by the callbacks in each
 * TouchBarInput.
 *
 * @param {string} commandName
 *        A XUL command.
 */
function execCommand(commandName) {
  if (!TouchBarHelper.window) {
    return;
  }
  let command = TouchBarHelper.window.document.getElementById(commandName);
  if (command) {
    command.doCommand();
  }
}

/**
 * Static helper function to convert a hexadecimal string to its integer
 * value. Used to convert colours to a format accepted by Apple's NSColor code.
 *
 * @param {string} hexString
 *        A hexadecimal string, optionally beginning with '#'.
 */
function hexToInt(hexString) {
  if (!hexString) {
    return null;
  }
  if (hexString.charAt(0) == "#") {
    hexString = hexString.slice(1);
  }
  let val = parseInt(hexString, 16);
  return isNaN(val) ? null : val;
}

const kInputTypes = {
  BUTTON: "button",
  LABEL: "label",
  MAIN_BUTTON: "mainButton",
  POPOVER: "popover",
  SCROLLVIEW: "scrollView",
  SCRUBBER: "scrubber",
};

/**
 * An object containing all implemented TouchBarInput objects.
 */
var gBuiltInInputs = {
  Back: {
    title: "back",
    image: "chrome://browser/skin/back.svg",
    type: kInputTypes.BUTTON,
    callback: () => {
      lazy.touchBarHelper.unfocusUrlbar();
      execCommand("Browser:Back");
    },
  },
  Forward: {
    title: "forward",
    image: "chrome://browser/skin/forward.svg",
    type: kInputTypes.BUTTON,
    callback: () => {
      lazy.touchBarHelper.unfocusUrlbar();
      execCommand("Browser:Forward");
    },
  },
  Reload: {
    title: "reload",
    image: "chrome://global/skin/icons/reload.svg",
    type: kInputTypes.BUTTON,
    callback: () => {
      lazy.touchBarHelper.unfocusUrlbar();
      execCommand("Browser:Reload");
    },
  },
  Home: {
    title: "home",
    image: "chrome://browser/skin/home.svg",
    type: kInputTypes.BUTTON,
    callback: () => {
      let win = lazy.BrowserWindowTracker.getTopWindow();
      win.BrowserCommands.home();
    },
  },
  Fullscreen: {
    title: "fullscreen",
    image: "chrome://browser/skin/fullscreen.svg",
    type: kInputTypes.BUTTON,
    callback: () => execCommand("View:FullScreen"),
  },
  Find: {
    title: "find",
    image: "chrome://global/skin/icons/search-glass.svg",
    type: kInputTypes.BUTTON,
    callback: () => execCommand("cmd_find"),
  },
  NewTab: {
    title: "new-tab",
    image: "chrome://global/skin/icons/plus.svg",
    type: kInputTypes.BUTTON,
    callback: () => execCommand("cmd_newNavigatorTabNoEvent"),
  },
  Sidebar: {
    title: "open-sidebar",
    image: "chrome://browser/skin/sidebar-collapsed.svg",
    type: kInputTypes.BUTTON,
    callback: () => {
      let win = lazy.BrowserWindowTracker.getTopWindow();
      win.SidebarController.toggle();
    },
  },
  AddBookmark: {
    title: "add-bookmark",
    image: "chrome://browser/skin/bookmark-hollow.svg",
    type: kInputTypes.BUTTON,
    callback: () => execCommand("Browser:AddBookmarkAs"),
  },
  ReaderView: {
    title: "reader-view",
    image: "chrome://browser/skin/reader-mode.svg",
    type: kInputTypes.BUTTON,
    callback: () => execCommand("View:ReaderView"),
    disabled: true, // Updated when the page is found to be Reader View-able.
  },
  OpenLocation: {
    key: "open-location",
    title: "open-location",
    image: "chrome://global/skin/icons/search-glass.svg",
    type: kInputTypes.MAIN_BUTTON,
    callback: () => lazy.touchBarHelper.toggleFocusUrlbar(),
  },
  // This is a special-case `type: kInputTypes.SCRUBBER` element.
  // Scrubbers are not yet generally implemented.
  // See follow-up bug 1502539.
  Share: {
    title: "share",
    image: "chrome://browser/skin/share.svg",
    type: kInputTypes.SCRUBBER,
    callback: () => execCommand("cmd_share"),
  },
  SearchPopover: {
    title: "search-popover",
    image: "chrome://global/skin/icons/search-glass.svg",
    type: kInputTypes.POPOVER,
    children: {
      SearchScrollViewLabel: {
        title: "search-search-in",
        type: kInputTypes.LABEL,
      },
      SearchScrollView: {
        key: "search-scrollview",
        type: kInputTypes.SCROLLVIEW,
        children: {
          Bookmarks: {
            title: "search-bookmarks",
            type: kInputTypes.BUTTON,
            callback: () =>
              lazy.touchBarHelper.insertRestrictionInUrlbar(
                lazy.UrlbarTokenizer.RESTRICT.BOOKMARK
              ),
          },
          OpenTabs: {
            title: "search-opentabs",
            type: kInputTypes.BUTTON,
            callback: () =>
              lazy.touchBarHelper.insertRestrictionInUrlbar(
                lazy.UrlbarTokenizer.RESTRICT.OPENPAGE
              ),
          },
          History: {
            title: "search-history",
            type: kInputTypes.BUTTON,
            callback: () =>
              lazy.touchBarHelper.insertRestrictionInUrlbar(
                lazy.UrlbarTokenizer.RESTRICT.HISTORY
              ),
          },
          Tags: {
            title: "search-tags",
            type: kInputTypes.BUTTON,
            callback: () =>
              lazy.touchBarHelper.insertRestrictionInUrlbar(
                lazy.UrlbarTokenizer.RESTRICT.TAG
              ),
          },
        },
      },
    },
  },
};

// We create a new flat object to cache strings. Since gBuiltInInputs is a
// tree, caching/retrieval of localized strings would otherwise require tree
// traversal.
var localizedStrings = {};

const kHelperObservers = new Set([
  "bookmark-icon-updated",
  "fullscreen-painted",
  "reader-mode-available",
  "touchbar-location-change",
  "quit-application",
  "intl:app-locales-changed",
  "urlbar-focus",
  "urlbar-blur",
]);

/**
 * JS-implemented TouchBarHelper class.
 * Provides services to the Mac Touch Bar.
 */
export class TouchBarHelper {
  constructor() {
    for (let topic of kHelperObservers) {
      Services.obs.addObserver(this, topic);
    }
    // We cache our search popover since otherwise it is frequently
    // created/destroyed for the urlbar-focus/blur events.
    this._searchPopover = this.getTouchBarInput("SearchPopover");

    this._inputsNotUpdated = new Set();
  }

  destructor() {
    this._searchPopover = null;
    for (let topic of kHelperObservers) {
      Services.obs.removeObserver(this, topic);
    }
  }

  get activeTitle() {
    if (!TouchBarHelper.window) {
      return "";
    }
    let tabbrowser = TouchBarHelper.window.gBrowser;
    let activeTitle;
    if (tabbrowser) {
      activeTitle = tabbrowser.selectedBrowser.contentTitle;
    }
    return activeTitle;
  }

  get allItems() {
    let layoutItems = Cc["@mozilla.org/array;1"].createInstance(
      Ci.nsIMutableArray
    );

    let window = TouchBarHelper.window;
    if (
      !window ||
      !window.isChromeWindow ||
      window.document.documentElement.getAttribute("windowtype") !=
        "navigator:browser"
    ) {
      return layoutItems;
    }

    // Every input must be updated at least once so that all assets (titles,
    // icons) are loaded. We keep track of which inputs haven't updated and
    // run an update on them ASAP.
    this._inputsNotUpdated.clear();

    for (let inputName of Object.keys(gBuiltInInputs)) {
      let input = this.getTouchBarInput(inputName);
      if (!input) {
        continue;
      }
      this._inputsNotUpdated.add(inputName);
      layoutItems.appendElement(input);
    }

    return layoutItems;
  }

  static get window() {
    return lazy.BrowserWindowTracker.getTopWindow();
  }

  get document() {
    if (!TouchBarHelper.window) {
      return null;
    }
    return TouchBarHelper.window.document;
  }

  get isUrlbarFocused() {
    if (!TouchBarHelper.window || !TouchBarHelper.window.gURLBar) {
      return false;
    }
    return TouchBarHelper.window.gURLBar.focused;
  }

  toggleFocusUrlbar() {
    if (this.isUrlbarFocused) {
      this.unfocusUrlbar();
    } else {
      execCommand("Browser:OpenLocation");
    }
  }

  unfocusUrlbar() {
    if (!this.isUrlbarFocused) {
      return;
    }
    TouchBarHelper.window.gURLBar.blur();
  }

  static get baseWindow() {
    return TouchBarHelper.window
      ? TouchBarHelper.window.docShell.treeOwner.QueryInterface(
          Ci.nsIBaseWindow
        )
      : null;
  }

  getTouchBarInput(inputName) {
    if (inputName == "SearchPopover" && this._searchPopover) {
      return this._searchPopover;
    }

    if (!inputName || !gBuiltInInputs.hasOwnProperty(inputName)) {
      return null;
    }

    let inputData = gBuiltInInputs[inputName];

    let item = new TouchBarInput(inputData);

    // Skip localization if there is already a cached localized title or if
    // no title is needed.
    if (
      !inputData.hasOwnProperty("title") ||
      localizedStrings[inputData.title]
    ) {
      return item;
    }

    // Async l10n fills in the localized input labels after the initial load.
    this._l10n.formatValue(inputData.title).then(result => {
      item.title = result;
      localizedStrings[inputData.title] = result; // Cache result.
      // Checking TouchBarHelper.window since this callback can fire after all windows are closed.
      if (TouchBarHelper.window) {
        if (this._inputsNotUpdated) {
          this._inputsNotUpdated.delete(inputName);
        }
        lazy.touchBarUpdater.updateTouchBarInputs(TouchBarHelper.baseWindow, [
          item,
        ]);
      }
    });

    return item;
  }

  /**
   * Fetches a specific Touch Bar Input by name and updates it on the Touch Bar.
   *
   * @param {...*} inputNames
   *        A key/keys to a value/values in the gBuiltInInputs object in this file.
   */
  _updateTouchBarInputs(...inputNames) {
    if (!TouchBarHelper.window || !inputNames.length) {
      return;
    }

    let inputs = [];
    for (let inputName of new Set([...inputNames, ...this._inputsNotUpdated])) {
      let input = this.getTouchBarInput(inputName);
      if (!input) {
        continue;
      }

      this._inputsNotUpdated.delete(inputName);
      inputs.push(input);
    }

    lazy.touchBarUpdater.updateTouchBarInputs(
      TouchBarHelper.baseWindow,
      inputs
    );
  }

  /**
   * Inserts a restriction token into the Urlbar ahead of the current typed
   * search term.
   *
   * @param {string} restrictionToken
   *        The restriction token to be inserted into the Urlbar. Preferably
   *        sourced from UrlbarTokenizer.RESTRICT.
   */
  insertRestrictionInUrlbar(restrictionToken) {
    if (!TouchBarHelper.window) {
      return;
    }
    let searchString = "";
    if (
      TouchBarHelper.window.gURLBar.getAttribute("pageproxystate") != "valid"
    ) {
      searchString = TouchBarHelper.window.gURLBar.lastSearchString.trimStart();
      if (
        Object.values(lazy.UrlbarTokenizer.RESTRICT).includes(searchString[0])
      ) {
        searchString = searchString.substring(1).trimStart();
      }
    }

    TouchBarHelper.window.gURLBar.search(
      `${restrictionToken} ${searchString}`,
      { searchModeEntry: "touchbar" }
    );
  }

  observe(subject, topic, data) {
    switch (topic) {
      case "touchbar-location-change": {
        let updatedInputs = ["Back", "Forward"];
        gBuiltInInputs.Back.disabled =
          !TouchBarHelper.window.gBrowser.canGoBack;
        gBuiltInInputs.Forward.disabled =
          !TouchBarHelper.window.gBrowser.canGoForward;
        if (subject.QueryInterface(Ci.nsIWebProgress)?.isTopLevel) {
          this.activeUrl = data;
          // ReaderView button is disabled on every toplevel location change
          // since Reader View must determine if the new page can be Reader
          // Viewed.
          updatedInputs.push("ReaderView");
          gBuiltInInputs.ReaderView.disabled = !data.startsWith("about:reader");
        }
        this._updateTouchBarInputs(...updatedInputs);
        break;
      }
      case "fullscreen-painted":
        if (TouchBarHelper.window.document.fullscreenElement) {
          gBuiltInInputs.OpenLocation.title = "touchbar-fullscreen-exit";
          gBuiltInInputs.OpenLocation.image =
            "chrome://browser/skin/fullscreen-exit.svg";
          gBuiltInInputs.OpenLocation.callback = () => {
            TouchBarHelper.window.windowUtils.exitFullscreen();
          };
        } else {
          gBuiltInInputs.OpenLocation.title = "open-location";
          gBuiltInInputs.OpenLocation.image =
            "chrome://global/skin/icons/search-glass.svg";
          gBuiltInInputs.OpenLocation.callback = () =>
            execCommand("Browser:OpenLocation", "OpenLocation");
        }
        this._updateTouchBarInputs("OpenLocation");
        break;
      case "bookmark-icon-updated":
        gBuiltInInputs.AddBookmark.image =
          data == "starred"
            ? "chrome://browser/skin/bookmark.svg"
            : "chrome://browser/skin/bookmark-hollow.svg";
        this._updateTouchBarInputs("AddBookmark");
        break;
      case "reader-mode-available":
        gBuiltInInputs.ReaderView.disabled = false;
        this._updateTouchBarInputs("ReaderView");
        break;
      case "urlbar-focus":
        if (!this._searchPopover) {
          this._searchPopover = this.getTouchBarInput("SearchPopover");
        }
        lazy.touchBarUpdater.showPopover(
          TouchBarHelper.baseWindow,
          this._searchPopover,
          true
        );
        break;
      case "urlbar-blur":
        if (!this._searchPopover) {
          this._searchPopover = this.getTouchBarInput("SearchPopover");
        }
        lazy.touchBarUpdater.showPopover(
          TouchBarHelper.baseWindow,
          this._searchPopover,
          false
        );
        break;
      case "intl:app-locales-changed":
        this._searchPopover = null;
        localizedStrings = {};

        // This event can fire before this._l10n updates to switch languages,
        // so all the new translations are in the old language. To avoid this,
        // we need to reinitialize this._l10n.
        this._l10n = new Localization(["browser/touchbar/touchbar.ftl"]);
        helperProto._l10n = this._l10n;

        this._updateTouchBarInputs(...Object.keys(gBuiltInInputs));
        break;
      case "quit-application":
        this.destructor();
        break;
    }
  }
}

const helperProto = TouchBarHelper.prototype;
helperProto.QueryInterface = ChromeUtils.generateQI(["nsITouchBarHelper"]);
helperProto._l10n = new Localization(["browser/touchbar/touchbar.ftl"]);

/**
 * A representation of a Touch Bar input.
 *
 *     @param {object} input
 *            An object representing a Touch Bar Input.
 *            Contains listed properties.
 *     @param {string} input.title
 *            The lookup key for the button's localized text title.
 *     @param {string} input.image
 *            A URL pointing to an SVG internal to Firefox.
 *     @param {string} input.type
 *            The type of Touch Bar input represented by the object.
 *            Must be a value from kInputTypes.
 *     @param {Function} input.callback
 *            A callback invoked when a touchbar item is touched.
 *     @param {string} [input.color]
 *            A string in hex format specifying the button's background color.
 *            If omitted, the default background color is used.
 *     @param {bool} [input.disabled]
 *            If `true`, the Touch Bar input is greyed out and inoperable.
 *     @param {Array} [input.children]
 *            An array of input objects that will be displayed as children of
 *            this input. Available only for types KInputTypes.POPOVER and
 *            kInputTypes.SCROLLVIEW.
 */
export class TouchBarInput {
  constructor(input) {
    this._key = input.key || input.title;
    this._title = localizedStrings[input.title] || "";
    this._image = input.image;
    this._type = input.type;
    this._callback = input.callback;
    this._color = hexToInt(input.color);
    this._disabled = input.hasOwnProperty("disabled") ? input.disabled : false;
    if (input.children) {
      this._children = [];
      let toLocalize = [];
      for (let childData of Object.values(input.children)) {
        let initializedChild = new TouchBarInput(childData);
        if (!initializedChild) {
          continue;
        }
        // Children's types are prepended by the parent's type. This is so we
        // can uniquely identify a child input from a standalone input with
        // the same name. (e.g. a button called "back" in a popover would be a
        // "popover-button.back" vs. a "button.back").
        initializedChild.type = input.type + "-" + initializedChild.type;
        this._children.push(initializedChild);
        // Skip l10n for inputs without a title or those already localized.
        if (childData.title && !localizedStrings[childData.title]) {
          toLocalize.push(initializedChild);
        }
      }
      this._localizeChildren(toLocalize);
    }
  }

  get key() {
    return this._key;
  }
  get title() {
    return this._title;
  }
  set title(title) {
    this._title = title;
  }
  get image() {
    return this._image ? Services.io.newURI(this._image) : null;
  }
  set image(image) {
    this._image = image;
  }
  get type() {
    return this._type == "" ? "button" : this._type;
  }
  set type(type) {
    this._type = type;
  }
  get callback() {
    return this._callback;
  }
  set callback(callback) {
    this._callback = callback;
  }
  get color() {
    return this._color;
  }
  set color(color) {
    this._color = this.hexToInt(color);
  }
  get disabled() {
    return this._disabled || false;
  }
  set disabled(disabled) {
    this._disabled = disabled;
  }
  get children() {
    if (!this._children) {
      return null;
    }
    let children = Cc["@mozilla.org/array;1"].createInstance(
      Ci.nsIMutableArray
    );
    for (let child of this._children) {
      children.appendElement(child);
    }
    return children;
  }

  /**
   * Apply Fluent l10n to child inputs.
   *
   * @param {Array} children
   *   An array of initialized TouchBarInputs.
   */
  async _localizeChildren(children) {
    if (!children || !children.length) {
      return;
    }

    let titles = await helperProto._l10n.formatValues(
      children.map(child => ({ id: child.key }))
    );
    // In the TouchBarInput constuctor, we filtered so children contains only
    // those inputs with titles to be localized. We can be confident that the
    // results in titles match up with the inputs to be localized.
    children.forEach(function (child, index) {
      child.title = titles[index];
      localizedStrings[child.key] = child.title;
    });

    lazy.touchBarUpdater.updateTouchBarInputs(
      TouchBarHelper.baseWindow,
      children
    );
  }
}

TouchBarInput.prototype.QueryInterface = ChromeUtils.generateQI([
  "nsITouchBarInput",
]);
