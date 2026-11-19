/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Registers the platform-side setting groups for about:preferences#home:
// defaultBrowserHome, startupHome, homepage, customHomepage. The
// homepage and customHomepage groups moved here from the newtab
// extension as part of Bug 2048379 so downstream builds without the
// newtab extension still get homepage configuration UI. The `home`
// group (Firefox Home content) stays in newtab.

import { SettingGroupManager } from "chrome://browser/content/preferences/config/SettingGroupManager.mjs";
import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { createDefaultBrowserConfig } from "chrome://browser/content/preferences/config/default-browser.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  ExtensionPreferencesManager:
    "resource://gre/modules/ExtensionPreferencesManager.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  HomePage: "resource:///modules/HomePage.sys.mjs",
  LaunchOnLogin: "resource://gre/modules/LaunchOnLogin.sys.mjs",
  Management: "resource://gre/modules/Extension.sys.mjs",
  NimbusFeatures: "resource://nimbus/ExperimentAPI.sys.mjs",
});

const DEFAULT_HOMEPAGE_URL = "about:home";
const HOMEPAGE_OVERRIDE_KEY = "homepage_override";
const URL_OVERRIDES_TYPE = "url_overrides";
const NEW_TAB_KEY = "newTabURL";
const PREF_SETTING_TYPE = "prefs";

const BLANK_HOMEPAGE_URL = "chrome://browser/content/blanktab.html";

/*
 * Preferences:
 *
 * browser.startup.page
 * - what page(s) to show when the user starts the application, as an integer:
 *
 *     0: a blank page (DEPRECATED - this can be set via browser.startup.homepage)
 *     1: the home page (as set by the browser.startup.homepage pref)
 *     2: the last page the user visited (DEPRECATED)
 *     3: windows and tabs from the last session (a.k.a. session restore)
 *
 *   The deprecated option is not exposed in UI; however, if the user has it
 *   selected and doesn't change the UI for this preference, the deprecated
 *   option is preserved.
 */
const STARTUP_PREF_BLANK = 0;
const STARTUP_PREF_HOMEPAGE = 1;
const STARTUP_PREF_RESTORE_SESSION = 3;

/*
 * Preferences:
 *
 * browser.startup.homepage
 * - the user's home page, as a string; if the home page is a set of tabs,
 *   this will be those URLs separated by the pipe character "|"
 * browser.newtabpage.enabled
 * - determines that is shown on the user's new tab page.
 *   true = Activity Stream is shown,
 *   false = about:blank is shown
 */

Preferences.addAll([
  { id: "browser.startup.homepage", type: "string" },
  { id: "pref.browser.homepage.disable_button.current_page", type: "bool" },
  { id: "pref.browser.homepage.disable_button.bookmark_page", type: "bool" },
  { id: "pref.browser.homepage.disable_button.restore_default", type: "bool" },
  { id: "browser.newtabpage.enabled", type: "bool" },

  // Startup
  { id: "browser.startup.page", type: "int" },
  { id: "browser.sessionstore.newTabOnRestore", type: "bool" },
  { id: "browser.startup.windowsLaunchOnLogin.enabled", type: "bool" },
]);

async function getExtensionOptions(type, key) {
  await lazy.ExtensionSettingsStore.initialize();
  let extensionSettings = lazy.ExtensionSettingsStore.getAllSettings(type, key);
  let options = [];
  // Skip extensions that have already been disabled or uninstalled — the
  // store can briefly still list them after the extension has shut down.
  for (let { id } of extensionSettings) {
    let policy = WebExtensionPolicy.getByID(id);
    if (policy) {
      options.push({
        value: policy.id,
        l10nId: "home-prefs-homepage-extension-option",
        l10nArgs: { extension: policy.name },
      });
    }
  }
  return options;
}

function getActiveExtensionForSetting(type, key) {
  try {
    let setting = lazy.ExtensionSettingsStore.getSetting(type, key);
    return setting?.id && WebExtensionPolicy.getByID(setting.id);
  } catch (e) {
    // ExtensionSettingsStore can throw if not yet initialized.
    console.error(e);
    return null;
  }
}

function getHomepageActiveExtension() {
  let ext = getActiveExtensionForSetting(
    PREF_SETTING_TYPE,
    HOMEPAGE_OVERRIDE_KEY
  );
  if (ext) {
    return ext;
  }
  let prefVal = Services.prefs.getStringPref("browser.startup.homepage", "");
  try {
    let uri = Services.io.newURI(prefVal);
    return WebExtensionPolicy.getByURI(uri);
  } catch {
    return null;
  }
}

function makeAddonListenerForRefresh(refreshFn) {
  return {
    onEnabled: refreshFn,
    onDisabled: refreshFn,
    onInstalled: refreshFn,
    onUninstalled: refreshFn,
  };
}

function makeExtensionSettingChangedListener(type, key, refreshFn) {
  return (_evt, changedSetting) => {
    if (changedSetting.key === key && changedSetting.type === type) {
      refreshFn();
    }
  };
}

function forceSelectValue(prefWindow, settingId, value) {
  if (!value || prefWindow.closed) {
    return;
  }
  prefWindow.requestAnimationFrame(() => {
    let control = prefWindow.document.getElementById(
      `setting-control-${settingId}`
    );
    // Setting may live on the home pane while we're rendered on the
    // customHomepage subpage — silently skip if the control isn't on the
    // active pane.
    if (!control) {
      return;
    }
    control.controlEl.value = value;
  });
}

/** @param {Window} prefWindow */
function setupHomepageGroup(prefWindow) {
  const { Preferences: panelPrefs } = prefWindow;

  // Extension APIs load on demand, and opening a settings page does not load
  // the ones that own the homepage and new tab overrides. Without this, the
  // dropdowns can show Firefox Home while an extension is in control, and will
  // not update when that extension is enabled or disabled.
  lazy.Management.asyncLoadSettingsModules().catch(e =>
    console.error("Failed to load extension settings modules", e)
  );

  // Set up `browser.startup.homepage` to show homepage options for Homepage / New Windows
  let homepageExtOptions = [];
  panelPrefs.addSetting(
    /** @type {{ useCustomHomepage: boolean } & SettingConfig } */ ({
      id: "homepageNewWindows",
      pref: "browser.startup.homepage",
      useCustomHomepage: false,
      setup(onChange) {
        let refreshExtensions = async () => {
          homepageExtOptions = await getExtensionOptions(
            PREF_SETTING_TYPE,
            HOMEPAGE_OVERRIDE_KEY
          );
          if (!prefWindow.closed) {
            onChange();
            let ext = getHomepageActiveExtension();
            forceSelectValue(prefWindow, "homepageNewWindows", ext?.id);
          }
        };

        refreshExtensions().catch(e =>
          console.error("Failed to refresh homepage extensions", e)
        );

        // Refresh whenever the homepage pref changes — covers third-party
        // writes (enterprise policy, manual edits) that bypass the
        // extension-setting-changed event.
        let homepagePrefObserver = () => {
          onChange();
          let ext = getHomepageActiveExtension();
          forceSelectValue(prefWindow, "homepageNewWindows", ext?.id);
        };
        Services.prefs.addObserver(
          "browser.startup.homepage",
          homepagePrefObserver
        );

        let onExtensionChange = makeExtensionSettingChangedListener(
          PREF_SETTING_TYPE,
          HOMEPAGE_OVERRIDE_KEY,
          refreshExtensions
        );
        lazy.Management.on("extension-setting-changed", onExtensionChange);

        let addonListener = makeAddonListenerForRefresh(refreshExtensions);
        lazy.AddonManager.addAddonListener(addonListener);

        return () => {
          lazy.AddonManager.removeAddonListener(addonListener);
          lazy.Management.off("extension-setting-changed", onExtensionChange);
          Services.prefs.removeObserver(
            "browser.startup.homepage",
            homepagePrefObserver
          );
        };
      },
      get(prefVal) {
        if (this.useCustomHomepage) {
          return "custom";
        }
        let ext = getHomepageActiveExtension();
        if (ext) {
          return ext.id;
        }
        switch (prefVal) {
          case DEFAULT_HOMEPAGE_URL:
            return "home";
          case BLANK_HOMEPAGE_URL:
            return "blank";
          // Custom value can be any string so leaving it as default value to catch
          // non-default/blank entries.
          default:
            return "custom";
        }
      },
      set(inputVal, _, setting) {
        let wasCustomHomepage = this.useCustomHomepage;
        this.useCustomHomepage = inputVal === "custom";
        if (wasCustomHomepage !== this.useCustomHomepage) {
          setting.onChange();
        }

        // Deselection uses the low-level ExtensionSettingsStore API
        // because the pref is already being set by the return value.
        if (["home", "blank", "custom"].includes(inputVal)) {
          let currentAddon = getActiveExtensionForSetting(
            PREF_SETTING_TYPE,
            HOMEPAGE_OVERRIDE_KEY
          );
          if (currentAddon) {
            try {
              lazy.ExtensionSettingsStore.select(
                null,
                PREF_SETTING_TYPE,
                HOMEPAGE_OVERRIDE_KEY
              );
            } catch (e) {
              console.error("Failed to deselect homepage extension", e);
            }
          }
        }

        switch (inputVal) {
          case "home":
            return DEFAULT_HOMEPAGE_URL;
          case "blank":
            return BLANK_HOMEPAGE_URL;
          case "custom":
            return setting.pref.value;
          default:
            // Selection uses ExtensionPreferencesManager.selectSetting,
            // which also applies the extension's pref value.
            lazy.ExtensionPreferencesManager.selectSetting(
              inputVal,
              HOMEPAGE_OVERRIDE_KEY
            ).catch(e =>
              console.error("Failed to select homepage extension", e)
            );
            return setting.pref.value;
        }
      },
      getControlConfig(config) {
        // `config` is retained across renders, so filter back to the
        // static builtins before reattaching the current extension entries.
        let builtinValues = new Set(["home", "blank", "custom"]);
        let builtinOptions = config.options.filter(o =>
          builtinValues.has(o.value)
        );
        let extOptions = [...homepageExtOptions];
        // Add an option for extensions that set the homepage pref
        // directly without registering in ExtensionSettingsStore.
        let ext = getHomepageActiveExtension();
        if (ext && !extOptions.some(o => o.value === ext.id)) {
          extOptions.push({
            value: ext.id,
            l10nId: "home-prefs-homepage-extension-option",
            l10nArgs: { extension: ext.name },
          });
        }
        return {
          ...config,
          options: [...builtinOptions, ...extOptions],
        };
      },
    })
  );

  // Set up `browser.startup.homepage` again to update and display its value
  // on the Homepage and Custom Homepage settings panes.
  panelPrefs.addSetting({
    id: "homepageDisplayPref",
    pref: "browser.startup.homepage",
  });

  panelPrefs.addSetting({
    id: "disableCurrentPagesButton",
    pref: "pref.browser.homepage.disable_button.current_page",
  });

  panelPrefs.addSetting({
    id: "disableBookmarkButton",
    pref: "pref.browser.homepage.disable_button.bookmark_page",
  });

  // Homepage / Choose Custom Homepage URL Button
  panelPrefs.addSetting({
    id: "homepageGoToCustomHomepageUrlPanel",
    deps: ["homepageNewWindows", "homepageDisplayPref"],
    visible: ({ homepageNewWindows }) => {
      return homepageNewWindows.value === "custom";
    },
    onUserClick: () => {
      prefWindow.gotoPref("customHomepage");
    },

    getControlConfig(config, { homepageDisplayPref }) {
      let customURLsDescription;

      // Make sure we only show user-provided values for custom URLs rather than
      // values we set in `browser.startup.homepage` for "Firefox Home",
      // "Blank Page", or extension-controlled URLs.
      let prefVal = homepageDisplayPref.value.trim();
      if ([DEFAULT_HOMEPAGE_URL, BLANK_HOMEPAGE_URL].includes(prefVal)) {
        customURLsDescription = null;
      } else {
        // Add a comma-separated list of Custom URLs the user set for their homepage
        // to the description part of the "Choose a specific site" box button.
        customURLsDescription = homepageDisplayPref.value
          .split("|")
          .map(uri => lazy.BrowserUtils.formatURIStringForDisplay(uri))
          .filter(Boolean)
          .join(", ");
      }

      return {
        ...config,
        controlAttrs: {
          ...config.controlAttrs,
          ".description": customURLsDescription,
        },
      };
    },
  });

  // Homepage / New Tabs
  let newTabExtOptions = [];
  panelPrefs.addSetting({
    id: "homepageNewTabs",
    pref: "browser.newtabpage.enabled",
    setup(onChange) {
      let refreshExtensions = async () => {
        newTabExtOptions = await getExtensionOptions(
          URL_OVERRIDES_TYPE,
          NEW_TAB_KEY
        );
        if (!prefWindow.closed) {
          onChange();
          let activeId = getActiveExtensionForSetting(
            URL_OVERRIDES_TYPE,
            NEW_TAB_KEY
          )?.id;
          forceSelectValue(prefWindow, "homepageNewTabs", activeId);
        }
      };

      refreshExtensions().catch(e =>
        console.error("Failed to refresh new tab extensions", e)
      );

      let onExtensionChange = makeExtensionSettingChangedListener(
        URL_OVERRIDES_TYPE,
        NEW_TAB_KEY,
        refreshExtensions
      );
      lazy.Management.on("extension-setting-changed", onExtensionChange);

      // Pick up extension installs that set AboutNewTab.newTabURL directly.
      let newTabObserver = () => refreshExtensions();
      Services.obs.addObserver(newTabObserver, "newtab-url-changed");

      let addonListener = makeAddonListenerForRefresh(refreshExtensions);
      lazy.AddonManager.addAddonListener(addonListener);

      return () => {
        lazy.AddonManager.removeAddonListener(addonListener);
        Services.obs.removeObserver(newTabObserver, "newtab-url-changed");
        lazy.Management.off("extension-setting-changed", onExtensionChange);
      };
    },
    get(prefVal) {
      // No URL-based fallback — new tab extensions always register
      // through ExtensionSettingsStore.
      let activeId = getActiveExtensionForSetting(
        URL_OVERRIDES_TYPE,
        NEW_TAB_KEY
      )?.id;
      if (activeId) {
        return activeId;
      }
      return prefVal ? "home" : "blank";
    },
    set(inputVal) {
      if (inputVal === "home" || inputVal === "blank") {
        let currentAddon = getActiveExtensionForSetting(
          URL_OVERRIDES_TYPE,
          NEW_TAB_KEY
        );
        if (currentAddon) {
          try {
            // Deselecting via the low-level API is sufficient here;
            // the url_overrides machinery listens for this and resets
            // AboutNewTab.newTabURL.
            lazy.ExtensionSettingsStore.select(
              null,
              URL_OVERRIDES_TYPE,
              NEW_TAB_KEY
            );
          } catch (e) {
            console.error("Failed to deselect new tab extension", e);
          }
        }
        return inputVal === "home";
      }
      try {
        lazy.ExtensionSettingsStore.select(
          inputVal,
          URL_OVERRIDES_TYPE,
          NEW_TAB_KEY
        );
      } catch (e) {
        console.error("Failed to select new tab extension", e);
      }
      return true;
    },
    getControlConfig(config) {
      // `config` is retained across renders, so filter back to the
      // static builtins before reattaching the current extension entries.
      let builtinValues = new Set(["home", "blank"]);
      let builtinOptions = config.options.filter(o =>
        builtinValues.has(o.value)
      );
      return {
        ...config,
        options: [...builtinOptions, ...newTabExtOptions],
      };
    },
  });

  // Homepage / Restore Defaults button
  panelPrefs.addSetting({
    id: "homepageRestoreDefaults",
    pref: "pref.browser.homepage.disable_button.restore_default",
    deps: ["homepageNewWindows", "homepageNewTabs"],
    disabled: ({ homepageNewWindows, homepageNewTabs }) => {
      return (
        homepageNewWindows.value === "home" && homepageNewTabs.value === "home"
      );
    },
    onUserClick: (e, { homepageNewWindows, homepageNewTabs }) => {
      e.preventDefault();

      homepageNewWindows.value = "home";
      homepageNewTabs.value = "home";
    },
  });

  return {
    inProgress: true,
    headingLevel: 2,
    iconSrc: "chrome://browser/skin/window-firefox.svg",
    l10nId: "home-homepage-title",
    subcategory: "homepage",
    items: [
      {
        id: "homepageNewWindows",
        subcategory: "homeOverride",
        control: "moz-select",
        l10nId: "home-homepage-new-windows",
        options: [
          {
            value: "home",
            l10nId: "home-mode-choice-default-fx",
          },
          { value: "blank", l10nId: "home-mode-choice-blank" },
          { value: "custom", l10nId: "home-mode-choice-custom" },
        ],
      },
      {
        id: "homepageGoToCustomHomepageUrlPanel",
        control: "moz-box-button",
        l10nId: "home-homepage-custom-homepage-button",
        loadPane: "customHomepage",
      },
      {
        id: "homepageNewTabs",
        subcategory: "newtabOverride",
        control: "moz-select",
        l10nId: "home-homepage-new-tabs",
        options: [
          {
            value: "home",
            l10nId: "home-mode-choice-default-fx",
          },
          { value: "blank", l10nId: "home-mode-choice-blank" },
        ],
      },
      {
        id: "homepageRestoreDefaults",
        control: "moz-button",
        iconSrc: "chrome://global/skin/icons/arrow-counterclockwise-16.svg",
        l10nId: "home-restore-defaults",
        controlAttrs: { id: "restoreDefaultHomePageBtn" },
      },
    ],
  };
}

/** @param {Window} prefWindow */
function setupCustomHomepageGroup(prefWindow) {
  const { Preferences: panelPrefs } = prefWindow;

  panelPrefs.addSetting(
    /** @type {{ _inputValue: string } & SettingConfig } */ ({
      id: "customHomepageAddUrlInput",
      deps: ["homepageDisplayPref"],
      _inputValue: "",
      get() {
        return this._inputValue;
      },
      set(val, _, setting) {
        this._inputValue = val.trim();
        setting.onChange();
      },
      disabled({ homepageDisplayPref }) {
        return homepageDisplayPref.locked;
      },
    })
  );

  panelPrefs.addSetting({
    id: "customHomepageAddAddressButton",
    deps: ["homepageDisplayPref", "customHomepageAddUrlInput"],
    onUserClick(e, { homepageDisplayPref, customHomepageAddUrlInput }) {
      // Focus is being stolen by a parent component here (moz-fieldset).
      // Focus on the button to get the input value.
      e.target.focus();

      let inputVal = customHomepageAddUrlInput.value;

      // Don't do anything for empty strings
      if (!inputVal) {
        return;
      }

      let currentVal = homepageDisplayPref.value.trim();
      if (
        [DEFAULT_HOMEPAGE_URL, BLANK_HOMEPAGE_URL].includes(currentVal) ||
        currentVal.startsWith("moz-extension://")
      ) {
        // Replace non-custom homepage values with the new Custom URL.
        homepageDisplayPref.value = inputVal;
      } else {
        // Append this URL to the list of Custom URLs saved in prefs.
        let urls = lazy.HomePage.parseCustomHomepageURLs(
          homepageDisplayPref.value
        );
        urls.push(inputVal);
        homepageDisplayPref.value = urls.join("|");
      }

      // Reset the field to empty string
      customHomepageAddUrlInput.value = "";
    },
    disabled({ homepageDisplayPref }) {
      return homepageDisplayPref.locked;
    },
  });

  panelPrefs.addSetting({
    id: "customHomepageReplaceWithCurrentPagesButton",
    deps: ["homepageDisplayPref", "disableCurrentPagesButton"],
    // Re-evaluate disabled state on tab open/close (add/remove tabs) and
    // pin/unpin (changes what getTabsForCustomHomepage() captures).
    setup(emitChange) {
      let win = /** @type {any} */ (
        Services.wm.getMostRecentWindow("navigator:browser")
      );
      if (!win) {
        return () => {};
      }
      const { tabContainer } = win.gBrowser;
      // Best-effort filter: skip events from tabs already showing about:preferences.
      // TabOpen fires before the URI is set, so it isn't caught here;
      // the real exclusion happens inside getTabsForCustomHomepage().
      const onTabChange = (/** @type {Event & { target: any }} */ event) => {
        if (
          event.target.linkedBrowser?.currentURI?.spec?.startsWith(
            "about:preferences"
          )
        ) {
          return;
        }
        emitChange();
      };
      tabContainer.addEventListener("TabOpen", onTabChange);
      tabContainer.addEventListener("TabClose", onTabChange);
      tabContainer.addEventListener("TabPinned", onTabChange);
      tabContainer.addEventListener("TabUnpinned", onTabChange);
      return () => {
        tabContainer.removeEventListener("TabOpen", onTabChange);
        tabContainer.removeEventListener("TabClose", onTabChange);
        tabContainer.removeEventListener("TabPinned", onTabChange);
        tabContainer.removeEventListener("TabUnpinned", onTabChange);
      };
    },
    onUserClick(e, { homepageDisplayPref }) {
      let tabs = lazy.HomePage.getTabsForCustomHomepage();

      if (tabs.length) {
        homepageDisplayPref.value = tabs
          .map(t => t.linkedBrowser.currentURI.spec)
          .join("|");
      }
    },
    disabled: ({ disableCurrentPagesButton, homepageDisplayPref }) =>
      // Disable this button if the only open tab is `about:preferences`/`about:settings`
      // or when an enterprise policy sets a special pref to true
      lazy.HomePage.getTabsForCustomHomepage().length < 1 ||
      disableCurrentPagesButton?.value === true ||
      homepageDisplayPref.locked,
  });

  panelPrefs.addSetting({
    id: "customHomepageReplaceWithBookmarksButton",
    deps: ["homepageDisplayPref", "disableBookmarkButton"],
    onUserClick(e, { homepageDisplayPref }) {
      const rv = { urls: null, names: null };

      // Callback to use when bookmark dialog closes
      const closingCallback = event => {
        if (event.detail.button !== "accept") {
          return;
        }
        if (rv.urls) {
          homepageDisplayPref.value = rv.urls.join("|");
        }
      };

      prefWindow.gSubDialog.open(
        "chrome://browser/content/preferences/dialogs/selectBookmark.xhtml",
        {
          features: "resizable=yes, modal=yes",
          closingCallback,
        },
        rv
      );
    },
    disabled: ({ disableBookmarkButton, homepageDisplayPref }) =>
      // Disable this button if an enterprise policy sets a special pref to true
      disableBookmarkButton?.value === true || homepageDisplayPref.locked,
  });

  panelPrefs.addSetting({
    id: "customHomepageBoxGroup",
    deps: ["homepageDisplayPref"],
    setup(onChange) {
      // Refresh the list when an extension's policy registers or
      // unregisters, so an extension URL in the pref renders as
      // "Extension (Name)" once the policy becomes available (and falls
      // back to the raw URL if it goes away).
      let onExtensionChange = makeExtensionSettingChangedListener(
        PREF_SETTING_TYPE,
        HOMEPAGE_OVERRIDE_KEY,
        onChange
      );
      lazy.Management.on("extension-setting-changed", onExtensionChange);

      let addonListener = makeAddonListenerForRefresh(onChange);
      lazy.AddonManager.addAddonListener(addonListener);

      return () => {
        lazy.AddonManager.removeAddonListener(addonListener);
        lazy.Management.off("extension-setting-changed", onExtensionChange);
      };
    },
    getControlConfig(config, { homepageDisplayPref }) {
      const urls = lazy.HomePage.parseCustomHomepageURLs(
        homepageDisplayPref.value
      );
      let listItems = [];
      let type = "list";

      // Show a reorderable list of Custom URLs if the user has provided any.
      // Make sure to exclude "Firefox Home", "Blank Page", and
      // extension-controlled URLs that are also stored in the homepage pref.
      let currentPrefVal = homepageDisplayPref.value.trim();
      if (
        ![DEFAULT_HOMEPAGE_URL, BLANK_HOMEPAGE_URL].includes(currentPrefVal)
      ) {
        type = homepageDisplayPref.locked ? "list" : "reorderable-list";
        listItems = urls.map((url, index) => ({
          id: `customHomepageUrl-${index}`,
          key: `url-${index}-${url}`,
          control: "moz-box-item",
          controlAttrs: {
            label: lazy.BrowserUtils.formatURIStringForDisplay(url),
            "data-url": url,
          },
          options: homepageDisplayPref.locked
            ? []
            : [
                {
                  control: "moz-button",
                  iconSrc: "chrome://global/skin/icons/delete.svg",
                  l10nId: "home-custom-homepage-delete-address-button",
                  slot: "actions-start",
                  controlAttrs: {
                    "data-action": "delete",
                    "data-index": index,
                  },
                },
              ],
        }));
      } else {
        // If no custom URLs have been set, show the "no results" string instead.
        listItems = [
          {
            control: "moz-box-item",
            l10nId: "home-custom-homepage-no-results",
            controlAttrs: {
              class: "description-deemphasized",
            },
          },
        ];
      }

      return {
        ...config,
        controlAttrs: {
          ...config.controlAttrs,
          type,
        },
        options: [
          {
            id: "customHomepageBoxForm",
            control: "moz-box-item",
            slot: "header",
            items: [
              {
                id: "customHomepageAddUrlInput",
                l10nId: "home-custom-homepage-address",
                control: "moz-input-text",
              },
              {
                id: "customHomepageAddAddressButton",
                l10nId: "home-custom-homepage-address-button",
                control: "moz-button",
                slot: "actions",
              },
            ],
          },
          ...listItems,
          {
            id: "customHomepageBoxActions",
            control: "moz-box-item",
            l10nId: "home-custom-homepage-replace-with-prompt",
            slot: "footer",
            items: [
              {
                id: "customHomepageReplaceWithCurrentPagesButton",
                l10nId: "home-custom-homepage-current-pages-button",
                control: "moz-button",
                slot: "actions",
              },
              {
                id: "customHomepageReplaceWithBookmarksButton",
                l10nId: "home-custom-homepage-bookmarks-button",
                control: "moz-button",
                slot: "actions",
                controlAttrs: {
                  "search-l10n-ids":
                    "select-bookmark-window2.title, select-bookmark-desc",
                },
              },
            ],
          },
        ],
      };
    },
    onUserReorder(e, { homepageDisplayPref }) {
      let urls = lazy.HomePage.parseCustomHomepageURLs(
        homepageDisplayPref.value
      );
      urls = e.target.reorderArrayFromEvent(urls, e);
      homepageDisplayPref.value = urls.join("|");
    },
    onUserClick(e, { homepageDisplayPref }) {
      let urls = lazy.HomePage.parseCustomHomepageURLs(
        homepageDisplayPref.value
      );

      if (
        e.target.localName === "moz-button" &&
        e.target.getAttribute("data-action") === "delete"
      ) {
        let index = Number(e.target.dataset.index);
        if (Number.isInteger(index) && index >= 0 && index < urls.length) {
          urls.splice(index, 1);
          homepageDisplayPref.value = urls.join("|");
        }
      }
    },
  });

  return {
    inProgress: true,
    headingLevel: 2,
    l10nId: "home-custom-homepage-card-header",
    iconSrc: "chrome://global/skin/icons/link.svg",
    items: [
      {
        id: "customHomepageBoxGroup",
        l10nId: "home-custom-homepage-list",
        control: "moz-box-group",
        controlAttrs: {
          type: "list",
        },
      },
    ],
  };
}

Preferences.addSetting({
  /**
   * The "Open previous windows and tabs" option on about:preferences page.
   */
  id: "browserRestoreSession",
  pref: "browser.startup.page",
  deps: ["privateBrowsingAutoStart"],
  get:
    /**
     * Returns the value of the "Open previous windows and tabs" option based
     * on the value of the browser.privatebrowsing.autostart pref.
     *
     * @param {number | undefined} value
     * @returns {boolean}
     */
    value => {
      const pbAutoStartPref = Preferences.get(
        "browser.privatebrowsing.autostart"
      );
      let newValue = pbAutoStartPref.value
        ? false
        : value === STARTUP_PREF_RESTORE_SESSION;

      return newValue;
    },
  set: checked => {
    const startupPref = Preferences.get("browser.startup.page");
    let newValue;

    if (checked) {
      // We need to restore the blank homepage setting in our other pref
      if (startupPref.value === STARTUP_PREF_BLANK) {
        // @ts-ignore bug 1996860
        lazy.HomePage.safeSet("about:blank");
      }
      newValue = STARTUP_PREF_RESTORE_SESSION;
    } else {
      newValue = STARTUP_PREF_HOMEPAGE;
    }
    return newValue;
  },
  disabled: deps => {
    return deps.privateBrowsingAutoStart.value;
  },
});

Preferences.addSetting(
  /** @type {{ _getLaunchOnLoginApprovedCachedValue: boolean } & SettingConfig} */ ({
    id: "launchOnLoginApproved",
    _getLaunchOnLoginApprovedCachedValue: true,
    get() {
      return this._getLaunchOnLoginApprovedCachedValue;
    },
    // Check for a launch on login registry key
    // This accounts for if a user manually changes it in the registry
    // Disabling in Task Manager works outside of just deleting the registry key
    // in HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run
    // but it is not possible to change it back to enabled as the disabled value is just a random
    // hexadecimal number
    setup(emitChange) {
      if (!lazy.LaunchOnLogin.isSupported()) {
        return;
      }
      // @ts-ignore bug 1996860
      lazy.LaunchOnLogin.isAllowed().then(val => {
        if (val !== this._getLaunchOnLoginApprovedCachedValue) {
          this._getLaunchOnLoginApprovedCachedValue = val;
          emitChange();
        }
      });
    },
  })
);

Preferences.addSetting({
  id: "sessionRestoreNewTab",
  pref: "browser.sessionstore.newTabOnRestore",
  deps: ["browserRestoreSession"],
  visible: () =>
    Services.prefs.getBoolPref(
      "browser.sessionstore.newTabOnRestore.showSetting",
      false
    ),
  disabled: deps => !deps.browserRestoreSession.value,
});

Preferences.addSetting({
  id: "windowsLaunchOnLoginEnabled",
  pref: "browser.startup.windowsLaunchOnLogin.enabled",
});

Preferences.addSetting(
  /** @type {{_getLaunchOnLoginEnabledValue: boolean} & SettingConfig} */ ({
    id: "windowsLaunchOnLogin",
    deps: ["launchOnLoginApproved", "windowsLaunchOnLoginEnabled"],
    _getLaunchOnLoginEnabledValue: false,
    get() {
      return this._getLaunchOnLoginEnabledValue;
    },
    setup(emitChange) {
      if (!lazy.LaunchOnLogin.isSupported()) {
        return;
      }

      // @ts-ignore bug 1996860
      lazy.LaunchOnLogin.isEnabled().then(val => {
        if (val !== this._getLaunchOnLoginEnabledValue) {
          this._getLaunchOnLoginEnabledValue = val;
          emitChange();
        }
      });
    },
    visible: ({ windowsLaunchOnLoginEnabled }) => {
      let isVisible =
        lazy.LaunchOnLogin.isSupported() && windowsLaunchOnLoginEnabled.value;
      if (isVisible) {
        // @ts-ignore bug 1996860
        lazy.NimbusFeatures.windowsLaunchOnLogin.recordExposureEvent({
          once: true,
        });
      }
      return isVisible;
    },
    disabled({ launchOnLoginApproved }) {
      return !launchOnLoginApproved.value;
    },
    onUserChange(checked) {
      Glean.launchOnLogin.userToggle.record({ enabled: checked });
      if (checked) {
        // windowsLaunchOnLogin has been checked: create registry key or shortcut
        // The shortcut is created with the same AUMID as Firefox itself. However,
        // this is not set during browser tests and the fallback of checking the
        // registry fails. As such we pass an arbitrary AUMID for the purpose
        // of testing.
        // @ts-ignore bug 1996860
        lazy.LaunchOnLogin.enable();
      } else {
        // windowsLaunchOnLogin has been unchecked: delete registry key and shortcut
        // @ts-ignore bug 1996860
        lazy.LaunchOnLogin.disable();
      }
    },
  })
);

Preferences.addSetting({
  id: "windowsLaunchOnLoginDisabledBox",
  deps: ["launchOnLoginApproved", "windowsLaunchOnLoginEnabled"],
  visible: ({ launchOnLoginApproved, windowsLaunchOnLoginEnabled }) => {
    if (!lazy.LaunchOnLogin.isSupported()) {
      return false;
    }
    return !launchOnLoginApproved.value && windowsLaunchOnLoginEnabled.value;
  },
});

function createStartupConfig() {
  return {
    l10nId: "startup-group",
    headingLevel: 2,
    items: [
      {
        id: "browserRestoreSession",
        l10nId: "startup-restore-windows-and-tabs",
        items: [
          {
            id: "sessionRestoreNewTab",
            l10nId: "windows-launch-on-login-open-new-tab",
          },
        ],
      },
      {
        id: "windowsLaunchOnLogin",
        l10nId: "windows-launch-on-login",
      },
      {
        id: "windowsLaunchOnLoginDisabledBox",
        control: "moz-message-bar",
        controlAttrs: {
          role: "status",
        },
        options: [
          {
            control: "span",
            l10nId: "windows-launch-on-login-disabled",
            slot: "message",
            options: [
              {
                control: "a",
                controlAttrs: {
                  "data-l10n-name": "startup-link",
                  href: "ms-settings:startupapps",
                  target: "_self",
                },
              },
            ],
          },
        ],
      },
      {
        id: "alwaysCheckDefault",
        l10nId: "always-check-default",
      },
    ],
  };
}

SettingGroupManager.registerGroups({
  defaultBrowserHome: createDefaultBrowserConfig(),
  startupHome: createStartupConfig(),
  homepage: setupHomepageGroup(window),
  customHomepage: setupCustomHomepageGroup(window),
});
