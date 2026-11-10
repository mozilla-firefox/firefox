/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Preferences } from "chrome://global/content/preferences/Preferences.mjs";
import { DefaultBrowserHelper } from "chrome://browser/content/preferences/DefaultBrowserHelper.mjs";

const { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

if (AppConstants.HAVE_SHELL_SERVICE) {
  Preferences.addAll([
    { id: "browser.shell.checkDefaultBrowser", type: "bool" },
  ]);
}

Preferences.addSetting({
  id: "alwaysCheckDefault",
  pref: "browser.shell.checkDefaultBrowser",
  setup: emitChange => {
    if (!DefaultBrowserHelper.canCheck) {
      return undefined;
    }
    return DefaultBrowserHelper.pollForDefaultChanges(emitChange);
  },
  visible: () => DefaultBrowserHelper.canCheck,
  disabled: (_, setting) =>
    !DefaultBrowserHelper.canCheck ||
    setting.locked ||
    DefaultBrowserHelper.isBrowserDefault,
});

Preferences.addSetting({
  id: "isDefaultPane",
  deps: ["alwaysCheckDefault"],
  visible: () =>
    DefaultBrowserHelper.canCheck &&
    DefaultBrowserHelper.isBrowserDefault &&
    Services.policies.isAllowed("setDefaultBrowser") &&
    !Services.prefs.prefIsLocked("pref.general.disable_button.default_browser"),
});

Preferences.addSetting({
  id: "isNotDefaultPane",
  deps: ["alwaysCheckDefault"],
  visible: () =>
    DefaultBrowserHelper.canCheck &&
    !DefaultBrowserHelper.isBrowserDefault &&
    Services.policies.isAllowed("setDefaultBrowser") &&
    !Services.prefs.prefIsLocked("pref.general.disable_button.default_browser"),
  onUserClick: (e, { alwaysCheckDefault }) => {
    if (!DefaultBrowserHelper.canCheck) {
      return;
    }
    const setDefaultButton = e.target;

    if (!setDefaultButton) {
      return;
    }
    if (setDefaultButton.disabled) {
      return;
    }

    setDefaultButton.disabled = true;
    alwaysCheckDefault.value = true;
    DefaultBrowserHelper.setDefaultBrowser().finally(() => {
      setDefaultButton.disabled = false;
    });
  },
});

export function createDefaultBrowserConfig({
  includeIsDefaultPane = true,
  hiddenFromSearch = false,
} = {}) {
  const isDefaultPane = {
    id: "isDefaultPane",
    l10nId: "is-default-browser-2",
    control: "moz-promo",
    controlAttrs: {
      imagesrc: "chrome://global/skin/illustrations/kit-happy.svg",
      imagedisplay: "cover",
    },
  };

  const isNotDefaultPane = {
    id: "isNotDefaultPane",
    l10nId: "is-not-default-browser-2",
    control: "moz-promo",
    options: [
      {
        control: "moz-button",
        l10nId: "set-as-my-default-browser-2",
        id: "setDefaultButton",
        slot: "actions",

        controlAttrs: {
          type: "primary",
        },
      },
    ],
    controlAttrs: {
      imagesrc: "chrome://global/skin/illustrations/kit-concerned.svg",
      imagedisplay: "cover",
    },
  };

  const items = includeIsDefaultPane
    ? [isDefaultPane, isNotDefaultPane]
    : [isNotDefaultPane];

  return {
    l10nId: "home-default-browser-title",
    headingLevel: 2,
    items,
    ...(hiddenFromSearch && { hiddenFromSearch }),
  };
}
