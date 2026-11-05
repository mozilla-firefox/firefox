/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  FaviconUtils: "moz-src:///toolkit/modules/FaviconUtils.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(lazy, {
  Favicons: ["@mozilla.org/browser/favicon-service;1", Ci.nsIFaviconService],
});

ChromeUtils.defineLazyGetter(
  lazy,
  "l10n",
  () =>
    new Localization(["browser/taskbartabs.ftl", "branding/brand.ftl"], true)
);

export const TaskbarTabsUtils = {
  /**
   * Builds the part of a window title that identifies a Taskbar Tab: its
   * name, its container and the current profile.
   *
   * @param {{name: string, userContextId: number}} aInfo - The name and
   * container of the Taskbar Tab, from TaskbarTabs.getTaskbarTabTitleInfo.
   * @param {string} [aProfile] - Name of the current profile, if it should
   * appear in the title.
   * @returns {string} The localized title.
   */
  createTaskbarTabTitle(aInfo, aProfile) {
    let containerLabel = aInfo.userContextId
      ? lazy.ContextualIdentityService.getUserContextLabel(aInfo.userContextId)
      : "";

    let stringName = "taskbar-tab-title-default";
    if (containerLabel && aProfile) {
      stringName = "taskbar-tab-title-container-profile";
    } else if (containerLabel && !aProfile) {
      stringName = "taskbar-tab-title-container";
    } else if (!containerLabel && aProfile) {
      stringName = "taskbar-tab-title-profile";
    }

    return lazy.l10n.formatValueSync(stringName, {
      name: aInfo.name,
      container: containerLabel,
      profile: aProfile,
    });
  },

  /**
   * Checks if Taskbar Tabs has been enabled.
   *
   * @returns {bool} `true` if the Taskbar Tabs pref is enabled.
   */
  isEnabled() {
    const pref = "browser.taskbarTabs.enabled";
    return Services.prefs.getBoolPref(pref, false);
  },

  isMSIX() {
    return (
      AppConstants.platform === "win" &&
      Services.sysinfo.getProperty("hasWinPackageId", false)
    );
  },

  /**
   * Returns a folder to store profile-specific Taskbar Tabs files.
   *
   * @returns {nsIFile} Folder to store Taskbar Tabs files.
   */
  getTaskbarTabsFolder() {
    // Construct the path `[Profile]/taskbartabs/`.
    let folder = Services.dirsvc.get("ProfD", Ci.nsIFile);
    folder.append("taskbartabs");
    return folder;
  },

  /**
   * Checks if the window is a Taskbar Tabs window.
   *
   * @param {Window} aWin - The window to inspect.
   * @returns {bool} true if the window is a Taskbar Tabs window.
   */
  isTaskbarTabWindow(aWin) {
    return aWin.document.documentElement.hasAttribute("taskbartab");
  },

  /**
   * Retrieves the Taskbar Tabs ID for the window.
   *
   * @param {DOMWindow} aWin - The window to retrieve the Taskbar Tabs ID.
   * @returns {string} The Taskbar Tabs ID for the window.
   */
  getTaskbarTabIdFromWindow(aWin) {
    return aWin.document.documentElement.getAttribute("taskbartab");
  },

  /**
   * Retrieves an image container for the provided URI and decodes it remotely,
   * e.g. in the content process of aBrowser.
   *
   * May throw if an error occurs while decoding.
   *
   * @param {nsIFile} aFile - The file to parse the image from.
   * @param {number} aSize - The width/height of the image to decode.
   * @param {Browser} aBrowser - The browser to decode the image in. Can be
   * null if there isn't a specific content process to use.
   * @param {string} aMimeType - The MIME type to use when decoding the image.
   * @returns {Promise<imgIContainer>} An image container with the decoded
   * image.
   * @throws {Components.Exception} The image could not be decoded.
   */
  async _remoteDecodeImageFromFile(aFile, aSize, aBrowser, aMimeType) {
    // We can't use a file URI since the content process wouldn't be able to
    // read it. Read the file now and create a data URI to read instead.
    let content = await IOUtils.read(aFile.path);
    let spec = `data:${aMimeType};base64,${content.toBase64()}`;
    let uri = Services.io.newURI(spec);

    return this._remoteDecodeImageFromURI(uri, aSize, aBrowser);
  },

  /**
   * Retrieves an image container for the provided URI and decodes it remotely,
   * e.g. in the content process of aBrowser.
   *
   * May throw if an error occurs while decoding.
   *
   * @param {nsIURI} aUri - The URI to parse the image from.
   * @param {number} aSize - The width/height of the image to decode.
   * @param {Browser?} [aBrowser] - The browser to decode the image in. Can be
   * null if there isn't a specific content process to use.
   * @returns {Promise<imgIContainer>} An image container with the decoded
   * image.
   * @throws {TypeError} aUri is not an nsIURI.
   * @throws {Components.Exception} The image could not be decoded.
   */
  async _remoteDecodeImageFromURI(aUri, aSize, aBrowser = null) {
    if (!(aUri instanceof Ci.nsIURI)) {
      throw new TypeError(
        "Invalid argument, `aUri` should be instance of `nsIURI`"
      );
    }

    let params = { size: aSize };
    if (aBrowser) {
      params.contentParentId =
        aBrowser.browsingContext.currentWindowContext.contentParentId;
    }

    let newUri = Services.io.newURI(
      lazy.FaviconUtils.getMozRemoteImageURL(aUri.spec, params)
    );
    return unsafeDecodeImageFromAnyURI(newUri);
  },

  /**
   * Retrieves an image container for the provided URI. The URI must be a local
   * URI, like chrome: or data:. (Note that being local _doesn't_ mean that it
   * is trusted---this usually should be used from tests or for images bundled
   * with the browser.)
   *
   * May throw if an error occurs while decoding.
   *
   * @param {nsIURI} aUri - The URI to parse the image from. Must be a local
   * URI, like data:, chrome:, or file:.
   * @returns {imgIContainer} A container of the icon retrieved, or the
   * default favicon.
   * @throws {TypeError} aUri is not an nsIURI.
   * @throws {Error} aUri is not a local URI.
   * @throws {Components.Exception} The image could not be decoded.
   */
  async _imageFromLocalURI(aUri) {
    if (!(aUri instanceof Ci.nsIURI)) {
      throw new TypeError(
        "Invalid argument, `aUri` should be instance of `nsIURI`"
      );
    }

    const protocolFlags = Services.io.getProtocolFlags(aUri.scheme);
    if (!(protocolFlags & Ci.nsIProtocolHandler.URI_IS_LOCAL_RESOURCE)) {
      throw new Error("Attempting to create an image from a non-local URI");
    }

    return unsafeDecodeImageFromAnyURI(aUri);
  },

  /**
   * Gets the favicon for aUri as a data URI.
   *
   * @param {nsIURI} aUri - The URI to look up the favicon for.
   * @returns {nsIURI} The data URI of the favicon.
   */
  async getFaviconUri(aUri) {
    let favicon = await lazy.Favicons.getFaviconForPage(aUri);
    return favicon?.dataURI;
  },

  /**
   * Gets the default favicon as an imgIContainer. This icon is used if no
   * other icons are available.
   *
   * @returns {imgIContainer} The default favicon.
   */
  async getDefaultIcon() {
    return await TaskbarTabsUtils._imageFromLocalURI(
      lazy.Favicons.defaultFavicon
    );
  },

  /**
   * Gets the name that should be used for a new desktop entry on Linux. This
   * avoids duplicating the logic between TaskbarTabsWindowManager, where we
   * want the name before pinning, and TaskbarTabsPin, where it is actually
   * pinned. As such, this should be constant within a single session.
   *
   * @param {string} aTaskbarTabId - The ID of the taskbar tab.
   * @returns {string} The desktop entry name, excluding the '.desktop' suffix.
   */
  _determineNewDesktopEntryName(aTaskbarTabId) {
    return `${lazy.ShellService.getGlibPrgname()}.webapp-${aTaskbarTabId}`;
  },
};

/**
 * Shared helper function for _remoteDecodeImageFromURI and _imageFromLocalURI;
 * fetches the given URI with the system principal and decodes it in the
 * current process. Do not use with untrusted images.
 *
 * @param {nsIURI} aUri - The URI to fetch and decode.
 * @returns {Promise<imgIContainer>} The parsed image container.
 * @throws {Components.Exception} The image could not be decoded.
 */
async function unsafeDecodeImageFromAnyURI(aUri) {
  const channel = Services.io.newChannelFromURI(
    aUri,
    null,
    Services.scriptSecurityManager.getSystemPrincipal(),
    null,
    Ci.nsILoadInfo.SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
    Ci.nsIContentPolicy.TYPE_IMAGE
  );

  return ChromeUtils.fetchDecodedImage(aUri, channel);
}
