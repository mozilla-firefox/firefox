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

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { PdfJsTelemetry } from "resource://pdf.js/PdfJsTelemetry.sys.mjs";
import { playSound } from "resource://gre/modules/FinderSound.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  createEngine: "chrome://global/content/ml/EngineProcess.sys.mjs",
  IndexedDB: "resource://gre/modules/IndexedDB.sys.mjs",
  MLUninstallService: "chrome://global/content/ml/Utils.sys.mjs",
  MultiProgressAggregator: "chrome://global/content/ml/Utils.sys.mjs",
  PdfJsFeaturesNotification:
    "resource://pdf.js/PdfJsFeaturesNotification.sys.mjs",
  PdfJsGuessAltTextFeature: "resource://pdf.js/PdfJsAIFeature.sys.mjs",
  Progress: "chrome://global/content/ml/Utils.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SetClipboardSearchString: "resource://gre/modules/Finder.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
});

const IMAGE_TO_TEXT_TASK = "moz-image-to-text";
const ML_ENGINE_MAX_TIMEOUT = 60000;
// Lets a test run the real alt-text model in automation.
const ALT_TEXT_INFERENCE_IN_AUTOMATION_PREF =
  "pdfjs.altText.inferenceInAutomation";
const PDFJS_DB_NAME = "pdfjs";
const PDFJS_DB_VERSION = 1;
const PDFJS_STORE_NAME = "signatures";
const PDFJS_SIGNATURE_STORAGE_CHANGED_TOPIC = "pdfjs:storedSignaturesChanged";

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "matchesCountLimit",
  "accessibility.typeaheadfind.matchesCountLimit"
);

let gFindTypes = [
  "find",
  "findagain",
  "findhighlightallchange",
  "findcasesensitivitychange",
  "findbarclose",
  "finddiacriticmatchingchange",
];

// Defence-in-depth validation for content-supplied signature and alt-text IPC
// payloads before they reach NSS, `about:certificate`, or the ML engine.
// Exported for direct xpcshell coverage.
const MAX_CERTS_PER_VIEW = 16;
const MAX_DER_BASE64_LEN = 64 * 1024; // 64 KiB / cert is far above any real chain.
const BASE64_RE = /^[A-Za-z0-9+/=]+$/;

// The four properties sent by the viewer for an ImageData buffer.
const ML_GUESS_KEYS = ["data", "width", "height", "channels"];
// RawImage supports one to four channels.
const MAX_ML_GUESS_CHANNELS = 4;

// `instanceof Uint8Array` is unreliable across realms (xpcshell exercises
// these validators from another global); brand-check a byte view instead.
const isBytes = x => ArrayBuffer.isView(x) && x.BYTES_PER_ELEMENT === 1;

/**
 * Validate the shape of a `verifyPdfSignature` IPC payload before
 * forwarding bytes to `nsIX509CertDB.asyncVerifyPKCS7Object`.
 *
 * @param {*} data Content-supplied object.
 * @returns {boolean} `true` if the payload is a `{pkcs7, data,
 *   signatureType}` triple with non-empty `Uint8Array` `pkcs7`, a
 *   non-empty `Uint8Array[]` `data`, and `signatureType ∈ {0, 1}`.
 * Exported for unit tests.
 * @internal
 */
export function validateVerifyPdfSignatureArgs(data) {
  const { pkcs7, data: detached, signatureType } = data || {};
  return (
    isBytes(pkcs7) &&
    Array.isArray(detached) &&
    detached.length &&
    detached.every(isBytes) &&
    (signatureType === 0 || signatureType === 1)
  );
}

/**
 * Validate + filter the cert payload of a `viewPdfCertificate` IPC
 * call before composing the `about:certificate` URL.
 *
 * @param {*} data Content-supplied object.
 * @returns {?string[]} The filtered list of valid base64 cert
 *   strings, or `null` when the input shape is bad or no cert
 *   passes the base64 / length checks.
 * Exported for unit tests.
 * @internal
 */
export function filterCertsForView(data) {
  const rawCerts = Array.isArray(data?.certs) ? data.certs : [];
  if (!rawCerts.length || rawCerts.length > MAX_CERTS_PER_VIEW) {
    return null;
  }
  const certs = rawCerts.filter(
    d =>
      typeof d === "string" &&
      d.length &&
      d.length <= MAX_DER_BASE64_LEN &&
      BASE64_RE.test(d)
  );
  return certs.length ? certs : null;
}

function stubAltTextInference() {
  return (
    Cu.isInAutomation &&
    !Services.prefs.getBoolPref(ALT_TEXT_INFERENCE_IN_AUTOMATION_PREF, false)
  );
}

/**
 * Validate an `mlGuess` IPC payload before forwarding it to the ML engine.
 *
 * The image-to-text pipeline calls `RawImage.fromURL()` when `url` is present,
 * while the viewer sends only pixel data and geometry. Reject unknown
 * properties and require the geometry to match `data`.
 *
 * @param {*} request Content-supplied object.
 * @returns {boolean} `true` for a valid
 *   `{data, width, height, channels}` request.
 * Exported for unit tests.
 * @internal
 */
export function validateMLGuessRequest(request) {
  if (request === null || typeof request !== "object") {
    return false;
  }
  const keys = Reflect.ownKeys(request);
  if (
    keys.length !== ML_GUESS_KEYS.length ||
    !keys.every(key => ML_GUESS_KEYS.includes(key))
  ) {
    return false;
  }
  const { data, width, height, channels } = request;
  return (
    isBytes(data) &&
    Number.isInteger(width) &&
    width > 0 &&
    Number.isInteger(height) &&
    height > 0 &&
    Number.isInteger(channels) &&
    channels > 0 &&
    channels <= MAX_ML_GUESS_CHANNELS &&
    data.length === width * height * channels
  );
}

export class PdfJsParent extends JSWindowActorParent {
  #signatureStorageChangedObserver = null;

  #mutablePreferences = new Set([
    "enableGuessAltText",
    "enableAltTextModelDownload",
    "enableNewAltTextWhenAddingImage",
  ]);

  #nextTextRequestId = 0;

  /**
   * Holds the Promise resolves for getTextContent requests.
   *
   * @type {Map<number, (text: string) => void>}
   */
  #textRequests = new Map();

  constructor() {
    super();
    this._boundToFindbar = null;
    this._findFailedString = null;
    this._lastNotFoundStringLength = 0;

    if (
      !Services.prefs.prefHasUserValue("pdfjs.enableAltText") &&
      !Services.locale.appLocaleAsBCP47.startsWith("en") &&
      Services.prefs.getBoolPref("pdfjs.enableAltText", true)
    ) {
      Services.prefs.setBoolPref("pdfjs.enableAltText", false);
    }

    this._updatedPreference();
  }

  didDestroy() {
    this._removeEventListener();
    if (this.#signatureStorageChangedObserver) {
      Services.obs.removeObserver(
        this.#signatureStorageChangedObserver,
        PDFJS_SIGNATURE_STORAGE_CHANGED_TOPIC
      );
      this.#signatureStorageChangedObserver = null;
    }
  }

  receiveMessage(aMsg) {
    switch (aMsg.name) {
      case "PDFJS:Parent:updateControlState":
        return this._updateControlState(aMsg);
      case "PDFJS:Parent:updateMatchesCount":
        return this._updateMatchesCount(aMsg);
      case "PDFJS:Parent:addEventListener":
        return this._addEventListener();
      case "PDFJS:Parent:saveURL":
        return this._saveURL(aMsg);
      case "PDFJS:Parent:reportText":
        return this._reportText(aMsg);
      case "PDFJS:Parent:recordExposure":
        return this._recordExposure();
      case "PDFJS:Parent:reportTelemetry":
        return this._reportTelemetry(aMsg);
      case "PDFJS:Parent:mlGuess":
        return this._mlGuess(aMsg);
      case "PDFJS:Parent:setPreferences":
        return this._setPreferences(aMsg);
      case "PDFJS:Parent:loadAIEngine":
        return this._loadAIEngine(aMsg);
      case "PDFJS:Parent:mlDelete":
        return this._mlDelete(aMsg);
      case "PDFJS:Parent:updatedPreference":
        return this._updatedPreference(aMsg);
      case "PDFJS:Parent:handleSignature":
        return this._handleSignature(aMsg);
      case "PDFJS:Parent:verifyPdfSignature":
        return this._verifyPdfSignature(aMsg);
      case "PDFJS:Parent:viewPdfCertificate":
        return this._viewPdfCertificate(aMsg);
      case "PDFJS:Parent:openAboutPdfFeatures":
        return this._openAboutPdfFeatures();
      case "PDFJS:Parent:claimFeaturesNotification":
        return this._claimFeaturesNotification();
    }
    return undefined;
  }

  _openAboutPdfFeatures() {
    // Ignore embedded viewers.
    if (this.browsingContext !== this.browsingContext.top) {
      return;
    }
    // Keep the PDF and unsaved edits open.
    this.browser?.documentGlobal.openTrustedLinkIn(
      "about:pdf#features",
      "tab",
      { inBackground: false, relatedToCurrent: true }
    );
  }

  _claimFeaturesNotification() {
    if (!lazy.PdfJsFeaturesNotification.isEligible()) {
      return false;
    }
    lazy.PdfJsFeaturesNotification.recordImpression();
    return true;
  }

  _viewPdfCertificate({ data }) {
    if (
      !Services.prefs.getBoolPref("pdfjs.enableSignatureVerification", true)
    ) {
      return false;
    }
    // Validate before the try — invalid input isn't an "exception", it
    // should produce a clean rejection without going through catch.
    const certs = filterCertsForView(data);
    if (!certs) {
      console.warn("viewPdfCertificate: bad cert payload");
      return false;
    }
    try {
      const params = certs.map(d => `cert=${encodeURIComponent(d)}`).join("&");
      const url = `about:certificate?${params}`;
      const browser = this.browser;
      let win = browser?.documentGlobal;
      if (!win?.openTrustedLinkIn) {
        win = Services.wm.getMostRecentBrowserWindow();
      }
      if (win?.openTrustedLinkIn) {
        win.openTrustedLinkIn(url, "tab", { relatedToCurrent: true });
        return true;
      }
      console.warn("viewPdfCertificate: no chrome window available");
      return false;
    } catch (ex) {
      console.error("viewPdfCertificate failed:", ex?.name || "Error");
      return false;
    }
  }

  async _verifyPdfSignature({ data }) {
    if (
      !Services.prefs.getBoolPref("pdfjs.enableSignatureVerification", true)
    ) {
      return { error: "disabled" };
    }
    if (!validateVerifyPdfSignatureArgs(data)) {
      return { error: "bad-args" };
    }
    const { pkcs7, data: detached, signatureType } = data;
    try {
      const certDB = Cc["@mozilla.org/security/x509certdb;1"].getService(
        Ci.nsIX509CertDB
      );
      const results = await certDB.asyncVerifyPKCS7Object(
        pkcs7,
        detached,
        signatureType
      );
      return Array.from(results, r => ({
        signatureResult: this.#nsresultName(r.signatureResult),
        certificateResult: this.#nsresultName(r.certificateResult),
        certificate: this.#flattenCertificate(r.signerCertificate),
      }));
    } catch (ex) {
      // Don't log `String(ex)` — `ex.message` can embed attacker-derived
      // strings from the PKCS#7 bytes. Log only the error class / NSS
      // result code.
      console.error(
        "verifyPdfSignature failed:",
        ex?.name || "Error",
        ex?.result ?? ""
      );
      return { error: ex?.name || "verify-failed" };
    }
  }

  #nsresultName(code) {
    if (code === Cr.NS_OK) {
      return "NS_OK";
    }
    try {
      const errSvc = Cc["@mozilla.org/nss_errors_service;1"].getService(
        Ci.nsINSSErrorsService
      );
      return errSvc.getErrorName?.(code) || `0x${(code >>> 0).toString(16)}`;
    } catch {
      return `0x${(code >>> 0).toString(16)}`;
    }
  }

  #flattenCertificate(cert) {
    if (!cert) {
      return null;
    }
    const validity = cert.validity;
    // PRTime is microseconds since epoch.
    const notBefore = validity?.notBefore
      ? new Date(Number(validity.notBefore) / 1000).toISOString()
      : null;
    const notAfter = validity?.notAfter
      ? new Date(Number(validity.notAfter) / 1000).toISOString()
      : null;
    let derBase64 = "";
    try {
      derBase64 = cert.getBase64DERString?.() || "";
    } catch {}
    return {
      subjectCN: cert.commonName || cert.subjectName || "",
      issuerCN: cert.issuerCommonName || cert.issuerName || "",
      notBefore,
      notAfter,
      serialNumber: cert.serialNumber || "",
      fingerprintSha256: cert.sha256Fingerprint || "",
      derBase64,
    };
  }

  /*
   * Internal
   */

  get browser() {
    return this.browsingContext.top.embedderElement;
  }

  /**
   * Extracts the text content from a PDF.
   *
   * @returns {Promise<string>}
   */
  getTextContent() {
    const { promise, resolve } = Promise.withResolvers();
    const requestId = this.#nextTextRequestId++;
    this.#textRequests.set(requestId, resolve);
    this.sendAsyncMessage("PDFJS:Child:handleEvent", {
      type: "requestTextContent",
      detail: { requestId },
    });
    return promise;
  }

  async #openDatabase() {
    return lazy.IndexedDB.open(PDFJS_DB_NAME, PDFJS_DB_VERSION, db => {
      db.createObjectStore(PDFJS_STORE_NAME, {
        keyPath: "uuid",
      });
    });
  }

  async _handleSignature({ data }) {
    switch (data.action) {
      case "create":
        return this.#createSignature(data);
      case "get":
        return this.#getSignatures(data);
      case "delete":
        return this.#deleteSignature(data);
      default:
        return null;
    }
  }

  async #getSignatures() {
    if (!this.#signatureStorageChangedObserver) {
      const self = this;
      this.#signatureStorageChangedObserver = {
        observe(aSubject, aTopic) {
          if (
            aTopic === PDFJS_SIGNATURE_STORAGE_CHANGED_TOPIC &&
            // No need to send an event to the viewer which triggered the
            // change because it already knows about it.
            (aSubject !== self || Cu.isInAutomation)
          ) {
            // The child will dispatch an event in the pdf.js window.
            // This way the viewer is able to update the UI (add/remove some
            // signatures).
            self.sendAsyncMessage("PDFJS:Child:handleEvent", {
              type: "storedSignaturesChanged",
              detail: null,
            });
          }
        },
      };
      Services.obs.addObserver(
        this.#signatureStorageChangedObserver,
        PDFJS_SIGNATURE_STORAGE_CHANGED_TOPIC
      );
    }

    let db;
    try {
      db = await this.#openDatabase();
      const store = await db.objectStore(PDFJS_STORE_NAME, "readonly");
      const signatures = await store.getAll();

      return signatures.sort((a, b) => a.timestamp - b.timestamp);
    } catch (e) {
      console.error("PDF.js", e);
      return null;
    } finally {
      await db?.close();
    }
  }

  async #createSignature({ description, signatureData }) {
    let db;
    try {
      db = await this.#openDatabase();
      const store = await db.objectStore(PDFJS_STORE_NAME, "readwrite");
      const uuid = Services.uuid.generateUUID().toString().replace(/[{}]/g, "");

      await store.put({
        uuid,
        description,
        signatureData,
        timestamp: Date.now(),
      });

      Services.obs.notifyObservers(this, PDFJS_SIGNATURE_STORAGE_CHANGED_TOPIC);

      return uuid;
    } catch (e) {
      console.error("PDF.js", e);
      return null;
    } finally {
      await db?.close();
    }
  }

  async #deleteSignature({ uuid }) {
    let db;
    try {
      db = await this.#openDatabase();
      const store = await db.objectStore(PDFJS_STORE_NAME, "readwrite");
      await store.delete(uuid);

      Services.obs.notifyObservers(this, PDFJS_SIGNATURE_STORAGE_CHANGED_TOPIC);

      return true;
    } catch (e) {
      console.error("PDF.js", e);
      return false;
    } finally {
      await db?.close();
    }
  }

  _updatedPreference() {
    PdfJsTelemetry.report({
      type: "editing",
      data: {
        type: "stamp",
        action: "pdfjs.image.alt_text_edit",
        data: {
          ask_to_edit:
            Services.prefs.getBoolPref("pdfjs.enableAltText", false) &&
            Services.prefs.getBoolPref(
              "pdfjs.enableNewAltTextWhenAddingImage",
              false
            ),
          ai_generation:
            Services.prefs.getBoolPref("pdfjs.enableAltText", false) &&
            Services.prefs.getBoolPref("pdfjs.enableGuessAltText", false) &&
            Services.prefs.getBoolPref(
              "pdfjs.enableAltTextModelDownload",
              false
            ) &&
            Services.prefs.getBoolPref("browser.ml.enable", false),
        },
      },
    });
  }

  _setPreferences({ data }) {
    if (!data || typeof data !== "object") {
      return;
    }
    // Map the viewer's dismissal flag to the shared impression state.
    if (data.featuresNotificationDismissed === true) {
      lazy.PdfJsFeaturesNotification.consume();
    }
    const branch = Services.prefs.getBranch("pdfjs.");
    for (const [key, value] of Object.entries(data)) {
      if (!this.#mutablePreferences.has(key)) {
        continue;
      }
      switch (branch.getPrefType(key)) {
        case Services.prefs.PREF_STRING:
          if (typeof value === "string") {
            branch.setStringPref(key, value);
          }
          break;
        case Services.prefs.PREF_INT:
          if (Number.isInteger(value)) {
            branch.setIntPref(key, value);
          }
          break;
        case Services.prefs.PREF_BOOL:
          if (typeof value === "boolean") {
            branch.setBoolPref(key, value);
          }
          break;
      }
    }
  }

  _recordExposure() {
    lazy.NimbusFeatures.pdfjs.recordExposureEvent({ once: true });
  }

  _reportTelemetry({ data }) {
    PdfJsTelemetry.report(data);
  }

  async _mlGuess({ data }) {
    const { service, request } = data ?? {};
    if (service !== IMAGE_TO_TEXT_TASK || !validateMLGuessRequest(request)) {
      return null;
    }
    try {
      const now = ChromeUtils.now();

      let response;
      if (stubAltTextInference()) {
        response = { output: "In Automation" };
      } else {
        const engine = await this.#createAIEngine(service, null);
        response = await engine.run(request);
      }

      const time = ChromeUtils.now() - now;
      const length = response?.output.length ?? 0;
      PdfJsTelemetry.report({
        type: "editing",
        data: {
          type: "stamp",
          action: "pdfjs.image.alt_text.model_result",
          data: { time, length },
        },
      });
      return response;
    } catch (e) {
      console.error("Failed to run AI engine", e);
      return { error: true };
    }
  }

  async _loadAIEngine({ data: { service, listenToProgress } }) {
    if (service !== IMAGE_TO_TEXT_TASK) {
      throw new Error("Invalid service");
    }

    if (stubAltTextInference()) {
      PdfJsTelemetry.report({
        type: "editing",
        data: {
          type: "stamp",
          action: "pdfjs.image.alt_text.model_download_start",
        },
      });
      PdfJsTelemetry.report({
        type: "editing",
        data: {
          type: "stamp",
          action: "pdfjs.image.alt_text.model_download_complete",
        },
      });
      return true;
    }

    let hasDownloadStarted = false;
    const self = this;
    const timeoutCallback = () => {
      lazy.clearTimeout(timeoutId);
      timeoutId = null;
      if (hasDownloadStarted) {
        PdfJsTelemetry.report({
          type: "editing",
          data: {
            type: "stamp",
            action: "pdfjs.image.alt_text.model_download_error",
          },
        });
      }
      if (!listenToProgress) {
        return;
      }
      self.sendAsyncMessage("PDFJS:Child:handleEvent", {
        type: "loadAIEngineProgress",
        detail: {
          service,
          ok: false,
          finished: true,
        },
      });
    };
    let timeoutId = lazy.setTimeout(timeoutCallback, ML_ENGINE_MAX_TIMEOUT);
    const aggregator = new lazy.MultiProgressAggregator({
      progressCallback({ ok, total, totalLoaded, statusText, type }) {
        if (timeoutId !== null) {
          lazy.clearTimeout(timeoutId);
          timeoutId = lazy.setTimeout(timeoutCallback, ML_ENGINE_MAX_TIMEOUT);
        } else {
          // The timeout has already fired, so we don't need to do anything.
          this.progressCallback = null;
          return;
        }
        if (
          !hasDownloadStarted &&
          type === lazy.Progress.ProgressType.DOWNLOAD
        ) {
          hasDownloadStarted = true;
          PdfJsTelemetry.report({
            type: "editing",
            data: {
              type: "stamp",
              action: "pdfjs.image.alt_text.model_download_start",
            },
          });
        }
        const finished = statusText === lazy.Progress.ProgressStatusText.DONE;
        if (listenToProgress) {
          self.sendAsyncMessage("PDFJS:Child:handleEvent", {
            type: "loadAIEngineProgress",
            detail: {
              service,
              ok,
              total,
              totalLoaded,
              finished,
            },
          });
        }
        if (finished) {
          if (
            hasDownloadStarted &&
            type === lazy.Progress.ProgressType.DOWNLOAD
          ) {
            PdfJsTelemetry.report({
              type: "editing",
              data: {
                type: "stamp",
                action: `pdfjs.image.alt_text.model_download_${
                  ok ? "complete" : "error"
                }`,
              },
            });
          }

          lazy.clearTimeout(timeoutId);
          // Once we're done, we can remove the progress callback.
          this.progressCallback = null;
        }
      },
      watchedTypes: [
        lazy.Progress.ProgressType.DOWNLOAD,
        lazy.Progress.ProgressType.LOAD_FROM_CACHE,
      ],
    });
    return !!(await this.#createAIEngine(service, aggregator));
  }

  async _mlDelete({ data: service }) {
    if (service !== IMAGE_TO_TEXT_TASK) {
      return null;
    }
    PdfJsTelemetry.report({
      type: "editing",
      data: {
        type: "stamp",
        action: "pdfjs.image.alt_text.model_deleted",
      },
    });
    if (Cu.isInAutomation) {
      return null;
    }
    try {
      await lazy.MLUninstallService.uninstall({
        engineIds: [lazy.PdfJsGuessAltTextFeature.engineId],
        // Used only for attribution/telemetry; the specific value is not significant.
        actor: "pdfjs",
      });
    } catch (e) {
      console.error("Failed to delete AI model", e);
    }

    return null;
  }

  async #createAIEngine(taskName, aggregator) {
    try {
      return await lazy.createEngine(
        {
          engineId: lazy.PdfJsGuessAltTextFeature.engineId,
          featureId: lazy.PdfJsGuessAltTextFeature.id,
          taskName,
          backend: "best-onnx",
        },
        aggregator?.aggregateCallback.bind(aggregator) || null
      );
    } catch (e) {
      console.error("Failed to create AI engine", e);
      return null;
    }
  }

  _saveURL(aMsg) {
    const { blobUrl, originalUrl, filename, isModified } = aMsg.data;
    this.browser.documentGlobal.saveURL(
      blobUrl /* aURL */,
      originalUrl /* aOriginalURL */,
      filename /* aFileName */,
      null /* aFilePickerTitleKey */,
      true /* aShouldBypassCache */,
      false /* aSkipPrompt */,
      null /* aReferrerInfo */,
      null /* aCookieJarSettings*/,
      null /* aSourceDocument */,
      lazy.PrivateBrowsingUtils.isBrowserPrivate(
        this.browser
      ) /* aIsContentWindowPrivate */,
      Services.scriptSecurityManager.getSystemPrincipal() /* aPrincipal */,
      () => {
        if (blobUrl.startsWith("blob:")) {
          URL.revokeObjectURL(blobUrl);
        }
        Services.obs.notifyObservers(null, "pdfjs:saveComplete");
      } /* aSaveCompleteCallback */,
      isModified /* aPreferSourceDirectory */
    );
  }

  _updateControlState(aMsg) {
    let data = aMsg.data;
    let browser = this.browser;
    let tabbrowser = browser.getTabBrowser();
    let tab = tabbrowser.getTabForBrowser(browser);
    tabbrowser.getFindBar(tab).then(fb => {
      if (!fb) {
        // The tab or window closed.
        return;
      }
      fb.updateControlState(data.result, data.findPrevious);

      if (
        data.result === Ci.nsITypeAheadFind.FIND_FOUND ||
        data.result === Ci.nsITypeAheadFind.FIND_WRAPPED ||
        (data.result === Ci.nsITypeAheadFind.FIND_PENDING &&
          !this._findFailedString)
      ) {
        this._findFailedString = null;
        lazy.SetClipboardSearchString(data.rawQuery);
      } else if (!this._findFailedString) {
        this._findFailedString = data.rawQuery;
        lazy.SetClipboardSearchString(data.rawQuery);
      }

      let searchLengthened;
      switch (data.result) {
        case Ci.nsITypeAheadFind.FIND_NOTFOUND:
          searchLengthened =
            data.rawQuery.length > this._lastNotFoundStringLength;
          this._lastNotFoundStringLength = data.rawQuery.length;

          if (searchLengthened && !data.entireWord) {
            playSound("not-found");
          }
          break;
        case Ci.nsITypeAheadFind.FIND_WRAPPED:
          playSound("wrapped");
          break;
        case Ci.nsITypeAheadFind.FIND_PENDING:
          break;
        default:
          this._lastNotFoundStringLength = 0;
      }

      const matchesCount = this._requestMatchesCount(data.matchesCount);
      fb.onMatchesCountResult(matchesCount);
    });
  }

  /**
   * Handle the response for extracting text.
   *
   * @param {{ data: { text: string, requestId: number } }}
   */
  _reportText({ data }) {
    const resolve = this.#textRequests.get(data.requestId);
    this.#textRequests.delete(data.requestId);
    if (!resolve) {
      console.error(
        "Unable to find the text content request",
        data.requestId,
        this.#textRequests
      );
      return;
    }
    resolve(data.text);
  }

  _updateMatchesCount(aMsg) {
    let data = aMsg.data;
    let browser = this.browser;
    let tabbrowser = browser.getTabBrowser();
    let tab = tabbrowser.getTabForBrowser(browser);
    tabbrowser.getFindBar(tab).then(fb => {
      if (!fb) {
        // The tab or window closed.
        return;
      }
      const matchesCount = this._requestMatchesCount(data);
      fb.onMatchesCountResult(matchesCount);
    });
  }

  _requestMatchesCount(data) {
    if (!data) {
      return { current: 0, total: 0 };
    }
    let result = {
      current: data.current,
      total: data.total,
      limit:
        typeof lazy.matchesCountLimit === "number" ? lazy.matchesCountLimit : 0,
    };
    if (result.total > result.limit) {
      result.total = -1;
    }
    return result;
  }

  handleEvent(aEvent) {
    const type = aEvent.type;
    // Handle the tab find initialized event specially:
    if (type == "TabFindInitialized") {
      let browser = aEvent.target.linkedBrowser;
      this._hookupEventListeners(browser);
      aEvent.target.removeEventListener(type, this);
      return;
    }

    if (type == "SwapDocShells") {
      this._removeEventListener();
      let newBrowser = aEvent.detail;
      newBrowser.addEventListener(
        "EndSwapDocShells",
        () => {
          this._hookupEventListeners(newBrowser);
        },
        { once: true }
      );
      return;
    }

    // Ignore events findbar events which arrive while the PdfJs document is in
    // the BFCache.
    if (this.windowContext.isInBFCache) {
      return;
    }

    // To avoid forwarding the message as a CPOW, create a structured cloneable
    // version of the event for both performance, and ease of usage, reasons.
    let detail = null;
    if (type !== "findbarclose") {
      detail = {
        query: aEvent.detail.query,
        caseSensitive: aEvent.detail.caseSensitive,
        entireWord: aEvent.detail.entireWord,
        highlightAll: aEvent.detail.highlightAll,
        findPrevious: aEvent.detail.findPrevious,
        matchDiacritics: aEvent.detail.matchDiacritics,
      };
    }

    let browser = aEvent.currentTarget.browser;
    if (!this._boundToFindbar) {
      throw new Error(
        "FindEventManager was not bound for the current browser."
      );
    }
    // Open and focus the findbar so Escape can clear PDF highlights.
    const findbar = aEvent.currentTarget;
    if (type === "findagain" && findbar.hidden) {
      findbar.open(findbar.FIND_NORMAL);
      findbar._findField.select();
      findbar._findField.focus();
    }
    browser.sendMessageToActor(
      "PDFJS:Child:handleEvent",
      { type, detail },
      "PdfJs"
    );
    aEvent.preventDefault();
  }

  _addEventListener() {
    let browser = this.browser;
    if (this._boundToFindbar) {
      throw new Error(
        "FindEventManager was bound 2nd time without unbinding it first."
      );
    }

    this._hookupEventListeners(browser);
  }

  /**
   * Either hook up all the find event listeners if a findbar exists,
   * or listen for a find bar being created and hook up event listeners
   * when it does get created.
   */
  _hookupEventListeners(aBrowser) {
    let tabbrowser = aBrowser.getTabBrowser();
    let tab = tabbrowser.getTabForBrowser(aBrowser);
    let findbar = tabbrowser.getCachedFindBar(tab);
    if (findbar) {
      // And we need to start listening to find events.
      for (var i = 0; i < gFindTypes.length; i++) {
        var type = gFindTypes[i];
        findbar.addEventListener(type, this, true);
      }
      this._boundToFindbar = findbar;
    } else {
      tab.addEventListener("TabFindInitialized", this);
    }
    aBrowser.addEventListener("SwapDocShells", this);
    return !!findbar;
  }

  _removeEventListener() {
    let browser = this.browser;

    // make sure the listener has been removed.
    let findbar = this._boundToFindbar;
    if (findbar) {
      // No reason to listen to find events any longer.
      for (var i = 0; i < gFindTypes.length; i++) {
        var type = gFindTypes[i];
        findbar.removeEventListener(type, this, true);
      }
    } else if (browser) {
      // If we registered a `TabFindInitialized` listener which never fired,
      // make sure we remove it.
      let tabbrowser = browser.getTabBrowser();
      let tab = tabbrowser.getTabForBrowser(browser);
      tab?.removeEventListener("TabFindInitialized", this);
    }

    this._boundToFindbar = null;

    // Clean up any SwapDocShells event listeners.
    browser?.removeEventListener("SwapDocShells", this);
  }
}
