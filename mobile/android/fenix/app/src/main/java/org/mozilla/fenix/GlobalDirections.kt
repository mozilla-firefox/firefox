/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import androidx.navigation.NavDirections
import mozilla.appservices.places.BookmarkRoot
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint

/**
 * Used with [HomeActivity] global navigation to indicate which fragment is being opened.
 *
 * @property navDirections NavDirections to navigate to destination
 * @property destinationId fragment ID of the fragment being navigated to
 */
enum class GlobalDirections(val navDirections: NavDirections, val destinationId: Int) {
    Home(NavGraphDirections.actionGlobalHome(), R.id.homeFragment),
    Bookmarks(
        NavGraphDirections.actionGlobalBookmarkFragment(BookmarkRoot.Root.id),
        R.id.bookmarkFragment,
    ),
    History(
        NavGraphDirections.actionGlobalHistoryFragment(),
        R.id.historyFragment,
    ),
    Settings(
        NavGraphDirections.actionGlobalSettingsFragment(),
        R.id.settingsFragment,
    ),
    Sync(
        NavGraphDirections.actionGlobalTurnOnSync(entrypoint = FenixFxAEntryPoint.DeepLink),
        R.id.turnOnSyncFragment,
    ),
    SearchEngine(
        NavGraphDirections.actionGlobalSearchEngineFragment(),
        R.id.searchEngineFragment,
    ),
    Accessibility(
        NavGraphDirections.actionGlobalAccessibilityFragment(),
        R.id.accessibilityFragment,
    ),
    DeleteData(
        NavGraphDirections.actionGlobalDeleteBrowsingDataFragment(),
        R.id.deleteBrowsingDataFragment,
    ),
    SettingsAddonManager(
        NavGraphDirections.actionGlobalAddonsManagementFragment(),
        R.id.addonsManagementFragment,
    ),
    SettingsAIControls(
        NavGraphDirections.actionGlobalAiControlsFragment(),
        R.id.aiControlsFragment,
    ),
    SettingsLogins(
        NavGraphDirections.actionGlobalSavedLoginsAuthFragment(),
        R.id.saveLoginSettingFragment,
    ),
    SettingsTrackingProtection(
        NavGraphDirections.actionGlobalTrackingProtectionFragment(),
        R.id.trackingProtectionFragment,
    ),
    WallpaperSettings(
        NavGraphDirections.actionGlobalWallpaperSettingsFragment(),
        R.id.wallpaperSettingsFragment,
    ),
    SettingsPrivateBrowsing(
        NavGraphDirections.actionGlobalPrivateBrowsingFragment(),
        R.id.privateBrowsingFragment,
    ),
    SettingsAppIcon(
        NavGraphDirections.actionGlobalAppIconSelectionFragment(),
        R.id.appIconSelectionFragment,
    ),
}
