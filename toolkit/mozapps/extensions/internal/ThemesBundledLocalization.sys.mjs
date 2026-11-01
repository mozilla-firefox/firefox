/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const DEFAULT_THEME_ID = "default-theme@mozilla.org";
const PREF_NOVA_ENABLED = "browser.nova.enabled";

const lazy = XPCOMUtils.declareLazy({
  l10n: () =>
    new Localization(
      // eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
      ["browser/appExtensionFields.ftl", "branding/brand.ftl"],
      true
    ),
  novaEnabled: { pref: PREF_NOVA_ENABLED, default: false },
});

// Every built-in and Curated AMO-hosted Nova theme id with bundled Fluent
// localization.
//
// Curated AMO-hosted Nova theme ids only have "name" localization bundled
// because they are meant to be shown in the Firefox Themes picker before
// they are actually installed (their description isn't shown by the picker
// and so it is meant to be originating from AMO/the manifest like any other
// installed theme).
//
// TODO(Bug 2053217): consider rolling into a themes_list.json file built into
// the omni jar as a single source of truth for this module and
// ThemesList.sys.mjs.
const THEME_L10N_IDS = new Map([
  [
    DEFAULT_THEME_ID,
    {
      name: "extension-default-theme-name2",
      description: "extension-default-theme-description2",
    },
  ],
  [
    "firefox-compact-light@mozilla.org",
    {
      name: "extension-firefox-compact-light-name",
      description: "extension-firefox-compact-light-description",
    },
  ],
  [
    "firefox-compact-dark@mozilla.org",
    {
      name: "extension-firefox-compact-dark-name",
      description: "extension-firefox-compact-dark-description",
    },
  ],
  [
    "firefox-alpenglow@mozilla.org",
    {
      name: "extension-firefox-alpenglow-name",
      description: "extension-firefox-alpenglow-description",
    },
  ],
  ["nova-sun@mozilla.org", { name: "extension-nova-sun-name" }],
  ["nova-spark@mozilla.org", { name: "extension-nova-spark-name" }],
  ["nova-flame@mozilla.org", { name: "extension-nova-flame-name" }],
  ["nova-flare@mozilla.org", { name: "extension-nova-flare-name" }],
  ["nova-lavender@mozilla.org", { name: "extension-nova-lavender-name" }],
  ["nova-dusk@mozilla.org", { name: "extension-nova-dusk-name" }],
  ["nova-lagoon@mozilla.org", { name: "extension-nova-lagoon-name" }],
  ["nova-pine@mozilla.org", { name: "extension-nova-pine-name" }],
  ["nova-tide@mozilla.org", { name: "extension-nova-tide-name" }],
  ["nova-ash@mozilla.org", { name: "extension-nova-ash-name" }],
  ["nova-smoke@mozilla.org", { name: "extension-nova-smoke-name" }],
]);

/**
 * Strips the "@mozilla.org" suffix shared by all official built-in and
 * AMO-hosted theme ids.
 *
 * @param {string} themeId
 * @returns {string} themeId minus its "@mozilla.org" suffix.
 */
export function themeIdPrefix(themeId) {
  return themeId.replace("@mozilla.org", "");
}

/**
 * Whether addonId has a Fluent-localized name/description bundled with
 * Firefox, rather than sourced from its manifest/AddonRepository.
 *
 * @param {string} addonId
 * @returns {boolean} Whether addonId is a theme with bundled Fluent
 *   localization (a built-in theme, or a curated Nova theme id).
 */
export function hasThemeIdBundledLocalization(addonId) {
  return THEME_L10N_IDS.has(addonId);
}

/**
 * Computes a theme's Fluent id for a given theme id without resolving it.
 *
 * @param {string} addonId
 * @param {string} prop AddonWrapper property name, e.g. "name" or
 *   "description".
 * @returns {string} Fluent id.
 */
export function getL10nIdForThemeProp(addonId, prop) {
  // Keep the pre-Nova name/description for the default theme unchanged when
  // Nova is disabled.
  if (addonId === DEFAULT_THEME_ID && !lazy.novaEnabled) {
    switch (prop) {
      case "name":
        return "extension-default-theme-name-auto";
      case "description":
        return "extension-default-theme-description";
    }
  }

  return THEME_L10N_IDS.get(addonId)?.[prop];
}

/**
 * Resolves a theme's localized property through Fluent, for an installed
 * AddonWrapper.
 */
export function getL10nThemeString(addonId, prop) {
  // TODO (Bug 2059562): Agree with Thunderbird engineering how we should allow thunderbird to
  // override or extend the mapping table for their own built-in themes localization
  // in the longer term, see their appExtensionFields.ftl file here:
  // https://searchfox.org/comm-central/rev/72b8ba0761/mail/locales/en-US/browser/appExtensionFields.ftl
  const fluentId =
    AppConstants.MOZ_APP_NAME === "thunderbird" && addonId !== DEFAULT_THEME_ID
      ? `extension-${themeIdPrefix(addonId)}-${prop}`
      : getL10nIdForThemeProp(addonId, prop);

  if (!fluentId) {
    // No fluent id found, e.g. a Nova theme's description -- not bundled here,
    // or previous set of curated Firefox themes hosted on AMO (e.g. Colorway themes)
    return null;
  }

  let [message] = lazy.l10n.formatMessagesSync([{ id: fluentId }]);
  return message?.value;
}
