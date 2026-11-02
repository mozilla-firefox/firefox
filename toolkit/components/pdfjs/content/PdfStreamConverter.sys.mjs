/* Copyright 2012 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

const PDFJS_EVENT_ID = "pdf.js.message";
const PDF_VIEWER_ORIGIN = "resource://pdf.js";
const PDF_VIEWER_WEB_PAGE = "resource://pdf.js/web/viewer.html";
const PDF_VIEWER_WORKER_URL = "resource://pdf.js/build/pdf.worker.mjs";
const MAX_NUMBER_OF_PREFS = 60;
// Recommended page-size limit in ISO 32000-1, Annex C.
const MAX_PRINT_TO_PDF_PAGE_SIZE_IN_PT = 14400;
const MAX_PRINT_TO_PDF_FONT_SIZE_IN_PT = 1000;
const VERTICAL_ALIGN_VALUES = new Set(["top", "center"]);
const PDF_CONTENT_TYPE = "application/pdf";
const SUMO_URL = "https://support.mozilla.org/";

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { isEmbeddedPdfLoad } from "resource://gre/modules/pdfjs.sys.mjs";

// Non-pdfjs preferences to get when the viewer is created and to observe.
const toolbarDensityPref = "browser.uidensity";
const caretBrowsingModePref = "accessibility.browsewithcaret";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  NetUtil: "resource://gre/modules/NetUtil.sys.mjs",
  NetworkManager: "resource://pdf.js/PdfJsNetwork.sys.mjs",
  PdfJs: "resource://pdf.js/PdfJs.sys.mjs",
  PdfJsFeaturesNotification:
    "resource://pdf.js/PdfJsFeaturesNotification.sys.mjs",
  PdfJsPrint: "resource://pdf.js/PdfJsPrint.sys.mjs",
  PdfJsTelemetryContent: "resource://pdf.js/PdfJsTelemetry.sys.mjs",
  PdfSandbox: "resource://pdf.js/PdfSandbox.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

var Svc = {};
XPCOMUtils.defineLazyServiceGetter(
  Svc,
  "mime",
  "@mozilla.org/mime;1",
  Ci.nsIMIMEService
);
XPCOMUtils.defineLazyServiceGetter(
  Svc,
  "handlers",
  "@mozilla.org/uriloader/handler-service;1",
  Ci.nsIHandlerService
);

ChromeUtils.defineLazyGetter(lazy, "gOurBinary", () => {
  let file = Services.dirsvc.get("XREExeF", Ci.nsIFile);
  // Make sure to get the .app on macOS
  if (AppConstants.platform == "macosx") {
    while (file) {
      if (/\.app\/?$/i.test(file.leafName)) {
        break;
      }
      file = file.parent;
    }
  }
  return file;
});

function log(aMsg) {
  if (!Services.prefs.getBoolPref("pdfjs.pdfBugEnabled", false)) {
    return;
  }
  var msg = "PdfStreamConverter.js: " + (aMsg.join ? aMsg.join("") : aMsg);
  Services.console.logStringMessage(msg);
  dump(msg + "\n");
}

function getDOMWindow(aChannel, aPrincipal) {
  var requestor = aChannel.notificationCallbacks
    ? aChannel.notificationCallbacks
    : aChannel.loadGroup.notificationCallbacks;
  var win = requestor.getInterface(Ci.nsIDOMWindow);
  // Ensure the window wasn't navigated to something that is not PDF.js.
  if (!win.document.nodePrincipal.equals(aPrincipal)) {
    return null;
  }
  return win;
}

function getActor(window) {
  try {
    const actorName =
      AppConstants.platform === "android" ? "GeckoViewPdfJs" : "PdfJs";
    return window.windowGlobalChild.getActor(actorName);
  } catch (ex) {
    return null;
  }
}

function isValidMatchesCount(data) {
  if (typeof data !== "object" || data === null) {
    return false;
  }
  const { current, total } = data;
  if (
    typeof total !== "number" ||
    total < 0 ||
    typeof current !== "number" ||
    current < 0 ||
    current > total
  ) {
    return false;
  }
  return true;
}

// PDF data storage
function PdfDataListener(length) {
  this.length = length; // less than 0, if length is unknown
  this.buffers = [];
  this.loaded = 0;
}

PdfDataListener.prototype = {
  append: function PdfDataListener_append(chunk) {
    // In most of the cases we will pass data as we receive it, but at the
    // beginning of the loading we may accumulate some data.
    this.buffers.push(chunk);
    this.loaded += chunk.length;
    if (this.length >= 0 && this.length < this.loaded) {
      this.length = -1; // reset the length, server is giving incorrect one
    }
    this.onprogress(this.loaded, this.length >= 0 ? this.length : void 0);
  },
  readData: function PdfDataListener_readData() {
    if (this.buffers.length === 0) {
      return null;
    }
    if (this.buffers.length === 1) {
      return this.buffers.pop();
    }
    // There are multiple buffers that need to be combined into a single
    // buffer.
    let combinedLength = 0;
    for (let buffer of this.buffers) {
      combinedLength += buffer.length;
    }
    let combinedArray = new Uint8Array(combinedLength);
    let writeOffset = 0;
    while (this.buffers.length) {
      let buffer = this.buffers.shift();
      combinedArray.set(buffer, writeOffset);
      writeOffset += buffer.length;
    }
    return combinedArray;
  },
  get isDone() {
    return !!this.isDataReady;
  },
  finish: function PdfDataListener_finish() {
    this.isDataReady = true;
    if (this.oncompleteCallback) {
      this.oncompleteCallback(this.readData());
    }
  },
  error: function PdfDataListener_error(errorCode) {
    this.errorCode = errorCode;
    if (this.oncompleteCallback) {
      this.oncompleteCallback(null, errorCode);
    }
  },
  onprogress() {},
  get oncomplete() {
    return this.oncompleteCallback;
  },
  set oncomplete(value) {
    this.oncompleteCallback = value;
    if (this.isDataReady) {
      value(this.readData());
    }
    if (this.errorCode) {
      value(null, this.errorCode);
    }
  },
};

class PrefObserver {
  #domWindow;

  #prefs = new Map([
    // [pref-trie-audit] "accessibility.browsewithcaret" is an ambiguous prefix of
    // "accessibility.browsewithcaret_shortcut.enabled"; triggers only for the exact pref
    // (sibling is not in this Map so the observe() handler returns early for it).
    [
      caretBrowsingModePref,
      {
        name: "supportsCaretBrowsingMode",
        type: "bool",
        dispatchToContent: true,
      },
    ],
  ]);

  constructor(domWindow, isMobile) {
    this.#domWindow = domWindow;
    this.#init(isMobile);
  }

  #init(isMobile) {
    if (!isMobile) {
      this.#prefs.set(toolbarDensityPref, {
        name: "toolbarDensity",
        type: "int",
        dispatchToContent: true,
      });
      this.#prefs.set("pdfjs.enableGuessAltText", {
        name: "enableGuessAltText",
        type: "bool",
        dispatchToContent: true,
        dispatchToParent: true,
      });
      this.#prefs.set("pdfjs.enableAltTextModelDownload", {
        name: "enableAltTextModelDownload",
        type: "bool",
        dispatchToContent: true,
        dispatchToParent: true,
      });

      // Once the experiment for new alt-text stuff is removed, we can remove this.
      // [pref-trie-audit] "pdfjs.enableAltText" is an ambiguous prefix of
      // "pdfjs.enableAltTextForEnglish", "pdfjs.enableAltTextModelDownload"; triggers only for
      // the exact pref ("enableAltTextModelDownload" has its own entry in this Map above).
      this.#prefs.set("pdfjs.enableAltText", {
        name: "enableAltText",
        type: "bool",
        dispatchToParent: true,
      });
      this.#prefs.set("pdfjs.enableNewAltTextWhenAddingImage", {
        name: "enableNewAltTextWhenAddingImage",
        type: "bool",
        dispatchToParent: true,
      });
      this.#prefs.set("browser.ml.enable", {
        name: "browser.ml.enable",
        type: "bool",
        dispatchToParent: true,
      });
      // Reaching the impression limit must not close open bars.
      this.#prefs.set(lazy.PdfJsFeaturesNotification.IMPRESSION_COUNT_PREF, {
        name: "featuresNotificationDismissed",
        getValue: () => lazy.PdfJsFeaturesNotification.isConsumed(),
        dispatchToContent: true,
        shouldDispatchToContent: value => value,
      });
    }
    for (const pref of this.#prefs.keys()) {
      Services.prefs.addObserver(pref, this, /* aHoldWeak = */ true);
    }
  }

  observe(_aSubject, aTopic, aPrefName) {
    if (aTopic != "nsPref:changed") {
      return;
    }

    const actor = getActor(this.#domWindow);
    if (!actor) {
      return;
    }
    const {
      name,
      type,
      getValue,
      dispatchToContent,
      shouldDispatchToContent,
      dispatchToParent,
    } = this.#prefs.get(aPrefName) || {};
    if (!name) {
      return;
    }
    let value;
    if (getValue) {
      value = getValue();
    } else {
      switch (type) {
        case "bool": {
          value = Services.prefs.getBoolPref(aPrefName);
          break;
        }
        case "int": {
          value = Services.prefs.getIntPref(aPrefName);
          break;
        }
      }
    }
    const data = { name, value };
    if (
      dispatchToContent &&
      (!shouldDispatchToContent || shouldDispatchToContent(value))
    ) {
      actor.dispatchEvent("updatedPreference", data);
    }
    if (dispatchToParent) {
      actor.sendAsyncMessage("PDFJS:Parent:updatedPreference", data);
    }
  }

  QueryInterface = ChromeUtils.generateQI([Ci.nsISupportsWeakReference]);
}

/**
 * All the privileged actions.
 */
class ChromeActions {
  #allowedGlobalEvents = new Set([
    "documentloaded",
    "pagesloaded",
    "layersloaded",
    "outlineloaded",
  ]);

  #viewerDocument = null;

  #worker = null;

  #workerUnloadListener = () => this.closeWorker();

  constructor(domWindow, contentDispositionFilename) {
    this.domWindow = domWindow;
    this.#viewerDocument = domWindow.document;
    this.contentDispositionFilename = contentDispositionFilename;
    this.sandbox = null;
    this.unloadListener = null;
    this.observer = new PrefObserver(domWindow, this.isMobile());
  }

  // PDF.js does not terminate a Worker supplied through workerPort.
  closeWorker() {
    try {
      const worker = this.#worker;
      this.#worker = null;
      worker?.terminate();

      const { domWindow } = this;
      if (domWindow.document === this.#viewerDocument) {
        domWindow.wrappedJSObject.pdfjsPreloadedWorker = null;
      }
    } catch (e) {
      console.error("Error while closing the PDF.js worker:", e);
    }
  }

  preloadWorker() {
    const { domWindow } = this;
    try {
      this.#worker?.terminate();
      this.#worker = new domWindow.Worker(PDF_VIEWER_WORKER_URL, {
        type: "module",
      });
      domWindow.wrappedJSObject.pdfjsPreloadedWorker = this.#worker;
      domWindow.addEventListener("unload", this.#workerUnloadListener, {
        once: true,
      });
    } catch (e) {
      console.error("Error while preloading the PDF.js worker:", e);
    }
  }

  createSandbox(data, sendResponse) {
    function sendResp(res) {
      if (sendResponse) {
        sendResponse(res);
      }
      return res;
    }

    if (!Services.prefs.getBoolPref("pdfjs.enableScripting", false)) {
      return sendResp(false);
    }

    if (this.sandbox !== null) {
      return sendResp(true);
    }

    try {
      this.sandbox = new lazy.PdfSandbox(this.domWindow, data);
    } catch (err) {
      // If there's an error here, it means that something is really wrong
      // on pdf.js side during sandbox initialization phase.
      console.error(err);
      return sendResp(false);
    }

    this.unloadListener = () => {
      this.destroySandbox();
    };
    this.domWindow.addEventListener("unload", this.unloadListener);

    return sendResp(true);
  }

  dispatchEventInSandbox(event) {
    if (this.sandbox) {
      this.sandbox.dispatchEvent(event);
    }
  }

  dispatchAsyncEventInSandbox(event, sendResponse) {
    this.dispatchEventInSandbox(event);
    sendResponse();
  }

  destroySandbox() {
    if (this.sandbox) {
      this.domWindow.removeEventListener("unload", this.unloadListener);
      this.sandbox.destroy();
      this.sandbox = null;
    }
  }

  isInPrivateBrowsing() {
    return lazy.PrivateBrowsingUtils.isContentWindowPrivate(this.domWindow);
  }

  getWindowOriginAttributes() {
    try {
      return this.domWindow.document.nodePrincipal.originAttributes;
    } catch (err) {
      return {};
    }
  }

  async mlDelete(data, sendResponse) {
    const actor = getActor(this.domWindow);
    if (!actor) {
      sendResponse(null);
      return;
    }
    const response = await actor.sendQuery("PDFJS:Parent:mlDelete", data);
    sendResponse(response);
  }

  async mlGuess(data, sendResponse) {
    const actor = getActor(this.domWindow);
    if (!actor) {
      sendResponse(null);
      return;
    }
    const response = await actor.sendQuery("PDFJS:Parent:mlGuess", data);
    sendResponse(response);
  }

  async loadAIEngine(data, sendResponse) {
    const actor = getActor(this.domWindow);
    if (!actor) {
      sendResponse(null);
      return;
    }
    sendResponse(await actor.sendQuery("PDFJS:Parent:loadAIEngine", data));
  }

  async verifyPdfSignature(data, sendResponse) {
    const actor = getActor(this.domWindow);
    if (!actor) {
      sendResponse({ error: "no-actor" });
      return;
    }
    sendResponse(
      await actor.sendQuery("PDFJS:Parent:verifyPdfSignature", data)
    );
  }

  async viewPdfCertificate(data, sendResponse) {
    const actor = getActor(this.domWindow);
    if (!actor) {
      sendResponse(false);
      return;
    }
    sendResponse(
      await actor.sendQuery("PDFJS:Parent:viewPdfCertificate", data)
    );
  }

  #validatePrintToPDFData(items) {
    if (!Array.isArray(items) || items.length === 0) {
      return null;
    }

    const entries = [];
    for (const item of items) {
      const pdfData = item?.data;
      if (typeof pdfData !== "object" || pdfData === null) {
        return null;
      }
      // The caller positions each generated appearance in the original PDF.
      const { width, height } = pdfData;
      if (
        !Number.isFinite(width) ||
        !Number.isFinite(height) ||
        width <= 0 ||
        height <= 0 ||
        width > MAX_PRINT_TO_PDF_PAGE_SIZE_IN_PT ||
        height > MAX_PRINT_TO_PDF_PAGE_SIZE_IN_PT
      ) {
        return null;
      }

      const { text, color, fontSize, fontFamily, verticalAlign } = pdfData;
      if (
        typeof text !== "string" ||
        text === "" ||
        typeof color !== "string" ||
        !/^#[0-9a-f]{6}$/i.test(color) ||
        !Number.isFinite(fontSize) ||
        fontSize <= 0 ||
        fontSize > MAX_PRINT_TO_PDF_FONT_SIZE_IN_PT ||
        (fontFamily != null && typeof fontFamily !== "string") ||
        (verticalAlign != null && !VERTICAL_ALIGN_VALUES.has(verticalAlign))
      ) {
        return null;
      }
      entries.push({
        width,
        height,
        text,
        color,
        fontSize,
        fontFamily: fontFamily || "sans-serif",
        verticalAlign: verticalAlign || "center",
      });
    }
    return entries;
  }

  /**
   * Validate field appearances and render them in the viewer's process.
   *
   * @param {Array<object>} data - one item per field appearance, each shaped
   *   like `{ data: { width, height, ... } }` where `width` and `height` are
   *   the size of the appearance box in pt, and the remaining properties
   *   describe a text appearance (`text`, `color`, `fontSize`, `fontFamily`
   *   and `verticalAlign`).
   * @param {Function} sendResponse - called with the PDF bytes, or with null
   *   if the data is invalid or printing failed.
   */
  printToPDF(data, sendResponse) {
    if (!sendResponse) {
      console.warn(
        "PdfStreamConverter: printToPDF called without a response callback."
      );
      return;
    }
    const entries = this.#validatePrintToPDFData(data);
    if (!entries) {
      console.warn("PdfStreamConverter: ignored invalid print data.");
      sendResponse(null);
      return;
    }
    try {
      sendResponse(
        lazy.PdfJsPrint.printToPDF(this.domWindow.document, entries)
      );
    } catch (ex) {
      console.error("PdfStreamConverter: printToPDF failed.", ex);
      sendResponse(null);
    }
  }

  download(data) {
    if (!this.supportsDownloading()) {
      console.warn("PdfStreamConverter: blocked a download request.");
      return;
    }

    const { originalUrl } = data;
    const blobUrl = data.blobUrl || originalUrl;
    let { filename } = data;
    if (
      typeof filename !== "string" ||
      (!/\.pdf$/i.test(filename) && !data.isAttachment)
    ) {
      filename = "document.pdf";
    }
    const actor = getActor(this.domWindow);
    actor.sendAsyncMessage("PDFJS:Parent:saveURL", {
      blobUrl,
      originalUrl,
      filename,
      isModified: data.isModified === true,
    });
  }

  getLocaleProperties() {
    const { requestedLocale, defaultLocale, isAppLocaleRTL } = Services.locale;
    return {
      lang: requestedLocale || defaultLocale,
      isRTL: isAppLocaleRTL,
    };
  }

  supportsIntegratedFind() {
    // Integrated find is only supported when we're not in a frame
    return this.domWindow.windowGlobalChild.browsingContext.parent === null;
  }

  supportsDownloading() {
    const context = this.domWindow.windowGlobalChild.browsingContext;
    // A top-level document may always trigger downloads. The sandboxed-downloads
    // flag is meant to let an embedder gate downloads from embedded content; at
    // top level there is no embedder, and since the PDF response's CSP sandbox
    // is already ignored for http(s) URLs, enforcing it only for the blob: edge
    // case would be inconsistent and needlessly stop users saving a PDF they
    // view.
    if (context.parent === null) {
      return true;
    }
    // Copied from nsSandboxFlags.h
    const SANDBOXED_DOWNLOADS = 0x10000;
    return (context.sandboxFlags & SANDBOXED_DOWNLOADS) === 0;
  }

  async getBrowserPrefs() {
    const isMobile = this.isMobile();
    const nimbusDataStr = isMobile
      ? await this.getNimbusExperimentData()
      : null;

    // Stay within DrawTargetWebgl's size limit when requesting acceleration.
    let maxCanvasDim = Services.prefs.getIntPref("gfx.canvas.max-size");
    const acceleratedMaxSize = Services.prefs.getIntPref(
      "gfx.canvas.accelerated.max-size",
      0
    );
    if (
      acceleratedMaxSize > 0 &&
      Services.prefs.getBoolPref("pdfjs.enableHWA", false) &&
      Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo)
        .usingAcceleratedCanvas
    ) {
      maxCanvasDim = Math.min(maxCanvasDim, acceleratedMaxSize);
    }

    return {
      allowedGlobalEvents: this.#allowedGlobalEvents,
      canvasMaxAreaInBytes: Services.prefs.getIntPref("gfx.max-alloc-size"),
      isInAutomation: Cu.isInAutomation,
      localeProperties: this.getLocaleProperties(),
      maxCanvasDim,
      nimbusDataStr,
      supportsDocumentFonts:
        !!Services.prefs.getIntPref("browser.display.use_document_fonts") &&
        Services.prefs.getBoolPref("gfx.downloadable_fonts.enabled"),
      supportsIntegratedFind: this.supportsIntegratedFind(),
      supportsDownloading: this.supportsDownloading(),
      supportsMouseWheelZoomCtrlKey:
        Services.prefs.getIntPref("mousewheel.with_control.action") === 3,
      supportsMouseWheelZoomMetaKey:
        Services.prefs.getIntPref("mousewheel.with_meta.action") === 3,
      supportsPinchToZoom: Services.prefs.getBoolPref("apz.allow_zooming"),
      supportsPrinting: Services.prefs.getBoolPref("print.enabled"),
      supportsCaretBrowsingMode: Services.prefs.getBoolPref(
        caretBrowsingModePref
      ),
      toolbarDensity: Services.prefs.getIntPref(toolbarDensityPref, 0),
    };
  }

  isMobile() {
    return AppConstants.platform === "android";
  }

  async getNimbusExperimentData() {
    if (!this.isMobile()) {
      return null;
    }
    const { promise, resolve } = Promise.withResolvers();

    const actor = getActor(this.domWindow);
    actor.sendAsyncMessage("PDFJS:Parent:getNimbus");
    Services.obs.addObserver(
      {
        observe(aSubject, aTopic) {
          if (aTopic === "pdfjs-getNimbus") {
            Services.obs.removeObserver(this, aTopic);
            resolve(aSubject && JSON.stringify(aSubject.wrappedJSObject));
          }
        },
      },
      "pdfjs-getNimbus"
    );
    return promise;
  }

  async dispatchGlobalEvent({ eventName, detail }) {
    if (!this.#allowedGlobalEvents.has(eventName)) {
      return;
    }
    const windowUtils = this.domWindow.windowUtils;
    if (!windowUtils) {
      return;
    }
    const event = new CustomEvent(eventName, {
      bubbles: true,
      cancelable: false,
      detail,
    });
    windowUtils.dispatchEventToChromeOnly(this.domWindow, event);
  }

  reportTelemetry(data) {
    const actor = getActor(this.domWindow);
    actor?.sendAsyncMessage("PDFJS:Parent:reportTelemetry", data);
  }

  reportText(data) {
    const actor = getActor(this.domWindow);
    actor?.sendAsyncMessage("PDFJS:Parent:reportText", data);
  }

  openAboutPdfFeatures() {
    const doc = this.domWindow.document;
    if (!doc.hasValidTransientUserGestureActivation) {
      return;
    }
    doc.consumeTransientUserGestureActivation();
    const actor = getActor(this.domWindow);
    actor?.sendAsyncMessage("PDFJS:Parent:openAboutPdfFeatures");
  }

  updateFindControlState(data) {
    if (!this.supportsIntegratedFind()) {
      return;
    }
    // Verify what we're sending to the findbar.
    var result = data.result;
    var findPrevious = data.findPrevious;
    var findPreviousType = typeof findPrevious;
    if (
      typeof result !== "number" ||
      result < 0 ||
      result > 3 ||
      (findPreviousType !== "undefined" && findPreviousType !== "boolean")
    ) {
      return;
    }
    // Allow the `matchesCount` property to be optional, and ensure that
    // it's valid before including it in the data sent to the findbar.
    let matchesCount = null;
    if (isValidMatchesCount(data.matchesCount)) {
      matchesCount = data.matchesCount;
    }
    // Same for the `rawQuery` property.
    let rawQuery = null;
    if (typeof data.rawQuery === "string") {
      rawQuery = data.rawQuery;
    }
    // Same for the `entireWord` property.
    let entireWord = false;
    if (typeof data.entireWord === "boolean") {
      entireWord = data.entireWord;
    }

    let actor = getActor(this.domWindow);
    actor?.sendAsyncMessage("PDFJS:Parent:updateControlState", {
      result,
      findPrevious,
      entireWord,
      matchesCount,
      rawQuery,
    });
  }

  updateFindMatchesCount(data) {
    if (!this.supportsIntegratedFind()) {
      return;
    }
    // Verify what we're sending to the findbar.
    if (!isValidMatchesCount(data)) {
      return;
    }

    let actor = getActor(this.domWindow);
    actor?.sendAsyncMessage("PDFJS:Parent:updateMatchesCount", data);
  }

  async getPreferences(prefs, sendResponse) {
    const browserPrefs = await this.getBrowserPrefs();

    var defaultBranch = Services.prefs.getDefaultBranch("pdfjs.");
    var currentPrefs = {},
      numberOfPrefs = 0;
    for (var key in prefs) {
      if (++numberOfPrefs > MAX_NUMBER_OF_PREFS) {
        log(
          "getPreferences - Exceeded the maximum number of preferences " +
            "that is allowed to be fetched at once."
        );
        break;
      } else if (!defaultBranch.getPrefType(key)) {
        continue;
      }
      const prefName = `pdfjs.${key}`,
        prefValue = prefs[key];
      switch (typeof prefValue) {
        case "boolean":
          currentPrefs[key] = Services.prefs.getBoolPref(prefName, prefValue);
          break;
        case "number":
          currentPrefs[key] = Services.prefs.getIntPref(prefName, prefValue);
          break;
        case "string": {
          // The URL contains some dynamic values (%VERSION%, ...), so we need to
          // format it.
          const str = Services.prefs.getStringPref(prefName, prefValue);
          currentPrefs[key] =
            str.startsWith(SUMO_URL) && str.includes("%")
              ? Services.urlFormatter.formatURLPref(prefName)
              : str;
          break;
        }
      }
    }

    if ("featuresNotificationDismissed" in currentPrefs) {
      currentPrefs.featuresNotificationDismissed =
        !(await this.#claimFeaturesNotification());
    }

    sendResponse({
      browserPrefs,
      prefs: currentPrefs,
    });
  }

  // Claim an impression in the parent before exposing the notification.
  async #claimFeaturesNotification() {
    if (this.isMobile() || this.domWindow !== this.domWindow.top) {
      return false;
    }
    return (
      (await getActor(this.domWindow)?.sendQuery(
        "PDFJS:Parent:claimFeaturesNotification"
      )) === true
    );
  }

  async setPreferences(data, sendResponse) {
    const actor = getActor(this.domWindow);
    await actor?.sendQuery("PDFJS:Parent:setPreferences", data);

    sendResponse(null);
  }

  /**
   * Set the different editor states in order to be able to update the context
   * menu.
   *
   * @param {object} details
   */
  updateEditorStates({ details }) {
    const doc = this.domWindow.document;
    doc.pdfStates ||= {
      isEditing: false,
      isEmpty: true,
      hasSomethingToUndo: false,
      hasSomethingToRedo: false,
      hasSelectedEditor: false,
      hasSelectedText: false,
      hasSelectedPages: false,
    };
    const { pdfStates } = doc;
    for (const [key, value] of Object.entries(details)) {
      if (typeof value === "boolean" && key in pdfStates) {
        pdfStates[key] = value;
      }
    }
  }

  async handleSignature(data, sendResponse) {
    const actor = getActor(this.domWindow);
    if (!actor) {
      sendResponse(null);
      return;
    }
    const response = await actor.sendQuery(
      "PDFJS:Parent:handleSignature",
      data
    );
    sendResponse(response);
  }
}

/**
 * This is for range requests.
 */
class RangedChromeActions extends ChromeActions {
  constructor(
    domWindow,
    contentDispositionFilename,
    originalRequest,
    rangeEnabled,
    streamingEnabled,
    dataListener
  ) {
    super(domWindow, contentDispositionFilename);
    this.dataListener = dataListener;
    this.originalRequest = originalRequest;
    this.rangeEnabled = rangeEnabled;
    this.streamingEnabled = streamingEnabled;

    this.pdfUrl = originalRequest.URI.spec;
    this.contentLength = originalRequest.contentLength;

    // Pass all the headers from the original request through
    var httpHeaderVisitor = {
      headers: {},
      visitHeader(aHeader, aValue) {
        if (aHeader === "Range") {
          // When loading the PDF from cache, firefox seems to set the Range
          // request header to fetch only the unfetched portions of the file
          // (e.g. 'Range: bytes=1024-'). However, we want to set this header
          // manually to fetch the PDF in chunks.
          return;
        }
        this.headers[aHeader] = aValue;
      },
    };
    if (originalRequest.visitRequestHeaders) {
      originalRequest.visitRequestHeaders(httpHeaderVisitor);
    }

    var self = this;
    var xhr_onreadystatechange = function xhr_onreadystatechange() {
      if (this.readyState === 1) {
        // LOADING
        var netChannel = this.channel;
        // override this XMLHttpRequest's OriginAttributes with our cached parent window's
        // OriginAttributes, as we are currently running under the SystemPrincipal
        this.setOriginAttributes(self.getWindowOriginAttributes());
        if (
          "nsIPrivateBrowsingChannel" in Ci &&
          netChannel instanceof Ci.nsIPrivateBrowsingChannel
        ) {
          var docIsPrivate = self.isInPrivateBrowsing();
          netChannel.setPrivate(docIsPrivate);
        }
      }
    };
    var getXhr = function getXhr() {
      var xhr = new XMLHttpRequest({ mozAnon: false });
      xhr.addEventListener("readystatechange", xhr_onreadystatechange);
      return xhr;
    };

    this.networkManager = new lazy.NetworkManager(this.pdfUrl, {
      httpHeaders: httpHeaderVisitor.headers,
      getXhr,
    });

    // If we are in range request mode, this means we manually issued xhr
    // requests, which we need to abort when we leave the page
    domWindow.addEventListener("unload", function unload(e) {
      domWindow.removeEventListener(e.type, unload);
      self.abortLoading();
    });
  }

  initPassiveLoading() {
    let data, done;
    if (!this.streamingEnabled) {
      this.originalRequest.cancel(Cr.NS_BINDING_ABORTED);
      this.originalRequest = null;
      data = this.dataListener.readData();
      done = this.dataListener.isDone;
      this.dataListener = null;
    } else {
      data = this.dataListener.readData();
      done = this.dataListener.isDone;

      this.dataListener.onprogress = (loaded, total) => {
        const chunk = this.dataListener.readData();

        this.domWindow.postMessage(
          {
            pdfjsLoadAction: "progressiveRead",
            loaded,
            total,
            chunk,
          },
          PDF_VIEWER_ORIGIN,
          chunk ? [chunk.buffer] : undefined
        );
      };
      this.dataListener.oncomplete = () => {
        const { dataListener } = this;
        if (!dataListener) {
          return;
        }
        this.dataListener = null;
        if (!done && dataListener.isDone) {
          this.domWindow.postMessage(
            {
              pdfjsLoadAction: "progressiveDone",
            },
            PDF_VIEWER_ORIGIN
          );
        }
      };
    }

    if (done && !data) {
      this.closeWorker();
    }
    this.domWindow.postMessage(
      {
        pdfjsLoadAction: "supportsRangedLoading",
        rangeEnabled: this.rangeEnabled,
        streamingEnabled: this.streamingEnabled,
        pdfUrl: this.pdfUrl,
        length: this.contentLength,
        data,
        done,
        filename: this.contentDispositionFilename,
      },
      PDF_VIEWER_ORIGIN,
      data ? [data.buffer] : undefined
    );

    return true;
  }

  requestDataRange(args) {
    if (!this.rangeEnabled) {
      return;
    }

    var begin = args.begin;
    var end = args.end;
    var domWindow = this.domWindow;
    // TODO(mack): Support error handler. We're not currently not handling
    // errors from chrome code for non-range requests, so this doesn't
    // seem high-pri
    this.networkManager.requestRange(begin, end, {
      onDone: function RangedChromeActions_onDone({ begin, chunk }) {
        domWindow.postMessage(
          {
            pdfjsLoadAction: "range",
            begin,
            chunk,
          },
          PDF_VIEWER_ORIGIN,
          chunk ? [chunk.buffer] : undefined
        );
      },
      onProgress: function RangedChromeActions_onProgress(evt) {
        domWindow.postMessage(
          {
            pdfjsLoadAction: "rangeProgress",
            loaded: evt.loaded,
          },
          PDF_VIEWER_ORIGIN
        );
      },
    });
  }

  abortLoading() {
    this.networkManager.abortAllRequests();
    if (this.originalRequest) {
      this.originalRequest.cancel(Cr.NS_BINDING_ABORTED);
      this.originalRequest = null;
    }
    this.dataListener = null;
  }
}

/**
 * This is for a single network stream.
 */
class StandardChromeActions extends ChromeActions {
  constructor(
    domWindow,
    contentDispositionFilename,
    originalRequest,
    dataListener
  ) {
    super(domWindow, contentDispositionFilename);
    this.originalRequest = originalRequest;
    this.dataListener = dataListener;
  }

  initPassiveLoading() {
    if (!this.dataListener) {
      return false;
    }

    this.dataListener.onprogress = (loaded, total) => {
      this.domWindow.postMessage(
        {
          pdfjsLoadAction: "progress",
          loaded,
          total,
        },
        PDF_VIEWER_ORIGIN
      );
    };

    this.dataListener.oncomplete = (data, errorCode) => {
      if (!data) {
        this.closeWorker();
      }
      this.domWindow.postMessage(
        {
          pdfjsLoadAction: "complete",
          data,
          errorCode,
          filename: this.contentDispositionFilename,
        },
        PDF_VIEWER_ORIGIN,
        data ? [data.buffer] : undefined
      );

      this.dataListener = null;
      this.originalRequest = null;
    };

    return true;
  }

  abortLoading() {
    if (this.originalRequest) {
      this.originalRequest.cancel(Cr.NS_BINDING_ABORTED);
      this.originalRequest = null;
    }
    this.dataListener = null;
  }
}

/**
 * Event listener to trigger chrome privileged code.
 */
class RequestListener {
  constructor(actions) {
    this.actions = actions;
  }

  // Receive an event and (optionally) asynchronously responds.
  receive({ target, detail }) {
    const doc = target.ownerDocument;
    const { action, data, responseExpected } = detail;

    const actionFn = this.actions[action];
    if (!actionFn) {
      log("Unknown action: " + action);
      return;
    }
    let response = null;

    if (!responseExpected) {
      doc.documentElement.removeChild(target);
    } else {
      response = function (aResponse) {
        try {
          const listener = doc.createEvent("CustomEvent");
          const detail = Cu.cloneInto({ response: aResponse }, doc.defaultView);
          listener.initCustomEvent("pdf.js.response", true, false, detail);
          return target.dispatchEvent(listener);
        } catch (e) {
          // doc is no longer accessible because the requestor is already
          // gone. unloaded content cannot receive the response anyway.
          return false;
        }
      };
    }
    actionFn.call(this.actions, data, response);
  }
}

export function PdfStreamConverter() {}

PdfStreamConverter.prototype = {
  QueryInterface: ChromeUtils.generateQI([
    "nsIStreamConverter",
    "nsIStreamListener",
    "nsIRequestObserver",
  ]),

  /*
   * This component works as such:
   * 1. asyncConvertData stores the listener
   * 2. onStartRequest creates a new channel, streams the viewer
   * 3. If range requests are supported:
   *      3.1. Leave the request open until the viewer is ready to switch to
   *           range requests.
   *
   *    If range rquests are not supported:
   *      3.1. Read the stream as it's loaded in onDataAvailable to send
   *           to the viewer
   *
   * The convert function just returns the stream, it's just the synchronous
   * version of asyncConvertData.
   */

  // nsIStreamConverter::convert
  convert() {
    throw Components.Exception("", Cr.NS_ERROR_NOT_IMPLEMENTED);
  },

  // nsIStreamConverter::asyncConvertData
  asyncConvertData(aFromType, aToType, aListener, aCtxt) {
    if (aCtxt && aCtxt instanceof Ci.nsIChannel) {
      aCtxt.QueryInterface(Ci.nsIChannel);
    }
    // We need to check if we're supposed to convert here, because not all
    // asyncConvertData consumers will call getConvertedType first:
    this.getConvertedType(aFromType, aCtxt);

    // Store the listener passed to us
    this.listener = aListener;
  },

  _usableHandler(handlerInfo) {
    let { preferredApplicationHandler } = handlerInfo;
    if (
      !preferredApplicationHandler ||
      !(preferredApplicationHandler instanceof Ci.nsILocalHandlerApp)
    ) {
      return false;
    }
    preferredApplicationHandler.QueryInterface(Ci.nsILocalHandlerApp);
    // We have an app, grab the executable
    let { executable } = preferredApplicationHandler;
    if (!executable) {
      return false;
    }
    return !executable.equals(lazy.gOurBinary);
  },

  /*
   * Check if the user wants to use PDF.js. Returns true if PDF.js should
   * handle PDFs, and false if not. Will always return true on non-parent
   * processes.
   *
   * If the user has selected to open PDFs with a helper app, and we are that
   * helper app, or if the user has selected the OS default, and we are that
   * OS default, reset the preference back to pdf.js .
   *
   */
  _validateAndMaybeUpdatePDFPrefs() {
    let { processType, PROCESS_TYPE_DEFAULT } = Services.appinfo;
    // If we're not in the parent, or are the default, then just say yes.
    if (processType != PROCESS_TYPE_DEFAULT || lazy.PdfJs.cachedIsDefault()) {
      return { shouldOpen: true };
    }

    // OK, PDF.js might not be the default. Find out if we've misled the user
    // into making Firefox an external handler or if we're the OS default and
    // Firefox is set to use the OS default:
    let mime = Svc.mime.getFromTypeAndExtension(PDF_CONTENT_TYPE, "pdf");
    // The above might throw errors. We're deliberately letting those bubble
    // back up, where they'll tell the stream converter not to use us.

    if (!mime) {
      // This shouldn't happen, but we can't fix what isn't there. Assume
      // we're OK to handle with PDF.js
      return { shouldOpen: true };
    }

    const { saveToDisk, useHelperApp, useSystemDefault } = Ci.nsIHandlerInfo;
    let { preferredAction, alwaysAskBeforeHandling } = mime;
    // return this info so getConvertedType can use it.
    let rv = { alwaysAskBeforeHandling, shouldOpen: false };
    // If the user has indicated they want to be asked or want to save to
    // disk, we shouldn't render inline immediately:
    if (alwaysAskBeforeHandling || preferredAction == saveToDisk) {
      return rv;
    }
    // If we have usable helper app info, don't use PDF.js
    if (preferredAction == useHelperApp && this._usableHandler(mime)) {
      return rv;
    }
    // If we want the OS default and that's not Firefox, don't use PDF.js
    if (preferredAction == useSystemDefault && !mime.isCurrentAppOSDefault()) {
      return rv;
    }
    rv.shouldOpen = true;
    // Log that we're doing this to help debug issues if people end up being
    // surprised by this behaviour.
    console.error("Found unusable PDF preferences. Fixing back to PDF.js");

    mime.preferredAction = Ci.nsIHandlerInfo.handleInternally;
    mime.alwaysAskBeforeHandling = false;
    Svc.handlers.store(mime);
    return true;
  },

  getConvertedType(aFromType, aChannel) {
    // nsIStreamConverter allows a null channel, but PDF.js needs one to decide
    // how the PDF must be handled.
    if (!aChannel) {
      Components.returnCode = Cr.NS_ERROR_INVALID_ARG;
      return "";
    }

    if (aChannel instanceof Ci.nsIMultiPartChannel) {
      throw new Components.Exception(
        "PDF.js doesn't support multipart responses.",
        Cr.NS_ERROR_NOT_IMPLEMENTED
      );
    }

    // Without a browsing context there's nowhere to render the result, so
    // PDF.js must not claim the channel and rewrite its type to text/html -
    // that breaks the external handler for attachments opened from the compose
    // window (bug 1698140).
    let browsingContext = aChannel.loadInfo?.targetBrowsingContext;
    if (!browsingContext) {
      throw new Components.Exception(
        "PDF.js can't be used without a browsing context.",
        Cr.NS_ERROR_FAILURE
      );
    }

    const HTML = "text/html";
    let channelURI = aChannel.URI;
    // We can be invoked for application/octet-stream; check if we want the
    // channel first:
    if (aFromType != "application/pdf") {
      // Check if the filename has a PDF extension.
      let isPDF = false;
      try {
        isPDF = aChannel.contentDispositionFilename.endsWith(".pdf");
      } catch (ex) {}
      if (!isPDF) {
        isPDF =
          channelURI?.QueryInterface(Ci.nsIURL).fileExtension.toLowerCase() ==
          "pdf";
      }

      let toplevelOctetStream =
        aFromType == "application/octet-stream" && !browsingContext.parent;
      if (
        !isPDF ||
        !toplevelOctetStream ||
        !Services.prefs.getBoolPref("pdfjs.handleOctetStream", false)
      ) {
        throw new Components.Exception(
          "Ignore PDF.js for this download.",
          Cr.NS_ERROR_FAILURE
        );
      }
      // fall through, this appears to be a pdf.
    }

    let { alwaysAskBeforeHandling, shouldOpen } =
      this._validateAndMaybeUpdatePDFPrefs();

    if (shouldOpen) {
      return HTML;
    }
    // Hm, so normally, no pdfjs. However... if this is a file: channel there
    // are some edge-cases.
    if (channelURI?.schemeIs("file")) {
      // If we're loaded with system principal, we were likely handed the PDF
      // by the OS or directly from the URL bar. Assume we should load it:
      let triggeringPrincipal = aChannel.loadInfo?.triggeringPrincipal;
      if (triggeringPrincipal?.isSystemPrincipal) {
        return HTML;
      }

      // If we're loading from a file: link, load it in PDF.js unless the user
      // has told us they always want to open/save PDFs.
      // This is because handing off the choice to open in Firefox itself
      // through the dialog doesn't work properly and making it work is
      // non-trivial (see https://bugzilla.mozilla.org/show_bug.cgi?id=1680147#c3 )
      // - and anyway, opening the file is what we do for *all*
      // other file types we handle internally (and users can then use other UI
      // to save or open it with other apps from there).
      if (triggeringPrincipal?.schemeIs("file") && alwaysAskBeforeHandling) {
        return HTML;
      }
    }

    // Keep embedded PDFs in PDF.js instead of invoking the configured handler.
    if (isEmbeddedPdfLoad(aChannel.loadInfo)) {
      return HTML;
    }

    throw new Components.Exception("Can't use PDF.js", Cr.NS_ERROR_FAILURE);
  },

  // nsIStreamListener::onDataAvailable
  onDataAvailable(aRequest, aInputStream, aOffset, aCount) {
    if (!this.dataListener) {
      return;
    }

    var binaryStream = this.binaryStream;
    binaryStream.setInputStream(aInputStream);
    let chunk = new ArrayBuffer(aCount);
    binaryStream.readArrayBuffer(aCount, chunk);
    this.dataListener.append(new Uint8Array(chunk));
  },

  // nsIRequestObserver::onStartRequest
  onStartRequest(aRequest) {
    // Setup the request so we can use it below.
    var isHttpRequest = false;
    try {
      aRequest.QueryInterface(Ci.nsIHttpChannel);
      isHttpRequest = true;
    } catch (e) {}

    var rangeRequest = false;
    var streamRequest = false;
    const searchParams = new URLSearchParams(aRequest.URI.ref.toLowerCase());
    const isPDFBugEnabled = Services.prefs.getBoolPref(
      "pdfjs.pdfBugEnabled",
      false
    );
    const disableWorker =
      isPDFBugEnabled && searchParams.get("disableworker") === "true";
    if (isHttpRequest) {
      var contentEncoding = "identity";
      try {
        contentEncoding = aRequest.getResponseHeader("Content-Encoding");
      } catch (e) {}

      var acceptRanges;
      try {
        acceptRanges = aRequest.getResponseHeader("Accept-Ranges");
      } catch (e) {}

      rangeRequest =
        contentEncoding === "identity" &&
        acceptRanges === "bytes" &&
        aRequest.contentLength >= 0 &&
        !Services.prefs.getBoolPref("pdfjs.disableRange", false) &&
        (!isPDFBugEnabled || searchParams.get("disablerange") !== "true");
      streamRequest =
        contentEncoding === "identity" &&
        aRequest.contentLength >= 0 &&
        !Services.prefs.getBoolPref("pdfjs.disableStream", false) &&
        (!isPDFBugEnabled || searchParams.get("disablestream") !== "true");
    }

    aRequest.QueryInterface(Ci.nsIChannel);

    aRequest.QueryInterface(Ci.nsIWritablePropertyBag);

    var contentDispositionFilename;
    try {
      contentDispositionFilename = aRequest.contentDispositionFilename;
    } catch (e) {}

    if (
      contentDispositionFilename &&
      !/\.pdf$/i.test(contentDispositionFilename)
    ) {
      contentDispositionFilename += ".pdf";
    }

    // Change the content type so we don't get stuck in a loop.
    aRequest.setProperty("contentType", aRequest.contentType);
    aRequest.contentType = "text/html";
    if (isHttpRequest) {
      // We trust PDF viewer, using no CSP
      aRequest.setResponseHeader("Content-Security-Policy", "", false);
      aRequest.setResponseHeader(
        "Content-Security-Policy-Report-Only",
        "",
        false
      );
      // The viewer does not need to handle HTTP Refresh header.
      aRequest.setResponseHeader("Refresh", "", false);
      // There is no reason to load something via <link>: the only external
      // resource is the pdf itself.
      aRequest.setResponseHeader("Link", "", false);
    }

    lazy.PdfJsTelemetryContent.onViewerIsUsed();

    // The document will be loaded via the stream converter as html,
    // but since we may have come here via a download or attachment
    // that was opened directly, force the content disposition to be
    // inline so that the html document will be loaded normally instead
    // of going to the helper service.
    aRequest.contentDisposition = Ci.nsIChannel.DISPOSITION_FORCE_INLINE;

    // Creating storage for PDF data
    var contentLength = aRequest.contentLength;
    this.dataListener = new PdfDataListener(contentLength);
    this.binaryStream = Cc["@mozilla.org/binaryinputstream;1"].createInstance(
      Ci.nsIBinaryInputStream
    );

    // Create a new channel that is viewer loaded as a resource.
    var channel = lazy.NetUtil.newChannel({
      uri: PDF_VIEWER_WEB_PAGE,
      loadUsingSystemPrincipal: true,
    });

    var listener = this.listener;
    var dataListener = this.dataListener;
    // Proxy all the request observer calls, when it gets to onStopRequest
    // we can get the dom window.  We also intentionally pass on the original
    // request(aRequest) below so we don't overwrite the original channel and
    // trigger an assertion.
    var proxy = {
      onStartRequest() {
        listener.onStartRequest(aRequest);
      },
      onDataAvailable(request, inputStream, offset, count) {
        listener.onDataAvailable(aRequest, inputStream, offset, count);
      },
      onStopRequest(request, statusCode) {
        const domWindow = getDOMWindow(channel, resourcePrincipal);
        if (!Components.isSuccessCode(statusCode) || !domWindow) {
          // The request may have been aborted and the document may have been
          // replaced with something that is not PDF.js, abort attaching.
          listener.onStopRequest(aRequest, statusCode);
          return;
        }
        var actions;
        if (rangeRequest || streamRequest) {
          actions = new RangedChromeActions(
            domWindow,
            contentDispositionFilename,
            aRequest,
            rangeRequest,
            streamRequest,
            dataListener
          );
        } else {
          actions = new StandardChromeActions(
            domWindow,
            contentDispositionFilename,
            aRequest,
            dataListener
          );
        }
        // viewer.mjs reads pdfjsPreloadedWorker during module evaluation.
        if (!disableWorker) {
          actions.preloadWorker();
        }

        var requestListener = new RequestListener(actions);
        domWindow.document.addEventListener(
          PDFJS_EVENT_ID,
          function (event) {
            requestListener.receive(event);
          },
          false,
          true
        );

        let actor = getActor(domWindow);
        actor?.init(actions.supportsIntegratedFind());
        actor?.sendAsyncMessage("PDFJS:Parent:recordExposure");
        listener.onStopRequest(aRequest, statusCode);
      },
    };

    // Keep the URL the same so the browser sees it as the same.
    channel.originalURI = aRequest.URI;
    channel.loadGroup = aRequest.loadGroup;
    channel.loadInfo.originAttributes = aRequest.loadInfo.originAttributes;

    // We can use the resource principal when data is fetched by the chrome,
    // e.g. useful for NoScript. Make make sure we reuse the origin attributes
    // from the request channel to keep isolation consistent.
    var uri = lazy.NetUtil.newURI(PDF_VIEWER_WEB_PAGE);
    var resourcePrincipal =
      Services.scriptSecurityManager.createContentPrincipal(
        uri,
        aRequest.loadInfo.originAttributes
      );
    // Remember the principal we would have had before we mess with it.
    let originalPrincipal =
      Services.scriptSecurityManager.getChannelResultPrincipal(aRequest);
    aRequest.owner = resourcePrincipal;
    aRequest.setProperty("noPDFJSPrincipal", originalPrincipal);

    channel.asyncOpen(proxy);
  },

  // nsIRequestObserver::onStopRequest
  onStopRequest(aRequest, aStatusCode) {
    if (!this.dataListener) {
      // Do nothing
      return;
    }

    if (Components.isSuccessCode(aStatusCode)) {
      this.dataListener.finish();
    } else {
      this.dataListener.error(aStatusCode);
    }
    delete this.dataListener;
    delete this.binaryStream;
  },
};
