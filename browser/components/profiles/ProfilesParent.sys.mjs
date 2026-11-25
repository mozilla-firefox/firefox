/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  SelectableProfileService,
  PROFILE_THEMES_MAP,
} from "resource:///modules/profiles/SelectableProfileService.sys.mjs";
import { ProfileAge } from "resource://gre/modules/ProfileAge.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  BackupService: "moz-src:///browser/components/backup/BackupService.sys.mjs",
  EveryWindow: "resource:///modules/EveryWindow.sys.mjs",
  formAutofillStorage: "resource://autofill/FormAutofillStorage.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  PlacesDBUtils: "resource://gre/modules/PlacesDBUtils.sys.mjs",
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  getThemesList: "moz-src:///browser/themes/ThemesList.sys.mjs",
});

/**
 * Actor implementation for the profile about pages.
 */
export class ProfilesParent extends JSWindowActorParent {
  get tab() {
    const gBrowser = this.browsingContext.topChromeWindow.gBrowser;
    const tab = gBrowser.getTabForBrowser(this.browsingContext.embedderElement);
    return tab;
  }

  async #getProfileContent(isDark) {
    await SelectableProfileService.init();
    let currentProfile = SelectableProfileService.currentProfile;
    let profileAge = await ProfileAge();
    let profiles = await SelectableProfileService.getAllProfiles();
    let themes = await this.getSafeForContentThemes(isDark);
    return {
      currentProfile: await currentProfile.toContentSafeObject(),
      isInAutomation: Cu.isInAutomation,
      hasDesktopShortcut: currentProfile.hasDesktopShortcut(),
      platform: AppConstants.platform,
      profiles: await Promise.all(profiles.map(p => p.toContentSafeObject())),
      profileCreated: await profileAge.created,
      themes,
      novaEnabled: Services.prefs.getBoolPref("browser.nova.enabled", false),
    };
  }

  async receiveMessage(message) {
    let gBrowser = this.browsingContext.topChromeWindow?.gBrowser;
    let source = this.browsingContext.embedderElement?.currentURI.displaySpec;
    switch (message.name) {
      case "Profiles:DeleteProfile": {
        if (source === "about:newprofile") {
          Glean.profilesNew.closed.record({ value: "delete" });
          GleanPings.profiles.submit();
        } else if (source === "about:deleteprofile") {
          Glean.profilesDelete.confirm.record();
        }

        // Notify windows that a quit has been requested.
        if (!Services.startup.canQuit()) {
          // Something blocked our attempt to quit.
          return null;
        }

        // Since this profile will be deleted, let's make sure to update any prefs
        // that depended on its existence
        await lazy.BackupService.maybeRemoveFromEnabledListPref();

        try {
          await SelectableProfileService.deleteCurrentProfile();

          // Finally, exit.
          Services.startup.quit(Ci.nsIAppStartup.eAttemptQuit);
        } catch (e) {
          // This is expected in tests.
          console.error(e);
        }
        break;
      }
      case "Profiles:CancelDelete": {
        Glean.profilesDelete.cancel.record();
        if (gBrowser.tabs.length === 1) {
          // If the profiles tab is the only open tab,
          // open a new tab first so the browser doesn't close
          gBrowser.addTrustedTab("about:newtab");
        }
        gBrowser.removeTab(this.tab);
        break;
      }
      case "Profiles:GetNewProfileContent": {
        Glean.profilesNew.displayed.record();
        let isDark = gBrowser.selectedBrowser.documentGlobal.matchMedia(
          "(-moz-system-dark-theme)"
        ).matches;
        return this.#getProfileContent(isDark);
      }
      case "Profiles:GetEditProfileContent": {
        Glean.profilesExisting.displayed.record();
        await lazy.BackupService.init().postRecoveryComplete;
        let isDark = gBrowser.selectedBrowser.documentGlobal.matchMedia(
          "(-moz-system-dark-theme)"
        ).matches;
        return this.#getProfileContent(isDark);
      }
      case "Profiles:MoreThemes": {
        if (message.data.source === "about:editprofile") {
          Glean.profilesExisting.learnMore.record();
        } else if (message.data.source === "about:newprofile") {
          Glean.profilesNew.learnMore.record();
        }
        break;
      }
      case "Profiles:OpenDeletePage": {
        Glean.profilesExisting.deleted.record();
        this.browsingContext.embedderElement.loadURI(
          Services.io.newURI("about:deleteprofile"),
          {
            triggeringPrincipal:
              Services.scriptSecurityManager.getSystemPrincipal(),
          }
        );
        break;
      }
      case "Profiles:PageHide": {
        if (source === "about:editprofile") {
          Glean.profilesExisting.closed.record({ value: "pagehide" });
        } else if (source === "about:newprofile") {
          Glean.profilesNew.closed.record({ value: "pagehide" });
        }
        break;
      }
      case "Profiles:UpdateProfileName": {
        if (source === "about:editprofile") {
          Glean.profilesExisting.name.record();
        } else if (source === "about:newprofile") {
          Glean.profilesNew.name.record();
        }
        let profileObj = message.data;
        SelectableProfileService.currentProfile.name = profileObj.name;
        break;
      }
      case "Profiles:SetDesktopShortcut": {
        let profile = SelectableProfileService.currentProfile;
        let { shouldEnable } = message.data;
        if (shouldEnable) {
          await profile.ensureDesktopShortcut();
          Glean.profilesExisting.shortcut.record({ value: "create" });
        } else {
          await profile.removeDesktopShortcut();
          Glean.profilesExisting.shortcut.record({ value: "delete" });
        }
        return {
          hasDesktopShortcut: profile.hasDesktopShortcut(),
        };
      }
      case "Profiles:GetDeleteProfileContent": {
        // Make sure SelectableProfileService is initialized
        await SelectableProfileService.init();
        Glean.profilesDelete.displayed.record();
        let profileObj =
          await SelectableProfileService.currentProfile.toContentSafeObject();
        let windowCount = lazy.EveryWindow.readyWindows.length;
        let tabCount = lazy.EveryWindow.readyWindows
          .flatMap(win => win.gBrowser.openTabs.length)
          .reduce((total, current) => total + current);
        let loginCount = (await lazy.LoginHelper.getAllUserFacingLogins())
          .length;

        let db = await lazy.PlacesUtils.promiseDBConnection();
        let bookmarksQuery = `SELECT count(*) FROM moz_bookmarks b
                    JOIN moz_bookmarks t ON t.id = b.parent
                    AND t.parent <> :tags_folder
                    WHERE b.type = :type_bookmark`;
        let bookmarksQueryParams = {
          tags_folder: lazy.PlacesUtils.tagsFolderId,
          type_bookmark: lazy.PlacesUtils.bookmarks.TYPE_BOOKMARK,
        };
        let bookmarkCount = (
          await db.executeCached(bookmarksQuery, bookmarksQueryParams)
        )[0].getResultByIndex(0);

        let stats = await lazy.PlacesDBUtils.getEntitiesStatsAndCounts();
        let visitCount = stats.find(
          item => item.entity == "moz_historyvisits"
        ).count;
        let cookieCount = Services.cookies.cookies.length;
        let historyCount = visitCount + cookieCount;

        await lazy.formAutofillStorage.initialize();
        const [addresses, creditCards] = await Promise.all([
          lazy.formAutofillStorage.addresses.getAll(),
          lazy.formAutofillStorage.creditCards.getAll(),
        ]);
        let autofillCount = addresses.length + creditCards.length;

        return {
          profile: profileObj,
          windowCount,
          tabCount,
          bookmarkCount,
          historyCount,
          autofillCount,
          loginCount,
        };
      }
      case "Profiles:UpdateProfileAvatar": {
        let { avatarOrFile } = message.data;
        await SelectableProfileService.currentProfile.setAvatar(avatarOrFile);
        let value = SelectableProfileService.currentProfile.hasCustomAvatar
          ? "custom"
          : avatarOrFile;

        if (source === "about:editprofile") {
          Glean.profilesExisting.avatar.record({ value });
        } else if (source === "about:newprofile") {
          Glean.profilesNew.avatar.record({ value });
        }
        let profileObj =
          await SelectableProfileService.currentProfile.toContentSafeObject();
        return profileObj;
      }
      case "Profiles:UpdateProfileTheme": {
        let themeId = message.data;
        // Where the theme was installed from
        let telemetryInfo = {
          method: "url",
          source,
        };
        await this.enableTheme(themeId, telemetryInfo);
        if (source === "about:editprofile") {
          Glean.profilesExisting.theme.record({ value: themeId });
        } else if (source === "about:newprofile") {
          Glean.profilesNew.theme.record({ value: themeId });
        }

        // The enable theme promise resolves after the
        // "lightweight-theme-styling-update" observer so we know the profile
        // theme is up to date at this point.
        return SelectableProfileService.currentProfile.toContentSafeObject();
      }
      case "Profiles:RecordThemeTelemetry": {
        // Record telemetry when theme is updated via theme-picker (Nova mode)
        let themeId = message.data;
        if (source === "about:editprofile") {
          Glean.profilesExisting.theme.record({ value: themeId });
        } else if (source === "about:newprofile") {
          Glean.profilesNew.theme.record({ value: themeId });
        }
        break;
      }
      case "Profiles:CloseProfileTab": {
        if (source === "about:editprofile") {
          Glean.profilesExisting.closed.record({ value: "done_editing" });
        } else if (source === "about:newprofile") {
          Glean.profilesNew.closed.record({ value: "done_editing" });
        }
        if (gBrowser.tabs.length === 1) {
          // If the profiles tab is the only open tab,
          // open a new tab first so the browser doesn't close
          gBrowser.addTrustedTab("about:newtab");
        }
        gBrowser.removeTab(this.tab);
        // Send a trigger to ASRouter on new profile creation
        if (source === "about:newprofile") {
          await lazy.ASRouter.waitForInitialized;
          const browser = gBrowser.selectedBrowser;
          await lazy.ASRouter.sendTriggerMessage({
            browser,
            id: "selectableProfileCreated",
          });
        }

        break;
      }
    }
    return null;
  }

  async enableTheme(themeId, telemetryInfo) {
    await SelectableProfileService.enableTheme(themeId, telemetryInfo);
  }

  async getSafeForContentThemes(isDark) {
    let novaEnabled = Services.prefs.getBoolPref("browser.nova.enabled", false);

    if (novaEnabled) {
      let themesList = await lazy.getThemesList({
        installSource: "about:newprofile",
      });
      return themesList.getThemesInfo();
    }

    let lightDark = isDark ? "dark" : "light";
    let themes = [];
    for (let [themeId, themeObj] of PROFILE_THEMES_MAP) {
      if (Object.hasOwn(themeObj, "isDark") && themeObj.isDark !== isDark) {
        continue;
      }

      let theme = await lazy.AddonManager.getAddonByID(themeId);
      themes.push({
        id: themeId,
        dataL10nId: themeObj.dataL10nId,
        dataL10nTitle: themeObj.dataL10nTitle,
        isActive: theme?.isActive ?? false,
        ...themeObj.colors[lightDark],
        useInAutomation: themeObj?.useInAutomation,
      });
    }

    let activeAddons = await lazy.AddonManager.getActiveAddons(["theme"]);
    let currentTheme = activeAddons.addons[0];

    // Only add the current theme if it's not one of the default 10 themes.
    if (!themes.find(t => t.id === currentTheme.id)) {
      let safeCurrentTheme = {
        id: currentTheme.id,
        name: currentTheme.name,
        dataL10nTitle: "profiles-custom-theme-title",
        isActive: currentTheme.isActive,
        chromeColor: SelectableProfileService.currentProfile.theme.themeBg,
        toolbarColor: SelectableProfileService.currentProfile.theme.themeFg,
      };

      themes.push(safeCurrentTheme);
    }

    return themes;
  }
}
