/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  SMART_TAB_GROUPING_CONFIG:
    "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs",
  SmartTabGroupingManager:
    "moz-src:///browser/components/tabbrowser/SmartTabGrouping.sys.mjs",
  buildConversation:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  loadPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  MODEL_FEATURES: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  renderPrompt: "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
  openAIEngine:
    "moz-src:///browser/components/aiwindow/models/openAIEngine.sys.mjs",
  sanitizeUntrustedContent:
    "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs",
});

// Dedicated topic-model slot so the SW naming model can be updated independently
// of the shared Smart Tab Grouping model. The model + revision resolve from the
// smart-window-tab-topic Remote Settings inference-options record.
const SW_TOPIC_FEATURE_ID = "smart-window-tab-topic";
const SW_TOPIC_ENGINE_ID = "smart-window-tab-topic-engine";

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "mlUtils",
  "@mozilla.org/ml-utils;1",
  Ci.nsIMLUtils
);

ChromeUtils.defineLazyGetter(lazy, "console", () =>
  console.createInstance({
    prefix: "AutoTabGrouping",
    maxLogLevelPref: "browser.smartwindow.autoTabGrouping.loglevel",
  })
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "maxGroups",
  "browser.smartwindow.autoTabGrouping.maxGroups",
  3
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "minTabsPerGroup",
  "browser.smartwindow.autoTabGrouping.minTabsPerGroup",
  2
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "preloadEnabled",
  "browser.smartwindow.autoTabGrouping.preloadModels",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "checkForMemory",
  "browser.ml.checkForMemory",
  true
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "minimumPhysicalMemoryGiB",
  "browser.ml.minimumPhysicalMemory",
  3
);

const ONE_GIB = 1024 ** 3;

// Drop clusters whose cohesion (average pairwise cosine similarity of the tabs'
// embeddings, 0..1, set by SmartTabGrouping) is below this, so weakly-related
// tabs are not offered as a group. See SmartTabGroupingResult.getCohesion.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "minCohesion",
  "browser.smartwindow.autoTabGrouping.minCohesion",
  "0.15",
  null,
  value => parseFloat(value)
);

// Tab-group color names, mirroring MozTabbrowserTabGroupMenu.COLORS. Each name
// resolves to the themed --tab-group-<name> custom property (defined on :root),
// so created groups reuse the real tab-strip palette rather than hardcoded
// values. Assigned to suggestions in order.
const TAB_GROUP_COLORS = [
  "blue",
  "purple",
  "cyan",
  "orange",
  "yellow",
  "pink",
  "green",
  "gray",
  "red",
];

const DEFAULT_FAVICON_URL = "chrome://global/skin/icons/defaultFavicon.svg";

/**
 * The suggestion engine for the Smart Window "Organize Tabs" feature: the only
 * code that talks to the on-device clustering model. Given a window it picks
 * candidate tabs, clusters and labels them, and turns each cluster into the
 * display data the panel shows: a name, a color, and one row per tab. Kept
 * free of any DOM so the pure parts stay unit-testable and every model call is
 * guarded against throwing.
 */
const MAX_LABEL_CACHE_ENTRIES = 50;

// Cap how many tab titles are sent to the LLM for naming, mirroring the
// on-device path (SmartTabGrouping.getRepresentativeDocuments), to bound the
// untrusted content in the prompt. Per-title length is capped by
// sanitizeUntrustedContent.
const MAX_LABEL_TABS = 10;

const MAX_LABEL_LENGTH = 25;

export const AutoTabGroupingSuggestions = {
  _manager: null,

  _preloadPromise: null,

  // Reopening the panel over an unchanged set of tabs reuses the title
  // instead of re-running the labeling model.
  _labelCache: new Map(),

  // Organize Tabs is only reachable from an active Smart Window, which the user
  // has already opted into, so the extra opt-in prefs that
  // SmartTabGroupingManager.isEnabled checks for the tab strip's own suggestions
  // don't apply here. All we need is on-device ML and isAllowed's locale check.
  get isAvailable() {
    return (
      Services.prefs.getBoolPref("browser.ml.enable", false) &&
      lazy.SmartTabGroupingManager.isAllowed &&
      this.hasEnoughMemory
    );
  },

  /**
   * We want to gate the Auto Tab Grouping button from the user if their machine
   * does not have the required memory
   *
   * @returns {boolean}
   */
  get hasEnoughMemory() {
    return (
      !lazy.checkForMemory ||
      lazy.mlUtils.totalPhysicalMemory >=
        lazy.minimumPhysicalMemoryGiB * ONE_GIB
    );
  },

  get manager() {
    if (!this._manager) {
      const config = structuredClone(lazy.SMART_TAB_GROUPING_CONFIG);
      config.topicGeneration.featureId = SW_TOPIC_FEATURE_ID;
      config.topicGeneration.engineId = SW_TOPIC_ENGINE_ID;
      this._manager = new lazy.SmartTabGroupingManager(config);
    }
    return this._manager;
  },

  /**
   * Downloads the clustering and labeling models, so the first panel open runs
   * against a warm cache. Repeated calls share one download and never reject. A
   * failed download is not retried: every new Smart Window would try again, and
   * opening the panel downloads the models anyway.
   *
   * Gated on its own pref, which test profiles turn off: this is the only place
   * that reaches the model hub without the user asking for a grouping, so it
   * would otherwise download models in every test that opens a Smart Window.
   *
   * @returns {Promise<void>}
   */
  preloadModels() {
    if (!lazy.preloadEnabled || !this.isAvailable) {
      return Promise.resolve();
    }
    if (!this._preloadPromise) {
      this._preloadPromise = this._preloadModels();
    }
    return this._preloadPromise;
  },

  async _preloadModels() {
    try {
      await this.manager.preloadAllModels();
    } catch (e) {
      lazy.console.warn("Preloading the tab grouping models failed", e);
    }
  },

  /**
   * Ungrouped, non-pinned, web-content tabs eligible for clustering. Tabs that
   * are already in a group are excluded so we never re-suggest an existing
   * group.
   *
   * @param {ChromeWindow} win
   * @returns {MozTabbrowserTab[]}
   */
  getCandidateTabs(win) {
    return win.gBrowser.tabs.filter(tab => {
      if (tab.pinned || tab.closing || tab.group || tab.hidden) {
        return false;
      }
      if (tab.hasAttribute("busy") || !tab.label) {
        return false;
      }
      const uri = tab.linkedBrowser?.currentURI;
      // Only cluster real web content; skip about:, chrome:, the Smart Window
      // new tab / chat pages, etc.
      return uri && (uri.schemeIs("http") || uri.schemeIs("https"));
    });
  },

  /**
   * Run clustering + labeling and return the top proposals.
   *
   * @param {MozTabbrowserTab[]} candidates
   * @returns {Promise<Array<{label: string, tabs: MozTabbrowserTab[]}>>}
   */
  async buildProposals(candidates) {
    const result = await this.manager.generateClusters(candidates, null, 0);
    const clusters = this.selectClusters(result?.clusterRepresentations);
    if (!clusters.length) {
      return [];
    }

    const groupedTabs = new Set(clusters.flatMap(c => c.tabs));
    const otherTabs = candidates.filter(t => !groupedTabs.has(t));

    // Label groups in parallel: LLM naming is a network call, so labeling them
    // serially would stack the latency across groups.
    const labeled = await Promise.all(
      clusters.map(async cluster => {
        try {
          const label = await this._labelForGroup(cluster.tabs, otherTabs);
          return { label: label?.trim(), tabs: cluster.tabs };
        } catch (e) {
          lazy.console.warn("Label generation failed", e);
          return { label: "", tabs: cluster.tabs };
        }
      })
    );
    // Drop groups the model left unlabeled: an empty title usually flags
    // content it declined to label (often a Trust & Safety case).
    return labeled.filter(proposal => proposal.label);
  },

  /**
   * Predicted label for a group, cached by its source tabs' URLs.
   *
   * @param {MozTabbrowserTab[]} tabs
   * @param {MozTabbrowserTab[]} otherTabs
   * @returns {Promise<string>}
   */
  async _labelForGroup(tabs, otherTabs) {
    const key = tabs
      .map(t => t.linkedBrowser?.currentURI?.spec ?? "")
      .sort()
      .join("\n");
    if (this._labelCache.has(key)) {
      return this._labelCache.get(key);
    }
    // Fall back to on-device only when the LLM throws (unavailable). A successful
    // empty result is a deliberate decline (NSFW); keep it so the group is dropped.
    let label;
    try {
      label = await this._llmLabelForGroup(tabs);
    } catch (e) {
      lazy.console.warn("LLM labeling failed; falling back to on-device", e);
      label = await this.manager.getPredictedLabelForGroup(tabs, otherTabs);
    }
    if (this._labelCache.size >= MAX_LABEL_CACHE_ENTRIES) {
      this._labelCache.delete(this._labelCache.keys().next().value);
    }
    this._labelCache.set(key, label);
    return label;
  },

  // Name a group with the Smart Window LLM. Throws when unavailable (caller falls
  // back to on-device); returns "" on a deliberate decline (NSFW), which drops it.
  async _llmLabelForGroup(tabs) {
    const fxAccountToken = await lazy.openAIEngine.getFxAccountToken();
    if (!fxAccountToken) {
      throw new Error("Smart Window LLM unavailable: no account token");
    }
    const titles = tabs
      .slice(0, MAX_LABEL_TABS)
      .map(tab => lazy.sanitizeUntrustedContent(tab.label || ""))
      .filter(Boolean)
      .join("\n");
    const conversation = await lazy.buildConversation(
      lazy.MODEL_FEATURES.TAB_GROUP_NAMING,
      {}
    );
    const model = conversation.engine.model;
    const [{ prompt: systemPrompt }, { prompt: userTemplate }] =
      await Promise.all([
        lazy.loadPrompt(lazy.MODEL_FEATURES.TAB_GROUP_NAMING, {
          module: "system-instructions",
          model,
        }),
        lazy.loadPrompt(lazy.MODEL_FEATURES.TAB_GROUP_NAMING, {
          module: "user-data",
          model,
        }),
      ]);
    conversation.setSystemMessage(systemPrompt);
    conversation.addUserMessage(lazy.renderPrompt(userTemplate, { titles }));
    const response = await conversation.run({ fxAccountToken });
    const raw = response?.finalOutput?.trim() || "";
    let label = lazy.sanitizeUntrustedContent(raw, true).trim();
    if (label.length > MAX_LABEL_LENGTH) {
      const cut = label.slice(0, MAX_LABEL_LENGTH);
      const lastSpace = cut.lastIndexOf(" ");
      label = lastSpace > 0 ? cut.slice(0, lastSpace) : cut;
    }
    // Drop a dangling trailing connective left by truncation ("Features and").
    return label.replace(/\s+(and|or|&)$/i, "").trim();
  },

  /**
   * Keep clusters big enough and cohesive enough to be worth grouping, largest
   * first, capped at maxGroups. Pure so it can be unit tested without the ML
   * model.
   *
   * @param {Array<{tabs: object[], cohesion: number}>} [clusterRepresentations]
   * @returns {Array<{tabs: object[], cohesion: number}>}
   */
  selectClusters(clusterRepresentations) {
    if (!clusterRepresentations?.length) {
      return [];
    }
    return clusterRepresentations
      .filter(
        c =>
          c.tabs &&
          c.tabs.length >= lazy.minTabsPerGroup &&
          c.cohesion >= lazy.minCohesion
      )
      .sort((a, b) => b.tabs.length - a.tabs.length)
      .slice(0, lazy.maxGroups);
  },

  /**
   * Turn a proposal into the display data the panel shows. The caller assigns a
   * stable id so it can track which suggestions have been consumed.
   *
   * @param {{label: string, tabs: MozTabbrowserTab[]}} proposal
   * @param {number} index - Position used to pick a distinct group color.
   * @returns {{label: string, color: string, tabs: MozTabbrowserTab[],
   *   tabInfos: object[]}}
   */
  toSuggestionData(proposal, index) {
    return {
      label: proposal.label,
      color: TAB_GROUP_COLORS[index % TAB_GROUP_COLORS.length],
      tabs: proposal.tabs,
      tabInfos: proposal.tabs.map(tab => this._tabInfo(tab)),
    };
  },

  _tabInfo(tab) {
    // Derive a user-visible site label from the tab's URI.
    const uri = tab.linkedBrowser?.currentURI;
    let site = "";
    if (uri) {
      try {
        site = lazy.BrowserUtils.formatURIForDisplay(uri, {
          onlyBaseDomain: true,
        });
      } catch (e) {
        site = "";
      }
    }
    return { iconUrl: this._faviconUrl(tab), title: tab.label || site };
  },

  _faviconUrl(tab) {
    const uri = tab.linkedBrowser?.currentURI;
    const icon = tab.linkedBrowser?.mIconURL;
    if (uri && (!icon || icon.startsWith("http"))) {
      return `page-icon:${uri.spec}`;
    }
    return icon || DEFAULT_FAVICON_URL;
  },
};
