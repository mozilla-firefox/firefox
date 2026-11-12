/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global gSubDialog */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";

/** @import {Integration} from "toolkit/modules/Integration.sys.mjs"; */

var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
var { getMozRemoteImageURL } = ChromeUtils.importESModule(
  "moz-src:///toolkit/modules/FaviconUtils.sys.mjs"
);

const gHandlerService = Cc[
  "@mozilla.org/uriloader/handler-service;1"
].getService(Ci.nsIHandlerService);

const gMIMEService = Cc["@mozilla.org/mime;1"].getService(Ci.nsIMIMEService);

/**
 * @type {Record<string, any>}
 */
const lazy = {};

/**
 * @type {Integration}
 */
var Integration = ChromeUtils.importESModule(
  "resource://gre/modules/Integration.sys.mjs"
).Integration;
Integration.downloads.defineESModuleGetter(
  lazy,
  "DownloadIntegration",
  "resource://gre/modules/DownloadIntegration.sys.mjs"
);

/**
 * @type {nsIGIOService | null}
 */
let gGIOService = null;

if (Cc["@mozilla.org/gio-service;1"]) {
  gGIOService = Cc["@mozilla.org/gio-service;1"].getService(Ci.nsIGIOService);
}

var { FileUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/FileUtils.sys.mjs"
);

var Downloads = ChromeUtils.importESModule(
  "resource://gre/modules/Downloads.sys.mjs"
).Downloads;

Preferences.addAll([
  { id: "browser.download.useDownloadDir", type: "bool", inverted: true },
  { id: "browser.download.enableDeletePrivate", type: "bool" },
  { id: "browser.download.deletePrivate", type: "bool" },
  { id: "browser.download.always_ask_before_handling_new_types", type: "bool" },
  { id: "browser.download.folderList", type: "int" },
  { id: "browser.download.dir", type: "file" },
  { id: "pref.downloads.disable_button.edit_actions", type: "bool" },
]);

// Downloads
/*
 * Preferences:
 *
 * browser.download.useDownloadDir - bool
 *   True - Save files directly to the folder configured via the
 *   browser.download.folderList preference.
 *   False - Always ask the user where to save a file and default to
 *  browser.download.lastDir when displaying a folder picker dialog.
 *  browser.download.deletePrivate - bool
 *   True - Delete files that were downloaded in a private browsing session
 *   on close of the session
 *   False - Keep files that were downloaded in a private browsing
 *   session
 * browser.download.always_ask_before_handling_new_types - bool
 *   Defines the default behavior for new file handlers.
 *   True - When downloading a file that doesn't match any existing
 *   handlers, ask the user whether to save or open the file.
 *   False - Save the file. The user can change the default action in
 *   the Applications section in the preferences UI.
 * browser.download.dir - local file handle
 *   A local folder the user may have selected for downloaded files to be
 *   saved. Migration of other browser settings may also set this path.
 *   This folder is enabled when folderList equals 2.
 * browser.download.lastDir - local file handle
 *   May contain the last folder path accessed when the user browsed
 *   via the file save-as dialog. (see contentAreaUtils.js)
 * browser.download.folderList - int
 *   Indicates the location users wish to save downloaded files too.
 *   It is also used to display special file labels when the default
 *   download location is either the Desktop or the Downloads folder.
 *   Values:
 *     0 - The desktop is the default download location.
 *     1 - The system's downloads folder is the default download location.
 *     2 - The default download location is elsewhere as specified in
 *         browser.download.dir.
 * browser.download.downloadDir
 *   deprecated.
 * browser.download.defaultFolder
 *   deprecated.
 */

/**
 * Helper object for managing the various downloads related settings.
 */
let DownloadsHelpers = new (class DownloadsHelpers {
  folder;
  folderPath;
  folderHostPath;
  displayName;
  downloadsDir;
  desktopDir;
  downloadsFolderLocalizedName;
  desktopFolderLocalizedName;

  setupDownloadsHelpersFields = async () => {
    this.downloadsDir = await this._getDownloadsFolder("Downloads");
    this.desktopDir = await this._getDownloadsFolder("Desktop");
    [this.downloadsFolderLocalizedName, this.desktopFolderLocalizedName] =
      await document.l10n.formatValues([
        { id: "downloads-folder-name" },
        { id: "desktop-folder-name" },
      ]);
  };

  /**
   * Returns the Downloads folder.  If aFolder is "Desktop", then the Downloads
   * folder returned is the desktop folder; otherwise, it is a folder whose name
   * indicates that it is a download folder and whose path is as determined by
   * the XPCOM directory service via the download manager's attribute
   * defaultDownloadsDirectory.
   *
   * @throws if aFolder is not "Desktop" or "Downloads"
   */
  async _getDownloadsFolder(aFolder) {
    switch (aFolder) {
      case "Desktop":
        return Services.dirsvc.get("Desk", Ci.nsIFile);
      case "Downloads": {
        let downloadsDir = await Downloads.getSystemDownloadsDirectory();
        return new FileUtils.File(downloadsDir);
      }
    }
    throw new Error(
      "ASSERTION FAILED: folder type should be 'Desktop' or 'Downloads'"
    );
  }

  _getSystemDownloadFolderDetails(folderIndex) {
    let currentDirPref = Preferences.get("browser.download.dir");

    let file;
    let firefoxLocalizedName;
    if (folderIndex == 2 && currentDirPref.value) {
      file = currentDirPref.value;
      if (this.downloadsDir && file.equals(this.downloadsDir)) {
        folderIndex = 1;
      } else if (this.desktopDir && file.equals(this.desktopDir)) {
        folderIndex = 0;
      }
    }
    switch (folderIndex) {
      case 2: // custom path, handled above.
        break;

      case 1: {
        // downloads
        file = this.downloadsDir;
        firefoxLocalizedName = this.downloadsFolderLocalizedName;
        break;
      }

      case 0:
      // fall through
      default: {
        file = this.desktopDir;
        firefoxLocalizedName = this.desktopFolderLocalizedName;
      }
    }

    if (file) {
      let displayName = file.path;

      // Attempt to translate path to the path as exists on the host
      // in case the provided path comes from the document portal
      if (AppConstants.platform == "linux") {
        if (this.folderHostPath && displayName == this.folderPath) {
          displayName = this.folderHostPath;
          if (displayName == this.downloadsDir?.path) {
            firefoxLocalizedName = this.downloadsFolderLocalizedName;
          } else if (displayName == this.desktopDir?.path) {
            firefoxLocalizedName = this.desktopFolderLocalizedName;
          }
        } else if (displayName != this.folderPath) {
          this.folderHostPath = null;
          try {
            file.hostPath().then(folderHostPath => {
              this.folderHostPath = folderHostPath;
              Preferences.getSetting("downloadFolder")?.onChange();
            });
          } catch (error) {
            /* ignored */
          }
        }
      }

      if (firefoxLocalizedName) {
        let folderDisplayName, leafName;
        // Either/both of these can throw, so check for failures in both cases
        // so we don't just break display of the download pref:
        try {
          folderDisplayName = file.displayName;
        } catch (ex) {
          /* ignored */
        }
        try {
          leafName = file.leafName;
        } catch (ex) {
          /* ignored */
        }

        // If we found a localized name that's different from the leaf name,
        // use that:
        if (folderDisplayName && folderDisplayName != leafName) {
          return { file, folderDisplayName };
        }

        // Otherwise, check if we've got a localized name ourselves.
        // You can't move the system download or desktop dir on macOS,
        // so if those are in use just display them. On other platforms
        // only do so if the folder matches the localized name.
        if (
          AppConstants.platform == "macosx" ||
          leafName == firefoxLocalizedName
        ) {
          return { file, folderDisplayName: firefoxLocalizedName };
        }
      }

      // If we get here, attempts to use a "pretty" name failed. Just display
      // the full path:
      // Force the left-to-right direction when displaying a custom path.
      return { file, folderDisplayName: `\u2066${displayName}\u2069` };
    }

    // Don't even have a file - fall back to desktop directory for the
    // use of the icon, and an empty label:
    file = this.desktopDir;
    return { file, folderDisplayName: "" };
  }

  /**
   * Determines the type of the given folder.
   *
   * @param   aFolder
   *          the folder whose type is to be determined
   * @returns integer
   *          0 if aFolder is the Desktop or is unspecified,
   *          1 if aFolder is the Downloads folder,
   *          2 otherwise
   */
  _folderToIndex(aFolder) {
    if (!aFolder || aFolder.equals(this.desktopDir)) {
      return 0;
    } else if (aFolder.equals(this.downloadsDir)) {
      return 1;
    }
    return 2;
  }

  getFolderDetails() {
    let folderIndex = Preferences.get("browser.download.folderList").value;
    let { folderDisplayName, file } =
      this._getSystemDownloadFolderDetails(folderIndex);

    this.folderPath = file?.path ?? "";
    this.displayName = folderDisplayName;
  }

  setFolder(folder) {
    this.folder = folder;

    let folderListPref = Preferences.get("browser.download.folderList");
    folderListPref.value = this._folderToIndex(this.folder);
  }
})();

Preferences.addSetting({
  id: "browserDownloadFolderList",
  pref: "browser.download.folderList",
});
Preferences.addSetting({
  id: "downloadFolder",
  pref: "browser.download.dir",
  deps: ["browserDownloadFolderList"],
  get() {
    DownloadsHelpers.getFolderDetails();
    return DownloadsHelpers.folderPath;
  },
  set(folder) {
    DownloadsHelpers.setFolder(folder);
    return DownloadsHelpers.folder;
  },
  getControlConfig(config) {
    if (DownloadsHelpers.displayName) {
      return {
        ...config,
        controlAttrs: {
          ...config.controlAttrs,
          ".displayValue": DownloadsHelpers.displayName,
        },
      };
    }
    return {
      ...config,
    };
  },
  setup(emitChange) {
    DownloadsHelpers.setupDownloadsHelpersFields().then(emitChange);
  },
  disabled: ({ browserDownloadFolderList }) => {
    return browserDownloadFolderList.locked;
  },
});
Preferences.addSetting({
  id: "alwaysAsk",
  pref: "browser.download.useDownloadDir",
});

Preferences.addSetting({
  id: "applicationsHandlersView",
  setup: emitChange => {
    emitChange();

    /**
     * @param {CustomEvent} event
     */
    async function appInitializer(event) {
      if (
        event.detail.category != "paneDownloads" &&
        event.detail.category != "paneSearchResults"
      ) {
        return;
      }
      await ApplicationsHandler.preInitApplications();
      /**
       * Need to send an observer notification so that tests will know when
       * everything in the applicationsHandlersView is built and loaded.
       */
      Services.obs.notifyObservers(window, "app-handler-loaded");
      window.removeEventListener("paneshown", appInitializer);
    }
    // Load the data and build the list of handlers for applications
    // pane after page is shown to ensure it doesn't delay painting
    // of the preferences page.
    window.addEventListener("paneshown", appInitializer);
    return () => {
      window.removeEventListener("paneshown", appInitializer);
    };
  },
});

Preferences.addSetting({
  id: "applicationsFilter",
  get(val) {
    return val || "";
  },
});

Preferences.addSetting({
  id: "handleNewFileTypes",
  pref: "browser.download.always_ask_before_handling_new_types",
});

Preferences.addSetting({
  id: "enableDeletePrivate",
  pref: "browser.download.enableDeletePrivate",
});
Preferences.addSetting({
  id: "deletePrivate",
  pref: "browser.download.deletePrivate",
  deps: ["enableDeletePrivate"],
  visible: ({ enableDeletePrivate }) => enableDeletePrivate.value,
  onUserChange() {
    Services.prefs.setBoolPref("browser.download.deletePrivate.chosen", true);
  },
});

const ICON_URL_APP =
  AppConstants.platform == "linux"
    ? "moz-icon://dummy.exe?size=16"
    : "chrome://browser/skin/preferences/application.png";

/**
 * This object wraps nsIHandlerInfo with some additional functionality
 * the Applications prefpane needs to display and allow modification of
 * the list of handled types.
 *
 * We create an instance of this wrapper for each entry we might display
 * in the prefpane, and we compose the instances from various sources,
 * including the handler service.
 *
 * We don't implement all the original nsIHandlerInfo functionality,
 * just the stuff that the prefpane needs.
 */
class HandlerInfoWrapper {
  /**
   * @type {nsIHandlerInfo}
   */
  wrappedHandlerInfo;

  /**
   * @param {string} type
   * @param {nsIHandlerInfo} handlerInfo
   */
  constructor(type, handlerInfo) {
    this.type = type;
    this.wrappedHandlerInfo = handlerInfo;
    this.disambiguateDescription = false;
  }

  get description() {
    if (this.wrappedHandlerInfo.description) {
      return { raw: this.wrappedHandlerInfo.description };
    }

    if (this.primaryExtension) {
      var extension = this.primaryExtension.toUpperCase();
      return { id: "applications-file-ending", args: { extension } };
    }

    return { raw: this.type };
  }

  /**
   * Describe, in a human-readable fashion, the type represented by the given
   * handler info object.  Normally this is just the description, but if more
   * than one object presents the same description, "disambiguateDescription"
   * is set and we annotate the duplicate descriptions with the type itself
   * to help users distinguish between those types.
   */
  get typeDescription() {
    if (this.disambiguateDescription) {
      const description = this.description;
      if (description.id) {
        // Pass through the arguments:
        let { args = {} } = description;
        args.type = this.type;
        return {
          id: description.id + "-with-type",
          args,
        };
      }

      return {
        id: "applications-type-description-with-type",
        args: {
          "type-description": description.raw,
          type: this.type,
        },
      };
    }

    return this.description;
  }

  get iconURLForSystemDefault() {
    // Handler info objects for MIME types on some OSes implement a property bag
    // interface from which we can get an icon for the default app, so if we're
    // dealing with a MIME type on one of those OSes, then try to get the icon.
    if (
      this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo &&
      this.wrappedHandlerInfo instanceof Ci.nsIPropertyBag
    ) {
      try {
        let url = this.wrappedHandlerInfo.getProperty(
          "defaultApplicationIconURL"
        );
        if (url) {
          return url + "?size=16";
        }
      } catch (ex) {}
    }

    // If this isn't a MIME type object on an OS that supports retrieving
    // the icon, or if we couldn't retrieve the icon for some other reason,
    // then use a generic icon.
    return ICON_URL_APP;
  }

  /**
   * @type {nsIHandlerApp | undefined}
   */
  get preferredApplicationHandler() {
    return this.wrappedHandlerInfo.preferredApplicationHandler;
  }

  set preferredApplicationHandler(aNewValue) {
    this.wrappedHandlerInfo.preferredApplicationHandler = aNewValue;

    // Make sure the preferred handler is in the set of possible handlers.
    if (aNewValue) {
      this.addPossibleApplicationHandler(aNewValue);
    }
  }

  get possibleApplicationHandlers() {
    return this.wrappedHandlerInfo.possibleApplicationHandlers;
  }

  /**
   * @param {nsIHandlerApp} aNewHandler
   * @returns {void}
   */
  addPossibleApplicationHandler(aNewHandler) {
    for (let app of this.possibleApplicationHandlers.enumerate()) {
      if (app.equals(aNewHandler)) {
        return;
      }
    }
    this.possibleApplicationHandlers.appendElement(aNewHandler);
  }

  /**
   * @param {nsIHandlerApp} aHandler
   * @returns {void}
   */
  removePossibleApplicationHandler(aHandler) {
    var defaultApp = this.preferredApplicationHandler;
    if (defaultApp && aHandler.equals(defaultApp)) {
      // If the app we remove was the default app, we must make sure
      // it won't be used anymore
      this.alwaysAskBeforeHandling = true;
      this.preferredApplicationHandler = null;
    }

    var handlers = this.possibleApplicationHandlers;
    for (var i = 0; i < handlers.length; ++i) {
      var handler = handlers.queryElementAt(i, Ci.nsIHandlerApp);
      if (handler.equals(aHandler)) {
        handlers.removeElementAt(i);
        break;
      }
    }
  }

  get hasDefaultHandler() {
    return this.wrappedHandlerInfo.hasDefaultHandler;
  }

  get defaultDescription() {
    return this.wrappedHandlerInfo.defaultDescription;
  }

  // What to do with content of this type.
  get preferredAction() {
    // If the action is to use a helper app, but we don't have a preferred
    // handler app, then switch to using the system default, if any; otherwise
    // fall back to saving to disk, which is the default action in nsMIMEInfo.
    // Note: "save to disk" is an invalid value for protocol info objects,
    // but the alwaysAskBeforeHandling getter will detect that situation
    // and always return true in that case to override this invalid value.
    if (
      this.wrappedHandlerInfo.preferredAction ==
        Ci.nsIHandlerInfo.useHelperApp &&
      !ApplicationsHandler.isValidHandlerApp(this.preferredApplicationHandler)
    ) {
      if (this.wrappedHandlerInfo.hasDefaultHandler) {
        return Ci.nsIHandlerInfo.useSystemDefault;
      }
      return Ci.nsIHandlerInfo.saveToDisk;
    }

    return this.wrappedHandlerInfo.preferredAction;
  }

  set preferredAction(aNewValue) {
    this.wrappedHandlerInfo.preferredAction = aNewValue;
  }

  get alwaysAskBeforeHandling() {
    // If this is a protocol type and the preferred action is "save to disk",
    // which is invalid for such types, then return true here to override that
    // action.  This could happen when the preferred action is to use a helper
    // app, but the preferredApplicationHandler is invalid, and there isn't
    // a default handler, so the preferredAction getter returns save to disk
    // instead.
    if (
      !(this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) &&
      this.preferredAction == Ci.nsIHandlerInfo.saveToDisk
    ) {
      return true;
    }

    return this.wrappedHandlerInfo.alwaysAskBeforeHandling;
  }

  set alwaysAskBeforeHandling(aNewValue) {
    this.wrappedHandlerInfo.alwaysAskBeforeHandling = aNewValue;
  }

  // The primary file extension associated with this type, if any.
  get primaryExtension() {
    try {
      if (
        this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo &&
        this.wrappedHandlerInfo.primaryExtension
      ) {
        return this.wrappedHandlerInfo.primaryExtension;
      }
    } catch (ex) {}

    return null;
  }

  store() {
    gHandlerService.store(this.wrappedHandlerInfo);
  }

  /**
   * @param {number} aSize
   * @param {number} aScale
   * @returns {string | null}
   */
  _getIcon(aSize, aScale = 1) {
    if (this.primaryExtension) {
      return `moz-icon://goat.${this.primaryExtension}?size=${aSize}&scale=${aScale}`;
    }

    if (this.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) {
      return `moz-icon://goat?size=${aSize}&scale=${aScale}&contentType=${this.type}`;
    }

    // FIXME: consider returning some generic icon when we can't get a URL for
    // one (for example in the case of protocol schemes).  Filed as bug 395141.
    return null;
  }
}

/**
 * InternalHandlerInfoWrapper provides a basic mechanism to create an internal
 * mime type handler that can be enabled/disabled in the applications preference
 * menu.
 */
class InternalHandlerInfoWrapper extends HandlerInfoWrapper {
  constructor(mimeType, extension) {
    let type = gMIMEService.getFromTypeAndExtension(mimeType, extension);
    super(mimeType || type.type, type);
  }

  // Override store so we so we can notify any code listening for registration
  // or unregistration of this handler.
  store() {
    super.store();
  }

  get preventInternalViewing() {
    return false;
  }

  get enabled() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  }
}

class PDFHandlerInfoWrapper extends InternalHandlerInfoWrapper {
  constructor() {
    super("application/pdf", null);
  }

  get preventInternalViewing() {
    return Services.prefs.getBoolPref("pdfjs.disabled");
  }

  // PDF is always shown in the list, but the 'show internally' option is
  // hidden when the internal PDF viewer is disabled.
  get enabled() {
    return true;
  }
}

class ViewableInternallyHandlerInfoWrapper extends InternalHandlerInfoWrapper {
  get enabled() {
    return lazy.DownloadIntegration.shouldViewDownloadInternally(this.type);
  }
}

// Utilities

function getFileDisplayName(file) {
  if (AppConstants.platform == "win") {
    if (file instanceof Ci.nsILocalFileWin) {
      try {
        return file.getVersionInfoField("FileDescription");
      } catch (e) {}
    }
  }
  if (AppConstants.platform == "macosx") {
    if (file instanceof Ci.nsILocalFileMac) {
      try {
        return file.bundleDisplayName;
      } catch (e) {}
    }
  }
  return file.leafName;
}

function getIconURLForAppId(aAppId) {
  return "moz-icon://" + aAppId + "?size=16";
}

function getIconURLForFile(aFile) {
  var fph = Services.io
    .getProtocolHandler("file")
    .QueryInterface(Ci.nsIFileProtocolHandler);
  var urlSpec = fph.getURLSpecFromActualFile(aFile);

  return "moz-icon://" + urlSpec + "?size=16";
}

function getIconURLForHandlerApp(aHandlerApp) {
  if (aHandlerApp instanceof Ci.nsILocalHandlerApp) {
    return getIconURLForFile(aHandlerApp.executable);
  }

  if (aHandlerApp instanceof Ci.nsIWebHandlerApp) {
    return getIconURLForWebApp(aHandlerApp.uriTemplate);
  }

  if (aHandlerApp instanceof Ci.nsIGIOHandlerApp) {
    return getIconURLForAppId(aHandlerApp.id);
  }

  // We know nothing about other kinds of handler apps.
  return "";
}

function getIconURLForWebApp(aWebAppURITemplate) {
  var uri = Services.io.newURI(aWebAppURITemplate);

  // Unfortunately we can't use the favicon service to get the favicon,
  // because the service looks in the annotations table for a record with
  // the exact URL we give it, and users won't have such records for URLs
  // they don't visit, and users won't visit the web app's URL template,
  // they'll only visit URLs derived from that template (i.e. with %s
  // in the template replaced by the URL of the content being handled).

  if (
    /^https?$/.test(uri.scheme) &&
    Services.prefs.getBoolPref("browser.chrome.site_icons")
  ) {
    // As the favicon originates from web content and is displayed in the parent process,
    // use the moz-remote-image: protocol to safely re-encode it.
    return getMozRemoteImageURL(uri.prePath + "/favicon.ico", { size: 16 });
  }

  return "";
}

/**
 * Localizes the label of the provided item.
 *
 * @param {MozBoxItem} item
 * @param {any} l10n - Either raw string to be used as text value of the element or the l10n-id, or l10n-id + l10n-args
 *
 * @returns {Promise<void>}
 */
async function setLocalizedLabel(item, l10n) {
  let label;
  if (l10n.hasOwnProperty("raw")) {
    label = l10n.raw;
  } else {
    [label] = await document.l10n.formatValues([l10n]);
  }
  item.removeAttribute("data-l10n-id");
  item.setAttribute("label", label);
}

var gNodeToObjectMap = new WeakMap();

/**
 * This is associated to <moz-box-item> elements in the handlers view.
 */
class ApplicationListItem {
  /**
   * @param {Node} node
   * @returns {Node | undefined}
   */
  static forNode(node) {
    return gNodeToObjectMap.get(node);
  }

  /**
   * @param {HandlerInfoWrapper} handlerInfoWrapper
   */
  constructor(handlerInfoWrapper) {
    this.handlerInfoWrapper = handlerInfoWrapper;
  }

  /**
   * Temporarily keeps track of the index of an action
   * menu option item so that its value will always be unique.
   *
   * @type {number}
   */
  actionsMenuOptionCount = 0;

  /**
   *
   * @param {Array<[HTMLElement | null, string, string]>} iterable
   */
  setOrRemoveAttributes(iterable) {
    for (let [element, attributeName, value] of iterable) {
      let node = element || this.node;
      if (value) {
        node.setAttribute(attributeName, value);
      } else {
        node.removeAttribute(attributeName);
      }
    }
  }

  async createNode() {
    this.node = document.createElement("moz-box-item");

    const iconSrc = this.handlerInfoWrapper._getIcon(16, 1);
    if (iconSrc) {
      this.node.setAttribute("iconsrc", iconSrc);
    }

    this.setOrRemoveAttributes([[null, "type", this.handlerInfoWrapper.type]]);

    let typeDescription = this.handlerInfoWrapper.typeDescription;
    await setLocalizedLabel(this.node, typeDescription);

    this.actionsMenu = document.createElement("moz-select");
    this.actionsMenu.slot = "actions";
    this.actionsMenu.classList.add("actionsMenu");
    this.actionsMenu.disabled = Services.prefs.prefIsLocked(
      "pref.downloads.disable_button.edit_actions"
    );

    this.node.appendChild(this.actionsMenu);

    this.buildActionsMenu();

    gNodeToObjectMap.set(this.node, this);
    return this.node;
  }

  /**
   * Creates an item for the actions dropdown.
   *
   * @private
   * @param {object} options
   * @param {string | void} [options.iconSrc]
   * @param {string} [options.l10nId]
   * @param {string} [options.value]
   * @param {number} [options.handlerActionId] - The action number associated with the handler
   * @param {any | void} [options.l10nIdArgs]
   * @returns {ApplicationFileHandlerItemActionsMenuOption}
   */
  _buildActionsMenuOption({
    iconSrc,
    l10nId,
    value,
    handlerActionId: handlerActionNumber,
    l10nIdArgs = {},
  }) {
    const option = /** @type {ApplicationFileHandlerItemActionsMenuOption} */ (
      document.createElement("moz-option")
    );
    value = value ? value : this.actionsMenuOptionCount++ + "";
    option.setAttribute("value", value);
    document.l10n.setAttributes(option, l10nId, l10nIdArgs);
    if (iconSrc) {
      option.setAttribute("iconsrc", iconSrc);
    }
    const action =
      handlerActionNumber || handlerActionNumber === 0
        ? handlerActionNumber + ""
        : "";
    if (action) {
      option.setAttribute("action", action);
    }
    return option;
  }

  /**
   * Gets the "Save file" icon that is determined by the user's OS.
   *
   * @returns {string} The icon given the current OS
   */
  _getSaveFileIcon() {
    if (AppConstants.platform == "linux") {
      return "moz-icon://stock/document-save?size=16";
    }
    return "chrome://browser/skin/preferences/saveFile.png";
  }

  /**
   * @param {HandlerInfoWrapper} handlerInfo
   * @returns {boolean}
   */
  _isInternalMenuItem(handlerInfo) {
    return (
      handlerInfo instanceof InternalHandlerInfoWrapper &&
      !handlerInfo.preventInternalViewing
    );
  }

  /**
   * Builds the default actions menu item based
   * on the OS default application, if any.
   *
   * @param {HandlerInfoWrapper} handlerInfo
   * @returns {MozOption | void}
   */
  _buildActionsMenuDefaultItem(handlerInfo) {
    if (!handlerInfo.hasDefaultHandler) {
      return undefined;
    }
    const defaultMenuItem = this._buildActionsMenuOption({
      iconSrc: ICON_URL_APP,
      handlerActionId: Ci.nsIHandlerInfo.useSystemDefault,
    });
    // If an internal option is available, don't show the application
    // name for the OS default to prevent two options from appearing
    // that may both say "Firefox".
    if (this._isInternalMenuItem(handlerInfo)) {
      document.l10n.setAttributes(
        defaultMenuItem,
        "applications-use-os-default"
      );
    } else {
      document.l10n.setAttributes(
        defaultMenuItem,
        "applications-use-app-default",
        {
          "app-name": handlerInfo.defaultDescription,
        }
      );
    }
    let image = handlerInfo.iconURLForSystemDefault;
    if (image) {
      defaultMenuItem.setAttribute("iconsrc", image);
    }
    return defaultMenuItem;
  }

  /**
   * Builds the actions menu for the item.
   */
  buildActionsMenu() {
    const { handlerInfoWrapper: handlerInfo } = this;

    // Clear out existing items.
    while (this.actionsMenu.hasChildNodes()) {
      this.actionsMenu.removeChild(this.actionsMenu.lastChild);
    }
    this.actionsMenuOptionCount = 0;

    /**
     * @type {ApplicationFileHandlerItemActionsMenuOption | undefined}
     */
    let internalMenuItem;
    // Add the "Open in Firefox" option for optional internal handlers.
    if (this._isInternalMenuItem(handlerInfo)) {
      internalMenuItem = this._buildActionsMenuOption({
        l10nId: "applications-open-inapp",
        iconSrc: "chrome://branding/content/icon32.png",
        handlerActionId: Ci.nsIHandlerInfo.handleInternally,
      });

      this.actionsMenu.appendChild(internalMenuItem);
    }

    const askMenuItem = this._buildActionsMenuOption({
      iconSrc: "chrome://browser/skin/preferences/alwaysAsk.png",
      l10nId: "applications-always-ask",
      handlerActionId: Ci.nsIHandlerInfo.alwaysAsk,
    });
    this.actionsMenu.appendChild(askMenuItem);

    // Create a menu item for saving to disk.
    // Note: this option isn't available to protocol types, since we don't know
    // what it means to save a URL having a certain scheme to disk.
    /**
     * @type {MozOption | void}
     */
    let saveMenuItem;
    if (handlerInfo.wrappedHandlerInfo instanceof Ci.nsIMIMEInfo) {
      saveMenuItem = this._buildActionsMenuOption({
        l10nId: "applications-action-save",
        iconSrc: this._getSaveFileIcon(),
        handlerActionId: Ci.nsIHandlerInfo.saveToDisk,
      });
      saveMenuItem.className = "menuitem-iconic";
      this.actionsMenu.appendChild(saveMenuItem);
    }

    // Add a separator to distinguish these items from the helper app items
    // that follow them.
    this.actionsMenu.appendChild(document.createElement("hr"));

    let defaultMenuItem = this._buildActionsMenuDefaultItem(handlerInfo);
    if (defaultMenuItem) {
      this.actionsMenu.appendChild(defaultMenuItem);
    }

    // Create menu items for possible handlers.
    let preferredApp = handlerInfo.preferredApplicationHandler;
    var possibleAppMenuItems = [];
    for (let possibleApp of handlerInfo.possibleApplicationHandlers.enumerate()) {
      if (!ApplicationsHandler.isValidHandlerApp(possibleApp)) {
        continue;
      }

      let label;
      if (possibleApp instanceof Ci.nsILocalHandlerApp) {
        label = getFileDisplayName(possibleApp.executable);
      } else {
        label = possibleApp.name;
      }
      let menuItem = this._buildActionsMenuOption({
        l10nId: "applications-use-app",
        iconSrc: getIconURLForHandlerApp(possibleApp),
        handlerActionId: Ci.nsIHandlerInfo.useHelperApp,
        l10nIdArgs: {
          "app-name": label,
        },
      });

      // Attach the handler app object to the menu item so we can use it
      // to make changes to the datastore when the user selects the item.
      menuItem.handlerApp = possibleApp;

      this.actionsMenu.appendChild(menuItem);
      possibleAppMenuItems.push(menuItem);
    }
    // Add gio handlers
    if (gGIOService) {
      var gioApps = gGIOService.getAppsForURIScheme(handlerInfo.type);
      let possibleHandlers = handlerInfo.possibleApplicationHandlers;
      for (let handler of gioApps.enumerate(Ci.nsIHandlerApp)) {
        // OS handler share the same name, it's most likely the same app, skipping...
        if (handler.name == handlerInfo.defaultDescription) {
          continue;
        }
        // Check if the handler is already in possibleHandlers
        let appAlreadyInHandlers = false;
        for (let i = possibleHandlers.length - 1; i >= 0; --i) {
          let app = possibleHandlers.queryElementAt(i, Ci.nsIHandlerApp);
          // nsGIOMimeApp::Equals is able to compare with nsILocalHandlerApp
          if (handler.equals(app)) {
            appAlreadyInHandlers = true;
            break;
          }
        }
        if (!appAlreadyInHandlers) {
          const menuItem = this._buildActionsMenuOption({
            value: Ci.nsIHandlerInfo.useHelperApp + "",
            l10nId: "applications-use-app",
            iconSrc: getIconURLForHandlerApp(handler),
            handlerActionId: Ci.nsIHandlerInfo.useHelperApp,
            l10nIdArgs: {
              "app-name": handler.name,
            },
          });
          // Attach the handler app object to the menu item so we can use it
          // to make changes to the datastore when the user selects the item.
          menuItem.handlerApp = handler;

          this.actionsMenu.appendChild(menuItem);
          possibleAppMenuItems.push(menuItem);
        }
      }
    }

    // Create a menu item for selecting a local application.
    let canOpenWithOtherApp = true;
    if (AppConstants.platform == "win") {
      // On Windows, selecting an application to open another application
      // would be meaningless so we special case executables.
      let executableType = Cc["@mozilla.org/mime;1"]
        .getService(Ci.nsIMIMEService)
        .getTypeFromExtension("exe");
      canOpenWithOtherApp = handlerInfo.type != executableType;
    }
    if (canOpenWithOtherApp) {
      let menuItem = this._buildActionsMenuOption({
        value: "choose-app",
        l10nId: "applications-use-other",
      });
      menuItem.className = "choose-app-item";
      this.actionsMenu.appendChild(menuItem);
    }

    // Create a menu item for managing applications.
    if (possibleAppMenuItems.length) {
      this.actionsMenu.appendChild(document.createElement("hr"));

      const menuItem = this._buildActionsMenuOption({
        value: "manage-app",
        l10nId: "applications-manage-app",
      });
      menuItem.className = "manage-app-item";
      this.actionsMenu.appendChild(menuItem);
    }

    // Select the item corresponding to the preferred action.  If the always
    // ask flag is set, it overrides the preferred action.  Otherwise we pick
    // the item identified by the preferred action (when the preferred action
    // is to use a helper app, we have to pick the specific helper app item).
    if (handlerInfo.alwaysAskBeforeHandling) {
      this.actionsMenu.value = askMenuItem.value;
    } else {
      // The nsHandlerInfoAction enumeration values in nsIHandlerInfo identify
      // the actions the application can take with content of various types.
      // But since we've stopped support for plugins, there's no value
      // identifying the "use plugin" action, so we use this constant instead.
      const kActionUsePlugin = 5;

      switch (handlerInfo.preferredAction) {
        case Ci.nsIHandlerInfo.handleInternally:
          if (internalMenuItem) {
            this.actionsMenu.value = internalMenuItem.value;
          } else {
            console.error("No menu item defined to set!");
          }
          break;
        case Ci.nsIHandlerInfo.useSystemDefault:
          // We might not have a default item if we're not aware of an
          // OS-default handler for this type:
          this.actionsMenu.value = defaultMenuItem
            ? defaultMenuItem.value
            : askMenuItem.value;
          break;
        case Ci.nsIHandlerInfo.useHelperApp:
          if (preferredApp) {
            let preferredItem = possibleAppMenuItems.find(v =>
              v.handlerApp.equals(preferredApp)
            );
            if (preferredItem) {
              this.actionsMenu.value = preferredItem.value;
            } else {
              // This shouldn't happen, but let's make sure we end up with a
              // selected item:
              let possible = possibleAppMenuItems
                .map(v => v.handlerApp && v.handlerApp.name)
                .join(", ");
              console.error(
                new Error(
                  `Preferred handler for ${handlerInfo.type} not in list of possible handlers!? (List: ${possible})`
                )
              );
              this.actionsMenu.value = askMenuItem.value;
            }
          }
          break;
        case kActionUsePlugin:
          // We no longer support plugins, select "ask" instead:
          this.actionsMenu.value = askMenuItem.value;
          break;
        case Ci.nsIHandlerInfo.saveToDisk:
          if (saveMenuItem) {
            this.actionsMenu.value = saveMenuItem.value;
          }
          break;
      }
    }
  }
}

/**
 * Handler class for the new "Applications" section of settings.
 */
const ApplicationsHandler = (function () {
  return new (class Handler {
    /**
     * The set of types the app knows how to handle.  A hash of HandlerInfoWrapper
     * objects, indexed by type.
     *
     * @type {Record<string, any>}
     */
    _handledTypes = {};

    /**
     * The list of types we can show, sorted by the sort column/direction.
     * An array of HandlerInfoWrapper objects.  We build this list when we first
     * load the data and then rebuild it when users change a pref that affects
     * what types we can show or change the sort column/direction.
     * Note: this isn't necessarily the list of types we *will* show; if the user
     * provides a filter string, we'll only show the subset of types in this list
     * that match that string.
     *
     * @type {Array<any>}
     */
    _visibleTypes = [];

    /**
     * @type {ApplicationListItem | null}
     */
    selectedHandlerListItem = null;

    /**
     * Currently-showing handler items.
     *
     * @type {Array<HandlerListItem>}
     */
    items = [];

    /**
     * Whether the view has already been initialized and built.
     *
     * @type {boolean}
     */
    initialized = false;

    get _list() {
      return /** @type {MozBoxGroup} */ (
        document.getElementById("applicationsHandlersView")
      );
    }

    get _filter() {
      return /** @type {MozInputSearch} */ (
        document.getElementById("applicationsFilter")
      );
    }

    async preInitApplications() {
      if (this.initialized) {
        return;
      }
      this.initialized = true;

      /**
       * applicationsHandlersView won't be available in many
       * test implementations, so skip initializing for those.
       */
      if (!this._list) {
        return;
      }

      this._loadInternalHandlers();
      this._loadApplicationHandlers();
      await this._list.updateComplete;

      this.headerElement = this._buildHeader();
      this._list.appendChild(this.headerElement);
      await this._rebuildVisibleTypes();
      await this._buildView();
    }

    /**
     * Load higher level internal handlers so they can be turned on/off in the
     * applications menu.
     */
    _loadInternalHandlers() {
      let internalHandlers = [new PDFHandlerInfoWrapper()];

      let enabledHandlers = Services.prefs
        .getCharPref("browser.download.viewableInternally.enabledTypes", "")
        .trim();
      if (enabledHandlers) {
        for (let ext of enabledHandlers.split(",")) {
          internalHandlers.push(
            new ViewableInternallyHandlerInfoWrapper(null, ext.trim())
          );
        }
      }

      for (let internalHandler of internalHandlers) {
        if (internalHandler.enabled) {
          this._handledTypes[internalHandler.type] = internalHandler;
        }
      }
    }

    /**
     * Load the set of handlers defined by the application datastore.
     */
    _loadApplicationHandlers() {
      for (let wrappedHandlerInfo of gHandlerService.enumerate()) {
        let type = wrappedHandlerInfo.type;
        if (type in this._handledTypes) {
          continue;
        }
        if (lazy.DownloadIntegration.shouldViewDownloadInternally(type)) {
          this._handledTypes[type] = new ViewableInternallyHandlerInfoWrapper(
            type
          );
        } else {
          this._handledTypes[type] = new HandlerInfoWrapper(
            type,
            wrappedHandlerInfo
          );
        }
      }
    }

    async _rebuildVisibleTypes() {
      this._visibleTypes = [];

      // Map whose keys are string descriptions and values are references to the
      // first visible HandlerInfoWrapper that has this description. We use this
      // to determine whether or not to annotate descriptions with their types to
      // distinguish duplicate descriptions from each other.
      let visibleDescriptions = new Map();
      for (let type in this._handledTypes) {
        // Yield before processing each handler info object to avoid monopolizing
        // the main thread, as the objects are retrieved lazily, and retrieval
        // can be expensive on Windows.
        await new Promise(resolve => Services.tm.dispatchToMainThread(resolve));

        let handlerInfo = this._handledTypes[type];

        // We couldn't find any reason to exclude the type, so include it.
        this._visibleTypes.push(handlerInfo);

        let key = JSON.stringify(handlerInfo.description);
        let otherHandlerInfo = visibleDescriptions.get(key);
        if (!otherHandlerInfo) {
          // This is the first type with this description that we encountered
          // while rebuilding the _visibleTypes array this time. Make sure the
          // flag is reset so we won't add the type to the description.
          handlerInfo.disambiguateDescription = false;
          visibleDescriptions.set(key, handlerInfo);
        } else {
          // There is at least another type with this description. Make sure we
          // add the type to the description on both HandlerInfoWrapper objects.
          handlerInfo.disambiguateDescription = true;
          otherHandlerInfo.disambiguateDescription = true;
        }
      }
    }

    /**
     * Creates the header item.
     *
     * @return {MozBoxItem}
     */
    _buildHeader() {
      const headerElement = document.createElement("moz-box-item");
      headerElement.slot = "header";
      this.typeColumn = document.createElement("label");
      this.typeColumn.setAttribute("data-l10n-id", "applications-type-heading");
      headerElement.appendChild(this.typeColumn);

      this.actionColumn = document.createElement("label");
      this.actionColumn.slot = "actions";
      this.actionColumn.setAttribute(
        "data-l10n-id",
        "applications-action-heading"
      );
      headerElement.appendChild(this.actionColumn);

      return headerElement;
    }

    /**
     * Sorts the items alphabetically by their label.
     *
     * @param {Array<ApplicationFileHandlerItemActionsMenuOption>} unorderedItems
     * @returns {Array<ApplicationFileHandlerItemActionsMenuOption>}
     */
    _sortItems(unorderedItems) {
      let comp = new Services.intl.Collator(undefined, {
        usage: "sort",
      });
      const textForNode = item => item.getAttribute("label");
      let multiplier = 1;
      return unorderedItems.sort(
        (a, b) => multiplier * comp.compare(textForNode(a), textForNode(b))
      );
    }

    async _rebuildView() {
      this.items = [];
      this._list.textContent = "";

      await this._rebuildVisibleTypes();
      await this._buildView();
    }

    async _buildView() {
      // Hide entire list of items.
      for (let item of this.items) {
        item.node.hidden = true;
      }
      let itemsFragment = document.createDocumentFragment();

      /**
       * @type {Array<ApplicationFileHandlerItemActionsMenuOption>}
       */
      const unorderedItems = [];

      /**
       * @type {Array<Promise<void>>}
       */
      let promises = [];

      var visibleTypes = this._visibleTypes;
      for (const visibleType of visibleTypes) {
        const handlerItem = new ApplicationListItem(visibleType);

        promises.push(
          handlerItem
            .createNode()
            .then(node => {
              unorderedItems.push(node);

              this.items.push(handlerItem);

              let originalValue = handlerItem.actionsMenu.value;

              handlerItem.actionsMenu.addEventListener("change", async () => {
                const newValue = handlerItem.actionsMenu.value;

                /**
                 * Must explicitly wait for MozSelect to update the value
                 * here, because sometimes it hasn't updated yet.
                 */
                await handlerItem.actionsMenu.updateComplete;

                if (newValue !== "choose-app" && newValue !== "manage-app") {
                  this._onSelectActionsMenuOption(handlerItem);
                } else {
                  /**
                   * Temporarily revert the value back to its original
                   * until dialogs interaction ends.
                   */
                  handlerItem.actionsMenu.value = originalValue;

                  if (newValue === "choose-app") {
                    this.chooseApp(handlerItem);
                  } else {
                    this.manageApp(handlerItem);
                  }
                }
              });
            })
            .catch(console.error)
        );
      }

      await Promise.allSettled(promises);
      /**
       * Append items sorted.
       */
      const sortedItems = this._sortItems(unorderedItems);
      for (const element of sortedItems) {
        itemsFragment.appendChild(element);
      }

      // If the user is filtering the list, then only show matching types.
      // If we filter, we need to first localize the fragment, to
      // be able to filter by localized values.
      if (this._filter.value) {
        await document.l10n.translateFragment(itemsFragment);
        this.filter();

        document.l10n.pauseObserving();
        document.l10n.resumeObserving();
      }
      // Otherwise we can just append the fragment and it'll
      // get localized via the Mutation Observer.

      this._list.appendChild(itemsFragment);

      this._filter.addEventListener("MozInputSearch:search", () =>
        this.filter()
      );
    }

    /**
     * Filter the list based on the term in the filter input.
     */
    filter() {
      const filterValue = this._filter.value.toLowerCase();
      for (let item of this.items) {
        item.node.hidden =
          !item.node.label.toLowerCase().includes(filterValue) &&
          !item.actionsMenu.selectedOption.label
            .toLowerCase()
            .includes(filterValue);
      }
    }

    // Changes

    // Whether or not we are currently storing the action selected by the user.
    // We use this to suppress notification-triggered updates to the list when
    // we make changes that may spawn such updates.
    // XXXgijs: this was definitely necessary when we changed feed preferences
    // from within _storeAction and its calltree. Now, it may still be
    // necessary, to avoid calling _rebuildView. bug 1499350 has more details.
    _storingAction = false;

    /**
     * When an option in the actions menu dropdown is selected.
     *
     * @param {ApplicationListItem} handlerItem
     */
    _onSelectActionsMenuOption(handlerItem) {
      this._storeAction(handlerItem);
    }

    /**
     * @param {ApplicationListItem} handlerItem
     */
    _storeAction(handlerItem) {
      this._storingAction = true;

      try {
        var handlerInfo = handlerItem.handlerInfoWrapper;
        const selectedOption = handlerItem.actionsMenu.querySelector(
          `moz-option[value="${handlerItem.actionsMenu.value}"]`
        );
        let action = parseInt(selectedOption.getAttribute("action"));

        // Set the preferred application handler.
        // We leave the existing preferred app in the list when we set
        // the preferred action to something other than useHelperApp so that
        // legacy datastores that don't have the preferred app in the list
        // of possible apps still include the preferred app in the list of apps
        // the user can choose to handle the type.
        if (action == Ci.nsIHandlerInfo.useHelperApp) {
          handlerInfo.preferredApplicationHandler = selectedOption.handlerApp;
        }

        // Set the "always ask" flag.
        if (action == Ci.nsIHandlerInfo.alwaysAsk) {
          handlerInfo.alwaysAskBeforeHandling = true;
        } else {
          handlerInfo.alwaysAskBeforeHandling = false;
        }

        // Set the preferred action.
        handlerInfo.preferredAction = action;

        handlerInfo.store();
      } finally {
        this._storingAction = false;
      }
    }

    /**
     * @param {ApplicationListItem} handlerItem
     */
    manageApp(handlerItem) {
      let handlerAppHelpers = {
        isValidHandlerApp: this.isValidHandlerApp.bind(this),
        getIconURLForHandlerApp,
      };
      gSubDialog.open(
        "chrome://browser/content/preferences/dialogs/applicationManager.xhtml",
        {
          features: "resizable=no",
          closedCallback: () => {
            // Rebuild menu items to reflect any potential modification of apps in the dialog
            handlerItem.buildActionsMenu();
          },
        },
        handlerItem.handlerInfoWrapper,
        handlerAppHelpers
      );
    }

    /**
     * @param {ApplicationListItem} handlerItem
     */
    async chooseApp(handlerItem) {
      var handlerInfo = handlerItem.handlerInfoWrapper;
      /**
       * @type {nsIHandlerApp}
       */
      var handlerApp;
      let chooseAppCallback =
        /**
         * @param {nsIHandlerApp} aHandlerApp
         */
        aHandlerApp => {
          // If the user picked a new app from the menu, select it.
          if (aHandlerApp) {
            // Rebuild menu items so that newly-selected app shows up in options so it can be evaluated below and selected as the option.
            handlerItem.buildActionsMenu();

            let actionsMenu = handlerItem.actionsMenu;
            for (const [idx, menuItem] of [
              ...actionsMenu.querySelectorAll("moz-option"),
            ].entries()) {
              if (
                menuItem.handlerApp &&
                menuItem.handlerApp.equals(aHandlerApp)
              ) {
                actionsMenu.value = idx + "";
                this._storeAction(handlerItem);
                break;
              }
            }
          }
        };

      if (AppConstants.platform == "win") {
        var params = {};

        params.mimeInfo = handlerInfo.wrappedHandlerInfo;
        params.title = await document.l10n.formatValue(
          "applications-select-helper"
        );
        if ("id" in handlerInfo.description) {
          params.description = await document.l10n.formatValue(
            handlerInfo.description.id,
            handlerInfo.description.args
          );
        } else {
          params.description = handlerInfo.typeDescription.raw;
        }
        params.filename = null;
        params.handlerApp = null;

        let onAppPickerClose = () => {
          if (this.isValidHandlerApp(params.handlerApp)) {
            handlerApp = params.handlerApp;

            // Add the app to the type's list of possible handlers.
            handlerInfo.addPossibleApplicationHandler(handlerApp);
          }

          chooseAppCallback(handlerApp);
          // rebuild menu items to either
          // 1. revert action menu back to original value if dialog was closed without selecting an app or
          // 2. to update the menu if a new app (or same app as current) was selected
          handlerItem.buildActionsMenu();
        };

        gSubDialog.open(
          "chrome://global/content/appPicker.xhtml",
          { closingCallback: onAppPickerClose },
          params
        );
      } else {
        let winTitle = await document.l10n.formatValue(
          "applications-select-helper"
        );
        let fp = Cc["@mozilla.org/filepicker;1"].createInstance(
          Ci.nsIFilePicker
        );
        let fpCallback = aResult => {
          if (
            aResult == Ci.nsIFilePicker.returnOK &&
            fp.file &&
            this._isValidHandlerExecutable(fp.file)
          ) {
            handlerApp = Cc[
              "@mozilla.org/uriloader/local-handler-app;1"
            ].createInstance(Ci.nsILocalHandlerApp);
            handlerApp.name = getFileDisplayName(fp.file);
            handlerApp.executable = fp.file;

            // Add the app to the type's list of possible handlers.
            let handler = handlerItem.handlerInfoWrapper;
            handler.addPossibleApplicationHandler(handlerApp);

            chooseAppCallback(handlerApp);
          } else {
            // closed the dialog without choosing an app... so rebuild menu items to revert back to original value
            handlerItem.buildActionsMenu();
          }
        };

        // Prompt the user to pick an app.  If they pick one, and it's a valid
        // selection, then add it to the list of possible handlers.
        fp.init(window.browsingContext, winTitle, Ci.nsIFilePicker.modeOpen);
        fp.appendFilters(Ci.nsIFilePicker.filterApps);
        fp.open(fpCallback);
      }
    }

    /**
     * Whether or not the given handler app is valid.
     *
     * @param aHandlerApp {nsIHandlerApp} the handler app in question
     * @returns {boolean} whether or not it's valid
     */
    isValidHandlerApp(aHandlerApp) {
      if (!aHandlerApp) {
        return false;
      }

      if (aHandlerApp instanceof Ci.nsILocalHandlerApp) {
        return this._isValidHandlerExecutable(aHandlerApp.executable);
      }

      if (aHandlerApp instanceof Ci.nsIWebHandlerApp) {
        return aHandlerApp.uriTemplate;
      }

      if (aHandlerApp instanceof Ci.nsIGIOMimeApp) {
        return aHandlerApp.command;
      }
      if (aHandlerApp instanceof Ci.nsIGIOHandlerApp) {
        return aHandlerApp.id;
      }

      return false;
    }

    _isValidHandlerExecutable(aExecutable) {
      let leafName;
      if (AppConstants.platform == "win") {
        leafName = `${AppConstants.MOZ_APP_NAME}.exe`;
      } else if (AppConstants.platform == "macosx") {
        leafName = AppConstants.MOZ_MACBUNDLE_NAME;
      } else {
        leafName = `${AppConstants.MOZ_APP_NAME}-bin`;
      }
      return (
        aExecutable &&
        aExecutable.exists() &&
        aExecutable.isExecutable() &&
        // XXXben - we need to compare this with the running instance executable
        //          just don't know how to do that via script...
        // XXXmano TBD: can probably add this to nsIShellService
        aExecutable.leafName != leafName
      );
    }
  })();
})();

SettingGroupManager.registerGroups({
  downloads: {
    l10nId: "download-save-files-header",
    headingLevel: 2,
    items: [
      {
        id: "downloadFolder",
        l10nId: "download-save-where-3",
        control: "moz-input-folder",
        controlAttrs: {
          id: "chooseFolder",
        },
      },
      {
        id: "alwaysAsk",
        l10nId: "download-always-ask-where2",
      },
      {
        id: "deletePrivate",
        l10nId: "download-private-browsing-delete2",
      },
    ],
  },
  applications: {
    l10nId: "applications-setting2",
    headingLevel: 2,
    items: [
      {
        id: "applicationsFilter",
        control: "moz-input-search",
        l10nId: "applications-filter",
        controlAttrs: {
          "aria-controls": "applicationsHandlersView",
          "data-l10n-attrs": "placeholder",
        },
      },
      {
        id: "applicationsHandlersView",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
      },
      {
        id: "handleNewFileTypes",
        l10nId: "applications-setting-new-file-types",
        control: "moz-radio-group",
        options: [
          {
            l10nId: "applications-save-for-new-types2",
            control: "moz-radio",
            value: false,
          },
          {
            l10nId: "applications-ask-before-handling2",
            control: "moz-radio",
            value: true,
          },
        ],
      },
    ],
  },
});
