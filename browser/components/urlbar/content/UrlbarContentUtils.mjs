/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { XPCOMUtils } =
  typeof ChromeUtils == "undefined"
    ? { XPCOMUtils: null }
    : ChromeUtils.importESModule("resource://gre/modules/XPCOMUtils.sys.mjs");

const lazy = XPCOMUtils?.declareLazy({
  UrlbarPrefs: "moz-src:///browser/components/urlbar/UrlbarPrefs.sys.mjs",
});

/**
 * @import {UrlbarActorPort} from "moz-src:///browser/components/urlbar/actors/UrlbarChild.sys.mjs"
 * @import {URIFixupPrimitives} from "chrome://browser/content/urlbar/UrlbarShared.mjs"
 */

/**
 * @typedef {object} ContainerInfo
 *   A container, in the form a menu needs to offer it.
 *
 * @property {number} userContextId
 *   The container's user context id.
 * @property {?string} name
 *   The container's name, null for one of the default containers.
 * @property {?string} l10nId
 *   The l10n id of a default container's name, null for a user-created one.
 * @property {?string} iconURL
 *   The container's icon, null if its icon isn't a known one.
 * @property {?string} colorCode
 *   The container's color as a CSS color, null if its color isn't a known one.
 */

/**
 * The port the actor publishes on a realm that routes through it, or null where
 * this realm reaches its privileged side itself. The actor's own scope has no
 * window, so its handlers always take the direct branch and never re-enter.
 *
 * @returns {?UrlbarActorPort}
 */
function port() {
  return globalThis.window?.UrlbarActorPort ?? null;
}

let platform;

/**
 * Per-realm accessors for things a content module can't reach for itself. Where
 * the Urlbar actor has published a port on the window, they go through it;
 * otherwise this realm reaches them directly. Keying on the port rather than on
 * the realm means the chrome message path takes the same route an unprivileged
 * input does. One class for all of them, so the branch isn't duplicated per
 * accessor.
 */
export class UrlbarContentUtils {
  /**
   * The platform, as `AppConstants.platform` names it. Read on first use rather
   * than on import: a content realm has no port until the actor publishes one.
   *
   * @returns {string}
   *   The platform, e.g. "macosx", "win" or "linux".
   */
  static getPlatform() {
    if (platform) {
      return platform;
    }
    if (!port()) {
      platform = ChromeUtils.importESModule(
        "resource://gre/modules/AppConstants.sys.mjs"
      ).AppConstants.platform;
    } else {
      // In child processes the Urlbar actor exposes this on the window, as part of
      // the single port it publishes there. To expose more, change the Urlbar
      // actor.
      platform = port().getPlatform();
    }
    return platform;
  }

  /**
   * Whether a window is private.
   *
   * @param {Window} win
   *   The window to check.
   * @returns {boolean}
   */
  static isWindowPrivate(win) {
    if (!port()) {
      return ChromeUtils.importESModule(
        "resource://gre/modules/PrivateBrowsingUtils.sys.mjs"
      ).PrivateBrowsingUtils.isWindowPrivate(win);
    }
    return port().isWindowPrivate;
  }

  /**
   * A URL's display spec: the IDN-safe Unicode form the URL parser can't produce.
   *
   * @param {string} url
   *   The URL to parse.
   * @returns {string|null}
   *   The display spec, or null if the URL can't be parsed.
   */
  static getDisplaySpec(url) {
    if (!port()) {
      try {
        return Services.io.newURI(url).displaySpec;
      } catch (ex) {
        return null;
      }
    }
    return port().getDisplaySpec(url);
  }

  /**
   * Unwraps an about:reader URL.
   *
   * @param {string} url
   * @returns {string|null}
   *   The display spec, or null if `url` isn't a well-formed about:reader URL.
   */
  static getReaderOriginalDisplaySpec(url) {
    if (!port()) {
      let uri = ChromeUtils.importESModule(
        "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs"
      ).ReaderMode.getOriginalUrlObjectForDisplay(url);
      return uri ? uri.displaySpec : null;
    }
    return port().getReaderOriginalDisplaySpec(url);
  }

  /**
   * Unescapes a URI's percent-encoding for display, applying the spoofing
   * protections `decodeURIComponent` doesn't.
   *
   * @param {string} uri
   *   The URI fragment to unescape.
   * @returns {string}
   */
  static unEscapeURIForUI(uri) {
    if (!port()) {
      return Services.textToSubURI.unEscapeURIForUI(uri);
    }
    return port().unEscapeURIForUI(uri);
  }

  /**
   * Whether a host ends with, or is, a suffix on the public suffix list.
   *
   * @param {string} host
   * @returns {boolean}
   */
  static hasKnownPublicSuffix(host) {
    if (!port()) {
      try {
        return Services.eTLD.hasKnownPublicSuffixFromHost(host);
      } catch (ex) {
        return false;
      }
    }
    return port().hasKnownPublicSuffix(host);
  }

  /**
   * The SUMO URL for a support topic.
   *
   * @param {string} topic
   *   The support page slug to append to the SUMO base URL.
   * @returns {string}
   */
  static getSupportUrl(topic) {
    if (!port()) {
      return Services.urlFormatter.formatURLPref("app.support.baseURL") + topic;
    }
    return port().getSupportUrl(topic);
  }

  /**
   * URI fixup primitives for a string, so a caller never holds an
   * `nsIURIFixupInfo`.
   *
   * @param {string} searchString
   *   The string to fix up.
   * @param {boolean} isPrivate
   *   Whether the fixup runs for a private context.
   * @returns {URIFixupPrimitives|null}
   *   The primitives, or null if fixup threw.
   */
  static getFixupPrimitives(searchString, isPrivate) {
    if (!port()) {
      return ChromeUtils.importESModule(
        "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs"
      ).UrlbarUtils.getFixupPrimitives(searchString, isPrivate);
    }
    return port().getFixupPrimitives(searchString, isPrivate);
  }

  /**
   * Retrieves clipboard data as text.
   *
   * @param {number} threshold
   *   The approximate byte size past which the clipboard is left unread. The
   *   count is platform-dependent; see `nsIClipboard.getDataIfSmallerThan`.
   * @param {Window} win
   *   The chrome window reading the clipboard.
   * @returns {Promise<?string>}
   *   The text, or null if it exceeds the threshold, can't be read, or the
   *   platform is Windows.
   */
  static getClipboardTextIfSmallerThan(threshold, win) {
    if (!port()) {
      return ChromeUtils.importESModule(
        "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs"
      ).UrlbarUtils.getClipboardTextIfSmallerThan(threshold, win);
    }
    return port().getClipboardTextIfSmallerThan(threshold);
  }

  /**
   * Whether a string reads right-to-left.
   *
   * @param {string} value
   *   The text to check.
   * @param {Window} win
   *   Any window.
   *   When calling from a content global, this window must have a UrlbarActorPort.
   * @returns {boolean}
   */
  static isTextDirectionRTL(value, win) {
    if (!port()) {
      return (
        win.windowUtils.getDirectionFromText(value) ==
        win.windowUtils.DIRECTION_RTL
      );
    }
    return port().isTextDirectionRTL(value, win);
  }

  /**
   * Where an event says a link should be opened.
   *
   * @param {Event} event
   *   The event that triggered the opening.
   * @returns {"current" | "tabshifted" | "tab" | "save" | "window"}
   */
  static whereToOpenLink(event) {
    if (!port()) {
      return ChromeUtils.importESModule(
        "resource://gre/modules/BrowserUtils.sys.mjs"
      ).BrowserUtils.whereToOpenLink(event, false, false);
    }
    return port().whereToOpenLink(event);
  }

  /**
   * Whether a pick opened with the given `where` will load in the background.
   *
   * @param {string} where
   *   Where the pick will open, as returned by `whereToOpenLink`.
   * @param {object} params
   *   The params that will be passed to `openLinkIn`.
   * @returns {boolean}
   */
  static willLoadInBackground(where, params) {
    if (!port()) {
      return ChromeUtils.importESModule(
        "resource://gre/modules/BrowserUtils.sys.mjs"
      ).BrowserUtils.willLoadInBackground(where, params);
    }
    return port().willLoadInBackground(where, params);
  }

  /**
   * The public containers, in display order. Async because
   * `ContextualIdentityService` reads the profile, which only the parent process
   * can do, so a content realm takes them over the actor.
   *
   * @returns {Promise<ContainerInfo[]>}
   */
  static getContainers() {
    if (port()) {
      return port().sendQuery("GetContainers", {});
    }
    let { ContextualIdentityService } = ChromeUtils.importESModule(
      "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
    );
    return Promise.resolve(
      ContextualIdentityService.getPublicIdentities().map(identity => ({
        userContextId: identity.userContextId,
        name: ContextualIdentityService.getUserContextLabel(
          identity.userContextId
        ),
        iconURL: ContextualIdentityService.getContainerIconURL(identity.icon),
        colorCode: ContextualIdentityService.getContainerColorCode(
          identity.color
        ),
      }))
    );
  }

  /**
   * Whether the message path or direct path should be used.
   *
   * @returns {boolean}
   */
  static usesMessagePath() {
    return (
      !lazy ||
      Services.appinfo.processType != Services.appinfo.PROCESS_TYPE_DEFAULT ||
      lazy.UrlbarPrefs.get("ipc.chromeMessagePassing")
    );
  }
}
