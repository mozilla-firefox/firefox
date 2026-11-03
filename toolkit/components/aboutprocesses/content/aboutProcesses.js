/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Time in ms before we start changing the sort order again after receiving a
// mousemove event.
const TIME_BEFORE_SORTING_AGAIN = 5000;

// How long a row's unload/reload button ignores further clicks, so a
// double-click doesn't act twice.
const ACTION_REPEAT_DELAY_MS = 500;

// How long we should wait between samples.
const MINIMUM_INTERVAL_BETWEEN_SAMPLES_MS = 1000;

// How often we should update
const UPDATE_INTERVAL_MS = 2000;

const NS_PER_US = 1000;
const NS_PER_MS = 1000 * 1000;
const NS_PER_S = 1000 * 1000 * 1000;
const NS_PER_MIN = NS_PER_S * 60;
const NS_PER_HOUR = NS_PER_MIN * 60;
const NS_PER_DAY = NS_PER_HOUR * 24;

const ONE_GIGA = 1024 * 1024 * 1024;
const ONE_MEGA = 1024 * 1024;
const ONE_KILO = 1024;

const WEB_ISOLATED_L10N_ID = "about-processes-web-isolated-process2";

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "ProfilerPopupBackground", function () {
  return ChromeUtils.importESModule(
    "resource://devtools/client/performance-new/shared/background.sys.mjs"
  );
});

const { WebExtensionPolicy } = Cu.getGlobalForObject(Services);

XPCOMUtils.defineLazyServiceGetter(
  this,
  "IDNService",
  "@mozilla.org/network/idn-service;1",
  Ci.nsIIDNService
);

const SHOW_THREADS = Services.prefs.getBoolPref(
  "toolkit.aboutProcesses.showThreads"
);
const SHOW_ALL_SUBFRAMES = Services.prefs.getBoolPref(
  "toolkit.aboutProcesses.showAllSubframes"
);
const SHOW_PROFILER_ICONS = Services.prefs.getBoolPref(
  "toolkit.aboutProcesses.showProfilerIcons"
);
const PROFILE_DURATION = Math.max(
  1,
  Services.prefs.getIntPref("toolkit.aboutProcesses.profileDuration")
);

/**
 * For the time being, Fluent doesn't support duration or memory formats, so we need
 * to fetch units from Fluent. To avoid re-fetching at each update, we prefetch these
 * units during initialization, asynchronously, and keep them.
 *
 * @type {
 *   duration: { ns: String, us: String, ms: String, s: String, m: String, h: String, d: String },
 *   memory: { B: String, KB: String, MB: String, GB: String, TB: String, PB: String, EB: String }
 * }.
 */
let gLocalizedUnits;

/**
 * To avoid needing to re-fetching at each update we prefetch the localized
 * process properties.
 *
 * @type {
 *   privateWindow: string,
 *   serviceWorker: string,
 *   jitDisabled: string,
 *   withCoopCoep: string,
 * }.
 */
let gLocalizedProcessProperties;

let tabFinder = {
  update() {
    this._map = new Map();
    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      let tabbrowser = win.gBrowser;
      for (let browser of tabbrowser.browsers) {
        let id = browser.outerWindowID; // May be `null` if the browser isn't loaded yet
        if (id != null) {
          this._map.set(id, browser);
        }
      }
      if (tabbrowser.preloadedBrowser) {
        let browser = tabbrowser.preloadedBrowser;
        if (browser.outerWindowID) {
          this._map.set(browser.outerWindowID, browser);
        }
      }
    }
  },

  /**
   * Find the <xul:tab> for a window id.
   *
   * This is useful e.g. for reloading or closing tabs.
   *
   * @return null If the xul:tab could not be found, e.g. if the
   * windowId is that of a chrome window.
   * @return {{tabbrowser: <xul:tabbrowser>, tab: <xul.tab>}} The
   * tabbrowser and tab if the latter could be found.
   */
  get(id) {
    let browser = this._map.get(id);
    if (!browser) {
      return null;
    }
    let tabbrowser = browser.getTabBrowser();
    if (!tabbrowser) {
      return {
        tabbrowser: null,
        tab: {
          getAttribute() {
            return "";
          },
          linkedBrowser: browser,
        },
      };
    }
    return { tabbrowser, tab: tabbrowser.getTabForBrowser(browser) };
  },
};

// requestProcInfo() can't attribute a subframe back to its owning tab (its
// WindowInfoDictionary carries no such link), so weights are computed by
// walking each tab's own BrowsingContext tree instead, weighting the root
// frame higher than subframes.
const TOP_LEVEL_FRAME_WEIGHT = 2;
const SUBFRAME_WEIGHT = 1;

function* iterateBrowsingContexts(browsingContext) {
  if (!browsingContext) {
    return;
  }
  yield browsingContext;
  for (let child of browsingContext.children) {
    yield* iterateBrowsingContexts(child);
  }
}

// Host-less URIs group under their scheme, except about: pages, which group
// per page ignoring the query (about:reader?url=...).
function getSiteKey(uri) {
  try {
    return Services.eTLD.getSchemelessSite(uri) || uri.scheme;
  } catch (e) {
    return uri.schemeIs("about") ? uri.prePath + uri.filePath : uri.scheme;
  }
}

// Shown in place of the key: punycode decoded under the address bar's
// spoofing rules, and an extension's name rather than its UUID.
function getSiteLabel(uri, siteKey) {
  if (uri.schemeIs("moz-extension")) {
    return WebExtensionPolicy.getByHostname(uri.host)?.name ?? siteKey;
  }
  try {
    return IDNService.convertToDisplayIDN(siteKey);
  } catch (e) {
    return siteKey;
  }
}

/**
 * Attributes per-process resource usage (from `State.getCounters()`) to the
 * individual tabs that share each process, weighted by how much of that
 * process each tab's frames occupy.
 */
class TabAttribution {
  // Set once by init(); read synchronously after that since core count
  // doesn't change at runtime.
  _cpuCount = 1;
  // Keyed by <tab>: a reloading tab shows its initial about:blank until the
  // load commits, and keeps its last real URI meanwhile.
  _lastURIs = new WeakMap();

  async init() {
    try {
      this._cpuCount = Math.max(
        1,
        Number((await Services.sysinfo.processInfo)?.count) || 1
      );
    } catch (e) {
      console.error("about:processes: failed to get CPU core count", e);
    }
  }

  // tabbrowser.tabs (not .browsers) so discarded/never-loaded tabs, which
  // have no content window, are still included. Uses tabFinder's own
  // Services.wm.getEnumerator pattern -- BrowserWindowTracker would be
  // nicer but is browser/-owned, off-limits from toolkit/.
  _getTopLevelTabs() {
    let ownBrowser = window.docShell.browsingContext.embedderElement;
    // A non-private view must never show a private tab's title/URL (or vice
    // versa) -- a privacy hazard, not just a missing label.
    let ownIsPrivate = PrivateBrowsingUtils.isBrowserPrivate(ownBrowser);
    let tabs = [];
    for (let win of Services.wm.getEnumerator("navigator:browser")) {
      let tabbrowser = win.gBrowser;
      if (!tabbrowser) {
        continue;
      }
      for (let tab of tabbrowser.tabs) {
        if (tab.closing) {
          continue;
        }
        let browser = tab.linkedBrowser;
        if (browser == ownBrowser) {
          continue;
        }
        if (PrivateBrowsingUtils.isBrowserPrivate(browser) != ownIsPrivate) {
          continue;
        }
        // currentURI/contentTitle are lazy getters backed by SessionStore's
        // lazy tab data, so they stay meaningful with no content process.
        let uri = browser.currentURI;
        let windowGlobal = browser.browsingContext?.currentWindowGlobal;
        if (
          browser.isConnected &&
          (!windowGlobal || windowGlobal.isInitialDocument)
        ) {
          uri = this._lastURIs.get(tab) ?? uri;
        } else {
          this._lastURIs.set(tab, uri);
        }
        if (!uri || uri.schemeIs("chrome")) {
          continue;
        }
        tabs.push({
          tab,
          tabbrowser,
          browser,
          uri,
          title: tab.label || browser.contentTitle || "",
          // Discarded or never loaded. Not !outerWindowID, which stays unset
          // for a moment after a reload re-inserts the browser.
          discarded: !browser.isConnected,
        });
      }
    }
    return tabs;
  }

  // Keyed by <tab>, not outerWindowID: discarded tabs lack one, and a
  // reload would change it anyway.
  _computeFrameWeights(tabs) {
    let tabWeights = new Map();
    let weightsByPid = new Map();
    for (let tab of tabs) {
      let rootBrowsingContext = tab.browser.browsingContext;
      if (!rootBrowsingContext) {
        continue;
      }
      let perTabWeights = new Map();
      for (let browsingContext of iterateBrowsingContexts(
        rootBrowsingContext
      )) {
        let pid = browsingContext.currentWindowGlobal?.osPid;
        // The parent process's own window globals report osPid === -1, a
        // real pid a falsy check wouldn't catch -- excluded explicitly.
        if (pid == null || pid < 0) {
          continue;
        }
        let weight =
          browsingContext == rootBrowsingContext
            ? TOP_LEVEL_FRAME_WEIGHT
            : SUBFRAME_WEIGHT;
        perTabWeights.set(pid, (perTabWeights.get(pid) ?? 0) + weight);
        weightsByPid.set(pid, (weightsByPid.get(pid) ?? 0) + weight);
      }
      tabWeights.set(tab.tab, perTabWeights);
    }
    return { tabWeights, weightsByPid };
  }

  // `slopeCpuOfTotal` is a fraction of total CPU capacity, not one core, so
  // entries across tabs are summable. It's null until each of the tab's
  // processes has two samples.
  getTabCounters(counters) {
    let tabs = this._getTopLevelTabs();
    let { tabWeights, weightsByPid } = this._computeFrameWeights(tabs);

    let ramByPid = new Map();
    let cpuByPid = new Map();
    for (let process of counters) {
      ramByPid.set(process.pid, process.totalRamSize);
      cpuByPid.set(process.pid, process.slopeCpu);
    }

    return tabs.map(tab => {
      let weights = tabWeights.get(tab.tab);
      let totalRamSize = 0;
      let slopeCpuOneCore = 0;
      let hasUsageData = false;
      if (weights) {
        for (let [pid, weight] of weights) {
          let totalWeight = weightsByPid.get(pid);
          if (!totalWeight || !ramByPid.has(pid)) {
            continue;
          }
          hasUsageData = true;
          let share = weight / totalWeight;
          totalRamSize += (ramByPid.get(pid) ?? 0) * share;
          let slopeCpu = cpuByPid.get(pid);
          slopeCpuOneCore =
            slopeCpu == null || slopeCpuOneCore == null
              ? null
              : slopeCpuOneCore + slopeCpu * share;
        }
      }
      let siteKey = getSiteKey(tab.uri);
      return {
        tab: tab.tab,
        tabbrowser: tab.tabbrowser,
        uri: tab.uri,
        title: tab.title,
        siteKey,
        siteLabel: getSiteLabel(tab.uri, siteKey),
        discarded: tab.discarded,
        // False for discarded/never-loaded tabs and ones whose process
        // isn't in `counters` at all -- shown as "—", not a misleading 0.
        hasUsageData,
        totalRamSize,
        slopeCpuOfTotal:
          slopeCpuOneCore == null ? null : slopeCpuOneCore / this._cpuCount,
      };
    });
  }
}

/**
 * Utilities for dealing with state
 */
var State = {
  // Store the previous and current samples so they can be compared.
  _previous: null,
  _latest: null,

  async _promiseSnapshot() {
    let date = ChromeUtils.now();
    let main = await ChromeUtils.requestProcInfo();
    main.date = date;

    let processes = new Map();
    processes.set(main.pid, main);
    for (let child of main.children) {
      child.date = date;
      processes.set(child.pid, child);
    }

    return { processes, date };
  },

  /**
   * Update the internal state.
   *
   * @return {Promise}
   */
  async update(force = false) {
    if (
      force ||
      !this._latest ||
      ChromeUtils.now() - this._latest.date >
        MINIMUM_INTERVAL_BETWEEN_SAMPLES_MS
    ) {
      // Replacing this._previous before we are done awaiting
      // this._promiseSnapshot can cause this._previous and this._latest to be
      // equal for a short amount of time, which can cause test failures when
      // a forced update of the display is triggered in the meantime.
      let newSnapshot = await this._promiseSnapshot();
      this._previous = this._latest;
      this._latest = newSnapshot;
    }
  },

  _getThreadDelta(cur, prev, deltaT) {
    let result = {
      tid: cur.tid,
      name: cur.name || `(${cur.tid})`,
      // Total amount of CPU used, in ns.
      totalCpu: cur.cpuTime,
      slopeCpu: null,
      active: null,
    };
    if (!deltaT) {
      return result;
    }
    result.slopeCpu = (result.totalCpu - (prev ? prev.cpuTime : 0)) / deltaT;
    result.active =
      !!result.slopeCpu || cur.cpuCycleCount > (prev ? prev.cpuCycleCount : 0);
    return result;
  },

  _getDOMWindows(process) {
    if (!process.windows) {
      return [];
    }
    if (!process.type == "extensions") {
      return [];
    }
    let windows = process.windows.map(win => {
      let tab = tabFinder.get(win.outerWindowId);
      let addon =
        process.type == "extension"
          ? WebExtensionPolicy.getByURI(win.documentURI)
          : null;
      let displayRank;
      if (tab) {
        displayRank = 1;
      } else if (win.isProcessRoot) {
        displayRank = 2;
      } else if (win.documentTitle) {
        displayRank = 3;
      } else {
        displayRank = 4;
      }
      return {
        outerWindowId: win.outerWindowId,
        documentURI: win.documentURI,
        documentTitle: win.documentTitle,
        isProcessRoot: win.isProcessRoot,
        isInProcess: win.isInProcess,
        tab,
        addon,
        // The number of instances we have collapsed.
        count: 1,
        // A rank used to quickly sort windows.
        displayRank,
      };
    });

    // We keep all tabs and addons but we collapse subframes that have the same host.

    // A map from host -> subframe.
    let collapsible = new Map();
    let result = [];
    for (let win of windows) {
      if (win.tab || win.addon) {
        result.push(win);
        continue;
      }
      let prev = collapsible.get(win.documentURI.prePath);
      if (prev) {
        prev.count += 1;
      } else {
        collapsible.set(win.documentURI.prePath, win);
        result.push(win);
      }
    }
    return result;
  },

  /**
   * Compute the delta between two process snapshots.
   *
   * @param {ProcessSnapshot} cur
   * @param {ProcessSnapshot?} prev
   */
  _getProcessDelta(cur, prev) {
    let windows = this._getDOMWindows(cur);
    let result = {
      pid: cur.pid,
      childID: cur.childID,
      totalRamSize: cur.memory,
      deltaRamSize: null,
      totalCpu: cur.cpuTime,
      slopeCpu: null,
      active: null,
      type: cur.type,
      origin: cur.origin || "",
      threads: null,
      displayRank: Control._getDisplayGroupRank(cur, windows),
      windows,
      utilityActors: cur.utilityActors,
      // If this process has an unambiguous title, store it here.
      title: null,
    };
    // Attempt to determine a title for this process.
    let titles = [
      ...new Set(
        result.windows
          .filter(win => win.documentTitle)
          .map(win => win.documentTitle)
      ),
    ];
    if (titles.length == 1) {
      result.title = titles[0];
    }
    if (!prev) {
      if (SHOW_THREADS) {
        result.threads = cur.threads.map(data => this._getThreadDelta(data));
      }
      return result;
    }
    if (prev.pid != cur.pid) {
      throw new Error("Assertion failed: A process cannot change pid.");
    }
    let deltaT = (cur.date - prev.date) * NS_PER_MS;
    let threads = null;
    if (SHOW_THREADS) {
      let prevThreads = new Map();
      for (let thread of prev.threads) {
        prevThreads.set(thread.tid, thread);
      }
      threads = cur.threads.map(curThread =>
        this._getThreadDelta(curThread, prevThreads.get(curThread.tid), deltaT)
      );
    }
    result.deltaRamSize = cur.memory - prev.memory;
    result.slopeCpu = (cur.cpuTime - prev.cpuTime) / deltaT;
    result.active = !!result.slopeCpu || cur.cpuCycleCount > prev.cpuCycleCount;
    result.threads = threads;
    return result;
  },

  getCounters() {
    tabFinder.update();

    let counters = [];

    for (let cur of this._latest.processes.values()) {
      let prev = this._previous?.processes.get(cur.pid);
      counters.push(this._getProcessDelta(cur, prev));
    }

    return counters;
  },
};

/**
 * Row-reuse bookkeeping shared by both view/controller pairs in this file
 * (see ProcessesTabView below): rows are keyed by id and diff-inserted into
 * `tbody` in commitOrder() so unrelated rows aren't recreated -- and don't
 * lose scroll/focus state -- just because their position in the list moved.
 */
class RowSet {
  _rowsById = new Map();
  _orderedRows = [];

  getRow(rowId) {
    return this._rowsById.get(rowId);
  }

  _getOrCreateRow(rowId, createRow) {
    let row = this._rowsById.get(rowId);
    if (!row) {
      row = createRow();
      row.rowId = rowId;
      this._rowsById.set(rowId, row);
    }
    this._orderedRows.push(row);
    return row;
  }

  _removeRow(row) {
    this._rowsById.delete(row.rowId);
    row.remove();
  }

  _commitOrder() {
    let tbody = document.getElementById("process-tbody");
    let insertPoint = tbody.firstChild;
    let nextRow;
    while ((nextRow = this._orderedRows.shift())) {
      if (insertPoint && insertPoint === nextRow) {
        insertPoint = insertPoint.nextSibling;
      } else {
        tbody.insertBefore(nextRow, insertPoint);
      }
    }
    if (insertPoint) {
      while ((nextRow = insertPoint.nextSibling)) {
        this._removeRow(nextRow);
      }
      this._removeRow(insertPoint);
    }
  }
}

class ProcessesView extends RowSet {
  // Processes, tabs and subframes that we killed during the previous iteration.
  // Array<{pid:Number} | {windowId:Number}>
  _killedRecently = [];

  commit() {
    this._killedRecently.length = 0;
    this._commitOrder();
  }
  // Drops not-yet-inserted rows entirely, unlike ProcessesTabView's
  // _discardOrder() (which appends new rows immediately) -- this isn't a
  // persistently-visible panel, so a briefly-invisible new row doesn't matter.
  discardUpdate() {
    for (let row of this._orderedRows) {
      if (!row.parentNode) {
        this._rowsById.delete(row.rowId);
      }
    }
    this._orderedRows = [];
  }
  insertAfterRow(row) {
    let tbody = row.parentNode;
    let nextRow;
    while ((nextRow = this._orderedRows.pop())) {
      tbody.insertBefore(nextRow, row.nextSibling);
    }
  }
  _getOrCreateRow(rowId, cellCount) {
    return super._getOrCreateRow(rowId, () => {
      let row = document.createElement("tr");
      while (cellCount--) {
        row.appendChild(document.createElement("td"));
      }
      return row;
    });
  }

  displayCpu(data, cpuCell, maxSlopeCpu) {
    // Put a value < 0% when we really don't want to see a bar as
    // otherwise it sometimes appears due to rounding errors when we
    // don't have an integer number of pixels.
    let barWidth = -0.5;
    if (data.slopeCpu == null) {
      fillCell(cpuCell, {
        fluentName: "about-processes-cpu-user-and-kernel-not-ready",
        classes: ["cpu"],
      });
    } else {
      let { duration, unit } = this._getDuration(data.totalCpu);
      if (data.totalCpu == 0) {
        // A thread having used exactly 0ns of CPU time is not possible.
        // When we get 0 it means the thread used less than the precision of
        // the measurement, and it makes more sense to show '0ms' than '0ns'.
        // This is useful on Linux where the minimum non-zero CPU time value
        // for threads of child processes is 10ms, and on Windows ARM64 where
        // the minimum non-zero value is 16ms.
        unit = "ms";
      }
      let localizedUnit = gLocalizedUnits.duration[unit];
      if (data.slopeCpu == 0) {
        let fluentName = data.active
          ? "about-processes-cpu-almost-idle"
          : "about-processes-cpu-fully-idle";
        fillCell(cpuCell, {
          fluentName,
          fluentArgs: {
            total: duration,
            unit: localizedUnit,
          },
          classes: ["cpu"],
        });
      } else {
        fillCell(cpuCell, {
          fluentName: "about-processes-cpu",
          fluentArgs: {
            percent: data.slopeCpu,
            total: duration,
            unit: localizedUnit,
          },
          classes: ["cpu"],
        });

        let cpuPercent = data.slopeCpu * 100;
        if (maxSlopeCpu > 1) {
          cpuPercent /= maxSlopeCpu;
        }
        // Ensure we always have a visible bar for non-0 values.
        barWidth = Math.max(0.5, cpuPercent);
      }
    }
    cpuCell.style.setProperty("--bar-width", barWidth);
  }

  /**
   * Updates the name cell of a process row.
   *
   * @param {ProcessDelta} data The data to display.
   * @param {DOMElement} nameCell The cell displaying the process name.
   */
  updateProcessName(data, nameCell) {
    let classNames = [];
    let fluentName;
    let fluentArgs = {
      pid: "" + data.pid, // Make sure that this number is not localized
    };
    let processProperties = [];

    switch (data.type) {
      case "web":
        fluentName = "about-processes-web-process";
        break;
      case "webIsolated":
        fluentName = WEB_ISOLATED_L10N_ID;
        fluentArgs.origin = data.origin;
        processProperties.push(fluentArgs.pid);
        break;
      case "webServiceWorker":
        fluentName = WEB_ISOLATED_L10N_ID;
        fluentArgs.origin = data.origin;
        processProperties.push(fluentArgs.pid);
        processProperties.push(gLocalizedProcessProperties.serviceWorker);
        break;
      case "file":
        fluentName = "about-processes-file-process";
        break;
      case "extension":
        fluentName = "about-processes-extension-process";
        classNames = ["extensions"];
        break;
      case "privilegedabout":
        fluentName = "about-processes-privilegedabout-process";
        break;
      case "privilegedmozilla":
        fluentName = "about-processes-privilegedmozilla-process";
        break;
      case "withCoopCoep":
        fluentName = WEB_ISOLATED_L10N_ID;
        fluentArgs.origin = data.origin;
        processProperties.push(fluentArgs.pid);
        processProperties.push(gLocalizedProcessProperties.withCoopCoep);
        break;
      case "browser":
        fluentName = "about-processes-browser-process";
        break;
      case "plugin":
        fluentName = "about-processes-plugin-process";
        break;
      case "gmpPlugin":
        fluentName = "about-processes-gmp-plugin-process";
        break;
      case "gpu":
        fluentName = "about-processes-gpu-process";
        break;
      case "vr":
        fluentName = "about-processes-vr-process";
        break;
      case "rdd":
        fluentName = "about-processes-rdd-process";
        break;
      case "forkServer":
        fluentName = "about-processes-fork-server-process";
        break;
      case "preallocated":
        fluentName = "about-processes-preallocated-process";
        break;
      case "utility":
        fluentName = "about-processes-utility-process";
        break;
      case "inference":
        fluentName = "about-processes-inference-process";
        break;
      // The following are probably not going to show up for users
      // but let's handle the case anyway to avoid heisenoranges
      // during tests in case of a leftover process from a previous
      // test.
      default:
        fluentName = "about-processes-unknown-process";
        fluentArgs.type = data.type;
        break;
    }

    // Show container names instead of raw origin attribute suffixes.
    if (fluentArgs.origin?.includes("^")) {
      let origin = fluentArgs.origin;
      if (
        origin.endsWith("^disableJit=1") ||
        origin.endsWith("&disableJit=1")
      ) {
        processProperties.push(gLocalizedProcessProperties.jitDisabled);
      }

      let privateBrowsingId, userContextId;
      try {
        ({ privateBrowsingId, userContextId } =
          ChromeUtils.createOriginAttributesFromOrigin(origin));
        fluentArgs.origin = origin.slice(0, origin.indexOf("^"));
      } catch (e) {
        // createOriginAttributesFromOrigin can throw NS_ERROR_FAILURE for incorrect origin strings.
      }
      if (userContextId) {
        let identityLabel =
          ContextualIdentityService.getUserContextLabel(userContextId);
        if (identityLabel) {
          fluentArgs.origin += ` — ${identityLabel}`;
        }
      }
      if (privateBrowsingId) {
        processProperties.push(gLocalizedProcessProperties.privateWindow);
      }
    }

    if (processProperties.length) {
      fluentArgs.properties = new Intl.ListFormat(undefined, {
        type: "unit",
      }).format(processProperties);
    }

    let processNameElement = nameCell;
    if (SHOW_PROFILER_ICONS) {
      if (!nameCell.firstChild) {
        processNameElement = document.createElement("span");
        nameCell.appendChild(processNameElement);

        let profilerButton = document.createElement("span");
        profilerButton.className = "profiler-icon";
        profilerButton.setAttribute("role", "button");
        profilerButton.setAttribute("tabindex", "0");
        profilerButton.setAttribute("aria-pressed", "false");
        document.l10n.setAttributes(
          profilerButton,
          "about-processes-profile-process",
          { duration: PROFILE_DURATION }
        );
        nameCell.appendChild(profilerButton);
      } else {
        processNameElement = nameCell.firstChild;
      }
    }
    document.l10n.setAttributes(processNameElement, fluentName, fluentArgs);
    nameCell.className = ["type", "favicon", ...classNames].join(" ");
    nameCell.setAttribute("id", data.pid + "-label");

    let image;
    switch (data.type) {
      case "browser":
      case "privilegedabout":
        image = "chrome://branding/content/icon32.png";
        break;
      case "extension":
        image = "chrome://mozapps/skin/extensions/extension.svg";
        break;
      default:
        // If all favicons match, pick the shared favicon.
        // Otherwise, pick a default icon.
        // If some tabs have no favicon, we ignore them.
        for (let win of data.windows || []) {
          if (!win.tab) {
            continue;
          }
          let favicon = win.tab.tab.getAttribute("image");
          if (!favicon) {
            // No favicon here, let's ignore the tab.
          } else if (!image) {
            // Let's pick a first favicon.
            // We'll remove it later if we find conflicting favicons.
            image = favicon;
          } else if (image == favicon) {
            // So far, no conflict, keep the favicon.
          } else {
            // Conflicting favicons, fallback to default.
            image = null;
            break;
          }
        }
        if (!image) {
          image = "chrome://global/skin/icons/link.svg";
        }
    }
    nameCell.style.backgroundImage = `url('${image}')`;
  }

  /**
   * Display a row showing a single process (without its threads).
   *
   * @param {ProcessDelta} data The data to display.
   * @param {number} maxSlopeCpu The largest slopeCpu value.
   * @return {DOMElement} The row displaying the process.
   */
  displayProcessRow(data, maxSlopeCpu) {
    const cellCount = 4;
    let rowId = "p:" + data.pid;
    let row = this._getOrCreateRow(rowId, cellCount);
    row.process = data;
    {
      let classNames = "process";
      if (data.isHung) {
        classNames += " hung";
      }
      row.className = classNames;
    }

    // Column: Name
    let nameCell = row.firstChild;
    this.updateProcessName(data, nameCell);

    // Column: Memory
    let memoryCell = nameCell.nextSibling;
    {
      let formattedTotal = formatMemory(data.totalRamSize);
      if (data.deltaRamSize) {
        let formattedDelta = formatMemory(data.deltaRamSize);
        fillCell(memoryCell, {
          fluentName: "about-processes-total-memory-size-changed",
          fluentArgs: {
            total: formattedTotal.amount,
            totalUnit: gLocalizedUnits.memory[formattedTotal.unit],
            delta: Math.abs(formattedDelta.amount),
            deltaUnit: gLocalizedUnits.memory[formattedDelta.unit],
            deltaSign: data.deltaRamSize > 0 ? "+" : "-",
          },
          classes: ["memory"],
        });
      } else {
        fillCell(memoryCell, {
          fluentName: "about-processes-total-memory-size-no-change",
          fluentArgs: {
            total: formattedTotal.amount,
            totalUnit: gLocalizedUnits.memory[formattedTotal.unit],
          },
          classes: ["memory"],
        });
      }
    }

    // Column: CPU
    let cpuCell = memoryCell.nextSibling;
    this.displayCpu(data, cpuCell, maxSlopeCpu);

    // Column: Actions with Kill button – but not for all processes.
    let actionCell = cpuCell.nextSibling;

    if (!actionCell.firstChild) {
      let span = document.createElement("span");
      actionCell.appendChild(span);
    }

    let killButton = actionCell.firstChild;

    killButton.className = "action-icon";

    if (data.type != "browser") {
      // This type of process can be killed.
      if (this._killedRecently.some(kill => kill.pid && kill.pid == data.pid)) {
        // We're racing between the "kill" action and the visual refresh.
        // In a few cases, we could end up with the visual refresh showing
        // a process as un-killed while we actually just killed it.
        //
        // We still want to display the process in case something actually
        // went bad and the user needs the information to realize this.
        // But we also want to make it visible that the process is being
        // killed.
        row.classList.add("killed");
        row.setAttribute("aria-busy", "true");
      } else {
        // Otherwise, let's display the kill button.
        killButton.classList.add("close-icon");
        killButton.setAttribute("role", "button");
        killButton.setAttribute("tabindex", "0");
        let killButtonLabelId = data.type.startsWith("web")
          ? "about-processes-shutdown-process"
          : "about-processes-kill-process";
        document.l10n.setAttributes(killButton, killButtonLabelId);
      }
    }

    return row;
  }

  /**
   * Display a thread summary row with the thread count and a twisty to
   * open/close the list.
   *
   * @param {ProcessDelta} data The data to display.
   * @return {boolean} Whether the full thread list should be displayed.
   */
  displayThreadSummaryRow(data) {
    const cellCount = 2;
    let rowId = "ts:" + data.pid;
    let row = this._getOrCreateRow(rowId, cellCount);
    row.process = data;
    row.className = "thread-summary";
    let isOpen = false;

    // Column: Name
    let nameCell = row.firstChild;
    let threads = data.threads;
    let activeThreads = new Map();
    let activeThreadCount = 0;
    for (let t of data.threads) {
      if (!t.active) {
        continue;
      }
      ++activeThreadCount;
      let name = t.name.replace(/ ?#[0-9]+$/, "");
      if (!activeThreads.has(name)) {
        activeThreads.set(name, { name, slopeCpu: t.slopeCpu, count: 1 });
      } else {
        let thread = activeThreads.get(name);
        thread.count++;
        thread.slopeCpu += t.slopeCpu;
      }
    }
    let fluentName, fluentArgs;
    if (activeThreadCount) {
      let percentFormatter = new Intl.NumberFormat(undefined, {
        style: "percent",
        minimumSignificantDigits: 1,
      });

      let threadList = Array.from(activeThreads.values());
      threadList.sort((t1, t2) => t2.slopeCpu - t1.slopeCpu);

      fluentName = "about-processes-active-threads";
      fluentArgs = {
        number: threads.length,
        active: activeThreadCount,
        list: new Intl.ListFormat(undefined, { style: "narrow" }).format(
          threadList.map(t => {
            let name = t.count > 1 ? `${t.count} × ${t.name}` : t.name;
            let percent = Math.round(t.slopeCpu * 1000) / 1000;
            if (percent) {
              return `${name} ${percentFormatter.format(percent)}`;
            }
            return name;
          })
        ),
      };
    } else {
      fluentName = "about-processes-inactive-threads";
      fluentArgs = {
        number: threads.length,
      };
    }

    let span;
    if (!nameCell.firstChild) {
      nameCell.className = "name indent";
      // Create the nodes:
      let imgButton = document.createElement("span");
      // Provide markup for an accessible disclosure button:
      imgButton.className = "twisty";
      imgButton.setAttribute("role", "button");
      imgButton.setAttribute("tabindex", "0");
      // Label to include both summary and details texts
      imgButton.setAttribute("aria-labelledby", `${data.pid}-label ${rowId}`);
      if (!imgButton.hasAttribute("aria-expanded")) {
        imgButton.setAttribute("aria-expanded", "false");
      }
      nameCell.appendChild(imgButton);

      span = document.createElement("span");
      span.setAttribute("id", rowId);
      nameCell.appendChild(span);
    } else {
      // The only thing that can change is the thread count.
      let imgButton = nameCell.firstChild;
      isOpen = imgButton.classList.contains("open");
      span = imgButton.nextSibling;
    }
    document.l10n.setAttributes(span, fluentName, fluentArgs);

    // Column: action
    let actionCell = nameCell.nextSibling;
    actionCell.className = "action-icon";
    // Note: if/when this action-icon would become a control, ensure it is marked up
    // as a button that is focusable and actionable with keyboard (see killButton)

    return isOpen;
  }

  displayDOMWindowRow(data) {
    const cellCount = 2;
    let rowId = "w:" + data.outerWindowId;
    let row = this._getOrCreateRow(rowId, cellCount);
    row.win = data;
    row.className = "window";

    // Column: name
    let nameCell = row.firstChild;
    let tab = tabFinder.get(data.outerWindowId);
    let fluentName;
    let fluentArgs = {};
    let className;
    if (tab && tab.tabbrowser) {
      fluentName = "about-processes-tab-name";
      fluentArgs.name = tab.tab.label;
      className = "tab";
    } else if (tab) {
      fluentName = "about-processes-preloaded-tab";
      className = "preloaded-tab";
    } else if (data.count == 1) {
      fluentName = "about-processes-frame-name-one";
      fluentArgs.url = data.documentURI.spec;
      className = "frame-one";
    } else {
      fluentName = "about-processes-frame-name-many";
      fluentArgs.number = data.count;
      fluentArgs.shortUrl =
        data.documentURI.scheme == "about"
          ? data.documentURI.spec
          : data.documentURI.prePath;
      className = "frame-many";
    }
    fillCell(nameCell, {
      fluentName,
      fluentArgs,
      classes: ["name", "indent", "favicon", className],
    });
    let image = tab?.tab.getAttribute("image");
    if (image) {
      nameCell.style.backgroundImage = `url('${image}')`;
    }

    // Column: action
    let actionCell = nameCell.nextSibling;

    if (!actionCell.firstChild) {
      let span = document.createElement("span");
      actionCell.appendChild(span);
    }

    let killButton = actionCell.firstChild;

    killButton.className = "action-icon";

    if (data.tab && data.tab.tabbrowser) {
      // A tab. We want to be able to close it.
      if (
        this._killedRecently.some(
          kill => kill.windowId && kill.windowId == data.outerWindowId
        )
      ) {
        // We're racing between the "kill" action and the visual refresh.
        // In a few cases, we could end up with the visual refresh showing
        // a window as un-killed while we actually just killed it.
        //
        // We still want to display the window in case something actually
        // went bad and the user needs the information to realize this.
        // But we also want to make it visible that the window is being
        // killed.
        row.classList.add("killed");
        row.setAttribute("aria-busy", "true");
      } else {
        // Otherwise, let's display the kill button.
        killButton.classList.add("close-icon");
        killButton.setAttribute("role", "button");
        killButton.setAttribute("tabindex", "0");
        document.l10n.setAttributes(killButton, "about-processes-shutdown-tab");
      }
    }
  }

  utilityActorNameToFluentName(actorName) {
    let fluentName;
    switch (actorName) {
      case "audioDecoder_Generic":
        fluentName = "about-processes-utility-actor-audio-decoder-generic";
        break;

      case "audioDecoder_AppleMedia":
        fluentName = "about-processes-utility-actor-audio-decoder-applemedia";
        break;

      case "audioDecoder_WMF":
        fluentName = "about-processes-utility-actor-audio-decoder-wmf";
        break;

      case "mfMediaEngineCDM":
        fluentName = "about-processes-utility-actor-mf-media-engine";
        break;

      case "jSOracle":
        fluentName = "about-processes-utility-actor-js-oracle";
        break;

      case "windowsUtils":
        fluentName = "about-processes-utility-actor-windows-utils";
        break;

      case "windowsFileDialog":
        fluentName = "about-processes-utility-actor-windows-file-dialog";
        break;

      case "pkcs11Module":
        fluentName = "about-processes-utility-actor-pkcs11-module";
        break;

      case "hwInference":
        fluentName = "about-processes-utility-actor-hw-inference";
        break;

      default:
        fluentName = "about-processes-utility-actor-unknown";
        break;
    }
    return fluentName;
  }

  displayUtilityActorRow(data, parent) {
    const cellCount = 2;
    // The actor name is expected to be unique within a given utility process.
    let rowId = "u:" + parent.pid + data.actorName;
    let row = this._getOrCreateRow(rowId, cellCount);
    row.actor = data;
    row.className = "actor";

    // Column: name
    let nameCell = row.firstChild;
    let fluentName = this.utilityActorNameToFluentName(data.actorName);
    let fluentArgs = {};
    fillCell(nameCell, {
      fluentName,
      fluentArgs,
      classes: ["name", "indent", "favicon"],
    });
  }

  /**
   * Display a row showing a single thread.
   *
   * @param {ThreadDelta} data The data to display.
   * @param {number} maxSlopeCpu The largest slopeCpu value.
   */
  displayThreadRow(data, maxSlopeCpu) {
    const cellCount = 3;
    let rowId = "t:" + data.tid;
    let row = this._getOrCreateRow(rowId, cellCount);
    row.thread = data;
    row.className = "thread";

    // Column: name
    let nameCell = row.firstChild;
    fillCell(nameCell, {
      fluentName: "about-processes-thread-name-and-id",
      fluentArgs: {
        name: data.name,
        tid: "" + data.tid /* Make sure that this number is not localized */,
      },
      classes: ["name", "double_indent"],
    });

    // Column: CPU
    this.displayCpu(data, nameCell.nextSibling, maxSlopeCpu);

    // Third column (Buttons) is empty, nothing to do.
  }

  _getDuration(rawDurationNS) {
    if (rawDurationNS <= NS_PER_US) {
      return { duration: rawDurationNS, unit: "ns" };
    }
    if (rawDurationNS <= NS_PER_MS) {
      return { duration: rawDurationNS / NS_PER_US, unit: "us" };
    }
    if (rawDurationNS <= NS_PER_S) {
      return { duration: rawDurationNS / NS_PER_MS, unit: "ms" };
    }
    if (rawDurationNS <= NS_PER_MIN) {
      return { duration: rawDurationNS / NS_PER_S, unit: "s" };
    }
    if (rawDurationNS <= NS_PER_HOUR) {
      return { duration: rawDurationNS / NS_PER_MIN, unit: "m" };
    }
    if (rawDurationNS <= NS_PER_DAY) {
      return { duration: rawDurationNS / NS_PER_HOUR, unit: "h" };
    }
    return { duration: rawDurationNS / NS_PER_DAY, unit: "d" };
  }
}

function updateTriStateCheckbox(checkbox, selectedCount, total) {
  checkbox.checked = !!selectedCount && selectedCount == total;
  checkbox.indeterminate = !!selectedCount && selectedCount < total;
}

// Shared by both view/controller pairs below (ProcessesView and
// ProcessesTabView), so neither needs to reach into the other's instance.
function fillCell(elt, { classes, fluentName, fluentArgs }) {
  document.l10n.setAttributes(elt, fluentName, fluentArgs);
  elt.className = classes.join(" ");
}

/**
 * Format a value representing an amount of memory.
 *
 * As a special case, we also handle `null`, which represents the case in which we do
 * not have sufficient information to compute an amount of memory.
 *
 * @param {number?} value The value to format. Must be either `null` or a non-negative number.
 * @return { {unit: "GB" | "MB" | "KB" | B" | "?"}, amount: Number } The formated amount and its
 *  unit, which may be used for e.g. additional CSS formating.
 */
function formatMemory(value) {
  if (value == null) {
    return { unit: "?", amount: 0 };
  }
  if (typeof value != "number") {
    throw new Error(`Invalid memory value ${value}`);
  }
  let abs = Math.abs(value);
  if (abs >= ONE_GIGA) {
    return {
      unit: "GB",
      amount: value / ONE_GIGA,
    };
  }
  if (abs >= ONE_MEGA) {
    return {
      unit: "MB",
      amount: value / ONE_MEGA,
    };
  }
  if (abs >= ONE_KILO) {
    return {
      unit: "KB",
      amount: value / ONE_KILO,
    };
  }
  return {
    unit: "B",
    amount: value,
  };
}

// Rounded to the units telemetry reports: MB and hundredths of a percent.
function summarizeTabCounters(tabCounters) {
  let { totalRamSize, slopeCpuOfTotal } = sumTabCounters(tabCounters);
  return {
    totalTabCount: tabCounters.length,
    totalRamMb: Math.round(totalRamSize / ONE_MEGA),
    totalCpuHundredths:
      slopeCpuOfTotal == null ? null : Math.round(slopeCpuOfTotal * 10000),
  };
}

// Shared by both view/controller pairs (see the bottom of this file) so
// gLocalizedUnits/gLocalizedProcessProperties get populated regardless of
// which one is entered first.
async function promiseLocalizations() {
  let [
    ns,
    us,
    ms,
    s,
    m,
    h,
    d,
    B,
    KB,
    MB,
    GB,
    TB,
    PB,
    EB,
    privateWindow,
    serviceWorker,
    jitDisabled,
    withCoopCoep,
  ] = await document.l10n.formatValues([
    "duration-unit-ns",
    "duration-unit-us",
    "duration-unit-ms",
    "duration-unit-s",
    "duration-unit-m",
    "duration-unit-h",
    "duration-unit-d",
    "memory-unit-B",
    "memory-unit-KB",
    "memory-unit-MB",
    "memory-unit-GB",
    "memory-unit-TB",
    "memory-unit-PB",
    "memory-unit-EB",
    "about-processes-web-isolated-property-private",
    "about-processes-web-isolated-property-serviceworker",
    "about-processes-web-isolated-property-jit-disabled",
    "about-processes-web-isolated-property-with-coop-coep",
  ]);

  return {
    units: {
      duration: { ns, us, ms, s, m, h, d },
      memory: { B, KB, MB, GB, TB, PB, EB },
    },
    properties: { privateWindow, serviceWorker, jitDisabled, withCoopCoep },
  };
}

class ProcessesController {
  // The set of all processes reported as "hung" by the process hang monitor.
  //
  // type: Set<ChildID>
  _hungItems = new Set();
  _sortColumn = null;
  _sortAscendent = true;
  _lastMouseEvent = 0;

  constructor(view) {
    this._view = view;
  }

  _removeSubtree(row) {
    let sibling = row.nextSibling;
    while (sibling && !sibling.classList.contains("process")) {
      let next = sibling.nextSibling;
      if (sibling.classList.contains("thread")) {
        this._view._removeRow(sibling);
      }
      sibling = next;
    }
  }
  init() {
    this._initHangReports();

    // Start prefetching localizations.
    this._promiseLocalizations = promiseLocalizations();

    let tbody = document.getElementById("process-tbody");

    // Single click:
    // - show or hide the contents of a twisty;
    // - close a process;
    // - profile a process;
    // - change selection.
    tbody.addEventListener("click", event => {
      this._updateLastMouseEvent();

      this._handleActivate(event.target);
    });

    // Enter or Space keypress:
    // - show or hide the contents of a twisty;
    // - close a process;
    // - profile a process;
    // - change selection.
    tbody.addEventListener("keypress", event => {
      // Handle showing or hiding subitems of a row, when keyboard is used.
      if (event.key === "Enter" || event.key === " ") {
        this._handleActivate(event.target);
      }
    });

    // Double click:
    // - navigate to tab;
    // - navigate to about:addons.
    tbody.addEventListener("dblclick", event => {
      this._updateLastMouseEvent();
      event.stopPropagation();

      // Bubble up the doubleclick manually.
      for (
        let target = event.target;
        target && target.getAttribute("id") != "process-tbody";
        target = target.parentNode
      ) {
        if (target.classList.contains("tab")) {
          // We've clicked on a tab, navigate.
          let { tab, tabbrowser } = target.parentNode.win.tab;
          tabbrowser.selectedTab = tab;
          tabbrowser.documentGlobal.focus();
          return;
        }
        if (target.classList.contains("extensions")) {
          // We've clicked on the extensions process, open or reuse window.
          let parentWin =
            window.docShell.browsingContext.embedderElement.documentGlobal;
          parentWin.BrowserAddonUI.openAddonsMgr();
          return;
        }
        // Otherwise, proceed.
      }
    });

    tbody.addEventListener("mousemove", () => {
      this._updateLastMouseEvent();
    });

    // Visibility change:
    // - stop updating while the user isn't looking;
    // - resume updating when the user returns.
    window.addEventListener("visibilitychange", () => {
      if (!document.hidden) {
        this._updateDisplay(true);
      }
    });

    document
      .getElementById("process-thead")
      .addEventListener("click", async event => {
        if (!event.target.classList.contains("clickable")) {
          return;
        }
        // Linux has conventions opposite to Windows and macOS on the direction of arrows
        // when sorting.
        const platformIsLinux = AppConstants.platform == "linux";
        const ascArrow = platformIsLinux ? "arrow-up" : "arrow-down";
        const descArrow = platformIsLinux ? "arrow-down" : "arrow-up";

        if (this._sortColumn) {
          const td = document.getElementById(this._sortColumn);
          // Reset the sorting indication.
          td.setAttribute("aria-sort", "none");
          td.classList.remove(ascArrow, descArrow);
        }

        const columnId = event.target.id;
        if (columnId == this._sortColumn) {
          // Reverse sorting order.
          this._sortAscendent = !this._sortAscendent;
        } else {
          this._sortColumn = columnId;
          this._sortAscendent = true;
        }
        event.target.classList.toggle(ascArrow, this._sortAscendent);
        event.target.classList.toggle(descArrow, !this._sortAscendent);
        event.target.setAttribute(
          "aria-sort",
          this._sortAscendent ? "descending" : "ascending"
        );

        await this._updateDisplay(true);
      });
  }
  _updateLastMouseEvent() {
    this._lastMouseEvent = Date.now();
  }
  _initHangReports() {
    const PROCESS_HANG_REPORT_NOTIFICATION = "process-hang-report";

    // Receiving report of a hung child.
    // Let's store if for our next update.
    let hangReporter = report => {
      report.QueryInterface(Ci.nsIHangReport);
      this._hungItems.add(report.childID);
    };
    Services.obs.addObserver(hangReporter, PROCESS_HANG_REPORT_NOTIFICATION);

    // Don't forget to unregister the reporter.
    window.addEventListener(
      "unload",
      () => {
        Services.obs.removeObserver(
          hangReporter,
          PROCESS_HANG_REPORT_NOTIFICATION
        );
      },
      { once: true }
    );
  }
  async update(force = false) {
    await State.update(force);

    if (document.hidden) {
      return;
    }

    await this._updateDisplay(force);
  }

  // The force parameter can force a full update even when the mouse has been
  // moved recently.
  async _updateDisplay(force = false) {
    let counters = State.getCounters();
    if (this._promiseLocalizations) {
      let { units, properties } = await this._promiseLocalizations;
      gLocalizedUnits = units;
      gLocalizedProcessProperties = properties;
      this._promiseLocalizations = null;
    }

    // We reset `_hungItems`, based on the assumption that the process hang
    // monitor will inform us again before the next update. Since the process hang monitor
    // pings its clients about once per second and we update about once per 2 seconds
    // (or more if the mouse moves), we should be ok.
    let hungItems = this._hungItems;
    this._hungItems = new Set();

    counters = this._sortProcesses(counters);

    // Stored because it is used when opening the list of threads.
    this._maxSlopeCpu = Math.max(...counters.map(process => process.slopeCpu));

    let previousProcess = null;
    for (let process of counters) {
      this._sortDOMWindows(process.windows);

      process.isHung = process.childID && hungItems.has(process.childID);

      let processRow = this._view.displayProcessRow(process, this._maxSlopeCpu);

      if (process.type != "extension") {
        // We do not want to display extensions.
        for (let win of process.windows) {
          if (SHOW_ALL_SUBFRAMES || win.tab || win.isProcessRoot) {
            this._view.displayDOMWindowRow(win, process);
          }
        }
      }

      if (process.type === "utility") {
        for (let actor of process.utilityActors) {
          this._view.displayUtilityActorRow(actor, process);
        }
      }

      if (SHOW_THREADS) {
        if (this._view.displayThreadSummaryRow(process)) {
          this._showThreads(processRow, this._maxSlopeCpu);
        }
      }
      if (
        this._sortColumn == null &&
        previousProcess &&
        previousProcess.displayRank != process.displayRank
      ) {
        // Add a separation between successive categories of processes.
        processRow.classList.add("separate-from-previous-process-group");
      }
      previousProcess = process;
    }

    if (
      !force &&
      Date.now() - this._lastMouseEvent < TIME_BEFORE_SORTING_AGAIN
    ) {
      // If there has been a recent mouse event, we don't want to reorder,
      // add or remove rows so that the table content under the mouse pointer
      // doesn't change when the user might be about to click to close a tab
      // or kill a process.
      // We didn't return earlier because updating CPU and memory values is
      // still valuable.
      this._view.discardUpdate();
      return;
    }

    this._view.commit();

    // Reset the selectedRow field if that row is no longer in the DOM
    // to avoid keeping forever references to dead processes.
    if (this.selectedRow && !this.selectedRow.parentNode) {
      this.selectedRow = null;
    }

    // Used by tests to differentiate full updates from l10n updates.
    document.dispatchEvent(new CustomEvent("AboutProcessesUpdated"));
  }
  _compareCpu(a, b) {
    return (
      b.slopeCpu - a.slopeCpu || b.active - a.active || b.totalCpu - a.totalCpu
    );
  }
  _showThreads(row, maxSlopeCpu) {
    let process = row.process;
    this._sortThreads(process.threads);
    for (let thread of process.threads) {
      this._view.displayThreadRow(thread, maxSlopeCpu);
    }
  }
  _sortThreads(threads) {
    return threads.sort((a, b) => {
      let order;
      switch (this._sortColumn) {
        case "column-name":
          order = a.name.localeCompare(b.name) || a.tid - b.tid;
          break;
        case "column-cpu-total":
          order = this._compareCpu(a, b);
          break;
        case "column-memory-resident":
        case null:
          order = a.tid - b.tid;
          break;
        default:
          throw new Error("Unsupported order: " + this._sortColumn);
      }
      if (!this._sortAscendent) {
        order = -order;
      }
      return order;
    });
  }
  _sortProcesses(counters) {
    return counters.sort((a, b) => {
      let order;
      switch (this._sortColumn) {
        case "column-name":
          order =
            String(a.origin).localeCompare(b.origin) ||
            String(a.type).localeCompare(b.type) ||
            a.pid - b.pid;
          break;
        case "column-cpu-total":
          order = this._compareCpu(a, b);
          break;
        case "column-memory-resident":
          order = b.totalRamSize - a.totalRamSize;
          break;
        case null:
          // Default order: classify processes by group.
          order =
            a.displayRank - b.displayRank ||
            // Other processes are ordered by origin.
            String(a.origin).localeCompare(b.origin);
          break;
        default:
          throw new Error("Unsupported order: " + this._sortColumn);
      }
      if (!this._sortAscendent) {
        order = -order;
      }
      return order;
    });
  }
  _sortDOMWindows(windows) {
    return windows.sort((a, b) => {
      let order =
        a.displayRank - b.displayRank ||
        a.documentTitle.localeCompare(b.documentTitle) ||
        a.documentURI.spec.localeCompare(b.documentURI.spec);
      if (!this._sortAscendent) {
        order = -order;
      }
      return order;
    });
  }

  // Assign a display rank to a process.
  //
  // The `browser` process comes first (rank 0).
  // Then come web tabs (rank 1).
  // Then come web frames (rank 2).
  // Then come special processes (minus preallocated) (rank 3).
  // Then come preallocated processes (rank 4).
  _getDisplayGroupRank(data, windows) {
    const RANK_BROWSER = 0;
    const RANK_WEB_TABS = 1;
    const RANK_WEB_FRAMES = 2;
    const RANK_UTILITY = 3;
    const RANK_PREALLOCATED = 4;
    let type = data.type;
    switch (type) {
      // Browser comes first.
      case "browser":
        return RANK_BROWSER;
      // Web content comes next.
      case "webIsolated":
      case "webServiceWorker":
      case "withCoopCoep": {
        if (windows.some(w => w.tab)) {
          return RANK_WEB_TABS;
        }
        return RANK_WEB_FRAMES;
      }
      // Preallocated processes come last.
      case "preallocated":
        return RANK_PREALLOCATED;
      // "web" is special, as it could be one of:
      // - web content currently loading/unloading/...
      // - a preallocated process.
      case "web":
        if (windows.some(w => w.tab)) {
          return RANK_WEB_TABS;
        }
        if (windows.length >= 1) {
          return RANK_WEB_FRAMES;
        }
        // For the time being, we do not display DOM workers
        // (and there's no API to get information on them).
        // Once the blockers for bug 1663737 have landed, we'll be able
        // to find out whether this process has DOM workers. If so, we'll
        // count this process as a content process.
        return RANK_PREALLOCATED;
      // Other special processes before preallocated.
      default:
        return RANK_UTILITY;
    }
  }

  // Handle events on image controls.
  _handleActivate(target) {
    if (target.classList.contains("twisty")) {
      this._handleTwisty(target);
      return;
    }
    if (target.classList.contains("close-icon")) {
      this._handleKill(target);
      return;
    }

    if (target.classList.contains("profiler-icon")) {
      this._handleProfiling(target);
      return;
    }

    this._handleSelection(target);
  }

  // Open/close list of threads.
  _handleTwisty(target) {
    let row = target.closest("tr");
    if (target.classList.toggle("open")) {
      target.setAttribute("aria-expanded", "true");
      this._showThreads(row, this._maxSlopeCpu);
      this._view.insertAfterRow(row);
    } else {
      target.setAttribute("aria-expanded", "false");
      this._removeSubtree(row);
    }
  }

  // Kill process/close tab/close subframe.
  _handleKill(target) {
    let row = target.closest("tr");
    if (row.process) {
      // Kill process immediately.
      let pid = row.process.pid;

      // Make sure that the user can't click twice on the kill button.
      // Otherwise, chaos might ensue. Plus we risk crashing under Windows.
      this._view._killedRecently.push({ pid });

      // Discard tab contents and show that the process and all its contents are getting killed.
      row.classList.add("killing");
      row.setAttribute("aria-busy", "true");

      // Avoid continuing to show the tooltip when the button isn't visible.
      target.removeAttribute("data-l10n-id");
      target.removeAttribute("title");
      for (
        let childRow = row.nextSibling;
        childRow && !childRow.classList.contains("process");
        childRow = childRow.nextSibling
      ) {
        childRow.classList.add("killing");
        const win = childRow.win;
        if (win) {
          this._view._killedRecently.push({ pid: win.outerWindowId });
          win.tab?.tabbrowser?.discardBrowser(win.tab.tab, {
            forceDiscard: true,
          });
        }
      }

      // Finally, kill the process.
      const ProcessTools = Cc["@mozilla.org/processtools-service;1"].getService(
        Ci.nsIProcessToolsService
      );
      ProcessTools.kill(pid);
    } else if (row.win && row.win.tab && row.win.tab.tabbrowser) {
      // This is a tab, close it.
      row.win.tab.tabbrowser.removeTab(row.win.tab.tab, {
        skipPermitUnload: true,
        animate: true,
      });
      this._view._killedRecently.push({ outerWindowId: row.win.outerWindowId });
      row.classList.add("killing");
      row.setAttribute("aria-busy", "true");
      target.removeAttribute("data-l10n-id");
      target.removeAttribute("title");

      // If this was the only root window of the process, show that the process is also getting killed.
      if (row.previousSibling.classList.contains("process")) {
        let parentRow = row.previousSibling;
        let roots = 0;
        for (let win of parentRow.process.windows) {
          if (win.isProcessRoot) {
            roots += 1;
          }
        }
        if (roots <= 1) {
          // Yes, we're the only process root, so the process is dying.
          //
          // It might actually become a preloaded process rather than
          // dying. That's an acceptable error. Even if we display incorrectly
          // that the process is dying, this error will last only one refresh.
          this._view._killedRecently.push({ pid: parentRow.process.pid });
          parentRow.classList.add("killing");
          let actionIcon = parentRow.querySelector(".action-item");
          actionIcon?.removeAttribute("data-l10n-id");
          actionIcon?.removeAttribute("title");
        }
      }
    }
  }

  // Handle profiling of a process.
  _handleProfiling(target) {
    if (Services.profiler.IsActive()) {
      return;
    }
    Services.profiler.StartProfiler(
      10000000,
      1,
      ["default", "ipcmessages", "power"],
      ["pid:" + target.parentNode.parentNode.process.pid]
    );
    target.classList.add("profiler-active");
    target.setAttribute("aria-pressed", "true");
    setTimeout(() => {
      ProfilerPopupBackground.captureProfile("aboutprofiling");
      target.classList.remove("profiler-active");
      target.setAttribute("aria-pressed", "false");
    }, PROFILE_DURATION * 1000);
  }

  // Handle selection changes.
  _handleSelection(target) {
    let row = target.closest("tr");
    if (!row) {
      return;
    }
    if (this.selectedRow) {
      this.selectedRow.removeAttribute("selected");
      if (this.selectedRow.rowId == row.rowId) {
        // Clicking the same row again clears the selection.
        this.selectedRow = null;
        return;
      }
    }
    row.setAttribute("selected", "true");
    this.selectedRow = row;
  }
}

// Below this share of total capacity (0.005%) the value would display as
// "0.00%", indistinguishable from idle.
const TAB_CPU_ROUNDS_TO_ZERO_THRESHOLD = 0.00005;

// slopeCpuOfTotal is summable across tabs (see TabAttribution.getTabCounters),
// and null if any tab's is still null.
function sumTabCounters(tabCounters) {
  let totalRamSize = 0;
  let slopeCpuOfTotal = 0;
  for (let tabData of tabCounters) {
    totalRamSize += tabData.totalRamSize;
    slopeCpuOfTotal =
      tabData.slopeCpuOfTotal == null || slopeCpuOfTotal == null
        ? null
        : slopeCpuOfTotal + tabData.slopeCpuOfTotal;
  }
  return { totalRamSize, slopeCpuOfTotal };
}

// "—" for tabs with no process to attribute usage from, "…" for CPU not yet
// measured. Clears the l10n attributes so a locale-change re-translation
// can't overwrite the text.
function fillNoDataCell(cell, className, text = "—") {
  cell.className = className;
  cell.removeAttribute("data-l10n-id");
  cell.removeAttribute("data-l10n-args");
  cell.textContent = text;
}

function fillTabMemoryCell(cell, totalRamSize, hasUsageData = true) {
  if (!hasUsageData) {
    fillNoDataCell(cell, "memory");
    return;
  }
  let formatted = formatMemory(totalRamSize);
  fillCell(cell, {
    classes: ["memory"],
    fluentName:
      formatted.unit == "GB"
        ? "about-processes-tab-memory-gb"
        : "about-processes-total-memory-size-no-change",
    fluentArgs: {
      total: formatted.amount,
      totalUnit: gLocalizedUnits.memory[formatted.unit],
    },
  });
}

function fillTabCpuCell(cell, slopeCpuOfTotal, hasUsageData = true) {
  if (!hasUsageData) {
    fillNoDataCell(cell, "cpu");
    return;
  }
  if (slopeCpuOfTotal == null) {
    fillNoDataCell(cell, "cpu", "…");
    return;
  }
  let fluentName = "about-processes-tab-cpu2";
  let fluentArgs = { percent: slopeCpuOfTotal };
  if (slopeCpuOfTotal == 0) {
    fluentName = "about-processes-tab-cpu-fully-idle";
    fluentArgs = undefined;
  } else if (slopeCpuOfTotal < TAB_CPU_ROUNDS_TO_ZERO_THRESHOLD) {
    fluentName = "about-processes-tab-cpu-almost-idle2";
    fluentArgs = undefined;
  }
  fillCell(cell, { classes: ["cpu"], fluentName, fluentArgs });
}

/**
 * A flat, per-tab view. Extends RowSet (see above) for the row-reuse
 * mechanics it shares with ProcessesView; rendering (_createRow/_updateRow)
 * is its own, since the two views show different columns.
 */
class ProcessesTabView extends RowSet {
  #nextRowId = 0;
  #searching = false;

  // groups drives the rows; tabCounters is only for the flat totals-bar sum.
  // During a search, rows outside group.shownTabs are hidden, not removed, so
  // they keep their state. Like row order, that changes only on a reorder.
  commit(tabCounters, groups, { reorder = true, searching = false } = {}) {
    this.#searching = searching;
    let setShown = (row, shown) => {
      if (reorder || !row.parentNode) {
        row.hidden = !shown;
      }
    };
    for (let group of groups) {
      let shownTabs = new Set(group.shownTabs);
      // A new group waits for the reorder, so no row shifts under the cursor.
      if (group.solo || (!reorder && !this.getRow(group.siteKey))) {
        for (let tabData of group.tabs) {
          let row = this._getOrCreateTabRow(tabData);
          row.classList.remove("child-row", "collapsed");
          setShown(row, shownTabs.has(tabData));
        }
        continue;
      }
      let headerRow = this._getOrCreateRow(group.siteKey, () =>
        // A solo tab gaining a sibling opens its new group, so the tab being
        // looked at doesn't disappear.
        this._createHeaderRow(
          group.tabs.some(tabData => {
            let row = this.getRow(tabData.tab);
            return row && !row.classList.contains("child-row");
          })
        )
      );
      setShown(headerRow, shownTabs.size);
      for (let tabData of group.tabs) {
        let row = this._getOrCreateTabRow(tabData);
        row.classList.add("child-row");
        setShown(row, shownTabs.has(tabData));
      }
      this._updateHeaderRow(headerRow, group);
      this.setSiteExpanded(headerRow, headerRow.classList.contains("expanded"));
      this.updateHeaderSelection(headerRow);
    }
    if (reorder) {
      this._commitOrder();
    } else {
      this._discardOrder();
    }
    this._updateTotals(tabCounters);
  }

  _getOrCreateTabRow(tabData) {
    let row = this._getOrCreateRow(tabData.tab, () => this._createTabRow());
    this._updateRow(row, tabData);
    return row;
  }

  // labelId names the row's checkbox.
  _createRow(className, labelId) {
    const cellCount = 5;
    let row = document.createElement("tr");
    row.className = className;
    while (row.children.length < cellCount) {
      row.appendChild(document.createElement("td"));
    }

    let selectCell = row.children[0];
    selectCell.className = "select-cell";
    let checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.className = "select-checkbox";
    checkbox.setAttribute("aria-labelledby", labelId);
    selectCell.appendChild(checkbox);
    row.checkbox = checkbox;

    let unloadButton = document.createElement("button");
    unloadButton.className = "action-icon unload-icon";
    row.children[4].appendChild(unloadButton);
    row.unloadButton = unloadButton;

    return row;
  }

  // Discarded rows stay listed, dimmed, with a reload button in place of
  // unload.
  _updateUnloadButton(row, discarded, fluentId, fluentArgs) {
    row.classList.toggle("killed", discarded);
    row.unloadButton.classList.toggle("unload-icon", !discarded);
    row.unloadButton.classList.toggle("reload-icon", discarded);
    document.l10n.setAttributes(row.unloadButton, fluentId, fluentArgs);
  }

  _createHeaderRow(expanded) {
    let label = document.createElement("span");
    label.className = "site-label";
    label.id = `site-label-${this.#nextRowId++}`;

    let row = this._createRow("site-header-row", label.id);
    row.classList.toggle("expanded", expanded);
    row.label = label;
    let nameCell = row.children[1];

    let caret = document.createElement("button");
    caret.className = "caret-icon ghost-button icon-button";
    caret.setAttribute("aria-labelledby", label.id);
    nameCell.appendChild(caret);
    row.caret = caret;

    let favicon = document.createElement("span");
    favicon.className = "site-favicon";
    nameCell.appendChild(favicon);
    row.favicon = favicon;

    nameCell.appendChild(label);

    let tabCount = document.createElement("span");
    tabCount.className = "site-tab-count";
    // l10n fills the count asynchronously, and an empty span can't be clicked.
    tabCount.textContent = " ";
    nameCell.appendChild(tabCount);
    row.tabCount = tabCount;

    return row;
  }

  // The header row's .expanded class is the group's expanded state. Every
  // group shows expanded during a search, without changing that state.
  setSiteExpanded(row, expanded) {
    row.classList.toggle("expanded", expanded);
    let shownExpanded = expanded || this.#searching;
    row.caret.disabled = this.#searching;
    row.caret.setAttribute("aria-expanded", shownExpanded);
    document.l10n.setAttributes(
      row.caret,
      shownExpanded
        ? "about-processes-collapse-site"
        : "about-processes-expand-site"
    );
    for (let tabData of row.group.tabs) {
      this.getRow(tabData.tab).classList.toggle("collapsed", !shownExpanded);
    }
  }

  getShownChildRows(row) {
    return row.group.tabs
      .map(tabData => this.getRow(tabData.tab))
      .filter(childRow => !childRow.hidden);
  }

  // Any selected child highlights the header; there is no partial shade.
  updateHeaderSelection(row) {
    let rows = this.getShownChildRows(row);
    let selectedCount = rows.filter(
      childRow => childRow.checkbox.checked
    ).length;
    updateTriStateCheckbox(row.checkbox, selectedCount, rows.length);
    row.toggleAttribute("selected", !!selectedCount);
  }

  _updateHeaderRow(row, group) {
    row.group = group;
    row.classList.remove("departed");
    row.inert = false;
    let tabs = this.getShownChildRows(row).map(childRow => childRow.tabData);
    let allDiscarded = tabs.every(tabData => tabData.discarded);
    this._updateUnloadButton(
      row,
      allDiscarded,
      allDiscarded
        ? "about-processes-reload-site"
        : "about-processes-unload-site",
      { site: group.siteLabel }
    );
    let [, nameCell, memoryCell, cpuCell] = row.children;
    nameCell.className = "name";
    // The group's first tab stands in for the site; a real element rather than
    // td.favicon's background, which leaves no room for the caret.
    let image = group.tabs[0].tab.getAttribute("image");
    row.favicon.style.backgroundImage = image ? `url('${image}')` : "";
    row.label.textContent = group.siteLabel;
    document.l10n.setAttributes(
      row.tabCount,
      "about-processes-site-tab-count",
      { count: tabs.length }
    );
    // A fully-discarded group has nothing to sum, which reads as "—", not "0B".
    let hasUsageData = tabs.some(tabData => tabData.hasUsageData);
    let { totalRamSize, slopeCpuOfTotal } = sumTabCounters(tabs);
    fillTabMemoryCell(memoryCell, totalRamSize, hasUsageData);
    fillTabCpuCell(cpuCell, slopeCpuOfTotal, hasUsageData);
  }

  _updateTotals(tabCounters) {
    let [countCell, memoryCell, cpuCell] = document.querySelector(
      "#process-tfoot > tr.totals-row"
    ).children;
    document.l10n.setAttributes(countCell, "about-processes-tab-count", {
      count: tabCounters.length,
    });
    let { totalRamSize, slopeCpuOfTotal } = sumTabCounters(tabCounters);
    let hasUsageData = tabCounters.some(tabData => tabData.hasUsageData);
    fillTabMemoryCell(memoryCell, totalRamSize, hasUsageData);
    fillTabCpuCell(cpuCell, slopeCpuOfTotal, hasUsageData);
  }

  // Unlike ProcessesView.discardUpdate(), appends new rows immediately (no
  // invisible freeze window). Rows that would leave stay in place, dimmed and
  // inert, so nothing shifts under the cursor; the next reorder removes them.
  _discardOrder() {
    let tbody = document.getElementById("process-tbody");
    let live = new Set(this._orderedRows);
    for (let row of tbody.children) {
      if (!live.has(row)) {
        this.setRowSelected(row, false);
        row.classList.add("departed");
        row.inert = true;
      }
    }
    // A new tab in an existing group joins it instead of the end of the list.
    let rows = this._orderedRows;
    for (let [i, row] of rows.entries()) {
      if (row.parentNode) {
        continue;
      }
      if (row.classList.contains("child-row")) {
        rows[i - 1].after(row);
      } else {
        tbody.appendChild(row);
      }
    }
    this._orderedRows = [];
  }

  _createTabRow() {
    let nameLink = document.createElement("button");
    nameLink.className = "name-link text-link";
    nameLink.id = `tab-name-${this.#nextRowId++}`;

    let row = this._createRow("tab-row", nameLink.id);
    row.children[1].appendChild(nameLink);
    row.nameLink = nameLink;
    return row;
  }

  setRowSelected(row, isSelected) {
    row.toggleAttribute("selected", isSelected);
    row.checkbox.checked = isSelected;
  }

  _updateRow(row, tabData) {
    row.tabData = tabData;
    row.classList.remove("departed");
    row.inert = false;

    if (tabData.discarded) {
      row.classList.remove("killing");
    }
    this._updateUnloadButton(
      row,
      !!tabData.discarded,
      tabData.discarded
        ? "about-processes-reload-tab"
        : "about-processes-unload-tab"
    );

    let [, nameCell, memoryCell, cpuCell] = row.children;
    nameCell.className = "name favicon";
    row.nameLink.textContent = tabData.title || tabData.uri?.spec || "";
    // Rows are reused across polls, so a stale image needs clearing, not
    // just skipping, when the current tab has none.
    let image = tabData.tab.getAttribute("image");
    nameCell.style.backgroundImage = image ? `url('${image}')` : "";

    fillTabMemoryCell(memoryCell, tabData.totalRamSize, tabData.hasUsageData);
    fillTabCpuCell(cpuCell, tabData.slopeCpuOfTotal, tabData.hasUsageData);
  }
}

class ProcessesTabController {
  // Reuses the default view's column header markup, but unlike that view,
  // ascending always means the up caret here -- this view's columns don't
  // have per-column sort-direction semantics.
  _sortColumn = null;
  _sortAscendent = false;
  _lastTabCounters = null;
  _lastMouseEvent = 0;
  _panelOpenedRecorded = false;
  // From the openType URL param SidebarController appends; absent means the
  // page was reached without the sidebar.
  _openType = "direct_nav";
  // Trimmed, original casing (echoed in the no-results message); "" means no
  // filter.
  _searchQuery = "";
  _noResultsQuery = "";

  constructor(view) {
    this._view = view;
    this._tabAttribution = new TabAttribution();
  }

  init() {
    let openType = new URLSearchParams(location.search).get("openType");
    if (["sidebar_launch", "restored", "other"].includes(openType)) {
      this._openType = openType;
    }

    // Prefetch localizations: this view's entry point doesn't go through
    // ProcessesController.init(), so gLocalizedUnits isn't populated yet.
    this._promiseLocalizations = promiseLocalizations();
    this._promiseCpuCount = this._tabAttribution.init();

    // The CPU header is shared with the default view, but means % of total
    // capacity here rather than % of one core -- swap in a tooltip
    // clarifying that, since the visible header text stays the same.
    document.l10n.setAttributes(
      document.getElementById("column-cpu-total"),
      "about-processes-column-cpu-total-tab"
    );

    // Shortened here rather than renaming the shared "Memory" string, to
    // reclaim width in this narrower view without changing the default one.
    document.l10n.setAttributes(
      document.getElementById("column-memory-resident"),
      "about-processes-column-memory-resident-tab"
    );

    this._selectAllCheckbox = document.getElementById("select-all-checkbox");
    this._selectAllCheckbox.addEventListener("click", () => {
      this._setRowsSelected(
        this._getDisplayedRows(),
        this._selectAllCheckbox.checked
      );
    });

    this._bulkUnloadButton = document.getElementById("bulk-unload-button");
    this._bulkReloadButton = document.getElementById("bulk-reload-button");
    this._bulkUnloadButton.addEventListener("click", () =>
      this._unloadOrReloadSelectedTabs(false)
    );
    this._bulkReloadButton.addEventListener("click", () =>
      this._unloadOrReloadSelectedTabs(true)
    );
    document
      .getElementById("bulk-close-button")
      .addEventListener("click", () => this._closeSelectedTabs());
    document
      .getElementById("bulk-cancel-button")
      .addEventListener("click", () => this._clearSelection());

    this._expandAllButton = document.getElementById("expand-all-button");
    this._expandAllButton.addEventListener("click", () =>
      this._toggleAllSites()
    );

    let searchInput = document.getElementById("tab-search-input");
    searchInput.addEventListener("MozInputSearch:search", event => {
      let query = event.detail.query.trim();
      if (query == this._searchQuery) {
        return;
      }
      this._searchQuery = query;
      if (this._lastTabCounters) {
        this._commitView(this._lastTabCounters, { force: true });
      }
    });

    let tbody = document.getElementById("process-tbody");

    // One delegate at the tbody level, matching ProcessesController.init()'s
    // own pattern, rather than a listener per row. Every control is a native
    // button or checkbox, so keyboard activation arrives here as a click too,
    // and starts the mouse freeze the same way.
    tbody.addEventListener("click", event => {
      this._lastMouseEvent = Date.now();
      let headerRow = event.target.closest("tr.site-header-row");
      if (headerRow) {
        // The label and count are mouse-only; the caret is the keyboard path.
        if (event.target.closest(".caret-icon, .site-label, .site-tab-count")) {
          if (!headerRow.caret.disabled) {
            this._toggleSite(headerRow);
          }
          return;
        }
        this._handleActivate(event.target);
        if (event.target.closest(".select-checkbox")) {
          this._setRowsSelected(
            this._view.getShownChildRows(headerRow),
            event.target.checked
          );
        }
        return;
      }
      this._handleActivate(event.target);
      let row = event.target.closest("tr.tab-row");
      if (row) {
        this._handleRowClick(event, row);
      }
    });

    tbody.addEventListener("mousemove", () => {
      this._lastMouseEvent = Date.now();
    });

    // Escape doesn't fire keypress, so it needs its own listener to clear a selection.
    document.addEventListener("keydown", event => {
      // Escape in the search box is for the search, not the selection.
      if (
        event.key === "Escape" &&
        this._getSelectedTabData().length &&
        !event.target.closest("moz-input-search")
      ) {
        event.preventDefault();
        this._clearSelection();
      }
    });

    // State already re-polls every interval tick regardless of
    // document.hidden, so re-derive tab counters from it here instead of
    // forcing another ProcInfo snapshot on resume.
    window.addEventListener("visibilitychange", () => {
      if (!document.hidden) {
        this._rerender({ force: true });
      }
    });

    document
      .getElementById("process-thead")
      .addEventListener("click", event => {
        if (!event.target.classList.contains("clickable")) {
          return;
        }
        const columnId = event.target.id;
        let ascending =
          columnId == this._sortColumn
            ? !this._sortAscendent
            : // Resource columns start with the busiest tabs first.
              columnId == "column-name";
        this._setSortIndicator(columnId, ascending);
        this._resort();
      });

    // Match the real initial sort (memory, descending) with a visible
    // indicator -- otherwise the first click on Memory looks like a no-op.
    this._setSortIndicator("column-memory-resident", false);
  }

  _setSortIndicator(columnId, ascending) {
    const ascArrow = "arrow-up";
    const descArrow = "arrow-down";
    if (this._sortColumn) {
      let previous = document.getElementById(this._sortColumn);
      previous.setAttribute("aria-sort", "none");
      previous.classList.remove(ascArrow, descArrow);
    }
    this._sortColumn = columnId;
    this._sortAscendent = ascending;
    let header = document.getElementById(columnId);
    header.classList.toggle(ascArrow, ascending);
    header.classList.toggle(descArrow, !ascending);
    header.setAttribute("aria-sort", ascending ? "ascending" : "descending");
  }

  _handleActivate(target) {
    // Not .unload-icon: that class is swapped for .reload-icon once
    // discarded, but the button itself is still the only activation target.
    if (!target.classList.contains("action-icon")) {
      return;
    }
    let headerRow = target.closest("tr.site-header-row");
    if (headerRow) {
      this._runRowAction(headerRow, () =>
        this._unloadOrReloadSiteRow(
          headerRow,
          this._view
            .getShownChildRows(headerRow)
            .every(row => row.tabData.discarded)
        )
      );
      return;
    }
    let row = target.closest("tr.tab-row");
    this._runRowAction(row, () =>
      row.tabData.discarded ? this._reloadRow(row) : this._unloadRow(row)
    );
  }

  _navigateToTab(row) {
    this._recordTabAction([row.tabData], "navigate", "tab_row");
    let { tab, tabbrowser } = row.tabData;
    tabbrowser.selectedTab = tab;
    tabbrowser.documentGlobal.focus();
  }

  _handleRowClick(event, row) {
    if (event.target.closest(".select-checkbox")) {
      this._setRowsSelected([row], event.target.checked);
      return;
    }
    if (event.target.closest(".name-link")) {
      this._navigateToTab(row);
    }
  }

  // Shown rows of tabs from the last poll. Rows outlive polls, so their
  // checkboxes hold the selection.
  _getDisplayedRows() {
    return (this._lastTabCounters || [])
      .map(tabData => this._view.getRow(tabData.tab))
      .filter(row => !row.hidden);
  }

  _getSelectedTabData() {
    return this._getDisplayedRows()
      .filter(row => row.checkbox.checked)
      .map(row => row.tabData);
  }

  _setRowsSelected(rows, selected) {
    for (let row of rows) {
      this._view.setRowSelected(row, selected);
    }
    this._updateSelectAllCheckbox();
    for (let headerRow of this._getHeaderRows()) {
      this._view.updateHeaderSelection(headerRow);
    }
    this._updateBulkActionBar();
  }

  _updateSelectAllCheckbox() {
    let rows = this._getDisplayedRows();
    updateTriStateCheckbox(
      this._selectAllCheckbox,
      rows.filter(row => row.checkbox.checked).length,
      rows.length
    );
  }

  _clearSelection() {
    this._setRowsSelected(this._getDisplayedRows(), false);
  }

  _updateBulkActionBar() {
    let selectedTabData = this._getSelectedTabData();
    let selectedCount = selectedTabData.length;
    document.body.classList.toggle("bulk-action-visible", !!selectedCount);
    if (!selectedCount) {
      return;
    }

    this._bulkUnloadButton.hidden = selectedTabData.every(
      tabData => tabData.discarded
    );

    let [, countCell, memoryCell, cpuCell] = document.querySelector(
      "#process-tfoot > tr.bulk-summary-row"
    ).children;
    document.l10n.setAttributes(
      countCell,
      "about-processes-bulk-selected-count",
      { count: selectedCount }
    );
    let { totalRamSize, slopeCpuOfTotal } = sumTabCounters(selectedTabData);
    let hasUsageData = selectedTabData.some(tabData => tabData.hasUsageData);
    fillTabMemoryCell(memoryCell, totalRamSize, hasUsageData);
    fillTabCpuCell(cpuCell, slopeCpuOfTotal, hasUsageData);
  }

  // Tab actions are per-tabbrowser, so cross-window selections must be grouped.
  _groupTabsByTabbrowser(tabDataList) {
    let groups = new Map();
    for (let { tab, tabbrowser } of tabDataList) {
      let tabs = groups.get(tabbrowser);
      if (!tabs) {
        tabs = [];
        groups.set(tabbrowser, tabs);
      }
      tabs.push(tab);
    }
    return groups;
  }

  async _unloadOrReloadTabs(tabDataList, reload) {
    for (let [tabbrowser, tabs] of this._groupTabsByTabbrowser(tabDataList)) {
      if (reload) {
        tabbrowser.reloadTabs(tabs);
      } else {
        await tabbrowser.explicitUnloadTabs(tabs);
      }
    }
  }

  async _unloadOrReloadSelectedTabs(reload) {
    let eligible = this._getSelectedTabData();
    if (!reload) {
      eligible = eligible.filter(tabData => !tabData.discarded);
    }
    if (eligible.length) {
      this._recordTabAction(eligible, reload ? "reload" : "unload", "bulk_bar");
    }
    this._clearSelection();
    await this._unloadOrReloadTabs(eligible, reload);
    this._rerender({ force: true });
  }

  async _closeSelectedTabs() {
    // Warns from this panel's own window, even for a cross-window selection.
    let ownTabbrowser = window.browsingContext.topChromeWindow.gBrowser;
    let selectedTabData = this._getSelectedTabData();
    if (
      !ownTabbrowser.warnAboutClosingTabs(
        selectedTabData.length,
        ownTabbrowser.constructor.closingTabsEnum.MULTI_SELECTED
      )
    ) {
      return;
    }
    this._recordTabAction(selectedTabData, "close", "bulk_bar");
    for (let [tabbrowser, tabs] of this._groupTabsByTabbrowser(
      selectedTabData
    )) {
      // animate: false so tabs are gone before the eager refresh below runs.
      tabbrowser.removeTabs(tabs, { animate: false });
    }
    this._clearSelection();
    // Forced: the selection's rows go now, rather than dimming out the freeze.
    this._rerender({ force: true });
  }

  // Unloads rather than closes, then dims the row and confirms via an
  // eager refresh. Not removeTab(): reload-on-reselect brings it back.
  async _unloadRow(row) {
    this._recordTabAction([row.tabData], "unload", "tab_row");
    row.classList.add("killing");
    let { tab, tabbrowser } = row.tabData;
    await tabbrowser.explicitUnloadTabs([tab]);
    if (!tab.hasAttribute("discarded")) {
      // The tab was ineligible for unload -- don't leave it looking busy.
      row.classList.remove("killing");
    }
    this._rerender();
  }

  _reloadRow(row) {
    this._recordTabAction([row.tabData], "reload", "tab_row");
    row.tabData.tabbrowser.reloadTab(row.tabData.tab);
    this._rerender();
  }

  // Ignores repeat activations, like a double-click, until the action has
  // finished and ACTION_REPEAT_DELAY_MS has passed.
  async _runRowAction(row, action) {
    if (row.hasAttribute("aria-busy")) {
      return;
    }
    row.setAttribute("aria-busy", "true");
    try {
      await Promise.all([
        action(),
        new Promise(resolve => setTimeout(resolve, ACTION_REPEAT_DELAY_MS)),
      ]);
    } finally {
      row.removeAttribute("aria-busy");
    }
  }

  // Doesn't skip the active tab, matching per-row and bulk-bar unload.
  async _unloadOrReloadSiteRow(row, reload) {
    let eligible = this._view
      .getShownChildRows(row)
      .map(childRow => childRow.tabData)
      .filter(tabData => !!tabData.discarded == reload);
    if (eligible.length) {
      this._recordTabAction(
        eligible,
        reload ? "reload" : "unload",
        "site_header"
      );
    }
    await this._unloadOrReloadTabs(eligible, reload);
    this._rerender();
  }

  _recordTabAction(tabDataList, actionType, actionSource) {
    let affected = summarizeTabCounters(tabDataList);
    let all = summarizeTabCounters(this._lastTabCounters);
    Glean.resourceMonitor.tabActionTaken.record({
      tab_count: affected.totalTabCount,
      action_type: actionType,
      tab_ram_mb: affected.totalRamMb,
      tab_cpu_hundredths: affected.totalCpuHundredths,
      total_tab_count: all.totalTabCount,
      total_ram_mb: all.totalRamMb,
      total_cpu_hundredths: all.totalCpuHundredths,
      action_source: actionSource,
    });
  }

  // Re-reads tab state (closed, discarded, titles) against the last sample.
  _rerender({ force = false } = {}) {
    let tabCounters = this._tabAttribution.getTabCounters(State.getCounters());
    this._lastTabCounters = tabCounters;
    this._sortTabCounters(tabCounters);
    this._commitView(tabCounters, { force });
  }

  // Re-sorts/re-renders from the last sample rather than update(): resampling
  // CPU right after the last poll amplifies slopeCpu's noise. Forces the
  // reorder despite a recent mouse event too -- sorting was the explicit ask,
  // so freezing here would just look broken.
  _resort() {
    if (!this._lastTabCounters) {
      return;
    }
    this._sortTabCounters(this._lastTabCounters);
    this._commitView(this._lastTabCounters, { force: true });
  }

  // Also ranks groups, whose aggregates share tabData's field names.
  _getSortOrder() {
    switch (this._sortColumn) {
      case "column-name":
        return (a, b) =>
          (a.title || "").localeCompare(b.title || "") ||
          (a.uri?.spec ?? "").localeCompare(b.uri?.spec ?? "");
      case "column-cpu-total":
        return (a, b) => a.slopeCpuOfTotal - b.slopeCpuOfTotal;
      case "column-memory-resident":
        return (a, b) => a.totalRamSize - b.totalRamSize;
      default:
        throw new Error("Unsupported order: " + this._sortColumn);
    }
  }

  _sortTabCounters(tabCounters) {
    let order = this._getSortOrder();
    tabCounters.sort(this._sortAscendent ? order : (a, b) => order(b, a));
  }

  // Groups the sorted tabCounters by site, keeping each site's tabs in order,
  // then ranks the groups by the same column, by their displayed name.
  _groupTabsBySite(tabCounters, reorder) {
    let displayedGroups = new Map();
    if (!reorder) {
      for (let row of this._getHeaderRows()) {
        for (let { tab } of row.group.tabs) {
          displayedGroups.set(tab, row.group);
        }
      }
    }
    let groups = [];
    for (let [siteKey, tabs] of Map.groupBy(
      tabCounters,
      tabData =>
        displayedGroups.get(tabData.tab)?.siteKey ??
        (!reorder && this._view.getRow(tabData.tab)
          ? tabData.tab
          : tabData.siteKey)
    )) {
      let displayedGroup = !reorder && this._view.getRow(siteKey)?.group;
      let solo = !displayedGroup && tabs.length == 1;
      // Selected tabs stay shown, so bulk actions only act on shown tabs.
      let shownTabs = this._searchQuery
        ? tabs.filter(
            tabData =>
              this._matchesSearch(tabData) ||
              this._view.getRow(tabData.tab)?.checkbox.checked
          )
        : tabs;
      let { totalRamSize, slopeCpuOfTotal } = sumTabCounters(shownTabs);
      let siteLabel = displayedGroup?.siteLabel ?? tabs[0].siteLabel;
      groups.push({
        siteKey,
        siteLabel,
        tabs,
        shownTabs,
        solo,
        title: solo ? tabs[0].title || tabs[0].uri?.spec : siteLabel,
        totalRamSize,
        slopeCpuOfTotal,
      });
    }
    let order = this._getSortOrder();
    groups.sort(this._sortAscendent ? order : (a, b) => order(b, a));
    return groups;
  }

  // Matches what the row shows: the title, or the URL without one, or the
  // site's label.
  _matchesSearch(tabData) {
    let query = this._searchQuery.toLowerCase();
    return [tabData.title || tabData.uri?.spec || "", tabData.siteLabel].some(
      text => text.toLowerCase().includes(query)
    );
  }

  // Only on change: the message is a live region, announced on each update.
  _updateNoResults(tabCounters) {
    let query = tabCounters.some(tabData => this._matchesSearch(tabData))
      ? ""
      : this._searchQuery;
    if (query == this._noResultsQuery) {
      return;
    }
    this._noResultsQuery = query;
    let message = document.getElementById("tab-search-no-results");
    if (query) {
      document.l10n.setAttributes(
        message,
        "about-processes-search-no-results",
        { query }
      );
    } else {
      message.removeAttribute("data-l10n-id");
      message.removeAttribute("data-l10n-args");
      message.textContent = "";
    }
  }

  _getHeaderRows() {
    return document.querySelectorAll("#process-tbody > tr.site-header-row");
  }

  _updateExpandAllButton() {
    let headerRows = [...this._getHeaderRows()].filter(row => !row.hidden);
    let allExpanded =
      !!headerRows.length &&
      headerRows.every(row => row.classList.contains("expanded"));
    this._expandAllButton.classList.toggle("expanded", allExpanded);
    // Every group shows expanded during a search.
    this._expandAllButton.disabled = !headerRows.length || !!this._searchQuery;
    document.l10n.setAttributes(
      this._expandAllButton,
      allExpanded
        ? "about-processes-collapse-all-sites"
        : "about-processes-expand-all-sites"
    );
  }

  _toggleSite(row) {
    this._view.setSiteExpanded(row, !row.classList.contains("expanded"));
    this._updateExpandAllButton();
  }

  _toggleAllSites() {
    let expanded = !this._expandAllButton.classList.contains("expanded");
    for (let row of this._getHeaderRows()) {
      this._view.setSiteExpanded(row, expanded);
    }
    this._updateExpandAllButton();
  }

  _commitView(tabCounters, { force = false } = {}) {
    // If there's been a recent mouse event, don't reorder or remove rows,
    // so the row under the cursor doesn't shift right as the user is about
    // to click its unload button. Matches ProcessesController._updateDisplay.
    let reorder =
      force || Date.now() - this._lastMouseEvent >= TIME_BEFORE_SORTING_AGAIN;
    let groups = this._groupTabsBySite(tabCounters, reorder);
    this._view.commit(tabCounters, groups, {
      reorder,
      searching: !!this._searchQuery,
    });
    this._updateExpandAllButton();
    this._updateNoResults(tabCounters);
    this._updateSelectAllCheckbox();
    this._updateBulkActionBar();
    document.dispatchEvent(new CustomEvent("AboutProcessesUpdated"));
  }

  async update() {
    // force=true is intentional, unlike the default view: right after
    // opening the panel this poll can beat MINIMUM_INTERVAL_BETWEEN_SAMPLES_MS,
    // and skipping the real snapshot then races a just-opened tab's title
    // propagating from content to chrome -- a no-op during regular polling,
    // since UPDATE_INTERVAL_MS always exceeds that window anyway.
    await State.update(true);
    if (document.hidden) {
      return;
    }
    if (this._promiseLocalizations) {
      let { units, properties } = await this._promiseLocalizations;
      gLocalizedUnits = units;
      gLocalizedProcessProperties = properties;
      this._promiseLocalizations = null;
    }
    if (this._promiseCpuCount) {
      await this._promiseCpuCount;
      this._promiseCpuCount = null;
    }
    this._rerender();

    // Once CPU is known, which takes a second sample.
    if (
      !this._panelOpenedRecorded &&
      sumTabCounters(this._lastTabCounters).slopeCpuOfTotal != null
    ) {
      this._panelOpenedRecorded = true;
      let all = summarizeTabCounters(this._lastTabCounters);
      Glean.resourceMonitor.panelOpened.record({
        total_tab_count: all.totalTabCount,
        total_ram_mb: all.totalRamMb,
        total_cpu_hundredths: all.totalCpuHundredths,
        open_type: this._openType,
      });
    }
  }
}

// Instantiable, not static, so a future URL-parameter-selected view (e.g. a
// per-tab/site grouping) can be added as its own View/Controller pair
// without touching this one.
var View = new ProcessesView();
var Control = new ProcessesController(View);
var TabView = new ProcessesTabView();
var TabControl = new ProcessesTabController(TabView);

window.onload = async function () {
  let groupBy = new URLSearchParams(location.search).get("groupby");
  if (groupBy == "tab") {
    document.body.classList.add("tab-view");
    TabControl.init();
    await TabControl.update();
    window.setInterval(() => TabControl.update(), UPDATE_INTERVAL_MS);
    return;
  }

  Control.init();

  // Display immediately the list of processes. CPU values will be missing.
  await Control.update();

  // After the minimum interval between samples, force an update to show
  // valid CPU values asap.
  await new Promise(resolve =>
    setTimeout(resolve, MINIMUM_INTERVAL_BETWEEN_SAMPLES_MS)
  );
  await Control.update(true);

  // Then update at the normal frequency.
  window.setInterval(() => Control.update(), UPDATE_INTERVAL_MS);
};
