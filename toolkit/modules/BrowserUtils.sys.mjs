/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  ReaderMode: "moz-src:///toolkit/components/reader/ReaderMode.sys.mjs",
  Region: "resource://gre/modules/Region.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "IDNService",
  "@mozilla.org/network/idn-service;1",
  Ci.nsIIDNService
);

ChromeUtils.defineLazyGetter(lazy, "CatManListenerManager", () => {
  const CatManListenerManager = {
    cachedModules: {},
    cachedListeners: {},
    // All 3 category manager notifications will have the category name
    // as the `data` part of the observer notification.
    observe(_subject, _topic, categoryName) {
      delete this.cachedListeners[categoryName];
    },
    /**
     * Fetch and parse category manager consumers for a given category name.
     * Will use cachedListeners for the given category name if they exist.
     */
    getListeners(categoryName) {
      if (Object.hasOwn(this.cachedListeners, categoryName)) {
        return this.cachedListeners[categoryName];
      }
      let rv = Array.from(
        Services.catMan.enumerateCategory(categoryName),
        ({ data: module, value }) => {
          try {
            let [objName, method] = value.split(".");
            let fn = (jsGlobal, ...args) => {
              let obj;
              if (module.endsWith(".js")) {
                if (!jsGlobal) {
                  throw new Error(
                    `jsGlobal must be provided to load ${objName} from ${module}.`
                  );
                }
                // For plain JS scripts, rely on a lazy getter defined on
                // jsGlobal to load the script on first access.
                obj = jsGlobal[objName];
                if (!obj) {
                  throw new Error(
                    `Could not access ${objName} from ${module}. ` +
                      `Did you forget to define a lazy getter for ${objName} on the global?`
                  );
                }
              } else {
                if (!Object.hasOwn(this.cachedModules, module)) {
                  this.cachedModules[module] =
                    ChromeUtils.importESModule(module);
                }
                obj = this.cachedModules[module][objName];
              }
              if (!obj) {
                throw new Error(
                  `Could not access ${objName} in ${module}. Is it exported?`
                );
              }
              if (typeof obj[method] != "function") {
                throw new Error(
                  `${objName}.${method} in ${module} is not a function.`
                );
              }
              return obj[method](...args);
            };
            fn._descriptiveName = value;
            return fn;
          } catch (ex) {
            console.error(
              `Error processing category manifest for ${module}: ${value}`,
              ex
            );
            return null;
          }
        }
      );
      // Remove any null entries.
      rv = rv.filter(l => !!l);
      this.cachedListeners[categoryName] = rv;
      return rv;
    },
  };
  Services.obs.addObserver(
    CatManListenerManager,
    "xpcom-category-entry-removed"
  );
  Services.obs.addObserver(CatManListenerManager, "xpcom-category-entry-added");
  Services.obs.addObserver(CatManListenerManager, "xpcom-category-cleared");
  return CatManListenerManager;
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "INVALID_SHAREABLE_SCHEMES",
  "services.sync.engine.tabs.filteredSchemes",
  "",
  null,
  val => {
    return new Set(val.split("|"));
  }
);

ChromeUtils.defineLazyGetter(lazy, "gLocalization", () => {
  return new Localization(["toolkit/global/browser-utils.ftl"], true);
});

function stringPrefToSet(prefVal) {
  return new Set(
    prefVal
      .toLowerCase()
      .split(/\s*,\s*/g) // split on commas, ignoring whitespace
      .filter(v => !!v) // discard any falsey values
  );
}

/**
 * @class BrowserUtils
 *
 * A motley collection of utilities (also known as a dumping ground).
 *
 * Please avoid expanding this if possible.
 */
export var BrowserUtils = {
  /**
   * Return or create a principal with the content of one, and the originAttributes
   * of an existing principal (e.g. on a docshell, where the originAttributes ought
   * not to change, that is, we should keep the userContextId, privateBrowsingId,
   * etc. the same when changing the principal).
   *
   * @param {nsIPrincipal} principal
   *        The principal whose content/null/system-ness we want.
   * @param {nsIPrincipal} existingPrincipal
   *        The principal whose originAttributes we want, usually the current
   *        principal of a docshell.
   * @returns {nsIPrincipal} an nsIPrincipal that matches the content/null/system-ness of the first
   *         param, and the originAttributes of the second.
   */
  principalWithMatchingOA(principal, existingPrincipal) {
    // Don't care about system principals:
    if (principal.isSystemPrincipal) {
      return principal;
    }

    // If the originAttributes already match, just return the principal as-is.
    if (existingPrincipal.originSuffix == principal.originSuffix) {
      return principal;
    }

    let secMan = Services.scriptSecurityManager;
    if (principal.isContentPrincipal) {
      return secMan.principalWithOA(
        principal,
        existingPrincipal.originAttributes
      );
    }

    if (principal.isNullPrincipal) {
      return secMan.createNullPrincipal(existingPrincipal.originAttributes);
    }
    throw new Error(
      "Can't change the originAttributes of an expanded principal!"
    );
  },

  /**
   * Copy a link with a text label to the clipboard.
   *
   * @param {string} url
   *   The URL we're wanting to copy to the clipboard.
   * @param {string} title
   *   The label/title of the URL
   */
  copyLink(url, title) {
    this.copyLinks([{ url, title }]);
  },

  /**
   * Copy multiple links to the clipboard. Writes three clipboard flavors:
   *   text/x-moz-url  — "url\ntitle" pairs separated by newlines
   *   text/html       — "<A HREF="url">title</A>" anchors separated by <BR>
   *   text/plain      — plain URLs separated by newlines
   *
   * @param {Array<{url: string, title: string}>} links
   */
  copyLinks(links) {
    let htmlEscape = s =>
      s
        .replace(/&/g, "&amp;")
        .replace(/>/g, "&gt;")
        .replace(/</g, "&lt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&apos;");

    let mozURLData = links
      .map(({ url, title }) => `${url}\n${title}`)
      .join("\n");
    let htmlData = links
      .map(({ url, title }) => `<A HREF="${url}">${htmlEscape(title)}</A>`)
      .join("<BR>\n");
    let textData = links.map(({ url }) => url).join("\n");

    let xferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
      Ci.nsITransferable
    );
    xferable.init(null);

    for (let [type, data] of [
      // This order is _important_! It controls how this and other applications
      // select data to be inserted based on type.
      ["text/x-moz-url", mozURLData],
      ["text/html", htmlData],
      ["text/plain", textData],
    ]) {
      let str = Cc["@mozilla.org/supports-string;1"].createInstance(
        Ci.nsISupportsString
      );
      str.data = data;
      xferable.addDataFlavor(type);
      xferable.setTransferData(type, str);
    }

    Services.clipboard.setData(
      xferable,
      null,
      Ci.nsIClipboard.kGlobalClipboard
    );
  },

  /**
   * Copy image data from an ArrayBuffer to the system clipboard.
   *
   * @param {ArrayBuffer} arrayBuffer
   *   The image data encoded as PNG.
   */
  copyImageToClipboard(arrayBuffer) {
    const imageTools = Cc["@mozilla.org/image/tools;1"].getService(
      Ci.imgITools
    );

    const imgDecoded = imageTools.decodeImageFromArrayBuffer(
      arrayBuffer,
      "image/png"
    );

    const transferable = Cc[
      "@mozilla.org/widget/transferable;1"
    ].createInstance(Ci.nsITransferable);
    transferable.init(null);
    // Internal consumers expect the image data to be stored as a
    // nsIInputStream. On Linux and Windows, pasted data is directly
    // retrieved from the system's native clipboard, and made available
    // as a nsIInputStream.
    //
    // On macOS, nsClipboard::GetNativeClipboardData (nsClipboard.mm) uses
    // a cached copy of nsITransferable if available, e.g. when the copy
    // was initiated by the same browser instance. To make sure that a
    // nsIInputStream is returned instead of the cached imgIContainer,
    // the image is exported as `kNativeImageMime`. Data associated
    // with this type is converted to a platform-specific image format
    // when written to the clipboard. The type is not used when images
    // are read from the clipboard (on all platforms, not just macOS).
    // This forces nsClipboard::GetNativeClipboardData to fall back to
    // the native clipboard, and return the image as a nsITransferable.
    transferable.addDataFlavor("application/x-moz-nativeimage");
    transferable.setTransferData("application/x-moz-nativeimage", imgDecoded);

    Services.clipboard.setData(
      transferable,
      null,
      Services.clipboard.kGlobalClipboard
    );
  },

  /**
   * Returns true if |mimeType| is text-based, or false otherwise.
   *
   * @param {string} mimeType
   *        The MIME type to check.
   */
  mimeTypeIsTextBased(mimeType) {
    return (
      mimeType.startsWith("text/") ||
      mimeType.endsWith("+xml") ||
      mimeType.endsWith("+json") ||
      mimeType == "application/x-javascript" ||
      mimeType == "application/javascript" ||
      mimeType == "application/json" ||
      mimeType == "application/xml"
    );
  },

  /**
   * Returns true if we can show a find bar, including FAYT, for the specified
   * document location. The location must not be in a blocklist of specific
   * "about:" pages for which find is disabled.
   *
   * This can be called from the parent process or from content processes.
   */
  canFindInPage(location) {
    return (
      !location.startsWith("about:preferences") &&
      !location.startsWith("about:settings") &&
      !location.startsWith("about:logins") &&
      !location.startsWith("about:firefoxview")
    );
  },

  isFindbarVisible(docShell) {
    const FINDER_SYS_MJS = "resource://gre/modules/Finder.sys.mjs";
    return (
      Cu.isESModuleLoaded(FINDER_SYS_MJS) &&
      ChromeUtils.importESModule(FINDER_SYS_MJS).Finder.isFindbarVisible(
        docShell
      )
    );
  },

  /**
   * Returns a Promise which resolves when the given observer topic has been
   * observed.
   *
   * @param {string} topic
   *        The topic to observe.
   * @param {(subject: nsISupports, data: string) => boolean} [test]
   *        An optional test function which, when called with the
   *        observer's subject and data, should return true if this is the
   *        expected notification, false otherwise.
   * @returns {Promise<object>}
   */
  promiseObserved(topic, test = () => true) {
    return new Promise(resolve => {
      let observer = (subject, _topic, data) => {
        if (test(subject, data)) {
          Services.obs.removeObserver(observer, topic);
          resolve({ subject, data });
        }
      };
      Services.obs.addObserver(observer, topic);
    });
  },

  formatURIStringForDisplay(uriString, options = {}) {
    try {
      return this.formatURIForDisplay(Services.io.newURI(uriString), options);
    } catch (ex) {
      return uriString;
    }
  },

  /**
   * Show a URI in the UI in a user-friendly (but security-sensitive) way.
   *
   * @param {nsIURI} uri
   * @param {object} [options={}]
   * @param {boolean} [options.showInsecureHTTP=false]
   *        Whether to show "http://" for insecure HTTP URLs.
   * @param {boolean} [options.showWWW=false]
   *        Whether to show "www." for URLs that have it.
   * @param {boolean} [options.onlyBaseDomain=false]
   *        Whether to show only the base domain (eTLD+1) for HTTP(S) URLs.
   * @param {boolean} [options.showFilenameForLocalURIs=false]
   *        If false (default), will show a protocol-specific label for local
   *        URIs (file:, chrome:, resource:, moz-src:, jar:).
   *        Otherwise, will show the filename for such URIs. Only use 'true' if
   *        the context in which the URI is being represented is not security-
   *        critical.
   */
  formatURIForDisplay(uri, options = {}) {
    let {
      showInsecureHTTP = false,
      showWWW = false,
      onlyBaseDomain = false,
      showFilenameForLocalURIs = false,
    } = options;
    // For moz-icon and jar etc. which wrap nsIURLs, if we want to show the
    // actual filename, unwrap:
    if (uri && uri instanceof Ci.nsINestedURI && showFilenameForLocalURIs) {
      return this.formatURIForDisplay(uri.innermostURI, options);
    }
    switch (uri.scheme) {
      case "view-source": {
        let innerURI = uri.spec.substring("view-source:".length);
        return this.formatURIStringForDisplay(innerURI, options);
      }
      case "http":
      // Fall through.
      case "https": {
        let host = uri.displayHostPort;
        let removeSubdomains =
          !showInsecureHTTP &&
          (onlyBaseDomain || (!showWWW && host.startsWith("www.")));
        if (removeSubdomains) {
          try {
            host = lazy.IDNService.domainToDisplay(
              Services.eTLD.getSchemelessSite(uri)
            );
          } catch (ex) {
            // Fall back to the full host for invalid IDN. This should never
            // happen but we'll be defensive so we don't break display.
            console.error(ex);
            host = uri.host;
          }
          if (uri.port != -1) {
            host += ":" + uri.port;
          }
        }
        if (showInsecureHTTP && uri.scheme == "http") {
          return "http://" + host;
        }
        return host;
      }
      case "about":
        return "about:" + uri.filePath;
      case "blob":
        try {
          let url = URL.fromURI(uri);
          // _If_ we find a non-null origin, report that.
          if (url.origin && url.origin != "null") {
            return this.formatURIStringForDisplay(url.origin, options);
          }
          // otherwise, fall through...
        } catch (ex) {
          console.error("Invalid blob URI passed to formatURIForDisplay: ", ex);
        }
      /* For blob URIs without an origin, fall through and use the data URI
       * logic (shows just "(data)", localized). */
      case "data":
        return lazy.gLocalization.formatValueSync("browser-utils-url-data");
      case "moz-extension": {
        let policy = WebExtensionPolicy.getByURI(uri);
        return lazy.gLocalization.formatValueSync(
          "browser-utils-url-extension",
          { extension: policy?.name.trim() || uri.spec }
        );
      }
      case "chrome":
      case "resource":
      case "moz-icon":
      case "moz-src":
      case "jar":
      case "file":
        if (!showFilenameForLocalURIs) {
          if (uri.scheme == "file") {
            return lazy.gLocalization.formatValueSync(
              "browser-utils-file-scheme"
            );
          }
          return lazy.gLocalization.formatValueSync(
            "browser-utils-url-scheme",
            { scheme: uri.scheme }
          );
        }
      // Otherwise, fall through to show filename...
      default:
        try {
          let url = uri.QueryInterface(Ci.nsIURL);
          // Just the filename if we have one:
          if (url.fileName) {
            return url.fileName;
          }
          // We won't get a filename for a path that looks like:
          // /foo/bar/baz/
          // So try the directory name:
          if (url.directory) {
            let parts = url.directory.split("/");
            // Pop off any empty bits at the end:
            let last;
            while (!last && parts.length) {
              last = parts.pop();
            }
            if (last) {
              return last;
            }
          }
        } catch (ex) {
          console.error(ex);
        }
    }
    return uri.spec;
  },

  // Given a URL returns a (possibly transformed) URL suitable for sharing, or null if
  // no such URL can be obtained.
  getShareableURL(url) {
    if (!url) {
      return null;
    }

    // Carve out an exception for about:reader.
    if (url.spec.startsWith("about:reader?")) {
      url = Services.io.newURI(lazy.ReaderMode.getOriginalUrl(url.spec));
    }
    // Disallow sharing URLs with more than 65535 characters.
    if (url.spec.length > 65535) {
      return null;
    }
    // Use the same preference as synced tabs to disable what kind
    // of tabs we can send to another device
    return lazy.INVALID_SHAREABLE_SCHEMES.has(url.scheme) ? null : url;
  },

  /**
   * Extracts linkNode and href for a click event.
   *
   * @param {UIEvent} event
   *        The click event.
   * @returns {Array<any>} [href, linkNode, linkPrincipal].
   *
   * Note that linkNode will be null if the click wasn't on an anchor
   *       element. This includes SVG links, because callers expect |node|
   *       to behave like an <a> element, which SVG links (XLink) don't.
   */
  hrefAndLinkNodeForClickEvent(event) {
    // We should get a window off the event, and bail if not:
    let content = event.view || event.composedTarget?.documentGlobal;
    if (!content?.HTMLAnchorElement) {
      return null;
    }
    // Be consistent with what ContextMenuChild.sys.mjs does.
    function hrefAndLinkNodeForHTMLLink(aElement) {
      if (
        (content.HTMLAnchorElement.isInstance(aElement) && aElement.href) ||
        (content.HTMLAreaElement.isInstance(aElement) && aElement.href) ||
        content.HTMLLinkElement.isInstance(aElement)
      ) {
        let href = URL.parse(aElement.href)?.href ?? null;
        if (href) {
          return [href, aElement, aElement.ownerDocument.nodePrincipal];
        }
      }
      return null;
    }
    function hrefAndLinkNodeForNonHTMLink(aElement) {
      if (
        aElement.localName == "a" ||
        (content.MathMLElement.isInstance(aElement) &&
          !Services.prefs.getBoolPref(
            "mathml.href_link_on_non_anchor_element.disabled"
          ))
      ) {
        let href =
          aElement.getAttribute("href") ||
          aElement.getAttributeNS("http://www.w3.org/1999/xlink", "href");
        href =
          URL.parse(href, aElement.ownerDocument.baseURIObject.spec)?.href ??
          null;
        if (href) {
          // Don't return the aElement we got href from since callers expect
          // <a>-like elements.
          return [href, null, aElement.ownerDocument.nodePrincipal];
        }
      }
      return null;
    }

    let node = event.composedTarget;
    while (node) {
      if (node.nodeType == node.ELEMENT_NODE) {
        let linkData =
          hrefAndLinkNodeForHTMLLink(node) ||
          hrefAndLinkNodeForNonHTMLink(node);
        if (linkData) {
          return linkData;
        }
      }
      node = node.flattenedTreeParentNode;
    }
    return [null, null, null];
  },

  /**
   * whereToOpenLink() looks at an event to decide where to open a link.
   *
   * The event may be a mouse event (click, double-click, middle-click) or keypress event (enter).
   *
   * On Windows, the modifiers are:
   * Ctrl        new tab, selected
   * Shift       new window
   * Ctrl+Shift  new tab, in background
   * Alt         save
   *
   * Middle-clicking is the same as Ctrl+clicking (it opens a new tab).
   *
   * Exceptions:
   * - Alt is ignored for menu items selected using the keyboard so you don't accidentally save stuff.
   *    (Currently, the Alt isn't sent here at all for menu items, but that will change in bug 126189.)
   * - Alt is hard to use in context menus, because pressing Alt closes the menu.
   * - Alt can't be used on the bookmarks toolbar because Alt is used for "treat this as something draggable".
   * - The button is ignored for the middle-click-paste-URL feature, since it's always a middle-click.
   *
   * @param {Event|object} e
   *   Event or JSON Object
   * @param {boolean} ignoreButton
   * @param {boolean} ignoreAlt
   * @returns {"current" | "tabshifted" | "tab" | "save" | "window"}
   */
  whereToOpenLink(e, ignoreButton, ignoreAlt) {
    // This method must treat a null event like a left click without modifier keys (i.e.
    // e = { shiftKey:false, ctrlKey:false, metaKey:false, altKey:false, button:0 })
    // for compatibility purposes.
    if (!e) {
      return "current";
    }

    e = this.getRootEvent(e);

    var shift = e.shiftKey;
    var ctrl = e.ctrlKey;
    var meta = e.metaKey;
    var alt = e.altKey && !ignoreAlt;

    // ignoreButton allows "middle-click paste" to use function without always opening in a new window.
    let middle = !ignoreButton && e.button == 1;
    let middleUsesTabs = Services.prefs.getBoolPref(
      "browser.tabs.opentabfor.middleclick",
      true
    );
    let middleUsesNewWindow = Services.prefs.getBoolPref(
      "middlemouse.openNewWindow",
      false
    );

    // Don't do anything special with right-mouse clicks.  They're probably clicks on context menu items.

    // See also nsWindowWatcher::GetWindowOpenLocation in
    // toolkit/components/windowwatcher/nsWindowWatcher.cpp

    var metaKey = AppConstants.platform == "macosx" ? meta : ctrl;
    if (metaKey || (middle && middleUsesTabs)) {
      return shift ? "tabshifted" : "tab";
    }

    if (alt && Services.prefs.getBoolPref("browser.altClickSave", false)) {
      return "save";
    }

    if (shift || (middle && !middleUsesTabs && middleUsesNewWindow)) {
      return "window";
    }

    return "current";
  },

  // Utility function to check command events for potential middle-click events
  // from checkForMiddleClick and unwrap them.
  getRootEvent(aEvent) {
    // Part of the fix for Bug 1523813.
    // Middle-click events arrive here wrapped in different numbers (1-2) of
    // command events, depending on the button originally clicked.
    if (!aEvent) {
      return aEvent;
    }
    let tempEvent = aEvent;
    while (tempEvent.sourceEvent) {
      if (tempEvent.sourceEvent.button == 1) {
        aEvent = tempEvent.sourceEvent;
        break;
      }
      tempEvent = tempEvent.sourceEvent;
    }
    return aEvent;
  },

  /**
   * Invoke all the category manager consumers of a given JS consumer.
   * Similar to the (C++-only) ``NS_CreateServicesFromCategory`` in that it'll
   * abstract away the actual work of invoking the modules/services.
   * Different in that it's JS-only and will invoke methods in modules
   * instead of using XPCOM services.
   *
   * More context is available in
   * https://firefox-source-docs.mozilla.org/browser/CategoryManagerIndirection.html
   *
   * @param {object} options
   * @param {string} options.categoryName
   *        What category's consumers to call.
   * @param {boolean} [options.idleDispatch=false]
   *        If set to true, call each consumer in an idle task.
   * @param {string} [options.profilerMarker=""]
   *        If specified, will create a profiler marker with the provided
   *        identifier for each consumer.
   * @param {Function} [options.failureHandler]
   *        If specified, will be called for any exceptions raised, in
   *        order to do custom failure handling.
   * @param {object} [options.jsGlobal=null]
   *        If specified, will be used as the global object when loading any
   *        JS modules specified in the category entries.
   * @param {...any} args
   *        Arguments to pass to the consumers.
   * @returns {Promise}
   *   A Promise that resolves when all consumers have settled.
   */
  callModulesFromCategory(
    {
      categoryName,
      profilerMarker = "",
      idleDispatch = false,
      failureHandler = null,
      jsGlobal = null,
    },
    ...args
  ) {
    // Use an async function for profiler markers and error handling.
    // Note that we deliberately don't await at the top level, so we
    // can guarantee all consumers get run/queued.
    let callSingleListener = async fn => {
      let startTime = profilerMarker ? ChromeUtils.now() : 0;
      try {
        await fn(jsGlobal, ...args);
      } catch (ex) {
        console.error(
          `Error in processing ${categoryName} for ${fn._descriptiveName}`
        );
        console.error(ex);
        try {
          await failureHandler?.(ex);
        } catch (nestedEx) {
          console.error(`Error in handling failure: ${nestedEx}`);
          // Crash in automation:
          if (BrowserUtils._inAutomation) {
            Cc["@mozilla.org/xpcom/debug;1"]
              .getService(Ci.nsIDebug2)
              .abort(nestedEx.filename, nestedEx.lineNumber);
          }
        }
      }
      if (profilerMarker) {
        ChromeUtils.addProfilerMarker(
          profilerMarker,
          startTime,
          fn._descriptiveName
        );
      }
    };

    let allTasks = [];

    for (let listener of lazy.CatManListenerManager.getListeners(
      categoryName
    )) {
      if (idleDispatch) {
        allTasks.push(
          new Promise(resolve => {
            ChromeUtils.idleDispatch(() => {
              resolve(callSingleListener(listener));
            });
          })
        );
      } else {
        allTasks.push(callSingleListener(listener));
      }
    }

    return Promise.allSettled(allTasks);
  },

  /**
   * Returns whether the build is a China repack.
   *
   * @returns {boolean} True if the distribution ID is 'MozillaOnline',
   *                   otherwise false.
   */
  isChinaRepack() {
    return (
      Services.prefs
        .getDefaultBranch("")
        .getCharPref("distribution.id", "default") === "MozillaOnline"
    );
  },

  /**
   * An enumeration of the promotion types that can be passed to shouldShowPromo
   */
  PromoType: {
    DEFAULT: 0, // invalid
    VPN: 1,
    RELAY: 2,
    FOCUS: 3,
    PIN: 4,
    COOKIE_BANNERS: 5,
  },

  /**
   * Should a given promo be shown to the user now, based on things including:
   *
   *  current region
   *  home region
   *  where ads for a particular thing are allowed
   *  where they are illegal
   *  in what regions is the thing being promoted supported?
   *  whether there is an active enterprise policy
   *  settings of specific preferences related to this promo
   *
   * @param {BrowserUtils.PromoType} promoType - What promo are we checking on?
   *
   * @returns {boolean} - should we display this promo now or not?
   */
  shouldShowPromo(promoType) {
    switch (promoType) {
      case this.PromoType.VPN:
      case this.PromoType.FOCUS:
      case this.PromoType.PIN:
      case this.PromoType.RELAY:
      case this.PromoType.COOKIE_BANNERS:
        break;
      default:
        throw new Error("Unknown promo type: ", promoType);
    }

    const info = PromoInfo[promoType];
    const promoEnabled =
      !info.enabledPref || Services.prefs.getBoolPref(info.enabledPref, true);

    const homeRegion = lazy.Region.home || "";
    const currentRegion = lazy.Region.current || "";

    let inSupportedRegion = true;
    if ("supportedRegions" in info.lazyStringSetPrefs) {
      const supportedRegions =
        info.lazyStringSetPrefs.supportedRegions.lazyValue;
      inSupportedRegion =
        supportedRegions.has(currentRegion.toLowerCase()) ||
        supportedRegions.has(homeRegion.toLowerCase());
    }

    const avoidAdsRegions =
      info.lazyStringSetPrefs.disallowedRegions?.lazyValue;

    // Don't show promo if there's an active enterprise policy
    const noActivePolicy =
      info.showForEnterprise ||
      !Services.policies ||
      Services.policies.status !== Services.policies.ACTIVE;

    // Promos may add custom checks that must pass.
    const passedExtraCheck = !info.extraCheck || info.extraCheck();

    return (
      promoEnabled &&
      !avoidAdsRegions?.has(homeRegion.toLowerCase()) &&
      !avoidAdsRegions?.has(currentRegion.toLowerCase()) &&
      !info.illegalRegions.includes(homeRegion.toLowerCase()) &&
      !info.illegalRegions.includes(currentRegion.toLowerCase()) &&
      inSupportedRegion &&
      noActivePolicy &&
      passedExtraCheck
    );
  },

  /**
   * @deprecated in favor of shouldShowPromo
   */
  shouldShowVPNPromo() {
    return this.shouldShowPromo(this.PromoType.VPN);
  },

  // Return true if Send to Device emails are supported for user's locale
  sendToDeviceEmailsSupported() {
    const userLocale = Services.locale.appLocaleAsBCP47.toLowerCase();
    return this.emailSupportedLocales.has(userLocale);
  },
};

/**
 * A table of promos used by shouldShowPromo to decide whether or not to show.
 * Each entry defines the criteria for a given promo, and also houses lazy
 * getters for specified string set preferences.
 */
let PromoInfo = {
  [BrowserUtils.PromoType.VPN]: {
    enabledPref: "browser.vpn_promo.enabled",
    lazyStringSetPrefs: {
      supportedRegions: {
        name: "browser.contentblocking.report.vpn_regions",
        default:
          "as,at,au,bd,be,bg,br,ca,ch,cl,co,cy,cz,de,dk,ee,eg,es,fi,fr,gb,gg,gr,hr,hu,id,ie,im,in,io,it,je,ke,kr,lt,lu,lv,ma,mp,mt,mx,my,ng,nl,no,nz,pl,pr,pt,ro,sa,se,sg,si,sk,sn,th,tr,tw,ua,ug,uk,um,us,vg,vi,vn,za",
      },
      disallowedRegions: {
        name: "browser.vpn_promo.disallowed_regions",
        default: "ae,by,cn,cu,iq,ir,kp,om,ru,sd,sy,tm,tr",
      },
    },
    //See https://github.com/search?q=repo%3Amozilla%2Fbedrock+VPN_EXCLUDED_COUNTRY_CODES&type=code
    illegalRegions: [
      "ae",
      "by",
      "cn",
      "cu",
      "iq",
      "ir",
      "kp",
      "om",
      "ru",
      "sd",
      "sy",
      "tm",
      "tr",
    ],
  },
  [BrowserUtils.PromoType.FOCUS]: {
    enabledPref: "browser.promo.focus.enabled",
    lazyStringSetPrefs: {
      // there are no particular limitions to where it is "supported",
      // so we leave out the supported pref
      disallowedRegions: {
        name: "browser.promo.focus.disallowed_regions",
        default: "cn",
      },
    },
    illegalRegions: ["cn"],
  },
  [BrowserUtils.PromoType.PIN]: {
    enabledPref: "browser.promo.pin.enabled",
    lazyStringSetPrefs: {},
    illegalRegions: [],
  },
  [BrowserUtils.PromoType.RELAY]: {
    lazyStringSetPrefs: {},
    illegalRegions: [],
    // Returns true if user is using the FxA "production" instance, or returns
    // false for custom FxA instance (such as accounts.firefox.com.cn for the
    // China repack) which doesn't support authentication for addons like Relay.
    extraCheck: () =>
      !Services.prefs.getCharPref("identity.fxaccounts.autoconfig.uri", "") &&
      [
        "identity.fxaccounts.remote.root",
        "identity.fxaccounts.auth.uri",
        "identity.fxaccounts.remote.oauth.uri",
        "identity.fxaccounts.remote.profile.uri",
        "identity.fxaccounts.remote.pairing.uri",
        "identity.sync.tokenserver.uri",
      ].every(pref => !Services.prefs.prefHasUserValue(pref)),
  },
  [BrowserUtils.PromoType.COOKIE_BANNERS]: {
    enabledPref: "browser.promo.cookiebanners.enabled",
    lazyStringSetPrefs: {},
    illegalRegions: [],
    showForEnterprise: true,
  },
};

/*
 * Finish setting up the PromoInfo data structure by attaching lazy prefs getters
 * as specified in the structure. (the object for each pref in the lazyStringSetPrefs
 * gets a `lazyValue` property attached to it).
 */
for (let promo of Object.values(PromoInfo)) {
  for (let prefObj of Object.values(promo.lazyStringSetPrefs)) {
    XPCOMUtils.defineLazyPreferenceGetter(
      prefObj,
      "lazyValue",
      prefObj.name,
      prefObj.default,
      null,
      stringPrefToSet
    );
  }
}

XPCOMUtils.defineLazyPreferenceGetter(
  BrowserUtils,
  "navigationRequireUserInteraction",
  "browser.navigation.requireUserInteraction",
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  BrowserUtils,
  "emailSupportedLocales",
  "browser.send_to_device_locales",
  "de,en-GB,en-US,es-AR,es-CL,es-ES,es-MX,fr,id,pl,pt-BR,ru,zh-TW",
  null,
  stringPrefToSet
);
