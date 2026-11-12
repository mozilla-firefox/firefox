/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  AboutPreferences,
  PREFERENCES_LOADED_EVENT,
  PREFERENCES_LOADED_EVENT_SUBPANE,
} from "lib/AboutPreferences.sys.mjs";
import { actionTypes as at } from "common/Actions.mjs";
import { WIDGET_REGISTRY } from "common/WidgetsRegistry.mjs";
import { CUSTOMIZE_PANEL_SOURCES } from "content-src/lib/constants";
import { mockServices, stubGlobals } from "test/jest/test-utils";

describe("AboutPreferences Feed", () => {
  let restoreGlobals;
  let fakeServices;
  let instance;

  // sinon's `.withArgs(pref, false).returns(value)` becomes a lookup table:
  // any pref that isn't listed reads back as undefined.
  const setBoolPrefs = prefs => {
    fakeServices.prefs.getBoolPref.mockImplementation(pref => prefs[pref]);
  };

  const PREF_BRANCH = "browser.newtabpage.activity-stream.";

  beforeEach(() => {
    instance = new AboutPreferences();
    instance.store = {
      getState: () => ({}),
    };
    fakeServices = mockServices(["obs", "prefs"]);
    restoreGlobals = stubGlobals({
      Services: fakeServices,
    });
  });
  afterEach(() => {
    restoreGlobals();
    jest.restoreAllMocks();
  });

  describe("#onAction", () => {
    it("should call .init() on an INIT action", () => {
      const stub = jest.spyOn(instance, "init").mockImplementation(() => {});

      instance.onAction({ type: at.INIT });

      expect(stub).toHaveBeenCalledTimes(1);
    });
    it("should call .uninit() on an UNINIT action", () => {
      const stub = jest.spyOn(instance, "uninit").mockImplementation(() => {});

      instance.onAction({ type: at.UNINIT });

      expect(stub).toHaveBeenCalledTimes(1);
    });
    it("should call .openPreferences on SETTINGS_OPEN", () => {
      const action = {
        type: at.SETTINGS_OPEN,
        _target: {
          window: { openPreferences: jest.fn() },
        },
      };
      instance.onAction(action);
      expect(action._target.window.openPreferences).toHaveBeenCalledTimes(1);
    });
    it("should call .BrowserAddonUI.openAddonsMgr with the extension id on OPEN_WEBEXT_SETTINGS", () => {
      const action = {
        type: at.OPEN_WEBEXT_SETTINGS,
        data: "foo",
        _target: {
          window: {
            BrowserAddonUI: { openAddonsMgr: jest.fn() },
          },
        },
      };
      instance.onAction(action);
      expect(
        action._target.window.BrowserAddonUI.openAddonsMgr
      ).toHaveBeenCalledWith("addons://detail/foo");
    });
  });

  describe("#observe", () => {
    let restoreObserveGlobals;
    let registerGroups;
    let getSettingGroup;
    let insertFTLIfNeeded;

    beforeEach(() => {
      registerGroups = jest.fn();
      let homeCalls = 0;
      getSettingGroup = jest.fn(group => {
        if (group !== "home") {
          return undefined;
        }
        homeCalls += 1;
        if (homeCalls === 1) {
          throw new Error("Not yet registered");
        }
        return true;
      });
      insertFTLIfNeeded = jest.fn();
      restoreObserveGlobals = stubGlobals({
        SettingGroupManager: {
          registerGroups,
          get: getSettingGroup,
        },
        MozXULElement: { insertFTLIfNeeded },
      });
      jest.spyOn(instance, "_registerPreferences").mockImplementation();
      jest.spyOn(instance, "_setupHomeGroup").mockReturnValue({});
    });

    afterEach(() => {
      restoreObserveGlobals();
    });

    it("should watch for about:preferences loading", () => {
      instance.init();

      expect(fakeServices.obs.addObserver).toHaveBeenCalledTimes(2);
      expect(fakeServices.obs.addObserver).toHaveBeenCalledWith(
        instance,
        PREFERENCES_LOADED_EVENT
      );
      expect(fakeServices.obs.addObserver).toHaveBeenCalledWith(
        instance,
        PREFERENCES_LOADED_EVENT_SUBPANE
      );
    });
    it("should stop watching on uninit", () => {
      instance.uninit();

      expect(fakeServices.obs.removeObserver).toHaveBeenCalledTimes(2);
      expect(fakeServices.obs.removeObserver).toHaveBeenCalledWith(
        instance,
        PREFERENCES_LOADED_EVENT
      );
      expect(fakeServices.obs.removeObserver).toHaveBeenCalledWith(
        instance,
        PREFERENCES_LOADED_EVENT_SUBPANE
      );
    });
    it("should register newtab.ftl with the preferences document", () => {
      instance.observe(window);

      expect(insertFTLIfNeeded).toHaveBeenCalledWith(
        "browser/newtab/newtab.ftl"
      );
    });

    it("should call registerGroups with home only", async () => {
      // homepage/customHomepage are owned by components/preferences.
      await instance.observe(window);

      expect(registerGroups).toHaveBeenCalledTimes(1);
      const [[groups]] = registerGroups.mock.calls;
      expect(Object.keys(groups)).toEqual(["home"]);
      expect(groups).not.toHaveProperty("homepage");
      expect(groups).not.toHaveProperty("customHomepage");
    });

    it("should not register a second time when observe fires again for the same window", async () => {
      await instance.observe(window, PREFERENCES_LOADED_EVENT);
      await instance.observe(window, PREFERENCES_LOADED_EVENT_SUBPANE);

      expect(instance._registerPreferences).toHaveBeenCalledTimes(1);
      expect(registerGroups).toHaveBeenCalledTimes(1);
    });
  });

  describe("#_registerPreferences", () => {
    it("should call Preferences.addAll once with all pref ids", () => {
      const addAll = jest.fn();

      instance._registerPreferences({ Preferences: { addAll } });

      expect(addAll).toHaveBeenCalledTimes(1);
      // Spot-check prefs from the beginning, middle, and end of the list.
      const [[prefs]] = addAll.mock.calls;
      expect(Array.isArray(prefs)).toBe(true);
      expect(
        prefs.some(
          p => p.id === "browser.newtabpage.activity-stream.showSearch"
        )
      ).toBe(true);
      expect(
        prefs.some(
          p => p.id === "browser.newtabpage.activity-stream.feeds.topsites"
        )
      ).toBe(true);
      expect(
        prefs.some(
          p =>
            p.id ===
            "browser.newtabpage.activity-stream.section.highlights.includeVisited"
        )
      ).toBe(true);
      expect(
        prefs.some(
          p =>
            p.id === "browser.newtabpage.activity-stream.hideLogo" &&
            p.type === "bool" &&
            p.inverted === true
        )
      ).toBe(true);
    });

    it("registers Weather's widget prefs and no other widget's", () => {
      const addAll = jest.fn();

      instance._registerPreferences({ Preferences: { addAll } });

      const [[prefs]] = addAll.mock.calls;
      expect(prefs).toContainEqual({
        id: `${PREF_BRANCH}widgets.system.weather.enabled`,
        type: "bool",
      });
      expect(prefs).toContainEqual({
        id: `${PREF_BRANCH}widgets.weather.enabled`,
        type: "bool",
      });
      for (const widget of WIDGET_REGISTRY.filter(w => w.id !== "weather")) {
        expect(prefs.some(p => p.id === PREF_BRANCH + widget.enabledPref)).toBe(
          false
        );
        expect(
          prefs.some(p => p.id === PREF_BRANCH + widget.systemEnabledPref)
        ).toBe(false);
      }
    });

    it("registers every pref id once", () => {
      const addAll = jest.fn();

      instance._registerPreferences({ Preferences: { addAll } });

      const [[prefs]] = addAll.mock.calls;
      const ids = prefs.map(p => p.id);
      expect(new Set(ids).size).toBe(ids.length);
    });
  });

  describe("#_setupHomeGroup", () => {
    let addSetting;
    let Preferences;

    beforeEach(() => {
      addSetting = jest.fn();
      Preferences = { addSetting };
    });

    const findSetting = id => {
      const call = addSetting.mock.calls.find(([s]) => s.id === id);
      if (!call) {
        throw new Error(`no setting registered with id "${id}"`);
      }
      return call[0];
    };

    it("should register weather against showWeather prefs when Nova is disabled", () => {
      setBoolPrefs({
        "browser.newtabpage.activity-stream.nova.enabled": false,
      });

      instance._setupHomeGroup({ Preferences });

      const calls = addSetting.mock.calls.map(([{ id, pref }]) => ({
        id,
        pref,
      }));
      expect(
        calls.some(
          c =>
            c.id === "weatherStandalone" &&
            c.pref === "browser.newtabpage.activity-stream.showWeather"
        )
      ).toBe(true);
    });

    it("should register weather against widgets.weather.enabled when Nova is enabled", () => {
      setBoolPrefs({ "browser.newtabpage.activity-stream.nova.enabled": true });

      instance._setupHomeGroup({ Preferences });

      const calls = addSetting.mock.calls.map(([{ id, pref }]) => ({
        id,
        pref,
      }));
      expect(
        calls.some(
          c =>
            c.id === "weatherStandalone" &&
            c.pref ===
              "browser.newtabpage.activity-stream.widgets.weather.enabled"
        )
      ).toBe(true);
      expect(
        calls.some(
          c => c.pref === "browser.newtabpage.activity-stream.showWeather"
        )
      ).toBe(false);
      expect(findSetting("weatherStandalone").deps).toEqual([
        "weatherEnabled",
        "homepageNewWindows",
        "homepageNewTabs",
      ]);
    });

    it("shows the widgets group when the container is enabled via trainhopConfig even if the system pref is off", () => {
      fakeServices.prefs.getBoolPref.mockReturnValue(false);
      instance.store.getState = () => ({
        Prefs: {
          values: {
            region: "US",
            trainhopConfig: { widgets: { enabled: true } },
          },
        },
      });

      instance._setupHomeGroup({ Preferences });

      expect(
        findSetting("widgets").visible({ widgetsEnabled: { value: false } })
      ).toBe(true);
    });

    it("shows the widgets group when the container system pref is on (read live from deps)", () => {
      fakeServices.prefs.getBoolPref.mockReturnValue(false);
      instance.store.getState = () => ({ Prefs: { values: {} } });

      instance._setupHomeGroup({ Preferences });

      expect(
        findSetting("widgets").visible({ widgetsEnabled: { value: true } })
      ).toBe(true);
    });

    it("hides the widgets group when neither the system pref nor trainhopConfig enable the container", () => {
      fakeServices.prefs.getBoolPref.mockReturnValue(false);
      instance.store.getState = () => ({ Prefs: { values: {} } });

      instance._setupHomeGroup({ Preferences });

      expect(
        findSetting("widgets").visible({ widgetsEnabled: { value: false } })
      ).toBe(false);
    });

    it("shows the widgets group when revealed via widgetsSettings even if the system pref is off", () => {
      fakeServices.prefs.getBoolPref.mockReturnValue(false);
      instance.store.getState = () => ({
        Prefs: {
          values: {
            region: "US",
            trainhopConfig: { widgetsSettings: { enabled: true } },
          },
        },
      });

      instance._setupHomeGroup({ Preferences });

      expect(
        findSetting("widgets").visible({ widgetsEnabled: { value: false } })
      ).toBe(true);
    });

    it("keeps the weather toggle standalone when Nova is enabled but the widgets system pref is off", () => {
      setBoolPrefs({ "browser.newtabpage.activity-stream.nova.enabled": true });
      instance.store.getState = () => ({
        Prefs: {
          values: {
            "widgets.system.enabled": false,
            "widgets.system.weather.enabled": true,
          },
        },
      });

      const group = instance._setupHomeGroup({ Preferences });

      expect(group.items.some(i => i.id === "weatherStandalone")).toBe(true);
      expect(
        group.items.find(i => i.id === "widgets").subcategory
      ).toBeUndefined();
    });

    it("keeps the weather toggle as a standalone row when Nova is disabled", () => {
      setBoolPrefs({
        "browser.newtabpage.activity-stream.nova.enabled": false,
      });

      const group = instance._setupHomeGroup({ Preferences });

      const standaloneWeather = group.items.find(
        i => i.id === "weatherStandalone"
      );
      expect(standaloneWeather).toBeDefined();
      // Standalone, Weather is a top-level toggle like the other rows.
      expect(standaloneWeather.control).toBe("moz-toggle");
      expect(
        group.items.find(i => i.id === "widgets").subcategory
      ).toBeUndefined();
    });

    it("registers every setting id once with Nova on and off", () => {
      for (const novaEnabled of [false, true]) {
        setBoolPrefs({
          "browser.newtabpage.activity-stream.nova.enabled": novaEnabled,
        });
        addSetting.mockClear();

        instance._setupHomeGroup({ Preferences });

        const ids = addSetting.mock.calls.map(([s]) => s.id);
        expect(new Set(ids).size).toBe(ids.length);
      }
    });

    it("labels standalone Weather with the settings Weather string", () => {
      setBoolPrefs({
        "browser.newtabpage.activity-stream.nova.enabled": false,
      });

      const group = instance._setupHomeGroup({ Preferences });

      const standalone = group.items.find(i => i.id === "weatherStandalone");
      expect(standalone.l10nId).toBe("home-prefs-weather-header-srd");
      expect(standalone.subcategory).toBe("weather");
    });

    it.each([true, false])(
      "disables the widgets group and standalone weather when Firefox Home is not in use (Nova %s)",
      novaEnabled => {
        setBoolPrefs({
          "browser.newtabpage.activity-stream.nova.enabled": novaEnabled,
        });
        instance.store.getState = () => ({
          Prefs: { values: { "widgets.system.enabled": false } },
        });

        instance._setupHomeGroup({ Preferences });

        const homeOff = {
          homepageNewWindows: { value: "blank" },
          homepageNewTabs: { value: "blank" },
        };
        const homeOn = {
          homepageNewWindows: { value: "home" },
          homepageNewTabs: { value: "blank" },
        };
        expect(findSetting("widgets").disabled(homeOff)).toBe(true);
        expect(findSetting("widgets").disabled(homeOn)).toBe(false);
        expect(findSetting("weatherStandalone").disabled(homeOff)).toBe(true);
        expect(findSetting("weatherStandalone").disabled(homeOn)).toBe(false);
      }
    );

    it("lists the rows in the Figma order with Nova on", () => {
      setBoolPrefs({ "browser.newtabpage.activity-stream.nova.enabled": true });
      instance.store.getState = () => ({
        Prefs: { values: { "widgets.system.enabled": true } },
      });

      const group = instance._setupHomeGroup({ Preferences });

      expect(group.items.map(i => i.id)).toEqual([
        "firefoxHomeDisabledNotice",
        "chooseWallpaper",
        "firefoxLogo",
        "webSearch",
        "shortcuts",
        "widgets",
        "stories",
        "recentActivity",
        "supportFirefox",
      ]);
    });

    it("puts standalone Weather in the Widgets slot when the container is hidden", () => {
      setBoolPrefs({
        "browser.newtabpage.activity-stream.nova.enabled": false,
      });

      const group = instance._setupHomeGroup({ Preferences });

      expect(group.items.map(i => i.id)).toEqual([
        "firefoxHomeDisabledNotice",
        "chooseWallpaper",
        "webSearch",
        "shortcuts",
        "widgets",
        "weatherStandalone",
        "stories",
        "recentActivity",
        "supportFirefox",
      ]);
    });

    it("manages Weather with the other widgets when Nova is on and the container is visible", () => {
      setBoolPrefs({ "browser.newtabpage.activity-stream.nova.enabled": true });
      instance.store.getState = () => ({
        Prefs: { values: { "widgets.system.enabled": true } },
      });

      const group = instance._setupHomeGroup({ Preferences });

      expect(
        group.items.find(i => i.id === "weatherStandalone")
      ).toBeUndefined();
      // Keeps about:preferences#home-weather resolving.
      expect(group.items.find(i => i.id === "widgets").subcategory).toBe(
        "weather"
      );
    });

    it("keeps the standalone Weather row with Nova off even when the container is visible", () => {
      setBoolPrefs({
        "browser.newtabpage.activity-stream.nova.enabled": false,
      });
      instance.store.getState = () => ({
        Prefs: { values: { "widgets.system.enabled": true } },
      });

      const group = instance._setupHomeGroup({ Preferences });

      expect(
        group.items.find(i => i.id === "weatherStandalone").subcategory
      ).toBe("weather");
      expect(
        group.items.find(i => i.id === "widgets").subcategory
      ).toBeUndefined();
    });

    it("manages Weather with the other widgets when only trainhopConfig shows the container", () => {
      setBoolPrefs({ "browser.newtabpage.activity-stream.nova.enabled": true });
      instance.store.getState = () => ({
        Prefs: {
          values: {
            region: "US",
            trainhopConfig: { widgets: { enabled: true } },
          },
        },
      });

      const group = instance._setupHomeGroup({ Preferences });

      expect(
        group.items.find(i => i.id === "weatherStandalone")
      ).toBeUndefined();
    });

    it("shows standalone Weather when its system pref is on (read live from deps)", () => {
      setBoolPrefs({ "browser.newtabpage.activity-stream.nova.enabled": true });
      instance.store.getState = () => ({ Prefs: { values: {} } });

      instance._setupHomeGroup({ Preferences });

      const { visible } = findSetting("weatherStandalone");
      expect(visible({ weatherEnabled: { value: true } })).toBe(true);
      expect(visible({ weatherEnabled: { value: false } })).toBe(false);
    });

    it("shows standalone Weather when trainhopConfig enables it with the system pref off", () => {
      setBoolPrefs({ "browser.newtabpage.activity-stream.nova.enabled": true });
      instance.store.getState = () => ({
        Prefs: {
          values: {
            region: "US",
            trainhopConfig: { widgets: { weatherEnabled: true } },
          },
        },
      });

      instance._setupHomeGroup({ Preferences });

      expect(
        findSetting("weatherStandalone").visible({
          weatherEnabled: { value: false },
        })
      ).toBe(true);
    });

    it("nests only a Manage widgets link under Widgets", () => {
      setBoolPrefs({ "browser.newtabpage.activity-stream.nova.enabled": true });
      instance.store.getState = () => ({
        Prefs: { values: { "widgets.system.enabled": true } },
      });

      const group = instance._setupHomeGroup({ Preferences });

      expect(group.items.find(i => i.id === "widgets").items).toEqual([
        {
          id: "manageWidgets",
          l10nId: "home-prefs-manage-widgets-link",
          control: "moz-box-link",
          controlAttrs: {
            href: "about:home#customize-widgets?source=settings",
          },
        },
      ]);
    });

    it("marks the Firefox Home links with the settings source", () => {
      setBoolPrefs({ "browser.newtabpage.activity-stream.nova.enabled": true });
      instance.store.getState = () => ({
        Prefs: { values: { "widgets.system.enabled": true } },
      });

      const group = instance._setupHomeGroup({ Preferences });

      const links = group.items.flatMap(i => [i, ...(i.items ?? [])]);
      for (const id of ["chooseWallpaper", "manageWidgets", "manageTopics"]) {
        const { href } = links.find(i => i.id === id).controlAttrs;
        expect(href).toMatch(
          new RegExp(`\\?source=${CUSTOMIZE_PANEL_SOURCES.SETTINGS}$`)
        );
      }
    });

    it("shows Manage widgets only while Firefox Home is in use and Widgets is on", () => {
      fakeServices.prefs.getBoolPref.mockReturnValue(false);

      instance._setupHomeGroup({ Preferences });

      const { visible } = findSetting("manageWidgets");
      const homeOn = {
        homepageNewWindows: { value: "home" },
        homepageNewTabs: { value: "blank" },
      };
      const homeOff = {
        homepageNewWindows: { value: "blank" },
        homepageNewTabs: { value: "blank" },
      };
      expect(visible({ ...homeOn, widgets: { value: true } })).toBe(true);
      expect(visible({ ...homeOn, widgets: { value: false } })).toBe(false);
      expect(visible({ ...homeOff, widgets: { value: true } })).toBe(false);
    });

    it("opens Manage widgets in a tab or a window like the other Firefox Home links", () => {
      fakeServices.prefs.getBoolPref.mockReturnValue(false);
      const openTrustedLinkIn = jest.fn();

      instance._setupHomeGroup({ Preferences, openTrustedLinkIn });

      const { onUserClick } = findSetting("manageWidgets");
      const preventDefault = jest.fn();
      onUserClick({ preventDefault }, { homepageNewTabs: { value: "home" } });
      expect(preventDefault).toHaveBeenCalled();
      expect(openTrustedLinkIn).toHaveBeenLastCalledWith(
        "about:home#customize-widgets?source=settings",
        "tab"
      );
      onUserClick({ preventDefault }, { homepageNewTabs: { value: "blank" } });
      expect(openTrustedLinkIn).toHaveBeenLastCalledWith(
        "about:home#customize-widgets?source=settings",
        "window"
      );
    });

    it("registers no setting for any widget other than Weather", () => {
      for (const novaEnabled of [false, true]) {
        setBoolPrefs({
          "browser.newtabpage.activity-stream.nova.enabled": novaEnabled,
        });
        addSetting.mockClear();

        instance._setupHomeGroup({ Preferences });

        const ids = addSetting.mock.calls.map(([s]) => s.id);
        for (const widget of WIDGET_REGISTRY.filter(w => w.id !== "weather")) {
          expect(ids).not.toContain(widget.id);
          expect(ids).not.toContain(widget.trainhopEnabledKey);
        }
      }
    });
  });
});
