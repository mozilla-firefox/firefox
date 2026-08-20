/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const APPLY_CONFIG_TIMEOUT_MS = 60 * 1000;
const HOME_PAGE = "resource://mozscreenshots/lib/mozscreenshots.html";

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { setTimeout } from "resource://gre/modules/Timer.sys.mjs";
import { Rect } from "resource://gre/modules/Geometry.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserTestUtils: "resource://testing-common/BrowserTestUtils.sys.mjs",
  // Screenshot.sys.mjs must be imported this way for xpcshell tests to work
  Screenshot: "resource://mozscreenshots/Screenshot.sys.mjs",
});

export var TestRunner = {
  combos: null,
  completedCombos: 0,
  currentComboIndex: 0,
  _lastCombo: null,
  _libDir: null,
  croppingPadding: 0,
  mochitestScope: null,

  init(extensionPath) {
    this._extensionPath = extensionPath;
    this.setupOS();
  },

  /**
   * Initialize the mochitest interface. This allows TestRunner to integrate
   * with mochitest functions like is(...) and ok(...). This must be called
   * prior to invoking any of the TestRunner functions. Note that this should
   * be properly setup in head.js, so you probably don't need to call it.
   */
  initTest(mochitestScope) {
    this.mochitestScope = mochitestScope;
  },

  setupOS() {
    switch (AppConstants.platform) {
      case "macosx": {
        this.disableNotificationCenter();
        break;
      }
    }
  },

  disableNotificationCenter() {
    let killall = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    killall.initWithPath("/bin/bash");

    let killallP = Cc["@mozilla.org/process/util;1"].createInstance(
      Ci.nsIProcess
    );
    killallP.init(killall);
    let ncPlist =
      "/System/Library/LaunchAgents/com.apple.notificationcenterui.plist";
    let killallArgs = [
      "-c",
      `/bin/launchctl unload -w ${ncPlist} && ` +
        "/usr/bin/killall -v NotificationCenter",
    ];
    killallP.run(true, killallArgs, killallArgs.length);
  },

  /**
   * Load specified sets, execute all combinations of them, and capture screenshots.
   */
  async start(setNames, jobName = null) {
    let subDirs = [
      "mozscreenshots",
      new Date().toISOString().replace(/:/g, "-") + "_" + Services.appinfo.OS,
    ];
    let screenshotPath = PathUtils.join(PathUtils.tempDir, ...subDirs);

    const MOZ_UPLOAD_DIR = Services.env.get("MOZ_UPLOAD_DIR");
    if (MOZ_UPLOAD_DIR) {
      screenshotPath = MOZ_UPLOAD_DIR;
    }

    this.mochitestScope.info(`Saving screenshots to: ${screenshotPath}`);

    let screenshotPrefix = Services.appinfo.appBuildID;
    if (jobName) {
      screenshotPrefix += "-" + jobName;
    }
    screenshotPrefix += "_";
    lazy.Screenshot.init(screenshotPath, this._extensionPath, screenshotPrefix);
    this._libDir = this._extensionPath
      .QueryInterface(Ci.nsIFileURL)
      .file.clone();
    this._libDir.append("chrome");
    this._libDir.append("mozscreenshots");
    this._libDir.append("lib");

    let sets = this.loadSets(setNames);

    this.mochitestScope.info(`${sets.length} sets: ${setNames}`);
    this.combos = new LazyProduct(sets);
    this.mochitestScope.info(this.combos.length + " combinations");

    this.currentComboIndex = this.completedCombos = 0;
    this._lastCombo = null;

    // Setup some prefs
    Services.prefs.setCharPref(
      "extensions.ui.lastCategory",
      "addons://list/extension"
    );
    // Don't let the caret blink since it causes false positives for image diffs
    Services.prefs.setIntPref("ui.caretBlinkTime", -1);
    // Disable some animations that can cause false positives, such as the
    // reload/stop button spinning animation.
    Services.prefs.setIntPref("ui.prefersReducedMotion", 1);

    let browserWindow = Services.wm.getMostRecentWindow("navigator:browser");

    // Prevent the mouse cursor from causing hover styles or tooltips to appear.
    browserWindow.windowUtils.disableNonTestMouseEvents(true);

    // When being automated through Marionette, Firefox shows a prominent indication
    // in the urlbar and identity block. We don't want this to show when testing browser UI.
    // Note that this doesn't prevent subsequently opened windows from showing the automation UI.
    browserWindow.document
      .getElementById("main-window")
      .removeAttribute("remotecontrol");

    let selectedBrowser = browserWindow.gBrowser.selectedBrowser;
    lazy.BrowserTestUtils.startLoadingURIString(selectedBrowser, HOME_PAGE);
    await lazy.BrowserTestUtils.browserLoaded(selectedBrowser);

    for (let i = 0; i < this.combos.length; i++) {
      this.currentComboIndex = i;
      await this._performCombo(this.combos.item(this.currentComboIndex));
    }

    this.mochitestScope.info(
      "Done: Completed " +
        this.completedCombos +
        " out of " +
        this.combos.length +
        " configurations."
    );
    this.cleanup();
  },

  /**
   * Helper function for loadSets. This filters out the restricted configs from setName.
   * This was made a helper function to facilitate xpcshell unit testing.
   *
   * @param {string} setName - set name to be filtered e.g. "Toolbars[onlyNavBar,allToolbars]"
   * @returns {object} Returns an object with two values: the filtered set name and a set of
   *                  restricted configs.
   */
  filterRestrictions(setName) {
    let match = /\[([^\]]+)\]$/.exec(setName);
    if (!match) {
      throw new Error(`Invalid restrictions in ${setName}`);
    }
    // Trim the restrictions from the set name.
    setName = setName.slice(0, match.index);
    let restrictions = match[1]
      .split(",")
      .reduce((set, name) => set.add(name.trim()), new Set());

    return { trimmedSetName: setName, restrictions };
  },

  /**
   * Load sets of configurations from JSMs.
   *
   * @param {string[]} setNames - array of set names (e.g. ["Tabs", "WindowSize"].
   * @returns {object[]} Array of sets containing `name` and `configurations` properties.
   */
  loadSets(setNames) {
    let sets = [];
    for (let setName of setNames) {
      let restrictions = null;
      if (setName.includes("[")) {
        let filteredData = this.filterRestrictions(setName);
        setName = filteredData.trimmedSetName;
        restrictions = filteredData.restrictions;
      }
      let imported = ChromeUtils.importESModule(
        `resource://mozscreenshots/configurations/${setName}.sys.mjs`
      );
      imported[setName].init(this._libDir);
      let configurationNames = Object.keys(imported[setName].configurations);
      if (!configurationNames.length) {
        throw new Error(
          setName + " has no configurations for this environment"
        );
      }
      // Checks to see if nonexistent configuration have been specified
      if (restrictions) {
        let incorrectConfigs = [...restrictions].filter(
          r => !configurationNames.includes(r)
        );
        if (incorrectConfigs.length) {
          throw new Error("non existent configurations: " + incorrectConfigs);
        }
      }
      let configurations = {};
      for (let config of configurationNames) {
        // Automatically set the name property of the configuration object to
        // its name from the configuration object.
        imported[setName].configurations[config].name = config;
        // Filter restricted configurations.
        if (!restrictions || restrictions.has(config)) {
          configurations[config] = imported[setName].configurations[config];
        }
      }
      sets.push(configurations);
    }
    return sets;
  },

  cleanup() {
    let browserWindow = Services.wm.getMostRecentWindow("navigator:browser");
    let gBrowser = browserWindow.gBrowser;
    while (gBrowser.tabs.length > 1) {
      gBrowser.removeTab(gBrowser.selectedTab, { animate: false });
    }
    gBrowser.unpinTab(gBrowser.selectedTab);
    lazy.BrowserTestUtils.startLoadingURIString(
      gBrowser.selectedBrowser,
      "data:text/html;charset=utf-8,<h1>Done!"
    );
    browserWindow.restore();
    Services.prefs.clearUserPref("ui.caretBlinkTime");
    Services.prefs.clearUserPref("ui.prefersReducedMotion");
    browserWindow.windowUtils.disableNonTestMouseEvents(false);
  },

  // helpers

  /**
   * Calculate the bounding box based on CSS selector from config for cropping
   *
   * @param {string[]} selectors - array of CSS selectors for relevant DOM element
   * @returns {Rect}
   *   A Geometry.sys.mjs Rect holding relevant x, y, width, height with padding
   */
  _findBoundingBox(selectors, windowType) {
    if (!selectors.length) {
      throw new Error("No selectors specified.");
    }

    // Set window type, default "navigator:browser"
    windowType = windowType || "navigator:browser";
    let browserWindow = Services.wm.getMostRecentWindow(windowType);
    // Scale for high-density displays
    const scale = Cc["@mozilla.org/gfx/screenmanager;1"]
      .getService(Ci.nsIScreenManager)
      .screenForRect(
        browserWindow.screenX,
        browserWindow.screenY,
        1,
        1
      ).defaultCSSScaleFactor;

    const windowLeft = browserWindow.screenX * scale;
    const windowTop = browserWindow.screenY * scale;
    const windowWidth = browserWindow.outerWidth * scale;
    const windowHeight = browserWindow.outerHeight * scale;

    let bounds;
    const rects = [];
    // Grab bounding boxes and find the union
    for (let selector of selectors) {
      let elements;
      // Check for function to find anonymous content
      if (typeof selector == "function") {
        elements = [selector()];
      } else {
        elements = browserWindow.document.querySelectorAll(selector);
      }

      if (!elements.length) {
        throw new Error(`No element for '${selector}' found.`);
      }

      for (let element of elements) {
        // Calculate box region, convert to Rect
        let elementRect = element.getBoundingClientRect();
        // documentGlobal doesn't exist in content privileged windows.
        // eslint-disable-next-line mozilla/use-documentGlobal
        let win = element.ownerDocument.defaultView;
        let rect = new Rect(
          (win.mozInnerScreenX + elementRect.left) * scale,
          (win.mozInnerScreenY + elementRect.top) * scale,
          elementRect.width * scale,
          elementRect.height * scale
        );
        rect.inflateFixed(this.croppingPadding * scale);
        rect.left = Math.max(rect.left, windowLeft);
        rect.top = Math.max(rect.top, windowTop);
        rect.right = Math.min(rect.right, windowLeft + windowWidth);
        rect.bottom = Math.min(rect.bottom, windowTop + windowHeight);

        if (rect.width === 0 && rect.height === 0) {
          this.mochitestScope.todo(
            false,
            `Selector '${selector}' gave a 0x0 rect`
          );
          continue;
        }

        rects.push(rect);

        if (!bounds) {
          bounds = rect;
        } else {
          bounds = bounds.union(rect);
        }
      }
    }

    return { bounds, rects };
  },

  _do_skip(reason, combo, config, func) {
    const { todo } = reason;
    if (todo) {
      this.mochitestScope.todo(
        false,
        `Skipped configuration ` +
          `[ ${combo.map(e => e.name).join(", ")} ] for failure in ` +
          `${config.name}.${func}: ${todo}`
      );
    } else {
      this.mochitestScope.info(
        `\tSkipped configuration ` +
          `[ ${combo.map(e => e.name).join(", ")} ] ` +
          `for "${reason}" in ${config.name}.${func}`
      );
    }
  },

  async _performCombo(combo) {
    let paddedComboIndex = padLeft(
      this.currentComboIndex + 1,
      String(this.combos.length).length
    );
    this.mochitestScope.info(
      `Combination ${paddedComboIndex}/${this.combos.length}: ${this._comboName(
        combo
      ).substring(1)}`
    );

    // Notice that this does need to be a closure, not a function, as otherwise
    // "this" gets replaced and we lose access to this.mochitestScope.
    const changeConfig = config => {
      this.mochitestScope.info("calling " + config.name);

      let applyPromise = Promise.resolve(config.applyConfig());
      let timeoutPromise = new Promise((resolve, reject) => {
        setTimeout(reject, APPLY_CONFIG_TIMEOUT_MS, "Timed out");
      });

      this.mochitestScope.info("called " + config.name);
      // Add a default timeout of 700ms to avoid conflicts when configurations
      // try to apply at the same time. e.g WindowSize and CustomTitlebar
      return Promise.race([applyPromise, timeoutPromise]).then(result => {
        return new Promise(resolve => {
          setTimeout(() => resolve(result), 700);
        });
      });
    };

    try {
      // First go through and actually apply all of the configs
      for (let i = 0; i < combo.length; i++) {
        let config = combo[i];
        if (!this._lastCombo || config !== this._lastCombo[i]) {
          this.mochitestScope.info(`promising ${config.name}`);
          const reason = await changeConfig(config);
          if (reason) {
            this._do_skip(reason, combo, config, "applyConfig");
            return;
          }
        }
      }

      // Update the lastCombo since it's now been applied regardless of whether it's accepted below.
      this.mochitestScope.info(
        "fulfilled all applyConfig so setting lastCombo."
      );
      this._lastCombo = combo;

      // Then ask configs if the current setup is valid. We can't can do this in
      // the applyConfig methods of the config since it doesn't know what configs
      // later in the loop will do that may invalidate the combo.
      for (let i = 0; i < combo.length; i++) {
        let config = combo[i];
        // A configuration can specify an optional verifyConfig method to indicate
        // if the current config is valid for a screenshot. This gets called even
        // if the this config was used in the lastCombo since another config may
        // have invalidated it.
        if (config.verifyConfig) {
          this.mochitestScope.info(
            `checking if the combo is valid with ${config.name}`
          );
          const reason = await config.verifyConfig();
          if (reason) {
            this._do_skip(reason, combo, config, "applyConfig");
            return;
          }
        }
      }
    } catch (ex) {
      this.mochitestScope.ok(
        false,
        `Unexpected exception in [ ${combo
          .map(({ name }) => name)
          .join(", ")} ]: ${ex.toString()}`
      );
      this.mochitestScope.info(`\t${ex}`);
      if (ex.stack) {
        this.mochitestScope.info(`\t${ex.stack}`);
      }
      return;
    }
    this.mochitestScope.info(
      `Configured UI for [ ${combo
        .map(({ name }) => name)
        .join(", ")} ] successfully`
    );

    // Collect selectors from combo configs for cropping region
    let windowType;
    const finalSelectors = [];
    for (const obj of combo) {
      if (!windowType) {
        windowType = obj.windowType;
      } else if (windowType !== obj.windowType) {
        this.mochitestScope.ok(
          false,
          "All configurations in the combo have a single window type"
        );
        return;
      }
      for (const selector of obj.selectors) {
        finalSelectors.push(selector);
      }
    }

    const { bounds, rects } = this._findBoundingBox(finalSelectors, windowType);
    this.mochitestScope.ok(bounds, "A valid bounding box was found");
    if (!bounds) {
      return;
    }
    await this._onConfigurationReady(combo, bounds, rects);
  },

  async _onConfigurationReady(combo, bounds, rects) {
    let filename =
      padLeft(this.currentComboIndex + 1, String(this.combos.length).length) +
      this._comboName(combo);
    const imagePath = await lazy.Screenshot.captureExternal(filename);

    let browserWindow = Services.wm.getMostRecentWindow("navigator:browser");
    await this._cropImage(
      browserWindow,
      PathUtils.toFileURI(imagePath),
      bounds,
      rects,
      imagePath
    ).catch(msg => {
      throw new Error(
        `Cropping combo [${combo.map(e => e.name).join(", ")}] failed: ${msg}`
      );
    });
    this.completedCombos++;
    this.mochitestScope.info("_onConfigurationReady");
  },

  _comboName(combo) {
    return combo.reduce(function (a, b) {
      return a + "_" + b.name;
    }, "");
  },

  /**
   * imageFileURIString is a screenshot of the whole OS desktop, in desktop
   * (screen) pixel coordinates. `bounds` and `rects` (from
   * _findBoundingBox()) are in that same desktop coordinate space, not
   * window-relative: `rects` are the individual selected elements'
   * bounding boxes, and `bounds` is their union, i.e. the overall region to
   * crop out of the desktop screenshot. If the browser window is
   * positioned partially or fully outside the visible desktop (as can
   * happen with windows moved off-screen for a test), `bounds`/`rects` can
   * fall outside the captured image entirely, hence the clamping below.
   */
  async _cropImage(window, imageFileURIString, bounds, rects, targetPath) {
    const { document } = window;

    // colorSpaceConversion: "none" preserves raw pixel values without ICC
    // profile conversion, ensuring lossless round-tripping.
    const blob = await fetch(imageFileURIString).then(r => r.blob());
    const img = await window.createImageBitmap(blob, {
      colorSpaceConversion: "none",
    });

    // Clip the cropping region to the size of the screenshot
    // This is necessary mostly to deal with offscreen windows, since we
    // are capturing an image of the operating system's desktop.
    bounds.left = Math.max(0, bounds.left);
    bounds.right = Math.min(img.width, bounds.right);
    bounds.top = Math.max(0, bounds.top);
    bounds.bottom = Math.min(img.height, bounds.bottom);

    // Create a new offscreen canvas with the width and height given by the
    // size of the region we want to crop to
    const canvas = document.createElementNS(
      "http://www.w3.org/1999/xhtml",
      "canvas"
    );
    canvas.width = bounds.width;
    canvas.height = bounds.height;
    const ctx = canvas.getContext("2d", { colorSpace: "srgb" });
    ctx.imageSmoothingEnabled = false;

    ctx.fillStyle = "hotpink";
    ctx.fillRect(0, 0, bounds.width, bounds.height);

    for (const rect of rects) {
      rect.left = Math.max(0, rect.left);
      rect.right = Math.min(img.width, rect.right);
      rect.top = Math.max(0, rect.top);
      rect.bottom = Math.min(img.height, rect.bottom);

      const width = rect.width;
      const height = rect.height;
      // Where this rect sits in the desktop screenshot (source image).
      const screenX = rect.left;
      const screenY = rect.top;
      // Where this rect lands in the cropped output canvas, relative to
      // the union bounds computed above.
      const imageX = screenX - bounds.left;
      const imageY = screenY - bounds.top;

      ctx.drawImage(
        img,
        screenX,
        screenY,
        width,
        height,
        imageX,
        imageY,
        width,
        height
      );
    }

    await new Promise((resolve, reject) => {
      canvas.toBlob(blob2 => {
        const fr = new FileReader();
        fr.onload = e => {
          IOUtils.write(targetPath, new Uint8Array(e.target.result)).then(
            resolve
          );
        };
        fr.onerror = reject;
        fr.readAsArrayBuffer(blob2);
      }, "image/png");
    });
  },

  /**
   * Finds the index of the first comma that is not enclosed within square brackets.
   *
   * @param {string} envVar - the string that needs to be searched
   * @returns {number} index of valid comma or -1 if not found.
   */
  findComma(envVar) {
    let nestingDepth = 0;
    for (let i = 0; i < envVar.length; i++) {
      if (envVar[i] === "[") {
        nestingDepth += 1;
      } else if (envVar[i] === "]") {
        nestingDepth -= 1;
      } else if (envVar[i] === "," && nestingDepth === 0) {
        return i;
      }
    }

    return -1;
  },

  /**
   * Splits the environment variable around commas not enclosed in brackets.
   *
   * @param {string} envVar - The environment variable
   * @returns {string[]} Array of strings containing the configurations
   * e.g. ["Toolbars[onlyNavBar,allToolbars]","DevTools[jsdebugger,webconsole]","Tabs"]
   */
  splitEnv(envVar) {
    let result = [];

    let commaIndex = this.findComma(envVar);
    while (commaIndex != -1) {
      result.push(envVar.slice(0, commaIndex).trim());
      envVar = envVar.slice(commaIndex + 1);
      commaIndex = this.findComma(envVar);
    }
    result.push(envVar.trim());
    return result;
  },
};

/**
 * Helper to lazily compute the Cartesian product of all of the sets of configurations.
 */
function LazyProduct(sets) {
  /**
   * An entry for each set with the value being:
   * [the number of permutations of the sets with lower index,
   *  the number of items in the set at the index]
   */
  this.sets = sets;
  this.lookupTable = [];
  let combinations = 1;
  for (let i = this.sets.length - 1; i >= 0; i--) {
    let set = this.sets[i];
    let setLength = Object.keys(set).length;
    this.lookupTable[i] = [combinations, setLength];
    combinations *= setLength;
  }
}
LazyProduct.prototype = {
  get length() {
    let last = this.lookupTable[0];
    if (!last) {
      return 0;
    }
    return last[0] * last[1];
  },

  item(n) {
    // For set i, get the item from the set with the floored value of
    // (n / the number of permutations of the sets already chosen from) modulo the length of set i
    let result = [];
    for (let i = this.sets.length - 1; i >= 0; i--) {
      let priorCombinations = this.lookupTable[i][0];
      let setLength = this.lookupTable[i][1];
      let keyIndex = Math.floor(n / priorCombinations) % setLength;
      let keys = Object.keys(this.sets[i]);
      result[i] = this.sets[i][keys[keyIndex]];
    }
    return result;
  },
};

function padLeft(number, width, padding = "0") {
  return padding.repeat(Math.max(0, width - String(number).length)) + number;
}
