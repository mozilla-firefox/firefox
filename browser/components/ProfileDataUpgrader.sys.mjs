/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  FormAutofillUtils: "resource://gre/modules/shared/FormAutofillUtils.sys.mjs",
  FirefoxBridgeExtensionUtils:
    "resource:///modules/FirefoxBridgeExtensionUtils.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  PlacesUIUtils: "moz-src:///browser/components/places/PlacesUIUtils.sys.mjs",
  UsageReporting: "resource://gre/modules/UsageReporting.sys.mjs",
});

export let ProfileDataUpgrader = {
  _migrateXULStoreForDocument(fromURL, toURL) {
    Array.from(Services.xulStore.getIDsEnumerator(fromURL)).forEach(id => {
      Array.from(Services.xulStore.getAttributeEnumerator(fromURL, id)).forEach(
        attr => {
          let value = Services.xulStore.getValue(fromURL, id, attr);
          Services.xulStore.setValue(toURL, id, attr, value);
        }
      );
    });
  },

  _migrateHashedKeysForXULStoreForDocument(docUrl) {
    Array.from(Services.xulStore.getIDsEnumerator(docUrl))
      .filter(id => id.startsWith("place:"))
      .forEach(id => {
        Services.xulStore.removeValue(docUrl, id, "open");
        let hashedId = lazy.PlacesUIUtils.obfuscateUrlForXulStore(id);
        Services.xulStore.setValue(docUrl, hashedId, "open", "true");
      });
  },

  /**
   * This method transforms data in the profile directory so that it can be
   * used in the current version of Firefox. It is organized similar to
   * typical database version upgrades: we are invoked with the version of the
   * profile data on disk (`existingDataVersion`) and the version we expect/need
   * (`newVersion`), and execute any necessary migrations.
   *
   * In practice, most of the migrations move user choices from one preference
   * to another, or ensure that other mechanical file moves (e.g. of document
   * URLs like browser.xhtml).
   *
   * If you're adding a new migration, you will need to increment
   * APP_DATA_VERSION in BrowserGlue.sys.mjs' _migrateUI. That version is not
   * in this module so that we can avoid loading this module entirely in common
   * cases (Firefox startups where a profile is not upgraded).
   *
   * Note that this is invoked very early on startup and should try to avoid
   * doing very expensive things immediately unless absolutely necessary. Some
   * of the migrations will therefore set a pref or otherwise flag that their
   * component needs to do more work later, perhaps during idle tasks or
   * similar, to avoid front-loading the component initialization into this
   * early part of startup. Of course, some of these migrations (e.g. to ensure
   * that browser windows remember their sizes if the URL to browser.xhtml has
   * changed) _need_ to run very early, and that is OK.
   *
   * @param {integer} existingDataVersion
   *  The version of the data in the profile.
   * @param {integer} newVersion
   *  The version that the application expects/needs.
   */
  // eslint-disable-next-line complexity
  upgrade(existingDataVersion, newVersion) {
    const BROWSER_DOCURL = AppConstants.BROWSER_CHROME_URL;

    let xulStore = Services.xulStore;

    if (existingDataVersion < 90) {
      this._migrateXULStoreForDocument(
        "chrome://browser/content/places/historySidebar.xul",
        "chrome://browser/content/places/historySidebar.xhtml"
      );
      this._migrateXULStoreForDocument(
        "chrome://browser/content/places/places.xul",
        "chrome://browser/content/places/places.xhtml"
      );
      this._migrateXULStoreForDocument(
        "chrome://browser/content/places/bookmarksSidebar.xul",
        "chrome://browser/content/places/bookmarksSidebar.xhtml"
      );
    }

    // Clear socks proxy values if they were shared from http, to prevent
    // websocket breakage after bug 1577862 (see bug 969282).
    if (
      existingDataVersion < 91 &&
      Services.prefs.getBoolPref("network.proxy.share_proxy_settings", false) &&
      Services.prefs.getIntPref("network.proxy.type", 0) == 1
    ) {
      let httpProxy = Services.prefs.getCharPref("network.proxy.http", "");
      let httpPort = Services.prefs.getIntPref("network.proxy.http_port", 0);
      let socksProxy = Services.prefs.getCharPref("network.proxy.socks", "");
      let socksPort = Services.prefs.getIntPref("network.proxy.socks_port", 0);
      if (httpProxy && httpProxy == socksProxy && httpPort == socksPort) {
        Services.prefs.setCharPref(
          "network.proxy.socks",
          Services.prefs.getCharPref("network.proxy.backup.socks", "")
        );
        Services.prefs.setIntPref(
          "network.proxy.socks_port",
          Services.prefs.getIntPref("network.proxy.backup.socks_port", 0)
        );
      }
    }

    if (existingDataVersion < 92) {
      // privacy.userContext.longPressBehavior pref was renamed and changed to a boolean
      let longpress = Services.prefs.getIntPref(
        "privacy.userContext.longPressBehavior",
        0
      );
      if (longpress == 1) {
        Services.prefs.setBoolPref(
          "privacy.userContext.newTabContainerOnLeftClick.enabled",
          true
        );
      }
    }

    if (existingDataVersion < 93) {
      // The Gecko Profiler Addon is now an internal component. Remove the old
      // addon, and enable the new UI.

      function enableProfilerButton(wasAddonActive) {
        // Enable the feature pref. This will add it to the customization palette,
        // but not to the the navbar.
        Services.prefs.setBoolPref(
          "devtools.performance.popup.feature-flag",
          true
        );

        if (wasAddonActive) {
          const { ProfilerMenuButton } = ChromeUtils.importESModule(
            "resource://devtools/client/performance-new/popup/menu-button.sys.mjs"
          );
          if (!ProfilerMenuButton.isInNavbar()) {
            ProfilerMenuButton.addToNavbar();
          }
        }
      }

      let addonPromise;
      try {
        addonPromise = lazy.AddonManager.getAddonByID(
          "geckoprofiler@mozilla.com"
        );
      } catch (error) {
        console.error(
          "Could not access the AddonManager to upgrade the profile. This is most " +
            "likely because the upgrader is being run from an xpcshell test where " +
            "the AddonManager is not initialized."
        );
      }
      Promise.resolve(addonPromise).then(addon => {
        if (!addon) {
          // Either the addon wasn't installed, or the call to getAddonByID failed.
          return;
        }
        // Remove the old addon.
        const wasAddonActive = addon.isActive;
        addon
          .uninstall()
          .catch(console.error)
          .then(() => enableProfilerButton(wasAddonActive))
          .catch(console.error);
      }, console.error);
    }

    // Clear unused socks proxy backup values - see bug 1625773.
    if (existingDataVersion < 94) {
      let backup = Services.prefs.getCharPref("network.proxy.backup.socks", "");
      let backupPort = Services.prefs.getIntPref(
        "network.proxy.backup.socks_port",
        0
      );
      let socksProxy = Services.prefs.getCharPref("network.proxy.socks", "");
      let socksPort = Services.prefs.getIntPref("network.proxy.socks_port", 0);
      if (backup == socksProxy) {
        Services.prefs.clearUserPref("network.proxy.backup.socks");
      }
      if (backupPort == socksPort) {
        Services.prefs.clearUserPref("network.proxy.backup.socks_port");
      }
    }

    if (existingDataVersion < 95) {
      const oldPrefName = "media.autoplay.enabled.user-gestures-needed";
      const oldPrefValue = Services.prefs.getBoolPref(oldPrefName, true);
      const newPrefValue = oldPrefValue ? 0 : 1;
      Services.prefs.setIntPref("media.autoplay.blocking_policy", newPrefValue);
      Services.prefs.clearUserPref(oldPrefName);
    }

    if (existingDataVersion < 96) {
      const oldPrefName = "browser.urlbar.openViewOnFocus";
      const oldPrefValue = Services.prefs.getBoolPref(oldPrefName, true);
      Services.prefs.setBoolPref(
        "browser.urlbar.suggest.topsites",
        oldPrefValue
      );
      Services.prefs.clearUserPref(oldPrefName);
    }

    if (existingDataVersion < 97) {
      let userCustomizedWheelMax = Services.prefs.prefHasUserValue(
        "general.smoothScroll.mouseWheel.durationMaxMS"
      );
      let userCustomizedWheelMin = Services.prefs.prefHasUserValue(
        "general.smoothScroll.mouseWheel.durationMinMS"
      );

      if (!userCustomizedWheelMin && !userCustomizedWheelMax) {
        // If the user has an existing profile but hasn't customized the wheel
        // animation duration, they will now get the new default values. This
        // condition used to set a migrationPercent pref to 0, so that users
        // upgrading an older profile would gradually have their wheel animation
        // speed migrated to the new values. However, that "gradual migration"
        // was phased out by FF 86, so we don't need to set that pref anymore.
      } else if (userCustomizedWheelMin && !userCustomizedWheelMax) {
        // If they customized just one of the two, save the old value for the
        // other one as well, because the two values go hand-in-hand and we
        // don't want to move just one to a new value and leave the other one
        // at a customized value. In both of these cases, we leave the "migration
        // complete" percentage at 100, because they have customized this and
        // don't need any further migration.
        Services.prefs.setIntPref(
          "general.smoothScroll.mouseWheel.durationMaxMS",
          400
        );
      } else if (!userCustomizedWheelMin && userCustomizedWheelMax) {
        // Same as above case, but for the other pref.
        Services.prefs.setIntPref(
          "general.smoothScroll.mouseWheel.durationMinMS",
          200
        );
      } else {
        // The last remaining case is if they customized both values, in which
        // case also don't need to do anything; the user's customized values
        // will be retained and respected.
      }
    }

    if (existingDataVersion < 98) {
      Services.prefs.clearUserPref("browser.search.cohort");
    }

    if (existingDataVersion < 99) {
      Services.prefs.clearUserPref("security.tls.version.enable-deprecated");
    }

    if (existingDataVersion < 102) {
      // In Firefox 83, we moved to a dynamic button, so it needs to be removed
      // from default placement. This is done early enough that it doesn't
      // impact adding new managed bookmarks.
      const { CustomizableUI } = ChromeUtils.importESModule(
        "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs"
      );
      CustomizableUI.removeWidgetFromArea("managed-bookmarks");
    }

    // We have to rerun these because we had to use 102 on beta.
    // They were 101 and 102 before.
    if (existingDataVersion < 103) {
      // Set a pref if the bookmarks toolbar was already visible,
      // so we can keep it visible when navigating away from newtab
      let bookmarksToolbarWasVisible =
        Services.xulStore.getValue(
          BROWSER_DOCURL,
          "PersonalToolbar",
          "collapsed"
        ) == "false";
      if (bookmarksToolbarWasVisible) {
        // Migrate the user to the "always visible" value. See firefox.js for
        // the other possible states.
        Services.prefs.setCharPref(
          "browser.toolbars.bookmarks.visibility",
          "always"
        );
      }
      Services.xulStore.removeValue(
        BROWSER_DOCURL,
        "PersonalToolbar",
        "collapsed"
      );

      Services.prefs.clearUserPref(
        "browser.livebookmarks.migrationAttemptsLeft"
      );
    }

    // For existing profiles, continue putting bookmarks in the
    // "other bookmarks" folder.
    if (existingDataVersion < 104) {
      Services.prefs.setCharPref(
        "browser.bookmarks.defaultLocation",
        "unfiled"
      );
    }

    // Renamed and flipped the logic of a pref to make its purpose more clear.
    if (existingDataVersion < 105) {
      const oldPrefName = "browser.urlbar.imeCompositionClosesPanel";
      const oldPrefValue = Services.prefs.getBoolPref(oldPrefName, true);
      Services.prefs.setBoolPref(
        "browser.urlbar.keepPanelOpenDuringImeComposition",
        !oldPrefValue
      );
      Services.prefs.clearUserPref(oldPrefName);
    }

    if (existingDataVersion < 107) {
      // Migrate old http URIs for mailto handlers to their https equivalents.
      // The handler service will do this. We need to wait with migrating
      // until the handler service has started up, so just set a pref here.
      const kPref = "browser.handlers.migrations";
      // We might have set up another migration further up. Create an array,
      // and drop empty strings resulting from the `split`:
      let migrations = Services.prefs
        .getCharPref(kPref, "")
        .split(",")
        .filter(x => !!x);
      migrations.push("secure-mail");
      Services.prefs.setCharPref(kPref, migrations.join(","));
    }

    if (existingDataVersion < 108) {
      // Migrate old ctrlTab pref to new ctrlTab pref
      let defaultValue = false;
      let oldPrefName = "browser.ctrlTab.recentlyUsedOrder";
      let oldPrefDefault = true;
      // Use old pref value if the user used Ctrl+Tab before, elsewise use new default value
      if (Services.prefs.getBoolPref("browser.engagement.ctrlTab.has-used")) {
        let newPrefValue = Services.prefs.getBoolPref(
          oldPrefName,
          oldPrefDefault
        );
        Services.prefs.setBoolPref(
          "browser.ctrlTab.sortByRecentlyUsed",
          newPrefValue
        );
      } else {
        Services.prefs.setBoolPref(
          "browser.ctrlTab.sortByRecentlyUsed",
          defaultValue
        );
      }
    }

    if (existingDataVersion < 109) {
      // Migrate old pref to new pref
      if (
        Services.prefs.prefHasUserValue("signon.recipes.remoteRecipesEnabled")
      ) {
        // Fetch the previous value of signon.recipes.remoteRecipesEnabled and assign it to signon.recipes.remoteRecipes.enabled.
        Services.prefs.setBoolPref(
          "signon.recipes.remoteRecipes.enabled",
          Services.prefs.getBoolPref(
            "signon.recipes.remoteRecipesEnabled",
            true
          )
        );
        //Then clear user pref
        Services.prefs.clearUserPref("signon.recipes.remoteRecipesEnabled");
      }
    }

    if (existingDataVersion < 120) {
      // Migrate old titlebar bool pref to new int-based one.
      const oldPref = "browser.tabs.drawInTitlebar";
      const newPref = "browser.tabs.inTitlebar";
      if (Services.prefs.prefHasUserValue(oldPref)) {
        // We may have int prefs for builds between bug 1736518 and bug 1739539.
        const oldPrefType = Services.prefs.getPrefType(oldPref);
        if (oldPrefType == Services.prefs.PREF_BOOL) {
          Services.prefs.setIntPref(
            newPref,
            Services.prefs.getBoolPref(oldPref) ? 1 : 0
          );
        } else {
          Services.prefs.setIntPref(
            newPref,
            Services.prefs.getIntPref(oldPref)
          );
        }
        Services.prefs.clearUserPref(oldPref);
      }
    }

    if (existingDataVersion < 121) {
      // Migrate stored uris and convert them to use hashed keys
      this._migrateHashedKeysForXULStoreForDocument(BROWSER_DOCURL);
      this._migrateHashedKeysForXULStoreForDocument(
        "chrome://browser/content/places/bookmarksSidebar.xhtml"
      );
      this._migrateHashedKeysForXULStoreForDocument(
        "chrome://browser/content/places/historySidebar.xhtml"
      );
    }

    if (existingDataVersion < 122) {
      // Migrate xdg-desktop-portal pref from old to new prefs.
      try {
        const oldPref = "widget.use-xdg-desktop-portal";
        if (Services.prefs.getBoolPref(oldPref)) {
          Services.prefs.setIntPref(
            "widget.use-xdg-desktop-portal.file-picker",
            1
          );
          Services.prefs.setIntPref(
            "widget.use-xdg-desktop-portal.mime-handler",
            1
          );
        }
        Services.prefs.clearUserPref(oldPref);
      } catch (ex) {}
    }

    // Bug 1745248: Due to multiple backouts, do not use UI Version 123
    // as this version is most likely set for the Nightly channel

    if (existingDataVersion < 124) {
      // Migrate "extensions.formautofill.available" and
      // "extensions.formautofill.creditCards.available" from old to new prefs
      const oldFormAutofillModule = "extensions.formautofill.available";
      const oldCreditCardsAvailable =
        "extensions.formautofill.creditCards.available";
      const newCreditCardsAvailable =
        "extensions.formautofill.creditCards.supported";
      const newAddressesAvailable =
        "extensions.formautofill.addresses.supported";
      if (Services.prefs.prefHasUserValue(oldFormAutofillModule)) {
        let moduleAvailability = Services.prefs.getCharPref(
          oldFormAutofillModule
        );
        if (moduleAvailability == "on") {
          Services.prefs.setCharPref(newAddressesAvailable, moduleAvailability);
          Services.prefs.setCharPref(
            newCreditCardsAvailable,
            Services.prefs.getBoolPref(oldCreditCardsAvailable) ? "on" : "off"
          );
        }

        if (moduleAvailability == "off") {
          Services.prefs.setCharPref(
            newCreditCardsAvailable,
            moduleAvailability
          );
          Services.prefs.setCharPref(newAddressesAvailable, moduleAvailability);
        }
      }

      // after migrating, clear old prefs so we can remove them later.
      Services.prefs.clearUserPref(oldFormAutofillModule);
      Services.prefs.clearUserPref(oldCreditCardsAvailable);
    }

    if (existingDataVersion < 125) {
      // Bug 1756243 - Clear PiP cached coordinates since we changed their
      // coordinate space.
      const PIP_PLAYER_URI =
        "chrome://global/content/pictureinpicture/player.xhtml";
      try {
        for (let value of ["left", "top", "width", "height"]) {
          Services.xulStore.removeValue(
            PIP_PLAYER_URI,
            "picture-in-picture",
            value
          );
        }
      } catch (ex) {
        console.error("Failed to clear XULStore PiP values: ", ex);
      }
    }

    function migrateXULAttributeToStyle(url, id, attr) {
      try {
        let value = Services.xulStore.getValue(url, id, attr);
        if (value) {
          Services.xulStore.setValue(url, id, "style", `${attr}: ${value}px;`);
        }
      } catch (ex) {
        console.error(`Error migrating ${id}'s ${attr} value: `, ex);
      }
    }

    // Bug 1792748 used version 129 with a buggy variant of the sidebar width
    // migration. This version is already in use in the nightly channel, so it
    // shouldn't be used.

    // Bug 1793366: migrate sidebar persisted attribute from width to style.
    if (existingDataVersion < 130) {
      migrateXULAttributeToStyle(BROWSER_DOCURL, "sidebar-box", "width");
    }

    // Migration 131 was moved to 133 to allow for an uplift.

    if (existingDataVersion < 132) {
      // These attributes are no longer persisted, thus remove them from xulstore.
      for (let url of [
        "chrome://browser/content/places/bookmarkProperties.xhtml",
        "chrome://browser/content/places/bookmarkProperties2.xhtml",
      ]) {
        for (let attr of ["width", "screenX", "screenY"]) {
          xulStore.removeValue(url, "bookmarkproperties", attr);
        }
      }
    }

    if (existingDataVersion < 133) {
      xulStore.removeValue(BROWSER_DOCURL, "urlbar-container", "width");
    }

    // Migration 134 was removed because it was no longer necessary.

    if (existingDataVersion < 135 && AppConstants.platform == "linux") {
      // Avoid changing titlebar setting for users that used to had it off.
      try {
        if (!Services.prefs.prefHasUserValue("browser.tabs.inTitlebar")) {
          let de = Services.appinfo.desktopEnvironment;
          let oldDefault = de.includes("gnome") || de.includes("pantheon");
          if (!oldDefault) {
            Services.prefs.setIntPref("browser.tabs.inTitlebar", 0);
          }
        }
      } catch (e) {
        console.error("Error migrating tabsInTitlebar setting", e);
      }
    }

    if (existingDataVersion < 136) {
      migrateXULAttributeToStyle(
        "chrome://browser/content/places/places.xhtml",
        "placesList",
        "width"
      );
    }

    if (existingDataVersion < 137) {
      // The default value for enabling smooth scrolls is now false if the
      // user prefers reduced motion. If the value was previously set, do
      // not reset it, but if it was not explicitly set preserve the old
      // default value.
      if (
        !Services.prefs.prefHasUserValue("general.smoothScroll") &&
        Services.appinfo.prefersReducedMotion
      ) {
        Services.prefs.setBoolPref("general.smoothScroll", true);
      }
    }

    if (existingDataVersion < 138) {
      // Bug 1757297: Change scheme of all existing 'https-only-load-insecure'
      // permissions with https scheme to http scheme.
      try {
        Services.perms
          .getAllByTypes(["https-only-load-insecure"])
          .filter(permission => permission.principal.schemeIs("https"))
          .forEach(permission => {
            const capability = permission.capability;
            const uri = permission.principal.URI.mutate()
              .setScheme("http")
              .finalize();
            const principal =
              Services.scriptSecurityManager.createContentPrincipal(uri, {});
            Services.perms.removePermission(permission);
            Services.perms.addFromPrincipal(
              principal,
              "https-only-load-insecure",
              capability
            );
          });
      } catch (e) {
        console.error("Error migrating https-only-load-insecure permission", e);
      }
    }

    if (existingDataVersion < 139) {
      // Reset the default permissions to ALLOW_ACTION to rollback issues for
      // affected users, see Bug 1579517
      // originInfo in the format [origin, type]
      [
        ["https://www.mozilla.org", "uitour"],
        ["https://support.mozilla.org", "uitour"],
        ["about:home", "uitour"],
        ["about:newtab", "uitour"],
        ["https://addons.mozilla.org", "install"],
        ["https://support.mozilla.org", "remote-troubleshooting"],
        ["about:welcome", "autoplay-media"],
      ].forEach(originInfo => {
        // Reset permission on the condition that it is set to
        // UNKNOWN_ACTION, we want to prevent resetting user
        // manipulated permissions
        if (
          Services.perms.UNKNOWN_ACTION ==
          Services.perms.testPermissionFromPrincipal(
            Services.scriptSecurityManager.createContentPrincipalFromOrigin(
              originInfo[0]
            ),
            originInfo[1]
          )
        ) {
          // Adding permissions which have default values does not create
          // new permissions, but rather remove the UNKNOWN_ACTION permission
          // overrides. User's not affected by Bug 1579517 will not be affected by this addition.
          Services.perms.addFromPrincipal(
            Services.scriptSecurityManager.createContentPrincipalFromOrigin(
              originInfo[0]
            ),
            originInfo[1],
            Services.perms.ALLOW_ACTION
          );
        }
      });
    }

    if (existingDataVersion < 140) {
      // Remove browser.fixup.alternate.enabled pref in Bug 1850902.
      Services.prefs.clearUserPref("browser.fixup.alternate.enabled");
    }

    if (existingDataVersion < 141) {
      for (const filename of ["signons.sqlite", "signons.sqlite.corrupt"]) {
        const filePath = PathUtils.join(PathUtils.profileDir, filename);
        IOUtils.remove(filePath, { ignoreAbsent: true }).catch(console.error);
      }
    }

    if (existingDataVersion < 142) {
      // Bug 1860392 - Remove incorrectly persisted theming values from sidebar style.
      try {
        let value = xulStore.getValue(BROWSER_DOCURL, "sidebar-box", "style");
        if (value) {
          // Remove custom properties.
          value = value
            .split(";")
            .filter(v => !v.trim().startsWith("--"))
            .join(";");
          xulStore.setValue(BROWSER_DOCURL, "sidebar-box", "style", value);
        }
      } catch (ex) {
        console.error(ex);
      }
    }

    if (existingDataVersion < 143) {
      // Version 143 has been superseded by version 145 below.
    }

    if (existingDataVersion < 144) {
      // TerminatorTelemetry was removed in bug 1879136. Before it was removed,
      // the ShutdownDuration.json file would be written to disk at shutdown
      // so that the next launch of the browser could read it in and send
      // shutdown performance measurements.
      //
      // Unfortunately, this mechanism and its measurements were fairly
      // unreliable, so they were removed.
      for (const filename of [
        "ShutdownDuration.json",
        "ShutdownDuration.json.tmp",
      ]) {
        const filePath = PathUtils.join(PathUtils.profileDir, filename);
        IOUtils.remove(filePath, { ignoreAbsent: true }).catch(console.error);
      }
    }

    if (existingDataVersion < 145) {
      if (AppConstants.platform == "win") {
        // In Firefox 122, we enabled the firefox and firefox-private protocols.
        // We switched over to using firefox-bridge and firefox-private-bridge,
        // but we want to clean up the use of the other protocols.
        lazy.FirefoxBridgeExtensionUtils.maybeDeleteBridgeProtocolRegistryEntries(
          lazy.FirefoxBridgeExtensionUtils.OLD_PUBLIC_PROTOCOL,
          lazy.FirefoxBridgeExtensionUtils.OLD_PRIVATE_PROTOCOL
        );

        // Clean up the old user prefs from FX 122
        Services.prefs.clearUserPref(
          "network.protocol-handler.external.firefox"
        );
        Services.prefs.clearUserPref(
          "network.protocol-handler.external.firefox-private"
        );

        // In Firefox 126, we switched over to using native messaging so the
        // protocols are no longer necessary even in firefox-bridge and
        // firefox-private-bridge form
        lazy.FirefoxBridgeExtensionUtils.maybeDeleteBridgeProtocolRegistryEntries(
          lazy.FirefoxBridgeExtensionUtils.PUBLIC_PROTOCOL,
          lazy.FirefoxBridgeExtensionUtils.PRIVATE_PROTOCOL
        );
        Services.prefs.clearUserPref(
          "network.protocol-handler.external.firefox-bridge"
        );
        Services.prefs.clearUserPref(
          "network.protocol-handler.external.firefox-private-bridge"
        );
        Services.prefs.clearUserPref("browser.shell.customProtocolsRegistered");
      }
    }

    let hasRun146Migration = false;
    if (existingDataVersion < 147) {
      // Directly copy old os auth pref values prior to 147 to the new boolean
      // pref for OS auth (UI migration 156). Previously, this migration version
      // migrated the old OS auth pref values to an encrypted string pref.
      const prevOsAuthForCc = !Services.prefs.getBoolPref(
        "signon.management.page.os-auth.enabled",
        false
      );
      const newOSAuthNameForCc =
        "extensions.formautofill.creditCards.os-auth.locked.enabled";
      Services.prefs.setBoolPref(newOSAuthNameForCc, prevOsAuthForCc);
      Services.prefs.lockPref(newOSAuthNameForCc);

      const prevOsAuthForPw = !Services.prefs.getBoolPref(
        "extensions.formautofill.reauth.enabled",
        false
      );
      const newOSAuthNameForPw =
        "signon.management.page.os-auth.locked.enabled";
      Services.prefs.setBoolPref(newOSAuthNameForPw, prevOsAuthForPw);
      Services.prefs.lockPref(newOSAuthNameForPw);

      hasRun146Migration = true;
      Services.prefs.clearUserPref("extensions.formautofill.reauth.enabled");
      Services.prefs.clearUserPref("signon.management.page.os-auth.enabled");
    }

    if (existingDataVersion < 148) {
      // The Firefox Translations addon is now a built-in Firefox feature.
      let addonPromise;
      try {
        addonPromise = lazy.AddonManager.getAddonByID(
          "firefox-translations-addon@mozilla.org"
        );
      } catch (error) {
        // This always throws in xpcshell as the AddonManager is not initialized.
        if (!Services.env.exists("XPCSHELL_TEST_PROFILE_DIR")) {
          console.error(
            "Could not access the AddonManager to upgrade the profile."
          );
        }
      }
      addonPromise?.then(addon => addon?.uninstall()).catch(console.error);
    }

    if (existingDataVersion < 149) {
      // remove permissions used by deleted nsContentManager
      [
        "other",
        "script",
        "image",
        "stylesheet",
        "object",
        "document",
        "subdocument",
        "refresh",
        "xbl",
        "ping",
        "xmlhttprequest",
        "objectsubrequest",
        "dtd",
        "font",
        "websocket",
        "csp_report",
        "xslt",
        "beacon",
        "fetch",
        "manifest",
        "speculative",
      ].forEach(type => {
        Services.perms.removeByType(type);
      });
    }

    if (existingDataVersion < 150) {
      Services.prefs.clearUserPref("toolkit.telemetry.pioneerId");
    }

    if (existingDataVersion < 151) {
      // Existing Firefox users should have the usage reporting upload
      // preference "inherit" the general data reporting preference.
      lazy.UsageReporting.adoptDataReportingPreference();
    }

    if (
      existingDataVersion < 152 &&
      Services.prefs.getBoolPref("sidebar.revamp") &&
      !Services.prefs.getBoolPref("browser.ml.chat.enabled")
    ) {
      let tools = Services.prefs.getCharPref("sidebar.main.tools");
      if (tools?.includes("aichat")) {
        let updatedTools = tools
          .split(",")
          .filter(t => t != "aichat")
          .join(",");
        Services.prefs.setCharPref("sidebar.main.tools", updatedTools);
      }
    }

    if (
      existingDataVersion < 153 &&
      Services.prefs.getBoolPref("sidebar.revamp") &&
      !Services.prefs.prefHasUserValue("sidebar.main.tools")
    ) {
      // This pref will now be a user set branch but we want to preserve the previous
      // default value for existing sidebar.revamp users who hadn't changed it.
      Services.prefs.setCharPref(
        "sidebar.main.tools",
        "aichat,syncedtabs,history"
      );
    }

    if (existingDataVersion < 154) {
      // Remove mibbit handler.
      // The handler service will do this. We need to wait with migrating
      // until the handler service has started up, so just set a pref here.
      const kPref = "browser.handlers.migrations";
      // We might have set up another migration further up. Create an array,
      // and drop empty strings resulting from the `split`:
      let migrations = Services.prefs
        .getCharPref(kPref, "")
        .split(",")
        .filter(x => !!x);
      migrations.push("mibbit");
      Services.prefs.setCharPref(kPref, migrations.join(","));
    }

    if (existingDataVersion < 155) {
      // Remove outdated sidebar info from XULStore.
      for (const attr of [
        "checked",
        "positionend",
        "sidebarcommand",
        "style",
      ]) {
        Services.xulStore.removeValue(BROWSER_DOCURL, "sidebar-box", attr);
      }
    }

    if (existingDataVersion < 156) {
      const customBlockListEnabled = Services.prefs.getBoolPref(
        "browser.contentblocking.customBlockList.preferences.ui.enabled",
        false
      );
      if (customBlockListEnabled) {
        Services.prefs.clearUserPref(
          "browser.contentblocking.customBlockList.preferences.ui.enabled"
        );
        Services.prefs.clearUserPref("urlclassifier.trackingTable");
      }
    }

    if (existingDataVersion < 157) {
      // We've changed the 147 migration to copy the old OS auth pref values of
      // that version to the new locked boolean prefs instead, so no need to
      // perform a migration if it's already been done.
      if (!hasRun146Migration) {
        // We're moving away from string encrypted prefs for OS Authentication and
        // using locked boolean prefs instead.
        // To determine the state of the old OS Auth value, manually read these prefs.
        // This treats any non-empty-string value as "turned off", irrespective of
        // whether it correctly decrypts to the correct value, because we cannot do
        // the decryption if the primary password has not yet been provided.
        const prevOsAuthForCc = !Services.prefs.getStringPref(
          "extensions.formautofill.creditCards.reauth.optout",
          ""
        );
        const prevOsAuthForPw = !Services.prefs.getStringPref(
          "signon.management.page.os-auth.optout",
          ""
        );

        lazy.LoginHelper.setOSAuthEnabled(prevOsAuthForPw);
        lazy.FormAutofillUtils.setOSAuthEnabled(prevOsAuthForCc);

        Services.prefs.clearUserPref(
          "extensions.formautofill.creditCards.reauth.optout"
        );
        Services.prefs.clearUserPref("signon.management.page.os-auth.optout");
      }
    }

    // Nightly users who have run the migration for 157 will have had OS auth
    // set to true for everyone who has migrated and there's no way to retrieve
    // the old OS auth pref value because it was cleared in the previous
    // migration. So we force the pref to false. See bug 1974217.
    if (AppConstants.NIGHTLY_BUILD && existingDataVersion === 158) {
      lazy.LoginHelper.setOSAuthEnabled(false);
      lazy.FormAutofillUtils.setOSAuthEnabled(false);
    }

    if (existingDataVersion < 159) {
      // Bug 1979014 / bug 1980398 - autohide attribute becomes a real boolean attribute.
      let menubarWasEnabled =
        Services.xulStore.getValue(
          BROWSER_DOCURL,
          "toolbar-menubar",
          "autohide"
        ) == "false";
      if (menubarWasEnabled) {
        Services.xulStore.setValue(
          BROWSER_DOCURL,
          "toolbar-menubar",
          "autohide",
          "-moz-missing\n"
        );
      }
    }

    if (existingDataVersion < 160) {
      // Force all logins to be re-encrypted to make use of more modern crypto.
      // This pref is checked in the initialization of the LoginManagerStorage.
      Services.prefs.setBoolPref("signon.reencryptionNeeded", true);
    }

    if (existingDataVersion < 164) {
      const { PREF_BOOL, PREF_INT, PREF_STRING } = Services.prefs;
      const METHODS = {
        [PREF_BOOL]: ["getBoolPref", "setBoolPref"],
        [PREF_INT]: ["getIntPref", "setIntPref"],
        [PREF_STRING]: ["getStringPref", "setStringPref"],
      };
      const OLD_PREFIX = "browser.aiwindow.";
      for (let oldPref of Services.prefs.getChildList(OLD_PREFIX)) {
        let prefType = Services.prefs.getPrefType(oldPref);
        if (
          !Services.prefs.prefHasUserValue(oldPref) ||
          !Object.hasOwn(METHODS, prefType)
        ) {
          continue;
        }
        let newPref =
          "browser.smartwindow." + oldPref.substring(OLD_PREFIX.length);
        let [getter, setter] = METHODS[prefType];
        Services.prefs[setter](newPref, Services.prefs[getter](oldPref));
        Services.prefs.clearUserPref(oldPref);
      }
    }

    if (existingDataVersion < 166) {
      // Bug 1978550: Migrate Local Network Access permissions from old
      // "localhost" type to new "loopback-network" type.
      try {
        Services.perms.getAllByTypes(["localhost"]).forEach(permission => {
          Services.perms.removePermission(permission);
          Services.perms.addFromPrincipal(
            permission.principal,
            "loopback-network",
            permission.capability,
            permission.expireType,
            permission.expireTime
          );
        });
      } catch (e) {
        console.error("Error migrating localhost permission", e);
      }

      // Migrate permissions.default.localhost preference to
      // permissions.default.loopback-network
      try {
        const oldValue = Services.prefs.getIntPref(
          "permissions.default.localhost"
        );
        Services.prefs.setIntPref(
          "permissions.default.loopback-network",
          oldValue
        );
        Services.prefs.clearUserPref("permissions.default.localhost");
      } catch (e) {}
    }

    if (existingDataVersion < 169) {
      // Clear prefs removed by bug 2018089 and bug 2018516.
      Services.prefs.clearUserPref("widget.macos.native-anchored-menulists");
      Services.prefs.clearUserPref("widget.macos.native-anchored-select");
    }

    // Updating from 170 to 171 to trigger re-migrations of the Rusts store.
    if (existingDataVersion < 171) {
      // Force all logins to be re-migrated to the rust store.
      Services.prefs.setBoolPref("signon.rustMirror.migrationNeeded", true);
    }

    if (existingDataVersion < 172) {
      if (Services.prefs.getBoolPref("browser.smartwindow.enabled", false)) {
        Services.prefs.setBoolPref(
          "places.semanticHistory.smartwindow.featureGate",
          true
        );
      }
    }

    // Update the migration version.
    Services.prefs.setIntPref("browser.migration.version", newVersion);
  },
};
