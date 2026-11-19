/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { RemoteSettings } from "resource://services-settings/remote-settings.sys.mjs";

const COLLECTION_NAME = "remote-permissions";

/**
 * Allowlist of permission types and values allowed to be set through remote
 * settings. In this map, the key is the permission type, while the value is an
 * array of allowed permission values/capabilities allowed to be set. Possible
 * values for most permissions are:
 *
 * - Ci.nsIPermissionManager.ALLOW_ACTION
 * - Ci.nsIPermissionManager.DENY_ACTION
 * - Ci.nsIPermissionManager.PROMPT_ACTION
 * - "*" (Allows all values)
 *
 * Permission types with custom permission values (like
 * https-only-load-insecure) may include different values. Only change this
 * value with a review from #permissions-reviewers.
 */
const ALLOWED_PERMISSION_VALUES = {
  "bounce-tracking-protection-exception": [
    Ci.nsIPermissionManager.ALLOW_ACTION,
  ],
  "https-only-load-insecure": [
    Ci.nsIHttpsOnlyModePermission.HTTPSFIRST_LOAD_INSECURE_ALLOW,
  ],
  "loopback-network": ["*"],
  "local-network": ["*"],
};

/**
 * See nsIRemotePermissionService.idl
 */
export class RemotePermissionService {
  classId = Components.ID("{a4b1b3b1-b68a-4129-aa2f-eb086162a8c7}");
  QueryInterface = ChromeUtils.generateQI(["nsIRemotePermissionService"]);

  #rs = RemoteSettings(COLLECTION_NAME);
  #initialized = Promise.withResolvers();
  #allowedPermissionValues = ALLOWED_PERMISSION_VALUES;

  constructor() {
    this.init();
  }

  /**
   * Asynchonously import all default permissions from remote settings into the
   * permission manager and set up remote settings event listener to keep
   * remote permissions in sync.
   */
  async init() {
    try {
      if (Services.startup.shuttingDown) {
        return;
      }

      if (
        !Services.prefs.getBoolPref("permissions.manager.remote.enabled", false)
      ) {
        return;
      }

      let remotePermissions = await this.#rs.get();
      for (const permission of remotePermissions) {
        this.#addDefaultPermission(permission);
      }

      this.#rs.on("sync", this.#onSync.bind(this));

      this.#initialized.resolve();
    } catch (e) {
      this.#initialized.reject(e);
      throw e;
    }
  }

  get isInitialized() {
    return this.#initialized.promise;
  }

  get testAllowedPermissionValues() {
    return this.#allowedPermissionValues;
  }

  set testAllowedPermissionValues(allowedPermissionValues) {
    Cu.crashIfNotInAutomation();
    this.#allowedPermissionValues = allowedPermissionValues;
  }

  // eslint-disable-next-line jsdoc/require-param
  /**
   * Callback for the "sync" event from remote settings. This function will
   * receive the created, updated and deleted permissions from remote settings,
   * and will update the permission manager accordingly.
   */
  #onSync({ data: { created = [], updated = [], deleted = [] } }) {
    const toBeDeletedPermissions = [
      // Delete permissions that got deleted in remote settings.
      ...deleted,
      // If an existing entry got updated in remote settings, but the origin or
      // type changed, we can not just update it, as permissions are identified
      // by origin and type in the permission manager. Instead, we need to
      // remove the old permission and add a new one.
      ...updated
        .filter(
          ({
            old: { origin: oldOrigin, type: oldType },
            new: { origin: newOrigin, type: newType },
          }) => oldOrigin != newOrigin || oldType != newType
        )
        .map(({ old }) => old),
    ];

    const toBeAddedPermissions = [
      // Add newly created permissions.
      ...created,
      // "Add" permissions updated in remote settings (the permission manager
      // will automatically update the existing default permission instead of
      // creating a new one if the permission origin and type match).
      ...updated.map(({ new: newPermission }) => newPermission),
      // Delete permissions by "adding" them with value UNKNOWN_ACTION.
      ...toBeDeletedPermissions.map(({ origin, type }) => ({
        origin,
        type,
        capability: Ci.nsIPermissionManager.UNKNOWN_ACTION,
      })),
    ];

    for (const permission of toBeAddedPermissions) {
      this.#addDefaultPermission(permission);
    }
  }

  /**
   * Check if a permission type and value is allowed to be set through remote
   * settings, based on the ALLOWED_PERMISSION_VALUES allowlist.
   *
   * @param {string} type       Permission type to check
   * @param {string} capability Permission capability to check
   * @returns {boolean}
   */
  #isAllowed(type, capability) {
    if (!this.#allowedPermissionValues[type]) {
      if (this.#allowedPermissionValues["*"]) {
        this.#allowedPermissionValues[type] =
          this.#allowedPermissionValues["*"];
      } else {
        return false;
      }
    }

    return (
      this.#allowedPermissionValues[type].includes("*") ||
      this.#allowedPermissionValues[type].includes(capability) ||
      capability === Ci.nsIPermissionManager.UNKNOWN_ACTION
    );
  }

  /**
   * Add a default permission to the permission manager.
   *
   * @param {object} permission            The permission to add
   * @param {string} permission.origin     Origin string of the permission
   * @param {string} permission.type       Type of the permission
   * @param {number} permission.capability Capability of the permission
   */
  #addDefaultPermission({ origin, type, capability }) {
    if (!this.#isAllowed(type, capability)) {
      console.error(
        `Remote Settings contain default permission of disallowed type '${type}' with value '${capability}' for origin '${origin}', skipping import`
      );
      return;
    }

    try {
      let principal = Services.scriptSecurityManager.createContentPrincipal(
        Services.io.newURI(origin),
        {}
      );
      Services.perms.addDefaultFromPrincipal(principal, type, capability);
    } catch (e) {
      console.error(e);
    }
  }
}
