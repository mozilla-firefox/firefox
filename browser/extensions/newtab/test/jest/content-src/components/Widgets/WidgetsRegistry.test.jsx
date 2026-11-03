/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  WIDGET_REGISTRY,
  getWidgetOrder,
  getWidgets,
  isWeatherAvailable,
  isWidgetAddable,
  isWidgetEnabled,
  isWidgetToggleVisible,
  isWidgetsContainerVisible,
  resolveWidgetSize,
  resolveWidgetOrder,
  resolveWidgetHasSidebar,
  resolveSidebarWidget,
  hasContentAreaWidgets,
  resolveCrosswordEndpoint,
  resolveWidgetSrc,
  resolvePrivacyBlankChance,
  resolvePrivacyMaxCount,
  resolvePrivacyDisplayCount,
  resolvePrivacyShowVpnMessages,
  resolvePrivacyCelebrationThreshold,
  PREF_WIDGETS_ORDER,
} from "common/WidgetsRegistry.mjs";

const registryIds = WIDGET_REGISTRY.map(w => w.id);
const listsWidget = WIDGET_REGISTRY.find(w => w.id === "lists");

describe("isWidgetToggleVisible", () => {
  it("is false when nothing enables it", () => {
    expect(isWidgetToggleVisible(listsWidget, {})).toBe(false);
  });

  it("is true via the system pref", () => {
    expect(
      isWidgetToggleVisible(listsWidget, {
        "widgets.system.lists.enabled": true,
      })
    ).toBe(true);
  });

  it("is true via trainhopConfig.widgets (addable)", () => {
    expect(
      isWidgetToggleVisible(listsWidget, {
        region: "US",
        trainhopConfig: { widgets: { listsEnabled: true } },
      })
    ).toBe(true);
  });

  it("is true via the widgetsSettings.*Visible override", () => {
    expect(
      isWidgetToggleVisible(listsWidget, {
        region: "US",
        trainhopConfig: { widgetsSettings: { listsVisible: true } },
      })
    ).toBe(true);
  });

  it("is true via the legacy widgetsConfig override", () => {
    expect(
      isWidgetToggleVisible(listsWidget, {
        widgetsConfig: { listsEnabled: true },
      })
    ).toBe(true);
  });

  it("is additive only — a false widgetsSettings value cannot hide a system-enabled toggle", () => {
    expect(
      isWidgetToggleVisible(listsWidget, {
        "widgets.system.lists.enabled": true,
        trainhopConfig: { widgetsSettings: { listsVisible: false } },
      })
    ).toBe(true);
  });
});

// Bug 2063207: the privacy widget's readout needs history, so it is hidden
// outright on profiles that record none.
describe("privacy widget on a profile with no history", () => {
  const privacy = WIDGET_REGISTRY.find(w => w.id === "privacy");
  const everythingOn = {
    "widgets.enabled": true,
    "widgets.privacy.enabled": true,
    "widgets.system.privacy.enabled": true,
    region: "US",
    widgetsConfig: { privacyEnabled: true },
    trainhopConfig: {
      widgetPrivacy: { visible: true },
      widgets: { privacyEnabled: true },
      widgetsSettings: { privacyVisible: true },
    },
  };

  it("is not addable, visible or enabled when history is off", () => {
    const prefs = { ...everythingOn, recordsHistory: false };
    expect(isWidgetAddable(privacy, prefs)).toBe(false);
    expect(isWidgetToggleVisible(privacy, prefs)).toBe(false);
    expect(isWidgetEnabled(privacy, prefs, true)).toBe(false);
  });

  it("is unaffected when history is on", () => {
    const prefs = { ...everythingOn, recordsHistory: true };
    expect(isWidgetAddable(privacy, prefs)).toBe(true);
    expect(isWidgetToggleVisible(privacy, prefs)).toBe(true);
    expect(isWidgetEnabled(privacy, prefs, true)).toBe(true);
  });

  it("stays available when the value is missing entirely", () => {
    // A missing broadcast must not hide a working widget.
    expect(isWidgetAddable(privacy, everythingOn)).toBe(true);
  });

  it("leaves widgets that do not need history alone", () => {
    expect(
      isWidgetAddable(listsWidget, {
        "widgets.system.lists.enabled": true,
        recordsHistory: false,
      })
    ).toBe(true);
  });
});

describe("widgets on a profile with no resolved region", () => {
  const trainhopReveal = {
    [listsWidget.enabledPref]: true,
    trainhopConfig: { widgets: { [listsWidget.trainhopEnabledKey]: true } },
  };

  it("ignores a trainhopConfig.widgets reveal", () => {
    expect(isWidgetAddable(listsWidget, trainhopReveal)).toBe(false);
    expect(isWidgetToggleVisible(listsWidget, trainhopReveal)).toBe(false);
    expect(isWidgetEnabled(listsWidget, trainhopReveal, true)).toBe(false);
  });

  it("ignores a widgetsSettings.*Visible reveal", () => {
    const prefs = {
      [listsWidget.enabledPref]: true,
      trainhopConfig: {
        widgetsSettings: { [listsWidget.widgetsSettingsVisibleKey]: true },
      },
    };
    expect(isWidgetAddable(listsWidget, prefs)).toBe(false);
    expect(isWidgetToggleVisible(listsWidget, prefs)).toBe(false);
    expect(isWidgetEnabled(listsWidget, prefs, true)).toBe(false);
  });

  it("ignores a dedicated trainhopNamespace reveal", () => {
    const potd = WIDGET_REGISTRY.find(w => w.id === "pictureOfTheDay");
    const prefs = {
      [potd.enabledPref]: true,
      trainhopConfig: { [potd.trainhopNamespace]: { visible: true } },
    };
    expect(isWidgetAddable(potd, prefs)).toBe(false);
    expect(isWidgetToggleVisible(potd, prefs)).toBe(false);
    expect(isWidgetEnabled(potd, prefs, true)).toBe(false);
  });

  it("treats an empty or null region as unresolved", () => {
    expect(
      isWidgetAddable(listsWidget, { ...trainhopReveal, region: "" })
    ).toBe(false);
    expect(
      isWidgetAddable(listsWidget, { ...trainhopReveal, region: null })
    ).toBe(false);
  });

  it("acts on the reveal as soon as any region resolves", () => {
    const prefs = { ...trainhopReveal, region: "FR" };
    expect(isWidgetAddable(listsWidget, prefs)).toBe(true);
    expect(isWidgetToggleVisible(listsWidget, prefs)).toBe(true);
    expect(isWidgetEnabled(listsWidget, prefs, true)).toBe(true);
  });

  it("leaves the system pref ungated, so a build or operator reveal still works", () => {
    const prefs = {
      [listsWidget.enabledPref]: true,
      [listsWidget.systemEnabledPref]: true,
    };
    expect(isWidgetAddable(listsWidget, prefs)).toBe(true);
    expect(isWidgetToggleVisible(listsWidget, prefs)).toBe(true);
    expect(isWidgetEnabled(listsWidget, prefs, true)).toBe(true);
  });

  it("withholds the reveal without hiding a system-enabled widget", () => {
    const prefs = {
      ...trainhopReveal,
      [listsWidget.systemEnabledPref]: true,
    };
    expect(isWidgetAddable(listsWidget, prefs)).toBe(true);
    expect(isWidgetToggleVisible(listsWidget, prefs)).toBe(true);
    expect(isWidgetEnabled(listsWidget, prefs, true)).toBe(true);
    expect(
      isWidgetsContainerVisible({
        "widgets.system.enabled": true,
        trainhopConfig: { widgets: { enabled: true } },
      })
    ).toBe(true);
  });

  it("leaves the legacy widgetsConfig toggle ungated", () => {
    const prefs = {
      [listsWidget.enabledPref]: true,
      widgetsConfig: { [listsWidget.trainhopEnabledKey]: true },
    };
    expect(isWidgetToggleVisible(listsWidget, prefs)).toBe(true);
    expect(isWidgetAddable(listsWidget, prefs)).toBe(false);
    expect(isWidgetEnabled(listsWidget, prefs, true)).toBe(false);
  });

  it("gates the container's trainhop terms but not its pref terms", () => {
    expect(
      isWidgetsContainerVisible({
        trainhopConfig: { widgets: { enabled: true } },
      })
    ).toBe(false);
    expect(
      isWidgetsContainerVisible({
        trainhopConfig: { widgetsSettings: { enabled: true } },
      })
    ).toBe(false);
    expect(isWidgetsContainerVisible({ "widgets.system.enabled": true })).toBe(
      true
    );
    expect(
      isWidgetsContainerVisible({ widgetsConfig: { enabled: true } })
    ).toBe(true);
    expect(
      isWidgetsContainerVisible({
        region: "FR",
        trainhopConfig: { widgets: { enabled: true } },
      })
    ).toBe(true);
  });
});

describe("isWidgetsContainerVisible", () => {
  it("is false when nothing enables it", () => {
    expect(isWidgetsContainerVisible({})).toBe(false);
  });

  it("is true via the system pref", () => {
    expect(isWidgetsContainerVisible({ "widgets.system.enabled": true })).toBe(
      true
    );
  });

  it("is true via trainhopConfig.widgets.enabled", () => {
    expect(
      isWidgetsContainerVisible({
        region: "US",
        trainhopConfig: { widgets: { enabled: true } },
      })
    ).toBe(true);
  });

  it("is true via widgetsSettings.enabled", () => {
    expect(
      isWidgetsContainerVisible({
        region: "US",
        trainhopConfig: { widgetsSettings: { enabled: true } },
      })
    ).toBe(true);
  });
});

describe("getWidgetOrder", () => {
  it("returns registry default order when pref is empty", () => {
    expect(getWidgetOrder("")).toEqual(registryIds);
  });

  it("returns registry default order when pref is null/undefined", () => {
    expect(getWidgetOrder(null)).toEqual(registryIds);
    expect(getWidgetOrder(undefined)).toEqual(registryIds);
  });

  it("respects a fully-specified custom order", () => {
    expect(
      getWidgetOrder("focusTimer,lists,weather,clocks,privacy,crossword")
    ).toEqual([
      "focusTimer",
      "lists",
      "weather",
      "clocks",
      "privacy",
      "crossword",
      "pictureOfTheDay",
      "stocks",
      "recentSearches",
      "horoscopes",
      "travel",
      "solitaire",
    ]);
  });

  it("appends missing registry IDs after saved ones", () => {
    expect(getWidgetOrder("weather")).toEqual([
      "weather",
      "pictureOfTheDay",
      "clocks",
      "lists",
      "focusTimer",
      "privacy",
      "crossword",
      "stocks",
      "recentSearches",
      "horoscopes",
      "travel",
      "solitaire",
    ]);
  });

  it("filters out unknown IDs from the saved pref", () => {
    expect(getWidgetOrder("unknownWidget,lists,weather")).toEqual([
      "lists",
      "weather",
      "pictureOfTheDay",
      "clocks",
      "focusTimer",
      "privacy",
      "crossword",
      "stocks",
      "recentSearches",
      "horoscopes",
      "travel",
      "solitaire",
    ]);
  });

  it("handles partial order with only one ID", () => {
    const result = getWidgetOrder("focusTimer");
    expect(result[0]).toBe("focusTimer");
    expect(result.length).toBe(registryIds.length);
  });

  it("deduplicates repeated IDs in the saved pref", () => {
    const result = getWidgetOrder("focusTimer,focusTimer,lists");
    expect(result).toEqual([
      "focusTimer",
      "lists",
      "pictureOfTheDay",
      "clocks",
      "weather",
      "privacy",
      "crossword",
      "stocks",
      "recentSearches",
      "horoscopes",
      "travel",
      "solitaire",
    ]);
    expect(result.length).toBe(registryIds.length);
  });
});

describe("resolveWidgetOrder", () => {
  it("returns registry order when no user or trainhop order is set", () => {
    expect(resolveWidgetOrder({ [PREF_WIDGETS_ORDER]: "" })).toEqual(
      registryIds
    );
  });

  it("uses the user-saved order when set", () => {
    expect(
      resolveWidgetOrder({ [PREF_WIDGETS_ORDER]: "weather,lists,focusTimer" })
    ).toEqual([
      "weather",
      "lists",
      "focusTimer",
      "pictureOfTheDay",
      "clocks",
      "privacy",
      "crossword",
      "stocks",
      "recentSearches",
      "horoscopes",
      "travel",
      "solitaire",
    ]);
  });

  it("uses trainhop order when no user order is saved", () => {
    expect(
      resolveWidgetOrder({
        [PREF_WIDGETS_ORDER]: "",
        trainhopConfig: { widgets: { order: "focusTimer,weather,lists" } },
      })
    ).toEqual([
      "focusTimer",
      "weather",
      "lists",
      "pictureOfTheDay",
      "clocks",
      "privacy",
      "crossword",
      "stocks",
      "recentSearches",
      "horoscopes",
      "travel",
      "solitaire",
    ]);
  });

  it("user order takes precedence over trainhop order", () => {
    expect(
      resolveWidgetOrder({
        [PREF_WIDGETS_ORDER]: "lists,focusTimer,weather",
        trainhopConfig: { widgets: { order: "weather,lists,focusTimer" } },
      })
    ).toEqual([
      "lists",
      "focusTimer",
      "weather",
      "pictureOfTheDay",
      "clocks",
      "privacy",
      "crossword",
      "stocks",
      "recentSearches",
      "horoscopes",
      "travel",
      "solitaire",
    ]);
  });
});

describe("isWidgetAddable", () => {
  const listsWidget = WIDGET_REGISTRY.find(w => w.id === "lists");

  it("returns true when system pref is set", () => {
    expect(
      isWidgetAddable(listsWidget, {
        [listsWidget.systemEnabledPref]: true,
      })
    ).toBe(true);
  });

  it("returns true when trainhop overrides the system gate", () => {
    expect(
      isWidgetAddable(listsWidget, {
        [listsWidget.systemEnabledPref]: false,
        region: "US",
        trainhopConfig: {
          widgets: { [listsWidget.trainhopEnabledKey]: true },
        },
      })
    ).toBe(true);
  });

  it("returns false when neither system nor trainhop are set", () => {
    expect(
      isWidgetAddable(listsWidget, {
        [listsWidget.systemEnabledPref]: false,
      })
    ).toBe(false);
  });

  it("is addable when revealed via widgetsSettings (so the toggle is functional)", () => {
    expect(
      isWidgetAddable(listsWidget, {
        [listsWidget.systemEnabledPref]: false,
        region: "US",
        trainhopConfig: { widgetsSettings: { listsVisible: true } },
      })
    ).toBe(true);
  });

  it("does not consider the user's enabled pref", () => {
    expect(
      isWidgetAddable(listsWidget, {
        [listsWidget.systemEnabledPref]: true,
        [listsWidget.enabledPref]: false,
      })
    ).toBe(true);
  });

  it("is addable when revealed via the dedicated widgetPictureOfTheDay namespace", () => {
    const potd = WIDGET_REGISTRY.find(w => w.id === "pictureOfTheDay");
    expect(
      isWidgetAddable(potd, {
        [potd.systemEnabledPref]: false,
        region: "US",
        trainhopConfig: { widgetPictureOfTheDay: { visible: true } },
      })
    ).toBe(true);
  });

  it("is addable when revealed via the dedicated widgetCrossword namespace", () => {
    const crossword = WIDGET_REGISTRY.find(w => w.id === "crossword");
    expect(
      isWidgetAddable(crossword, {
        [crossword.systemEnabledPref]: false,
        region: "US",
        trainhopConfig: { widgetCrossword: { visible: true } },
      })
    ).toBe(true);
  });

  it.each([
    ["widgets", { privacyEnabled: true }],
    ["widgetsSettings", { privacyVisible: true }],
  ])(
    "makes privacy addable via %s despite its preffed-off default",
    (type, payload) => {
      const privacy = WIDGET_REGISTRY.find(w => w.id === "privacy");
      const prefs = { [privacy.systemEnabledPref]: false, region: "US" };
      expect(isWidgetAddable(privacy, prefs)).toBe(false);
      expect(
        isWidgetAddable(privacy, {
          ...prefs,
          trainhopConfig: { [type]: payload },
        })
      ).toBe(true);
    }
  );

  it("is addable when revealed via the dedicated widgetPrivacy namespace", () => {
    const privacy = WIDGET_REGISTRY.find(w => w.id === "privacy");
    expect(
      isWidgetAddable(privacy, {
        [privacy.systemEnabledPref]: false,
        region: "US",
        trainhopConfig: { widgetPrivacy: { visible: true } },
      })
    ).toBe(true);
  });

  it.each([
    ["widgets", { stocksEnabled: true }],
    ["widgetsSettings", { stocksVisible: true }],
  ])(
    "makes stocks addable via %s despite its preffed-off default",
    (type, payload) => {
      const stocks = WIDGET_REGISTRY.find(w => w.id === "stocks");
      const prefs = { [stocks.systemEnabledPref]: false, region: "US" };
      expect(isWidgetAddable(stocks, prefs)).toBe(false);
      expect(
        isWidgetAddable(stocks, {
          ...prefs,
          trainhopConfig: { [type]: payload },
        })
      ).toBe(true);
    }
  );

  it.each([
    ["widgets", { recentSearchesEnabled: true }],
    ["widgetsSettings", { recentSearchesVisible: true }],
    ["widgetRecentSearches", { visible: true }],
  ])(
    "makes recent searches addable via %s despite its preffed-off default",
    (type, payload) => {
      const recentSearches = WIDGET_REGISTRY.find(
        w => w.id === "recentSearches"
      );
      const prefs = {
        [recentSearches.systemEnabledPref]: false,
        region: "US",
      };
      expect(isWidgetAddable(recentSearches, prefs)).toBe(false);
      expect(
        isWidgetAddable(recentSearches, {
          ...prefs,
          trainhopConfig: { [type]: payload },
        })
      ).toBe(true);
    }
  );

  it("is addable when revealed via the dedicated widgetRecentSearches namespace", () => {
    const recentSearches = WIDGET_REGISTRY.find(w => w.id === "recentSearches");
    expect(
      isWidgetAddable(recentSearches, {
        [recentSearches.systemEnabledPref]: false,
        region: "US",
        trainhopConfig: { widgetRecentSearches: { visible: true } },
      })
    ).toBe(true);
  });

  it("is addable when revealed via the dedicated widgetHoroscopes namespace", () => {
    const horoscopes = WIDGET_REGISTRY.find(w => w.id === "horoscopes");
    expect(
      isWidgetAddable(horoscopes, {
        [horoscopes.systemEnabledPref]: false,
        region: "US",
        trainhopConfig: { widgetHoroscopes: { visible: true } },
      })
    ).toBe(true);
  });

  it("is addable when revealed via the dedicated widgetTravel namespace", () => {
    const travel = WIDGET_REGISTRY.find(w => w.id === "travel");
    expect(
      isWidgetAddable(travel, {
        [travel.systemEnabledPref]: false,
        region: "US",
        trainhopConfig: { widgetTravel: { visible: true } },
      })
    ).toBe(true);
  });

  it("is addable when revealed via the dedicated widgetSolitaire namespace", () => {
    const solitaire = WIDGET_REGISTRY.find(w => w.id === "solitaire");
    expect(
      isWidgetAddable(solitaire, {
        [solitaire.systemEnabledPref]: false,
        region: "US",
        trainhopConfig: { widgetSolitaire: { visible: true } },
      })
    ).toBe(true);
  });
});

describe("isWidgetEnabled", () => {
  const listsWidget = WIDGET_REGISTRY.find(w => w.id === "lists");

  it("returns false when widgetsEnabled is false", () => {
    expect(
      isWidgetEnabled(
        listsWidget,
        {
          [listsWidget.enabledPref]: true,
          [listsWidget.systemEnabledPref]: true,
        },
        false
      )
    ).toBe(false);
  });

  it("returns true when system pref and user pref are both set", () => {
    expect(
      isWidgetEnabled(
        listsWidget,
        {
          [listsWidget.enabledPref]: true,
          [listsWidget.systemEnabledPref]: true,
        },
        true
      )
    ).toBe(true);
  });

  it("returns false when system pref is not set and no trainhop", () => {
    expect(
      isWidgetEnabled(
        listsWidget,
        {
          [listsWidget.enabledPref]: true,
          [listsWidget.systemEnabledPref]: false,
        },
        true
      )
    ).toBe(false);
  });

  it("returns false when user pref is disabled even if system is set", () => {
    expect(
      isWidgetEnabled(
        listsWidget,
        {
          [listsWidget.enabledPref]: false,
          [listsWidget.systemEnabledPref]: true,
        },
        true
      )
    ).toBe(false);
  });

  it("returns true when trainhop overrides the system gate", () => {
    expect(
      isWidgetEnabled(
        listsWidget,
        {
          [listsWidget.enabledPref]: true,
          [listsWidget.systemEnabledPref]: false,
          region: "US",
          trainhopConfig: {
            widgets: { [listsWidget.trainhopEnabledKey]: true },
          },
        },
        true
      )
    ).toBe(true);
  });
});

describe("resolveWidgetSize", () => {
  const weatherWidget = WIDGET_REGISTRY.find(w => w.id === "weather");

  it("returns the user-set pref when non-empty", () => {
    expect(
      resolveWidgetSize(weatherWidget, { [weatherWidget.sizePref]: "large" })
    ).toBe("large");
  });

  it("falls back to defaultSize when pref is empty and no trainhop", () => {
    expect(
      resolveWidgetSize(weatherWidget, { [weatherWidget.sizePref]: "" })
    ).toBe(weatherWidget.defaultSize);
  });

  it("uses trainhop size suggestion when pref is empty", () => {
    expect(
      resolveWidgetSize(weatherWidget, {
        [weatherWidget.sizePref]: "",
        trainhopConfig: {
          widgets: { [weatherWidget.trainhopSizeKey]: "small" },
        },
      })
    ).toBe("small");
  });

  it("user pref wins over trainhop suggestion", () => {
    expect(
      resolveWidgetSize(weatherWidget, {
        [weatherWidget.sizePref]: "medium",
        trainhopConfig: {
          widgets: { [weatherWidget.trainhopSizeKey]: "large" },
        },
      })
    ).toBe("medium");
  });

  it("prefers the dedicated widgetPictureOfTheDay size over the shared widgets key", () => {
    const potd = WIDGET_REGISTRY.find(w => w.id === "pictureOfTheDay");
    expect(
      resolveWidgetSize(potd, {
        [potd.sizePref]: "",
        trainhopConfig: {
          widgetPictureOfTheDay: { size: "large" },
          widgets: { [potd.trainhopSizeKey]: "medium" },
        },
      })
    ).toBe("large");
  });

  it("falls back to the shared widgets size key for POTD when no dedicated size", () => {
    const potd = WIDGET_REGISTRY.find(w => w.id === "pictureOfTheDay");
    expect(
      resolveWidgetSize(potd, {
        [potd.sizePref]: "",
        trainhopConfig: { widgets: { [potd.trainhopSizeKey]: "large" } },
      })
    ).toBe("large");
  });

  it("prefers the dedicated widgetCrossword size over the shared widgets key", () => {
    const crossword = WIDGET_REGISTRY.find(w => w.id === "crossword");
    expect(
      resolveWidgetSize(crossword, {
        [crossword.sizePref]: "",
        trainhopConfig: {
          widgetCrossword: { size: "large" },
          widgets: { [crossword.trainhopSizeKey]: "medium" },
        },
      })
    ).toBe("large");
  });

  it("falls back to the shared widgets size key for crossword when no dedicated size", () => {
    const crossword = WIDGET_REGISTRY.find(w => w.id === "crossword");
    expect(
      resolveWidgetSize(crossword, {
        [crossword.sizePref]: "",
        trainhopConfig: { widgets: { [crossword.trainhopSizeKey]: "large" } },
      })
    ).toBe("large");
  });

  it("prefers the dedicated widgetPrivacy size over the shared widgets key", () => {
    const privacy = WIDGET_REGISTRY.find(w => w.id === "privacy");
    expect(
      resolveWidgetSize(privacy, {
        [privacy.sizePref]: "",
        trainhopConfig: {
          widgetPrivacy: { size: "large" },
          widgets: { [privacy.trainhopSizeKey]: "medium" },
        },
      })
    ).toBe("large");
  });

  it("falls back to the shared widgets size key for privacy when no dedicated size", () => {
    const privacy = WIDGET_REGISTRY.find(w => w.id === "privacy");
    expect(
      resolveWidgetSize(privacy, {
        [privacy.sizePref]: "",
        trainhopConfig: { widgets: { [privacy.trainhopSizeKey]: "large" } },
      })
    ).toBe("large");
  });
});

describe("resolveWidgetHasSidebar", () => {
  const weatherWidget = WIDGET_REGISTRY.find(w => w.id === "weather");
  const listsWidget = WIDGET_REGISTRY.find(w => w.id === "lists");

  it("returns the static hasSidebar value when no trainhop key is defined", () => {
    expect(resolveWidgetHasSidebar(listsWidget, {})).toBe(false);
  });

  it("returns the static hasSidebar value when trainhop key is not present in prefs", () => {
    expect(resolveWidgetHasSidebar(weatherWidget, {})).toBe(true);
  });

  it("returns the trainhop override when present", () => {
    expect(
      resolveWidgetHasSidebar(weatherWidget, {
        trainhopConfig: {
          widgets: { [weatherWidget.trainhopSidebarKey]: false },
        },
      })
    ).toBe(false);
  });

  it("trainhop can force sidebar on for a widget with hasSidebar: true", () => {
    expect(
      resolveWidgetHasSidebar(weatherWidget, {
        trainhopConfig: {
          widgets: { [weatherWidget.trainhopSidebarKey]: true },
        },
      })
    ).toBe(true);
  });
});

describe("resolveSidebarWidget", () => {
  const weatherWidget = WIDGET_REGISTRY.find(w => w.id === "weather");
  const privacyWidget = WIDGET_REGISTRY.find(w => w.id === "privacy");

  const weatherSmall = {
    "widgets.enabled": true,
    [weatherWidget.enabledPref]: true,
    [weatherWidget.systemEnabledPref]: true,
    [weatherWidget.sizePref]: "small",
  };
  const privacySmall = {
    "widgets.enabled": true,
    [privacyWidget.enabledPref]: true,
    [privacyWidget.systemEnabledPref]: true,
    [privacyWidget.sizePref]: "small",
    trainhopConfig: {
      widgets: { [privacyWidget.trainhopSidebarKey]: true },
    },
  };

  it("returns null when nothing qualifies", () => {
    expect(resolveSidebarWidget({ "widgets.enabled": true })).toBeNull();
  });

  it("returns weather when it is enabled and small", () => {
    expect(resolveSidebarWidget(weatherSmall)).toBe(weatherWidget);
  });

  it("honors an explicit widgetsEnabled argument over the pref", () => {
    expect(
      resolveSidebarWidget({ ...weatherSmall, "widgets.enabled": false }, true)
    ).toBe(weatherWidget);
  });

  it("returns null when the widget is not small", () => {
    expect(
      resolveSidebarWidget({
        ...weatherSmall,
        [weatherWidget.sizePref]: "medium",
      })
    ).toBeNull();
  });

  it("does not place privacy in the sidebar by default", () => {
    expect(
      resolveSidebarWidget({ ...privacySmall, trainhopConfig: {} })
    ).toBeNull();
  });

  it("returns privacy when trainhop turns its sidebar placement on at small", () => {
    expect(resolveSidebarWidget(privacySmall)).toBe(privacyWidget);
  });

  it("returns only the first qualifying widget in registry order", () => {
    const both = { ...weatherSmall, ...privacySmall };
    expect(resolveSidebarWidget(both)).toBe(weatherWidget);
  });
});

describe("hasContentAreaWidgets", () => {
  const weatherWidget = WIDGET_REGISTRY.find(w => w.id === "weather");
  const privacyWidget = WIDGET_REGISTRY.find(w => w.id === "privacy");
  const listsWidget = WIDGET_REGISTRY.find(w => w.id === "lists");

  const weatherSmall = {
    "widgets.enabled": true,
    [weatherWidget.enabledPref]: true,
    [weatherWidget.systemEnabledPref]: true,
    [weatherWidget.sizePref]: "small",
  };
  const privacyEnabled = {
    [privacyWidget.enabledPref]: true,
    [privacyWidget.systemEnabledPref]: true,
  };
  const listsEnabled = {
    [listsWidget.enabledPref]: true,
    [listsWidget.systemEnabledPref]: true,
  };

  it("is false when no widget is enabled", () => {
    expect(hasContentAreaWidgets({ "widgets.enabled": true })).toBe(false);
  });

  it("is false when the only enabled widget sits in the sidebar", () => {
    expect(hasContentAreaWidgets(weatherSmall)).toBe(false);
  });

  it("is true when another widget is enabled beside the sidebar one", () => {
    expect(hasContentAreaWidgets({ ...weatherSmall, ...listsEnabled })).toBe(
      true
    );
  });

  it("is false when privacy is the only widget and it sits in the sidebar", () => {
    expect(
      hasContentAreaWidgets({
        "widgets.enabled": true,
        ...privacyEnabled,
        [privacyWidget.sizePref]: "small",
        trainhopConfig: {
          widgets: { [privacyWidget.trainhopSidebarKey]: true },
        },
      })
    ).toBe(false);
  });

  it("uses the widgetsEnabled argument instead of the pref", () => {
    expect(
      hasContentAreaWidgets({ "widgets.enabled": false, ...listsEnabled }, true)
    ).toBe(true);
    expect(
      hasContentAreaWidgets({ "widgets.enabled": true, ...listsEnabled }, false)
    ).toBe(false);
  });
});

describe("resolveCrosswordEndpoint", () => {
  const dedicatedEndpoint = "https://dedicated.example.com/index.html";
  const sharedEndpoint = "https://shared.example.com/index.html";
  const prefEndpoint = "https://pref.example.com/index.html";

  it("prefers the dedicated widgetCrossword endpoint over the shared key and pref", () => {
    expect(
      resolveCrosswordEndpoint({
        "widgets.crossword.endpoint": prefEndpoint,
        trainhopConfig: {
          widgetCrossword: { endpoint: dedicatedEndpoint },
          widgets: { crosswordEndpoint: sharedEndpoint },
        },
      })
    ).toBe(dedicatedEndpoint);
  });

  it("falls back to the shared widgets endpoint when no dedicated endpoint", () => {
    expect(
      resolveCrosswordEndpoint({
        "widgets.crossword.endpoint": prefEndpoint,
        trainhopConfig: { widgets: { crosswordEndpoint: sharedEndpoint } },
      })
    ).toBe(sharedEndpoint);
  });

  it("falls back to the raw pref when no trainhop override is present", () => {
    expect(
      resolveCrosswordEndpoint({ "widgets.crossword.endpoint": prefEndpoint })
    ).toBe(prefEndpoint);
  });
});

const testWidget = {
  id: "testWidget",
  src: "https://static.example.com/widget.html",
  trainhopNamespace: "widgetTest",
};

describe("resolveWidgetSrc", () => {
  it("prefers the trainhop namespace src over the static src", () => {
    const trainhopSrc = "https://trainhop.example.com/widget.html";
    expect(
      resolveWidgetSrc(testWidget, {
        trainhopConfig: { widgetTest: { src: trainhopSrc } },
      })
    ).toBe(trainhopSrc);
  });

  it("falls back to the static entry src", () => {
    expect(resolveWidgetSrc(testWidget, {})).toBe(testWidget.src);
  });
});

describe("getWidgets", () => {
  const extensionWidget = {
    id: "extensionWidget",
    extensionID: "test-widget@mozilla.org",
    customizeL10nId: "registry-label",
  };

  it("keeps an entry without an extensionID", () => {
    expect(getWidgets([listsWidget], {})).toEqual([listsWidget]);
  });

  it("drops an extension entry the ExtensionWidgets state lacks", () => {
    expect(getWidgets([extensionWidget], {})).toEqual([]);
  });

  it("merges the ExtensionWidgets entry under the registry fields", () => {
    const src = "moz-extension://test/w.html";
    expect(
      getWidgets([extensionWidget], {
        [extensionWidget.extensionID]: { src },
      })
    ).toEqual([{ ...extensionWidget, src }]);
  });

  it("treats an undefined ExtensionWidgets state as empty", () => {
    expect(getWidgets([listsWidget, extensionWidget], undefined)).toEqual([
      listsWidget,
    ]);
  });
});

describe("resolvePrivacyBlankChance", () => {
  it("parses the string pref as a 0-1 fraction", () => {
    expect(
      resolvePrivacyBlankChance({ "widgets.privacy.blankChance": "0.25" })
    ).toBe(0.25);
  });

  it("defaults to 0.4 when unset, without warning", () => {
    const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
    expect(resolvePrivacyBlankChance({})).toBe(0.4);
    expect(warn).not.toHaveBeenCalled();
    warn.mockRestore();
  });

  it("warns and defaults to 0.4 for a present-but-unparseable value", () => {
    const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
    expect(
      resolvePrivacyBlankChance({ "widgets.privacy.blankChance": "abc" })
    ).toBe(0.4);
    expect(warn).toHaveBeenCalled();
    warn.mockRestore();
  });

  it("treats a parsed 0 as valid (blanks off), not as the default", () => {
    expect(
      resolvePrivacyBlankChance({ "widgets.privacy.blankChance": "0" })
    ).toBe(0);
  });

  it("warns and falls back to 0.4 for an out-of-range value (e.g. 40)", () => {
    const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
    expect(
      resolvePrivacyBlankChance({ "widgets.privacy.blankChance": "40" })
    ).toBe(0.4);
    expect(warn).toHaveBeenCalled();
    warn.mockRestore();
  });

  it("prefers a valid trainhopConfig number over the pref", () => {
    expect(
      resolvePrivacyBlankChance({
        "widgets.privacy.blankChance": "0.9",
        trainhopConfig: { widgets: { privacyBlankChance: 0.1 } },
      })
    ).toBe(0.1);
  });

  it("prefers the dedicated widgetPrivacy blankChance over the shared key", () => {
    expect(
      resolvePrivacyBlankChance({
        "widgets.privacy.blankChance": "0.9",
        trainhopConfig: {
          widgetPrivacy: { blankChance: 0.1 },
          widgets: { privacyBlankChance: 0.7 },
        },
      })
    ).toBe(0.1);
  });

  it("lets a dedicated 0 win over a nonzero shared key", () => {
    expect(
      resolvePrivacyBlankChance({
        trainhopConfig: {
          widgetPrivacy: { blankChance: 0 },
          widgets: { privacyBlankChance: 0.7 },
        },
      })
    ).toBe(0);
  });

  it("warns and falls through to the shared key when the dedicated value is a string", () => {
    const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
    expect(
      resolvePrivacyBlankChance({
        trainhopConfig: {
          // The string form is only correct for the pref, not the payload.
          widgetPrivacy: { blankChance: "0.4" },
          widgets: { privacyBlankChance: 0.7 },
        },
      })
    ).toBe(0.7);
    expect(warn).toHaveBeenCalled();
    warn.mockRestore();
  });
});

describe("resolvePrivacyMaxCount", () => {
  it("prefers the dedicated widgetPrivacy key over the shared key and pref", () => {
    expect(
      resolvePrivacyMaxCount({
        "widgets.privacy.maxCount": 10,
        trainhopConfig: {
          widgetPrivacy: { maxCount: 50 },
          widgets: { privacyMaxCount: 25 },
        },
      })
    ).toBe(50);
  });

  it("falls back to the shared key, then the pref, then 100", () => {
    expect(
      resolvePrivacyMaxCount({
        "widgets.privacy.maxCount": 10,
        trainhopConfig: { widgets: { privacyMaxCount: 25 } },
      })
    ).toBe(25);
    expect(resolvePrivacyMaxCount({ "widgets.privacy.maxCount": 10 })).toBe(10);
    expect(resolvePrivacyMaxCount({})).toBe(100);
  });

  it("warns and falls through to the shared key when the dedicated value is a string", () => {
    const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
    expect(
      resolvePrivacyMaxCount({
        trainhopConfig: {
          widgetPrivacy: { maxCount: "50" },
          widgets: { privacyMaxCount: 25 },
        },
      })
    ).toBe(25);
    expect(warn).toHaveBeenCalled();
    warn.mockRestore();
  });
});

describe("resolvePrivacyDisplayCount", () => {
  it("prefers the dedicated widgetPrivacy key over the shared key and pref", () => {
    expect(
      resolvePrivacyDisplayCount({
        "widgets.privacy.maxDisplayCount": 200,
        trainhopConfig: {
          widgetPrivacy: { maxDisplayCount: 50 },
          widgets: { privacyMaxDisplayCount: 100 },
        },
      })
    ).toBe(50);
  });

  it("falls back to the shared key, then the pref, then 999", () => {
    expect(
      resolvePrivacyDisplayCount({
        "widgets.privacy.maxDisplayCount": 200,
        trainhopConfig: { widgets: { privacyMaxDisplayCount: 100 } },
      })
    ).toBe(100);
    expect(
      resolvePrivacyDisplayCount({ "widgets.privacy.maxDisplayCount": 200 })
    ).toBe(200);
    expect(resolvePrivacyDisplayCount({})).toBe(999);
  });

  it("warns and falls through to the shared key when the dedicated value is a string", () => {
    const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
    expect(
      resolvePrivacyDisplayCount({
        trainhopConfig: {
          widgetPrivacy: { maxDisplayCount: "50" },
          widgets: { privacyMaxDisplayCount: 100 },
        },
      })
    ).toBe(100);
    expect(warn).toHaveBeenCalled();
    warn.mockRestore();
  });
});

describe("resolvePrivacyShowVpnMessages", () => {
  it("prefers the dedicated widgetPrivacy key over the shared key and pref", () => {
    expect(
      resolvePrivacyShowVpnMessages({
        "widgets.privacy.showVpnMessages": false,
        trainhopConfig: {
          widgetPrivacy: { showVpnMessages: true },
          widgets: { privacyShowVpnMessages: false },
        },
      })
    ).toBe(true);
  });

  it("lets a dedicated false win over a shared true", () => {
    expect(
      resolvePrivacyShowVpnMessages({
        "widgets.privacy.showVpnMessages": true,
        trainhopConfig: {
          widgetPrivacy: { showVpnMessages: false },
          widgets: { privacyShowVpnMessages: true },
        },
      })
    ).toBe(false);
  });

  it("falls back to the shared key, then the pref, then false", () => {
    expect(
      resolvePrivacyShowVpnMessages({
        "widgets.privacy.showVpnMessages": false,
        trainhopConfig: { widgets: { privacyShowVpnMessages: true } },
      })
    ).toBe(true);
    expect(
      resolvePrivacyShowVpnMessages({
        "widgets.privacy.showVpnMessages": true,
      })
    ).toBe(true);
    expect(resolvePrivacyShowVpnMessages({})).toBe(false);
  });

  it("warns and falls through to the shared key when the dedicated value is a string", () => {
    const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
    // Regression: the string used to mask the shared true and resolve to false,
    // silently leaving VPN promos off in an experiment that asked for them on.
    expect(
      resolvePrivacyShowVpnMessages({
        trainhopConfig: {
          widgetPrivacy: { showVpnMessages: "true" },
          widgets: { privacyShowVpnMessages: true },
        },
      })
    ).toBe(true);
    expect(warn).toHaveBeenCalled();
    warn.mockRestore();
  });
});

describe("resolvePrivacyCelebrationThreshold", () => {
  const PREF = "widgets.privacy.celebrationThreshold";

  it("defaults to 10 when unset", () => {
    expect(resolvePrivacyCelebrationThreshold({})).toBe(10);
  });

  it("uses a valid numeric pref", () => {
    expect(resolvePrivacyCelebrationThreshold({ [PREF]: 25 })).toBe(25);
  });

  it("prefers trainhopConfig over the pref", () => {
    expect(
      resolvePrivacyCelebrationThreshold({
        [PREF]: 25,
        trainhopConfig: { widgets: { privacyCelebrationThreshold: 5 } },
      })
    ).toBe(5);
  });

  it("ignores a non-numeric trainhop value and falls back to the pref", () => {
    expect(
      resolvePrivacyCelebrationThreshold({
        [PREF]: 25,
        trainhopConfig: { widgets: { privacyCelebrationThreshold: "5" } },
      })
    ).toBe(25);
  });

  it("rejects zero and negatives, which would fire on every refresh", () => {
    expect(resolvePrivacyCelebrationThreshold({ [PREF]: 0 })).toBe(10);
    expect(resolvePrivacyCelebrationThreshold({ [PREF]: -5 })).toBe(10);
  });

  it("rejects non-finite and non-numeric values", () => {
    expect(resolvePrivacyCelebrationThreshold({ [PREF]: NaN })).toBe(10);
    expect(resolvePrivacyCelebrationThreshold({ [PREF]: Infinity })).toBe(10);
    expect(resolvePrivacyCelebrationThreshold({ [PREF]: "25" })).toBe(10);
  });

  it("prefers the dedicated widgetPrivacy key over the shared key", () => {
    expect(
      resolvePrivacyCelebrationThreshold({
        [PREF]: 25,
        trainhopConfig: {
          widgetPrivacy: { celebrationThreshold: 5 },
          widgets: { privacyCelebrationThreshold: 20 },
        },
      })
    ).toBe(5);
  });

  it("warns and falls through to the shared key when the dedicated value is a string", () => {
    const warn = jest.spyOn(console, "warn").mockImplementation(() => {});
    expect(
      resolvePrivacyCelebrationThreshold({
        [PREF]: 25,
        trainhopConfig: {
          widgetPrivacy: { celebrationThreshold: "5" },
          widgets: { privacyCelebrationThreshold: 20 },
        },
      })
    ).toBe(20);
    expect(warn).toHaveBeenCalled();
    warn.mockRestore();
  });
});

describe("WIDGET_REGISTRY customize panel fields", () => {
  const activeWidgets = WIDGET_REGISTRY.filter(w => !w.retired);

  // Literal values, so a renamed source fails this test.
  const EXPECTED_EVENT_SOURCES = {
    pictureOfTheDay: "WIDGET_PICTURE_OF_THE_DAY",
    clocks: "WIDGET_CLOCKS",
    lists: "WIDGET_LISTS",
    focusTimer: "WIDGET_TIMER",
    weather: "WEATHER",
    privacy: "WIDGET_PRIVACY",
    crossword: "WIDGET_CROSSWORD",
    stocks: "WIDGET_STOCKS",
    recentSearches: "WIDGET_RECENT_SEARCHES",
    horoscopes: "WIDGET_HOROSCOPES",
    travel: "WIDGET_TRAVEL",
    solitaire: "WIDGET_SOLITAIRE",
  };

  it("gives every active entry a customizeL10nId", () => {
    for (const widget of activeWidgets) {
      expect(typeof widget.customizeL10nId).toBe("string");
      expect(widget.customizeL10nId).toMatch(/^newtab-custom-widget-/);
    }
  });

  it("keeps customizeL10nId unique across entries", () => {
    const ids = activeWidgets.map(w => w.customizeL10nId);
    expect(new Set(ids).size).toBe(ids.length);
  });

  it("carries today's customizeEventSource for every active entry", () => {
    expect(
      Object.fromEntries(activeWidgets.map(w => [w.id, w.customizeEventSource]))
    ).toEqual(EXPECTED_EVENT_SOURCES);
  });
});

describe("isWeatherAvailable", () => {
  it("is false when nothing reveals the weather toggle", () => {
    expect(isWeatherAvailable({})).toBe(false);
  });

  it("is true via the system.showWeather pref", () => {
    expect(isWeatherAvailable({ "system.showWeather": true })).toBe(true);
  });

  it("is true via trainhopConfig.weather.enabled", () => {
    expect(
      isWeatherAvailable({
        trainhopConfig: { weather: { enabled: true } },
      })
    ).toBe(true);
  });

  it("is true via trainhopConfig.widgetWeather.visible", () => {
    expect(
      isWeatherAvailable({
        trainhopConfig: { widgetWeather: { visible: true } },
      })
    ).toBe(true);
  });

  it("is true via the widgetsSettings.weatherVisible override", () => {
    expect(
      isWeatherAvailable({
        trainhopConfig: { widgetsSettings: { weatherVisible: true } },
      })
    ).toBe(true);
  });
});
