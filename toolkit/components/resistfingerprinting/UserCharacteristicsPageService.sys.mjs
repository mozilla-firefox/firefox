/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

/* global CompressionStream */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  HiddenBrowserManager: "resource://gre/modules/HiddenFrame.sys.mjs",
  setTimeout: "resource://gre/modules/Timer.sys.mjs",
  clearTimeout: "resource://gre/modules/Timer.sys.mjs",
  ProcessType: "resource://gre/modules/ProcessType.sys.mjs",
});

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

ChromeUtils.defineLazyGetter(lazy, "console", () => {
  return console.createInstance({
    logModule: "UserCharacteristicsPage",
    maxLogLevelPref: "toolkit.telemetry.user_characteristics_ping.logLevel",
  });
});

ChromeUtils.defineLazyGetter(lazy, "contentPrefs", () => {
  return Cc["@mozilla.org/content-pref/service;1"].getService(
    Ci.nsIContentPrefService2
  );
});

ChromeUtils.defineLazyGetter(lazy, "isAndroid", () => {
  return Services.appinfo.OS === "Android";
});

ChromeUtils.defineLazyGetter(lazy, "windowType", () => {
  return lazy.isAndroid ? "navigator:geckoview" : "navigator:browser";
});

export class UserCharacteristicsPageService {
  classId = Components.ID("{ce3e9659-e311-49fb-b18b-7f27c6659b23}");
  QueryInterface = ChromeUtils.generateQI([
    "nsIUserCharacteristicsPageService",
  ]);

  _initialized = false;
  _isParentProcess = false;

  /**
   * A map of hidden browsers to a resolve function that should be passed the
   * actor that was created for the browser.
   *
   * @type {WeakMap<Browser, function(PageDataParent): void>}
   */
  _backgroundBrowsers = new WeakMap();

  constructor() {
    lazy.console.debug("Init");

    if (
      Services.appinfo.processType !== Services.appinfo.PROCESS_TYPE_DEFAULT
    ) {
      throw new Error(
        "Shouldn't init UserCharacteristicsPage in content processes."
      );
    }

    // Return if we have initiated.
    if (this._initialized) {
      lazy.console.warn("preventing re-initilization...");
      return;
    }
    this._initialized = true;
    this.handledErrors = [];
    // Needed to collect canvas renderings from two different places for unified filtering
    this.allCanvasData = new Map();
  }

  shutdown() {}

  createContentPage(principal) {
    lazy.console.debug("called createContentPage");

    lazy.console.debug("Registering actor");
    ChromeUtils.registerWindowActor("UserCharacteristics", {
      parent: {
        esModuleURI:
          "moz-src:///toolkit/actors/UserCharacteristicsParent.sys.mjs",
      },
      child: {
        esModuleURI:
          "moz-src:///toolkit/actors/UserCharacteristicsChild.sys.mjs",
        events: {
          UserCharacteristicsDataDone: { wantUntrusted: true },
        },
      },
      matches: ["about:fingerprintingprotection"],
      remoteTypes: ["privilegedabout"],
    });

    return lazy.HiddenBrowserManager.withHiddenBrowser(async browser => {
      lazy.console.debug(`In withHiddenBrowser`);
      try {
        const { promise, resolve } = Promise.withResolvers();
        this._backgroundBrowsers.set(browser, resolve);

        const loadURIOptions = {
          triggeringPrincipal: principal,
        };

        const userCharacteristicsPageURI = Services.io.newURI(
          "about:fingerprintingprotection" +
            (Cu.isInAutomation ? "#automation" : "")
        );

        browser.loadURI(userCharacteristicsPageURI, loadURIOptions);

        const data = await promise;
        if (data.debug) {
          lazy.console.debug(`Debugging Output:`);
          for (const line of data.debug) {
            lazy.console.debug(line);
          }
          lazy.console.debug(`(debugging output done)`);
        }
        lazy.console.debug(`Data:`, data.output);

        lazy.console.debug(`Gamepad data:`, data.gamepads);

        lazy.console.debug("Populating Glean metrics...");

        await this.populateAndCollectErrors(browser, data);

        // Notify test observers that the data has been populated.
        Services.obs.notifyObservers(
          null,
          "user-characteristics-populating-data-done"
        );
      } finally {
        lazy.console.debug("Unregistering actors");
        this.cleanUp();
        lazy.console.debug("Cleanup done");
        this._backgroundBrowsers.delete(browser);
        lazy.console.debug("Background browser removed");
      }
    });
  }

  cleanUp() {
    ChromeUtils.unregisterWindowActor("UserCharacteristics");
    // unregisterWindowActor doesn't throw if the actor is not registered.
    // (Do note it console.error's but doesn't throw)
    // We can safely double unregister. We do this to handle the case where
    // the actor was registered but the function it was registered timed out.
    ChromeUtils.unregisterWindowActor("UserCharacteristicsWindowInfo");
    ChromeUtils.unregisterWindowActor("UserCharacteristicsCanvasRendering");
  }

  async populateAndCollectErrors(browser, data) {
    // List of functions to populate Glean metrics
    const populateFuncs = [
      [this.populateIntlLocale, []],
      [this.populateZoomPrefs, []],
      [this.populateDisabledMediaPrefs, []],
      [this.populateMathOps, []],
      [this.populateMathOpsFdlibm2, []],
      [this.populateMappableData, [data.output]],
      [this.populateGamepads, [data.gamepads]],
      [this.populateClientInfo, []],
      [this.populateCPUInfo, []],
      [this.populateScreenInfo, []],
      [this.populatePointerInfo, []],
      [this.initWindowInfoActor, []],
      [this.populateWebGlInfo, [browser, 1, false]],
      [this.populateWebGlInfo, [browser, 1, true]],
      [this.populateWebGlInfo, [browser, 2, false]],
      [this.populateWebGlInfo, [browser, 2, true]],
      [this.populateCanvasData, []],
      [this.populateWebGPUProperties, [browser.documentGlobal]],
      [this.populateUserAgent, [browser.documentGlobal]],
    ];
    // Bind them to the class and run them in parallel.
    // Timeout if any of them takes too long (5 minutes).
    const results = await Promise.allSettled(
      populateFuncs.map(([f, args]) =>
        timeoutPromise(f.bind(this)(...args), 5 * 60 * 1000)
      )
    );

    // data?.output?.jsErrors is previous errors that happened in usercharacteristics.js
    const errors = JSON.parse(data?.output?.jsErrors ?? "[]");
    for (const [i, [func]] of populateFuncs.entries()) {
      if (results[i].status == "rejected") {
        const error = `${func.name}: ${await stringifyError(
          results[i].reason
        )}`;
        errors.push(error);
        lazy.console.debug(error);
      }
    }

    // After all populate functions complete, apply unified canvas filtering
    // This is where ALL canvas data (Canvas 2D + WebGL) is filtered together
    try {
      await this.filterAndPopulateAllCanvasData();
    } catch (e) {
      const error = `filterAndPopulateAllCanvasData: ${await stringifyError(
        e
      )}`;
      errors.push(error);
      lazy.console.debug(error);
    }

    errors.push(...this.handledErrors);

    Glean.characteristics.jsErrors.set(JSON.stringify(errors));
  }

  // Compress strings that are all the same repeated character
  // e.g., "AAAAAAA" (length 7) becomes "[A * 7]"
  compressRepeatedChars(value) {
    if (typeof value !== "string" || value.length < 10) {
      return value;
    }
    const firstChar = value[0];
    if (value.split("").every(c => c === firstChar)) {
      return `[${firstChar} * ${value.length}]`;
    }
    return value;
  }

  async collectGleanMetricsFromMap(
    data,
    { prefix = "", suffix = "", operation = "set" } = {}
  ) {
    const entries = data instanceof Map ? data.entries() : Object.entries(data);
    for (const [key, value] of entries) {
      const metricName = prefix + key + suffix;
      const processedValue = this.compressRepeatedChars(value);
      try {
        Glean.characteristics[metricName][operation](processedValue);
      } catch (e) {
        lazy.console.error(
          `ERROR setting metric "${metricName}" (prefix: "${prefix}", key: "${key}", suffix: "${suffix}"):`,
          e
        );
        throw e;
      }
    }
  }

  *getActorFromTabsOrWindows(windows, name, diagnostics = {}) {
    for (const win of windows) {
      diagnostics.winCount++;
      if (win.closed) {
        diagnostics.closed++;
        continue;
      }

      if (lazy.isAndroid) {
        diagnostics.tabCount++;
        try {
          const actor = win.moduleManager.getActor(name);
          diagnostics.noActor += !actor;
          yield { success: !!actor, actor };
        } catch (e) {
          diagnostics.noActor++;
          yield { success: false, error: e };
        }

        continue;
      }

      for (const tab of win.gBrowser.tabs) {
        diagnostics.tabCount++;
        diagnostics.remoteTypes?.push(
          sanitizeRemoteType(tab.linkedBrowser.remoteType)
        );
        try {
          const actor =
            tab.linkedBrowser.browsingContext?.currentWindowGlobal.getActor(
              name
            );
          diagnostics.noActor += !actor;
          yield {
            success: !!actor,
            actor,
          };
        } catch (e) {
          diagnostics.noActor++;
          yield { success: false, error: e };
        }
      }
    }
  }

  async initWindowInfoActor() {
    // We use two different methods to get any loaded document.
    // First one is, DOMContentLoaded event. If the user loads
    // a new document after actor registration, we will get it.
    // Second one is, we iterate over all open windows and tabs
    // and try to get the screen info from them.
    // The reason we do both is, for DOMContentLoaded, we can't
    // guarantee that all the documents were not loaded before the
    // actor registration.
    // We could only use the second method and add a load event
    // listener, but that assumes the user won't close already
    // existing tabs and continue on a new one before the page
    // is loaded. This is a rare case, but we want to cover it.

    Services.obs.addObserver(function observe(_subject, topic, _data) {
      Services.obs.removeObserver(observe, topic);
      ChromeUtils.unregisterWindowActor(actorName);
    }, "user-characteristics-window-info-done");

    const actorName = "UserCharacteristicsWindowInfo";
    ChromeUtils.registerWindowActor(actorName, {
      parent: {
        esModuleURI:
          "moz-src:///toolkit/actors/UserCharacteristicsParent.sys.mjs",
      },
      child: {
        esModuleURI:
          "moz-src:///toolkit/actors/UserCharacteristicsWindowInfoChild.sys.mjs",
        events: {
          DOMContentLoaded: {},
        },
      },
      safeForUntrustedWebProcess: true,
    });

    for (const { success, actor, error } of this.getActorFromTabsOrWindows(
      Services.wm.getEnumerator(lazy.windowType),
      actorName
    )) {
      if (success) {
        actor.sendAsyncMessage("WindowInfo:PopulateFromDocument");
      } else if (error) {
        lazy.console.error("Error getting actor", error);
        this.handledErrors.push(await stringifyError(error));
      }
    }
  }

  async populateScreenInfo() {
    const { promise, resolve } = Promise.withResolvers();

    Services.obs.addObserver(function observe(_subject, topic, data) {
      Services.obs.removeObserver(observe, topic);
      resolve(JSON.parse(data));
    }, "user-characteristics-screen-info-done");

    await promise.then(data => this.collectGleanMetricsFromMap(data));
  }

  async populatePointerInfo() {
    const { promise, resolve } = Promise.withResolvers();

    Services.obs.addObserver(function observe(_subject, topic, data) {
      Services.obs.removeObserver(observe, topic);
      resolve(JSON.parse(data));
    }, "user-characteristics-pointer-info-done");

    await promise.then(data => this.collectGleanMetricsFromMap(data));
  }

  async filterAndPopulateAllCanvasData() {
    // Apply unified filtering to all canvas data (Canvas 2D + WebGL)
    // This sees ALL canvas data at once before populating any Glean metrics
    lazy.console.debug(
      `Applying unified filtering to all canvas data (${this.allCanvasData.size} entries)`
    );

    const filteredCanvasData = await this.filterAllCanvasRawData(
      this.allCanvasData
    );

    // Populate all canvas metrics with filtered data
    this.collectGleanMetricsFromMap(filteredCanvasData);
  }

  // This function is different from above to allow us to return early in
  // certain circumstances.  After we return we still need to populate the Glean
  // metrics, so we do this in a wrapper function
  async filterAllCanvasRawData(allCanvasData) {
    // Check if we should skip compression (test mode)
    const skipCompression = Services.prefs.getBoolPref(
      "toolkit.telemetry.user_characteristics_ping.test_skip_compression",
      false
    );

    // Compress all raw data and cache both sizes and compressed data
    const compressedData = new Map(); // key -> { size, data (base64) }

    lazy.console.debug("Raw canvas data sizes:");
    for (const [key, value] of allCanvasData.entries()) {
      if (key.endsWith("Raw")) {
        const size = value ? value.length : 0;

        let compressedSize = 0;
        let compressedBase64 = value; // Default to original if compression fails/skipped
        if (value && !skipCompression) {
          try {
            const encoder = new TextEncoder();
            const bytes = encoder.encode(value);
            const cs = new CompressionStream("gzip");
            const writer = cs.writable.getWriter();
            writer.write(bytes);
            writer.close();

            const reader = cs.readable.getReader();
            const chunks = [];
            while (true) {
              const { done, value: chunk } = await reader.read();
              if (done) {
                break;
              }
              chunks.push(chunk);
            }

            // Combine chunks into single Uint8Array
            const totalLength = chunks.reduce((acc, c) => acc + c.length, 0);
            const compressed = new Uint8Array(totalLength);
            let offset = 0;
            for (const chunk of chunks) {
              compressed.set(chunk, offset);
              offset += chunk.length;
            }

            // Convert to base64 for storage in Glean
            compressedBase64 = btoa(String.fromCharCode(...compressed));
            compressedSize = compressedBase64.length;
          } catch (e) {
            lazy.console.debug(`  Error compressing ${key}: ${e}`);
            compressedSize = size; // Fall back to uncompressed size
            compressedBase64 = value;
          }
        } else if (skipCompression) {
          compressedSize = size;
        }

        // Cache the compressed size and data for later use
        compressedData.set(key, {
          size: compressedSize,
          data: compressedBase64,
        });

        lazy.console.debug(
          `  ${key}: ${size} bytes (compressed: ${compressedSize} bytes, ${((compressedSize / size) * 100).toFixed(1)}%)`
        );
      }
    }

    // -----------------------------------------------------------
    // First go through and decide what we want to keep based on the value
    // and the collect probability.

    // Check if we should ignore probability filtering
    const ignoreProbability = Services.prefs.getBoolPref(
      "toolkit.telemetry.user_characteristics_ping.ignore_canvas_probability",
      false
    );

    // Import canvas hash lookup functions and length
    const { isKnownHash, getHashProbability, CANVAS_HASH_LENGTH } =
      ChromeUtils.importESModule(
        "resource://gre/modules/CanvasHashData.sys.mjs"
      );

    // Iterate through all canvas metrics (1-13) for both HW and SW
    // Use camelCase naming to match Glean conventions
    const canvasMetrics = [
      "canvasdata1",
      "canvasdata2",
      "canvasdata3",
      "canvasdata4",
      "canvasdata5",
      "canvasdata6",
      "canvasdata7",
      "canvasdata8",
      "canvasdata9",
      "canvasdata10",
      "canvasdata11Webgl",
      "canvasdata12Fingerprintjs1",
      "canvasdata13Fingerprintjs2",
    ];

    const variants = [
      { key: "", suffix: "" }, // HW
      { key: "Software", suffix: "Software" }, // SW
    ];

    for (const metric of canvasMetrics) {
      for (const variant of variants) {
        // Construct the key names (all camelCase)
        // Examples:
        //   - canvasdata1, canvasdata1Software
        //   - canvasdata11Webgl, canvasdata11WebglSoftware
        //   - canvasdata12Fingerprintjs1, canvasdata12Fingerprintjs1Software
        const hashKey = metric + variant.suffix;
        const rawKey = hashKey + "Raw";

        // Check if this raw data exists
        if (!allCanvasData.has(rawKey)) {
          continue;
        }

        // Get the hash value
        const hash = allCanvasData.get(hashKey);
        if (!hash) {
          // No hash, remove the raw data
          allCanvasData.delete(rawKey);
          continue;
        }

        // Determine if we should keep this raw data based on probability
        // Use substring of hash for comparison (trimmed hash)
        const hashSubstring =
          CANVAS_HASH_LENGTH > 0 && hash.length >= CANVAS_HASH_LENGTH
            ? hash.substring(0, CANVAS_HASH_LENGTH)
            : hash;

        const buf = new Uint32Array(1);
        crypto.getRandomValues(buf);
        const rand = buf[0] / 0x100000000; // [0,1)

        let shouldKeep;
        if (isKnownHash(hashSubstring)) {
          // Known hash - sample using cryptographically secure randomness
          // getHashProbability applies channel-specific multiplier
          const probability = getHashProbability(hashSubstring);
          shouldKeep = rand < probability;
        } else {
          // Unknown hash - use 1/10 sampling rate
          shouldKeep = rand < 1 / 10;
        }

        if (ignoreProbability) {
          if (!shouldKeep) {
            lazy.console.debug(
              `Canvas data ${hashKey} would have been excluded (probability filter), but keeping due to ignore_canvas_probability pref`
            );
          }
          shouldKeep = true;
        } else if (!shouldKeep) {
          lazy.console.debug(
            `Canvas data ${hashKey} excluded by probability filter (hash: ${hashSubstring})`
          );
        }

        if (!shouldKeep) {
          allCanvasData.delete(rawKey);
        }
      }
    }

    // -----------------------------------------------------------
    // Collect all raw data keys with their cached compressed sizes for budget filtering
    const rawDataEntries = [];
    for (const [key, value] of allCanvasData.entries()) {
      if (key.endsWith("Raw")) {
        if (!value) {
          continue;
        }

        // Use cached compressed data from earlier compression
        const cached = compressedData.get(key);
        const compressedSize = cached?.size || value.length;

        rawDataEntries.push({
          key,
          size: compressedSize,
          uncompressedSize: value.length,
        });
      }
    }

    if (rawDataEntries.length === 0) {
      return allCanvasData; // No raw data to filter
    }

    const totalUncompressed = rawDataEntries.reduce(
      (sum, e) => sum + e.uncompressedSize,
      0
    );
    const totalCompressed = rawDataEntries.reduce((sum, e) => sum + e.size, 0);
    lazy.console.debug(
      `Found ${rawDataEntries.length} raw canvas entries (Canvas 2D + WebGL), ` +
        `uncompressed: ~${Math.round(totalUncompressed / 1024)}KB, ` +
        `compressed: ~${Math.round(totalCompressed / 1024)}KB ` +
        `(${((totalCompressed / totalUncompressed) * 100).toFixed(1)}%)`
    );

    // -----------------------------------------------------------
    // Budget: ~170KB total for ALL raw canvas data (Canvas 2D + WebGL)
    // TODO: Subject to change
    const MAX_BUDGET_BYTES = 170 * 1024;

    // Priority algorithm:
    // 1. canvasdata01 (if doesn't match, include it - prioritize software over hardware)
    // 2. canvasdata02 (same logic)
    // 3. canvasdata09 and canvasdata10
    // 4. Remaining images picked at random

    const getBaseName = key => {
      // Extract base name: "canvasdata1Raw" -> "canvasdata1"
      // "canvasdata1SoftwareRaw" -> "canvasdata1"
      // "canvasdata11WebglRaw" -> "canvasdata11Webgl"
      // "canvasdata11WebglSoftwareRaw" -> "canvasdata11Webgl"
      return key.replace(/Raw$/, "").replace(/Software$/, "");
    };

    const isSoftware = key => key.includes("Software");

    // Map to store random priorities for non-priority canvases
    const randomPriorities = new Map();

    const getPriority = key => {
      const baseName = getBaseName(key);
      const soft = isSoftware(key);

      // Priority 0-1: canvasdata1 (software first, then hardware)
      if (baseName === "canvasdata1") {
        return soft ? 0 : 1;
      }
      // Priority 2-3: canvasdata2 (software first, then hardware)
      if (baseName === "canvasdata2") {
        return soft ? 2 : 3;
      }
      // Priority 4-5: canvasdata9 (software first, then hardware)
      if (baseName === "canvasdata9") {
        return soft ? 4 : 5;
      }
      // Priority 6-7: canvasdata10 (software first, then hardware)
      if (baseName === "canvasdata10") {
        return soft ? 6 : 7;
      }
      // Priority 8+: All others - use random value for shuffling
      if (!randomPriorities.has(key)) {
        randomPriorities.set(key, 8 + Math.random());
      }
      return randomPriorities.get(key);
    };

    // Sort by priority
    rawDataEntries.sort((a, b) => {
      const priorityA = getPriority(a.key);
      const priorityB = getPriority(b.key);
      return priorityA - priorityB;
    });

    // Apply budget by removing lowest-priority items
    // Use compressed sizes since we store compressed data in the ping
    let totalSize = rawDataEntries.reduce((sum, e) => sum + e.size, 0);
    const keysToRemove = [];

    while (totalSize > MAX_BUDGET_BYTES && rawDataEntries.length) {
      const removed = rawDataEntries.pop(); // Remove lowest priority
      keysToRemove.push(removed.key);
      totalSize -= removed.size;
    }

    // Build result map with compressed data replacing raw data
    const result = new Map(allCanvasData);

    // Remove entries that exceeded budget
    for (const key of keysToRemove) {
      result.delete(key);
      lazy.console.debug(`Budget exceeded, removing: ${key}`);
    }

    // Replace remaining raw data with compressed versions
    for (const [key, cached] of compressedData.entries()) {
      if (result.has(key) && cached?.data) {
        result.set(key, cached.data);
      }
    }

    lazy.console.debug(
      `Budget filtering complete: kept ${rawDataEntries.length} entries, ` +
        `removed ${keysToRemove.length}, total compressed size: ~${Math.round(totalSize / 1024)}KB`
    );
    return result;
  }

  async populateCanvasData() {
    const actorName = "UserCharacteristicsCanvasRendering";
    ChromeUtils.registerWindowActor(actorName, {
      parent: {
        esModuleURI:
          "moz-src:///toolkit/actors/UserCharacteristicsParent.sys.mjs",
      },
      child: {
        esModuleURI:
          "moz-src:///toolkit/actors/UserCharacteristicsCanvasRenderingChild.sys.mjs",
      },
      safeForUntrustedWebProcess: true,
    });

    let data = new Map();
    // Returns true if we need to try again
    const attemptRender = async allowSoftwareRenderer => {
      const diagnostics = {
        winCount: 0,
        tabCount: 0,
        closed: 0,
        noActor: 0,
        noDebugInfo: 0,
        notHW: 0,
        remoteTypes: [],
      };
      // Try to find a window that supports hardware rendering
      let acceleratedActor = null;
      let fallbackActor = null;
      for (const { success, actor, error } of this.getActorFromTabsOrWindows(
        Services.wm.getEnumerator(lazy.windowType),
        actorName,
        diagnostics
      )) {
        if (!success) {
          if (error) {
            lazy.console.error("Error getting actor", error);
            this.handledErrors.push(await stringifyError(error));
          }
          continue;
        }

        // Example data: {"backendType":3,"drawTargetType":0,"isAccelerated":false,"isShared":true}
        const debugInfo = await timeoutPromise(
          actor.sendQuery("CanvasRendering:GetDebugInfo"),
          5000
        ).catch(async e => {
          lazy.console.error("Canvas rendering debug info failed", e);
          this.handledErrors.push(await stringifyError(e));
        });
        if (!debugInfo) {
          diagnostics.noDebugInfo++;
          continue;
        }

        lazy.console.debug("Canvas rendering debug info", debugInfo);

        fallbackActor = actor;
        if (debugInfo.isAccelerated) {
          acceleratedActor = actor;
          break;
        }
        diagnostics.notHW++;
      }

      // If we didn't find a hardware accelerated window, we use the last one
      const actor = acceleratedActor || fallbackActor;

      if (!actor) {
        lazy.console.error("No actor found for canvas rendering");
        // There's no actor/window to render canvases
        return { error: { message: "NO_ACTOR", diagnostics }, retry: false };
      }

      // We have an actor, hw accelerated or not
      // Ask it to render the canvases.
      // Timeout after 1 minute to give multiple
      // chances to render canvases.
      if (allowSoftwareRenderer || acceleratedActor) {
        try {
          data = await timeoutPromise(
            actor.sendQuery("CanvasRendering:Render", {
              hwRenderingExpected: !!acceleratedActor,
            }),
            1 * 60 * 1000
          );
        } catch (e) {
          lazy.console.error(
            "Canvas rendering timed out or actor was destroyed (tab closed etc.)",
            e
          );
          return {
            error: { message: await stringifyError(e), diagnostics },
            retry: true,
          };
        }

        // Successfully rendered at least some canvases, maybe all of them
        lazy.console.debug(
          `Canvases rendered because ${
            acceleratedActor
              ? "we found a hardware accelerated window"
              : "we allowed software rendering"
          }`
        );
        return {
          error: null,
          retry: false,
        };
      }

      // We have a fallback actor, but we don't want to use it for software rendering
      lazy.console.error(
        "No hardware accelerated windows found and software rendering is not allowed"
      );
      return { error: { message: "NO_HW_ACTOR", diagnostics }, retry: true };
    };

    // Try to render canvases
    let result = null;
    let tries = 1;
    while (
      (result = await attemptRender(tries === 5 || Cu.isInAutomation)).retry
    ) {
      // We either don't have hardware accelerated windows or we failed to render canvases.
      this.handledErrors.push(result.error);
      // We allow software rendering on the fifth try.
      tries++;
      // Rendering might fail if the user closes the tab before we render the canvases.
      // Wait for a bit before trying again
      await new Promise(resolve => lazy.setTimeout(resolve, 10 * 1000));
    }

    if (!result.retry && result.error) {
      this.handledErrors.push(result.error);
    }

    // Store Canvas 2D data for unified filtering later
    // Don't populate Glean yet - wait for unified filtering
    const renderings = data.get("renderings");
    if (renderings) {
      for (const [key, value] of renderings.entries()) {
        this.allCanvasData.set(key, value);
      }
    }

    // Store DPR for later population
    this.allCanvasData.set("canvasDpr", data.get("dpr") ?? "");

    ChromeUtils.unregisterWindowActor(actorName);

    // Record the errors
    const errors = data.get("errors");
    if (errors?.length) {
      this.handledErrors.push(...errors);
    }
  }

  async populateZoomPrefs() {
    const zoomPrefsCount = await new Promise(resolve => {
      lazy.contentPrefs.getByName("browser.content.full-zoom", null, {
        _result: 0,
        handleResult(_) {
          this._result++;
        },
        handleCompletion() {
          resolve(this._result);
        },
      });
    });

    Glean.characteristics.zoomCount.set(zoomPrefsCount);
  }

  async populateIntlLocale() {
    const locale = new Intl.DisplayNames(undefined, {
      type: "region",
    }).resolvedOptions().locale;
    Glean.characteristics.intlLocale.set(locale);
  }

  async populateGamepads(gamepads) {
    for (const gamepad of gamepads) {
      Glean.characteristics.gamepads.add(gamepad);
    }
  }

  async populateWebGPUProperties(window) {
    const adapter = await window.navigator.gpu?.requestAdapter();
    if (!adapter) {
      return;
    }

    Glean.characteristics.wgpuMissingFeatures.set(
      adapter.missingFeatures.toString()
    );
    Glean.characteristics.wgpuMaxtexturedimension1d.set(
      adapter.limits.maxTextureDimension1D
    );
    Glean.characteristics.wgpuMaxtexturedimension2d.set(
      adapter.limits.maxTextureDimension2D
    );
    Glean.characteristics.wgpuMaxtexturedimension3d.set(
      adapter.limits.maxTextureDimension3D
    );
    Glean.characteristics.wgpuMaxtexturearraylayers.set(
      adapter.limits.maxTextureArrayLayers
    );
    Glean.characteristics.wgpuMaxbindgroups.set(adapter.limits.maxBindGroups);
    Glean.characteristics.wgpuMaxbindgroupsplusvertexbuffers.set(
      adapter.limits.maxBindGroupsPlusVertexBuffers
    );
    Glean.characteristics.wgpuMaxbindingsperbindgroup.set(
      adapter.limits.maxBindingsPerBindGroup
    );
    Glean.characteristics.wgpuMaxdynamicuniformbuffersperpipelinelayout.set(
      adapter.limits.maxDynamicUniformBuffersPerPipelineLayout
    );
    Glean.characteristics.wgpuMaxdynamicstoragebuffersperpipelinelayout.set(
      adapter.limits.maxDynamicStorageBuffersPerPipelineLayout
    );
    Glean.characteristics.wgpuMaxsampledtexturespershaderstage.set(
      adapter.limits.maxSampledTexturesPerShaderStage
    );
    Glean.characteristics.wgpuMaxsamplerspershaderstage.set(
      adapter.limits.maxSamplersPerShaderStage
    );
    Glean.characteristics.wgpuMaxstoragebufferspershaderstage.set(
      adapter.limits.maxStorageBuffersPerShaderStage
    );
    Glean.characteristics.wgpuMaxstoragetexturespershaderstage.set(
      adapter.limits.maxStorageTexturesPerShaderStage
    );
    Glean.characteristics.wgpuMaxuniformbufferspershaderstage.set(
      adapter.limits.maxUniformBuffersPerShaderStage
    );
    Glean.characteristics.wgpuMaxuniformbufferbindingsize.set(
      adapter.limits.maxUniformBufferBindingSize
    );
    Glean.characteristics.wgpuMaxstoragebufferbindingsize.set(
      adapter.limits.maxStorageBufferBindingSize
    );
    Glean.characteristics.wgpuMinuniformbufferoffsetalignment.set(
      adapter.limits.minUniformBufferOffsetAlignment
    );
    Glean.characteristics.wgpuMinstoragebufferoffsetalignment.set(
      adapter.limits.minStorageBufferOffsetAlignment
    );
    Glean.characteristics.wgpuMaxvertexbuffers.set(
      adapter.limits.maxVertexBuffers
    );
    Glean.characteristics.wgpuMaxbuffersize.set(adapter.limits.maxBufferSize);
    Glean.characteristics.wgpuMaxvertexattributes.set(
      adapter.limits.maxVertexAttributes
    );
    Glean.characteristics.wgpuMaxvertexbufferarraystride.set(
      adapter.limits.maxVertexBufferArrayStride
    );
    Glean.characteristics.wgpuMaxinterstageshadervariables.set(
      adapter.limits.maxInterStageShaderVariables
    );
    Glean.characteristics.wgpuMaxcolorattachments.set(
      adapter.limits.maxColorAttachments
    );
    Glean.characteristics.wgpuMaxcolorattachmentbytespersample.set(
      adapter.limits.maxColorAttachmentBytesPerSample
    );
    Glean.characteristics.wgpuMaxcomputeworkgroupstoragesize.set(
      adapter.limits.maxComputeWorkgroupStorageSize
    );
    Glean.characteristics.wgpuMaxcomputeinvocationsperworkgroup.set(
      adapter.limits.maxComputeInvocationsPerWorkgroup
    );
    Glean.characteristics.wgpuMaxcomputeworkgroupsizex.set(
      adapter.limits.maxComputeWorkgroupSizeX
    );
    Glean.characteristics.wgpuMaxcomputeworkgroupsizey.set(
      adapter.limits.maxComputeWorkgroupSizeY
    );
    Glean.characteristics.wgpuMaxcomputeworkgroupsizez.set(
      adapter.limits.maxComputeWorkgroupSizeZ
    );
    Glean.characteristics.wgpuMaxcomputeworkgroupsperdimension.set(
      adapter.limits.maxComputeWorkgroupsPerDimension
    );

    // Collect adapter metadata
    Glean.characteristics.wgpuIsFallbackAdapter.set(
      adapter.isFallbackAdapter || false
    );
  }

  async populateUserAgent(window) {
    Glean.characteristics.userAgent.set(window.navigator.userAgent);
  }

  async populateMappableData(data) {
    // Store WebGL canvas data for unified filtering later
    // Don't populate WebGL metrics yet - wait for unified filtering
    // Content script keys are already in camelCase format matching filtering logic
    const webglKeys = [
      "canvasdata11Webgl",
      "canvasdata11WebglSoftware",
      "canvasdata11WebglRaw",
      "canvasdata11WebglSoftwareRaw",
    ];

    for (const key of webglKeys) {
      if (data.has(key)) {
        this.allCanvasData.set(key, data.get(key));
      }
    }

    // Handle mathmlDiagValues array (similar to mathOps)
    const mathmlDiagValues = data.get("mathmlDiagValues");
    if (mathmlDiagValues) {
      Glean.characteristics.mathmlDiagValues.set(
        JSON.stringify(mathmlDiagValues)
      );
    }

    // We set non-canvas data from usercharacteristics.js
    // Keys must match to data returned from
    // usercharacteristics.js and the metric defined
    // Note: WebGL canvas data keys (hash and raw) are NOT included here -
    // they go through filterAndPopulateAllCanvasData for unified filtering
    const metrics = {
      set: [
        "voicesCount",
        "voicesLocalCount",
        "voicesDefault",
        "voicesSample",
        "voicesSha1",
        "voicesAllSsdeep",
        "voicesLocalSsdeep",
        "voicesNonlocalSsdeep",
        "mediaCapabilitiesUnsupported",
        "mediaCapabilitiesNotSmooth",
        "mediaCapabilitiesNotEfficient",
        "mediaCapabilitiesH264",
        "audioCompressorGainReduction",
        "audioFingerprint",
        "audioFloatFrequencySum",
        "audioFloatTimeDomainSum",
        "audioFingerprint2",
        "jsErrors",
        "pointerType",
        "anyPointerType",
        "iceSd",
        "iceOrder",
        "motionDecimals",
        "orientationDecimals",
        "orientationabsDecimals",
        "motionFreq",
        "orientationFreq",
        "orientationabsFreq",
        "mathml1",
        "mathml2",
        "mathml3",
        "mathml4",
        "mathml5",
        "mathml6",
        "mathml7",
        "mathml8",
        "mathml9",
        "mathml10",
        "monochrome",
        "cssSystemColors",
        "cssSystemFonts",
        "clientrectsElementGcr01",
        "clientrectsElementGcr02",
        "clientrectsElementGcr03",
        "clientrectsElementGcr04",
        "clientrectsElementGcr05",
        "clientrectsElementGcr06",
        "clientrectsElementGcr07",
        "clientrectsElementGcr08",
        "clientrectsElementGcr09",
        "clientrectsElementGcr10",
        "clientrectsElementGcr11",
        "clientrectsElementGcr12",
        "clientrectsElementGbcr01",
        "clientrectsElementGbcr02",
        "clientrectsElementGbcr03",
        "clientrectsElementGbcr04",
        "clientrectsElementGbcr05",
        "clientrectsElementGbcr06",
        "clientrectsElementGbcr07",
        "clientrectsElementGbcr08",
        "clientrectsElementGbcr09",
        "clientrectsElementGbcr10",
        "clientrectsElementGbcr11",
        "clientrectsElementGbcr12",
        "clientrectsRangeGcr01",
        "clientrectsRangeGcr02",
        "clientrectsRangeGcr03",
        "clientrectsRangeGcr04",
        "clientrectsRangeGcr05",
        "clientrectsRangeGcr06",
        "clientrectsRangeGcr07",
        "clientrectsRangeGcr08",
        "clientrectsRangeGcr09",
        "clientrectsRangeGcr10",
        "clientrectsRangeGcr11",
        "clientrectsRangeGcr12",
        "clientrectsRangeGbcr01",
        "clientrectsRangeGbcr02",
        "clientrectsRangeGbcr03",
        "clientrectsRangeGbcr04",
        "clientrectsRangeGbcr05",
        "clientrectsRangeGbcr06",
        "clientrectsRangeGbcr07",
        "clientrectsRangeGbcr08",
        "clientrectsRangeGbcr09",
        "clientrectsRangeGbcr10",
        "clientrectsRangeGbcr11",
        "clientrectsRangeGbcr12",
        "clientrectsKnownDimensions",
        "clientrectsGhostDimensions",
        "clientrectsEmoji01",
        "clientrectsEmoji02",
        "clientrectsEmoji03",
        "clientrectsEmoji04",
        "clientrectsEmoji05",
        "clientrectsEmoji06",
        "clientrectsTextFontFamily",
        "clientrectsEmojiFontFamily",
        "svgBbox",
        "svgComputedTextLength",
        "svgExtentOfChar",
        "svgSubstringLength",
        "svgEmojiSet",
        "oscpu",
        "pdfViewer",
        "platform",
        "audioFrames",
        "audioRate",
        "audioChannels",
        "audioUniqueSamples",
        "timezoneWeb",
        "timezoneOffsetWeb",
        "sdpCodecList",
        "webauthnCapabilities",
        "storageQuota",
      ],
    };

    for (const type in metrics) {
      for (const metric of metrics[type]) {
        const value = data.get(metric);
        // Populators may omit a field when no valid value is available
        // (e.g. populateVoiceList omits all voices_* fields when the 5s
        // populate timeout wins, so timed-out runs are absent rather than
        // collapsed to sha1("") / count=0).
        if (value === undefined) {
          continue;
        }
        Glean.characteristics[metric][type](value);
      }
    }
  }

  async populateMathOps() {
    // Taken from https://github.com/fingerprintjs/fingerprintjs/blob/da64ad07a9c1728af595068e4a306a4151c5d503/src/sources/math.ts
    // At the time, fingerprintjs was licensed under MIT. Slightly modified to reduce payload size.
    const getResults = () =>
      (() =>
        [
          // Native
          [Math.acos, 0.123124234234234242],
          [Math.acosh, 1e308],
          [Math.asin, 0.123124234234234242],
          [Math.asinh, 1],
          [Math.atanh, 0.5],
          [Math.atan, 0.5],
          [Math.sin, -1e300],
          [Math.sinh, 1],
          [Math.cos, 10.000000000123],
          [Math.cosh, 1],
          [Math.tan, -1e300],
          [Math.tanh, 1],
          [Math.exp, 1],
          [Math.expm1, 1],
          [Math.log1p, 10],
          // Polyfills (I'm not sure if we need polyfills since firefox seem to have all of these operations, but I'll leave it here just in case they yield different values due to chaining)
          [value => Math.pow(Math.PI, value), -100],
          [value => Math.log(value + Math.sqrt(value * value - 1)), 1e154],
          [value => Math.log(value + Math.sqrt(value * value + 1)), 1],
          [value => Math.log((1 + value) / (1 - value)) / 2, 0.5],
          [value => Math.exp(value) - 1 / Math.exp(value) / 2, 1],
          [value => (Math.exp(value) + 1 / Math.exp(value)) / 2, 1],
          [value => Math.exp(value) - 1, 1],
          [value => (Math.exp(2 * value) - 1) / (Math.exp(2 * value) + 1), 1],
          [value => Math.log(1 + value), 10],
        ].map(([op, value]) => [op || (() => 0), value]))().map(([op, value]) =>
        op(value)
      );

    Glean.characteristics.mathOps.set(JSON.stringify(getResults()));

    if (AppConstants.platform === "win") {
      const sandbox = Cu.Sandbox(null, {
        alwaysUseFdlibm: true,
      });
      const results = Cu.evalInSandbox(`(${getResults.toString()})()`, sandbox);
      Glean.characteristics.mathOpsFdlibm.set(JSON.stringify(results));
    }
  }

  async populateMathOpsFdlibm2() {
    // IF YOU ADD ENTRIES TO THIS LIST, PLEASE ALSO UPDATE THE COUNT IN
    // toolkit/components/resistfingerprinting/tests/browser/browser_usercharacteristics_math.js
    const getResults = () => {
      const results = [];

      // Math constants
      results.push(Math.PI);
      results.push(Math.E);
      results.push(Math.SQRT2);
      results.push(Math.SQRT1_2);
      results.push(Math.LN2);
      results.push(Math.LN10);
      results.push(Math.LOG2E);
      results.push(Math.LOG10E);

      // Intermediate values from operation 15: Math.pow(Math.PI, -100)
      results.push(Math.pow(Math.PI, -100));

      // Intermediate values from operation 16: Math.log(1e154 + Math.sqrt(1e154^2 - 1))
      const v16 = 1e154;
      results.push(v16);
      results.push(v16 * v16);
      const sqrt16 = Math.sqrt(v16 * v16 - 1);
      results.push(sqrt16);
      results.push(v16 + sqrt16);

      // Intermediate values from operation 17: Math.log(1 + Math.sqrt(2))
      const sqrt2 = Math.sqrt(2);
      results.push(sqrt2);
      results.push(1 + sqrt2);

      // Intermediate values from operation 18: Math.log((1 + 0.5) / (1 - 0.5)) / 2
      results.push(1 + 0.5);
      results.push(1 - 0.5);
      results.push((1 + 0.5) / (1 - 0.5));

      // Intermediate values from operation 19: Math.exp(1) - 1 / Math.exp(1) / 2
      const exp1 = Math.exp(1);
      results.push(exp1);
      results.push(1 / exp1);
      results.push(1 / exp1 / 2);
      results.push(exp1 - 1 / exp1 / 2);

      // Intermediate values from operation 20: (Math.exp(1) + 1 / Math.exp(1)) / 2
      results.push(exp1 + 1 / exp1);
      results.push((exp1 + 1 / exp1) / 2);

      // Intermediate values from operation 21: Math.exp(1) - 1
      results.push(exp1 - 1);

      // Intermediate values from operation 22: (Math.exp(2) - 1) / (Math.exp(2) + 1)
      const exp2 = Math.exp(2);
      results.push(exp2);
      results.push(exp2 - 1);
      results.push(exp2 + 1);
      results.push((exp2 - 1) / (exp2 + 1));

      // Intermediate values from operation 23: Math.log(1 + 10)
      results.push(1 + 10);

      // Additional illustrative values
      results.push(Math.sqrt(3));
      results.push(Math.sqrt(5));
      results.push(Math.pow(2, 0.5));
      results.push(Math.pow(Math.E, 2));
      results.push(Math.log(Math.E));
      results.push(Math.log(10));

      return results;
    };

    const sandbox = Cu.Sandbox(null, {
      alwaysUseFdlibm: true,
    });
    const results = Cu.evalInSandbox(`(${getResults.toString()})()`, sandbox);
    Glean.characteristics.mathOpsFdlibm2.set(JSON.stringify(results));
  }

  async populateClientInfo() {
    const buildID = Services.appinfo.appBuildID;
    const buildDate =
      Date.UTC(
        buildID.slice(0, 4),
        buildID.slice(4, 6) - 1,
        buildID.slice(6, 8),
        buildID.slice(8, 10),
        buildID.slice(10, 12),
        buildID.slice(12, 14)
      ) / 1000;

    Glean.characteristics.version.set(Services.appinfo.version);
    Glean.characteristics.channel.set(AppConstants.MOZ_UPDATE_CHANNEL);
    Glean.characteristics.osName.set(Services.appinfo.OS);
    Glean.characteristics.osVersion.set(
      Services.sysinfo.getProperty("version")
    );
    if (Services.sysinfo.hasKey("distro")) {
      Glean.characteristics.osDistro.set(
        Services.sysinfo.getProperty("distro")
      );
    }
    if (Services.sysinfo.hasKey("distroVersion")) {
      Glean.characteristics.osDistroVersion.set(
        Services.sysinfo.getProperty("distroVersion")
      );
    }
    if (Services.appinfo.distributionID) {
      Glean.characteristics.osDistroId.set(Services.appinfo.distributionID);
    }
    Glean.characteristics.buildDate.set(buildDate);
  }

  async populateCPUInfo() {
    Glean.characteristics.cpuModel.set(
      await Services.sysinfo.processInfo.then(r => r.name)
    );
    Glean.characteristics.cpuArch.set(Services.sysinfo.get("arch"));

    // Collect Firefox binary architecture (can differ from CPU arch)
    // e.g., x86-64 Firefox via Rosetta 2 on ARM64 Mac
    let binaryArch = "unknown";
    try {
      const xpcomAbi = Services.appinfo.XPCOMABI || "unknown";
      const is64Bit = Services.appinfo.is64Bit ? "true" : "false";
      // Concatenate both values for robustness
      binaryArch = `xpcomabi:${xpcomAbi}|is64bit:${is64Bit}`;
    } catch (e) {
      lazy.console.error("Failed to get Firefox binary architecture:", e);
      binaryArch = "error";
    }
    Glean.characteristics.firefoxBinaryArch.set(binaryArch);
  }

  // Collect WebGL characteristics via the UserCharacteristics content actor
  // (see UserCharacteristicsChild.collectWebGLInfo), which runs the WebGL canvas
  // and getParameter() calls in the content process where content WebGL normally
  // runs. Only the (parent-only) Glean population happens here, using the plain
  // serializable map returned over IPC.
  async populateWebGlInfo(browser, version, forceSoftwareRendering) {
    const windowGlobal = browser.browsingContext?.currentWindowGlobal;
    if (!windowGlobal) {
      lazy.console.error("No currentWindowGlobal for hidden browser");
      return;
    }
    const actor = windowGlobal.getActor("UserCharacteristics");

    const map = await actor.sendQuery("WebGLInfo:Collect", {
      version,
      forceSoftwareRendering,
    });
    if (!map) {
      return;
    }

    this.collectGleanMetricsFromMap(map, {
      prefix: version === 2 ? "gl2" : "gl",
      suffix: forceSoftwareRendering ? "Software" : "",
    });
  }

  async pageLoaded(browsingContext, data) {
    lazy.console.debug(
      `pageLoaded browsingContext=${browsingContext} data=${data}`
    );

    const browser = browsingContext.embedderElement;

    const backgroundResolve = this._backgroundBrowsers.get(browser);
    if (backgroundResolve) {
      backgroundResolve(data);
      return;
    }
    throw new Error(`No backround resolve for ${browser} found`);
  }

  async populateDisabledMediaPrefs() {
    const PREFS = [
      "media.wave.enabled",
      "media.ogg.enabled",
      "media.opus.enabled",
      "media.mp4.enabled",
      "media.hevc.enabled",
      "media.webm.enabled",
      "media.av1.enabled",
      "media.encoder.webm.enabled",
      "media.mediasource.enabled",
      "media.mediasource.vp9.enabled",
    ];

    const defaultBranch = Services.prefs.getDefaultBranch("");
    const changedPrefs = {};
    for (const pref of PREFS) {
      if (Services.prefs.prefHasUserValue(pref)) {
        const value = Services.prefs.getBoolPref(pref);
        if (defaultBranch.getBoolPref(pref) !== value) {
          const key = pref.substring(6).substring(0, pref.length - 8 - 6);
          changedPrefs[key] = value;
        }
      }
    }
    Glean.characteristics.changedMediaPrefs.set(JSON.stringify(changedPrefs));
  }
}

// =============================================================
// Utility Functions

async function stringifyError(error) {
  if (error instanceof Error) {
    const stack = (error.stack ?? "").replaceAll(
      /@chrome.+?UserCharacteristicsPageService.sys.mjs:/g,
      ""
    );
    return `${error.toString()} ${stack}`;
  }
  // A hacky attempt to extract as much as info from error
  const errStr = await (async () => {
    const asStr = await (async () => error.toString())().catch(() => "");
    const asJson = await (async () => JSON.stringify(error))().catch(() => "");
    return asStr.length > asJson.len ? asStr : asJson;
  })();
  return errStr;
}

function timeoutPromise(promise, ms) {
  return new Promise((resolve, reject) => {
    const timeoutId = lazy.setTimeout(() => {
      reject(new Error("TIMEOUT"));
    }, ms);

    promise.then(
      value => {
        lazy.clearTimeout(timeoutId);
        resolve(value);
      },
      error => {
        lazy.clearTimeout(timeoutId);
        reject(error);
      }
    );
  });
}

// Remote type may include current site url, which is not needed.
// We only need the remote type.
// See https://firefox-source-docs.mozilla.org/dom/ipc/process_model.html
// and search for $SITE for processes that have site information.
// We return unknown if it isn't one of the known types.
function sanitizeRemoteType(remoteTypeStr) {
  if (!remoteTypeStr) {
    return "null";
  }

  const remoteTypes = remoteTypeStr.split("=");
  if (remoteTypes.length >= 2) {
    return isValidRemoteType(remoteTypes[0]) ? remoteTypes[0] : "unknown";
  }

  return isValidRemoteType(remoteTypeStr) ? remoteTypeStr : "unknown";
}

function isValidRemoteType(sanitizedRemoteType) {
  return (
    lazy.ProcessType.fluentNameFromProcessTypeString(sanitizedRemoteType) !==
    lazy.ProcessType.kFallback
  );
}

// Export constants for testing
export const MAX_CANVAS_RAW_DATA_BUDGET_BYTES = 170 * 1024;
