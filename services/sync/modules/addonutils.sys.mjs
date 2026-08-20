/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { Log } from "resource://gre/modules/Log.sys.mjs";

import { Svc } from "resource://services-sync/util.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  AddonRepository: "resource://gre/modules/addons/AddonRepository.sys.mjs",
});

function AddonUtilsInternal() {
  this._log = Log.repository.getLogger("Sync.AddonUtils");
  this._log.Level =
    Log.Level[Svc.PrefBranch.getStringPref("log.logger.addonutils", null)];
}
AddonUtilsInternal.prototype = {
  /**
   * Obtain an AddonInstall object from an AddonSearchResult instance.
   *
   * The returned promise will be an AddonInstall on success or null (failure or
   * addon not found)
   *
   * @param addon
   *        AddonSearchResult to obtain install from.
   */
  getInstallFromSearchResult(addon) {
    this._log.debug("Obtaining install for " + addon.id);

    // We should theoretically be able to obtain (and use) addon.install if
    // it is available. However, the addon.sourceURI rewriting won't be
    // reflected in the AddonInstall, so we can't use it. If we ever get rid
    // of sourceURI rewriting, we can avoid having to reconstruct the
    // AddonInstall.
    return lazy.AddonManager.getInstallForURL(addon.sourceURI.spec, {
      name: addon.name,
      icons: addon.iconURL,
      version: addon.version,
      telemetryInfo: { source: "sync" },
    });
  },

  /**
   * Installs an add-on from an AddonSearchResult instance.
   *
   * The options argument defines extra options to control the install.
   * Recognized keys in this map are:
   *
   *   syncGUID - Sync GUID to use for the new add-on.
   *   enabled - Boolean indicating whether the add-on should be enabled upon
   *             install.
   *
   * The result object has the following keys:
   *
   *   id      ID of add-on that was installed.
   *   install AddonInstall that was installed.
   *   addon   Addon that was installed.
   *
   * @param addon
   *        AddonSearchResult to install add-on from.
   * @param options
   *        Object with additional metadata describing how to install add-on.
   */
  async installAddonFromSearchResult(addon, options) {
    this._log.info("Trying to install add-on from search result: " + addon.id);

    const install = await this.getInstallFromSearchResult(addon);
    if (!install) {
      throw new Error("AddonInstall not available: " + addon.id);
    }

    try {
      this._log.info("Installing " + addon.id);
      let log = this._log;
      let self = this;

      return new Promise((res, rej) => {
        let listener = {
          onInstallStarted: function onInstallStarted(install) {
            if (!options) {
              return;
            }

            if (options.syncGUID) {
              log.info(
                "Setting syncGUID of " + install.name + ": " + options.syncGUID
              );
              install.addon.syncGUID = options.syncGUID;
            }

            // We only need to change userDisabled if it is disabled because
            // enabled is the default.
            if (
              "enabled" in options &&
              !options.enabled &&
              self.canDisableAddon(install.addon.id)
            ) {
              log.info(
                "Marking add-on as disabled for install: " + install.name
              );
              install.addon.disable();
            }
          },
          onInstallEnded(install, addon) {
            // Themes-addons are not automatically enabled when installed, but the active
            // theme is synced by prefs, so we use a transient pref to enable it
            // after install
            let hasIncomingActiveThemeId = Services.prefs.getStringPref(
              "extensions.pendingActiveThemeID",
              ""
            );
            if (
              hasIncomingActiveThemeId &&
              addon.id === hasIncomingActiveThemeId
            ) {
              try {
                addon.enable();
              } catch (e) {
                log.error("Failed to enable the incoming theme", e);
              } finally {
                // If something went wrong with enabling the theme, we don't have a good
                // way to retry -- so we'll clear it rather than keeping the pref around
                Services.prefs.clearUserPref("extensions.pendingActiveThemeID");
              }
            }
            install.removeListener(listener);

            res({ id: addon.id, install, addon });
          },
          onInstallFailed(install) {
            install.removeListener(listener);

            rej(new Error("Install failed: " + install.error));
          },
          onDownloadFailed(install) {
            install.removeListener(listener);

            rej(new Error("Download failed: " + install.error));
          },
        };
        install.addListener(listener);
        install.install();
      });
    } catch (ex) {
      this._log.error("Error installing add-on", ex);
      throw ex;
    }
  },

  /**
   * Uninstalls the addon instance.
   *
   * @param addon
   *        Addon instance to uninstall.
   */
  async uninstallAddon(addon) {
    if (!this.canUninstallAddon(addon.id)) {
      return addon;
    }

    return new Promise(res => {
      let listener = {
        onUninstalling(uninstalling, needsRestart) {
          if (addon.id != uninstalling.id) {
            return;
          }

          // We assume restartless add-ons will send the onUninstalled event
          // soon.
          if (!needsRestart) {
            return;
          }

          // For non-restartless add-ons, we issue the callback on uninstalling
          // because we will likely never see the uninstalled event.
          lazy.AddonManager.removeAddonListener(listener);
          res(addon);
        },
        onUninstalled(uninstalled) {
          if (addon.id != uninstalled.id) {
            return;
          }

          lazy.AddonManager.removeAddonListener(listener);
          res(addon);
        },
      };
      lazy.AddonManager.addAddonListener(listener);
      addon.uninstall();
    });
  },

  /**
   * Installs multiple add-ons specified by metadata.
   *
   * The first argument is an array of objects. Each object must have the
   * following keys:
   *
   *   id - public ID of the add-on to install.
   *   syncGUID - syncGUID for new add-on.
   *   enabled - boolean indicating whether the add-on should be enabled.
   *   requireSecureURI - Boolean indicating whether to require a secure
   *     URI when installing from a remote location. This defaults to
   *     true.
   *
   * The callback will be called when activity on all add-ons is complete. The
   * callback receives 2 arguments, error and result.
   *
   * If error is truthy, it contains a string describing the overall error.
   *
   * The 2nd argument to the callback is always an object with details on the
   * overall execution state. It contains the following keys:
   *
   *   installedIDs  Array of add-on IDs that were installed.
   *   installs      Array of AddonInstall instances that were installed.
   *   addons        Array of Addon instances that were installed.
   *   errors        Array of errors encountered. Only has elements if error is
   *                 truthy.
   *
   * @param installs
   *        Array of objects describing add-ons to install.
   */
  async installAddons(installs) {
    let ids = [];
    for (let addon of installs) {
      ids.push(addon.id);
    }

    let addons = await lazy.AddonRepository.getAddonsByIDs(ids);
    this._log.info(
      `Found ${addons.length} / ${ids.length}` +
        " add-ons during repository search."
    );

    let ourResult = {
      installedIDs: [],
      installs: [],
      addons: [],
      skipped: [],
      errors: [],
    };

    let toInstall = [];

    // Rewrite the "src" query string parameter of the source URI to note
    // that the add-on was installed by Sync and not something else so
    // server-side metrics aren't skewed (bug 708134). The server should
    // ideally send proper URLs, but this solution was deemed too
    // complicated at the time the functionality was implemented.
    for (let addon of addons) {
      // Find the specified options for this addon.
      let options;
      for (let install of installs) {
        if (install.id == addon.id) {
          options = install;
          break;
        }
      }
      if (!this.canInstallAddon(addon, options)) {
        ourResult.skipped.push(addon.id);
        continue;
      }

      // We can go ahead and attempt to install it.
      toInstall.push(addon);

      // We should always be able to QI the nsIURI to nsIURL. If not, we
      // still try to install the add-on, but we don't rewrite the URL,
      // potentially skewing metrics.
      try {
        addon.sourceURI.QueryInterface(Ci.nsIURL);
      } catch (ex) {
        this._log.warn(
          "Unable to QI sourceURI to nsIURL: " + addon.sourceURI.spec
        );
        continue;
      }

      let params = addon.sourceURI.query
        .split("&")
        .map(function rewrite(param) {
          if (param.indexOf("src=") == 0) {
            return "src=sync";
          }
          return param;
        });

      addon.sourceURI = addon.sourceURI
        .mutate()
        .setQuery(params.join("&"))
        .finalize();
    }

    if (!toInstall.length) {
      return ourResult;
    }

    const installPromises = [];
    // Start all the installs asynchronously. They will report back to us
    // as they finish, eventually triggering the global callback.
    for (let addon of toInstall) {
      let options = {};
      for (let install of installs) {
        if (install.id == addon.id) {
          options = install;
          break;
        }
      }

      installPromises.push(
        (async () => {
          try {
            const result = await this.installAddonFromSearchResult(
              addon,
              options
            );
            ourResult.installedIDs.push(result.id);
            ourResult.installs.push(result.install);
            ourResult.addons.push(result.addon);
          } catch (error) {
            ourResult.errors.push(error);
          }
        })()
      );
    }

    await Promise.all(installPromises);

    if (ourResult.errors.length) {
      throw new Error("1 or more add-ons failed to install");
    }
    return ourResult;
  },

  /**
   * Returns true if we are able to install the specified addon, false
   * otherwise. It is expected that this will log the reason if it returns
   * false.
   *
   * @param addon
   *        (Addon) Add-on instance to check.
   * @param options
   *        (object) The options specified for this addon. See installAddons()
   *        for the valid elements.
   */
  canInstallAddon(addon, options) {
    // sourceURI presence isn't enforced by AddonRepository. So, we skip
    // add-ons without a sourceURI.
    if (!addon.sourceURI) {
      this._log.info(
        "Skipping install of add-on because missing sourceURI: " + addon.id
      );
      return false;
    }
    // Verify that the source URI uses TLS. We don't allow installs from
    // insecure sources for security reasons. The Addon Manager ensures
    // that cert validation etc is performed.
    // (We should also consider just dropping this entirely and calling
    // XPIProvider.isInstallAllowed, but that has additional semantics we might
    // need to think through...)
    let requireSecureURI = true;
    if (options && options.requireSecureURI !== undefined) {
      requireSecureURI = options.requireSecureURI;
    }

    if (requireSecureURI) {
      let scheme = addon.sourceURI.scheme;
      if (scheme != "https") {
        this._log.info(
          `Skipping install of add-on "${addon.id}" because sourceURI's scheme of "${scheme}" is not trusted`
        );
        return false;
      }
    }

    // Policy prevents either installing this addon or any addon
    if (
      Services.policies &&
      (!Services.policies.mayInstallAddon({ id: addon.id, type: addon.type }) ||
        !Services.policies.isAllowed("xpinstall"))
    ) {
      this._log.info(
        `Skipping install of "${addon.id}" due to enterprise policy`
      );
      return false;
    }

    this._log.info(`Add-on "${addon.id}" is able to be installed`);
    return true;
  },

  /**
   * Returns true if enterprise policy allows the add-on to be disabled. Other
   * clients sharing the account aren't subject to this client's policies, so
   * their records must never be able to disable a locked add-on.
   *
   * @param id
   *        (string) ID of the add-on to check.
   */
  canDisableAddon(id) {
    if (
      Services.policies &&
      !Services.policies.isAllowed(`disable-extension:${id}`)
    ) {
      this._log.info(`Skipping disable of "${id}" due to enterprise policy`);
      return false;
    }
    return true;
  },

  /**
   * Returns true if enterprise policy allows the add-on to be uninstalled.
   *
   * @param id
   *        (string) ID of the add-on to check.
   */
  canUninstallAddon(id) {
    if (
      Services.policies &&
      !Services.policies.isAllowed(`uninstall-extension:${id}`)
    ) {
      this._log.info(`Skipping uninstall of "${id}" due to enterprise policy`);
      return false;
    }
    return true;
  },

  /**
   * Update the user disabled flag for an add-on.
   *
   * If the new flag matches the existing or if the add-on
   * isn't currently active, the function will return immediately.
   *
   * @param addon
   *        (Addon) Add-on instance to operate on.
   * @param value
   *        (bool) New value for add-on's userDisabled property.
   */
  updateUserDisabled(addon, value) {
    if (addon.userDisabled == value) {
      return;
    }

    if (value && !this.canDisableAddon(addon.id)) {
      return;
    }

    this._log.info("Updating userDisabled flag: " + addon.id + " -> " + value);
    if (value) {
      addon.disable();
    } else {
      addon.enable();
    }
  },
};

export const AddonUtils = new AddonUtilsInternal();
