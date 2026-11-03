/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { act, render, fireEvent } from "@testing-library/react";
import { Provider } from "react-redux";
import { createStore, combineReducers } from "redux";
import { INITIAL_STATE, reducers } from "common/Reducers.sys.mjs";
import { WIDGET_REGISTRY } from "common/WidgetsRegistry.mjs";
import { WidgetsManagementPanel } from "content-src/components/Nova/CustomizeMenu/WidgetsManagementPanel/WidgetsManagementPanel";

// The active widgets in registry order, kept as literals so a renamed
// string, id or telemetry source fails here. The rendered order is pinned by
// EN_US_ORDER and the other order constants below.
const WIDGETS = [
  {
    id: "pictureOfTheDay",
    l10nId: "newtab-custom-widget-picture-toggle",
    source: "WIDGET_PICTURE_OF_THE_DAY",
    widget_name: "picture_of_the_day",
    preference: "widgets.pictureOfTheDay.enabled",
    widgetSize: "medium",
  },
  {
    id: "clocks",
    l10nId: "newtab-custom-widget-clock-toggle",
    source: "WIDGET_CLOCKS",
    widget_name: "clocks",
    preference: "widgets.clocks.enabled",
    widgetSize: "medium",
  },
  {
    id: "lists",
    l10nId: "newtab-custom-widget-lists-toggle",
    source: "WIDGET_LISTS",
    widget_name: "lists",
    preference: "widgets.lists.enabled",
    widgetSize: "medium",
  },
  {
    id: "focusTimer",
    l10nId: "newtab-custom-widget-timer-toggle",
    source: "WIDGET_TIMER",
    widget_name: "focus_timer",
    preference: "widgets.focusTimer.enabled",
    widgetSize: "medium",
  },
  {
    id: "weather",
    l10nId: "newtab-custom-widget-weather-toggle",
    source: "WEATHER",
    widget_name: "weather",
    preference: "widgets.weather.enabled",
    widgetSize: "small",
  },
  {
    id: "privacy",
    l10nId: "newtab-custom-widget-privacy-toggle",
    source: "WIDGET_PRIVACY",
    widget_name: "privacy",
    preference: "widgets.privacy.enabled",
    widgetSize: "medium",
  },
  {
    id: "crossword",
    l10nId: "newtab-custom-widget-crossword-toggle",
    source: "WIDGET_CROSSWORD",
    widget_name: "crossword",
    preference: "widgets.crossword.enabled",
    widgetSize: "medium",
  },
  {
    id: "stocks",
    l10nId: "newtab-custom-widget-stocks-toggle2",
    source: "WIDGET_STOCKS",
    widget_name: "stocks",
    preference: "widgets.stocks.enabled",
    widgetSize: "medium",
  },
  {
    id: "recentSearches",
    l10nId: "newtab-custom-widget-search-toggle",
    source: "WIDGET_RECENT_SEARCHES",
    widget_name: "recent_searches",
    preference: "widgets.recentSearches.enabled",
    widgetSize: "medium",
  },
  {
    id: "horoscopes",
    l10nId: "newtab-custom-widget-horoscopes-toggle",
    source: "WIDGET_HOROSCOPES",
    widget_name: "horoscopes",
    preference: "widgets.horoscopes.enabled",
    widgetSize: "medium",
  },
  {
    id: "travel",
    l10nId: "newtab-custom-widget-travel-toggle",
    source: "WIDGET_TRAVEL",
    widget_name: "travel",
    preference: "widgets.travel.enabled",
    widgetSize: "medium",
  },
  {
    id: "solitaire",
    l10nId: "newtab-custom-widget-solitaire-toggle",
    source: "WIDGET_SOLITAIRE",
    widget_name: "solitaire",
    preference: "widgets.solitaire.enabled",
    widgetSize: "medium",
  },
];

// The minimum entry the panel renders from; the registry helpers tolerate the
// missing fields.
const FIXTURE_WIDGET = {
  id: "fixtureWidget",
  telemetryName: "fixture_widget",
  customizeL10nId: "newtab-custom-widget-fixture-toggle",
  customizeEventSource: "WIDGET_FIXTURE",
  enabledPref: "widgets.fixtureWidget.enabled",
  sizePref: "widgets.fixtureWidget.size",
  defaultSize: "medium",
  systemEnabledPref: "widgets.system.fixtureWidget.enabled",
};

// Literal en-US labels; a missing key stands for a missing message.
const PANEL_LABELS = {
  "newtab-custom-widget-clock-toggle": "Clock",
  "newtab-custom-widget-crossword-toggle": "Crossword",
  "newtab-custom-widget-horoscopes-toggle": "Horoscopes",
  "newtab-custom-widget-lists-toggle": "Lists",
  "newtab-custom-widget-picture-toggle": "Picture of the day",
  "newtab-custom-widget-privacy-toggle": "Privacy",
  "newtab-custom-widget-search-toggle": "Search",
  "newtab-custom-widget-solitaire-toggle": "Solitaire",
  "newtab-custom-widget-stocks-toggle2": "Finance",
  "newtab-custom-widget-timer-toggle": "Timer",
  "newtab-custom-widget-travel-toggle": "Travel",
  "newtab-custom-widget-weather-toggle": "Weather",
  "newtab-custom-widget-fixture-toggle": "Fixture widget",
};

// The German, Greek and Japanese labels are Firefox's own translations, apart
// from the fixture widget's, the Japanese Search label, and the Horoscopes,
// Travel and Solitaire labels, which have no translations yet. The Finance fixtures reuse the Stocks
// translations as stand-ins until the renamed message is translated.
// Übersicht sorts before Uhr only under locale-aware collation; a code point
// comparison would put it after Wetter.
const GERMAN_PANEL_LABELS = {
  "newtab-custom-widget-stocks-toggle2": "Aktien",
  "newtab-custom-widget-picture-toggle": "Bild des Tages",
  "newtab-custom-widget-privacy-toggle": "Datenschutz",
  "newtab-custom-widget-horoscopes-toggle": "Horoskope",
  "newtab-custom-widget-crossword-toggle": "Kreuzworträtsel",
  "newtab-custom-widget-lists-toggle": "Listen",
  "newtab-custom-widget-travel-toggle": "Reisen",
  "newtab-custom-widget-solitaire-toggle": "Solitär",
  "newtab-custom-widget-search-toggle": "Suche",
  "newtab-custom-widget-timer-toggle": "Timer",
  "newtab-custom-widget-fixture-toggle": "Übersicht",
  "newtab-custom-widget-clock-toggle": "Uhr",
  "newtab-custom-widget-weather-toggle": "Wetter",
};

const GREEK_PANEL_LABELS = {
  "newtab-custom-widget-search-toggle": "Αναζήτηση",
  "newtab-custom-widget-timer-toggle": "Αντίστροφη μέτρηση",
  "newtab-custom-widget-privacy-toggle": "Απόρρητο",
  "newtab-custom-widget-picture-toggle": "Εικόνα της ημέρας",
  "newtab-custom-widget-weather-toggle": "Καιρός",
  "newtab-custom-widget-lists-toggle": "Λίστες",
  "newtab-custom-widget-stocks-toggle2": "Μετοχές",
  "newtab-custom-widget-solitaire-toggle": "Πασιέντζα",
  "newtab-custom-widget-clock-toggle": "Ρολόι",
  "newtab-custom-widget-crossword-toggle": "Σταυρόλεξο",
  "newtab-custom-widget-travel-toggle": "Ταξίδια",
  "newtab-custom-widget-horoscopes-toggle": "Ωροσκόπια",
};

// Latin letters sort before kana and kana before kanji under the default
// collation; the kanji follow radical and stroke order.
const JAPANESE_PANEL_LABELS = {
  "newtab-custom-widget-lists-toggle": "ToDo リスト",
  "newtab-custom-widget-crossword-toggle": "クロスワードパズル",
  "newtab-custom-widget-solitaire-toggle": "ソリティア",
  "newtab-custom-widget-timer-toggle": "タイマー",
  "newtab-custom-widget-travel-toggle": "トラベル",
  "newtab-custom-widget-privacy-toggle": "プライバシー",
  "newtab-custom-widget-horoscopes-toggle": "ホロスコープ",
  "newtab-custom-widget-picture-toggle": "今日の一枚",
  "newtab-custom-widget-search-toggle": "最近の検索",
  "newtab-custom-widget-weather-toggle": "天気予報",
  "newtab-custom-widget-clock-toggle": "時計",
  "newtab-custom-widget-stocks-toggle2": "株価情報",
};

// Expected orders are written by hand so the tests do not mirror the sort.
const EN_US_ORDER = [
  "clocks",
  "crossword",
  "stocks",
  "horoscopes",
  "lists",
  "pictureOfTheDay",
  "privacy",
  "recentSearches",
  "solitaire",
  "focusTimer",
  "travel",
  "weather",
];

// "Finance" sorts before "Fixture widget".
const EN_US_ORDER_WITH_FIXTURE = [
  "clocks",
  "crossword",
  "stocks",
  "fixtureWidget",
  "horoscopes",
  "lists",
  "pictureOfTheDay",
  "privacy",
  "recentSearches",
  "solitaire",
  "focusTimer",
  "travel",
  "weather",
];

const GERMAN_ORDER = [
  "stocks",
  "pictureOfTheDay",
  "privacy",
  "horoscopes",
  "crossword",
  "lists",
  "travel",
  "solitaire",
  "recentSearches",
  "focusTimer",
  "fixtureWidget",
  "clocks",
  "weather",
];

const GREEK_ORDER = [
  "recentSearches",
  "focusTimer",
  "privacy",
  "pictureOfTheDay",
  "weather",
  "lists",
  "stocks",
  "solitaire",
  "clocks",
  "crossword",
  "travel",
  "horoscopes",
];

const JAPANESE_ORDER = [
  "lists",
  "crossword",
  "solitaire",
  "focusTimer",
  "travel",
  "privacy",
  "horoscopes",
  "pictureOfTheDay",
  "recentSearches",
  "weather",
  "clocks",
  "stocks",
];

// The order when Timer alone sorts under its id, the rest under their labels:
// "focusTimer" follows "Finance" and precedes "Lists".
const TIMER_ID_SORT_ORDER = [
  "clocks",
  "crossword",
  "stocks",
  "focusTimer",
  "horoscopes",
  "lists",
  "pictureOfTheDay",
  "privacy",
  "recentSearches",
  "solitaire",
  "travel",
  "weather",
];

// The order when every widget sorts under its own id.
const ID_ORDER = [
  "clocks",
  "crossword",
  "focusTimer",
  "horoscopes",
  "lists",
  "pictureOfTheDay",
  "privacy",
  "recentSearches",
  "solitaire",
  "stocks",
  "travel",
  "weather",
];

describe("<WidgetsManagementPanel>", () => {
  let props;
  // Fluent id to label for this test; a missing key means no such message.
  let labels;

  // Pushed onto the real registry so tests can prove a new entry needs no
  // panel change.
  const pushedFixtures = [];
  const withFixtureWidget = (overrides = {}) => {
    const widget = { ...FIXTURE_WIDGET, ...overrides };
    WIDGET_REGISTRY.push(widget);
    pushedFixtures.push(widget);
    return widget;
  };

  // Turns on every registry widget's system pref plus Weather's. Call it after
  // pushing a fixture so the fixture is included.
  function allWidgetsVisible(extra = {}) {
    const prefs = { "system.showWeather": true };
    for (const widget of WIDGET_REGISTRY.filter(w => !w.retired)) {
      prefs[widget.systemEnabledPref] = true;
    }
    return { ...prefs, ...extra };
  }

  async function renderPanel(overrides = {}, prefValues = {}, state = {}) {
    const store = createStore(combineReducers(reducers), {
      ...INITIAL_STATE,
      ...state,
      Prefs: {
        ...INITIAL_STATE.Prefs,
        values: { ...INITIAL_STATE.Prefs.values, ...prefValues },
      },
    });
    jest.spyOn(store, "dispatch");
    const utils = render(
      <Provider store={store}>
        <WidgetsManagementPanel {...props} {...overrides} />
      </Provider>
    );
    // Flush the label effect so the toggles are rendered before the test
    // queries the DOM.
    await act(async () => {});
    return { ...utils, store };
  }

  function toggleIds(container) {
    return [...container.querySelectorAll("moz-toggle")].map(el => el.id);
  }

  // jsdom does not upgrade moz-toggle, so the test sets the pressed state the
  // real element would carry before firing its toggle event.
  function fireToggle(toggle, pressed) {
    toggle.pressed = pressed;
    fireEvent(toggle, new Event("toggle", { bubbles: true }));
  }

  function dispatchedAction(store, type) {
    return store.dispatch.mock.calls
      .map(([action]) => action)
      .find(action => action.type === type);
  }

  beforeEach(() => {
    props = { togglePanel: jest.fn(), showPanel: true, setPref: jest.fn() };
    labels = { ...PANEL_LABELS };
    // jsdom has no document.l10n; the labels are attribute-only messages, so
    // the mock answers formatMessages.
    document.l10n = {
      formatMessages: jest.fn(async keys =>
        keys.map(({ id }) =>
          id in labels
            ? {
                value: null,
                attributes: [{ name: "label", value: labels[id] }],
              }
            : null
        )
      ),
    };
  });

  afterEach(() => {
    delete document.l10n;
    for (const widget of pushedFixtures.splice(0)) {
      const index = WIDGET_REGISTRY.indexOf(widget);
      if (index !== -1) {
        WIDGET_REGISTRY.splice(index, 1);
      }
    }
    jest.restoreAllMocks();
  });

  it("renders the manage widgets button", async () => {
    const { container } = await renderPanel({ showPanel: false });
    expect(
      container.querySelector(".widgets-mgmt-panel-container")
    ).toBeInTheDocument();
    expect(container.querySelector("moz-box-button")).toBeInTheDocument();
  });

  describe("while the user places widgets on Home", () => {
    const placement = homeWidgets => ({
      "pageLayouts.variant": "spaces-thematic-v1",
      trainhopConfig: {
        spaces: {
          order: ["home", "widgets"],
          default: "home",
          widgetLibrary: "widgets",
          spaces: {
            home: { label: "Home", widgets: ["pictureOfTheDay"] },
            widgets: { label: "Widgets" },
          },
        },
      },
      "widgets.home": homeWidgets,
      "widgets.enabled": true,
      "widgets.crossword.enabled": true,
      "widgets.lists.enabled": true,
    });
    const homeWrites = store =>
      store.dispatch.mock.calls
        .map(([action]) => action)
        .filter(action => action.data?.name === "widgets.home")
        .map(action => action.data.value);

    it("keeps the toggles as on/off", async () => {
      const { container } = await renderPanel(
        {},
        allWidgetsVisible({
          ...placement('["crossword"]'),
          "widgets.clocks.enabled": false,
        })
      );
      expect(container.querySelector("#lists-toggle")).toHaveAttribute(
        "pressed"
      );
      expect(container.querySelector("#clocks-toggle")).not.toHaveAttribute(
        "pressed"
      );
    });

    // Otherwise a turned-off widget would hold one of Home's two slots.
    it("unpins a pinned widget it turns off", async () => {
      const { container, store } = await renderPanel(
        {},
        allWidgetsVisible(placement('["crossword"]'))
      );
      fireToggle(container.querySelector("#crossword-toggle"), false);
      expect(props.setPref).toHaveBeenCalledWith(
        "widgets.crossword.enabled",
        false
      );
      expect(homeWrites(store)).toEqual(["[]"]);
    });

    it("leaves the Home list alone for widgets that aren't pinned", async () => {
      const { container, store } = await renderPanel(
        {},
        allWidgetsVisible(placement('["crossword"]'))
      );
      fireToggle(container.querySelector("#lists-toggle"), false);
      fireToggle(container.querySelector("#clocks-toggle"), true);
      expect(homeWrites(store)).toEqual([]);
    });
  });

  it("calls togglePanel when the manage widgets button is clicked", async () => {
    const { container } = await renderPanel({ showPanel: false });
    fireEvent.click(container.querySelector("moz-box-button"));
    expect(props.togglePanel).toHaveBeenCalledTimes(1);
  });

  it("does not render the panel until showPanel is true", async () => {
    const { container } = await renderPanel({ showPanel: false });
    expect(
      container.querySelector(".widgets-mgmt-panel")
    ).not.toBeInTheDocument();
  });

  it("renders the panel with a title and a back button when showPanel is true", async () => {
    const { container } = await renderPanel();
    const panel = container.querySelector(".widgets-mgmt-panel");
    expect(panel).toBeInTheDocument();
    expect(panel.querySelectorAll("h2")).toHaveLength(1);
    expect(container.querySelector("moz-button.arrow-button")).toHaveAttribute(
      "data-l10n-id",
      "newtab-customize-panel-back-button"
    );
  });

  it("calls togglePanel when the back button is clicked", async () => {
    const { container } = await renderPanel();
    fireEvent.click(container.querySelector(".arrow-button"));
    expect(props.togglePanel).toHaveBeenCalledTimes(1);
  });

  describe("widget toggles", () => {
    it("renders no toggles when no widget is available", async () => {
      const { container } = await renderPanel();
      expect(toggleIds(container)).toEqual([]);
    });

    it("renders one toggle per available widget, A-Z by label", async () => {
      const { container } = await renderPanel({}, allWidgetsVisible());
      expect(toggleIds(container)).toEqual(
        EN_US_ORDER.map(id => `${id}-toggle`)
      );
    });

    it("disables the toggle of a widget whose pref is locked", async () => {
      const { container } = await renderPanel(
        {},
        allWidgetsVisible({ lockedPrefs: ["widgets.lists.enabled"] })
      );

      expect(container.querySelector("#lists-toggle")).toHaveAttribute(
        "disabled"
      );
      expect(container.querySelector("#clocks-toggle")).not.toHaveAttribute(
        "disabled"
      );
    });

    it.each(WIDGETS)(
      "labels the $id toggle with $l10nId and points it at $preference",
      async ({ id, l10nId, preference }) => {
        const { container } = await renderPanel({}, allWidgetsVisible());
        const toggle = container.querySelector(`#${id}-toggle`);
        expect(toggle).toHaveAttribute("data-l10n-id", l10nId);
        expect(toggle).toHaveAttribute("data-preference", preference);
      }
    );

    it.each(WIDGETS)(
      "drops the $id toggle when its system pref is off",
      async ({ id }) => {
        const widget = WIDGET_REGISTRY.find(w => w.id === id);
        const prefs = allWidgetsVisible();
        prefs[widget.systemEnabledPref] = false;

        const { container } = await renderPanel({}, prefs);

        expect(
          container.querySelector(`#${id}-toggle`)
        ).not.toBeInTheDocument();
        expect(toggleIds(container)).toHaveLength(WIDGETS.length - 1);
      }
    );

    it.each(WIDGETS)(
      "presses the $id toggle when $preference is set",
      async ({ id, preference }) => {
        const { container } = await renderPanel(
          {},
          allWidgetsVisible({ [preference]: true })
        );
        expect(container.querySelector(`#${id}-toggle`)).toHaveAttribute(
          "pressed"
        );
      }
    );

    it("leaves a toggle unpressed when its pref is off", async () => {
      const { container } = await renderPanel({}, allWidgetsVisible());
      expect(container.querySelector("#lists-toggle")).not.toHaveAttribute(
        "pressed"
      );
    });

    it("renders no toggle for the retired sports widget", async () => {
      const { container } = await renderPanel(
        {},
        allWidgetsVisible({ "widgets.system.sportsWidget.enabled": true })
      );
      expect(
        container.querySelector("#sportsWidget-toggle")
      ).not.toBeInTheDocument();
    });

    it("renders a widget added to the registry, with no panel change", async () => {
      const fixture = withFixtureWidget();

      const { container } = await renderPanel({}, allWidgetsVisible());

      expect(container.querySelector("#fixtureWidget-toggle")).toHaveAttribute(
        "data-l10n-id",
        fixture.customizeL10nId
      );
      expect(toggleIds(container)).toEqual(
        EN_US_ORDER_WITH_FIXTURE.map(id => `${id}-toggle`)
      );
    });

    it("labels an extension widget's toggle with its extension's name", async () => {
      withFixtureWidget({
        extensionID: "fixture-widget@mozilla.org",
        customizeL10nId: undefined,
      });

      const { container } = await renderPanel({}, allWidgetsVisible(), {
        ExtensionWidgets: {
          "fixture-widget@mozilla.org": { src: "", name: "Tally" },
        },
      });

      expect(container.querySelector("#fixtureWidget-toggle")).toHaveAttribute(
        "label",
        "Tally"
      );
    });

    it("skips a retired registry entry", async () => {
      withFixtureWidget({ retired: true });

      const { container } = await renderPanel(
        {},
        allWidgetsVisible({ "widgets.system.fixtureWidget.enabled": true })
      );

      expect(
        container.querySelector("#fixtureWidget-toggle")
      ).not.toBeInTheDocument();
    });

    describe("weather", () => {
      it("shows the weather toggle via its system pref and system.showWeather", async () => {
        const { container } = await renderPanel(
          {},
          { "widgets.system.weather.enabled": true, "system.showWeather": true }
        );
        expect(toggleIds(container)).toEqual(["weather-toggle"]);
      });

      it("shows the weather toggle via trainhopConfig.weather.enabled", async () => {
        const { container } = await renderPanel(
          {},
          {
            "widgets.system.weather.enabled": true,
            trainhopConfig: { weather: { enabled: true } },
          }
        );
        expect(toggleIds(container)).toEqual(["weather-toggle"]);
      });

      it("shows the weather toggle via widgetsSettings.weatherVisible", async () => {
        const { container } = await renderPanel(
          {},
          {
            "widgets.system.weather.enabled": true,
            trainhopConfig: { widgetsSettings: { weatherVisible: true } },
          }
        );
        expect(toggleIds(container)).toEqual(["weather-toggle"]);
      });

      it("hides the weather toggle when only its own system pref is set", async () => {
        const { container } = await renderPanel(
          {},
          { "widgets.system.weather.enabled": true }
        );
        expect(
          container.querySelector("#weather-toggle")
        ).not.toBeInTheDocument();
      });

      it("hides the weather toggle when only the legacy prefs are set", async () => {
        const { container } = await renderPanel(
          {},
          { "system.showWeather": true, showWeather: true }
        );
        expect(
          container.querySelector("#weather-toggle")
        ).not.toBeInTheDocument();
      });
    });

    it("orders the toggles A-Z by German label, umlaut included", async () => {
      labels = { ...GERMAN_PANEL_LABELS };
      withFixtureWidget();

      const { container } = await renderPanel({}, allWidgetsVisible());

      expect(toggleIds(container)).toEqual(
        GERMAN_ORDER.map(id => `${id}-toggle`)
      );
    });

    it("orders the toggles A-Z by Greek label, Weather among its peers", async () => {
      labels = { ...GREEK_PANEL_LABELS };

      const { container } = await renderPanel({}, allWidgetsVisible());

      expect(toggleIds(container)).toEqual(
        GREEK_ORDER.map(id => `${id}-toggle`)
      );
      expect(toggleIds(container).indexOf("weather-toggle")).toBe(4);
    });

    it("orders the toggles by Japanese label, Latin before kana before kanji", async () => {
      labels = { ...JAPANESE_PANEL_LABELS };

      const { container } = await renderPanel({}, allWidgetsVisible());

      expect(toggleIds(container)).toEqual(
        JAPANESE_ORDER.map(id => `${id}-toggle`)
      );
    });

    it("keeps a reduced set sorted when only some widgets are available", async () => {
      const prefs = allWidgetsVisible();
      for (const id of ["clocks", "crossword", "lists", "privacy", "stocks"]) {
        prefs[WIDGET_REGISTRY.find(w => w.id === id).systemEnabledPref] = false;
      }

      const { container } = await renderPanel({}, prefs);

      expect(toggleIds(container)).toEqual([
        "horoscopes-toggle",
        "pictureOfTheDay-toggle",
        "recentSearches-toggle",
        "solitaire-toggle",
        "focusTimer-toggle",
        "travel-toggle",
        "weather-toggle",
      ]);
    });

    it("keeps the same order whatever the user has switched on", async () => {
      const { container } = await renderPanel(
        {},
        allWidgetsVisible({
          "widgets.focusTimer.enabled": true,
          "widgets.weather.enabled": true,
        })
      );

      expect(toggleIds(container)).toEqual(
        EN_US_ORDER.map(id => `${id}-toggle`)
      );
    });

    it("gives two renders the same order", async () => {
      const first = await renderPanel({}, allWidgetsVisible());
      const second = await renderPanel({}, allWidgetsVisible());

      expect(toggleIds(first.container)).toEqual(
        EN_US_ORDER.map(id => `${id}-toggle`)
      );
      expect(toggleIds(second.container)).toEqual(toggleIds(first.container));
    });

    it("renders no toggles until the labels resolve", async () => {
      document.l10n.formatMessages = jest.fn(() => new Promise(() => {}));

      const { container } = await renderPanel({}, allWidgetsVisible());

      expect(toggleIds(container)).toEqual([]);
    });

    it("falls back to widget ids when document.l10n is absent", async () => {
      delete document.l10n;

      const { container } = await renderPanel({}, allWidgetsVisible());

      expect(toggleIds(container)).toEqual(ID_ORDER.map(id => `${id}-toggle`));
    });

    it("sorts a widget whose label has no message under its id", async () => {
      delete labels["newtab-custom-widget-timer-toggle"];

      const { container } = await renderPanel({}, allWidgetsVisible());

      expect(toggleIds(container)).toEqual(
        TIMER_ID_SORT_ORDER.map(id => `${id}-toggle`)
      );
    });
  });

  describe("toggling a widget", () => {
    it.each(WIDGETS)(
      "records $source and $widget_name and writes $preference for $id",
      async ({
        id,
        source,
        widget_name: widgetName,
        preference,
        widgetSize,
      }) => {
        const { container, store } = await renderPanel({}, allWidgetsVisible());

        fireToggle(container.querySelector(`#${id}-toggle`), true);

        expect(
          dispatchedAction(store, "TELEMETRY_USER_EVENT").data
        ).toMatchObject({
          event: "PREF_CHANGED",
          source,
          value: { status: true, menu_source: "CUSTOMIZE_MENU" },
        });
        expect(dispatchedAction(store, "WIDGETS_ENABLED").data).toMatchObject({
          widget_name: widgetName,
          widget_source: "customize_panel",
          enabled: true,
          widget_size: widgetSize,
        });
        expect(props.setPref).toHaveBeenCalledTimes(1);
        expect(props.setPref).toHaveBeenCalledWith(preference, true);
      }
    );

    it("reports enabled: false when a widget is switched off", async () => {
      const { container, store } = await renderPanel({}, allWidgetsVisible());

      fireToggle(container.querySelector("#weather-toggle"), false);

      expect(dispatchedAction(store, "WIDGETS_ENABLED").data).toMatchObject({
        widget_name: "weather",
        enabled: false,
      });
      expect(props.setPref).toHaveBeenCalledWith(
        "widgets.weather.enabled",
        false
      );
    });

    it("sends the registry default size when no size pref is set", async () => {
      const { container, store } = await renderPanel({}, allWidgetsVisible());

      fireToggle(container.querySelector("#weather-toggle"), true);

      expect(dispatchedAction(store, "WIDGETS_ENABLED").data.widget_size).toBe(
        "small"
      );
    });

    it("sends a user-set size pref", async () => {
      const { container, store } = await renderPanel(
        {},
        allWidgetsVisible({ "widgets.weather.size": "large" })
      );

      fireToggle(container.querySelector("#weather-toggle"), true);

      expect(dispatchedAction(store, "WIDGETS_ENABLED").data.widget_size).toBe(
        "large"
      );
    });

    it("sends a trainhop size suggestion when no size pref is set", async () => {
      const { container, store } = await renderPanel(
        {},
        allWidgetsVisible({
          trainhopConfig: { widgets: { weatherSize: "large" } },
        })
      );

      fireToggle(container.querySelector("#weather-toggle"), true);

      expect(dispatchedAction(store, "WIDGETS_ENABLED").data.widget_size).toBe(
        "large"
      );
    });

    it("records a widget added to the registry with its own source", async () => {
      const fixture = withFixtureWidget();
      const { container, store } = await renderPanel({}, allWidgetsVisible());

      fireToggle(container.querySelector("#fixtureWidget-toggle"), true);

      expect(dispatchedAction(store, "TELEMETRY_USER_EVENT").data.source).toBe(
        "WIDGET_FIXTURE"
      );
      expect(dispatchedAction(store, "WIDGETS_ENABLED").data).toMatchObject({
        widget_name: "fixture_widget",
        widget_size: "medium",
        enabled: true,
      });
      expect(props.setPref).toHaveBeenCalledWith(fixture.enabledPref, true);
    });

    it("stops the toggle event before it reaches the outer Widgets toggle", async () => {
      const { container } = await renderPanel({}, allWidgetsVisible());
      const outerHandler = jest.fn();
      container.addEventListener("toggle", outerHandler);

      fireToggle(container.querySelector("#clocks-toggle"), false);

      expect(outerHandler).not.toHaveBeenCalled();
    });
  });
});
