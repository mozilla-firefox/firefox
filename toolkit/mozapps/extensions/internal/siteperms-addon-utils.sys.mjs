/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

export const GATED_PERMISSIONS = ["midi", "midi-sysex", "serial"];
export const SITEPERMS_ADDON_PROVIDER_PREF =
  "dom.sitepermsaddon-provider.enabled";
export const SITEPERMS_ADDON_TYPE = "sitepermission";
export const SITEPERMS_ADDON_BLOCKEDLIST_PREF =
  "dom.sitepermsaddon-provider.separatedBlocklistedDomains";

export const GATED_PERMISSIONS_STRING_IDS = {
  midi: {
    installPrompt: {
      header: "site-permission-install-first-prompt-midi-header",
      message: "site-permission-install-first-prompt-midi-message",
    },
    permissionsPrompt: {
      header: "webext-site-perms-header-with-gated-perms-midi",
      description: "webext-site-perms-description-gated-perms-midi",
    },
    shortDescription: "webext-site-perms-midi",
  },
  "midi-sysex": {
    installPrompt: {
      header: "site-permission-install-first-prompt-midi-header",
      message: "site-permission-install-first-prompt-midi-message",
    },
    permissionsPrompt: {
      header: "webext-site-perms-header-with-gated-perms-midi-sysex",
      description: "webext-site-perms-description-gated-perms-midi",
    },
    shortDescription: "webext-site-perms-midi-sysex",
  },
  serial: {
    installPrompt: {
      header: "site-permission-install-first-prompt-serial-header",
      message: "site-permission-install-first-prompt-serial-message",
    },
    permissionsPrompt: {
      header: "webext-site-perms-header-with-gated-perms-serial",
      description: "webext-site-perms-description-gated-perms-webserial",
    },
    shortDescription: "webext-site-perms-serial",
  },
};

const lazy = {};
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "blocklistedOriginsSet",
  SITEPERMS_ADDON_BLOCKEDLIST_PREF,
  // Default value
  "",
  // onUpdate
  null,
  // transform
  prefValue => new Set(prefValue.split(","))
);

/**
 * @param {string} type
 * @returns {boolean}
 */
export function isGatedPermissionType(type) {
  return GATED_PERMISSIONS.includes(type);
}

/**
 * @param {string} siteOrigin
 * @returns {boolean}
 */
export function isKnownPublicSuffix(siteOrigin) {
  const { host } = new URL(siteOrigin);

  let isPublic = false;
  // getKnownPublicSuffixFromHost throws when passed an IP, in such case, assume
  // this is not a public etld.
  try {
    isPublic = Services.eTLD.getKnownPublicSuffixFromHost(host) == host;
  } catch (e) {}

  return isPublic;
}

/**
 * ⚠️ This should be only used for testing purpose ⚠️
 *
 * @param {Array<string>} permissionTypes
 * @throws if not called from xpcshell test
 */
export function addGatedPermissionTypesForXpcShellTests(permissionTypes) {
  if (!Services.env.exists("XPCSHELL_TEST_PROFILE_DIR")) {
    throw new Error("This should only be called from XPCShell tests");
  }

  GATED_PERMISSIONS.push(...permissionTypes);
}

/**
 * @param {nsIPrincipal} principal
 * @returns {boolean}
 */
export function isPrincipalInSitePermissionsBlocklist(principal) {
  return lazy.blocklistedOriginsSet.has(principal.baseDomain);
}

/**
 * Get the localized string IDs for the install prompt of a gated permission type.
 *
 * @param {string} type
 * @returns {object|null}
 */
export function getSitePermsInstallPromptStringIds(type) {
  return GATED_PERMISSIONS_STRING_IDS[type]?.installPrompt ?? null;
}

/**
 * Get the localized string IDs for the permissions prompt of a gated permission type.
 *
 * @param {string} type
 * @returns {object|null}
 */
export function getSitePermsPermissionsPromptStringIds(type) {
  return GATED_PERMISSIONS_STRING_IDS[type]?.permissionsPrompt ?? null;
}

/**
 * Get the localized string ID for the permissions list that is used in the
 * SitePermissions addon cards in about:addons.
 *
 * @param {string} type
 * @returns {object|null}
 */
export function getSitePermsShortDescriptionStringId(type) {
  return GATED_PERMISSIONS_STRING_IDS[type]?.shortDescription ?? null;
}
