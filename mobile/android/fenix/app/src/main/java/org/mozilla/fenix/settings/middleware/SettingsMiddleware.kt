/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.middleware

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Addons
import org.mozilla.fenix.GleanMetrics.CookieBanners
import org.mozilla.fenix.GleanMetrics.Translations
import org.mozilla.fenix.R
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.settings.data.SettingsRepository
import org.mozilla.fenix.settings.navigation.SettingsDestination
import org.mozilla.fenix.settings.navigation.SettingsNavController
import org.mozilla.fenix.settings.store.SettingsAction
import org.mozilla.fenix.settings.store.SettingsAction.DestinationChanged
import org.mozilla.fenix.settings.store.SettingsAction.ItemClicked
import org.mozilla.fenix.settings.store.SettingsAction.ItemToggled
import org.mozilla.fenix.settings.store.SettingsAction.SearchEnded
import org.mozilla.fenix.settings.store.SettingsAction.SearchQueryUpdated
import org.mozilla.fenix.settings.store.SettingsAction.SearchResultsUpdated
import org.mozilla.fenix.settings.store.SettingsAction.SearchStarted
import org.mozilla.fenix.settings.store.SettingsAction.SearchableItemsUpdated
import org.mozilla.fenix.settings.store.SettingsAction.SettingsAreLoading
import org.mozilla.fenix.settings.store.SettingsAction.SettingsLoaded
import org.mozilla.fenix.settings.store.SettingsAction.SettingsUpdateFeedbackAvailable
import org.mozilla.fenix.settings.store.SettingsItem
import org.mozilla.fenix.settings.store.SettingsState
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.GleanMetrics.Settings as SettingsMetrics

/**
 * Middleware for handling SettingsStore side effects.
 *
 * @param uiContext [Context] used for various system interactions.
 * @param components Application components.
 * @param settings Application settings.
 * @param repository Repository for building settings data.
 * @param navController Navigation controller for navigating between settings screens.
 * @param scope Coroutine scope for launching async operations.
 */
class SettingsMiddleware(
    private val uiContext: Context,
    private val components: Components,
    private val settings: Settings,
    private val repository: SettingsRepository,
    private val navController: SettingsNavController,
    private val scope: CoroutineScope,
) : Middleware<SettingsState, SettingsAction> {

    override fun invoke(
        context: MiddlewareContext<SettingsState, SettingsAction>,
        next: (SettingsAction) -> Unit,
        action: SettingsAction,
    ) {
        next(action)

        when (action) {
            is DestinationChanged -> {
                context.dispatch(SettingsAreLoading)

                loadSettings(context, action.destination)
            }

            is ItemClicked -> {
                handleItemClick(context, action.item)
            }

            is ItemToggled -> {
                handleTogglePreference(context, action.preferenceKey, action.newValue)
            }

            is SearchStarted -> {
                // The below dispatches should not be needed here
                // but they don't work nicely in SearchEnded at the moment
                context.dispatch(SearchQueryUpdated(""))
                context.dispatch(SearchResultsUpdated(emptyList()))
                context.dispatch(SearchableItemsUpdated(emptyList()))

                resetSearchResults(context)
            }

            is SearchEnded -> {
                context.dispatch(SearchQueryUpdated(""))
                context.dispatch(SearchResultsUpdated(emptyList()))
                context.dispatch(SearchableItemsUpdated(emptyList()))
            }

            is SearchQueryUpdated -> {
                handleSearching(context, action.query)
            }

            else -> {}
        }
    }

    private fun handleItemClick(
        context: MiddlewareContext<SettingsState, SettingsAction>,
        item: SettingsItem,
    ) {
        when (item) {
            is SettingsItem.SimplePreference -> {
                handleSimplePreferenceClick(context, item)
            }

            is SettingsItem.TogglePreference -> {
                // Clicking a toggle preference means actually toggling it <=> not handled here.
            }

            is SettingsItem.Category -> {
                // Categories are not clickable
                // Probably need a separate interface for just clickable items
            }
        }
    }

    private fun handleSimplePreferenceClick(
        context: MiddlewareContext<SettingsState, SettingsAction>,
        item: SettingsItem.SimplePreference,
    ) {
        // Record telemetry
        when (item.title.resId) {
            R.string.preferences_passwords_logins_and_passwords_2 -> SettingsMetrics.passwords.record()
            R.string.preferences_autofill,
            R.string.preferences_credit_cards_2,
                -> SettingsMetrics.autofill.record()

            R.string.preferences_extensions -> Addons.openAddonsInSettings.record(NoExtras())
            R.string.preference_enhanced_tracking_protection -> org.mozilla.fenix.GleanMetrics.TrackingProtection.etpSettings.record(
                NoExtras(),
            )

            R.string.preferences_translations -> Translations.action.record(
                Translations.ActionExtra("global_settings_from_preferences"),
            )
        }

        // Handle navigating to outside the settings screens.
        // We'd need new dependencies for delegating this to.
        when (item.id) {
            R.string.preferences_notifications -> {
                // Navigate to the notification settings for this app in system settings
            }

            R.string.preferences_rate -> {
                // Start the rating UX
            }

            R.string.preferences_install_local_extension -> {
                // Handled by AddonFilePicker in fragment or here?
            }

            R.string.preferences_customize_extension_collection -> {
                // Handled by dialog in fragment or here?
            }

            else -> {
                destinationMap.value[item.id]?.let {
                    navController.navigateTo(it)
                }
            }
        }
    }

    private fun handleTogglePreference(
        context: MiddlewareContext<SettingsState, SettingsAction>,
        preferenceKey: Int,
        newValue: Boolean,
    ) {
        when (preferenceKey) {
            R.string.pref_key_cookie_banner_private_mode -> {
                settings.shouldUseCookieBannerPrivateMode = newValue
                val metricTag = if (newValue) "reject_all" else "disabled"
                CookieBanners.settingChangedPmb.record(
                    CookieBanners.SettingChangedPmbExtra(
                        metricTag,
                    ),
                )

                val mode = settings.getCookieBannerHandlingPrivateMode()
                components.core.engine.settings.cookieBannerHandlingModePrivateBrowsing = mode
                components.useCases.sessionUseCases.reload()
            }

            R.string.pref_key_remote_debugging -> {
                settings.preferences.edit().putBoolean(uiContext.getString(preferenceKey), newValue).apply()
                components.core.engine.settings.remoteDebuggingEnabled = newValue
            }

            R.string.pref_key_enable_gecko_logs -> {
                settings.enableGeckoLogs = newValue
                context.dispatch(SettingsUpdateFeedbackAvailable(uiContext.getString(R.string.quit_application)))
            }

            else -> {
                settings.preferences.edit().putBoolean(uiContext.getString(preferenceKey), newValue).apply()
            }
        }

        // Reload settings to reflect changes.
        if (navController.currentDestination == SettingsDestination.SearchResults) {
            resetSearchResults(context)
        } else {
            loadSettings(context)
        }
    }

    private fun handleSearching(
        context: MiddlewareContext<SettingsState, SettingsAction>,
        query: String,
    ) {
        if (query.isBlank()) {
            return context.dispatch(SearchResultsUpdated(emptyList()))
        }

        val allItems = context.state.allSearchableItems
        val query = query.lowercase()
        val filteredItems = allItems.filter { item ->
            // Getting a string is cheap but we should not do this for every query change and every title/summary
            // We should store the allSearchableItems with the strings values already resolved.
            item.title.resolve(uiContext).lowercase().contains(query) ||
                    item.summary?.resolve(uiContext)?.lowercase()?.contains(query) == true ||
                    (item as? SettingsItem.SimplePreference)?.keywords?.contains(query) == true ||
                    (item as? SettingsItem.TogglePreference)?.keywords?.contains(query) == true
        }

        context.dispatch(SearchResultsUpdated(filteredItems))
    }

    private fun loadSettings(
        context: MiddlewareContext<SettingsState, SettingsAction>,
        destination: SettingsDestination = navController.currentDestination,
    ) = scope.launch {
        if (destination == SettingsDestination.Root) {
            context.dispatch(SettingsLoaded(repository.main))
        }
    }

    private fun resetSearchResults(
        context: MiddlewareContext<SettingsState, SettingsAction>,
    ) = scope.launch {
        val allItems = mutableListOf<SettingsItem>()
        repository.all.forEach {
            when (it) {
                is SettingsItem.Category -> allItems.addAll(it.items)
                else -> allItems.add(it)
            }
        }
        context.dispatch(SearchableItemsUpdated(allItems))

        if (context.state.searchQuery.isNotBlank()) {
            handleSearching(context, context.state.searchQuery)
        }
    }

    private val destinationMap = lazy {
        mapOf(
            R.string.preferences_search to SettingsDestination.SearchEngine,
            R.string.preferences_tabs to SettingsDestination.Tabs,
            R.string.preferences_home_2 to SettingsDestination.Home,
            R.string.preferences_customize to SettingsDestination.Customize,
            R.string.preferences_passwords_logins_and_passwords_2 to SettingsDestination.Passwords,
            R.string.preferences_autofill to SettingsDestination.Autofill,
            R.string.preferences_credit_cards_2 to SettingsDestination.Autofill,
            R.string.preferences_accessibility to SettingsDestination.Accessibility,
            R.string.preferences_language to SettingsDestination.Language,
            R.string.preferences_translations to SettingsDestination.Translations,
            R.string.preferences_private_browsing_options to SettingsDestination.PrivateBrowsing,
            R.string.preferences_https_only_title to SettingsDestination.HttpsOnly,
            R.string.preference_enhanced_tracking_protection to SettingsDestination.TrackingProtection,
            R.string.preference_doh_title to SettingsDestination.DnsOverHttps,
            R.string.preferences_site_settings to SettingsDestination.SitePermissions,
            R.string.preferences_delete_browsing_data to SettingsDestination.DeleteBrowsingData,
            R.string.preferences_delete_browsing_data_on_quit to SettingsDestination.DeleteBrowsingDataOnQuit,
            R.string.preferences_data_collection to SettingsDestination.DataChoices,
            R.string.preferences_extensions to SettingsDestination.Addons,
            R.string.preferences_link_sharing to SettingsDestination.LinkSharing,
            R.string.preferences_open_links_in_apps to SettingsDestination.OpenLinksInApps,
            R.string.preferences_downloads to SettingsDestination.Downloads,
            R.string.preferences_about to SettingsDestination.About,
        )
    }
}
