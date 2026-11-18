/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.state

import android.os.Bundle
import androidx.fragment.app.Fragment
import java.util.UUID
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.lib.state.State
import org.mozilla.focus.autocomplete.AutocompleteAddFragment
import org.mozilla.focus.autocomplete.AutocompleteListFragment
import org.mozilla.focus.autocomplete.AutocompleteRemoveFragment
import org.mozilla.focus.autocomplete.AutocompleteSettingsFragment
import org.mozilla.focus.exceptions.ExceptionsListFragment
import org.mozilla.focus.exceptions.ExceptionsRemoveFragment
import org.mozilla.focus.fragment.CrashListFragment
import org.mozilla.focus.fragment.about.AboutFragment
import org.mozilla.focus.locale.screen.LanguageFragment
import org.mozilla.focus.settings.AboutLibrariesFragment
import org.mozilla.focus.settings.GeneralSettingsFragment
import org.mozilla.focus.settings.InstalledSearchEnginesSettingsFragment
import org.mozilla.focus.settings.ManualAddSearchEngineSettingsFragment
import org.mozilla.focus.settings.MozillaSettingsFragment
import org.mozilla.focus.settings.RemoveSearchEnginesSettingsFragment
import org.mozilla.focus.settings.SearchSettingsFragment
import org.mozilla.focus.settings.SettingsFragment
import org.mozilla.focus.settings.advanced.AdvancedSettingsFragment
import org.mozilla.focus.settings.advanced.SecretSettingsFragment
import org.mozilla.focus.settings.permissions.SitePermissionsFragment
import org.mozilla.focus.settings.permissions.permissionoptions.SitePermission
import org.mozilla.focus.settings.privacy.PrivacySecuritySettingsFragment

/**
 * Global state of the application.
 *
 * @property screen The currently displayed screen.
 * @property topSites The list of [TopSite] to display on the Home screen.
 * @property sitePermissionOptionChange A flag which reflects the state of site permission rules, whether they have been
 *   updated or not
 * @property secretSettingsEnabled A flag which reflects the state of debug secret settings
 * @property showEraseTabsCfr A flag which reflects the state erase tabs CFR
 * @property showSearchWidgetSnackbar A flag which reflects the state of search widget snackbar
 * @property showTrackingProtectionCfrForTab A map where keys are tab IDs and values indicate whether to show the
 *   Tracking Protection CFR for that tab.
 * @property showStartBrowsingTabsCfr A flag which reflects the state of start browsing CFR
 * @property isPinningSupported A nullable flag indicating whether pinning shortcuts to the home screen is supported.
 */
data class AppState(
    val screen: Screen,
    val topSites: List<TopSite> = emptyList(),
    val sitePermissionOptionChange: Boolean = false,
    val secretSettingsEnabled: Boolean = false,
    val showEraseTabsCfr: Boolean = false,
    val showSearchWidgetSnackbar: Boolean = false,
    val showTrackingProtectionCfrForTab: Map<String, Boolean> = emptyMap(),
    val showStartBrowsingTabsCfr: Boolean = false,
    val isPinningSupported: Boolean? = null,
) : State

private const val HOME_ID = "HOME"

/**
 * A group of screens that the app can display.
 *
 * Each screen has a unique [id].
 */
sealed class Screen {
    open val id = UUID.randomUUID().toString()

    /** First run onboarding. */
    object FirstRun : Screen()

    /** Second screen from new onboarding flow. */
    object OnboardingSecondScreen : Screen()

    /** Screen to show content on about:crashes */
    object CrashListScreen : Screen()

    /**
     * The home screen.
     *
     * @property accessPoint The search access point used for searches started from this screen.
     */
    data class Home(val accessPoint: String = SearchAccessPoint.DEFAULT) : Screen() {
        // When the access point changes, the id stays the same and it is treated as the same screen.
        override val id: String = HOME_ID
    }

    /**
     * Browser screen.
     *
     * @property tabId The ID of the displayed tab.
     * @property showTabs Whether to show the tabs tray.
     */
    data class Browser(
        val tabId: String,
        val showTabs: Boolean,
    ) : Screen() {
        // Whenever the showTabs property changes we want to treat this as a new screen and force
        // a navigation.
        override val id: String = "${super.id}_$showTabs}"
    }

    /** Editing the URL of a tab. */
    data class EditUrl(val tabId: String) : Screen()

    /**
     * The application is locked (and requires unlocking).
     *
     * @property bundle it is used for app navigation. If the user can unlock with success he should be redirected to a
     *   certain screen.It comes from the external intent.
     * @property homeAccessPoint The search access point of the home screen to show once unlocked.
     */
    data class Locked(
        val bundle: Bundle? = null,
        val homeAccessPoint: String = SearchAccessPoint.DEFAULT,
    ) : Screen()

    /** Screen for site permission options. */
    data class SitePermissionOptionsScreen(val sitePermission: SitePermission) : Screen()

    /** Settings screen. */
    data class Settings(val page: Page = Page.Start) : Screen() {
        /** Pages within the settings screen. */
        enum class Page(val fragmentClass: Class<out Fragment>) {
            Start(SettingsFragment::class.java),
            General(GeneralSettingsFragment::class.java),
            Privacy(PrivacySecuritySettingsFragment::class.java),
            Search(SearchSettingsFragment::class.java),
            Advanced(AdvancedSettingsFragment::class.java),
            Mozilla(MozillaSettingsFragment::class.java),
            About(AboutFragment::class.java),
            Licenses(AboutLibrariesFragment::class.java),
            Locale(LanguageFragment::class.java),
            PrivacyExceptions(ExceptionsListFragment::class.java),
            PrivacyExceptionsRemove(ExceptionsRemoveFragment::class.java),
            SitePermissions(SitePermissionsFragment::class.java),
            SecretSettings(SecretSettingsFragment::class.java),
            SearchList(InstalledSearchEnginesSettingsFragment::class.java),
            SearchRemove(RemoveSearchEnginesSettingsFragment::class.java),
            SearchAdd(ManualAddSearchEngineSettingsFragment::class.java),
            SearchAutocomplete(AutocompleteSettingsFragment::class.java),
            SearchAutocompleteList(AutocompleteListFragment::class.java),
            SearchAutocompleteAdd(AutocompleteAddFragment::class.java),
            SearchAutocompleteRemove(AutocompleteRemoveFragment::class.java),
            CrashList(CrashListFragment::class.java),
        }
    }
}
