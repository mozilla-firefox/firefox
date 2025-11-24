/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.data

import android.content.Context
import androidx.annotation.StringRes
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.Engine
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.settings.store.SettingsItem
import org.mozilla.fenix.settings.ui.TextValue
import org.mozilla.fenix.utils.Settings

/**
 * Repository for building and managing settings data.
 * This class is responsible for creating the settings structure that should be displayed in the UI.
 */
class SettingsRepository(
    private val context: Context,
    private val settings: Settings,
    private val browserStore: BrowserStore,
) {
    // Get all settings items
    val all
        get() = main/* + search + customize + advanced + etc. */

    /**
     * Current configuration of the settings items shown on the main settings screen.
     */
    val main
        get() = buildSettings {
            category(R.string.preferences_category_general) {
                simple(R.string.preferences_search)
                simple(
                    titleRes = R.string.preferences_home_2,
                    summaryRes = getHomepageSummaryRes(),
                    keywords = listOf("test"),
                )
                simple(
                    titleRes = R.string.preferences_tabs,
                    summaryRes = getTabTimeoutSummaryRes(),
                )
                simple(R.string.preferences_customize)
                simple(R.string.preferences_passwords_logins_and_passwords_2)
                simple(
                    titleRes = if (settings.addressFeature) {
                        R.string.preferences_autofill
                    } else {
                        R.string.preferences_credit_cards_2
                    },
                )
                simple(R.string.preferences_accessibility)

                simple(R.string.preferences_language)
                simple(
                    titleRes = R.string.preferences_translations,
                    isVisible = FxNimbus.features.translations.value().globalSettingsEnabled &&
                            browserStore.state.translationEngine.isEngineSupported == true,
                )
            }

            category(titleRes = R.string.preferences_category_privacy_security) {
                simple(R.string.preferences_private_browsing_options)
                simple(
                    titleRes = R.string.preferences_https_only_title,
                    summaryRes = getHttpsOnlySummaryRes(),
                )
                toggle(
                    titleRes = R.string.preferences_cookie_banner_reduction_private_mode,
                    backingPreference = R.string.pref_key_cookie_banner_private_mode,
                    isChecked = settings.shouldUseCookieBannerPrivateMode,
                    isVisible = settings.shouldShowCookieBannerUI,
                    keywords = listOf("test"),
                )
                simple(
                    titleRes = R.string.preference_enhanced_tracking_protection,
                    summaryRes = getTrackingProtectionSummaryRes(),
                )
                simple(
                    titleRes = R.string.preference_doh_title,
                    summaryRes = getDohSummaryRes(),
                    isVisible = settings.showDohEntryPoint,
                )
                simple(R.string.preferences_site_settings)
                simple(R.string.preferences_delete_browsing_data)
                simple(
                    titleRes = R.string.preferences_delete_browsing_data_on_quit,
                    summaryRes = if (settings.shouldDeleteBrowsingDataOnQuit) {
                        R.string.delete_browsing_data_quit_on
                    } else {
                        R.string.delete_browsing_data_quit_off
                    },
                )
                simple(R.string.preferences_notifications)
                simple(R.string.preferences_data_collection)
            }

            category(titleRes = R.string.preferences_category_advanced) {
                simple(R.string.preferences_extensions)
                simple(
                    titleRes = R.string.preferences_install_local_extension,
                    isVisible = settings.showSecretDebugMenuThisSession,
                )
                if (settings.overrideAmoCollection.isNotEmpty()) {
                    simple(
                        titleRes = R.string.preferences_customize_extension_collection,
                        summaryFormatArgs = listOf(settings.overrideAmoCollection),
                        isVisible = settings.amoCollectionOverrideConfigured() || settings.showSecretDebugMenuThisSession,
                    )
                } else {
                    simple(
                        titleRes = R.string.preferences_customize_extension_collection,
                        isVisible = settings.amoCollectionOverrideConfigured() || settings.showSecretDebugMenuThisSession,
                    )
                }
                simple(
                    titleRes = R.string.preferences_link_sharing,
                    isVisible = FxNimbus.features.sentFromFirefox.value().enabled,
                )
                simple(
                    titleRes = R.string.preferences_open_links_in_apps,
                    summaryRes = getOpenLinksInAppsSummaryRes(),
                )
                simple(R.string.preferences_downloads)
                toggle(
                    titleRes = R.string.preferences_remote_debugging,
                    backingPreference = R.string.pref_key_remote_debugging,
                    isChecked = settings.isRemoteDebuggingEnabled,
                )
                toggle(
                    titleRes = R.string.preferences_enable_gecko_logs,
                    backingPreference = R.string.pref_key_enable_gecko_logs,
                    isChecked = settings.enableGeckoLogs,
                    isVisible = settings.showSecretDebugMenuThisSession,
                )
            }

            category(titleRes = R.string.preferences_category_about) {
                simple(R.string.preferences_rate)
                simple(
                    titleRes = R.string.preferences_about,
                    titleFormatArgs = listOf(R.string.app_name),
                )
            }

            simple(
                titleRes = R.string.preferences_debug_settings,
                isVisible = settings.showSecretDebugMenuThisSession,
            )
            simple(
                titleRes = R.string.preferences_debug_info,
                isVisible = settings.showSecretDebugMenuThisSession,
            )
            simple(
                titleRes = R.string.preferences_nimbus_experiments,
                isVisible = settings.showSecretDebugMenuThisSession,
            )
            simple(
                titleRes = R.string.preferences_sync_debug,
                isVisible = settings.showSecretDebugMenuThisSession,
            )
        }

    @StringRes
    private fun getHomepageSummaryRes(): Int? = when {
        settings.alwaysOpenTheHomepageWhenOpeningTheApp -> R.string.opening_screen_homepage_summary
        settings.openHomepageAfterFourHoursOfInactivity -> R.string.opening_screen_after_four_hours_of_inactivity_summary
        settings.alwaysOpenTheLastTabWhenOpeningTheApp -> R.string.opening_screen_last_tab_summary
        else -> null
    }

    @StringRes
    private fun getTabTimeoutSummaryRes(): Int? = when {
        settings.closeTabsAfterOneDay -> R.string.close_tabs_after_one_day_summary
        settings.closeTabsAfterOneWeek -> R.string.close_tabs_after_one_week_summary
        settings.closeTabsAfterOneMonth -> R.string.close_tabs_after_one_month_summary
        settings.manuallyCloseTabs -> R.string.close_tabs_manually_summary
        else -> null
    }

    @StringRes
    private fun getTrackingProtectionSummaryRes(): Int = when {
        !settings.shouldUseTrackingProtection -> R.string.tracking_protection_off
        settings.useStandardTrackingProtection -> R.string.tracking_protection_standard
        settings.useStrictTrackingProtection -> R.string.tracking_protection_strict
        settings.useCustomTrackingProtection -> R.string.tracking_protection_custom
        else -> R.string.tracking_protection_standard
    }

    @StringRes
    private fun getDohSummaryRes(): Int = when (settings.getDohSettingsMode()) {
        Engine.DohSettingsMode.DEFAULT -> R.string.preference_doh_default_protection
        Engine.DohSettingsMode.OFF -> R.string.preference_doh_off
        Engine.DohSettingsMode.INCREASED -> R.string.preference_doh_increased_protection
        Engine.DohSettingsMode.MAX -> R.string.preference_doh_max_protection
    }

    @StringRes
    private fun getHttpsOnlySummaryRes(): Int? = when {
        !settings.shouldUseHttpsOnly -> R.string.preferences_https_only_off
        settings.shouldUseHttpsOnlyInAllTabs -> R.string.preferences_https_only_on_all
        settings.shouldUseHttpsOnlyInPrivateTabsOnly -> R.string.preferences_https_only_on_private
        else -> null
    }

    @StringRes
    private fun getOpenLinksInAppsSummaryRes(): Int = when (settings.openLinksInExternalApp) {
        context.getString(R.string.pref_key_open_links_in_apps_always) -> {
            if (settings.lastKnownMode == BrowsingMode.Normal) {
                R.string.preferences_open_links_in_apps_always
            } else {
                R.string.preferences_open_links_in_apps_ask
            }
        }
        context.getString(R.string.pref_key_open_links_in_apps_ask) -> {
            R.string.preferences_open_links_in_apps_ask
        }
        else -> R.string.preferences_open_links_in_apps_never
    }

    /**
     * DSL builder for creating a new settings structure for a settings screen.
     */
    private fun buildSettings(
        block: SettingsBuilder.() -> Unit,
    ) = SettingsBuilder()
        .apply(block)
        .build()
}

/**
 * DSL marker to prevent inadvertent nesting of settings scopes.
 */
@DslMarker
annotation class SettingsDslMarker

/**
 * Base scope for building settings.
 */
@SettingsDslMarker
abstract class SettingsScope {
    protected val _items = mutableListOf<SettingsItem>()
    val items: List<SettingsItem>
        get() = _items

    /**
     * Creates a simple preference with a string resource title.
     *
     * @param titleRes String resource ID for the title.
     * @param summaryFormatArgs Format arguments for the title. Can be other @StringRes or plain values.
     * @param summaryRes Optional string resource ID for the summary.
     * @param summaryFormatArgs Format arguments for the summary. Can be other @StringRes or plain values.
     * @param icon Optional icon resource ID.
     * @param isVisible Whether this item should be visible.
     * @param isEnabled Whether this item should be enabled.
     * @param id Optional navigation destination ID. Defaults to [titleRes].
     * @param keywords Optional keywords for this item used in the search functionality.
     */
    fun simple(
        @StringRes titleRes: Int,
        titleFormatArgs: List<Any> = emptyList(),
        @StringRes summaryRes: Int? = null,
        summaryFormatArgs: List<Any> = emptyList(),
        icon: Int? = null,
        isVisible: Boolean = true,
        isEnabled: Boolean = true,
        id: Int = titleRes,
        keywords: List<String> = emptyList(),
    ) {
        if (isVisible) {
            _items.add(
                SettingsItem.SimplePreference(
                    title = TextValue(titleRes, titleFormatArgs),
                    summary = summaryRes?.let { TextValue(it, summaryFormatArgs) },
                    icon = icon,
                    isVisible = true,
                    isEnabled = isEnabled,
                    id = id,
                    keywords = keywords,
                ),
            )
        }
    }

    /**
     * Creates a toggle preference with a string resource title.
     *
     * @param titleRes String resource ID for the title.
     * @param summaryFormatArgs Format arguments for the title. Can be other @StringRes or plain values.
     * @param summaryRes Optional string resource ID for the summary.
     * @param summaryFormatArgs Format arguments for the summary. Can be other @StringRes or plain values.
     * @param icon Optional icon resource ID.
     * @param backingPreference String resource ID of the preference key backing this toggle.
     * @param isChecked Whether the toggle is currently checked.
     * @param isVisible Whether this item should be visible.
     * @param isEnabled Whether this item should be enabled.
     * @param id Optional navigation destination ID. Defaults to [titleRes].
     * @param keywords Optional keywords for this item used in the search functionality.
     */
    fun toggle(
        @StringRes titleRes: Int,
        titleFormatArgs: List<Any> = emptyList(),
        @StringRes summaryRes: Int? = null,
        summaryFormatArgs: List<Any> = emptyList(),
        icon: Int? = null,
        @StringRes backingPreference: Int,
        isChecked: Boolean,
        isVisible: Boolean = true,
        isEnabled: Boolean = true,
        id: Int = titleRes,
        keywords: List<String> = emptyList(),
    ) {
        if (isVisible) {
            _items.add(
                SettingsItem.TogglePreference(
                    title = TextValue(titleRes, titleFormatArgs),
                    summary = summaryRes?.let { TextValue(it, summaryFormatArgs) },
                    icon = icon,
                    isVisible = true,
                    isEnabled = isEnabled,
                    isChecked = isChecked,
                    preferenceKey = backingPreference,
                    id = id,
                    keywords = keywords,
                ),
            )
        }
    }

    internal fun build(): List<SettingsItem> = items.toList()
}

/**
 * Category scope that groups related settings.
 */
@SettingsDslMarker
class CategoryScope : SettingsScope()

/**
 * Top-level settings builder scope.
 */
@SettingsDslMarker
class SettingsBuilder : SettingsScope() {
    /**
     * Adds a category to the settings.
     *
     * @param titleRes String resource ID for the category title.
     * @param summaryFormatArgs Format arguments for the title. Can be other @StringRes or plain values.
     * @param summaryRes Optional string resource ID for the summary.
     * @param summaryFormatArgs Format arguments for the summary. Can be other @StringRes or plain values.
     * @param icon Optional icon resource ID.
     * @param isVisible Whether this category should be visible.
     * @param isEnabled Whether this category should be enabled.
     * @param content Factory of other [SettingsItem] that should be shown in this settings category.
     */
    fun category(
        @StringRes titleRes: Int,
        titleFormatArgs: List<Any> = emptyList(),
        @StringRes summaryRes: Int? = null,
        summaryFormatArgs: List<Any> = emptyList(),
        icon: Int? = null,
        isVisible: Boolean = true,
        isEnabled: Boolean = true,
        content: CategoryScope.() -> Unit = {},
    ) {
        val categoryScope = CategoryScope().apply(content)
        _items.add(
            SettingsItem.Category(
                title = TextValue(titleRes, titleFormatArgs),
                summary = summaryRes?.let { TextValue(it, summaryFormatArgs) },
                icon = icon,
                isVisible = isVisible,
                isEnabled = isEnabled,
                items = categoryScope.items,
            ),
        )
    }
}
