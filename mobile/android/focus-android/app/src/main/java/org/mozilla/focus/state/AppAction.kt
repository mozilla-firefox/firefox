/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.state

import android.os.Bundle
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.lib.state.Action
import org.mozilla.focus.settings.permissions.permissionoptions.SitePermission

/** An [Action] to be dispatched on the [AppStore]. */
sealed class AppAction : Action {
    /** The selected tab has changed. */
    data class SelectionChanged(val tabId: String) : AppAction()

    /** Action for editing the URL of the tab with the given [tabId]. */
    data class EditAction(val tabId: String) : AppAction()

    /** All tabs have been removed. */
    object NoTabs : AppAction()

    /** The user finished editing the URL of the tab with the given [tabId]. */
    data class FinishEdit(val tabId: String) : AppAction()

    /** Hide the tabs tray. */
    object HideTabs : AppAction()

    /** The user finished the first run onboarding. */
    data class FinishFirstRun(val tabId: String?) : AppAction()

    /** The app should get locked. */
    data class Lock(val bundle: Bundle? = null) : AppAction()

    /** The app should get unlocked. */
    data class Unlock(val tabId: String?) : AppAction()

    /** Clears the search access point of the home screen, including the one to show once unlocked. */
    internal object ResetHomeAccessPoint : AppAction()

    /** Action for opening the settings screen at a specific settings [page]. */
    data class OpenSettings(val page: Screen.Settings.Page) : AppAction()

    /** Show the content of about:crashes */
    internal object OpenCrashList : AppAction()

    /** Action for opening the site permission options screen for a given [sitePermission]. */
    data class OpenSitePermissionOptionsScreen(val sitePermission: SitePermission) : AppAction()

    /** Action for navigating up, optionally specifying a [tabId]. */
    data class NavigateUp(val tabId: String? = null) : AppAction()

    /** Forces showing the first run screen. */
    internal object ShowFirstRun : AppAction()

    internal object ShowOnboardingSecondScreen : AppAction()

    /**
     * Forces showing the home screen.
     *
     * @property accessPoint The search access point used for searches started from this screen.
     */
    internal data class ShowHomeScreen(val accessPoint: String = SearchAccessPoint.DEFAULT) : AppAction()

    /** Opens the tab with the given [tabId] and actively switches to the browser screen if needed. */
    data class OpenTab(val tabId: String) : AppAction()

    /** The list of [TopSite] has changed. */
    data class TopSitesChange(val topSites: List<TopSite>) : AppAction()

    /** Site permissions autoplay rules has changed. */
    data class SitePermissionOptionChange(val value: Boolean) : AppAction()

    /** State of secret settings has changed. */
    data class SecretSettingsStateChange(val enabled: Boolean) : AppAction()

    /** State of erase tabs CFR has changed */
    data class ShowEraseTabsCfrChange(val value: Boolean) : AppAction()

    /** State of show Tracking Protection CFR has changed */
    data class ShowTrackingProtectionCfrChange(val value: Map<String, Boolean>) : AppAction()

    /** State of Snackbar for promote search widget has changed */
    data class ShowSearchWidgetSnackBar(val value: Boolean) : AppAction()

    /** State of start browsing CFR has changed */
    data class ShowStartBrowsingCfrChange(val value: Boolean) : AppAction()

    /** State of isPinningSupported has changed */
    data class UpdateIsPinningSupported(val value: Boolean) : AppAction()
}
