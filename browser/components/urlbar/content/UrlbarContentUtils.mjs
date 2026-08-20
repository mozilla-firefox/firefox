/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Per-realm accessors for things a content module can't reach for itself: the
 * privileged branch reads them directly, the content branch goes through the
 * port the Urlbar actor publishes on the window. One module for all of them, so
 * the branch isn't duplicated per accessor.
 */

/**
 * @import {URIFixupPrimitives} from "chrome://browser/content/urlbar/UrlbarShared.mjs"
 */

let platform;

/**
 * The platform, as `AppConstants.platform` names it. Read on first use rather
 * than on import: a content realm has no port until the actor publishes one.
 *
 * @returns {string}
 *   The platform, e.g. "macosx", "win" or "linux".
 */
export function getPlatform() {
  if (platform) {
    return platform;
  }
  if (typeof ChromeUtils != "undefined") {
    platform = ChromeUtils.importESModule(
      "resource://gre/modules/AppConstants.sys.mjs"
    ).AppConstants.platform;
  } else {
    // In child processes the Urlbar actor exposes this on the window, as part of
    // the single port it publishes there. To expose more, change the Urlbar
    // actor.
    platform = window.UrlbarActorPort.getPlatform();
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
export function isWindowPrivate(win) {
  if (typeof ChromeUtils != "undefined") {
    return ChromeUtils.importESModule(
      "resource://gre/modules/PrivateBrowsingUtils.sys.mjs"
    ).PrivateBrowsingUtils.isWindowPrivate(win);
  }
  return win.UrlbarActorPort.isWindowPrivate;
}

/**
 * A URL's display spec: the IDN-safe Unicode form the URL parser can't produce.
 *
 * @param {string} url
 *   The URL to parse.
 * @returns {?string}
 *   The display spec, or null if the URL can't be parsed.
 */
export function getDisplaySpec(url) {
  if (typeof ChromeUtils != "undefined") {
    try {
      return Services.io.newURI(url).displaySpec;
    } catch (ex) {
      return null;
    }
  }
  return window.UrlbarActorPort.getDisplaySpec(url);
}

/**
 * Unescapes a URI's percent-encoding for display, applying the spoofing
 * protections `decodeURIComponent` doesn't.
 *
 * @param {string} uri
 *   The URI fragment to unescape.
 * @returns {string}
 */
export function unEscapeURIForUI(uri) {
  if (typeof ChromeUtils != "undefined") {
    return Services.textToSubURI.unEscapeURIForUI(uri);
  }
  return window.UrlbarActorPort.unEscapeURIForUI(uri);
}

/**
 * The SUMO URL for a support topic.
 *
 * @param {string} topic
 *   The support page slug to append to the SUMO base URL.
 * @returns {string}
 */
export function getSupportUrl(topic) {
  if (typeof ChromeUtils != "undefined") {
    return Services.urlFormatter.formatURLPref("app.support.baseURL") + topic;
  }
  return window.UrlbarActorPort.getSupportUrl(topic);
}

/**
 * URI fixup primitives for a string, so a caller never holds an
 * `nsIURIFixupInfo`.
 *
 * @param {string} searchString
 *   The string to fix up.
 * @param {boolean} isPrivate
 *   Whether the fixup runs for a private context.
 * @returns {?URIFixupPrimitives}
 *   The primitives, or null if fixup threw.
 */
export function getFixupPrimitives(searchString, isPrivate) {
  if (typeof ChromeUtils != "undefined") {
    return ChromeUtils.importESModule(
      "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs"
    ).UrlbarUtils.getFixupPrimitives(searchString, isPrivate);
  }
  return window.UrlbarActorPort.getFixupPrimitives(searchString, isPrivate);
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
export function isTextDirectionRTL(value, win) {
  if (typeof ChromeUtils != "undefined") {
    return (
      win.windowUtils.getDirectionFromText(value) ==
      win.windowUtils.DIRECTION_RTL
    );
  }
  return win.UrlbarActorPort.isTextDirectionRTL(value, win);
}

/**
 * Where an event says a link should be opened.
 *
 * @param {KeyboardEvent | MouseEvent} event
 *   The event that triggered the opening.
 * @returns {"current" | "tabshifted" | "tab" | "save" | "window"}
 */
export function whereToOpenLink(event) {
  if (typeof ChromeUtils != "undefined") {
    return ChromeUtils.importESModule(
      "resource://gre/modules/BrowserUtils.sys.mjs"
    ).BrowserUtils.whereToOpenLink(event, false, false);
  }
  return window.UrlbarActorPort.whereToOpenLink(event);
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
export function willLoadInBackground(where, params) {
  if (typeof ChromeUtils != "undefined") {
    return ChromeUtils.importESModule(
      "resource://gre/modules/BrowserUtils.sys.mjs"
    ).BrowserUtils.willLoadInBackground(where, params);
  }
  return window.UrlbarActorPort.willLoadInBackground(where, params);
}
