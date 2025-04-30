/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

ChromeUtils.defineESModuleGetters(this, {
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "containersEnabled",
  "privacy.userContext.enabled"
);

var { DefaultMap, DefaultWeakMap, ExtensionError, parseMatchPatterns } =
  ExtensionUtils;

var { defineLazyGetter } = ExtensionCommon;

/**
 * The platform-specific type of native tab objects, which are wrapped by
 * TabBase instances.
 *
 * @typedef {object | XULElement} NativeTab
 */

/**
 * @typedef {object} MutedInfo
 * @property {boolean} muted
 *        True if the tab is currently muted, false otherwise.
 * @property {string} [reason]
 *        The reason the tab is muted. Either "user", if the tab was muted by a
 *        user, or "extension", if it was muted by an extension.
 * @property {string} [extensionId]
 *        If the tab was muted by an extension, contains the internal ID of that
 *        extension.
 */

/**
 * A platform-independent base class for extension-specific wrappers around
 * native tab objects.
 *
 * @param {Extension} extension
 *        The extension object for which this wrapper is being created. Used to
 *        determine permissions for access to certain properties and
 *        functionality.
 * @param {NativeTab} nativeTab
 *        The native tab object which is being wrapped. The type of this object
 *        varies by platform.
 * @param {integer} id
 *        The numeric ID of this tab object. This ID should be the same for
 *        every extension, and for the lifetime of the tab.
 */
class TabBase {
  constructor(extension, nativeTab, id) {
    this.extension = extension;
    this.tabManager = extension.tabManager;
    this.id = id;
    this.nativeTab = nativeTab;
    this.activeTabWindowID = null;

    if (!extension.privateBrowsingAllowed && this._incognito) {
      throw new ExtensionError(`Invalid tab ID: ${id}`);
    }
  }

  /**
   * Capture the visible area of this tab, and return the result as a data: URI.
   *
   * @param {BaseContext} context
   *        The extension context for which to perform the capture.
   * @param {number} zoom
   *        The current zoom for the page.
   * @param {object} [options]
   *        The options with which to perform the capture.
   * @param {string} [options.format = "png"]
   *        The image format in which to encode the captured data. May be one of
   *        "png" or "jpeg".
   * @param {integer} [options.quality = 92]
   *        The quality at which to encode the captured image data, ranging from
   *        0 to 100. Has no effect for the "png" format.
   * @param {DOMRectInit} [options.rect]
   *        Area of the document to render, in CSS pixels, relative to the page.
   *        If null, the currently visible viewport is rendered.
   * @param {number} [options.scale]
   *        The scale to render at, defaults to devicePixelRatio.
   * @returns {Promise<string>}
   */
  async capture(context, zoom, options) {
    let win = this.browser.ownerGlobal;
    let scale = options?.scale || win.devicePixelRatio;
    let rect = options?.rect && win.DOMRect.fromRect(options.rect);

    // We only allow mozilla addons to use the resetScrollPosition option,
    // since it's not standardized.
    let resetScrollPosition = false;
    if (!context.extension.restrictSchemes) {
      resetScrollPosition = !!options?.resetScrollPosition;
    }

    let wgp = this.browsingContext.currentWindowGlobal;
    let image = await wgp.drawSnapshot(
      rect,
      scale * zoom,
      "white",
      resetScrollPosition
    );

    let canvas = new OffscreenCanvas(image.width, image.height);

    let ctx = canvas.getContext("bitmaprenderer", { alpha: false });
    ctx.transferFromImageBitmap(image);

    let blob = await canvas.convertToBlob({
      type: `image/${options?.format ?? "png"}`,
      quality: options?.quality / 100,
    });

    let dataURL = await new Promise((resolve, reject) => {
      let reader = new FileReader();
      reader.onload = () => resolve(reader.result);
      reader.onerror = () => reject(reader.error);
      reader.readAsDataURL(blob);
    });

    return dataURL;
  }

  /**
   * @property {integer | null} innerWindowID
   *        The last known innerWindowID loaded into this tab's docShell. This
   *        property must remain in sync with the last known values of
   *        properties such as `url` and `title`. Any operations on the content
   *        of an out-of-process tab will automatically fail if the
   *        innerWindowID of the tab when the message is received does not match
   *        the value of this property when the message was sent.
   *        @readonly
   */
  get innerWindowID() {
    return this.browser.innerWindowID;
  }

  /**
   * @property {boolean} hasTabPermission
   *        Returns true if the extension has permission to access restricted
   *        properties of this tab, such as `url`, `title`, and `favIconUrl`.
   *        @readonly
   */
  get hasTabPermission() {
    return (
      this.extension.hasPermission("tabs") ||
      this.hasActiveTabPermission ||
      this.matchesHostPermission
    );
  }

  /**
   * @property {boolean} hasActiveTabPermission
   *        Returns true if the extension has the "activeTab" permission, and
   *        has been granted access to this tab due to a user executing an
   *        extension action.
   *
   *        If true, the extension may load scripts and CSS into this tab, and
   *        access restricted properties, such as its `url`.
   *        @readonly
   */
  get hasActiveTabPermission() {
    return (
      (this.extension.originControls ||
        this.extension.hasPermission("activeTab")) &&
      this.activeTabWindowID != null &&
      this.activeTabWindowID === this.innerWindowID
    );
  }

  /**
   * @property {boolean} matchesHostPermission
   *        Returns true if the extensions host permissions match the current tab url.
   *        @readonly
   */
  get matchesHostPermission() {
    return this.extension.allowedOrigins.matches(this._uri);
  }

  /**
   * @property {boolean} incognito
   *        Returns true if this is a private browsing tab, false otherwise.
   *        @readonly
   */
  get _incognito() {
    return PrivateBrowsingUtils.isBrowserPrivate(this.browser);
  }

  /**
   * @property {string} _url
   *        Returns the current URL of this tab. Does not do any permission
   *        checks.
   *        @readonly
   */
  get _url() {
    return this.browser.currentURI.spec;
  }

  /**
   * @property {string | undefined} url
   *        Returns the current URL of this tab if the extension has permission
   *        to read it, or undefined otherwise.
   *        @readonly
   */
  get url() {
    if (this.hasTabPermission) {
      return this._url;
    }
    return undefined;
  }

  /**
   * @property {nsIURI} _uri
   *        Returns the current URI of this tab.
   *        @readonly
   */
  get _uri() {
    return this.browser.currentURI;
  }

  /**
   * @property {string} _title
   *        Returns the current title of this tab. Does not do any permission
   *        checks.
   *        @readonly
   */
  get _title() {
    return this.browser.contentTitle || this.nativeTab.label;
  }

  /**
   * @property {nsIURI | undefined} title
   *        Returns the current title of this tab if the extension has permission
   *        to read it, or undefined otherwise.
   *        @readonly
   */
  get title() {
    if (this.hasTabPermission) {
      return this._title;
    }
    return undefined;
  }

  /**
   * @property {string} _favIconUrl
   *        Returns the current favicon URL of this tab. Does not do any permission
   *        checks.
   *        @readonly
   *        @abstract
   */
  get _favIconUrl() {
    throw new Error("Not implemented");
  }

  /**
   * @property {nsIURI | undefined} faviconUrl
   *        Returns the current faviron URL of this tab if the extension has permission
   *        to read it, or undefined otherwise.
   *        @readonly
   */
  get favIconUrl() {
    if (this.hasTabPermission) {
      return this._favIconUrl;
    }
    return undefined;
  }

  /**
   * @property {integer} lastAccessed
   *        Returns the last time the tab was accessed as the number of
   *        milliseconds since epoch.
   *        @readonly
   *        @abstract
   */
  get lastAccessed() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} audible
   *        Returns true if the tab is currently playing audio, false otherwise.
   *        @readonly
   *        @abstract
   */
  get audible() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} autoDiscardable
   *        Returns true if the tab can be discarded on memory pressure, false otherwise.
   *        @readonly
   *        @abstract
   */
  get autoDiscardable() {
    throw new Error("Not implemented");
  }

  /**
   * @property {XULElement} browser
   *        Returns the XUL browser for the given tab.
   *        @readonly
   *        @abstract
   */
  get browser() {
    throw new Error("Not implemented");
  }

  /**
   * @property {BrowsingContext} browsingContext
   *        Returns the BrowsingContext for the given tab.
   *        @readonly
   */
  get browsingContext() {
    return this.browser?.browsingContext;
  }

  /**
   * @property {FrameLoader} frameLoader
   *        Returns the frameloader for the given tab.
   *        @readonly
   */
  get frameLoader() {
    return this.browser && this.browser.frameLoader;
  }

  /**
   * @property {string} cookieStoreId
   *        Returns the cookie store identifier for the given tab.
   *        @readonly
   *        @abstract
   */
  get cookieStoreId() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} openerTabId
   *        Returns the ID of the tab which opened this one.
   *        @readonly
   */
  get openerTabId() {
    return null;
  }

  /**
   * @property {integer} discarded
   *        Returns true if the tab is discarded.
   *        @readonly
   *        @abstract
   */
  get discarded() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} height
   *        Returns the pixel height of the visible area of the tab.
   *        @readonly
   *        @abstract
   */
  get height() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} hidden
   *        Returns true if the tab is hidden.
   *        @readonly
   *        @abstract
   */
  get hidden() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} index
   *        Returns the index of the tab in its window's tab list.
   *        @readonly
   *        @abstract
   */
  get index() {
    throw new Error("Not implemented");
  }

  /**
   * @property {MutedInfo} mutedInfo
   *        Returns information about the tab's current audio muting status.
   *        @readonly
   *        @abstract
   */
  get mutedInfo() {
    throw new Error("Not implemented");
  }

  /**
   * @property {SharingState} sharingState
   *        Returns object with tab sharingState.
   *        @readonly
   *        @abstract
   */
  get sharingState() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} pinned
   *        Returns true if the tab is pinned, false otherwise.
   *        @readonly
   *        @abstract
   */
  get pinned() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} active
   *        Returns true if the tab is the currently-selected tab, false
   *        otherwise.
   *        @readonly
   *        @abstract
   */
  get active() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} highlighted
   *        Returns true if the tab is highlighted.
   *        @readonly
   *        @abstract
   */
  get highlighted() {
    throw new Error("Not implemented");
  }

  /**
   * @property {string} status
   *        Returns the current loading status of the tab. May be either
   *        "loading" or "complete".
   *        @readonly
   *        @abstract
   */
  get status() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} height
   *        Returns the pixel height of the visible area of the tab.
   *        @readonly
   *        @abstract
   */
  get width() {
    throw new Error("Not implemented");
  }

  /**
   * @property {DOMWindow} window
   *        Returns the browser window to which the tab belongs.
   *        @readonly
   *        @abstract
   */
  get window() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} window
   *        Returns the numeric ID of the browser window to which the tab belongs.
   *        @readonly
   *        @abstract
   */
  get windowId() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} attention
   *          Returns true if the tab is drawing attention.
   *          @readonly
   *          @abstract
   */
  get attention() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} isArticle
   *        Returns true if the document in the tab can be rendered in reader
   *        mode.
   *        @readonly
   *        @abstract
   */
  get isArticle() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} isInReaderMode
   *        Returns true if the document in the tab is being rendered in reader
   *        mode.
   *        @readonly
   *        @abstract
   */
  get isInReaderMode() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} successorTabId
   *        @readonly
   *        @abstract
   */
  get successorTabId() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} groupId
   *        @readonly
   *        @abstract
   */
  get groupId() {
    throw new Error("Not implemented");
  }

  /**
   * Returns true if this tab matches the the given query info object. Omitted
   * or null have no effect on the match.
   *
   * @param {object} queryInfo
   *        The query info against which to match.
   * @param {boolean} [queryInfo.active]
   *        Matches against the exact value of the tab's `active` attribute.
   * @param {boolean} [queryInfo.audible]
   *        Matches against the exact value of the tab's `audible` attribute.
   * @param {boolean} [queryInfo.autoDiscardable]
   *        Matches against the exact value of the tab's `autoDiscardable` attribute.
   * @param {string} [queryInfo.cookieStoreId]
   *        Matches against the exact value of the tab's `cookieStoreId` attribute.
   * @param {boolean} [queryInfo.discarded]
   *        Matches against the exact value of the tab's `discarded` attribute.
   * @param {boolean} [queryInfo.hidden]
   *        Matches against the exact value of the tab's `hidden` attribute.
   * @param {boolean} [queryInfo.highlighted]
   *        Matches against the exact value of the tab's `highlighted` attribute.
   * @param {integer} [queryInfo.index]
   *        Matches against the exact value of the tab's `index` attribute.
   * @param {boolean} [queryInfo.muted]
   *        Matches against the exact value of the tab's `mutedInfo.muted` attribute.
   * @param {boolean} [queryInfo.pinned]
   *        Matches against the exact value of the tab's `pinned` attribute.
   * @param {string} [queryInfo.status]
   *        Matches against the exact value of the tab's `status` attribute.
   * @param {string} [queryInfo.title]
   *        Matches against the exact value of the tab's `title` attribute.
   * @param {string|boolean } [queryInfo.screen]
   *        Matches against the exact value of the tab's `sharingState.screen` attribute, or use true to match any screen sharing tab.
   * @param {boolean} [queryInfo.camera]
   *        Matches against the exact value of the tab's `sharingState.camera` attribute.
   * @param {boolean} [queryInfo.microphone]
   *        Matches against the exact value of the tab's `sharingState.microphone` attribute.
   *
   *        Note: Per specification, this should perform a pattern match, rather
   *        than an exact value match, and will do so in the future.
   * @param {MatchPattern} [queryInfo.url]
   *        Requires the tab's URL to match the given MatchPattern object.
   * @param {integer} [queryInfo.groupId]
   *        Matches against the exact value of the tab's `groupId` attribute.
   *
   * @returns {boolean}
   *        True if the tab matches the query.
   */
  matches(queryInfo) {
    const PROPS = [
      "active",
      "audible",
      "autoDiscardable",
      "discarded",
      "hidden",
      "highlighted",
      "index",
      "openerTabId",
      "pinned",
      "status",
      "groupId",
    ];

    function checkProperty(prop, obj) {
      return queryInfo[prop] != null && queryInfo[prop] !== obj[prop];
    }

    if (PROPS.some(prop => checkProperty(prop, this))) {
      return false;
    }

    if (checkProperty("muted", this.mutedInfo)) {
      return false;
    }

    let state = this.sharingState;
    if (["camera", "microphone"].some(prop => checkProperty(prop, state))) {
      return false;
    }
    // query for screen can be boolean (ie. any) or string (ie. specific).
    if (queryInfo.screen !== null) {
      let match =
        typeof queryInfo.screen == "boolean"
          ? queryInfo.screen === !!state.screen
          : queryInfo.screen === state.screen;
      if (!match) {
        return false;
      }
    }

    if (queryInfo.cookieStoreId) {
      if (!queryInfo.cookieStoreId.includes(this.cookieStoreId)) {
        return false;
      }
    }

    if (queryInfo.url || queryInfo.title) {
      if (!this.hasTabPermission) {
        return false;
      }
      // Using _uri and _title instead of url/title to avoid repeated permission checks.
      if (queryInfo.url && !queryInfo.url.matches(this._uri)) {
        return false;
      }
      if (queryInfo.title && !queryInfo.title.matches(this._title)) {
        return false;
      }
    }

    return true;
  }

  /**
   * Converts this tab object to a JSON-compatible object containing the values
   * of its properties which the extension is permitted to access, in the format
   * required to be returned by WebExtension APIs.
   *
   * @param {object} [fallbackTabSize]
   *        A geometry data if the lazy geometry data for this tab hasn't been
   *        initialized yet.
   * @returns {object}
   */
  convert(fallbackTabSize = null) {
    let result = {
      id: this.id,
      index: this.index,
      windowId: this.windowId,
      highlighted: this.highlighted,
      active: this.active,
      attention: this.attention,
      pinned: this.pinned,
      status: this.status,
      hidden: this.hidden,
      discarded: this.discarded,
      incognito: this.incognito,
      width: this.width,
      height: this.height,
      lastAccessed: this.lastAccessed,
      audible: this.audible,
      autoDiscardable: this.autoDiscardable,
      mutedInfo: this.mutedInfo,
      isArticle: this.isArticle,
      isInReaderMode: this.isInReaderMode,
      sharingState: this.sharingState,
      successorTabId: this.successorTabId,
      groupId: this.groupId,
      cookieStoreId: this.cookieStoreId,
    };

    // If the tab has not been fully layed-out yet, fallback to the geometry
    // from a different tab (usually the currently active tab).
    if (fallbackTabSize && (!result.width || !result.height)) {
      result.width = fallbackTabSize.width;
      result.height = fallbackTabSize.height;
    }

    let opener = this.openerTabId;
    if (opener) {
      result.openerTabId = opener;
    }

    if (this.hasTabPermission) {
      for (let prop of ["url", "title", "favIconUrl"]) {
        // We use the underscored variants here to avoid the redundant
        // permissions checks imposed on the public properties.
        let val = this[`_${prop}`];
        if (val) {
          result[prop] = val;
        }
      }
    }

    return result;
  }

  /**
   * Query each content process hosting subframes of the tab, return results.
   *
   * @param {string} message
   * @param {object} options
   *        These options are also sent to the message handler in the
   *        `ExtensionContentChild`.
   * @param {number[]} options.frameIds
   *        When omitted, all frames will be queried.
   * @param {boolean} options.returnResultsWithFrameIds
   * @returns {Promise[]}
   */
  async queryContent(message, options) {
    let { frameIds } = options;

    /** @type {Map<nsIDOMProcessParent, innerWindowId[]>} */
    let byProcess = new DefaultMap(() => []);
    // We use this set to know which frame IDs are potentially invalid (as in
    // not found when visiting the tab's BC tree below) when frameIds is a
    // non-empty list of frame IDs.
    let frameIdsSet = new Set(frameIds);

    // Recursively walk the tab's BC tree, find all frames, group by process.
    function visit(bc) {
      let win = bc.currentWindowGlobal;
      let frameId = bc.parent ? bc.id : 0;

      if (win?.domProcess && (!frameIds || frameIdsSet.has(frameId))) {
        byProcess.get(win.domProcess).push(win.innerWindowId);
        frameIdsSet.delete(frameId);
      }

      if (!frameIds || frameIdsSet.size > 0) {
        bc.children.forEach(visit);
      }
    }
    visit(this.browsingContext);

    if (frameIdsSet.size > 0) {
      throw new ExtensionError(
        `Invalid frame IDs: [${Array.from(frameIdsSet).join(", ")}].`
      );
    }

    let promises = Array.from(byProcess.entries(), ([proc, windows]) =>
      proc.getActor("ExtensionContent").sendQuery(message, { windows, options })
    );

    let results = await Promise.all(promises).catch(err => {
      if (err.name === "DataCloneError") {
        let fileName = options.jsPaths.slice(-1)[0] || "<anonymous code>";
        let message = `Script '${fileName}' result is non-structured-clonable data`;
        return Promise.reject({ message, fileName });
      }
      throw err;
    });
    results = results.flat();

    if (!results.length) {
      let errorMessage = "Missing host permission for the tab";
      if (!frameIds || frameIds.length > 1 || frameIds[0] !== 0) {
        errorMessage += " or frames";
      }

      throw new ExtensionError(errorMessage);
    }

    if (frameIds && frameIds.length === 1 && results.length > 1) {
      throw new ExtensionError("Internal error: multiple windows matched");
    }

    return results;
  }

  /**
   * Inserts a script or stylesheet in the given tab, and returns a promise
   * which resolves when the operation has completed.
   *
   * @param {BaseContext} context
   *        The extension context for which to perform the injection.
   * @param {InjectDetails} details
   *        The InjectDetails object, specifying what to inject, where, and
   *        when.
   * @param {string} kind
   *        The kind of data being injected. Either "script" or "css".
   * @param {string} method
   *        The name of the method which was called to trigger the injection.
   *        Used to generate appropriate error messages on failure.
   *
   * @returns {Promise}
   *        Resolves to the result of the execution, once it has completed.
   * @private
   */
  _execute(context, details, kind, method) {
    let options = {
      jsPaths: [],
      cssPaths: [],
      removeCSS: method == "removeCSS",
      extensionId: context.extension.id,
    };

    // We require a `code` or a `file` property, but we can't accept both.
    if ((details.code === null) == (details.file === null)) {
      return Promise.reject({
        message: `${method} requires either a 'code' or a 'file' property, but not both`,
      });
    }

    if (details.frameId !== null && details.allFrames) {
      return Promise.reject({
        message: `'frameId' and 'allFrames' are mutually exclusive`,
      });
    }

    options.hasActiveTabPermission = this.hasActiveTabPermission;
    options.matches = this.extension.allowedOrigins.patterns.map(
      host => host.pattern
    );

    if (details.code !== null) {
      options[`${kind}Code`] = details.code;
    }
    if (details.file !== null) {
      let url = context.uri.resolve(details.file);
      if (!this.extension.isExtensionURL(url)) {
        return Promise.reject({
          message: "Files to be injected must be within the extension",
        });
      }
      options[`${kind}Paths`].push(url);
    }

    if (details.allFrames) {
      options.allFrames = true;
    } else if (details.frameId !== null) {
      options.frameIds = [details.frameId];
    } else if (!details.allFrames) {
      options.frameIds = [0];
    }

    if (details.matchAboutBlank) {
      options.matchAboutBlank = details.matchAboutBlank;
    }
    if (details.runAt !== null) {
      options.runAt = details.runAt;
    } else {
      options.runAt = "document_idle";
    }
    if (details.cssOrigin !== null) {
      options.cssOrigin = details.cssOrigin;
    } else {
      options.cssOrigin = "author";
    }

    options.wantReturnValue = true;

    // The scripting API (defined in `parent/ext-scripting.js`) has its own
    // `execute()` function that calls `queryContent()` as well. Make sure to
    // keep both in sync when relevant.
    return this.queryContent("Execute", options);
  }

  /**
   * Executes a script in the tab's content window, and returns a Promise which
   * resolves to the result of the evaluation, or rejects to the value of any
   * error the injection generates.
   *
   * @param {BaseContext} context
   *        The extension context for which to inject the script.
   * @param {InjectDetails} details
   *        The InjectDetails object, specifying what to inject, where, and
   *        when.
   *
   * @returns {Promise}
   *        Resolves to the result of the evaluation of the given script, once
   *        it has completed, or rejects with any error the evaluation
   *        generates.
   */
  executeScript(context, details) {
    return this._execute(context, details, "js", "executeScript");
  }

  /**
   * Injects CSS into the tab's content window, and returns a Promise which
   * resolves when the injection is complete.
   *
   * @param {BaseContext} context
   *        The extension context for which to inject the script.
   * @param {InjectDetails} details
   *        The InjectDetails object, specifying what to inject, and where.
   *
   * @returns {Promise}
   *        Resolves when the injection has completed.
   */
  insertCSS(context, details) {
    return this._execute(context, details, "css", "insertCSS").then(() => {});
  }

  /**
   * Removes CSS which was previously into the tab's content window via
   * `insertCSS`, and returns a Promise which resolves when the operation is
   * complete.
   *
   * @param {BaseContext} context
   *        The extension context for which to remove the CSS.
   * @param {InjectDetails} details
   *        The InjectDetails object, specifying what to remove, and from where.
   *
   * @returns {Promise}
   *        Resolves when the operation has completed.
   */
  removeCSS(context, details) {
    return this._execute(context, details, "css", "removeCSS").then(() => {});
  }
}

defineLazyGetter(TabBase.prototype, "incognito", function () {
  return this._incognito;
});

// Note: These must match the values in windows.json.
const WINDOW_ID_NONE = -1;
const WINDOW_ID_CURRENT = -2;

/**
 * A platform-independent base class for extension-specific wrappers around
 * native browser windows
 *
 * @param {Extension} extension
 *        The extension object for which this wrapper is being created.
 * @param {DOMWindow} window
 *        The browser DOM window which is being wrapped.
 * @param {integer} id
 *        The numeric ID of this DOM window object. This ID should be the same for
 *        every extension, and for the lifetime of the window.
 */
class WindowBase {
  constructor(extension, window, id) {
    if (!extension.canAccessWindow(window)) {
      throw new ExtensionError("extension cannot access window");
    }
    this.extension = extension;
    this.window = window;
    this.id = id;
  }

  /**
   * @property {nsIAppWindow} appWindow
   *        The nsIAppWindow object for this browser window.
   *        @readonly
   */
  get appWindow() {
    return this.window.docShell.treeOwner
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIAppWindow);
  }

  /**
   * Returns true if this window is the current window for the given extension
   * context, false otherwise.
   *
   * @param {BaseContext} context
   *        The extension context for which to perform the check.
   *
   * @returns {boolean}
   */
  isCurrentFor(context) {
    if (context && context.currentWindow) {
      return this.window === context.currentWindow;
    }
    return this.isLastFocused;
  }

  /**
   * @property {string} type
   *        The type of the window, as defined by the WebExtension API. May be
   *        either "normal" or "popup".
   *        @readonly
   */
  get type() {
    let { chromeFlags } = this.appWindow;

    if (chromeFlags & Ci.nsIWebBrowserChrome.CHROME_OPENAS_DIALOG) {
      return "popup";
    }

    return "normal";
  }

  /**
   * Converts this window object to a JSON-compatible object which may be
   * returned to an extension, in the format required to be returned by
   * WebExtension APIs.
   *
   * @param {object} [getInfo]
   *        An optional object, the properties of which determine what data is
   *        available on the result object.
   * @param {boolean} [getInfo.populate]
   *        Of true, the result object will contain a `tabs` property,
   *        containing an array of converted Tab objects, one for each tab in
   *        the window.
   *
   * @returns {object}
   */
  convert(getInfo) {
    let result = {
      id: this.id,
      focused: this.focused,
      top: this.top,
      left: this.left,
      width: this.width,
      height: this.height,
      incognito: this.incognito,
      type: this.type,
      state: this.state,
      alwaysOnTop: this.alwaysOnTop,
      title: this.title,
    };

    if (getInfo && getInfo.populate) {
      result.tabs = Array.from(this.getTabs(), tab => tab.convert());
    }

    return result;
  }

  /**
   * Returns true if this window matches the the given query info object. Omitted
   * or null have no effect on the match.
   *
   * @param {object} queryInfo
   *        The query info against which to match.
   * @param {boolean} [queryInfo.currentWindow]
   *        Matches against against the return value of `isCurrentFor()` for the
   *        given context.
   * @param {boolean} [queryInfo.lastFocusedWindow]
   *        Matches against the exact value of the window's `isLastFocused` attribute.
   * @param {boolean} [queryInfo.windowId]
   *        Matches against the exact value of the window's ID, taking into
   *        account the special WINDOW_ID_CURRENT value.
   * @param {string} [queryInfo.windowType]
   *        Matches against the exact value of the window's `type` attribute.
   * @param {BaseContext} context
   *        The extension context for which the matching is being performed.
   *        Used to determine the current window for relevant properties.
   *
   * @returns {boolean}
   *        True if the window matches the query.
   */
  matches(queryInfo, context) {
    if (
      queryInfo.lastFocusedWindow !== null &&
      queryInfo.lastFocusedWindow !== this.isLastFocused
    ) {
      return false;
    }

    if (queryInfo.windowType !== null && queryInfo.windowType !== this.type) {
      return false;
    }

    if (queryInfo.windowId !== null) {
      if (queryInfo.windowId === WINDOW_ID_CURRENT) {
        if (!this.isCurrentFor(context)) {
          return false;
        }
      } else if (queryInfo.windowId !== this.id) {
        return false;
      }
    }

    if (
      queryInfo.currentWindow !== null &&
      queryInfo.currentWindow !== this.isCurrentFor(context)
    ) {
      return false;
    }

    return true;
  }

  /**
   * @property {boolean} focused
   *        Returns true if the browser window is currently focused.
   *        @readonly
   *        @abstract
   */
  get focused() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} top
   *        Returns the pixel offset of the top of the window from the top of
   *        the screen.
   *        @readonly
   *        @abstract
   */
  get top() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} left
   *        Returns the pixel offset of the left of the window from the left of
   *        the screen.
   *        @readonly
   *        @abstract
   */
  get left() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} width
   *        Returns the pixel width of the window.
   *        @readonly
   *        @abstract
   */
  get width() {
    throw new Error("Not implemented");
  }

  /**
   * @property {integer} height
   *        Returns the pixel height of the window.
   *        @readonly
   *        @abstract
   */
  get height() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} incognito
   *        Returns true if this is a private browsing window, false otherwise.
   *        @readonly
   *        @abstract
   */
  get incognito() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} alwaysOnTop
   *        Returns true if this window is constrained to always remain above
   *        other windows.
   *        @readonly
   *        @abstract
   */
  get alwaysOnTop() {
    throw new Error("Not implemented");
  }

  /**
   * @property {boolean} isLastFocused
   *        Returns true if this is the browser window which most recently had
   *        focus.
   *        @readonly
   *        @abstract
   */
  get isLastFocused() {
    throw new Error("Not implemented");
  }

  /**
   * @property {string} state
   *        Returns or sets the current state of this window, as determined by
   *        `getState()`.
   *        @abstract
   */
  get state() {
    throw new Error("Not implemented");
  }

  set state(state) {
    throw new Error("Not implemented");
  }

  /**
   * @property {nsIURI | undefined} title
   *        Returns the current title of this window if the extension has permission
   *        to read it, or undefined otherwise.
   *        @readonly
   */
  get title() {
    // activeTab may be null when a new window is adopting an existing tab as its first tab
    // (See Bug 1458918 for rationale).
    if (this.activeTab && this.activeTab.hasTabPermission) {
      return this._title;
    }
    return undefined;
  }

  // The JSDoc validator does not support @returns tags in abstract functions or
  // star functions without return statements.
  /* eslint-disable valid-jsdoc */
  /**
   * Returns the window state of the given window.
   *
   * @param {DOMWindow} _window
   *        The window for which to return a state.
   *
   * @returns {string}
   *        The window's state. One of "normal", "minimized", "maximized",
   *        "fullscreen", or "docked".
   * @static
   * @abstract
   */
  static getState(_window) {
    throw new Error("Not implemented");
  }

  /**
   * Returns an iterator of TabBase objects for each tab in this window.
   *
   * @returns {Iterator<TabBase>}
   */
  getTabs() {
    throw new Error("Not implemented");
  }

  /**
   * Returns an iterator of TabBase objects for each highlighted tab in this window.
   *
   * @returns {Iterator<TabBase>}
   */
  getHighlightedTabs() {
    throw new Error("Not implemented");
  }

  /**
   * @property {TabBase} The window's currently active tab.
   */
  get activeTab() {
    throw new Error("Not implemented");
  }

  /**
   * Returns the window's tab at the specified index.
   *
   * @param {integer} _index
   *        The index of the desired tab.
   *
   * @returns {TabBase|undefined}
   */
  getTabAtIndex(_index) {
    throw new Error("Not implemented");
  }
  /* eslint-enable valid-jsdoc */
}

Object.assign(WindowBase, { WINDOW_ID_NONE, WINDOW_ID_CURRENT });

/**
 * The parameter type of "tab-attached" events, which are emitted when a
 * pre-existing tab is attached to a new window.
 *
 * @typedef {object} TabAttachedEvent
 * @property {NativeTab} tab
 *        The native tab object in the window to which the tab is being
 *        attached. This may be a different object than was used to represent
 *        the tab in the old window.
 * @property {integer} tabId
 *        The ID of the tab being attached.
 * @property {integer} newWindowId
 *        The ID of the window to which the tab is being attached.
 * @property {integer} newPosition
 *        The position of the tab in the tab list of the new window.
 */

/**
 * The parameter type of "tab-detached" events, which are emitted when a
 * pre-existing tab is detached from a window, in order to be attached to a new
 * window.
 *
 * @typedef {object} TabDetachedEvent
 * @property {NativeTab} tab
 *        The native tab object in the window from which the tab is being
 *        detached. This may be a different object than will be used to
 *        represent the tab in the new window.
 * @property {NativeTab} adoptedBy
 *        The native tab object in the window to which the tab will be attached,
 *        and is adopting the contents of this tab. This may be a different
 *        object than the tab in the previous window.
 * @property {integer} tabId
 *        The ID of the tab being detached.
 * @property {integer} oldWindowId
 *        The ID of the window from which the tab is being detached.
 * @property {integer} oldPosition
 *        The position of the tab in the tab list of the window from which it is
 *        being detached.
 */

/**
 * The parameter type of "tab-created" events, which are emitted when a
 * new tab is created.
 *
 * @typedef {object} TabCreatedEvent
 * @property {NativeTab} tab
 *        The native tab object for the tab which is being created.
 */

/**
 * The parameter type of "tab-removed" events, which are emitted when a
 * tab is removed and destroyed.
 *
 * @typedef {object} TabRemovedEvent
 * @property {NativeTab} tab
 *        The native tab object for the tab which is being removed.
 * @property {integer} tabId
 *        The ID of the tab being removed.
 * @property {integer} windowId
 *        The ID of the window from which the tab is being removed.
 * @property {boolean} isWindowClosing
 *        True if the tab is being removed because the window is closing.
 */

/**
 * An object containing basic, extension-independent information about the window
 * and tab that a XUL <browser> belongs to.
 *
 * @typedef {object} BrowserData
 * @property {integer} tabId
 *        The numeric ID of the tab that a <browser> belongs to, or -1 if it
 *        does not belong to a tab.
 * @property {integer} windowId
 *        The numeric ID of the browser window that a <browser> belongs to, or -1
 *        if it does not belong to a browser window.
 */

/**
 * A platform-independent base class for the platform-specific TabTracker
 * classes, which track the opening and closing of tabs, and manage the mapping
 * of them between numeric IDs and native tab objects.
 *
 * Instances of this class are EventEmitters which emit the following events,
 * each with an argument of the given type:
 *
 * - "tab-attached" {@link TabAttacheEvent}
 * - "tab-detached" {@link TabDetachedEvent}
 * - "tab-created" {@link TabCreatedEvent}
 * - "tab-removed" {@link TabRemovedEvent}
 */
class TabTrackerBase extends EventEmitter {
  on(...args) {
    if (!this.initialized) {
      this.init();
    }

    return super.on(...args); // eslint-disable-line mozilla/balanced-listeners
  }

  /**
   * Called to initialize the tab tracking listeners the first time that an
   * event listener is added.
   *
   * @protected
   * @abstract
   */
  init() {
    throw new Error("Not implemented");
  }

  // The JSDoc validator does not support @returns tags in abstract functions or
  // star functions without return statements.
  /* eslint-disable valid-jsdoc */
  /**
   * Returns the numeric ID for the given native tab.
   *
   * @param {NativeTab} _nativeTab
   *        The native tab for which to return an ID.
   *
   * @returns {integer}
   *        The tab's numeric ID.
   * @abstract
   */
  getId(_nativeTab) {
    throw new Error("Not implemented");
  }

  /**
   * Returns the native tab with the given numeric ID.
   *
   * @param {integer} _tabId
   *        The numeric ID of the tab to return.
   * @param {*} _default
   *        The value to return if no tab exists with the given ID.
   *
   * @returns {NativeTab}
   * @throws {ExtensionError}
   *       If no tab exists with the given ID and a default return value is not
   *       provided.
   * @abstract
   */
  getTab(_tabId, _default) {
    throw new Error("Not implemented");
  }

  /**
   * Returns basic information about the tab and window that the given browser
   * belongs to.
   *
   * @param {XULElement} browser
   *        The XUL browser element for which to return data.
   *
   * @returns {BrowserData}
   * @abstract
   */
  /* eslint-enable valid-jsdoc */
  getBrowserData() {
    throw new Error("Not implemented");
  }

  /**
   * @property {NativeTab} activeTab
   *        Returns the native tab object for the active tab in the
   *        most-recently focused window, or null if no live tabs currently
   *        exist.
   *        @abstract
   */
  get activeTab() {
    throw new Error("Not implemented");
  }
}

/**
 * A browser progress listener instance which calls a given listener function
 * whenever the status of the given browser changes.
 *
 * @param {function(object): void} listener
 *        A function to be called whenever the status of a tab's top-level
 *        browser. It is passed an object with a `browser` property pointing to
 *        the XUL browser, and a `status` property with a string description of
 *        the browser's status.
 * @private
 */
class StatusListener {
  constructor(listener) {
    this.listener = listener;
  }

  onStateChange(browser, webProgress, request, stateFlags, statusCode) {
    if (!webProgress.isTopLevel) {
      return;
    }

    let status;
    if (stateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW) {
      if (stateFlags & Ci.nsIWebProgressListener.STATE_START) {
        status = "loading";
      } else if (stateFlags & Ci.nsIWebProgressListener.STATE_STOP) {
        status = "complete";
      }
    } else if (
      stateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
      statusCode == Cr.NS_BINDING_ABORTED
    ) {
      status = "complete";
    }

    if (status) {
      this.listener({ browser, status });
    }
  }

  onLocationChange(browser, webProgress, request, locationURI) {
    if (webProgress.isTopLevel) {
      let status = webProgress.isLoadingDocument ? "loading" : "complete";
      this.listener({ browser, status, url: locationURI.spec });
    }
  }
}

/**
 * A platform-independent base class for the platform-specific WindowTracker
 * classes, which track the opening and closing of windows, and manage the
 * mapping of them between numeric IDs and native tab objects.
 */
class WindowTrackerBase extends EventEmitter {
  constructor() {
    super();

    this._handleWindowOpened = this._handleWindowOpened.bind(this);

    this._openListeners = new Set();
    this._closeListeners = new Set();

    this._listeners = new DefaultMap(() => new Set());

    this._statusListeners = new DefaultWeakMap(listener => {
      return new StatusListener(listener);
    });

    this._windowIds = new DefaultWeakMap(window => {
      return window.docShell.outerWindowID;
    });
  }

  isBrowserWindow(window) {
    let { documentElement } = window.document;

    return documentElement.getAttribute("windowtype") === "navigator:browser";
  }

  // The JSDoc validator does not support @returns tags in abstract functions or
  // star functions without return statements.
  /* eslint-disable valid-jsdoc */
  /**
   * Returns an iterator for all currently active browser windows.
   *
   * @param {boolean} [includeInomplete = false]
   *        If true, include browser windows which are not yet fully loaded.
   *        Otherwise, only include windows which are.
   *
   * @returns {Iterator<DOMWindow>}
   */
  /* eslint-enable valid-jsdoc */
  *browserWindows(includeIncomplete = false) {
    // The window type parameter is only available once the window's document
    // element has been created. This means that, when looking for incomplete
    // browser windows, we need to ignore the type entirely for windows which
    // haven't finished loading, since we would otherwise skip browser windows
    // in their early loading stages.
    // This is particularly important given that the "domwindowcreated" event
    // fires for browser windows when they're in that in-between state, and just
    // before we register our own "domwindowcreated" listener.

    for (let window of Services.wm.getEnumerator("")) {
      let ok = includeIncomplete;
      if (window.document.readyState === "complete") {
        ok = this.isBrowserWindow(window);
      }

      if (ok) {
        yield window;
      }
    }
  }

  /**
   * @property {DOMWindow|null} topWindow
   *        The currently active, or topmost, browser window, or null if no
   *        browser window is currently open.
   *        @readonly
   */
  get topWindow() {
    return Services.wm.getMostRecentWindow("navigator:browser");
  }

  /**
   * @property {DOMWindow|null} topWindow
   *        The currently active, or topmost, browser window that is not
   *        private browsing, or null if no browser window is currently open.
   *        @readonly
   */
  get topNonPBWindow() {
    return Services.wm.getMostRecentNonPBWindow("navigator:browser");
  }

  /**
   * Returns the top window accessible by the extension.
   *
   * @param {BaseContext} context
   *        The extension context for which to return the current window.
   *
   * @returns {DOMWindow|null}
   */
  getTopWindow(context) {
    if (context && !context.privateBrowsingAllowed) {
      return this.topNonPBWindow;
    }
    return this.topWindow;
  }

  /**
   * Returns the numeric ID for the given browser window.
   *
   * @param {DOMWindow} window
   *        The DOM window for which to return an ID.
   *
   * @returns {integer}
   *        The window's numeric ID.
   */
  getId(window) {
    return this._windowIds.get(window);
  }

  /**
   * Returns the browser window to which the given context belongs, or the top
   * browser window if the context does not belong to a browser window.
   *
   * @param {BaseContext} context
   *        The extension context for which to return the current window.
   *
   * @returns {DOMWindow|null}
   */
  getCurrentWindow(context) {
    return (context && context.currentWindow) || this.getTopWindow(context);
  }

  /**
   * Returns the browser window with the given ID.
   *
   * @param {integer} id
   *        The ID of the window to return.
   * @param {BaseContext} [context]
   *        The extension context for which the matching is being performed.
   *        Used to determine the current window for relevant properties.
   * @param {boolean} [strict = true]
   *        If false, undefined will be returned instead of throwing an error
   *        in case no window exists with the given ID.
   *
   * @returns {DOMWindow|undefined}
   * @throws {ExtensionError}
   *        If no window exists with the given ID and `strict` is true.
   */
  getWindow(id, context, strict = true) {
    if (id === WINDOW_ID_CURRENT) {
      return this.getCurrentWindow(context);
    }

    let window = Services.wm.getOuterWindowWithId(id);
    if (
      window &&
      !window.closed &&
      (window.document.readyState !== "complete" ||
        this.isBrowserWindow(window))
    ) {
      if (!context || context.canAccessWindow(window)) {
        // Tolerate incomplete windows because isBrowserWindow is only reliable
        // once the window is fully loaded.
        return window;
      }
    }

    if (strict) {
      throw new ExtensionError(`Invalid window ID: ${id}`);
    }
  }

  /**
   * @property {boolean} _haveListeners
   *        Returns true if any window open or close listeners are currently
   *        registered.
   * @private
   */
  get _haveListeners() {
    return this._openListeners.size > 0 || this._closeListeners.size > 0;
  }

  /**
   * Register the given listener function to be called whenever a new browser
   * window is opened.
   *
   * @param {function(DOMWindow): void} listener
   *        The listener function to register.
   */
  addOpenListener(listener) {
    if (!this._haveListeners) {
      Services.ww.registerNotification(this);
    }

    this._openListeners.add(listener);

    for (let window of this.browserWindows(true)) {
      if (window.document.readyState !== "complete") {
        window.addEventListener("load", this);
      }
    }
  }

  /**
   * Unregister a listener function registered in a previous addOpenListener
   * call.
   *
   * @param {function(DOMWindow): void} listener
   *        The listener function to unregister.
   */
  removeOpenListener(listener) {
    this._openListeners.delete(listener);

    if (!this._haveListeners) {
      Services.ww.unregisterNotification(this);
    }
  }

  /**
   * Register the given listener function to be called whenever a browser
   * window is closed.
   *
   * @param {function(DOMWindow): void} listener
   *        The listener function to register.
   */
  addCloseListener(listener) {
    if (!this._haveListeners) {
      Services.ww.registerNotification(this);
    }

    this._closeListeners.add(listener);
  }

  /**
   * Unregister a listener function registered in a previous addCloseListener
   * call.
   *
   * @param {function(DOMWindow): void} listener
   *        The listener function to unregister.
   */
  removeCloseListener(listener) {
    this._closeListeners.delete(listener);

    if (!this._haveListeners) {
      Services.ww.unregisterNotification(this);
    }
  }

  /**
   * Handles load events for recently-opened windows, and adds additional
   * listeners which may only be safely added when the window is fully loaded.
   *
   * @param {Event} event
   *        A DOM event to handle.
   * @private
   */
  handleEvent(event) {
    if (event.type === "load") {
      event.currentTarget.removeEventListener(event.type, this);

      let window = event.target.defaultView;
      if (!this.isBrowserWindow(window)) {
        return;
      }

      for (let listener of this._openListeners) {
        try {
          listener(window);
        } catch (e) {
          Cu.reportError(e);
        }
      }
    }
  }

  /**
   * Observes "domwindowopened" and "domwindowclosed" events, notifies the
   * appropriate listeners, and adds necessary additional listeners to the new
   * windows.
   *
   * @param {DOMWindow} window
   *        A DOM window.
   * @param {string} topic
   *        The topic being observed.
   * @private
   */
  observe(window, topic) {
    if (topic === "domwindowclosed") {
      if (!this.isBrowserWindow(window)) {
        return;
      }

      window.removeEventListener("load", this);
      for (let listener of this._closeListeners) {
        try {
          listener(window);
        } catch (e) {
          Cu.reportError(e);
        }
      }
    } else if (topic === "domwindowopened") {
      window.addEventListener("load", this);
    }
  }

  /**
   * Add an event listener to be called whenever the given DOM event is received
   * at the top level of any browser window.
   *
   * @param {string} type
   *        The type of event to listen for. May be any valid DOM event name, or
   *        one of the following special cases:
   *
   *        - "progress": Adds a tab progress listener to every browser window.
   *        - "status": Adds a StatusListener to every tab of every browser
   *           window.
   *        - "domwindowopened": Acts as an alias for addOpenListener.
   *        - "domwindowclosed": Acts as an alias for addCloseListener.
   * @param {Function | object} listener
   *        The listener to invoke in response to the given events.
   *
   * @returns {undefined}
   */
  addListener(type, listener) {
    if (type === "domwindowopened") {
      return this.addOpenListener(listener);
    } else if (type === "domwindowclosed") {
      return this.addCloseListener(listener);
    }

    if (this._listeners.size === 0) {
      this.addOpenListener(this._handleWindowOpened);
    }

    if (type === "status") {
      listener = this._statusListeners.get(listener);
      type = "progress";
    }

    this._listeners.get(type).add(listener);

    // Register listener on all existing windows.
    for (let window of this.browserWindows()) {
      this._addWindowListener(window, type, listener);
    }
  }

  /**
   * Removes an event listener previously registered via an addListener call.
   *
   * @param {string} type
   *        The type of event to stop listening for.
   * @param {Function | object} listener
   *        The listener to remove.
   *
   * @returns {undefined}
   */
  removeListener(type, listener) {
    if (type === "domwindowopened") {
      return this.removeOpenListener(listener);
    } else if (type === "domwindowclosed") {
      return this.removeCloseListener(listener);
    }

    if (type === "status") {
      listener = this._statusListeners.get(listener);
      type = "progress";
    }

    let listeners = this._listeners.get(type);
    listeners.delete(listener);

    if (listeners.size === 0) {
      this._listeners.delete(type);
      if (this._listeners.size === 0) {
        this.removeOpenListener(this._handleWindowOpened);
      }
    }

    // Unregister listener from all existing windows.
    let useCapture = type === "focus" || type === "blur";
    for (let window of this.browserWindows()) {
      if (type === "progress") {
        this.removeProgressListener(window, listener);
      } else {
        window.removeEventListener(type, listener, useCapture);
      }
    }
  }

  /**
   * Adds a listener for the given event to the given window.
   *
   * @param {DOMWindow} window
   *        The browser window to which to add the listener.
   * @param {string} eventType
   *        The type of DOM event to listen for, or "progress" to add a tab
   *        progress listener.
   * @param {Function | object} listener
   *        The listener to add.
   * @private
   */
  _addWindowListener(window, eventType, listener) {
    let useCapture = eventType === "focus" || eventType === "blur";

    if (eventType === "progress") {
      this.addProgressListener(window, listener);
    } else {
      window.addEventListener(eventType, listener, useCapture);
    }
  }

  /**
   * A private method which is called whenever a new browser window is opened,
   * and adds the necessary listeners to it.
   *
   * @param {DOMWindow} window
   *        The window being opened.
   * @private
   */
  _handleWindowOpened(window) {
    for (let [eventType, listeners] of this._listeners) {
      for (let listener of listeners) {
        this._addWindowListener(window, eventType, listener);
      }
    }
  }

  /**
   * Adds a tab progress listener to the given browser window.
   *
   * @param {DOMWindow} _window
   *        The browser window to which to add the listener.
   * @param {object} _listener
   *        The tab progress listener to add.
   * @abstract
   */
  addProgressListener(_window, _listener) {
    throw new Error("Not implemented");
  }

  /**
   * Removes a tab progress listener from the given browser window.
   *
   * @param {DOMWindow} _window
   *        The browser window from which to remove the listener.
   * @param {object} _listener
   *        The tab progress listener to remove.
   * @abstract
   */
  removeProgressListener(_window, _listener) {
    throw new Error("Not implemented");
  }
}

/**
 * Manages native tabs, their wrappers, and their dynamic permissions for a
 * particular extension.
 *
 * @param {Extension} extension
 *        The extension for which to manage tabs.
 */
class TabManagerBase {
  constructor(extension) {
    this.extension = extension;

    this._tabs = new DefaultWeakMap(tab => this.wrapTab(tab));
  }

  /**
   * If the extension has requested activeTab permission, grant it those
   * permissions for the current inner window in the given native tab.
   *
   * @param {NativeTab} nativeTab
   *        The native tab for which to grant permissions.
   */
  addActiveTabPermission(nativeTab) {
    let tab = this.getWrapper(nativeTab);
    if (
      this.extension.hasPermission("activeTab") ||
      (this.extension.originControls &&
        this.extension.optionalOrigins.matches(tab._uri))
    ) {
      // Note that, unlike Chrome, we don't currently clear this permission with
      // the tab navigates. If the inner window is revived from BFCache before
      // we've granted this permission to a new inner window, the extension
      // maintains its permissions for it.
      tab.activeTabWindowID = tab.innerWindowID;
    }
  }

  /**
   * Revoke the extension's activeTab permissions for the current inner window
   * of the given native tab.
   *
   * @param {NativeTab} nativeTab
   *        The native tab for which to revoke permissions.
   */
  revokeActiveTabPermission(nativeTab) {
    this.getWrapper(nativeTab).activeTabWindowID = null;
  }

  /**
   * Returns true if the extension has requested activeTab permission, and has
   * been granted permissions for the current inner window if this tab.
   *
   * @param {NativeTab} nativeTab
   *        The native tab for which to check permissions.
   * @returns {boolean}
   *        True if the extension has activeTab permissions for this tab.
   */
  hasActiveTabPermission(nativeTab) {
    return this.getWrapper(nativeTab).hasActiveTabPermission;
  }

  /**
   * Activate MV3 content scripts if the extension has activeTab or an
   * (ungranted) host permission.
   *
   * @param {NativeTab} nativeTab
   */
  activateScripts(nativeTab) {
    let tab = this.getWrapper(nativeTab);
    if (
      this.extension.originControls &&
      !tab.matchesHostPermission &&
      (this.extension.optionalOrigins.matches(tab._uri) ||
        this.extension.hasPermission("activeTab")) &&
      (this.extension.contentScripts.length ||
        this.extension.registeredContentScripts.size)
    ) {
      tab.queryContent("ActivateScripts", { id: this.extension.id });
    }
  }

  /**
   * Returns true if the extension has permissions to access restricted
   * properties of the given native tab. In practice, this means that it has
   * either requested the "tabs" permission or has activeTab permissions for the
   * given tab.
   *
   * NOTE: Never use this method on an object that is not a native tab
   * for the current platform: this method implicitly generates a wrapper
   * for the passed nativeTab parameter and the platform-specific tabTracker
   * instance is likely to store it in a map which is cleared only when the
   * tab is closed (and so, if nativeTab is not a real native tab, it will
   * never be cleared from the platform-specific tabTracker instance),
   * See Bug 1458918 for a rationale.
   *
   * @param {NativeTab} nativeTab
   *        The native tab for which to check permissions.
   * @returns {boolean}
   *        True if the extension has permissions for this tab.
   */
  hasTabPermission(nativeTab) {
    return this.getWrapper(nativeTab).hasTabPermission;
  }

  /**
   * Returns this extension's TabBase wrapper for the given native tab. This
   * method will always return the same wrapper object for any given native tab.
   *
   * @param {NativeTab} nativeTab
   *        The tab for which to return a wrapper.
   *
   * @returns {TabBase|undefined}
   *        The wrapper for this tab.
   */
  getWrapper(nativeTab) {
    if (this.canAccessTab(nativeTab)) {
      return this._tabs.get(nativeTab);
    }
  }

  /**
   * Determines access using extension context.
   *
   * @param {NativeTab} _nativeTab
   *        The tab to check access on.
   * @returns {boolean}
   *        True if the extension has permissions for this tab.
   * @protected
   * @abstract
   */
  canAccessTab(_nativeTab) {
    throw new Error("Not implemented");
  }

  /**
   * Converts the given native tab to a JSON-compatible object, in the format
   * required to be returned by WebExtension APIs, which may be safely passed to
   * extension code.
   *
   * @param {NativeTab} nativeTab
   *        The native tab to convert.
   * @param {object} [fallbackTabSize]
   *        A geometry data if the lazy geometry data for this tab hasn't been
   *        initialized yet.
   *
   * @returns {object}
   */
  convert(nativeTab, fallbackTabSize = null) {
    return this.getWrapper(nativeTab).convert(fallbackTabSize);
  }

  // The JSDoc validator does not support @returns tags in abstract functions or
  // star functions without return statements.
  /* eslint-disable valid-jsdoc */
  /**
   * Returns an iterator of TabBase objects which match the given query info.
   *
   * @param {object | null} [queryInfo = null]
   *        An object containing properties on which to filter. May contain any
   *        properties which are recognized by {@link TabBase#matches} or
   *        {@link WindowBase#matches}. Unknown properties will be ignored.
   * @param {BaseContext|null} [context = null]
   *        The extension context for which the matching is being performed.
   *        Used to determine the current window for relevant properties.
   *
   * @returns {Iterator<TabBase>}
   */
  *query(queryInfo = null, context = null) {
    if (queryInfo) {
      if (queryInfo.url !== null) {
        queryInfo.url = parseMatchPatterns([].concat(queryInfo.url), {
          restrictSchemes: false,
        });
      }

      if (queryInfo.cookieStoreId !== null) {
        queryInfo.cookieStoreId = [].concat(queryInfo.cookieStoreId);
      }

      if (queryInfo.title !== null) {
        try {
          queryInfo.title = new MatchGlob(queryInfo.title);
        } catch (e) {
          throw new ExtensionError(`Invalid title: ${queryInfo.title}`);
        }
      }
    }
    function* candidates(windowWrapper) {
      if (queryInfo) {
        let { active, highlighted, index } = queryInfo;
        if (active === true) {
          let { activeTab } = windowWrapper;
          if (activeTab) {
            yield activeTab;
          }
          return;
        }
        if (index != null) {
          let tabWrapper = windowWrapper.getTabAtIndex(index);
          if (tabWrapper) {
            yield tabWrapper;
          }
          return;
        }
        if (highlighted === true) {
          yield* windowWrapper.getHighlightedTabs();
          return;
        }
      }
      yield* windowWrapper.getTabs();
    }
    let windowWrappers = this.extension.windowManager.query(queryInfo, context);
    for (let windowWrapper of windowWrappers) {
      for (let tabWrapper of candidates(windowWrapper)) {
        if (!queryInfo || tabWrapper.matches(queryInfo)) {
          yield tabWrapper;
        }
      }
    }
  }

  /**
   * Returns a TabBase wrapper for the tab with the given ID.
   *
   * @param {integer} _tabId
   *        The ID of the tab for which to return a wrapper.
   *
   * @returns {TabBase}
   * @throws {ExtensionError}
   *        If no tab exists with the given ID.
   * @abstract
   */
  get(_tabId) {
    throw new Error("Not implemented");
  }

  /**
   * Returns a new TabBase instance wrapping the given native tab.
   *
   * @param {NativeTab} _nativeTab
   *        The native tab for which to return a wrapper.
   *
   * @returns {TabBase}
   * @protected
   * @abstract
   */
  wrapTab(_nativeTab) {
    throw new Error("Not implemented");
  }
}

/**
 * Manages native browser windows and their wrappers for a particular extension.
 *
 * @param {Extension} extension
 *        The extension for which to manage windows.
 */
class WindowManagerBase {
  constructor(extension) {
    this.extension = extension;

    this._windows = new DefaultWeakMap(window => this.wrapWindow(window));
  }

  /**
   * Converts the given browser window to a JSON-compatible object, in the
   * format required to be returned by WebExtension APIs, which may be safely
   * passed to extension code.
   *
   * @param {DOMWindow} window
   *        The browser window to convert.
   * @param {*} args
   *        Additional arguments to be passed to {@link WindowBase#convert}.
   *
   * @returns {object}
   */
  convert(window, ...args) {
    return this.getWrapper(window).convert(...args);
  }

  /**
   * Returns this extension's WindowBase wrapper for the given browser window.
   * This method will always return the same wrapper object for any given
   * browser window.
   *
   * @param {DOMWindow} window
   *        The browser window for which to return a wrapper.
   *
   * @returns {WindowBase|undefined}
   *        The wrapper for this tab.
   */
  getWrapper(window) {
    if (this.extension.canAccessWindow(window)) {
      return this._windows.get(window);
    }
  }

  /**
   * Returns whether this window can be accessed by the extension in the given
   * context.
   *
   * @param {DOMWindow} window
   *        The browser window that is being tested
   * @param {BaseContext|null} context
   *        The extension context for which this test is being performed.
   * @returns {boolean}
   */
  canAccessWindow(window, context) {
    return (
      (context && context.canAccessWindow(window)) ||
      this.extension.canAccessWindow(window)
    );
  }

  // The JSDoc validator does not support @returns tags in abstract functions or
  // star functions without return statements.
  /* eslint-disable valid-jsdoc */
  /**
   * Returns an iterator of WindowBase objects which match the given query info.
   *
   * @param {object | null} [queryInfo = null]
   *        An object containing properties on which to filter. May contain any
   *        properties which are recognized by {@link WindowBase#matches}.
   *        Unknown properties will be ignored.
   * @param {BaseContext|null} [context = null]
   *        The extension context for which the matching is being performed.
   *        Used to determine the current window for relevant properties.
   *
   * @returns {Iterator<WindowBase>}
   */
  *query(queryInfo = null, context = null) {
    function* candidates(windowManager) {
      if (queryInfo) {
        let { currentWindow, windowId, lastFocusedWindow } = queryInfo;
        if (currentWindow === true && windowId == null) {
          windowId = WINDOW_ID_CURRENT;
        }
        if (windowId != null) {
          let window = global.windowTracker.getWindow(windowId, context, false);
          if (window) {
            yield windowManager.getWrapper(window);
          }
          return;
        }
        if (lastFocusedWindow === true) {
          let window = global.windowTracker.getTopWindow(context);
          if (window) {
            yield windowManager.getWrapper(window);
          }
          return;
        }
      }
      yield* windowManager.getAll(context);
    }
    for (let windowWrapper of candidates(this)) {
      if (!queryInfo || windowWrapper.matches(queryInfo, context)) {
        yield windowWrapper;
      }
    }
  }

  /**
   * Returns a WindowBase wrapper for the browser window with the given ID.
   *
   * @param {integer} _windowId
   *        The ID of the browser window for which to return a wrapper.
   * @param {BaseContext} _context
   *        The extension context for which the matching is being performed.
   *        Used to determine the current window for relevant properties.
   *
   * @returns {WindowBase}
   * @throws {ExtensionError}
   *        If no window exists with the given ID.
   * @abstract
   */
  get(_windowId, _context) {
    throw new Error("Not implemented");
  }

  /**
   * Returns an iterator of WindowBase wrappers for each currently existing
   * browser window.
   *
   * @returns {Iterator<WindowBase>}
   * @abstract
   */
  getAll() {
    throw new Error("Not implemented");
  }

  /**
   * Returns a new WindowBase instance wrapping the given browser window.
   *
   * @param {DOMWindow} _window
   *        The browser window for which to return a wrapper.
   *
   * @returns {WindowBase}
   * @protected
   * @abstract
   */
  wrapWindow(_window) {
    throw new Error("Not implemented");
  }
  /* eslint-enable valid-jsdoc */
}

function getUserContextIdForCookieStoreId(
  extension,
  cookieStoreId,
  isPrivateBrowsing
) {
  if (!extension.hasPermission("cookies")) {
    throw new ExtensionError(
      `No permission for cookieStoreId: ${cookieStoreId}`
    );
  }

  if (!isValidCookieStoreId(cookieStoreId)) {
    throw new ExtensionError(`Illegal cookieStoreId: ${cookieStoreId}`);
  }

  if (isPrivateBrowsing && !isPrivateCookieStoreId(cookieStoreId)) {
    throw new ExtensionError(
      `Illegal to set non-private cookieStoreId in a private window`
    );
  }

  if (!isPrivateBrowsing && isPrivateCookieStoreId(cookieStoreId)) {
    throw new ExtensionError(
      `Illegal to set private cookieStoreId in a non-private window`
    );
  }

  if (isContainerCookieStoreId(cookieStoreId)) {
    if (PrivateBrowsingUtils.permanentPrivateBrowsing) {
      // Container tabs are not supported in perma-private browsing mode - bug 1320757
      throw new ExtensionError(
        `Contextual identities are unavailable in permanent private browsing mode`
      );
    }
    if (!containersEnabled) {
      throw new ExtensionError(`Contextual identities are currently disabled`);
    }
    let userContextId = getContainerForCookieStoreId(cookieStoreId);
    if (!userContextId) {
      throw new ExtensionError(
        `No cookie store exists with ID ${cookieStoreId}`
      );
    }
    if (!extension.canAccessContainer(userContextId)) {
      throw new ExtensionError(`Cannot access ${cookieStoreId}`);
    }
    return userContextId;
  }

  return Services.scriptSecurityManager.DEFAULT_USER_CONTEXT_ID;
}

Object.assign(global, {
  TabTrackerBase,
  TabManagerBase,
  TabBase,
  WindowTrackerBase,
  WindowManagerBase,
  WindowBase,
  getUserContextIdForCookieStoreId,
});
