/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.navigation

import android.os.Parcelable
import androidx.annotation.StringRes
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.ui.Modifier
import androidx.navigation3.runtime.NavKey
import androidx.navigation3.runtime.entryProvider
import androidx.navigation3.ui.NavDisplay
import kotlinx.parcelize.Parcelize
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.navigation.SettingsDestination.About
import org.mozilla.fenix.settings.navigation.SettingsDestination.Accessibility
import org.mozilla.fenix.settings.navigation.SettingsDestination.Addons
import org.mozilla.fenix.settings.navigation.SettingsDestination.Autofill
import org.mozilla.fenix.settings.navigation.SettingsDestination.Customize
import org.mozilla.fenix.settings.navigation.SettingsDestination.DataChoices
import org.mozilla.fenix.settings.navigation.SettingsDestination.DeleteBrowsingData
import org.mozilla.fenix.settings.navigation.SettingsDestination.DeleteBrowsingDataOnQuit
import org.mozilla.fenix.settings.navigation.SettingsDestination.DnsOverHttps
import org.mozilla.fenix.settings.navigation.SettingsDestination.Downloads
import org.mozilla.fenix.settings.navigation.SettingsDestination.Home
import org.mozilla.fenix.settings.navigation.SettingsDestination.HttpsOnly
import org.mozilla.fenix.settings.navigation.SettingsDestination.Language
import org.mozilla.fenix.settings.navigation.SettingsDestination.LinkSharing
import org.mozilla.fenix.settings.navigation.SettingsDestination.NimbusExperiments
import org.mozilla.fenix.settings.navigation.SettingsDestination.OpenLinksInApps
import org.mozilla.fenix.settings.navigation.SettingsDestination.Passwords
import org.mozilla.fenix.settings.navigation.SettingsDestination.PrivateBrowsing
import org.mozilla.fenix.settings.navigation.SettingsDestination.Root
import org.mozilla.fenix.settings.navigation.SettingsDestination.SearchEngine
import org.mozilla.fenix.settings.navigation.SettingsDestination.SearchResults
import org.mozilla.fenix.settings.navigation.SettingsDestination.SecretDebugInfo
import org.mozilla.fenix.settings.navigation.SettingsDestination.SecretSettings
import org.mozilla.fenix.settings.navigation.SettingsDestination.SitePermissions
import org.mozilla.fenix.settings.navigation.SettingsDestination.SyncDebug
import org.mozilla.fenix.settings.navigation.SettingsDestination.Tabs
import org.mozilla.fenix.settings.navigation.SettingsDestination.TrackingProtection
import org.mozilla.fenix.settings.navigation.SettingsDestination.Translations

/**
 * All possible destinations in the Settings navigation graph.
 * Each destination implements [NavKey] for type-safe navigation with Navigation 3.
 */
@Parcelize
sealed class SettingsDestination(@param:StringRes open val title: Int) : NavKey, Parcelable {
    data object SearchResults : SettingsDestination(R.string.settings)

    data object Root : SettingsDestination(R.string.settings)
    data object SearchEngine : SettingsDestination(R.string.preferences_search)
    data object Tabs : SettingsDestination(R.string.preferences_tabs)
    data object Home : SettingsDestination(R.string.preferences_home_2)
    data object Customize : SettingsDestination(R.string.preferences_customize)
    data object Passwords : SettingsDestination(R.string.preferences_passwords_logins_and_passwords_2)
    data object Autofill : SettingsDestination(R.string.preferences_autofill)
    data object Accessibility : SettingsDestination(R.string.preferences_accessibility)
    data object Language : SettingsDestination(R.string.preferences_language)
    data object Translations : SettingsDestination(R.string.preferences_translations)
    data object PrivateBrowsing : SettingsDestination(R.string.preferences_private_browsing_options)
    data object HttpsOnly : SettingsDestination(R.string.preferences_https_only_title)
    data object TrackingProtection : SettingsDestination(R.string.preference_enhanced_tracking_protection)
    data object DnsOverHttps : SettingsDestination(R.string.preference_doh_title)
    data object SitePermissions : SettingsDestination(R.string.preferences_site_settings)
    data object DeleteBrowsingData : SettingsDestination(R.string.preferences_delete_browsing_data)
    data object DeleteBrowsingDataOnQuit : SettingsDestination(R.string.preferences_delete_browsing_data_on_quit)
    data object DataChoices : SettingsDestination(R.string.preferences_data_collection)
    data object Addons : SettingsDestination(R.string.preferences_extensions)
    data object LinkSharing : SettingsDestination(R.string.preferences_link_sharing)
    data object OpenLinksInApps : SettingsDestination(R.string.preferences_open_links_in_apps)
    data object Downloads : SettingsDestination(R.string.preferences_downloads)
    data object About : SettingsDestination(R.string.preferences_about)
    data object SecretSettings : SettingsDestination(R.string.preferences_debug_settings)
    data object SecretDebugInfo : SettingsDestination(R.string.preferences_debug_info)
    data object NimbusExperiments : SettingsDestination(R.string.preferences_nimbus_experiments)
    data object SyncDebug : SettingsDestination(R.string.preferences_sync_debug)
}

/**
 * Central back stack management for the settings screens.
 * This makes it easy to work with the Navigation 3 framework.
 *
 * @property backStack The navigation back stack as a mutable state list.
 */
class SettingsNavController(
    val backStack: SnapshotStateList<SettingsDestination> = mutableStateListOf(Root),
) {
    /**
     * Navigate to a destination by adding it to the back stack.
     *
     * @param destination The destination to navigate to.
     */
    fun navigateTo(destination: SettingsDestination) {
        backStack.add(destination)
    }

    /**
     * Navigate back by removing the last destination from the back stack.
     *
     * @return true if navigation was successful, false if already at root.
     */
    fun navigateBack(): Boolean {
        return if (backStack.size > 1) {
            backStack.removeAt(backStack.lastIndex)
            true
        } else {
            false
        }
    }

    /**
     * Pop back stack up to and including a specific destination.
     *
     * @param destination The destination to pop up to.
     * @param inclusive Whether to also remove the specified destination.
     */
    fun popUpTo(destination: SettingsDestination, inclusive: Boolean = false) {
        val index = backStack.indexOfLast { it == destination }
        if (index >= 0) {
            val removeFrom = if (inclusive) index else index + 1
            while (backStack.size > removeFrom) {
                backStack.removeAt(backStack.lastIndex)
            }
        }
    }

    /**
     * Clear the back stack and navigate to a new root destination.
     *
     * @param destination The new root destination.
     */
    fun navigateToRoot(destination: SettingsDestination = Root) {
        backStack.clear()
        backStack.add(destination)
    }

    /**
     * Check if the current destination is the root.
     */
    val isAtRoot: Boolean
        get() = backStack.size == 1

    /**
     * Get the current destination.
     */
    val currentDestination: SettingsDestination
        get() = backStack.last()
}

/**
 * Remember the settings navigation state.
 *
 * @param startDestination The initial destination for the navigation graph.
 * @return A remembered [SettingsNavController] instance.
 */
@Composable
fun rememberSettingsNavigationState(
    startDestination: SettingsDestination = Root,
): SettingsNavController {
    return remember {
        SettingsNavController(mutableStateListOf(startDestination))
    }
}

/**
 * Settings Navigation Host using Navigation 3.
 *
 * @param navigationState The navigation state holder.
 * @param modifier Modifier for the NavDisplay.
 * @param onExternalNavigation Callback for navigating outside the settings graph (e.g., to other fragments).
 * @param content Provides the screen content for each destination.
 */
@Composable
fun SettingsNavHost(
    navigationState: SettingsNavController,
    modifier: Modifier = Modifier,
    onExternalNavigation: ((Int) -> Unit)? = null,
    content: @Composable (SettingsDestination) -> Unit,
) {
    NavDisplay(
        backStack = navigationState.backStack,
        modifier = modifier,
        transitionSpec = {
            slideInHorizontally { it } togetherWith slideOutHorizontally { -it }
        },
        entryProvider = entryProvider {
            entry<SearchResults> { content(it) }
            entry<Root> { content(it) }
            entry<SearchEngine> { content(it) }
            entry<Tabs> { content(it) }
            entry<Home> { content(it) }
            entry<Customize> { content(it) }
            entry<Passwords> { content(it) }
            entry<Autofill> { content(it) }
            entry<Accessibility> { content(it) }
            entry<Language> { content(it) }
            entry<Translations> { content(it) }
            entry<PrivateBrowsing> { content(it) }
            entry<HttpsOnly> { content(it) }
            entry<TrackingProtection> { content(it) }
            entry<DnsOverHttps> { content(it) }
            entry<SitePermissions> { content(it) }
            entry<DeleteBrowsingData> { content(it) }
            entry<DeleteBrowsingDataOnQuit> { content(it) }
            entry<DataChoices> { content(it) }
            entry<Addons> { content(it) }
            entry<LinkSharing> { content(it) }
            entry<OpenLinksInApps> { content(it) }
            entry<Downloads> { content(it) }
            entry<About> { content(it) }
            entry<SecretSettings> { content(it) }
            entry<SecretDebugInfo> { content(it) }
            entry<NimbusExperiments> { content(it) }
            entry<SyncDebug> { content(it) }
        },
    )
}
