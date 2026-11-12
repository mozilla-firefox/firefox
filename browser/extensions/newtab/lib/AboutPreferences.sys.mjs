/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import {Store} from "resource://newtab/lib/Store.sys.mjs"
 */

import { actionTypes as at } from "resource://newtab/common/Actions.mjs";
import {
  WIDGET_REGISTRY,
  PREF_WIDGETS_SYSTEM_WEATHER_ENABLED,
  PREF_WIDGETS_WEATHER_ENABLED,
  isWidgetToggleVisible,
  isWidgetsContainerVisible,
} from "resource://newtab/common/WidgetsRegistry.mjs";

const ACTIVITY_STREAM_PREF_BRANCH = "browser.newtabpage.activity-stream.";

export const PREFERENCES_LOADED_EVENT = "home-pane-loaded";
export const PREFERENCES_LOADED_EVENT_SUBPANE = "customHomepage-pane-loaded";

export class AboutPreferences {
  /** @type {Store} */
  store = null;

  init() {
    Services.obs.addObserver(this, PREFERENCES_LOADED_EVENT);
    Services.obs.addObserver(this, PREFERENCES_LOADED_EVENT_SUBPANE);
  }

  uninit() {
    Services.obs.removeObserver(this, PREFERENCES_LOADED_EVENT);
    Services.obs.removeObserver(this, PREFERENCES_LOADED_EVENT_SUBPANE);
  }

  onAction(action) {
    switch (action.type) {
      case at.INIT:
        this.init();
        break;
      case at.UNINIT:
        this.uninit();
        break;
      case at.SETTINGS_OPEN:
        action._target.window.openPreferences("paneHome");
        break;
      // This is used to open the web extension settings page for an extension
      case at.OPEN_WEBEXT_SETTINGS:
        action._target.window.BrowserAddonUI.openAddonsMgr(
          `addons://detail/${encodeURIComponent(action.data)}`
        );
        break;
      // Open the about:addons themes list (from the New Tab theme picker).
      case at.OPEN_ABOUT_ADDONS_THEMES:
        action._target.window.BrowserAddonUI.openAddonsMgr(
          "addons://list/theme"
        );
        break;
    }
  }

  observe(window) {
    const { SettingGroupManager } = window;

    window.MozXULElement.insertFTLIfNeeded("browser/newtab/newtab.ftl");

    // newtab listens for home-pane-loaded because it owns the `home` group
    // (Firefox Home content). The `homepage` and `customHomepage` groups are
    // registered by components/preferences, not here.

    // We observe 2 signals that about:settings is loading - the
    // PREFERENCES_LOADED_EVENT and PREFERENCES_LOADED_EVENT_SUBPANE
    // observer notifications. The first is fired anytime about:settings
    // is loaded directly. The second (and not the first) fires if loading
    // about:preferences#customHomepage. We handle those cases by observing
    // both, and checking to see if the "home" settings group was already
    // registered. If so, we take that as a sign that we don't need to
    // re-register and we return early.
    try {
      if (SettingGroupManager.get("home")) {
        // The home group has already been registered for this load of
        // about:settings, so no need to do it again. Return early.
        return;
      }
    } catch (e) {
      // We didn't find the home settings group registered. That's okay,
      // we'll register the group(s) now - that's what we're here for.
    }

    this._registerPreferences(window);

    SettingGroupManager.registerGroups({
      home: this._setupHomeGroup(window),
    });
  }

  /** @param {Window} window */
  _registerPreferences(window) {
    const { Preferences } = window;

    Preferences.addAll([
      { id: "browser.newtabpage.activity-stream.showSearch", type: "bool" },
      {
        id: "browser.newtabpage.activity-stream.hideLogo",
        type: "bool",
        inverted: true,
      },
      {
        id: "browser.newtabpage.activity-stream.system.showWeather",
        type: "bool",
      },
      { id: "browser.newtabpage.activity-stream.showWeather", type: "bool" },
      {
        id: "browser.newtabpage.activity-stream.widgets.system.enabled",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.widgets.enabled",
        type: "bool",
      },
      {
        id: ACTIVITY_STREAM_PREF_BRANCH + PREF_WIDGETS_SYSTEM_WEATHER_ENABLED,
        type: "bool",
      },
      {
        id: ACTIVITY_STREAM_PREF_BRANCH + PREF_WIDGETS_WEATHER_ENABLED,
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.feeds.topsites",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.topSitesRows",
        type: "int",
      },
      {
        id: "browser.newtabpage.activity-stream.feeds.system.topstories",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.feeds.section.topstories",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.discoverystream.sections.enabled",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.discoverystream.topicLabels.enabled",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.discoverystream.sections.personalization.enabled",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.discoverystream.sections.customizeMenuPanel.enabled",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.showSponsoredCheckboxes",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.showSponsoredTopSites",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.showSponsored",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.feeds.section.highlights",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.section.highlights.rows",
        type: "int",
      },
      {
        id: "browser.newtabpage.activity-stream.section.highlights.includeVisited",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.section.highlights.includeBookmarks",
        type: "bool",
      },
      {
        id: "browser.newtabpage.activity-stream.section.highlights.includeDownloads",
        type: "bool",
      },
    ]);
  }

  /** @param {Window} window */
  // eslint-disable-next-line max-statements
  _setupHomeGroup(window) {
    const { Preferences } = window;

    const widgetPrefs = this.store.getState()?.Prefs?.values ?? {};

    // The Firefox Home section should be disabled when neither "New windows"
    // nor "New tabs" is set to Firefox Home.
    const firefoxHomeDeps = ["homepageNewWindows", "homepageNewTabs"];
    const firefoxHomeActive = ({ homepageNewWindows, homepageNewTabs }) =>
      homepageNewWindows.value === "home" || homepageNewTabs.value === "home";

    // A literal because this parent-process module can't import content-src,
    // where Base.jsx checks it against CUSTOMIZE_PANEL_SOURCES.SETTINGS.
    const SETTINGS_MARKER = "?source=settings";
    const HOME_CUSTOMIZE_URL = `about:home#customize${SETTINGS_MARKER}`;
    const HOME_CUSTOMIZE_TOPICS_URL = `about:home#customize-topics${SETTINGS_MARKER}`;
    const HOME_CUSTOMIZE_WIDGETS_URL = `about:home#customize-widgets${SETTINGS_MARKER}`;

    // Open in a new tab if "New tabs" is Firefox Home, else a new window.
    const dispatchForHomeLink = ({ homepageNewTabs }) =>
      homepageNewTabs.value === "home" ? "tab" : "window";

    Preferences.addSetting({
      id: "firefoxHomeDisabledNotice",
      deps: firefoxHomeDeps,
      visible: deps => !firefoxHomeActive(deps),
    });

    // @nova-cleanup(remove-conditional): Remove this lookup and inline `true` at every novaEnabled check below.
    const novaEnabled = Services.prefs.getBoolPref(
      "browser.newtabpage.activity-stream.nova.enabled",
      false
    );

    // Read once at page load. With Nova, Weather is one of the widgets, so
    // while the Widgets container is shown it is managed on New Tab and gets
    // no row here. Without Nova it is not a widget and keeps its own row.
    const weatherNested = novaEnabled && isWidgetsContainerVisible(widgetPrefs);

    // hideLogo only affects rendering when Nova is enabled (see Base.jsx),
    // so the toggle is registered only in that branch.
    if (novaEnabled) {
      Preferences.addSetting({
        id: "firefoxLogo",
        pref: "browser.newtabpage.activity-stream.hideLogo",
        deps: firefoxHomeDeps,
        disabled: deps => !firefoxHomeActive(deps),
      });
    }

    // Search
    Preferences.addSetting({
      id: "webSearch",
      pref: "browser.newtabpage.activity-stream.showSearch",
      deps: firefoxHomeDeps,
      disabled: deps => !firefoxHomeActive(deps),
    });

    // Widgets: general
    Preferences.addSetting({
      id: "widgetsEnabled",
      pref: "browser.newtabpage.activity-stream.widgets.system.enabled",
    });

    Preferences.addSetting({
      id: "widgets",
      pref: "browser.newtabpage.activity-stream.widgets.enabled",
      deps: ["widgetsEnabled", ...firefoxHomeDeps],
      visible: ({ widgetsEnabled }) =>
        isWidgetsContainerVisible({
          ...widgetPrefs,
          "widgets.system.enabled": widgetsEnabled.value,
        }),
      disabled: deps => !firefoxHomeActive(deps),
    });

    // moz-box-link has no disabled state, so the link is hidden while Widgets
    // is off. Otherwise it would stay clickable under the off toggle.
    Preferences.addSetting({
      id: "manageWidgets",
      deps: ["widgets", ...firefoxHomeDeps],
      visible: deps => firefoxHomeActive(deps) && deps.widgets.value,
      onUserClick: (e, deps) => {
        e.preventDefault();
        window.openTrustedLinkIn(
          HOME_CUSTOMIZE_WIDGETS_URL,
          dispatchForHomeLink(deps)
        );
      },
    });

    // Weather's own top-level row, used when the Widgets container is hidden.
    // @nova-cleanup(remove-conditional): Remove novaEnabled check and else branch; keep only the Nova registration block.
    if (novaEnabled) {
      const weatherWidget = WIDGET_REGISTRY.find(w => w.id === "weather");
      Preferences.addSetting({
        id: weatherWidget.trainhopEnabledKey,
        pref: ACTIVITY_STREAM_PREF_BRANCH + weatherWidget.systemEnabledPref,
      });
      Preferences.addSetting({
        id: "weatherStandalone",
        pref: ACTIVITY_STREAM_PREF_BRANCH + weatherWidget.enabledPref,
        deps: [weatherWidget.trainhopEnabledKey, ...firefoxHomeDeps],
        // Shown when the system pref or a trainhop (Nimbus) config enables
        // Weather. The system pref comes from the live setting value, so the
        // row reacts without a page refresh. trainhopConfig is read at page
        // load only.
        visible: deps =>
          isWidgetToggleVisible(weatherWidget, {
            ...widgetPrefs,
            [weatherWidget.systemEnabledPref]:
              deps[weatherWidget.trainhopEnabledKey]?.value,
          }),
        disabled: deps => !firefoxHomeActive(deps),
      });
    } else {
      Preferences.addSetting({
        id: "showWeather",
        pref: "browser.newtabpage.activity-stream.system.showWeather",
      });

      Preferences.addSetting({
        id: "weatherStandalone",
        pref: "browser.newtabpage.activity-stream.showWeather",
        deps: ["showWeather", ...firefoxHomeDeps],
        visible: ({ showWeather }) => showWeather.value,
        disabled: deps => !firefoxHomeActive(deps),
      });
    }

    // Shortcuts
    Preferences.addSetting({
      id: "shortcuts",
      pref: "browser.newtabpage.activity-stream.feeds.topsites",
      deps: firefoxHomeDeps,
      disabled: deps => !firefoxHomeActive(deps),
    });
    Preferences.addSetting({
      id: "shortcutsRows",
      pref: "browser.newtabpage.activity-stream.topSitesRows",
    });

    // Dependency prefs for stories & sponsored stories visibility
    Preferences.addSetting({
      id: "systemTopstories",
      pref: "browser.newtabpage.activity-stream.feeds.system.topstories",
    });

    // Stories
    Preferences.addSetting({
      id: "stories",
      pref: "browser.newtabpage.activity-stream.feeds.section.topstories",
      deps: ["systemTopstories", ...firefoxHomeDeps],
      visible: ({ systemTopstories }) => systemTopstories.value,
      disabled: deps => !firefoxHomeActive(deps),
    });

    // Dependencies for "manage topics" checkbox
    Preferences.addSetting({
      id: "sectionsEnabled",
      pref: "browser.newtabpage.activity-stream.discoverystream.sections.enabled",
    });
    Preferences.addSetting({
      id: "topicLabelsEnabled",
      pref: "browser.newtabpage.activity-stream.discoverystream.topicLabels.enabled",
    });
    Preferences.addSetting({
      id: "sectionsPersonalizationEnabled",
      pref: "browser.newtabpage.activity-stream.discoverystream.sections.personalization.enabled",
    });
    Preferences.addSetting({
      id: "sectionsCustomizeMenuPanelEnabled",
      pref: "browser.newtabpage.activity-stream.discoverystream.sections.customizeMenuPanel.enabled",
    });

    Preferences.addSetting({
      id: "manageTopics",
      deps: [
        "sectionsEnabled",
        "sectionsPersonalizationEnabled",
        "sectionsCustomizeMenuPanelEnabled",
        "stories",
        ...firefoxHomeDeps,
      ],
      visible: deps => {
        const {
          sectionsEnabled,
          sectionsPersonalizationEnabled,
          sectionsCustomizeMenuPanelEnabled,
          stories,
        } = deps;
        return (
          firefoxHomeActive(deps) &&
          sectionsEnabled.value &&
          sectionsPersonalizationEnabled.value &&
          sectionsCustomizeMenuPanelEnabled.value &&
          stories.value
        );
      },
      onUserClick: (e, deps) => {
        e.preventDefault();
        window.openTrustedLinkIn(
          HOME_CUSTOMIZE_TOPICS_URL,
          dispatchForHomeLink(deps)
        );
      },
    });

    // Support Firefox: sponsored content
    Preferences.addSetting({
      id: "supportFirefox",
      pref: "browser.newtabpage.activity-stream.showSponsoredCheckboxes",
      deps: ["sponsoredShortcuts", "sponsoredStories", ...firefoxHomeDeps],
      disabled: deps => !firefoxHomeActive(deps),
      onUserChange(value, { sponsoredShortcuts, sponsoredStories }) {
        // When supportFirefox changes, automatically update child preferences to match
        sponsoredShortcuts.value = !!value;
        sponsoredStories.value = !!value;
      },
    });
    Preferences.addSetting({
      id: "topsitesEnabled",
      pref: "browser.newtabpage.activity-stream.feeds.topsites",
    });
    Preferences.addSetting({
      id: "sponsoredShortcuts",
      pref: "browser.newtabpage.activity-stream.showSponsoredTopSites",
      deps: ["topsitesEnabled"],
      disabled: ({ topsitesEnabled }) => !topsitesEnabled.value,
    });
    Preferences.addSetting({
      id: "sponsoredStories",
      pref: "browser.newtabpage.activity-stream.showSponsored",
      deps: ["systemTopstories", "stories"],
      visible: ({ systemTopstories }) => !!systemTopstories.value,
      disabled: ({ stories }) => !stories.value,
    });
    // Not disabled when Firefox Home is off — the promo remains visible
    // regardless of the homepage setting.
    Preferences.addSetting({
      id: "supportFirefoxPromo",
      deps: ["supportFirefox"],
    });

    // Recent activity
    Preferences.addSetting({
      id: "recentActivity",
      pref: "browser.newtabpage.activity-stream.feeds.section.highlights",
      deps: firefoxHomeDeps,
      disabled: deps => !firefoxHomeActive(deps),
    });
    Preferences.addSetting({
      id: "recentActivityRows",
      pref: "browser.newtabpage.activity-stream.section.highlights.rows",
    });
    Preferences.addSetting({
      id: "recentActivityVisited",
      pref: "browser.newtabpage.activity-stream.section.highlights.includeVisited",
    });
    Preferences.addSetting({
      id: "recentActivityBookmarks",
      pref: "browser.newtabpage.activity-stream.section.highlights.includeBookmarks",
    });
    Preferences.addSetting({
      id: "recentActivityDownloads",
      pref: "browser.newtabpage.activity-stream.section.highlights.includeDownloads",
    });

    // Hidden when Firefox Home is off — the wallpaper page only applies when
    // Firefox Home is the active destination for new windows or new tabs.
    Preferences.addSetting({
      id: "chooseWallpaper",
      deps: firefoxHomeDeps,
      visible: deps => firefoxHomeActive(deps),
      onUserClick: (e, deps) => {
        e.preventDefault();
        window.openTrustedLinkIn(HOME_CUSTOMIZE_URL, dispatchForHomeLink(deps));
      },
    });

    return {
      inProgress: true,
      headingLevel: 2,
      l10nId: "home-prefs-content-header",
      iconSrc: "chrome://browser/skin/home.svg",
      subcategory: "contents",
      items: [
        {
          id: "firefoxHomeDisabledNotice",
          control: "moz-message-bar",
          l10nId: "home-prefs-firefox-home-disabled-notice",
          controlAttrs: {
            type: "info",
          },
        },
        {
          id: "chooseWallpaper",
          l10nId: "home-prefs-choose-wallpaper-link2",
          control: "moz-box-link",
          controlAttrs: {
            href: HOME_CUSTOMIZE_URL,
          },
          iconSrc: "chrome://browser/skin/customize.svg",
        },
        ...(novaEnabled
          ? [
              {
                id: "firefoxLogo",
                l10nId: "home-prefs-firefox-logo-header",
                control: "moz-toggle",
              },
            ]
          : []),
        {
          id: "webSearch",
          subcategory: "web-search",
          l10nId: "home-prefs-search-header2",
          control: "moz-toggle",
        },
        {
          id: "shortcuts",
          subcategory: "topsites",
          l10nId: "home-prefs-shortcuts-header-srd",
          control: "moz-toggle",
          items: [
            {
              id: "shortcutsRows",
              l10nId: "home-prefs-shortcuts-select",
              control: "moz-select",
              options: [
                {
                  value: 1,
                  l10nId: "home-prefs-sections-rows-option-srd",
                  l10nArgs: { num: 1 },
                },
                {
                  value: 2,
                  l10nId: "home-prefs-sections-rows-option-srd",
                  l10nArgs: { num: 2 },
                },
                {
                  value: 3,
                  l10nId: "home-prefs-sections-rows-option-srd",
                  l10nArgs: { num: 3 },
                },
                {
                  value: 4,
                  l10nId: "home-prefs-sections-rows-option-srd",
                  l10nArgs: { num: 4 },
                },
              ],
            },
          ],
        },
        {
          id: "widgets",
          l10nId: "home-prefs-widgets-header",
          control: "moz-toggle",
          // Keeps the about:preferences#home-weather deep link working while
          // Weather is managed with the other widgets.
          ...(weatherNested && { subcategory: "weather" }),
          items: [
            {
              id: "manageWidgets",
              l10nId: "home-prefs-manage-widgets-link",
              control: "moz-box-link",
              controlAttrs: {
                href: HOME_CUSTOMIZE_WIDGETS_URL,
              },
            },
          ],
        },
        ...(weatherNested
          ? []
          : [
              {
                id: "weatherStandalone",
                subcategory: "weather",
                l10nId: "home-prefs-weather-header-srd",
                control: "moz-toggle",
              },
            ]),
        {
          id: "stories",
          subcategory: "topstories",
          l10nId: "home-prefs-stories-header2",
          control: "moz-toggle",
          items: [
            {
              id: "manageTopics",
              l10nId: "home-prefs-manage-topics-link2",
              control: "moz-box-link",
              controlAttrs: {
                href: HOME_CUSTOMIZE_TOPICS_URL,
              },
            },
          ],
        },
        {
          id: "recentActivity",
          subcategory: "highlights",
          l10nId: "home-prefs-recent-activity-header-srd",
          control: "moz-toggle",
          items: [
            {
              id: "recentActivityRows",
              l10nId: "home-prefs-recent-activity-select",
              control: "moz-select",
              options: [
                {
                  value: 1,
                  l10nId: "home-prefs-sections-rows-option-srd",
                  l10nArgs: { num: 1 },
                },
                {
                  value: 2,
                  l10nId: "home-prefs-sections-rows-option-srd",
                  l10nArgs: { num: 2 },
                },
                {
                  value: 3,
                  l10nId: "home-prefs-sections-rows-option-srd",
                  l10nArgs: { num: 3 },
                },
                {
                  value: 4,
                  l10nId: "home-prefs-sections-rows-option-srd",
                  l10nArgs: { num: 4 },
                },
              ],
            },
            {
              id: "recentActivityVisited",
              l10nId: "home-prefs-highlights-option-visited-pages-srd",
            },
            {
              id: "recentActivityBookmarks",
              l10nId: "home-prefs-highlights-options-bookmarks-srd",
            },
            {
              id: "recentActivityDownloads",
              l10nId: "home-prefs-highlights-option-most-recent-download-srd",
            },
          ],
        },
        {
          id: "supportFirefox",
          subcategory: "support-firefox",
          l10nId: "home-prefs-support-firefox-header-srd",
          control: "moz-toggle",
          items: [
            {
              id: "sponsoredShortcuts",
              l10nId: "home-prefs-shortcuts-by-option-sponsored-srd",
            },
            {
              id: "sponsoredStories",
              l10nId: "home-prefs-recommended-by-option-sponsored-stories-srd",
            },
            {
              id: "supportFirefoxPromo",
              l10nId: "home-prefs-mission-message2",
              control: "moz-promo",
              options: [
                {
                  control: "a",
                  l10nId: "home-prefs-mission-message-learn-more-link-srd",
                  slot: "support-link",
                  controlAttrs: {
                    is: "moz-support-link",
                    "support-page": "sponsor-privacy",
                    "utm-content": "inproduct",
                  },
                },
              ],
            },
          ],
        },
      ],
    };
  }
}
