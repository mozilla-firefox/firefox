/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * WIDGET_REGISTRY — single source of truth for all New Tab widgets.
 *
 * WHY THIS EXISTS
 * Previously, every widget was hardcoded in three places: the render loop in
 * Widgets.jsx, the hideAllWidgets handler, and the toggleMaximize handler.
 * Adding or removing a widget required edits in all three spots and was easy
 * to get out of sync. This registry replaces those hardcoded lists so that
 * Widgets.jsx, WidgetsSidebar.jsx, and any future consumers share one
 * authoritative definition.
 *
 * HOW IT WORKS
 * Each entry describes one widget's static metadata:
 *
 *   id                — unique string key used in prefs and the order pref
 *   telemetryName     — the name sent in Glean events (snake_case; may differ from id).
 *                       An entry with an extensionID sends the widget id from its extension.
 *   customizeL10nId   — Fluent id of the toggle label in the New Tab customize panel
 *   customizeEventSource — source sent with the PREF_CHANGED event when the customize panel toggle flips
 *   order             — default render position (0-indexed); used when widgets.order is empty
 *   enabledPref       — the user-facing pref that toggles this widget on/off
 *   sizePref          — the pref that stores the user's chosen size (empty string = not set)
 *   defaultSize       — size to use when sizePref is empty and no trainhop suggestion exists
 *   validSizes        — the sizes this widget supports (drives size picker options)
 *   hasSidebar        — when true, the widget renders in the sidebar instead of the
 *                       widget row when its effective size equals "small". Size alone is not
 *                       sufficient — this flag must be set explicitly so that future
 *                       widgets that support "small" but stay in the row are not
 *                       accidentally moved to the sidebar. The sidebar holds one
 *                       widget: see resolveSidebarWidget.
 *   systemEnabledPref — system/operator pref that gates this widget independent of the user pref
 *   trainhopEnabledKey — key in trainhopConfig.widgets.* for the enabled override
 *   trainhopSizeKey    — key in trainhopConfig.widgets.* for the size default suggestion
 *                        (only applies when the user has not explicitly set sizePref)
 *   trainhopSidebarKey — key in trainhopConfig.widgets.* for the hasSidebar override;
 *                        null means the sidebar placement is not overridable via trainhop
 *   requiresHistory    — when true, the widget is hidden entirely on profiles that
 *                        record no history (see isWidgetDataUnavailable)
 *
 * SIZE PRIORITY
 * sizePref defaults to "" (empty string) in PREFS_CONFIG. An empty value
 * means the user has not explicitly chosen a size; resolveWidgetSize() falls
 * through to a trainhop suggestion and then to widget.defaultSize. Once the
 * user resizes a widget via the UI the pref is written with a real value and
 * trainhop can no longer override it. resolveWidgetSize() applies these in order:
 *   1. User-set pref (sizePref is non-empty) — always wins
 *   2. trainhopConfig suggestion (trainhopSizeKey) — acts as default, not override
 *   3. widget.defaultSize — final fallback
 *
 * Note: widgets.weather.size uses getValue: getWeatherWidgetSize in
 * ActivityStream.sys.mjs rather than value: "" because it has a Nova migration
 * path that infers the correct initial size from the user's previous weather
 * configuration. After migration the stored value is non-empty and the sentinel
 * logic above applies normally.
 *
 * ADDING A NEW WIDGET
 * 1. Add a new entry to WIDGET_REGISTRY below with the next `order` integer.
 *    Set telemetryName to the snake_case Glean name for this widget.
 * 2. Export its pref key constants from this file.
 * 3. Register both prefs (enabled + size) in lib/ActivityStream.sys.mjs.
 * 4. Add the component to WIDGET_ROW_COMPONENTS in WidgetsComponentRegistry.jsx.
 * 5. If it has a sidebar variant, set hasSidebar: true and add its component
 *    to WIDGET_SIDEBAR_COMPONENTS in WidgetsComponentRegistry.jsx.
 * 6. Add its Customize panel label to
 *    browser/locales/en-US/browser/newtab/newtab.ftl as an attribute-only
 *    message (.label = ...), alongside the other newtab-custom-widget-*-toggle
 *    strings, then set customizeL10nId.
 *
 *    Set customizeEventSource to WIDGET_ plus telemetryName in upper case
 *    (WIDGET_PICTURE_OF_THE_DAY). It ships as the PREF_CHANGED telemetry
 *    source, so pick it once. Weather (WEATHER) and the timer (WIDGET_TIMER)
 *    predate this rule.
 *
 *    The customize panel toggle is generated from the entry, so
 *    WidgetsManagementPanel.jsx needs no edit.
 *
 * RETIRING A WIDGET
 * Set retired: true on its entry. Turn its feed off separately in
 * lib/ActivityStream.sys.mjs — feeds read their own prefs, not the registry.
 * Keep the entry until the code goes: code that looks a widget up in
 * WIDGET_REGISTRY throws if the entry is missing.
 *
 * ADDING A NEW PER-WIDGET DIMENSION (e.g. "scale")
 * 1. Add scalePref and trainhopScaleKey fields to each registry entry.
 * 2. Export a resolveWidgetScale(widget, prefs) helper following the same
 *    user-pref-wins pattern as resolveWidgetSize().
 * 3. Update components to call the helper instead of reading the pref directly.
 *
 * DEVTOOLS ADMIN INTEGRATION
 * The New Tab admin devtools panel (DiscoveryStreamAdmin.jsx, shown when
 * browser.newtabpage.activity-stream.asrouter.devtoolsEnabled is true) drives a
 * "Widgets" section directly off this registry: it maps WIDGET_REGISTRY to render
 * one system-enable toggle per widget (from systemEnabledPref), plus "Enable all"
 * / "Disable all" and reset controls. Any widget added here appears there
 * automatically -- no devtools edit needed.
 *
 * To expose an extra pref-gated widget feature in that panel (e.g. an internal
 * feature that defaults off but QA/devs want to flip, such as
 * widgets.pictureOfTheDay.setAsWallpaper.enabled or
 * widgets.privacy.showVpnMessages), add an entry to the hand-maintained
 * WIDGET_EXTRA_FEATURES map in DiscoveryStreamAdmin.jsx keyed by widget id:
 *   privacy: [{ pref: "widgets.privacy.showVpnMessages", label: "VPN messages" }]
 * Each entry becomes a boolean toggle nested under that widget's row. This map is
 * intentionally kept in the devtools component, not the registry, so shipping code
 * carries no dependency on dev-only feature lists.
 *
 * The widgets.order pref (CSV of widget IDs) persists user-defined order.
 * It is only written when the user explicitly reorders widgets — never on
 * enable/disable. Disabled widgets keep their slot so they reappear in the
 * same position when re-enabled. See resolveWidgetOrder() below.
 */

// @experiment(remove-conditional) { bug 2078816 }
// Keep for spaces-thematic-v1.
import { selectFirstSlotWidgetIds } from "./PageLayoutVariants.mjs";

export const PREF_WIDGETS_LISTS_ENABLED = "widgets.lists.enabled";
export const PREF_WIDGETS_TIMER_ENABLED = "widgets.focusTimer.enabled";
export const PREF_WIDGETS_WEATHER_ENABLED = "widgets.weather.enabled";
export const PREF_LISTS_SIZE = "widgets.lists.size";
export const PREF_FOCUS_TIMER_SIZE = "widgets.focusTimer.size";
export const PREF_WEATHER_SIZE = "widgets.weather.size";
export const PREF_WIDGETS_ORDER = "widgets.order";
// The widgets pinned to Home where a thematic spaces config names a
// widgetLibrary, as a JSON array of ids.
export const PREF_HOME_WIDGETS = "widgets.home";
export const PREF_WIDGETS_SYSTEM_LISTS_ENABLED = "widgets.system.lists.enabled";
export const PREF_WIDGETS_SYSTEM_TIMER_ENABLED =
  "widgets.system.focusTimer.enabled";
export const PREF_WIDGETS_SYSTEM_WEATHER_ENABLED =
  "widgets.system.weather.enabled";
export const PREF_WIDGETS_CLOCKS_ENABLED = "widgets.clocks.enabled";
export const PREF_CLOCKS_SIZE = "widgets.clocks.size";
export const PREF_WIDGETS_SYSTEM_CLOCKS_ENABLED =
  "widgets.system.clocks.enabled";
export const PREF_WIDGETS_PRIVACY_ENABLED = "widgets.privacy.enabled";
export const PREF_PRIVACY_SIZE = "widgets.privacy.size";
export const PREF_WIDGETS_SYSTEM_PRIVACY_ENABLED =
  "widgets.system.privacy.enabled";
export const PREF_PRIVACY_MAX_COUNT = "widgets.privacy.maxCount";
export const PREF_PRIVACY_MAX_DISPLAY_COUNT = "widgets.privacy.maxDisplayCount";
export const PREF_PRIVACY_BLANK_CHANCE = "widgets.privacy.blankChance";
export const PREF_PRIVACY_SHOW_VPN_MESSAGES = "widgets.privacy.showVpnMessages";
export const PREF_PRIVACY_FORCE_MESSAGE_ID = "widgets.privacy.forceMessageId";
export const PREF_PRIVACY_MESSAGE_STATE = "widgets.privacy.messageState";
export const PREF_PRIVACY_CELEBRATION_THRESHOLD =
  "widgets.privacy.celebrationThreshold";
export const PREF_PRIVACY_CELEBRATION_STATE =
  "widgets.privacy.celebrationState";
export const PREF_PRIVACY_FORCE_CELEBRATION =
  "widgets.privacy.forceCelebration";
export const PREF_WIDGETS_CROSSWORD_ENABLED = "widgets.crossword.enabled";
export const PREF_CROSSWORD_SIZE = "widgets.crossword.size";
export const PREF_WIDGETS_SYSTEM_CROSSWORD_ENABLED =
  "widgets.system.crossword.enabled";
export const PREF_WIDGETS_STOCKS_ENABLED = "widgets.stocks.enabled";
export const PREF_STOCKS_SIZE = "widgets.stocks.size";
export const PREF_STOCKS_WATCHLIST = "widgets.stocks.watchlist";
export const PREF_WIDGETS_SYSTEM_STOCKS_ENABLED =
  "widgets.system.stocks.enabled";
export const PREF_CROSSWORD_ENDPOINT = "widgets.crossword.endpoint";
export const PREF_WIDGETS_PICTURE_OF_THE_DAY_ENABLED =
  "widgets.pictureOfTheDay.enabled";
export const PREF_PICTURE_OF_THE_DAY_SIZE = "widgets.pictureOfTheDay.size";
export const PREF_WIDGETS_SYSTEM_PICTURE_OF_THE_DAY_ENABLED =
  "widgets.system.pictureOfTheDay.enabled";
export const PREF_WIDGETS_RECENT_SEARCHES_ENABLED =
  "widgets.recentSearches.enabled";
export const PREF_RECENT_SEARCHES_SIZE = "widgets.recentSearches.size";
export const PREF_WIDGETS_SYSTEM_RECENT_SEARCHES_ENABLED =
  "widgets.system.recentSearches.enabled";
export const PREF_WIDGETS_HOROSCOPES_ENABLED = "widgets.horoscopes.enabled";
export const PREF_HOROSCOPES_SIZE = "widgets.horoscopes.size";
export const PREF_WIDGETS_SYSTEM_HOROSCOPES_ENABLED =
  "widgets.system.horoscopes.enabled";
export const PREF_WIDGETS_TRAVEL_ENABLED = "widgets.travel.enabled";
export const PREF_TRAVEL_SIZE = "widgets.travel.size";
export const PREF_WIDGETS_SYSTEM_TRAVEL_ENABLED =
  "widgets.system.travel.enabled";
export const PREF_WIDGETS_SOLITAIRE_ENABLED = "widgets.solitaire.enabled";
export const PREF_SOLITAIRE_SIZE = "widgets.solitaire.size";
export const PREF_WIDGETS_SYSTEM_SOLITAIRE_ENABLED =
  "widgets.system.solitaire.enabled";

/**
 * @typedef {object} WidgetRegistryEntry
 * @property {string} id - Unique key used in prefs and the order pref.
 * @property {string} [telemetryName] - Snake_case name sent in Glean events. May differ from id (e.g. "focus_timer" for id "focusTimer"). Required unless the entry has an extensionID, which sends the widget id from its extension.
 * @property {string} [customizeL10nId] - Fluent id of the widget's customize panel toggle label. The message carries a .label attribute. Required unless the entry is retired or has an extensionID.
 * @property {string} [customizeEventSource] - Source sent with the PREF_CHANGED user event when the widget's customize panel toggle flips. Required unless the entry is retired.
 * @property {number} order - Default render position (0-indexed).
 * @property {string} enabledPref - User-facing pref that toggles this widget on/off.
 * @property {string} sizePref - Pref that stores the user's chosen size ("" = not yet set).
 * @property {string} defaultSize - Fallback size when sizePref is empty and no trainhop suggestion exists.
 * @property {string[]} [validSizes] - Sizes this widget supports. Required unless the entry has an extensionID.
 * @property {boolean} hasSidebar - When true, the widget moves to the sidebar at size "small".
 * @property {string} systemEnabledPref - Operator pref that gates the widget independently of the user pref.
 * @property {string} trainhopEnabledKey - Key in trainhopConfig.widgets.* for the enabled override. For Weather it is also the id of its about:preferences system-pref setting.
 * @property {string|null} trainhopSizeKey - Key in trainhopConfig.widgets.* for the size default suggestion.
 * @property {string|null} trainhopSidebarKey - Key in trainhopConfig.widgets.* for the hasSidebar override.
 * @property {string} widgetsSettingsVisibleKey - Key in trainhopConfig.widgetsSettings.* that additively reveals this widget's toggle in the Customize menu, and Weather's standalone row in about:preferences#home (does not enable the widget).
 * @property {string} widgetsSettingsEnabledKey - Key in trainhopConfig.widgetsSettings.* that overrides this widget's default enabled value (written to the pref default branch; an explicit user toggle still wins).
 * @property {boolean} [requiresHistory] - When true, the widget is hidden entirely on profiles that record no history. See isWidgetDataUnavailable.
 * @property {boolean} [retired] - When true the widget never renders and gets no settings or devtools toggle, whatever its prefs and trainhopConfig say.
 * @property {boolean} [skipNightlyDefault] - When true, the widget opts out of being on everywhere in Nightly. See skipsNightlyDefault in lib/ActivityStream.sys.mjs.
 * @property {string|null} [trainhopNamespace] - When set, the widget ships its whole config in one dedicated object at trainhopConfig.<namespace>. Its `enabled` overrides the default value of enabledPref on the default branch (user toggle still wins, like widgetsSettings.*Enabled); `visible` reveals the widget (isWidgetAddable) without writing a pref; `size` is read by resolveWidgetSize. All widgets define this today.
 * @property {string} [src] - URL of the widget's document; a trainhop namespace `src` overrides it.
 * @property {string} [extensionID] - ID of an extension that provides the widget's document. getWidgets keeps the entry only while the ExtensionWidgets state has this extension.
 * @property {string} [icon] - URL of a 16px image drawn before the widget's name in its MozWidgetCard header.
 * @property {string} [l10nId] - Fluent id of the widget's name. The message carries .title, .label and .menu-label attributes.
 */

// If you add a widget market pref to firefox.js that older hosts don't have,
// also add it to MARKET_PREF_FALLBACKS in ActivityStream.sys.mjs as a
// @backward-compat stub, or train-hops to those hosts will ignore it.
/** @type {WidgetRegistryEntry[]} */
export const WIDGET_REGISTRY = [
  {
    id: "pictureOfTheDay",
    telemetryName: "picture_of_the_day",
    customizeL10nId: "newtab-custom-widget-picture-toggle",
    customizeEventSource: "WIDGET_PICTURE_OF_THE_DAY",
    order: 0,
    enabledPref: PREF_WIDGETS_PICTURE_OF_THE_DAY_ENABLED,
    sizePref: PREF_PICTURE_OF_THE_DAY_SIZE,
    defaultSize: "medium",
    validSizes: ["medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_PICTURE_OF_THE_DAY_ENABLED,
    trainhopEnabledKey: "pictureOfTheDayEnabled",
    trainhopSizeKey: "pictureOfTheDaySize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "pictureOfTheDayVisible",
    widgetsSettingsEnabledKey: "pictureOfTheDayEnabled",
    trainhopNamespace: "widgetPictureOfTheDay",
  },
  {
    id: "clocks",
    telemetryName: "clocks",
    customizeL10nId: "newtab-custom-widget-clock-toggle",
    customizeEventSource: "WIDGET_CLOCKS",
    order: 1,
    enabledPref: PREF_WIDGETS_CLOCKS_ENABLED,
    sizePref: PREF_CLOCKS_SIZE,
    defaultSize: "medium",
    validSizes: ["small", "medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_CLOCKS_ENABLED,
    trainhopEnabledKey: "clocksEnabled",
    trainhopSizeKey: "clocksSize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "clocksVisible",
    widgetsSettingsEnabledKey: "clocksEnabled",
    trainhopNamespace: "widgetClocks",
  },
  {
    id: "lists",
    telemetryName: "lists",
    customizeL10nId: "newtab-custom-widget-lists-toggle",
    customizeEventSource: "WIDGET_LISTS",
    order: 2,
    enabledPref: PREF_WIDGETS_LISTS_ENABLED,
    sizePref: PREF_LISTS_SIZE,
    defaultSize: "medium",
    validSizes: ["small", "medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_LISTS_ENABLED,
    trainhopEnabledKey: "listsEnabled",
    trainhopSizeKey: "listsSize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "listsVisible",
    widgetsSettingsEnabledKey: "listsEnabled",
    trainhopNamespace: "widgetLists",
  },
  {
    id: "focusTimer",
    telemetryName: "focus_timer",
    customizeL10nId: "newtab-custom-widget-timer-toggle",
    customizeEventSource: "WIDGET_TIMER",
    order: 3,
    enabledPref: PREF_WIDGETS_TIMER_ENABLED,
    sizePref: PREF_FOCUS_TIMER_SIZE,
    defaultSize: "medium",
    validSizes: ["small", "medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_TIMER_ENABLED,
    trainhopEnabledKey: "timerEnabled",
    trainhopSizeKey: "timerSize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "focusTimerVisible",
    widgetsSettingsEnabledKey: "focusTimerEnabled",
    trainhopNamespace: "widgetFocusTimer",
  },
  {
    id: "weather",
    telemetryName: "weather",
    customizeL10nId: "newtab-custom-widget-weather-toggle",
    customizeEventSource: "WEATHER",
    order: 4,
    enabledPref: PREF_WIDGETS_WEATHER_ENABLED,
    sizePref: PREF_WEATHER_SIZE,
    defaultSize: "small",
    validSizes: ["small", "medium", "large"],
    hasSidebar: true,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_WEATHER_ENABLED,
    trainhopEnabledKey: "weatherEnabled",
    trainhopSizeKey: "weatherSize",
    trainhopSidebarKey: "weatherSidebar",
    widgetsSettingsVisibleKey: "weatherVisible",
    widgetsSettingsEnabledKey: "weatherEnabled",
    trainhopNamespace: "widgetWeather",
  },
  {
    id: "privacy",
    telemetryName: "privacy",
    customizeL10nId: "newtab-custom-widget-privacy-toggle",
    customizeEventSource: "WIDGET_PRIVACY",
    order: 5,
    enabledPref: PREF_WIDGETS_PRIVACY_ENABLED,
    sizePref: PREF_PRIVACY_SIZE,
    defaultSize: "medium",
    // Small is reached only through the trainhop sidebar config; the widget's
    // size menu only displays medium and large.
    validSizes: ["small", "medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_PRIVACY_ENABLED,
    trainhopEnabledKey: "privacyEnabled",
    trainhopSizeKey: "privacySize",
    trainhopSidebarKey: "privacySidebar",
    widgetsSettingsVisibleKey: "privacyVisible",
    widgetsSettingsEnabledKey: "privacyEnabled",
    trainhopNamespace: "widgetPrivacy",
    requiresHistory: true,
  },
  {
    id: "crossword",
    telemetryName: "crossword",
    customizeL10nId: "newtab-custom-widget-crossword-toggle",
    customizeEventSource: "WIDGET_CROSSWORD",
    order: 6,
    enabledPref: PREF_WIDGETS_CROSSWORD_ENABLED,
    sizePref: PREF_CROSSWORD_SIZE,
    defaultSize: "medium",
    validSizes: ["medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_CROSSWORD_ENABLED,
    trainhopEnabledKey: "crosswordEnabled",
    trainhopSizeKey: "crosswordSize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "crosswordVisible",
    widgetsSettingsEnabledKey: "crosswordEnabled",
    trainhopNamespace: "widgetCrossword",
  },
  {
    id: "stocks",
    telemetryName: "stocks",
    customizeL10nId: "newtab-custom-widget-stocks-toggle2",
    customizeEventSource: "WIDGET_STOCKS",
    order: 7,
    enabledPref: PREF_WIDGETS_STOCKS_ENABLED,
    sizePref: PREF_STOCKS_SIZE,
    defaultSize: "medium",
    validSizes: ["small", "medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_STOCKS_ENABLED,
    trainhopEnabledKey: "stocksEnabled",
    trainhopSizeKey: "stocksSize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "stocksVisible",
    widgetsSettingsEnabledKey: "stocksEnabled",
    trainhopNamespace: "widgetStocks",
  },
  {
    id: "recentSearches",
    telemetryName: "recent_searches",
    customizeL10nId: "newtab-custom-widget-search-toggle",
    customizeEventSource: "WIDGET_RECENT_SEARCHES",
    order: 8,
    enabledPref: PREF_WIDGETS_RECENT_SEARCHES_ENABLED,
    sizePref: PREF_RECENT_SEARCHES_SIZE,
    defaultSize: "medium",
    validSizes: ["medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_RECENT_SEARCHES_ENABLED,
    trainhopEnabledKey: "recentSearchesEnabled",
    trainhopSizeKey: "recentSearchesSize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "recentSearchesVisible",
    widgetsSettingsEnabledKey: "recentSearchesEnabled",
    trainhopNamespace: "widgetRecentSearches",
  },
  {
    id: "horoscopes",
    telemetryName: "horoscopes",
    customizeL10nId: "newtab-custom-widget-horoscopes-toggle",
    customizeEventSource: "WIDGET_HOROSCOPES",
    order: 9,
    enabledPref: PREF_WIDGETS_HOROSCOPES_ENABLED,
    sizePref: PREF_HOROSCOPES_SIZE,
    defaultSize: "medium",
    validSizes: ["medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_HOROSCOPES_ENABLED,
    trainhopEnabledKey: "horoscopesEnabled",
    trainhopSizeKey: "horoscopesSize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "horoscopesVisible",
    widgetsSettingsEnabledKey: "horoscopesEnabled",
    trainhopNamespace: "widgetHoroscopes",
  },
  {
    id: "travel",
    telemetryName: "travel",
    customizeL10nId: "newtab-custom-widget-travel-toggle",
    customizeEventSource: "WIDGET_TRAVEL",
    order: 10,
    enabledPref: PREF_WIDGETS_TRAVEL_ENABLED,
    sizePref: PREF_TRAVEL_SIZE,
    defaultSize: "medium",
    validSizes: ["medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_TRAVEL_ENABLED,
    trainhopEnabledKey: "travelEnabled",
    trainhopSizeKey: "travelSize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "travelVisible",
    widgetsSettingsEnabledKey: "travelEnabled",
    trainhopNamespace: "widgetTravel",
  },
  {
    id: "solitaire",
    telemetryName: "solitaire",
    customizeL10nId: "newtab-custom-widget-solitaire-toggle",
    customizeEventSource: "WIDGET_SOLITAIRE",
    order: 11,
    enabledPref: PREF_WIDGETS_SOLITAIRE_ENABLED,
    sizePref: PREF_SOLITAIRE_SIZE,
    defaultSize: "medium",
    validSizes: ["medium", "large"],
    hasSidebar: false,
    systemEnabledPref: PREF_WIDGETS_SYSTEM_SOLITAIRE_ENABLED,
    trainhopEnabledKey: "solitaireEnabled",
    trainhopSizeKey: "solitaireSize",
    trainhopSidebarKey: null,
    widgetsSettingsVisibleKey: "solitaireVisible",
    widgetsSettingsEnabledKey: "solitaireEnabled",
    trainhopNamespace: "widgetSolitaire",
  },
];

/**
 * Returns an ordered list of all widget IDs (including disabled ones).
 * Saved order is respected; any widget IDs not in the saved pref are appended
 * in registry-default order. Unknown IDs in the saved pref are dropped.
 *
 * @param {string} orderPref - value of the widgets.order pref (CSV string)
 */
export function getWidgetOrder(orderPref) {
  const registryIds = WIDGET_REGISTRY.map(w => w.id);
  if (!orderPref) {
    return registryIds;
  }
  const seen = new Set();
  const saved = orderPref
    .split(",")
    .filter(id => registryIds.includes(id) && !seen.has(id) && seen.add(id));
  const appended = registryIds.filter(id => !seen.has(id));
  return [...saved, ...appended];
}

/**
 * Returns the effective widget render order. The user's saved order wins;
 * a trainhop suggestion applies only when no user order is saved.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {string[]} ordered array of widget IDs
 */
export function resolveWidgetOrder(prefs) {
  const userOrder = prefs[PREF_WIDGETS_ORDER];
  if (userOrder) {
    return getWidgetOrder(userOrder);
  }
  const trainhopOrder = prefs.trainhopConfig?.widgets?.order;
  if (trainhopOrder) {
    return getWidgetOrder(trainhopOrder);
  }
  return getWidgetOrder(null);
}

/**
 * The widget ids the user has pinned to Home, or null until they first pin or
 * unpin one, when the spaces config's default applies. An empty array is a
 * choice, so it is not null.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {string[]|null}
 */
export function parsePinnedWidgetIds(prefs) {
  try {
    const ids = JSON.parse(prefs?.[PREF_HOME_WIDGETS]);
    if (Array.isArray(ids)) {
      return ids.filter(id => typeof id === "string");
    }
  } catch {}
  return null;
}

/**
 * Returns true once the profile's region is known and false
 * for a region-less profile.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {boolean}
 */
export function isRegionResolved(prefs) {
  return Boolean(prefs.region);
}

/**
 * Returns true if the widget needs history and this profile records none, so it
 * could only ever render an empty state (Bug 2063207). An absent
 * `recordsHistory` counts as available, so a missing PrefsFeed broadcast cannot
 * hide a working widget.
 *
 * @param {object} widget - a WIDGET_REGISTRY entry
 * @param {object} prefs - current pref values from the Redux store
 * @returns {boolean}
 */
export function isWidgetDataUnavailable(widget, prefs) {
  return Boolean(widget.requiresHistory && prefs.recordsHistory === false);
}

/**
 * Returns the built-in registry entries and the extension entries present in
 * extensionWidgets. Each extension entry is merged with its ExtensionWidget.
 *
 * @param {WidgetRegistryEntry[]} registry - WIDGET_REGISTRY
 * @param {object} [extensionWidgets] - the ExtensionWidgets state from the
 *   Redux store, keyed by extension id
 * @returns {Array<WidgetRegistryEntry & {name?: string}>}
 */
export function getWidgets(registry, extensionWidgets) {
  return registry
    .filter(w => !w.extensionID || extensionWidgets?.[w.extensionID])
    .map(w =>
      w.extensionID ? { ...extensionWidgets[w.extensionID], ...w } : w
    );
}

/**
 * Returns the document URL a widget's card should load, in precedence order:
 * the trainhop namespace src, the entry's static src.
 *
 * @param {object} widget - a WIDGET_REGISTRY entry
 * @param {object} prefs - current pref values from the Redux store
 * @returns {string} document URL, "" when the widget has no document
 */
export function resolveWidgetSrc(widget, prefs) {
  return (
    (widget?.trainhopNamespace &&
      prefs.trainhopConfig?.[widget.trainhopNamespace]?.src) ||
    widget?.src ||
    ""
  );
}

/**
 * Returns true if the widget is available to the user, based on the
 * system pref, the trainhopConfig.widgets addable key, or a
 * widgetsSettings.*Visible override (revealing a toggle also makes the widget
 * addable so the toggle is functional). Does not consider whether the user has
 * turned the widget on, or whether the widgets container is enabled.
 *
 * A retired widget is never addable, and neither is one whose data source the
 * profile has turned off (see isWidgetDataUnavailable).
 *
 * @param {object} widget - a WIDGET_REGISTRY entry
 * @param {object} prefs - current pref values from the Redux store
 * @returns {boolean}
 */
export function isWidgetAddable(widget, prefs) {
  if (widget.retired || isWidgetDataUnavailable(widget, prefs)) {
    return false;
  }
  if (prefs[widget.systemEnabledPref]) {
    return true;
  }
  if (!isRegionResolved(prefs)) {
    return false;
  }
  return Boolean(
    (widget.trainhopNamespace &&
      prefs.trainhopConfig?.[widget.trainhopNamespace]?.visible) ||
    prefs.trainhopConfig?.widgets?.[widget.trainhopEnabledKey] ||
    prefs.trainhopConfig?.widgetsSettings?.[widget.widgetsSettingsVisibleKey]
  );
}

/**
 * Returns true if this widget's toggle should be shown in the Customize menu.
 * For Weather it also decides whether its standalone row in
 * about:preferences#home is shown. A widget is shown when it is addable
 * (system pref, trainhopConfig.widgets, or widgetsSettings.*Visible) or when
 * the legacy `widgetsConfig` Nimbus variable enables it. Showing a toggle does NOT enable the widget — enablement is the
 * widget's own enabled pref, whose default can be overridden via
 * widgetsSettings.*Enabled.
 *
 * A retired widget gets no toggle, and neither does one with no data source;
 * both checked here too, to beat widgetsConfig.
 *
 * @param {object} widget - a WIDGET_REGISTRY entry
 * @param {object} prefs - current pref values from the Redux store
 * @returns {boolean}
 */
export function isWidgetToggleVisible(widget, prefs) {
  if (widget.retired || isWidgetDataUnavailable(widget, prefs)) {
    return false;
  }
  return Boolean(
    isWidgetAddable(widget, prefs) ||
    prefs.widgetsConfig?.[widget.trainhopEnabledKey]
  );
}

/**
 * Returns true if Weather may be shown at all: when system.showWeather,
 * trainhopConfig.widgetWeather.visible or trainhopConfig.weather.enabled is
 * set, or when widgetsSettings.weatherVisible
 * overrides both. Base.jsx uses it for the customize menu's Weather row and
 * the page-level Weather styling; the widgets panel uses it for its Weather
 * toggle.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {boolean}
 */
export function isWeatherAvailable(prefs) {
  return Boolean(
    prefs["system.showWeather"] ||
    prefs.trainhopConfig?.widgetWeather?.visible ||
    prefs.trainhopConfig?.weather?.enabled ||
    prefs.trainhopConfig?.widgetsSettings?.weatherVisible
  );
}

/**
 * Returns true if the Widgets container/section toggle should be shown.
 * Additive across the system pref, the legacy `widgetsConfig` variable, the
 * `trainhopConfig.widgets.enabled` addable key, and the new
 * `trainhopConfig.widgetsSettings.enabled` override.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {boolean}
 */
export function isWidgetsContainerVisible(prefs) {
  if (prefs["widgets.system.enabled"] || prefs.widgetsConfig?.enabled) {
    return true;
  }
  if (!isRegionResolved(prefs)) {
    return false;
  }
  return Boolean(
    prefs.trainhopConfig?.widgets?.enabled ||
    prefs.trainhopConfig?.widgetsSettings?.enabled
  );
}

/**
 * Returns true if the widget is currently enabled: the widgets container is
 * on, the widget is addable, and the user's enabled pref is set.
 *
 * @param {object} widget - a WIDGET_REGISTRY entry
 * @param {object} prefs - current pref values from the Redux store
 * @param {boolean} widgetsEnabled - value of the widgets.enabled container pref
 * @returns {boolean}
 */
export function isWidgetEnabled(widget, prefs, widgetsEnabled) {
  return Boolean(
    widgetsEnabled &&
    isWidgetAddable(widget, prefs) &&
    prefs[widget.enabledPref]
  );
}

/**
 * Returns the effective size for a widget, applying priority:
 *   user-set pref > trainhop suggestion > registry defaultSize
 *
 * A sizePref value of "" means the user has not explicitly chosen a size,
 * so trainhop and defaultSize are consulted. Any non-empty value was written
 * by a user action (size picker, maximize/minimize button) and always wins.
 *
 * @param {object} widget - a WIDGET_REGISTRY entry
 * @param {object} prefs - current pref values from the Redux store
 * @returns {string}
 */
export function resolveWidgetSize(widget, prefs) {
  // @experiment(remove-conditional) { bug 2078816 }
  // Keep for spaces-thematic-v1.
  // A large widget fits a medium card.
  if (selectFirstSlotWidgetIds(prefs).has(widget.id)) {
    return "large";
  }
  const userPref = prefs[widget.sizePref];
  if (userPref) {
    return userPref;
  }
  const dedicatedSize = widget.trainhopNamespace
    ? prefs.trainhopConfig?.[widget.trainhopNamespace]?.size
    : null;
  const trainhopSize = widget.trainhopSizeKey
    ? prefs.trainhopConfig?.widgets?.[widget.trainhopSizeKey]
    : null;
  return dedicatedSize || trainhopSize || widget.defaultSize;
}

/**
 * Returns whether the widget should be placed in the sidebar.
 * A trainhop override (trainhopSidebarKey) takes precedence over the
 * static registry hasSidebar flag when present.
 *
 * @param {object} widget - a WIDGET_REGISTRY entry
 * @param {object} prefs - current pref values from the Redux store
 * @returns {boolean}
 */
export function resolveWidgetHasSidebar(widget, prefs) {
  if (widget.trainhopSidebarKey) {
    const override = prefs.trainhopConfig?.widgets?.[widget.trainhopSidebarKey];
    if (override !== undefined) {
      return override;
    }
  }
  return widget.hasSidebar;
}

/**
 * Returns the one widget that renders in the inline-end sidebar, or null.
 *
 * The sidebar holds a single widget. When more than one qualifies to show, the
 * first in registry order wins and the rest stay in the widget row. The row and
 * the layout helpers read this too, so a widget never renders in both places.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @param {boolean} [widgetsEnabled] - the master toggle, passed explicitly when
 *   the spaces experiment is overriding it
 * @returns {object|null} a WIDGET_REGISTRY entry
 */
export function resolveSidebarWidget(
  prefs,
  widgetsEnabled = prefs["widgets.enabled"]
) {
  return (
    WIDGET_REGISTRY.find(
      w =>
        resolveWidgetHasSidebar(w, prefs) &&
        isWidgetEnabled(w, prefs, widgetsEnabled) &&
        resolveWidgetSize(w, prefs) === "small"
    ) ?? null
  );
}

/**
 * Returns true if the widget renders in the inline-end sidebar rather than the
 * content area.
 *
 * @param {object} widget - a WIDGET_REGISTRY entry
 * @param {object} prefs - current pref values from the Redux store
 * @param {boolean} [widgetsEnabled] - the master toggle, passed explicitly when
 *   the spaces experiment is overriding it
 * @returns {boolean}
 */
export function isWidgetInSidebar(
  widget,
  prefs,
  widgetsEnabled = prefs["widgets.enabled"]
) {
  return resolveSidebarWidget(prefs, widgetsEnabled) === widget;
}

/**
 * Returns true if at least one enabled widget renders in the content area rather
 * than the inline-end sidebar.
 *
 * Layout code needs this rather than isWidgetsContainerVisible, which is true
 * even when the user has hidden every widget.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @param {boolean} [widgetsEnabled] - the master toggle, passed explicitly when
 *   the spaces experiment is overriding it
 * @returns {boolean}
 */
export function hasContentAreaWidgets(
  prefs,
  widgetsEnabled = prefs["widgets.enabled"]
) {
  return WIDGET_REGISTRY.some(
    w =>
      isWidgetEnabled(w, prefs, widgetsEnabled) &&
      !isWidgetInSidebar(w, prefs, widgetsEnabled)
  );
}

/**
 * Returns the Merino endpoint the Crossword widget iframe should load.
 * The dedicated widgetCrossword trainhop object wins, then the legacy
 * widgets.crosswordEndpoint key, then the raw pref, so the endpoint can be
 * swapped (e.g. staging to production) without a release. The raw pref is never
 * read directly by the component.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {string}
 */
export function resolveCrosswordEndpoint(prefs) {
  return (
    prefs.trainhopConfig?.widgetCrossword?.endpoint ||
    prefs.trainhopConfig?.widgets?.crosswordEndpoint ||
    prefs[PREF_CROSSWORD_ENDPOINT]
  );
}

/**
 * Picks the dedicated trainhopConfig.widgetPrivacy value when it has the
 * expected type, otherwise falls through to the shared trainhopConfig.widgets
 * key. A present-but-wrong-typed dedicated value is a recipe misconfig (e.g.
 * the string "0.4" where a number is required — the string form is only correct
 * for the pref, which cannot hold a float), so warn rather than silently
 * masking the shared key with a value that is about to be discarded.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @param {string} key - key in trainhopConfig.widgetPrivacy
 * @param {string} sharedKey - fallback key in trainhopConfig.widgets
 * @param {string} type - expected typeof result
 * @returns {*} the dedicated value, else the shared value, else undefined
 */
function resolvePrivacyTrainhopValue(prefs, key, sharedKey, type) {
  const dedicated = prefs.trainhopConfig?.widgetPrivacy?.[key];
  if (typeof dedicated === type) {
    return dedicated;
  }
  if (dedicated !== undefined) {
    console.warn(
      `trainhopConfig.widgetPrivacy.${key} is ${JSON.stringify(
        dedicated
      )}; expected a ${type}. Ignoring it.`
    );
  }
  return prefs.trainhopConfig?.widgets?.[sharedKey];
}

/**
 * Resolves the today-count at which the Privacy widget fires its "daily cap"
 * celebration message. This is NOT the display ceiling — the readout keeps
 * showing the real number past this point (see resolvePrivacyDisplayCount).
 * Priority: widgetPrivacy > widgets > pref > 100. Routed through this helper
 * (never the raw pref) per the trainhop-gate convention.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {number}
 */
export function resolvePrivacyMaxCount(prefs) {
  return (
    resolvePrivacyTrainhopValue(
      prefs,
      "maxCount",
      "privacyMaxCount",
      "number"
    ) ||
    prefs[PREF_PRIVACY_MAX_COUNT] ||
    100
  );
}

/**
 * Resolves the ceiling for the tracker-count readout: above it the number
 * shows as "{cap}+" so it stays a tidy few characters. Default 999 (three
 * digits). Distinct from resolvePrivacyMaxCount (the daily-cap celebration
 * threshold). Priority: widgetPrivacy > widgets > pref > 999.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {number}
 */
export function resolvePrivacyDisplayCount(prefs) {
  return (
    resolvePrivacyTrainhopValue(
      prefs,
      "maxDisplayCount",
      "privacyMaxDisplayCount",
      "number"
    ) ||
    prefs[PREF_PRIVACY_MAX_DISPLAY_COUNT] ||
    999
  );
}

/**
 * Resolves the Privacy widget "blank chance" — the probability (0..1) that an
 * eligible info message is suppressed to keep the experience calm. It's compared
 * against Math.random(), so it MUST be a 0–1 fraction (0.4 = 40%), not a percent.
 * A value > 1 (e.g. 40) would blank every message; guard against that by warning
 * and falling back to the default. Priority: widgetPrivacy > widgets > pref > 0.4.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {number}
 */
export function resolvePrivacyBlankChance(prefs) {
  const DEFAULT = 0.4;
  const trainhop = resolvePrivacyTrainhopValue(
    prefs,
    "blankChance",
    "privacyBlankChance",
    "number"
  );
  // The pref is stored as a string ("0.4") because Firefox prefs have no float
  // type — a numeric default would land as 0 and silently disable blanks.
  // trainhopConfig comes from JSON, so it's already a number.
  const rawPref = prefs[PREF_PRIVACY_BLANK_CHANCE];
  const raw = typeof trainhop === "number" ? trainhop : parseFloat(rawPref);
  if (Number.isNaN(raw)) {
    // Warn on a present-but-unparseable value (a misconfig); stay quiet when
    // the pref is simply unset.
    if (rawPref !== undefined && rawPref !== "") {
      console.warn(
        `widgets.privacy.blankChance is ${JSON.stringify(
          rawPref
        )}; expected a 0-1 number. Using ${DEFAULT}.`
      );
    }
    return DEFAULT;
  }
  if (raw < 0 || raw > 1) {
    console.warn(
      `widgets.privacy.blankChance is ${raw}; expected a 0-1 fraction (0.4 = 40%). Using ${DEFAULT}.`
    );
    return DEFAULT;
  }
  return raw;
}

/**
 * Resolves whether the Privacy widget may show VPN promotional messages. Off by
 * default: not all users are eligible for the built-in VPN (unsupported region,
 * enterprise-managed, removed from the toolbar), and promoting an unavailable
 * feature erodes trust. An experiment can enable them for eligible cohorts — or
 * force them off. Priority: widgetPrivacy > widgets > pref > false.
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {boolean}
 */
export function resolvePrivacyShowVpnMessages(prefs) {
  const trainhop = resolvePrivacyTrainhopValue(
    prefs,
    "showVpnMessages",
    "privacyShowVpnMessages",
    "boolean"
  );
  if (typeof trainhop === "boolean") {
    return trainhop;
  }
  return !!prefs[PREF_PRIVACY_SHOW_VPN_MESSAGES];
}

/**
 * Resolves how far the blocked-tracker count must climb before the count-up
 * celebration fires. Priority: widgetPrivacy > widgets > pref > 10 (HNT-2845).
 *
 * @param {object} prefs - current pref values from the Redux store
 * @returns {number}
 */
export function resolvePrivacyCelebrationThreshold(prefs) {
  const DEFAULT = 10;
  const trainhop = resolvePrivacyTrainhopValue(
    prefs,
    "celebrationThreshold",
    "privacyCelebrationThreshold",
    "number"
  );
  const raw =
    typeof trainhop === "number"
      ? trainhop
      : prefs[PREF_PRIVACY_CELEBRATION_THRESHOLD];
  // A zero or negative threshold would fire on every refresh.
  if (typeof raw !== "number" || !Number.isFinite(raw) || raw < 1) {
    return DEFAULT;
  }
  return raw;
}

/**
 * Returns the list of widgets to disable when "hide all" is triggered.
 * A widget is included if it has no sidebar variant OR if it is currently
 * in the row (not the sidebar). Each entry carries the pref to disable,
 * the telemetry name, and whether it was active (for telemetry filtering).
 *
 * @param {WidgetRegistryEntry[]} widgets - the widgets from getWidgets
 * @param {object} prefs - current pref values from the Redux store
 * @param {object} widgetEnabledMap - map of widget id → boolean (currently active in row)
 * @returns {{ enabledPref: string, telemetryName: string, active: boolean }[]}
 */
export function getHideAllTargets(widgets, prefs, widgetEnabledMap) {
  return widgets
    .filter(w => !resolveWidgetHasSidebar(w, prefs) || widgetEnabledMap[w.id])
    .map(w => ({
      enabledPref: w.enabledPref,
      telemetryName: w.telemetryName,
      active: !!widgetEnabledMap[w.id],
    }));
}
