/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* eslint-disable mozilla/valid-lazy */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ExtensionProcessScript:
    "resource://gre/modules/ExtensionProcessScript.sys.mjs",
  ExtensionTelemetry: "resource://gre/modules/ExtensionTelemetry.sys.mjs",
  ExtensionUserScriptsContent:
    "resource://gre/modules/ExtensionUserScriptsContent.sys.mjs",
  LanguageDetector:
    "resource://gre/modules/translations/LanguageDetector.sys.mjs",
  Schemas: "resource://gre/modules/Schemas.sys.mjs",
  WebNavigationFrames: "resource://gre/modules/WebNavigationFrames.sys.mjs",

  styleSheetService: {
    service: "@mozilla.org/content/style-sheet-service;1",
    iid: Ci.nsIStyleSheetService,
  },
  isContentScriptProcess: () =>
    Services.appinfo.processType === Services.appinfo.PROCESS_TYPE_CONTENT ||
    !WebExtensionPolicy.useRemoteWebExtensions ||
    // Thunderbird still loads some content in the parent process.
    AppConstants.MOZ_APP_NAME == "thunderbird",
  orderedContentScripts: {
    pref: "extensions.webextensions.content_scripts.ordered",
    default: true,
  },
});

const Timer = Components.Constructor(
  "@mozilla.org/timer;1",
  "nsITimer",
  "initWithCallback"
);

const ScriptError = Components.Constructor(
  "@mozilla.org/scripterror;1",
  "nsIScriptError",
  "initWithWindowID"
);

import {
  ChildAPIManager,
  ExtensionChild,
  ExtensionActivityLogChild,
  Messenger,
} from "resource://gre/modules/ExtensionChild.sys.mjs";
import { ExtensionCommon } from "resource://gre/modules/ExtensionCommon.sys.mjs";
import { ExtensionUtils } from "resource://gre/modules/ExtensionUtils.sys.mjs";

const {
  DefaultMap,
  DefaultWeakMap,
  getInnerWindowID,
  promiseDocumentIdle,
  promiseDocumentLoaded,
  promiseDocumentReady,
} = ExtensionUtils;

const {
  BaseContext,
  CanOfAPIs,
  SchemaAPIManager,
  defineLazyGetter,
  redefineGetter,
  runSafeSyncWithoutClone,
} = ExtensionCommon;

var DocumentManager;

const CATEGORY_EXTENSION_SCRIPTS_CONTENT = "webextension-scripts-content";

var apiManager = new (class extends SchemaAPIManager {
  constructor() {
    super("content", lazy.Schemas);
    this.initialized = false;
  }

  lazyInit() {
    if (!this.initialized) {
      this.initialized = true;
      this.initGlobal();
      for (let { value } of Services.catMan.enumerateCategory(
        CATEGORY_EXTENSION_SCRIPTS_CONTENT
      )) {
        this.loadScript(value);
      }
    }
  }
})();

const SCRIPT_EXPIRY_TIMEOUT_MS = 5 * 60 * 1000;
const SCRIPT_CLEAR_TIMEOUT_MS = 5 * 1000;

const CSS_EXPIRY_TIMEOUT_MS = 30 * 60 * 1000;
const CSSCODE_EXPIRY_TIMEOUT_MS = 10 * 60 * 1000;

const scriptCaches = new WeakSet();
const sheetCacheDocuments = new DefaultWeakMap(() => new WeakSet());

class CacheMap extends DefaultMap {
  constructor(timeout, getter, extension) {
    super(getter);

    this.expiryTimeout = timeout;

    // DocumentManager clears scriptCaches early under memory pressure. For
    // this to work, DocumentManager.lazyInit() should be called. In practice,
    // ScriptCache/CSSCache/CSSCodeCache are only instantiated and populated
    // when a content script/style is to be injected. This always depends on a
    // ContentScriptContextChild instance, which is always paired with a call
    // to DocumentManager.lazyInit().
    scriptCaches.add(this);

    // This ensures that all the cached scripts and stylesheets are deleted
    // from the cache and the xpi is no longer actively used.
    // See Bug 1435100 for rationale.
    extension.once("shutdown", () => {
      this.clear(-1);
    });
  }

  get(url) {
    let promise = super.get(url);

    promise.lastUsed = Date.now();
    if (promise.timer) {
      promise.timer.cancel();
    }
    promise.timer = Timer(
      this.delete.bind(this, url),
      this.expiryTimeout,
      Ci.nsITimer.TYPE_ONE_SHOT
    );

    return promise;
  }

  delete(url) {
    if (this.has(url)) {
      super.get(url).timer.cancel();
    }

    return super.delete(url);
  }

  clear(timeout = SCRIPT_CLEAR_TIMEOUT_MS) {
    let now = Date.now();
    for (let [url, promise] of this.entries()) {
      // Delete the entry if expired or if clear has been called with timeout -1
      // (which is used to force the cache to clear all the entries, e.g. when the
      // extension is shutting down).
      if (timeout === -1 || now - promise.lastUsed >= timeout) {
        this.delete(url);
      }
    }
  }
}

class ScriptCache extends CacheMap {
  constructor(options, extension) {
    super(
      SCRIPT_EXPIRY_TIMEOUT_MS,
      url => {
        /** @type {Promise<PrecompiledScript> & { script?: PrecompiledScript }} */
        let promise = ChromeUtils.compileScript(url, options);
        promise.then(script => {
          promise.script = script;
        });
        return promise;
      },
      extension
    );
  }
}

/**
 * Shared base class for the two specialized CSS caches:
 * CSSCache (for the "url"-based stylesheets) and CSSCodeCache
 * (for the stylesheet defined by plain CSS content as a string).
 */
class BaseCSSCache extends CacheMap {
  constructor(expiryTimeout, defaultConstructor, extension) {
    super(expiryTimeout, defaultConstructor, extension);
  }

  delete(key) {
    if (this.has(key)) {
      let sheetPromise = this.get(key);

      // Never remove a sheet from the cache if it's still being used by a
      // document. Rule processors can be shared between documents with the
      // same preloaded sheet, so we only lose by removing them while they're
      // still in use.
      let docs = ChromeUtils.nondeterministicGetWeakSetKeys(
        sheetCacheDocuments.get(sheetPromise)
      );
      if (docs.length) {
        return;
      }
    }

    return super.delete(key);
  }
}

/**
 * Cache of the preloaded stylesheet defined by url.
 */
class CSSCache extends BaseCSSCache {
  constructor(sheetType, extension) {
    super(
      CSS_EXPIRY_TIMEOUT_MS,
      url => {
        let uri = Services.io.newURI(url);
        const sheetPromise = lazy.styleSheetService.preloadSheetAsync(
          uri,
          sheetType
        );
        sheetPromise.then(sheet => {
          sheetPromise.sheet = sheet;
        });
        return sheetPromise;
      },
      extension
    );
  }
}

/**
 * Cache of the preloaded stylesheet defined by plain CSS content as a string,
 * the key of the cached stylesheet is the hash of its "CSSCode" string.
 */
class CSSCodeCache extends BaseCSSCache {
  constructor(sheetType, extension) {
    super(
      CSSCODE_EXPIRY_TIMEOUT_MS,
      hash => {
        if (!this.has(hash)) {
          // Do not allow the getter to be used to lazily create the cached stylesheet,
          // the cached CSSCode stylesheet has to be explicitly set.
          throw new Error(
            "Unexistent cached cssCode stylesheet: " + Error().stack
          );
        }

        return super.get(hash);
      },
      extension
    );

    // Store the preferred sheetType (used to preload the expected stylesheet type in
    // the addCSSCode method).
    this.sheetType = sheetType;
  }

  addCSSCode(hash, cssCode) {
    if (this.has(hash)) {
      // This cssCode have been already cached, no need to create it again.
      return;
    }
    // The `webext=style` portion is added metadata to help us distinguish
    // different kinds of data URL loads that are triggered with the
    // SystemPrincipal. It shall be removed with bug 1699425.
    const uri = Services.io.newURI(
      "data:text/css;extension=style;charset=utf-8," +
        encodeURIComponent(cssCode)
    );
    const sheetPromise = lazy.styleSheetService.preloadSheetAsync(
      uri,
      this.sheetType
    );
    sheetPromise.then(sheet => {
      sheetPromise.sheet = sheet;
    });
    // styleURI: windowUtils.removeSheet requires a URI to identify the sheet.
    sheetPromise.styleURI = uri;

    super.set(hash, sheetPromise);
  }
}

defineLazyGetter(ExtensionChild.prototype, "staticScripts", function () {
  return new ScriptCache({ hasReturnValue: false }, this);
});

defineLazyGetter(ExtensionChild.prototype, "dynamicScripts", function () {
  return new ScriptCache({ hasReturnValue: true }, this);
});

defineLazyGetter(ExtensionChild.prototype, "anonStaticScripts", function () {
  // TODO bug 1651557: Use dynamic name to improve debugger experience.
  const filename = "<anonymous code>";
  return new ScriptCache({ filename, hasReturnValue: false }, this);
});

defineLazyGetter(ExtensionChild.prototype, "anonDynamicScripts", function () {
  // TODO bug 1651557: Use dynamic name to improve debugger experience.
  const filename = "<anonymous code>";
  return new ScriptCache({ filename, hasReturnValue: true }, this);
});

defineLazyGetter(ExtensionChild.prototype, "userCSS", function () {
  return new CSSCache(Ci.nsIStyleSheetService.USER_SHEET, this);
});

defineLazyGetter(ExtensionChild.prototype, "authorCSS", function () {
  return new CSSCache(Ci.nsIStyleSheetService.AUTHOR_SHEET, this);
});

// These two caches are similar to the above but specialized to cache the cssCode
// using an hash computed from the cssCode string as the key (instead of the generated data
// URI which can be pretty long for bigger injected cssCode).
defineLazyGetter(ExtensionChild.prototype, "userCSSCode", function () {
  return new CSSCodeCache(Ci.nsIStyleSheetService.USER_SHEET, this);
});

defineLazyGetter(ExtensionChild.prototype, "authorCSSCode", function () {
  return new CSSCodeCache(Ci.nsIStyleSheetService.AUTHOR_SHEET, this);
});

/**
 * This is still an ExtensionChild, but with the properties added above.
 * Unfortunately we can't express that using just JSDocs types locally,
 * so this needs to be used with `& ExtensionChild` explicitly below.
 *
 * @typedef {object} ExtensionChildContent
 * @property {ScriptCache} staticScripts
 * @property {ScriptCache} dynamicScripts
 * @property {ScriptCache} anonStaticScripts
 * @property {ScriptCache} anonDynamicScripts
 * @property {CSSCache} userCSS
 * @property {CSSCache} authorCSS
 * @property {CSSCodeCache} userCSSCode
 * @property {CSSCodeCache} authorCSSCode
 */

/**
 * Script/style injections depend on compiled scripts/styles. If the previously
 * compiled script or style is not found, we block that and later script/style
 * executions until compilation finishes. This is achieved by storing a Promise
 * for that compilation in this gPendingScriptBlockers, for a given context.
 *
 * @type {WeakMap<ContentScriptContextChild, Promise>}
 */
const gPendingScriptBlockers = new WeakMap();

// Represents a content script.
class Script {
  /**
   * @param {ExtensionChild & ExtensionChildContent} extension
   * @param {WebExtensionContentScript|object} matcher
   *        An object with a "matchesWindowGlobal" method and content script
   *        execution details. This is usually a plain WebExtensionContentScript
   *        except when the script is run via `tabs.executeScript` or
   *        `scripting.executeScript`. In this case, the object may have some
   *        extra properties: wantReturnValue, removeCSS, cssOrigin
   */
  constructor(extension, matcher) {
    this.scriptType = "content_script";
    this.extension = extension;
    this.matcher = matcher;

    this.runAt = this.matcher.runAt;
    this.world = this.matcher.world;
    this.js = this.matcher.jsPaths;
    this.jsCode = null; // tabs/scripting.executeScript + ISOLATED world.
    this.jsCodeCompiledScript = null; // scripting.executeScript + MAIN world.
    this.css = this.matcher.cssPaths.slice();
    this.cssCodeHash = null;

    this.removeCSS = this.matcher.removeCSS;
    this.cssOrigin = this.matcher.cssOrigin;

    this.cssCache =
      extension[this.cssOrigin === "user" ? "userCSS" : "authorCSS"];
    this.cssCodeCache =
      extension[this.cssOrigin === "user" ? "userCSSCode" : "authorCSSCode"];
    if (this.world === "MAIN") {
      this.scriptCache = matcher.wantReturnValue
        ? extension.anonDynamicScripts
        : extension.anonStaticScripts;
    } else {
      this.scriptCache = matcher.wantReturnValue
        ? extension.dynamicScripts
        : extension.staticScripts;
    }

    /** @type {WeakSet<Document>} A set of documents injected into. */
    this.injectedInto = new WeakSet();

    if (matcher.wantReturnValue) {
      this.compileScripts();
      this.loadCSS();
    }
  }

  get requiresCleanup() {
    return !this.removeCSS && (!!this.css.length || this.cssCodeHash);
  }

  async addCSSCode(cssCode) {
    if (!cssCode) {
      return;
    }

    // Store the hash of the cssCode.
    const buffer = await crypto.subtle.digest(
      "SHA-1",
      new TextEncoder().encode(cssCode)
    );
    this.cssCodeHash = String.fromCharCode(...new Uint16Array(buffer));

    // Cache and preload the cssCode stylesheet.
    this.cssCodeCache.addCSSCode(this.cssCodeHash, cssCode);
  }

  addJSCode(jsCode) {
    if (!jsCode) {
      return;
    }
    if (this.world === "MAIN") {
      // To support the scripting.executeScript API, we would like to execute a
      // string in the context of the web page in #injectIntoMainWorld().
      // To do so without being blocked by the web page's CSP, we convert
      // jsCode to a PrecompiledScript, which is then executed by the logic
      // that is usually used for file-based execution.
      const dataUrl = `data:text/javascript,${encodeURIComponent(jsCode)}`;
      const options = {
        hasReturnValue: this.matcher.wantReturnValue,
        // Redact the file name to hide actual script content from web pages.
        // TODO bug 1651557: Use dynamic name to improve debugger experience.
        filename: "<anonymous code>",
      };
      // Note: this logic is similar to this.scriptCaches.get(...), but we are
      // not using scriptCaches because we don't want the URL to be cached.
      /** @type {Promise<PrecompiledScript> & {script?: PrecompiledScript}} */
      let promised = ChromeUtils.compileScript(dataUrl, options);
      promised.then(script => {
        promised.script = script;
      });
      this.jsCodeCompiledScript = promised;
    } else {
      // this.world === "ISOLATED".
      this.jsCode = jsCode;
    }
  }

  compileScripts() {
    return this.js.map(url => this.scriptCache.get(url));
  }

  loadCSS() {
    return this.css.map(url => this.cssCache.get(url));
  }

  preload() {
    this.loadCSS();
    this.compileScripts();
  }

  cleanup(window) {
    if (this.requiresCleanup) {
      if (window) {
        this.removeStyleSheets(window);
      }

      // Clear any sheets that were kept alive past their timeout as
      // a result of living in this document.
      this.cssCodeCache.clear(CSSCODE_EXPIRY_TIMEOUT_MS);
      this.cssCache.clear(CSS_EXPIRY_TIMEOUT_MS);
    }
  }

  matchesWindowGlobal(windowGlobal, ignorePermissions) {
    return this.matcher.matchesWindowGlobal(windowGlobal, ignorePermissions);
  }

  async injectInto(window, reportExceptions = true) {
    if (
      !lazy.isContentScriptProcess ||
      this.injectedInto.has(window.document)
    ) {
      return;
    }
    this.injectedInto.add(window.document);

    let context = this.extension.getContext(window);
    for (let script of this.matcher.jsPaths) {
      context.logActivity(this.scriptType, script, {
        url: window.location.href,
      });
    }

    try {
      // In case of initial about:blank documents, inject immediately without
      // awaiting the runAt logic in the blocks below, to avoid getting stuck
      // due to https://bugzilla.mozilla.org/show_bug.cgi?id=1900222#c7
      // This is only relevant for dynamic code execution because declarative
      // content scripts do not run on initial about:blank - bug 1415539).
      if (!window.document.isInitialDocument) {
        if (this.runAt === "document_end") {
          await promiseDocumentReady(window.document);
        } else if (this.runAt === "document_idle") {
          await Promise.race([
            promiseDocumentIdle(window),
            promiseDocumentLoaded(window.document),
          ]);
        }
      }

      return this.inject(context, reportExceptions);
    } catch (e) {
      return Promise.reject(context.normalizeError(e));
    }
  }

  /**
   * Tries to inject this script into the given window and sandbox, if
   * there are pending operations for the window's current load state.
   *
   * @param {ContentScriptContextChild} context
   *        The content script context into which to inject the scripts.
   * @param {boolean} reportExceptions
   *        Defaults to true and reports any exception directly to the console
   *        and no exception will be thrown out of this function.
   * @returns {Promise<any>}
   *        Resolves to the last value in the evaluated script, when
   *        execution is complete.
   */
  async inject(context, reportExceptions = true) {
    // NOTE: Avoid unnecessary use of "await" in this function, because doing
    // so can delay script execution beyond the scheduled point. In particular,
    // document_start scripts should run "immediately" in most cases.

    if (this.requiresCleanup) {
      context.addScript(this);
    }

    // To avoid another await (which affects timing) or .then() chaining
    // (which would create a new Promise that could duplicate a rejection),
    // we store the index where we expect the result of a Promise.all() call.
    let scriptsIndex, sheetsIndex;
    let sheets = this.getCompiledStyleSheets(context.contentWindow);
    let scripts = this.getCompiledScripts(context);

    let executionBlockingPromises = [];
    if (gPendingScriptBlockers.has(context) && lazy.orderedContentScripts) {
      executionBlockingPromises.push(gPendingScriptBlockers.get(context));
    }
    if (scripts instanceof Promise) {
      scriptsIndex = executionBlockingPromises.length;
      executionBlockingPromises.push(scripts);
    }
    if (sheets instanceof Promise) {
      sheetsIndex = executionBlockingPromises.length;
      executionBlockingPromises.push(sheets);
    }

    if (executionBlockingPromises.length) {
      let promise = Promise.all(executionBlockingPromises);

      // If we're supposed to inject at the start of the document load,
      // and we haven't already missed that point, block further parsing
      // until the scripts/styles have been loaded.
      // This maximizes the chance of content scripts executing before other
      // scripts in the web page.
      //
      // Blocking the full parser is overkill if we are only awaiting style
      // compilation, since we only need to block the parts that are dependent
      // on CSS (layout, onload event, CSSOM, etc). But we have an API to do
      // the former and not atter, so we do it that way. This hopefully isn't a
      // performance problem since there are no network loads involved, and
      // since we cache the stylesheets on first load. We should fix this up if
      // it does becomes a problem.
      const { document } = context.contentWindow;
      if (
        this.runAt === "document_start" &&
        document.readyState !== "complete"
      ) {
        document.blockParsing(promise, { blockScriptCreated: false });
      }

      // Store a promise that never rejects, so that failure to compile scripts
      // or styles here does not prevent the scheduling of others.
      let promiseSettled = promise.then(
        () => {},
        () => {}
      );
      gPendingScriptBlockers.set(context, promiseSettled);

      // Note: in theory, the following async await could result in script
      // execution being scheduled too late. That would be an issue for
      // document_start scripts. In practice, this is not a problem because the
      // compiled script is cached in the process, and preloading to compile
      // starts as soon as the network request for the document has been
      // received (see ExtensionPolicyService::CheckRequest).
      //
      // We use blockParsing() for document_start scripts (and styles) to
      // ensure that the DOM remains blocked when scripts are still compiling.
      try {
        // NOTE: This is the ONLY await in this injectInto function!
        const compiledResults = await promise;
        if (sheetsIndex !== undefined) {
          sheets = compiledResults[sheetsIndex];
        }
        if (scriptsIndex !== undefined) {
          scripts = compiledResults[scriptsIndex];
        }
      } finally {
        // gPendingScriptBlockers may be overwritten by another inject() call,
        // so check that this is the latest inject() attempt before clearing.
        if (gPendingScriptBlockers.get(context) === promiseSettled) {
          gPendingScriptBlockers.delete(context);
        }
      }
    }

    let window = context.contentWindow;
    if (!window) {
      // context unloaded or went into bfcache before compilation completed.
      return;
    }

    if (this.css.length || this.cssCodeHash) {
      if (this.removeCSS) {
        this.removeStyleSheets(window);
        // The tabs.removeCSS and scripting.removeCSS are never combined with
        // script execution, so we can now return early.
        return;
      }
      // Make sure we've injected any related CSS before we run content scripts.
      let { windowUtils } = window;
      let type =
        this.cssOrigin === "user"
          ? windowUtils.USER_SHEET
          : windowUtils.AUTHOR_SHEET;
      for (const sheet of sheets) {
        runSafeSyncWithoutClone(windowUtils.addSheet, sheet, type);
      }
    }

    const { extension } = context;

    // The evaluations below may throw, in which case the promise will be
    // automatically rejected.
    lazy.ExtensionTelemetry.contentScriptInjection.stopwatchStart(
      extension,
      context
    );
    try {
      if (this.world === "MAIN") {
        return this.#injectIntoMainWorld(context, scripts, reportExceptions);
      }
      if (this.world === "USER_SCRIPT") {
        return this.#injectIntoUserScriptWorld(
          context,
          scripts,
          reportExceptions
        );
      }
      return this.#injectIntoIsolatedWorld(context, scripts, reportExceptions);
    } finally {
      lazy.ExtensionTelemetry.contentScriptInjection.stopwatchFinish(
        extension,
        context
      );
    }
  }

  #injectIntoIsolatedWorld(context, scripts, reportExceptions) {
    let result;

    // Note: every script execution can potentially destroy the context, in
    // which case context.cloneScope becomes null (bug 1403505).
    for (let script of scripts) {
      result = script.executeInGlobal(context.cloneScope, { reportExceptions });
    }

    if (this.jsCode) {
      result = Cu.evalInSandbox(
        this.jsCode,
        context.cloneScope,
        "latest",
        // TODO bug 1651557: Use dynamic name to improve debugger experience.
        "sandbox eval code",
        1
      );
    }

    return result;
  }

  #injectIntoUserScriptWorld(context, scripts, reportExceptions) {
    let worldId = this.matcher.worldId;
    let sandbox = lazy.ExtensionUserScriptsContent.sandboxFor(context, worldId);

    let result;
    // Note: every script execution can potentially destroy the context or
    // navigate the window, in which case context.active will be false.
    for (let script of scripts) {
      if (!context.active) {
        // Return instead of throw, to avoid logspam like bug 1403505.
        return;
      }
      result = script.executeInGlobal(sandbox, { reportExceptions });
    }

    // NOTE: if userScripts.execute() is implemented (bug 1930776), we may have
    // to account for this.jsCode here (via addJSCode).

    return result;
  }

  #injectIntoMainWorld(context, scripts, reportExceptions) {
    let result;

    // Note: every script execution can potentially destroy the context or
    // navigate the window, in which case context.contentWindow will be null,
    // which would cause an error to be thrown (bug 1403505).
    for (let script of scripts) {
      result = script.executeInGlobal(context.contentWindow, {
        reportExceptions,
      });
    }

    // Note: string-based code execution (=our implementation of func+args in
    // scripting.executeScript) is not handled here, because we compile it in
    // addJSCode() and include it in the scripts array via getCompiledScripts().
    // We cannot use context.contentWindow.eval() here because the web page's
    // CSP may block it.

    return result;
  }

  /**
   * Get the compiled scripts (if they are already precompiled and cached) or a
   * promise which resolves to the precompiled scripts (once they have been
   * compiled and cached).
   *
   * @param {ContentScriptContextChild} context
   *        The context where the caller intends to run the compiled script.
   *
   * @returns {PrecompiledScript[] | Promise<PrecompiledScript[]>}
   */
  getCompiledScripts(context) {
    let scriptPromises = this.compileScripts();
    if (this.jsCodeCompiledScript) {
      scriptPromises.push(this.jsCodeCompiledScript);
    }
    let scripts = scriptPromises.map(promise => promise.script);

    // If not all scripts are already available in the cache, block
    // parsing and wait all promises to resolve.
    if (!scripts.every(script => script)) {
      let promise = Promise.all(scriptPromises);

      // If there is any syntax error, the script promises will be rejected.
      //
      // Notify the exception directly to the console so that it can
      // be displayed in the web console by flagging the error with the right
      // innerWindowID.
      for (const p of scriptPromises) {
        p.catch(error => {
          Services.console.logMessage(
            new ScriptError(
              error.toString(),
              error.fileName,
              error.lineNumber,
              error.columnNumber,
              Ci.nsIScriptError.errorFlag,
              "content javascript",
              context.innerWindowID
            )
          );
        });
      }

      return promise;
    }

    return scripts;
  }

  getCompiledStyleSheets(window) {
    const sheetPromises = this.loadCSS();
    if (this.cssCodeHash) {
      sheetPromises.push(this.cssCodeCache.get(this.cssCodeHash));
    }
    if (window) {
      for (const sheetPromise of sheetPromises) {
        sheetCacheDocuments.get(sheetPromise).add(window.document);
      }
    }

    let sheets = sheetPromises.map(sheetPromise => sheetPromise.sheet);
    if (!sheets.every(sheet => sheet)) {
      return Promise.all(sheetPromises);
    }
    return sheets;
  }

  removeStyleSheets(window) {
    let { windowUtils } = window;

    let type =
      this.cssOrigin === "user"
        ? windowUtils.USER_SHEET
        : windowUtils.AUTHOR_SHEET;

    for (let url of this.css) {
      if (this.cssCache.has(url)) {
        const sheetPromise = this.cssCache.get(url);
        sheetCacheDocuments.get(sheetPromise).delete(window.document);
      }

      if (!window.closed) {
        runSafeSyncWithoutClone(
          windowUtils.removeSheetUsingURIString,
          url,
          type
        );
      }
    }

    const { cssCodeHash } = this;

    if (cssCodeHash && this.cssCodeCache.has(cssCodeHash)) {
      const sheetPromise = this.cssCodeCache.get(cssCodeHash);
      sheetCacheDocuments.get(sheetPromise).delete(window.document);
      if (sheetPromise.sheet && !window.closed) {
        runSafeSyncWithoutClone(
          windowUtils.removeSheet,
          sheetPromise.styleURI,
          type
        );
      }
    }
  }
}

// Represents a user script.
class UserScript extends Script {
  /**
   * @param {ExtensionChild & ExtensionChildContent} extension
   * @param {WebExtensionContentScript|object} matcher
   *        An object with a "matchesWindowGlobal" method and content script
   *        execution details.
   */
  constructor(extension, matcher) {
    super(extension, matcher);
    this.scriptType = "user_script";

    // This is an opaque object that the extension provides, it is associated to
    // the particular userScript and it is passed as a parameter to the custom
    // userScripts APIs defined by the extension.
    this.scriptMetadata = matcher.userScriptOptions.scriptMetadata;
    this.apiScriptURL =
      extension.manifest.user_scripts &&
      extension.manifest.user_scripts.api_script;

    // Add the apiScript to the js scripts to compile.
    if (this.apiScriptURL) {
      this.js = [this.apiScriptURL].concat(this.js);
    }

    // WeakMap<ContentScriptContextChild, Sandbox>
    this.sandboxes = new DefaultWeakMap(context => {
      return this.createSandbox(context);
    });
  }

  async inject(context) {
    let scripts = this.getCompiledScripts(context);
    if (scripts instanceof Promise) {
      // If we're supposed to inject at the start of the document load,
      // and we haven't already missed that point, block further parsing
      // until the scripts have been loaded.
      const { document } = context.contentWindow;
      if (
        this.runAt === "document_start" &&
        document.readyState !== "complete"
      ) {
        document.blockParsing(scripts, { blockScriptCreated: false });
      }
      scripts = await scripts;
    }
    // NOTE: Other than "await scripts" above, there is no other "await" before
    // execution. This ensures that document_start scripts execute immediately.

    let apiScript, sandboxScripts;

    if (this.apiScriptURL) {
      [apiScript, ...sandboxScripts] = scripts;
    } else {
      sandboxScripts = scripts;
    }

    // Load and execute the API script once per context.
    if (apiScript) {
      context.executeAPIScript(apiScript);
    }

    let userScriptSandbox = this.sandboxes.get(context);

    context.callOnClose({
      close: () => {
        // Destroy the userScript sandbox when the related ContentScriptContextChild instance
        // is being closed.
        this.sandboxes.delete(context);
        Cu.nukeSandbox(userScriptSandbox);
      },
    });

    // Notify listeners subscribed to the userScripts.onBeforeScript API event,
    // to allow extension API script to provide its custom APIs to the userScript.
    if (apiScript) {
      context.userScriptsEvents.emit(
        "on-before-script",
        this.scriptMetadata,
        userScriptSandbox
      );
    }

    for (let script of sandboxScripts) {
      script.executeInGlobal(userScriptSandbox);
    }
  }

  createSandbox(context) {
    const { contentWindow } = context;
    const contentPrincipal = contentWindow.document.nodePrincipal;
    const ssm = Services.scriptSecurityManager;

    let principal;
    if (contentPrincipal.isSystemPrincipal) {
      principal = ssm.createNullPrincipal(contentPrincipal.originAttributes);
    } else {
      principal = [contentPrincipal];
    }

    const sandbox = Cu.Sandbox(principal, {
      sandboxName: `User Script registered by ${this.extension.policy.debugName}`,
      sandboxPrototype: contentWindow,
      sameZoneAs: contentWindow,
      wantXrays: true,
      wantGlobalProperties: ["XMLHttpRequest", "fetch", "WebSocket"],
      originAttributes: contentPrincipal.originAttributes,
      metadata: {
        "browser-id": context.browserId,
        "inner-window-id": context.innerWindowID,
        addonId: this.extension.policy.id,
      },
    });

    return sandbox;
  }
}

var contentScripts = new DefaultWeakMap(matcher => {
  const extension = lazy.ExtensionProcessScript.extensions.get(
    matcher.extension
  );

  if ("userScriptOptions" in matcher) {
    return new UserScript(extension, matcher);
  }

  return new Script(extension, matcher);
});

/**
 * An execution context for semi-privileged extension content scripts.
 *
 * This is the child side of the ContentScriptContextParent class
 * defined in ExtensionParent.sys.mjs.
 */
export class ContentScriptContextChild extends BaseContext {
  constructor(extension, contentWindow) {
    super("content_child", extension);

    this.setContentWindow(contentWindow);

    let frameId = lazy.WebNavigationFrames.getFrameId(contentWindow);
    this.frameId = frameId;

    this.browsingContextId = contentWindow.docShell.browsingContext.id;

    this.scripts = [];

    let contentPrincipal = contentWindow.document.nodePrincipal;
    let ssm = Services.scriptSecurityManager;

    // Copy origin attributes from the content window origin attributes to
    // preserve the user context id.
    let attrs = contentPrincipal.originAttributes;
    let extensionPrincipal = ssm.createContentPrincipal(
      this.extension.baseURI,
      attrs
    );

    this.isExtensionPage = contentPrincipal.equals(extensionPrincipal);

    if (this.isExtensionPage) {
      // This is an iframe with content script API enabled and its principal
      // should be the contentWindow itself. We create a sandbox with the
      // contentWindow as principal and with X-rays disabled because it
      // enables us to create the APIs object in this sandbox object and then
      // copying it into the iframe's window.  See bug 1214658.
      this.sandbox = Cu.Sandbox(contentWindow, {
        sandboxName: `Web-Accessible Extension Page ${extension.policy.debugName}`,
        sandboxPrototype: contentWindow,
        sameZoneAs: contentWindow,
        wantXrays: false,
        isWebExtensionContentScript: true,
      });
    } else {
      let principal;
      if (contentPrincipal.isSystemPrincipal) {
        // Make sure we don't hand out the system principal by accident.
        // Also make sure that the null principal has the right origin attributes.
        principal = ssm.createNullPrincipal(attrs);
      } else {
        principal = [contentPrincipal, extensionPrincipal];
      }
      // This metadata is required by the Developer Tools, in order for
      // the content script to be associated with both the extension and
      // the tab holding the content page.
      let metadata = {
        "browser-id": this.browserId,
        "inner-window-id": this.innerWindowID,
        addonId: extensionPrincipal.addonId,
      };

      let isMV2 = extension.manifestVersion == 2;
      let wantGlobalProperties;
      let sandboxContentSecurityPolicy;
      if (isMV2) {
        // In MV2, fetch/XHR support cross-origin requests.
        // WebSocket was also included to avoid CSP effects (bug 1676024).
        wantGlobalProperties = ["XMLHttpRequest", "fetch", "WebSocket"];
      } else {
        // In MV3, fetch/XHR have the same capabilities as the web page.
        wantGlobalProperties = [];
        // In MV3, the base CSP is enforced for content scripts. Overrides are
        // currently not supported, but this was considered at some point, see
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1581611#c10
        sandboxContentSecurityPolicy = extension.policy.baseCSP;
      }
      this.sandbox = Cu.Sandbox(principal, {
        metadata,
        sandboxName: `Content Script ${extension.policy.debugName}`,
        sandboxPrototype: contentWindow,
        sandboxContentSecurityPolicy,
        sameZoneAs: contentWindow,
        wantXrays: true,
        isWebExtensionContentScript: true,
        wantExportHelpers: true,
        wantGlobalProperties,
        originAttributes: attrs,
      });

      // Preserve a copy of the original Error and Promise globals from the sandbox object,
      // which are used in the WebExtensions internals (before any content script code had
      // any chance to redefine them).
      this.cloneScopePromise = this.sandbox.Promise;
      this.cloneScopeError = this.sandbox.Error;

      if (isMV2) {
        // Preserve a copy of the original window's XMLHttpRequest and fetch
        // in a content object (fetch is manually binded to the window
        // to prevent it from raising a TypeError because content object is not
        // a real window).
        Cu.evalInSandbox(
          `
          this.content = {
            XMLHttpRequest: window.XMLHttpRequest,
            fetch: window.fetch.bind(window),
            WebSocket: window.WebSocket,
          };

          window.JSON = JSON;
          window.XMLHttpRequest = XMLHttpRequest;
          window.fetch = fetch;
          window.WebSocket = WebSocket;
        `,
          this.sandbox
        );
      } else {
        // The sandbox's JSON API can deal with values from the sandbox and the
        // contentWindow, but window.JSON cannot (and it could potentially be
        // spoofed by the web page). jQuery.parseJSON relies on window.JSON.
        Cu.evalInSandbox("window.JSON = JSON;", this.sandbox);
      }
    }

    Object.defineProperty(this, "principal", {
      value: Cu.getObjectPrincipal(this.sandbox),
      enumerable: true,
      configurable: true,
    });

    this.url = contentWindow.location.href;

    lazy.Schemas.exportLazyGetter(
      this.sandbox,
      "browser",
      () => this.chromeObj
    );
    lazy.Schemas.exportLazyGetter(this.sandbox, "chrome", () => this.chromeObj);

    // Keep track if the userScript API script has been already executed in this context
    // (e.g. because there are more then one UserScripts that match the related webpage
    // and so the UserScript apiScript has already been executed).
    this.hasUserScriptAPIs = false;

    // A lazy created EventEmitter related to userScripts-specific events.
    defineLazyGetter(this, "userScriptsEvents", () => {
      return new ExtensionCommon.EventEmitter();
    });
  }

  injectAPI() {
    if (!this.isExtensionPage) {
      throw new Error("Cannot inject extension API into non-extension window");
    }

    // This is an iframe with content script API enabled (See Bug 1214658)
    lazy.Schemas.exportLazyGetter(
      this.contentWindow,
      "browser",
      () => this.chromeObj
    );
    lazy.Schemas.exportLazyGetter(
      this.contentWindow,
      "chrome",
      () => this.chromeObj
    );
  }

  async logActivity(type, name, data) {
    ExtensionActivityLogChild.log(this, type, name, data);
  }

  get cloneScope() {
    return this.sandbox;
  }

  async executeAPIScript(apiScript) {
    // Execute the UserScript apiScript only once per context (e.g. more then one UserScripts
    // match the same webpage and the apiScript has already been executed).
    if (apiScript && !this.hasUserScriptAPIs) {
      this.hasUserScriptAPIs = true;
      apiScript.executeInGlobal(this.cloneScope);
    }
  }

  addScript(script) {
    if (script.requiresCleanup) {
      this.scripts.push(script);
    }
  }

  close() {
    super.unload();

    // Cleanup the scripts even if the contentWindow have been destroyed.
    for (let script of this.scripts) {
      script.cleanup(this.contentWindow);
    }

    if (this.contentWindow) {
      // Overwrite the content script APIs with an empty object if the APIs objects are still
      // defined in the content window (See Bug 1214658).
      if (this.isExtensionPage) {
        Cu.createObjectIn(this.contentWindow, { defineAs: "browser" });
        Cu.createObjectIn(this.contentWindow, { defineAs: "chrome" });
      }
    }
    Services.obs.notifyObservers(this.sandbox, "content-script-destroyed");
    Cu.nukeSandbox(this.sandbox);

    this.sandbox = null;
  }

  get childManager() {
    apiManager.lazyInit();
    let can = new CanOfAPIs(this, apiManager, {});
    let childManager = new ChildAPIManager(this, this.messageManager, can, {
      envType: "content_parent",
      url: this.url,
    });
    this.callOnClose(childManager);
    return redefineGetter(this, "childManager", childManager);
  }

  get chromeObj() {
    let chromeObj = Cu.createObjectIn(this.sandbox);
    this.childManager.inject(chromeObj);
    return redefineGetter(this, "chromeObj", chromeObj);
  }

  get messenger() {
    return redefineGetter(this, "messenger", new Messenger(this));
  }
}

// Responsible for tracking the lifetime of a document, to manage the lifetime
// of ContentScriptContextChild instances for that document. When a caller
// wants to run extension code in a document (often in a sandbox) and need to
// have that code's lifetime be bound to the document, they call
// ExtensionContent.getContext() (indirectly via ExtensionChild's getContext()).
//
// As part of the initialization of a ContentScriptContextChild, the document's
// lifetime is tracked here, by DocumentManager. This DocumentManager ensures
// that the ContentScriptContextChild and any supporting caches are cleared
// when the document is destroyed.
DocumentManager = {
  /** @type {Map<number, Map<ExtensionChild, ContentScriptContextChild>>} */
  contexts: new Map(),

  initialized: false,

  lazyInit() {
    if (this.initialized) {
      return;
    }
    this.initialized = true;

    Services.obs.addObserver(this, "inner-window-destroyed");
    Services.obs.addObserver(this, "memory-pressure");
  },

  uninit() {
    Services.obs.removeObserver(this, "inner-window-destroyed");
    Services.obs.removeObserver(this, "memory-pressure");
  },

  observers: {
    "inner-window-destroyed"(subject) {
      let windowId = subject.QueryInterface(Ci.nsISupportsPRUint64).data;

      // Close any existent content-script context for the destroyed window.
      if (this.contexts.has(windowId)) {
        let extensions = this.contexts.get(windowId);
        for (let context of extensions.values()) {
          context.close();
        }

        this.contexts.delete(windowId);
      }
    },
    "memory-pressure"(subject, topic, data) {
      let timeout = data === "heap-minimize" ? 0 : undefined;

      for (let cache of ChromeUtils.nondeterministicGetWeakSetKeys(
        scriptCaches
      )) {
        cache.clear(timeout);
      }
    },
  },

  /**
   * @param {object} subject
   * @param {keyof typeof DocumentManager.observers} topic
   * @param {any} data
   */
  observe(subject, topic, data) {
    this.observers[topic].call(this, subject, topic, data);
  },

  shutdownExtension(extension) {
    for (let extensions of this.contexts.values()) {
      let context = extensions.get(extension);
      if (context) {
        context.close();
        extensions.delete(extension);
      }
    }
  },

  getContexts(window) {
    let winId = getInnerWindowID(window);

    let extensions = this.contexts.get(winId);
    if (!extensions) {
      extensions = new Map();
      this.contexts.set(winId, extensions);
      // When ExtensionContent.getContext() calls DocumentManager.getContexts,
      // it is about to create ContentScriptContextChild instances that wraps
      // the document. Call DocumentManager.lazyInit() to ensure that we have
      // the relevant observers to close contexts as needed.
      this.lazyInit();
    }

    return extensions;
  },

  // For test use only.
  getContext(extensionId, window) {
    for (let [extension, context] of this.getContexts(window)) {
      if (extension.id === extensionId) {
        return context;
      }
    }
  },

  getAllContentScriptGlobals() {
    const sandboxes = [];
    for (let extensions of this.contexts.values()) {
      for (let ctx of extensions.values()) {
        sandboxes.push(ctx.sandbox);
      }
    }
    return sandboxes;
  },

  initExtensionContext(extension, window) {
    // Note: getContext() always returns an ContentScriptContextChild instance.
    // This can be a content script, or a sandbox holding the extension APIs
    // for an extension document embedded in a non-extension document.
    extension.getContext(window).injectAPI();
  },
};

export var ExtensionContent = {
  contentScripts,

  shutdownExtension(extension) {
    DocumentManager.shutdownExtension(extension);
  },

  // This helper is exported to be integrated in the devtools RDP actors,
  // that can use it to retrieve all the existent WebExtensions ContentScripts
  // running in the current content process and be able to show the
  // ContentScripts source in the DevTools Debugger panel.
  getAllContentScriptGlobals() {
    return DocumentManager.getAllContentScriptGlobals();
  },

  initExtensionContext(extension, window) {
    DocumentManager.initExtensionContext(extension, window);
  },

  /**
   * Implementation of extension.getContext(window), which returns the "context"
   * that wraps the current document in the window. The returned context is
   * aware of the document's lifetime, including bfcache transitions.
   *
   * @param {ExtensionChild} extension
   * @param {DOMWindow} window
   * @returns {ContentScriptContextChild}
   */
  getContext(extension, window) {
    let extensions = DocumentManager.getContexts(window);

    let context = extensions.get(extension);
    if (!context) {
      context = new ContentScriptContextChild(extension, window);
      extensions.set(extension, context);
    }
    return context;
  },

  // For test use only.
  getContextByExtensionId(extensionId, window) {
    return DocumentManager.getContext(extensionId, window);
  },

  async handleDetectLanguage({ windows }) {
    let wgc = WindowGlobalChild.getByInnerWindowId(windows[0]);
    let doc = wgc.browsingContext.window.document;
    await promiseDocumentReady(doc);

    // The CLD2 library can analyze HTML, but that uses more memory, and
    // emscripten can't shrink its heap, so we use plain text instead.
    let encoder = Cu.createDocumentEncoder("text/plain");
    encoder.init(doc, "text/plain", Ci.nsIDocumentEncoder.SkipInvisibleContent);

    let result = await lazy.LanguageDetector.detectLanguage({
      language:
        doc.documentElement.getAttribute("xml:lang") ||
        doc.documentElement.getAttribute("lang") ||
        doc.contentLanguage ||
        null,
      tld: doc.location.hostname.match(/[a-z]*$/)[0],
      text: encoder.encodeToStringWithMaxLength(60 * 1024),
      encoding: doc.characterSet,
    });
    return result.language === "un" ? "und" : result.language;
  },

  // Activate MV3 content scripts in all same-origin frames for this tab.
  handleActivateScripts({ options, windows }) {
    let policy = WebExtensionPolicy.getByID(options.id);

    // Order content scripts by run_at timing.
    let runAt = { document_start: [], document_end: [], document_idle: [] };
    for (let matcher of policy.contentScripts) {
      runAt[matcher.runAt].push(this.contentScripts.get(matcher));
    }

    // If we got here, checks in TabManagerBase.activateScripts assert:
    // 1) this is a MV3 extension, with Origin Controls,
    // 2) with a host permission (or content script) for the tab's top origin,
    // 3) and that host permission hasn't been granted yet.

    // We treat the action click as implicit user's choice to activate the
    // extension on the current site, so we can safely run (matching) content
    // scripts in all sameOriginWithTop frames while ignoring host permission.

    let { browsingContext } = WindowGlobalChild.getByInnerWindowId(windows[0]);
    for (let bc of browsingContext.getAllBrowsingContextsInSubtree()) {
      let wgc = bc.currentWindowContext.windowGlobalChild;
      if (wgc?.sameOriginWithTop) {
        // This is TOCTOU safe: if a frame navigated after same-origin check,
        // wgc.isClosed would be true and .matchesWindowGlobal() would fail.
        const runScript = cs => {
          if (cs.matchesWindowGlobal(wgc, /* ignorePermissions */ true)) {
            return cs.injectInto(bc.window);
          }
        };

        // Inject all matching content scripts in proper run_at order.
        Promise.all(runAt.document_start.map(runScript))
          .then(() => Promise.all(runAt.document_end.map(runScript)))
          .then(() => Promise.all(runAt.document_idle.map(runScript)));
      }
    }
  },

  // Used to executeScript, insertCSS and removeCSS.
  async handleActorExecute({ options, windows }) {
    let policy = WebExtensionPolicy.getByID(options.extensionId);
    // `WebExtensionContentScript` uses `MozDocumentMatcher::Matches` to ensure
    // that a script can be run in a document. That requires either `frameId`
    // or `allFrames` to be set. When `frameIds` (plural) is used, we force
    // `allFrames` to be `true` in order to match any frame. This is OK because
    // `executeInWin()` below looks up the window for the given `frameIds`
    // immediately before `script.injectInto()`. Due to this, we won't run
    // scripts in windows with non-matching `frameId`, despite `allFrames`
    // being set to `true`.
    if (options.frameIds) {
      options.allFrames = true;
    }
    let matcher = new WebExtensionContentScript(policy, options);

    Object.assign(matcher, {
      wantReturnValue: options.wantReturnValue,
      removeCSS: options.removeCSS,
      cssOrigin: options.cssOrigin,
    });
    let script = contentScripts.get(matcher);

    if (options.jsCode) {
      script.addJSCode(options.jsCode);
      delete options.jsCode;
    }

    // Add the cssCode to the script, so that it can be converted into a cached URL.
    await script.addCSSCode(options.cssCode);
    delete options.cssCode;

    const executeInWin = innerId => {
      let wg = WindowGlobalChild.getByInnerWindowId(innerId);
      if (wg?.isCurrentGlobal && script.matchesWindowGlobal(wg)) {
        let bc = wg.browsingContext;

        return {
          frameId: bc.parent ? bc.id : 0,
          // Disable exception reporting directly to the console
          // in order to pass the exceptions back to the callsite.
          promise: script.injectInto(bc.window, false),
        };
      }
    };

    let promisesWithFrameIds = windows.map(executeInWin).filter(obj => obj);

    let result = await Promise.all(
      promisesWithFrameIds.map(async ({ frameId, promise }) => {
        if (!options.returnResultsWithFrameIds) {
          return promise;
        }

        try {
          const result = await promise;

          return { frameId, result };
        } catch (error) {
          return { frameId, error };
        }
      })
    ).catch(
      // This is useful when we do not return results/errors with frame IDs in
      // the promises above.
      e => Promise.reject({ message: e.message })
    );

    try {
      // Check if the result can be structured-cloned before sending back.
      return Cu.cloneInto(result, this);
    } catch (e) {
      let path = options.jsPaths.slice(-1)[0] ?? "<anonymous code>";
      let message = `Script '${path}' result is non-structured-clonable data`;
      return Promise.reject({ message, fileName: path });
    }
  },
};

/**
 * Child side of the ExtensionContent process actor, handles some tabs.* APIs.
 */
export class ExtensionContentChild extends JSProcessActorChild {
  receiveMessage({ name, data }) {
    if (!lazy.isContentScriptProcess) {
      return;
    }
    switch (name) {
      case "DetectLanguage":
        return ExtensionContent.handleDetectLanguage(data);
      case "Execute":
        return ExtensionContent.handleActorExecute(data);
      case "ActivateScripts":
        return ExtensionContent.handleActivateScripts(data);
    }
  }
}
