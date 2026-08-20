/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineLazyGetter(this, "strBundle", function () {
  return Services.strings.createBundle(
    "chrome://global/locale/extensions.properties"
  );
});
ChromeUtils.defineESModuleGetters(this, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
});

// We can't use Services.prompt here at the moment, as tests need to mock
// the prompt service. We could use sinon, but that didn't seem to work
// with Android builds.
// eslint-disable-next-line mozilla/use-services
XPCOMUtils.defineLazyServiceGetter(
  this,
  "promptService",
  "@mozilla.org/prompter;1",
  Ci.nsIPromptService
);

var { ExtensionError } = ExtensionUtils;

const _ = (key, ...args) => {
  if (args.length) {
    return strBundle.formatStringFromName(key, args);
  }
  return strBundle.GetStringFromName(key);
};

const installType = addon => {
  if (addon.temporarilyInstalled) {
    return "development";
  } else if (addon.foreignInstall) {
    return "sideload";
  } else if (addon.isSystem) {
    return "other";
  } else if (Services.policies?.isAddonRequiredByPolicy(addon.id)) {
    return "admin";
  }
  return "normal";
};

const getExtensionInfoForAddon = (extension, addon) => {
  let extInfo = {
    id: addon.id,
    name: addon.name,
    description: addon.description || "",
    version: addon.version,
    mayDisable: !!(addon.permissions & AddonManager.PERM_CAN_DISABLE),
    enabled: addon.isActive,
    optionsUrl: addon.optionsURL || "",
    installType: installType(addon),
    type: addon.type,
  };

  if (extension) {
    let m = extension.manifest;

    let hostPerms = extension.allowedOrigins.patterns.map(
      matcher => matcher.pattern
    );

    extInfo.permissions = Array.from(extension.permissions).filter(perm => {
      return !hostPerms.includes(perm);
    });
    extInfo.hostPermissions = hostPerms;

    extInfo.shortName = m.short_name || "";
    if (m.icons) {
      extInfo.icons = Object.keys(m.icons).map(key => {
        return { size: Number(key), url: m.icons[key] };
      });
    }
  }

  if (!addon.isActive) {
    extInfo.disabledReason = "unknown";
  }
  if (addon.homepageURL) {
    extInfo.homepageUrl = addon.homepageURL;
  }
  if (addon.updateURL) {
    extInfo.updateUrl = addon.updateURL;
  }
  return extInfo;
};

// Some management APIs are intentionally limited.
const allowedTypes = ["theme", "extension"];

function checkAllowedAddon(addon) {
  if (addon.isSystem) {
    return false;
  }
  if (addon.type == "extension" && !addon.isWebExtension) {
    return false;
  }
  return allowedTypes.includes(addon.type);
}

class ManagementAddonListener extends ExtensionCommon.EventEmitter {
  eventNames = ["onEnabled", "onDisabled", "onInstalled", "onUninstalled"];

  hasAnyListeners() {
    for (let event of this.eventNames) {
      if (this.has(event)) {
        return true;
      }
    }
    return false;
  }

  on(event, listener) {
    if (!this.eventNames.includes(event)) {
      throw new Error("unsupported event");
    }
    if (!this.hasAnyListeners()) {
      AddonManager.addAddonListener(this);
    }
    super.on(event, listener);
  }

  off(event, listener) {
    if (!this.eventNames.includes(event)) {
      throw new Error("unsupported event");
    }
    super.off(event, listener);
    if (!this.hasAnyListeners()) {
      AddonManager.removeAddonListener(this);
    }
  }

  getExtensionInfo(addon) {
    let ext = WebExtensionPolicy.getByID(addon.id)?.extension;
    return getExtensionInfoForAddon(ext, addon);
  }

  onEnabled(addon) {
    if (!checkAllowedAddon(addon)) {
      return;
    }
    this.emit("onEnabled", this.getExtensionInfo(addon));
  }

  onDisabled(addon) {
    if (!checkAllowedAddon(addon)) {
      return;
    }
    this.emit("onDisabled", this.getExtensionInfo(addon));
  }

  onInstalled(addon) {
    if (!checkAllowedAddon(addon)) {
      return;
    }
    this.emit("onInstalled", this.getExtensionInfo(addon));
  }

  onUninstalled(addon) {
    if (!checkAllowedAddon(addon)) {
      return;
    }
    this.emit("onUninstalled", this.getExtensionInfo(addon));
  }
}

this.management = class extends ExtensionAPIPersistent {
  addonListener = new ManagementAddonListener();

  onShutdown() {
    AddonManager.removeAddonListener(this.addonListener);
  }

  eventRegistrar(eventName) {
    return ({ fire }) => {
      let listener = (event, data) => {
        fire.async(data);
      };

      this.addonListener.on(eventName, listener);
      return {
        unregister: () => {
          this.addonListener.off(eventName, listener);
        },
        convert(_fire) {
          fire = _fire;
        },
      };
    };
  }

  PERSISTENT_EVENTS = {
    onDisabled: this.eventRegistrar("onDisabled"),
    onEnabled: this.eventRegistrar("onEnabled"),
    onInstalled: this.eventRegistrar("onInstalled"),
    onUninstalled: this.eventRegistrar("onUninstalled"),
  };

  getAPI(context) {
    let { extension } = context;

    return {
      management: {
        async get(id) {
          let addon = await AddonManager.getAddonByID(id);
          if (!addon) {
            throw new ExtensionError(`No such addon ${id}`);
          }
          if (!checkAllowedAddon(addon)) {
            throw new ExtensionError("get not allowed for this addon");
          }
          // If the extension is enabled get it and use it for more data.
          let ext = WebExtensionPolicy.getByID(addon.id)?.extension;
          return getExtensionInfoForAddon(ext, addon);
        },

        async getAll() {
          let addons = await AddonManager.getAddonsByTypes(allowedTypes);
          return addons.filter(checkAllowedAddon).map(addon => {
            // If the extension is enabled get it and use it for more data.
            let ext = WebExtensionPolicy.getByID(addon.id)?.extension;
            return getExtensionInfoForAddon(ext, addon);
          });
        },

        async install({ url, hash }) {
          let listener = {
            onDownloadEnded(install) {
              if (install.addon.appDisabled || install.addon.type !== "theme") {
                install.cancel();
                return false;
              }
            },
          };

          let telemetryInfo = {
            source: "extension",
            method: "management-webext-api",
          };
          let install = await AddonManager.getInstallForURL(url, {
            hash,
            telemetryInfo,
            triggeringPrincipal: extension.principal,
          });
          install.addListener(listener);
          try {
            await install.install();
          } catch (e) {
            Cu.reportError(e);
            throw new ExtensionError("Incompatible addon");
          }
          await install.addon.enable();
          return { id: install.addon.id };
        },

        async getSelf() {
          let addon = await AddonManager.getAddonByID(extension.id);
          return getExtensionInfoForAddon(extension, addon);
        },

        async uninstallSelf(options) {
          if (options && options.showConfirmDialog) {
            let message = _("uninstall.confirmation.message", extension.name);
            if (options.dialogMessage) {
              message = `${options.dialogMessage}\n${message}`;
            }
            let title = _("uninstall.confirmation.title", extension.name);
            let buttonFlags =
              Ci.nsIPrompt.BUTTON_POS_0 * Ci.nsIPrompt.BUTTON_TITLE_IS_STRING +
              Ci.nsIPrompt.BUTTON_POS_1 * Ci.nsIPrompt.BUTTON_TITLE_IS_STRING;
            let button0Title = _("uninstall.confirmation.button-0.label");
            let button1Title = _("uninstall.confirmation.button-1.label");
            let response = promptService.confirmEx(
              null,
              title,
              message,
              buttonFlags,
              button0Title,
              button1Title,
              null,
              null,
              { value: 0 }
            );
            if (response == 1) {
              throw new ExtensionError("User cancelled uninstall of extension");
            }
          }
          let addon = await AddonManager.getAddonByID(extension.id);
          let canUninstall = Boolean(
            addon.permissions & AddonManager.PERM_CAN_UNINSTALL
          );
          if (!canUninstall) {
            throw new ExtensionError("The add-on cannot be uninstalled");
          }
          addon.uninstall();
        },

        async setEnabled(id, enabled) {
          let addon = await AddonManager.getAddonByID(id);
          if (!addon) {
            throw new ExtensionError(`No such addon ${id}`);
          }

          if (
            addon.type !== "theme" &&
            !Services.policies?.isAddonRequiredByPolicy(extension.id)
          ) {
            throw new ExtensionError(
              "setEnabled can only be used for themes or by addons installed by enterprise policy"
            );
          }
          if (!checkAllowedAddon(addon)) {
            throw new ExtensionError("setEnabled not allowed for this addon");
          }
          if (
            !enabled &&
            Services.policies &&
            !Services.policies.isAllowed(`disable-extension:${id}`)
          ) {
            throw new ExtensionError(
              `Cannot disable add-on ${id}: disallowed by enterprise policy`
            );
          }
          if (enabled) {
            await addon.enable();
          } else {
            await addon.disable();
          }
        },

        onDisabled: new EventManager({
          context,
          module: "management",
          event: "onDisabled",
          extensionApi: this,
        }).api(),

        onEnabled: new EventManager({
          context,
          module: "management",
          event: "onEnabled",
          extensionApi: this,
        }).api(),

        onInstalled: new EventManager({
          context,
          module: "management",
          event: "onInstalled",
          extensionApi: this,
        }).api(),

        onUninstalled: new EventManager({
          context,
          module: "management",
          event: "onUninstalled",
          extensionApi: this,
        }).api(),
      },
    };
  }
};
