/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ExtensionPermissions: "resource://gre/modules/ExtensionPermissions.sys.mjs",
  Schemas: "resource://gre/modules/Schemas.sys.mjs",
});

var { ExtensionError } = ExtensionUtils;

XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "promptsEnabled",
  "extensions.webextOptionalPermissionPrompts"
);
XPCOMUtils.defineLazyPreferenceGetter(
  this,
  "dataCollectionPermissionsEnabled",
  "extensions.dataCollectionPermissions.enabled",
  false
);

ChromeUtils.defineLazyGetter(this, "OPTIONAL_ONLY_PERMISSIONS", () => {
  // Schemas.getPermissionNames() depends on API schemas to have been loaded.
  // This is always the case here - extension APIs can only be called when an
  // extension has started. And as part of startup, extension schemas are
  // always parsed, via extension.loadManifest at:
  // https://searchfox.org/mozilla-central/rev/2deb9bcf801f9de83d4f30c890d442072b9b6595/toolkit/components/extensions/Extension.sys.mjs#2094
  return new Set(Schemas.getPermissionNames(["OptionalOnlyPermission"]));
});

function normalizePermissions(perms) {
  perms = { ...perms };
  perms.permissions = perms.permissions.filter(
    perm => !perm.startsWith("internal:") && perm !== "<all_urls>"
  );

  if (!dataCollectionPermissionsEnabled) {
    delete perms.data_collection;
  }

  return perms;
}

this.permissions = class extends ExtensionAPIPersistent {
  PERSISTENT_EVENTS = {
    onAdded({ fire }) {
      let { extension } = this;
      let callback = (event, change) => {
        if (change.extensionId == extension.id && change.added) {
          let perms = normalizePermissions(change.added);
          if (
            perms.permissions.length ||
            perms.origins.length ||
            (dataCollectionPermissionsEnabled && perms.data_collection.length)
          ) {
            fire.async(perms);
          }
        }
      };

      extensions.on("change-permissions", callback);
      return {
        unregister() {
          extensions.off("change-permissions", callback);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    },
    onRemoved({ fire }) {
      let { extension } = this;
      let callback = (event, change) => {
        if (change.extensionId == extension.id && change.removed) {
          let perms = normalizePermissions(change.removed);
          if (
            perms.permissions.length ||
            perms.origins.length ||
            (dataCollectionPermissionsEnabled && perms.data_collection.length)
          ) {
            fire.async(perms);
          }
        }
      };

      extensions.on("change-permissions", callback);
      return {
        unregister() {
          extensions.off("change-permissions", callback);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    },
  };

  getAPI(context) {
    let { extension } = context;

    return {
      permissions: {
        async request(perms) {
          let { permissions, origins, data_collection } = perms;

          let { optionalPermissions } = context.extension;
          for (let perm of permissions) {
            if (!optionalPermissions.includes(perm)) {
              throw new ExtensionError(
                `Cannot request permission ${perm} since it was not declared in optional_permissions`
              );
            }
            if (
              OPTIONAL_ONLY_PERMISSIONS.has(perm) &&
              (permissions.length > 1 ||
                origins.length ||
                (dataCollectionPermissionsEnabled && data_collection.length))
            ) {
              throw new ExtensionError(
                `Cannot request permission ${perm} with another permission`
              );
            }
          }

          let optionalOrigins = context.extension.optionalOrigins;
          for (let origin of origins) {
            if (!optionalOrigins.subsumes(new MatchPattern(origin))) {
              throw new ExtensionError(
                `Cannot request origin permission for ${origin} since it was not declared in the manifest`
              );
            }
          }

          if (dataCollectionPermissionsEnabled) {
            let { optionalDataCollectionPermissions } = context.extension;
            for (let perm of data_collection) {
              if (!optionalDataCollectionPermissions.includes(perm)) {
                throw new ExtensionError(
                  `Cannot request data collection permission ${perm} since it ` +
                    "was not declared in data_collection_permissions.optional"
                );
              }
            }
          }

          if (promptsEnabled) {
            permissions = permissions.filter(
              perm => !context.extension.hasPermission(perm)
            );
            origins = origins.filter(
              origin =>
                !context.extension.allowedOrigins.subsumes(
                  new MatchPattern(origin)
                )
            );
            data_collection = data_collection.filter(
              perm => !context.extension.dataCollectionPermissions.has(perm)
            );

            if (
              !permissions.length &&
              !origins.length &&
              !data_collection.length
            ) {
              return true;
            }

            let browser = context.pendingEventBrowser || context.xulBrowser;
            let allowPromise = new Promise(resolve => {
              let subject = {
                wrappedJSObject: {
                  browser,
                  name: context.extension.name,
                  id: context.extension.id,
                  icon: context.extension.getPreferredIcon(32),
                  permissions: { permissions, origins, data_collection },
                  resolve,
                },
              };
              Services.obs.notifyObservers(
                subject,
                "webextension-optional-permission-prompt"
              );
            });
            if (context.isBackgroundContext) {
              extension.emit("background-script-idle-waituntil", {
                promise: allowPromise,
                reason: "permissions_request",
              });
            }
            if (!(await allowPromise)) {
              return false;
            }
          }

          await ExtensionPermissions.add(extension.id, perms, extension);
          return true;
        },

        async getAll() {
          let perms = normalizePermissions(context.extension.activePermissions);
          delete perms.apis;
          return perms;
        },

        async contains(permissions) {
          for (let perm of permissions.permissions) {
            if (!context.extension.hasPermission(perm)) {
              return false;
            }
          }

          for (let origin of permissions.origins) {
            if (
              !context.extension.allowedOrigins.subsumes(
                new MatchPattern(origin)
              )
            ) {
              return false;
            }
          }

          if (dataCollectionPermissionsEnabled) {
            for (let perm of permissions.data_collection) {
              if (!context.extension.dataCollectionPermissions.has(perm)) {
                return false;
              }
            }
          }

          return true;
        },

        async remove(permissions) {
          await ExtensionPermissions.remove(
            extension.id,
            permissions,
            extension
          );
          return true;
        },

        onAdded: new EventManager({
          context,
          module: "permissions",
          event: "onAdded",
          extensionApi: this,
        }).api(),

        onRemoved: new EventManager({
          context,
          module: "permissions",
          event: "onRemoved",
          extensionApi: this,
        }).api(),
      },
    };
  }
};
