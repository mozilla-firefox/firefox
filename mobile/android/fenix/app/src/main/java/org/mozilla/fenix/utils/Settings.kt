/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.utils

import android.accessibilityservice.AccessibilityServiceInfo.CAPABILITY_CAN_PERFORM_GESTURES
import android.content.Context
import android.content.Context.MODE_PRIVATE
import android.content.SharedPreferences
import android.content.pm.ShortcutManager
import android.os.Build
import android.view.accessibility.AccessibilityManager
import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import androidx.core.content.edit
import androidx.lifecycle.LifecycleOwner
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.Engine.HttpsOnlyMode
import mozilla.components.concept.engine.EngineSession.CookieBannerHandlingMode
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import mozilla.components.feature.sitepermissions.SitePermissionsRules.Action
import mozilla.components.feature.sitepermissions.SitePermissionsRules.AutoplayAction
import mozilla.components.support.ktx.android.content.PreferencesHolder
import mozilla.components.support.ktx.android.content.booleanPreference
import mozilla.components.support.ktx.android.content.floatPreference
import mozilla.components.support.ktx.android.content.intPreference
import mozilla.components.support.ktx.android.content.longPreference
import mozilla.components.support.ktx.android.content.stringPreference
import mozilla.components.support.ktx.android.content.stringSetPreference
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.utils.BrowsersCache
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.Config
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.tabstrip.isTabStripEnabled
import org.mozilla.fenix.components.settings.counterPreference
import org.mozilla.fenix.components.settings.featureFlagPreference
import org.mozilla.fenix.components.settings.lazyFeatureFlagPreference
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.components.toolbar.navbar.shouldAddNavigationBar
import org.mozilla.fenix.debugsettings.addresses.SharedPrefsAddressesDebugLocalesRepository
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.TOP_SITES_MAX_COUNT
import org.mozilla.fenix.nimbus.CookieBannersSection
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.HomeScreenSection
import org.mozilla.fenix.nimbus.Mr2022Section
import org.mozilla.fenix.nimbus.QueryParameterStrippingSection
import org.mozilla.fenix.nimbus.QueryParameterStrippingSection.QUERY_PARAMETER_STRIPPING
import org.mozilla.fenix.nimbus.QueryParameterStrippingSection.QUERY_PARAMETER_STRIPPING_ALLOW_LIST
import org.mozilla.fenix.nimbus.QueryParameterStrippingSection.QUERY_PARAMETER_STRIPPING_PMB
import org.mozilla.fenix.nimbus.QueryParameterStrippingSection.QUERY_PARAMETER_STRIPPING_STRIP_LIST
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.deletebrowsingdata.DeleteBrowsingDataOnQuitType
import org.mozilla.fenix.settings.logins.SavedLoginsSortingStrategyMenu
import org.mozilla.fenix.settings.logins.SortingStrategy
import org.mozilla.fenix.settings.registerOnSharedPreferenceChangeListener
import org.mozilla.fenix.settings.sitepermissions.AUTOPLAY_BLOCK_ALL
import org.mozilla.fenix.settings.sitepermissions.AUTOPLAY_BLOCK_AUDIBLE
import org.mozilla.fenix.wallpapers.Wallpaper
import java.security.InvalidParameterException
import java.util.UUID

private const val AUTOPLAY_USER_SETTING = "AUTOPLAY_USER_SETTING"

/**
 * A simple wrapper for SharedPreferences that makes reading preference a little bit easier.
 *
 * @param appContext Reference to application context.
 */
@Suppress("LargeClass", "TooManyFunctions")
class Settings(private val appContext: Context) : PreferencesHolder {

    companion object {
        const val FENIX_PREFERENCES = "fenix_preferences"

        private const val BLOCKED_INT = 0
        private const val ASK_TO_ALLOW_INT = 1
        private const val ALLOWED_INT = 2
        private const val INACTIVE_TAB_MINIMUM_TO_SHOW_AUTO_CLOSE_DIALOG = 20

        const val FOUR_HOURS_MS = 60 * 60 * 4 * 1000L
        const val ONE_MINUTE_MS = 60 * 1000L
        const val ONE_HOUR_MS = 60 * ONE_MINUTE_MS
        const val ONE_DAY_MS = 60 * 60 * 24 * 1000L
        const val TWO_DAYS_MS = 2 * ONE_DAY_MS
        const val THREE_DAYS_MS = 3 * ONE_DAY_MS
        const val ONE_WEEK_MS = 60 * 60 * 24 * 7 * 1000L
        const val ONE_MONTH_MS = (60 * 60 * 24 * 365 * 1000L) / 12

        /**
         * The minimum number a search groups should contain.
         */
        @VisibleForTesting
        internal var SEARCH_GROUP_MINIMUM_SITES: Int = 2

        /**
         * Minimum number of days between Set as default Browser prompt displays in home page.
         */
        const val DAYS_BETWEEN_DEFAULT_BROWSER_PROMPTS: Int = 14

        /**
         * Maximum number of times the Set as default Browser prompt from home page can be displayed to the user.
         */
        const val MAX_NUMBER_OF_DEFAULT_BROWSER_PROMPTS: Int = 3

        /**
         * Number of app cold starts before displaying the Set as default Browser prompt from home page.
         */
        const val APP_COLD_STARTS_TO_SHOW_DEFAULT_PROMPT: Int = 4

        private fun Action.toInt() = when (this) {
            Action.BLOCKED -> BLOCKED_INT
            Action.ASK_TO_ALLOW -> ASK_TO_ALLOW_INT
            Action.ALLOWED -> ALLOWED_INT
        }

        private fun AutoplayAction.toInt() = when (this) {
            AutoplayAction.BLOCKED -> BLOCKED_INT
            AutoplayAction.ALLOWED -> ALLOWED_INT
        }

        private fun Int.toAction() = when (this) {
            BLOCKED_INT -> Action.BLOCKED
            ASK_TO_ALLOW_INT -> Action.ASK_TO_ALLOW
            ALLOWED_INT -> Action.ALLOWED
            else -> throw InvalidParameterException("$this is not a valid SitePermissionsRules.Action")
        }

        private fun Int.toAutoplayAction() = when (this) {
            BLOCKED_INT -> AutoplayAction.BLOCKED
            ALLOWED_INT -> AutoplayAction.ALLOWED
            // Users from older versions may have saved invalid values. Migrate them to BLOCKED
            ASK_TO_ALLOW_INT -> AutoplayAction.BLOCKED
            else -> throw InvalidParameterException("$this is not a valid SitePermissionsRules.AutoplayAction")
        }

        /**
         * DoH setting is set to "Default", corresponds to TRR_MODE_OFF (0) from GeckoView
         */
        private const val DOH_SETTINGS_DEFAULT = 0

        /**
         * DoH setting is set to "Increased", corresponds to TRR_MODE_FIRST (2) from GeckoView
         */
        private const val DOH_SETTINGS_INCREASED = 2

        /**
         * DoH setting is set to "Max", corresponds to TRR_MODE_ONLY (3) from GeckoView
         */
        private const val DOH_SETTINGS_MAX = 3

        /**
         * DoH is disabled, corresponds to TRR_MODE_DISABLED (5) from GeckoView
         */
        private const val DOH_SETTINGS_OFF = 5

        /**
         * Bug 1946867 - Currently "hardcoded" to the DoH TRR URI of Cloudflare
         */
        private const val CLOUDFLARE_URI = "https://mozilla.cloudflare-dns.com/dns-query"
    }

    @VisibleForTesting
    internal val isCrashReportEnabledInBuild: Boolean =
        BuildConfig.CRASH_REPORTING && Config.channel.isReleased

    override val preferences: SharedPreferences =
        appContext.getSharedPreferences(FENIX_PREFERENCES, MODE_PRIVATE)

    /**
     * Indicates whether or not top sites should be shown on the home screen.
     */
    var showTopSitesFeature by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_top_sites),
        featureFlag = true,
        default = { homescreenSections[HomeScreenSection.TOP_SITES] == true },
    )

    var numberOfAppLaunches by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_times_app_opened),
        default = 0,
    )

    var lastReviewPromptTimeInMillis by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_last_review_prompt_shown_time),
        default = 0L,
    )

    var lastCfrShownTimeInMillis by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_last_cfr_shown_time),
        default = 0L,
    )

    val canShowCfr: Boolean
        get() = (System.currentTimeMillis() - lastCfrShownTimeInMillis) > THREE_DAYS_MS

    var forceEnableZoom by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_accessibility_force_enable_zoom),
        default = false,
    )

    var adjustCampaignId by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_adjust_campaign),
        default = "",
    )

    var adjustNetwork by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_adjust_network),
        default = "",
    )

    var adjustAdGroup by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_adjust_adgroup),
        default = "",
    )

    var adjustCreative by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_adjust_creative),
        default = "",
    )

    var nimbusExperimentsFetched by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_nimbus_experiments_fetched),
        default = false,
    )

    var utmParamsKnown by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_utm_params_known),
        default = false,
    )

    var utmSource by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_utm_source),
        default = "",
    )

    var utmMedium by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_utm_medium),
        default = "",
    )

    var utmCampaign by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_utm_campaign),
        default = "",
    )

    var utmTerm by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_utm_term),
        default = "",
    )

    var utmContent by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_utm_content),
        default = "",
    )

    var contileContextId by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_contile_context_id),
        default = "",
    )

    var currentWallpaperName by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_current_wallpaper),
        default = Wallpaper.Default.name,
    )

    /**
     * A cache of the text color to use on text overlaying the current wallpaper.
     * The value will be `0` if the color is unavailable.
     */
    var currentWallpaperTextColor by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_current_wallpaper_text_color),
        default = 0,
    )

    /**
     * A cache of the background color to use on cards overlaying the current wallpaper when the user's
     * theme is set to Light.
     */
    var currentWallpaperCardColorLight by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_current_wallpaper_card_color_light),
        default = 0,
    )

    /**
     * A cache of the background color to use on cards overlaying the current wallpaper when the user's
     * theme is set to Dark.
     */
    var currentWallpaperCardColorDark by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_current_wallpaper_card_color_dark),
        default = 0,
    )

    /**
     * Indicates if the current legacy wallpaper should be migrated.
     */
    var shouldMigrateLegacyWallpaper by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_should_migrate_wallpaper),
        default = true,
    )

    /**
     * Indicates if the current legacy wallpaper card colors should be migrated.
     */
    var shouldMigrateLegacyWallpaperCardColors by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_should_migrate_wallpaper_card_colors),
        default = true,
    )

    /**
     * Indicates if the wallpaper onboarding dialog should be shown.
     */
    var showWallpaperOnboarding by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_wallpapers_onboarding),
        featureFlag = true,
        default = { mr2022Sections[Mr2022Section.WALLPAPERS_SELECTION_TOOL] == true },
    )

    var openLinksInAPrivateTab by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_open_links_in_a_private_tab),
        default = false,
    )

    var allowScreenshotsInPrivateMode by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_allow_screenshots_in_private_mode),
        default = false,
    )

    var privateBrowsingLockedEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_private_browsing_locked_enabled),
        default = false,
    )

    var isPrivateScreenBlocked by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_private_screen_locked),
        default = false,
    )

    var shouldReturnToBrowser by booleanPreference(
        appContext.getString(R.string.pref_key_return_to_browser),
        false,
    )

    var defaultSearchEngineName by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_search_engine),
        default = "",
    )

    var openInAppOpened by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_open_in_app_opened),
        default = false,
    )

    var installPwaOpened by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_install_pwa_opened),
        default = false,
    )

    var showCollectionsPlaceholderOnHome by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_collections_placeholder_home),
        default = true,
    )

    val isCrashReportingEnabled: Boolean
        get() = isCrashReportEnabledInBuild &&
            preferences.getBoolean(
                appContext.getPreferenceKey(R.string.pref_key_crash_reporter),
                true,
            )

    val isRemoteDebuggingEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_remote_debugging),
        default = false,
    )

    var isTelemetryEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_telemetry),
        default = true,
    )

    var isMarketingTelemetryEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_marketing_telemetry),
        default = false,
    )

    var hasMadeMarketingTelemetrySelection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_marketing_telemetry_selection_made),
        default = false,
    )

    var hasAcceptedTermsOfService by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_terms_accepted),
        default = false,
    )

    /**
     * The daily usage ping is not normally tied to normal telemetry.  We set the default value to
     * [isTelemetryEnabled] because this setting was added in early 2025 and we want to make
     * sure that users who upgrade and had telemetry disabled don't start sending the
     * daily usage ping telemetry.
     */
    var isDailyUsagePingEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_daily_usage_ping),
        default = isTelemetryEnabled,
        persistDefaultIfNotExists = true,
    )

    var isExperimentationEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_experimentation_v2),
        default = isTelemetryEnabled,
    )

    var isOverrideTPPopupsForPerformanceTest = false

    // We do not use `booleanPreference` because we only want the "read" part of this setting to be
    // controlled by a shared pref (if any). In the secret settings, there is a toggle switch to enable
    // and disable this pref. Other than that, the `SecretDebugMenuTrigger` should be able to change
    // this setting for the duration of the session only, i.e. `SecretDebugMenuTrigger` should never
    // be able to (indirectly) change the value of the shared pref.
    var showSecretDebugMenuThisSession: Boolean = false
        get() = field || preferences.getBoolean(
            appContext.getPreferenceKey(R.string.pref_key_persistent_debug_menu),
            false,
        )

    val shouldShowSecurityPinWarningSync: Boolean
        get() = loginsSecureWarningSyncCount.underMaxCount()

    val shouldShowSecurityPinWarning: Boolean
        get() = secureWarningCount.underMaxCount()

    var shouldShowPrivacyPopWindow by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_privacy_pop_window),
        default = true,
    )

    var shouldUseLightTheme by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_light_theme),
        default = false,
    )

    var shouldUseAutoSize by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_accessibility_auto_size),
        default = true,
    )

    var fontSizeFactor by floatPreference(
        appContext.getPreferenceKey(R.string.pref_key_accessibility_font_scale),
        default = 1f,
    )

    val shouldShowHistorySuggestions by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_search_browsing_history),
        default = true,
    )

    val shouldShowBookmarkSuggestions by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_search_bookmarks),
        default = true,
    )

    /**
     * Indicates if the user has enabled shortcuts in Firefox Suggest.
     */
    val shortcutSuggestionsEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_shortcuts_suggestions),
        default = true,
    )

    /**
     * Returns true if shortcut suggestions feature should be shown to the user.
     */
    var isShortcutSuggestionsVisible by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_shortcuts_suggestions),
        default = { FxNimbus.features.topSitesSuggestions.value().enabled },
        featureFlag = true,
    )

    /**
     * Returns true if shortcut suggestions should be shown to the user.
     */
    val shouldShowShortcutSuggestions: Boolean
        get() = shortcutSuggestionsEnabled && isShortcutSuggestionsVisible

    val shouldShowSyncedTabsSuggestions by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_search_synced_tabs),
        default = true,
    )

    val shouldShowClipboardSuggestions by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_clipboard_suggestions),
        default = true,
    )

    val shouldShowSearchShortcuts by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_search_engine_shortcuts),
        default = false,
    )

    var gridTabView by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tab_view_grid),
        default = true,
    )

    var manuallyCloseTabs by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_close_tabs_manually),
        default = true,
    )

    var closeTabsAfterOneDay by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_close_tabs_after_one_day),
        default = false,
    )

    var closeTabsAfterOneWeek by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_close_tabs_after_one_week),
        default = false,
    )

    var closeTabsAfterOneMonth by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_close_tabs_after_one_month),
        default = false,
    )

    var allowThirdPartyRootCerts by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_allow_third_party_root_certs),
        default = false,
    )

    var nimbusUsePreview by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_nimbus_use_preview),
        default = false,
    )

    var isFirstNimbusRun: Boolean by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_is_first_run),
        default = true,
    )

    var isFirstSplashScreenShown: Boolean by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_is_first_splash_screen_shown),
        default = false,
    )

    var nimbusLastFetchTime: Long by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_nimbus_last_fetch),
        default = 0L,
    )

    /**
     * Indicates the last time when the user was interacting with the [BrowserFragment],
     * This is useful to determine if the user has to start on the [HomeFragment]
     * or it should go directly to the [BrowserFragment].
     *
     * This value defaults to 0L because we want to know if the user never had any interaction
     * with the [BrowserFragment]
     */
    var lastBrowseActivity by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_last_browse_activity_time),
        default = 0L,
    )

    /**
     * Indicates if the user has selected the option to start on the home screen after
     * four hours of inactivity.
     */
    var openHomepageAfterFourHoursOfInactivity by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_start_on_home_after_four_hours),
        default = true,
    )

    /**
     * Indicates if the user has selected the option to always start on the home screen.
     */
    var alwaysOpenTheHomepageWhenOpeningTheApp by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_start_on_home_always),
        default = false,
    )

    /**
     * Indicates if the user has selected the option to never start on the home screen and have
     * their last tab opened.
     */
    var alwaysOpenTheLastTabWhenOpeningTheApp by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_start_on_home_never),
        default = false,
    )

    /**
     * Indicates if the user should start on the home screen, based on the user's preferences.
     */
    fun shouldStartOnHome(): Boolean {
        return when {
            openHomepageAfterFourHoursOfInactivity -> timeNowInMillis() - lastBrowseActivity >= FOUR_HOURS_MS
            alwaysOpenTheHomepageWhenOpeningTheApp -> true
            alwaysOpenTheLastTabWhenOpeningTheApp -> false
            else -> false
        }
    }

    /**
     * Indicates if the user has enabled the inactive tabs feature.
     */
    var inactiveTabsAreEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_inactive_tabs),
        default = true,
    )

    /**
     * Indicates if the user has completed successfully first translation.
     */
    var showFirstTimeTranslation: Boolean by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_first_time_translation),
        default = true,
    )

    /**
     * Indicates if the user wants translations to automatically be offered as a popup of the dialog.
     */
    var offerTranslation: Boolean by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_translations_offer),
        default = true,
    )

    @VisibleForTesting
    internal fun timeNowInMillis(): Long = System.currentTimeMillis()

    fun getTabTimeout(): Long = when {
        closeTabsAfterOneDay -> ONE_DAY_MS
        closeTabsAfterOneWeek -> ONE_WEEK_MS
        closeTabsAfterOneMonth -> ONE_MONTH_MS
        else -> Long.MAX_VALUE
    }

    enum class TabView {
        GRID, LIST
    }

    fun getTabViewPingString() = if (gridTabView) TabView.GRID.name else TabView.LIST.name

    enum class TabTimout {
        ONE_DAY, ONE_WEEK, ONE_MONTH, MANUAL
    }

    fun getTabTimeoutPingString(): String = when {
        closeTabsAfterOneDay -> {
            TabTimout.ONE_DAY.name
        }
        closeTabsAfterOneWeek -> {
            TabTimout.ONE_WEEK.name
        }
        closeTabsAfterOneMonth -> {
            TabTimout.ONE_MONTH.name
        }
        else -> {
            TabTimout.MANUAL.name
        }
    }

    fun getTabTimeoutString(): String = when {
        closeTabsAfterOneDay -> {
            appContext.getString(R.string.close_tabs_after_one_day_summary)
        }
        closeTabsAfterOneWeek -> {
            appContext.getString(R.string.close_tabs_after_one_week_summary)
        }
        closeTabsAfterOneMonth -> {
            appContext.getString(R.string.close_tabs_after_one_month_summary)
        }
        else -> {
            appContext.getString(R.string.close_tabs_manually_summary)
        }
    }

    var whatsappLinkSharingEnabled by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_link_sharing),
        featureFlag = true,
        default = { FxNimbus.features.sentFromFirefox.value().enabled },
    )

    var linkSharingSettingsSnackbarShown by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_link_sharing_settings_snackbar),
        default = false,
    )

    /**
     * Get the display string for the current open links in apps setting
     */
    fun getOpenLinksInAppsString(): String =
        when (openLinksInExternalApp) {
            appContext.getString(R.string.pref_key_open_links_in_apps_always) -> {
                if (lastKnownMode == BrowsingMode.Normal) {
                    appContext.getString(R.string.preferences_open_links_in_apps_always)
                } else {
                    appContext.getString(R.string.preferences_open_links_in_apps_ask)
                }
            }
            appContext.getString(R.string.pref_key_open_links_in_apps_ask) -> {
                appContext.getString(R.string.preferences_open_links_in_apps_ask)
            }
            else -> {
                appContext.getString(R.string.preferences_open_links_in_apps_never)
            }
        }

    var shouldUseDarkTheme by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_dark_theme),
        default = false,
    )

    var shouldFollowDeviceTheme by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_follow_device_theme),
        default = false,
    )

    var shouldUseHttpsOnly by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_https_only),
        default = false,
    )

    var shouldUseHttpsOnlyInAllTabs by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_https_only_in_all_tabs),
        default = true,
    )

    var shouldUseHttpsOnlyInPrivateTabsOnly by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_https_only_in_private_tabs),
        default = false,
    )

    var shouldUseTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection),
        default = true,
    )

    var shouldEnableGlobalPrivacyControl by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_privacy_enable_global_privacy_control),
        false,
    )

    var shouldUseCookieBannerPrivateMode by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_cookie_banner_private_mode),
        featureFlag = true,
        default = { shouldUseCookieBannerPrivateModeDefaultValue },
    )

    val shouldUseCookieBannerPrivateModeDefaultValue: Boolean
        get() = cookieBannersSection[CookieBannersSection.FEATURE_SETTING_VALUE_PBM] == 1

    val shouldUseCookieBanner: Boolean
        get() = cookieBannersSection[CookieBannersSection.FEATURE_SETTING_VALUE] == 1

    val shouldShowCookieBannerUI: Boolean
        get() = cookieBannersSection[CookieBannersSection.FEATURE_UI] == 1

    val shouldEnableCookieBannerDetectOnly: Boolean
        get() = cookieBannersSection[CookieBannersSection.FEATURE_SETTING_DETECT_ONLY] == 1

    val shouldEnableCookieBannerGlobalRules: Boolean
        get() = cookieBannersSection[CookieBannersSection.FEATURE_SETTING_GLOBAL_RULES] == 1

    val shouldEnableCookieBannerGlobalRulesSubFrame: Boolean
        get() = cookieBannersSection[CookieBannersSection.FEATURE_SETTING_GLOBAL_RULES_SUB_FRAMES] == 1

    val shouldEnableQueryParameterStripping: Boolean
        get() = queryParameterStrippingSection[QUERY_PARAMETER_STRIPPING] == "1"

    val shouldEnableQueryParameterStrippingPrivateBrowsing: Boolean
        get() = queryParameterStrippingSection[QUERY_PARAMETER_STRIPPING_PMB] == "1"

    val queryParameterStrippingAllowList: String
        get() = queryParameterStrippingSection[QUERY_PARAMETER_STRIPPING_ALLOW_LIST].orEmpty()

    val queryParameterStrippingStripList: String
        get() = queryParameterStrippingSection[QUERY_PARAMETER_STRIPPING_STRIP_LIST].orEmpty()

    /**
     * Declared as a function for performance purposes. This could be declared as a variable using
     * booleanPreference like other members of this class. However, doing so will make it so it will
     * be initialized once Settings.kt is first called, which in turn will call `isDefaultBrowserBlocking()`.
     * This will lead to a performance regression since that function can be expensive to call.
     */
    fun checkIfFenixIsDefaultBrowserOnAppResume(): Boolean {
        val prefKey = appContext.getPreferenceKey(R.string.pref_key_default_browser)
        val isDefaultBrowserNow = isDefaultBrowserBlocking()
        val wasDefaultBrowserOnLastResume =
            this.preferences.getBoolean(prefKey, isDefaultBrowserNow)
        this.preferences.edit { putBoolean(prefKey, isDefaultBrowserNow) }
        return isDefaultBrowserNow && !wasDefaultBrowserOnLastResume
    }

    /**
     * This function is "blocking" since calling this can take approx. 30-40ms (timing taken on a
     * G5+).
     */
    fun isDefaultBrowserBlocking(): Boolean {
        val browsers = BrowsersCache.all(appContext)
        return browsers.isDefaultBrowser
    }

    var reEngagementNotificationShown by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_re_engagement_notification_shown),
        default = false,
    )

    /**
     * Check if we should set the re-engagement notification.
     */
    fun shouldSetReEngagementNotification(): Boolean {
        return numberOfAppLaunches <= 1 && !reEngagementNotificationShown
    }

    /**
     * Check if we should show the re-engagement notification.
     */
    fun shouldShowReEngagementNotification(): Boolean {
        return !reEngagementNotificationShown && !isDefaultBrowserBlocking()
    }

    /**
     * Indicates if the re-engagement notification feature is enabled
     */
    var reEngagementNotificationEnabled by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_re_engagement_notification_enabled),
        default = { FxNimbus.features.reEngagementNotification.value().enabled },
        featureFlag = true,
    )

    /**
     * Indicates if the re-engagement notification feature is enabled
     */
    val reEngagementNotificationType: Int
        get() =
            FxNimbus.features.reEngagementNotification.value().type

    val shouldUseAutoBatteryTheme by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_auto_battery_theme),
        default = false,
    )

    val useStandardTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_standard_option),
        true,
    )

    val useStrictTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_strict_default),
        false,
    )

    val useCustomTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_option),
        false,
    )

    @VisibleForTesting(otherwise = PRIVATE)
    fun setStrictETP() {
        preferences.edit {
            putBoolean(
                appContext.getPreferenceKey(R.string.pref_key_tracking_protection_strict_default),
                true,
            )
        }
        preferences.edit {
            putBoolean(
                appContext.getPreferenceKey(R.string.pref_key_tracking_protection_standard_option),
                false,
            )
        }
        appContext.components.let {
            val policy = it.core.trackingProtectionPolicyFactory
                .createTrackingProtectionPolicy()
            it.useCases.settingsUseCases.updateTrackingProtection.invoke(policy)
            it.useCases.sessionUseCases.reload.invoke()
        }
    }

    val blockCookiesInCustomTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_cookies),
        true,
    )

    val useProductionRemoteSettingsServer by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_remote_server_prod),
        default = true,
    )

    val enabledTotalCookieProtection: Boolean
        get() = mr2022Sections[Mr2022Section.TCP_FEATURE] == true

    /**
     * Indicates if the total cookie protection CRF should be shown.
     */
    var shouldShowEraseActionCFR by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_erase_action_popup),
        featureFlag = true,
        default = { feltPrivateBrowsingEnabled },
    )

    /**
     * Indicates if the cookie banners CRF should be shown.
     */
    var shouldShowCookieBannersCFR by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_cookie_banners_action_popup),
        featureFlag = true,
        default = { shouldShowCookieBannerUI },
    )

    var shouldShowTabSwipeCFR by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_toolbar_tab_swipe_cfr),
        default = false,
    )

    var hasShownTabSwipeCFR by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_toolbar_has_shown_tab_swipe_cfr),
        default = false,
    )

    val blockCookiesSelectionInCustomTrackingProtection by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_cookies_select),
        default = if (enabledTotalCookieProtection) {
            appContext.getString(R.string.total_protection)
        } else {
            appContext.getString(R.string.social)
        },
    )

    val blockTrackingContentInCustomTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_tracking_content),
        true,
    )

    val blockTrackingContentSelectionInCustomTrackingProtection by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_tracking_content_select),
        appContext.getString(R.string.all),
    )

    val blockCryptominersInCustomTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_cryptominers),
        true,
    )

    val blockFingerprintersInCustomTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_fingerprinters),
        true,
    )

    val blockRedirectTrackersInCustomTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_redirect_trackers),
        true,
    )

    val blockSuspectedFingerprintersInCustomTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_suspected_fingerprinters),
        true,
    )

    val blockSuspectedFingerprintersSelectionInCustomTrackingProtection by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_suspected_fingerprinters_select),
        "private",
    )

    val blockSuspectedFingerprinters: Boolean
        get() {
            return blockSuspectedFingerprintersInCustomTrackingProtection &&
                blockSuspectedFingerprintersSelectionInCustomTrackingProtection == appContext.getString(R.string.all)
        }

    val blockSuspectedFingerprintersPrivateBrowsing: Boolean
        get() {
            return blockSuspectedFingerprintersInCustomTrackingProtection &&
                blockSuspectedFingerprintersSelectionInCustomTrackingProtection == appContext.getString(
                    R.string.private_string,
                )
        }

    /**
     * Prefer to use a fixed top toolbar when:
     * - a talkback service is enabled or
     * - switch access is enabled.
     *
     * This is automatically inferred based on the current system status. Not a setting in our app.
     */
    val shouldUseFixedTopToolbar: Boolean
        get() {
            return touchExplorationIsEnabled || switchServiceIsEnabled
        }

    var lastKnownMode: BrowsingMode = BrowsingMode.Normal
        get() {
            val lastKnownModeWasPrivate = preferences.getBoolean(
                appContext.getPreferenceKey(R.string.pref_key_last_known_mode_private),
                false,
            )

            return if (lastKnownModeWasPrivate) {
                BrowsingMode.Private
            } else {
                BrowsingMode.Normal
            }
        }
        set(value) {
            val lastKnownModeWasPrivate = (value == BrowsingMode.Private)

            preferences.edit {
                putBoolean(
                    appContext.getPreferenceKey(R.string.pref_key_last_known_mode_private),
                    lastKnownModeWasPrivate,
                )
            }

            field = value
        }

    var shouldDeleteBrowsingDataOnQuit by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_delete_browsing_data_on_quit),
        default = false,
    )

    var deleteOpenTabs by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_delete_open_tabs_now),
        default = true,
    )

    var deleteBrowsingHistory by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_delete_browsing_history_now),
        default = true,
    )

    var deleteCookies by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_delete_cookies_now),
        default = true,
    )

    var deleteCache by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_delete_caches_now),
        default = true,
    )

    var deleteSitePermissions by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_delete_permissions_now),
        default = true,
    )

    var deleteDownloads by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_delete_downloads_now),
        default = true,
    )

    var shouldUseBottomToolbar by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_toolbar_bottom),
        default = false,
        persistDefaultIfNotExists = true,
    )

    val toolbarPosition: ToolbarPosition
        get() = if (appContext.isTabStripEnabled()) {
            ToolbarPosition.TOP
        } else if (shouldUseBottomToolbar) {
            ToolbarPosition.BOTTOM
        } else {
            ToolbarPosition.TOP
        }

    /**
     * Check each active accessibility service to see if it can perform gestures, if any can,
     * then it is *likely* a switch service is enabled. We are assuming this to be the case based on #7486
     */
    val switchServiceIsEnabled: Boolean
        get() {
            val accessibilityManager =
                appContext.getSystemService(Context.ACCESSIBILITY_SERVICE) as? AccessibilityManager

            accessibilityManager?.getEnabledAccessibilityServiceList(0)?.let { activeServices ->
                for (service in activeServices) {
                    if (service.capabilities.and(CAPABILITY_CAN_PERFORM_GESTURES) == 1) {
                        return true
                    }
                }
            }

            return false
        }

    val touchExplorationIsEnabled: Boolean
        get() {
            val accessibilityManager =
                appContext.getSystemService(Context.ACCESSIBILITY_SERVICE) as? AccessibilityManager
            return accessibilityManager?.isTouchExplorationEnabled ?: false
        }

    val accessibilityServicesEnabled: Boolean
        get() {
            return touchExplorationIsEnabled || switchServiceIsEnabled
        }

    fun getDeleteDataOnQuit(type: DeleteBrowsingDataOnQuitType): Boolean =
        preferences.getBoolean(type.getPreferenceKey(appContext), false)

    fun setDeleteDataOnQuit(type: DeleteBrowsingDataOnQuitType, value: Boolean) {
        preferences.edit { putBoolean(type.getPreferenceKey(appContext), value) }
    }

    fun shouldDeleteAnyDataOnQuit() =
        DeleteBrowsingDataOnQuitType.entries.any { getDeleteDataOnQuit(it) }

    val passwordsEncryptionKeyGenerated by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_encryption_key_generated),
        false,
    )

    fun recordPasswordsEncryptionKeyGenerated() = preferences.edit {
        putBoolean(
            appContext.getPreferenceKey(R.string.pref_key_encryption_key_generated),
            true,
        )
    }

    @VisibleForTesting(otherwise = PRIVATE)
    internal val loginsSecureWarningSyncCount = counterPreference(
        appContext.getPreferenceKey(R.string.pref_key_logins_secure_warning_sync),
        maxCount = 1,
    )

    @VisibleForTesting(otherwise = PRIVATE)
    internal val secureWarningCount = counterPreference(
        appContext.getPreferenceKey(R.string.pref_key_secure_warning),
        maxCount = 1,
    )

    fun incrementSecureWarningCount() = secureWarningCount.increment()

    fun incrementShowLoginsSecureWarningSyncCount() = loginsSecureWarningSyncCount.increment()

    val shouldShowSearchSuggestions by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_search_suggestions),
        default = true,
    )

    val shouldAutocompleteInAwesomebar by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_enable_autocomplete_urls),
        default = true,
    )

    var defaultTopSitesAdded by booleanPreference(
        appContext.getPreferenceKey(R.string.default_top_sites_added),
        default = false,
    )

    var shouldShowSearchSuggestionsInPrivate by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_search_suggestions_in_private),
        default = false,
    )

    /**
     * Indicates if the user have enabled trending search in search suggestions.
     */
    @VisibleForTesting
    internal var trendingSearchSuggestionsEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_trending_search_suggestions),
        default = true,
    )

    /**
     * Indicates if the user have enabled recent search in the search suggestions setting preference.
     */
    @VisibleForTesting
    internal var recentSearchSuggestionsEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_recent_search_suggestions),
        default = true,
    )

    /**
     * Returns true if recent searches should be shown to the user.
     */
    val shouldShowRecentSearchSuggestions: Boolean
        get() = recentSearchSuggestionsEnabled && isRecentSearchesVisible

    var showSearchSuggestionsInPrivateOnboardingFinished by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_search_suggestions_in_private_onboarding),
        default = false,
    )

    fun incrementVisitedInstallableCount() = pwaInstallableVisitCount.increment()

    @VisibleForTesting(otherwise = PRIVATE)
    internal val pwaInstallableVisitCount = counterPreference(
        appContext.getPreferenceKey(R.string.pref_key_install_pwa_visits),
        maxCount = 3,
    )

    private val userNeedsToVisitInstallableSites: Boolean
        get() = pwaInstallableVisitCount.underMaxCount()

    val shouldShowPwaCfr: Boolean
        get() {
            if (!canShowCfr) return false
            // We only want to show this on the 3rd time a user visits a site
            if (userNeedsToVisitInstallableSites) return false

            // ShortcutManager::pinnedShortcuts is only available on Oreo+
            if (!userKnowsAboutPwas && Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val manager = appContext.getSystemService(ShortcutManager::class.java)
                val alreadyHavePwaInstalled = manager != null && manager.pinnedShortcuts.size > 0

                // Users know about PWAs onboarding if they already have PWAs installed.
                userKnowsAboutPwas = alreadyHavePwaInstalled
            }
            // Show dialog only if user does not know abut PWAs
            return !userKnowsAboutPwas
        }

    var userKnowsAboutPwas by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_user_knows_about_pwa),
        default = false,
    )

    var shouldShowOpenInAppBanner by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_open_in_app_banner),
        default = true,
    )

    val shouldShowOpenInAppCfr: Boolean
        get() = canShowCfr && shouldShowOpenInAppBanner

    var shouldShowAutoCloseTabsBanner by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_auto_close_tabs_banner),
        default = true,
    )

    var shouldShowInactiveTabsOnboardingPopup by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_inactive_tabs_popup),
        default = true,
    )

    /**
     * Indicates if the auto-close dialog for inactive tabs has been dismissed before.
     */
    var hasInactiveTabsAutoCloseDialogBeenDismissed by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_has_inactive_tabs_auto_close_dialog_dismissed),
        default = false,
    )

    /**
     * Indicates if the auto-close dialog should be visible based on
     * if the user has dismissed it before [hasInactiveTabsAutoCloseDialogBeenDismissed],
     * if the minimum number of tabs has been accumulated [numbersOfTabs]
     * and if the auto-close setting is already set to [closeTabsAfterOneMonth].
     */
    fun shouldShowInactiveTabsAutoCloseDialog(numbersOfTabs: Int): Boolean {
        return !hasInactiveTabsAutoCloseDialogBeenDismissed &&
            numbersOfTabs >= INACTIVE_TAB_MINIMUM_TO_SHOW_AUTO_CLOSE_DIALOG &&
            !closeTabsAfterOneMonth
    }

    /**
     *  Returns a sitePermissions action for the provided [feature].
     */
    fun getSitePermissionsPhoneFeatureAction(
        feature: PhoneFeature,
        default: Action = Action.ASK_TO_ALLOW,
    ) =
        preferences.getInt(feature.getPreferenceKey(appContext), default.toInt()).toAction()

    /**
     * Saves the user selected autoplay setting.
     *
     * Under the hood, autoplay is represented by two settings, [AUTOPLAY_AUDIBLE] and
     * [AUTOPLAY_INAUDIBLE]. The user selection cannot be inferred from the combination of these
     * settings because, while on [AUTOPLAY_ALLOW_ON_WIFI], they will be indistinguishable from
     * either [AUTOPLAY_ALLOW_ALL] or [AUTOPLAY_BLOCK_ALL]. Because of this, we are forced to save
     * the user selected setting as well.
     */
    fun setAutoplayUserSetting(
        autoplaySetting: Int,
    ) {
        preferences.edit { putInt(AUTOPLAY_USER_SETTING, autoplaySetting) }
    }

    /**
     * Gets the user selected autoplay setting.
     *
     * Under the hood, autoplay is represented by two settings, [AUTOPLAY_AUDIBLE] and
     * [AUTOPLAY_INAUDIBLE]. The user selection cannot be inferred from the combination of these
     * settings because, while on [AUTOPLAY_ALLOW_ON_WIFI], they will be indistinguishable from
     * either [AUTOPLAY_ALLOW_ALL] or [AUTOPLAY_BLOCK_ALL]. Because of this, we are forced to save
     * the user selected setting as well.
     */
    fun getAutoplayUserSetting() = preferences.getInt(AUTOPLAY_USER_SETTING, AUTOPLAY_BLOCK_AUDIBLE)

    private fun getSitePermissionsPhoneFeatureAutoplayAction(
        feature: PhoneFeature,
        default: AutoplayAction = AutoplayAction.BLOCKED,
    ) = preferences.getInt(feature.getPreferenceKey(appContext), default.toInt()).toAutoplayAction()

    /**
     *  Sets a sitePermissions action for the provided [feature].
     */
    fun setSitePermissionsPhoneFeatureAction(
        feature: PhoneFeature,
        value: Action,
    ) {
        preferences.edit { putInt(feature.getPreferenceKey(appContext), value.toInt()) }
    }

    fun getSitePermissionsCustomSettingsRules(): SitePermissionsRules {
        return SitePermissionsRules(
            notification = getSitePermissionsPhoneFeatureAction(PhoneFeature.NOTIFICATION),
            microphone = getSitePermissionsPhoneFeatureAction(PhoneFeature.MICROPHONE),
            location = getSitePermissionsPhoneFeatureAction(PhoneFeature.LOCATION),
            camera = getSitePermissionsPhoneFeatureAction(PhoneFeature.CAMERA),
            autoplayAudible = getSitePermissionsPhoneFeatureAutoplayAction(
                feature = PhoneFeature.AUTOPLAY_AUDIBLE,
                default = AutoplayAction.BLOCKED,
            ),
            autoplayInaudible = getSitePermissionsPhoneFeatureAutoplayAction(
                feature = PhoneFeature.AUTOPLAY_INAUDIBLE,
                default = AutoplayAction.ALLOWED,
            ),
            persistentStorage = getSitePermissionsPhoneFeatureAction(PhoneFeature.PERSISTENT_STORAGE),
            crossOriginStorageAccess = getSitePermissionsPhoneFeatureAction(PhoneFeature.CROSS_ORIGIN_STORAGE_ACCESS),
            mediaKeySystemAccess = getSitePermissionsPhoneFeatureAction(PhoneFeature.MEDIA_KEY_SYSTEM_ACCESS),
        )
    }

    fun setSitePermissionSettingListener(lifecycleOwner: LifecycleOwner, listener: () -> Unit) {
        val sitePermissionKeys = listOf(
            PhoneFeature.NOTIFICATION,
            PhoneFeature.MICROPHONE,
            PhoneFeature.LOCATION,
            PhoneFeature.CAMERA,
            PhoneFeature.AUTOPLAY_AUDIBLE,
            PhoneFeature.AUTOPLAY_INAUDIBLE,
            PhoneFeature.PERSISTENT_STORAGE,
            PhoneFeature.CROSS_ORIGIN_STORAGE_ACCESS,
            PhoneFeature.MEDIA_KEY_SYSTEM_ACCESS,
        ).map { it.getPreferenceKey(appContext) }

        preferences.registerOnSharedPreferenceChangeListener(lifecycleOwner) { _, key ->
            if (key in sitePermissionKeys) listener.invoke()
        }
    }

    var shouldShowVoiceSearch by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_voice_search),
        default = true,
    )

    /**
     * Used in [SearchDialogFragment.kt], [SearchFragment.kt] (deprecated), and [PairFragment.kt]
     * to see if we need to check for camera permissions before using the QR code scanner.
     */
    var shouldShowCameraPermissionPrompt by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_camera_permissions_needed),
        default = true,
    )

    /**
     * Sets the state of permissions that have been checked, where [false] denotes already checked
     * and [true] denotes needing to check. See [shouldShowCameraPermissionPrompt].
     */
    var setCameraPermissionNeededState by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_camera_permissions_needed),
        default = true,
    )

    var shouldPromptToSaveLogins by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_save_logins),
        default = true,
    )

    var shouldAutofillLogins by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_autofill_logins),
        default = true,
    )

    /**
     * Used in [SearchWidgetProvider] to update when the search widget
     * exists on home screen or if it has been removed completely.
     */
    fun setSearchWidgetInstalled(installed: Boolean) {
        val key = appContext.getPreferenceKey(R.string.pref_key_search_widget_installed_2)
        preferences.edit { putBoolean(key, installed) }
    }

    /**
     * In Bug 1853113, we changed the type of [searchWidgetInstalled] from int to boolean without
     * changing the pref key, now we have to migrate users that were using the previous type int
     * to the new one boolean. The migration will only happens if pref_key_search_widget_installed
     * is detected.
     */
    fun migrateSearchWidgetInstalledPrefIfNeeded() {
        val oldKey = "pref_key_search_widget_installed"
        val installedCount = try {
            preferences.getInt(oldKey, 0)
        } catch (e: ClassCastException) {
            0
        }

        if (installedCount > 0) {
            setSearchWidgetInstalled(true)
            preferences.edit { remove(oldKey) }
        }
    }

    val searchWidgetInstalled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_search_widget_installed_2),
        default = false,
    )

    fun incrementNumTimesPrivateModeOpened() = numTimesPrivateModeOpened.increment()

    private val numTimesPrivateModeOpened = counterPreference(
        appContext.getPreferenceKey(R.string.pref_key_private_mode_opened),
    )

    var openLinksInExternalAppOld by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_open_links_in_external_app_old),
        default = true,
    )

    /**
     * Check to see if we should open the link in an external app
     */
    fun shouldOpenLinksInApp(isCustomTab: Boolean = false): Boolean {
        return when (openLinksInExternalApp) {
            appContext.getString(R.string.pref_key_open_links_in_apps_always) -> true
            appContext.getString(R.string.pref_key_open_links_in_apps_ask) -> true
            /* Some applications will not work if custom tab never open links in apps, return true if it's custom tab */
            appContext.getString(R.string.pref_key_open_links_in_apps_never) -> isCustomTab
            else -> false
        }
    }

    /**
     * Check to see if we need to prompt the user if the link can be opened in an external app
     */
    fun shouldPromptOpenLinksInApp(): Boolean {
        return when (openLinksInExternalApp) {
            appContext.getString(R.string.pref_key_open_links_in_apps_always) -> false
            appContext.getString(R.string.pref_key_open_links_in_apps_ask) -> true
            appContext.getString(R.string.pref_key_open_links_in_apps_never) -> true
            else -> true
        }
    }

    var openLinksInExternalApp by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_open_links_in_apps),
        default = when (openLinksInExternalAppOld) {
            true -> appContext.getString(R.string.pref_key_open_links_in_apps_ask)
            false -> appContext.getString(R.string.pref_key_open_links_in_apps_never)
        },
    )

    var overrideFxAServer by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_override_fxa_server),
        default = "",
    )

    var useReactFxAServer by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_use_react_fxa),
        default = false,
    )

    var overrideSyncTokenServer by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_override_sync_tokenserver),
        default = "",
    )

    var overridePushServer by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_override_push_server),
        default = "",
    )

    var overrideAmoUser by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_override_amo_user),
        default = "",
    )

    var overrideAmoCollection by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_override_amo_collection),
        default = "",
    )

    var enableGeckoLogs by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_enable_gecko_logs),
        default = Config.channel.isDebug,
    )

    fun amoCollectionOverrideConfigured(): Boolean {
        return overrideAmoUser.isNotEmpty() || overrideAmoCollection.isNotEmpty()
    }

    var topSitesSize by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_top_sites_size),
        default = 0,
    )

    val topSitesMaxLimit by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_top_sites_max_limit),
        default = TOP_SITES_MAX_COUNT,
    )

    var openTabsCount by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_open_tabs_count),
        0,
    )

    var openPrivateTabsCount by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_open_private_tabs_count),
        0,
    )

    var mobileBookmarksSize by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_mobile_bookmarks_size),
        0,
    )

    var desktopBookmarksSize by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_desktop_bookmarks_size),
        0,
    )

    /**
     * Storing number of installed add-ons for telemetry purposes
     */
    var installedAddonsCount by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_installed_addons_count),
        0,
    )

    /**
     * Storing the list of installed add-ons for telemetry purposes
     */
    var installedAddonsList by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_installed_addons_list),
        default = "",
    )

    /**
     *  URLs from the user's history that contain this search param will be hidden.
     *  The value is a string with one of the following forms:
     * - "" (empty) - Disable this feature
     * - "key" - Search param named "key" with any or no value
     * - "key=" - Search param named "key" with no value
     * - "key=value" - Search param named "key" with value "value"
     */
    val frecencyFilterQuery by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_frecency_filter_query),
        default = "mfadid=adm", // Parameter provided by adM
    )

    /**
     * Storing number of enabled add-ons for telemetry purposes
     */
    var enabledAddonsCount by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_enabled_addons_count),
        0,
    )

    /**
     * Storing the list of enabled add-ons for telemetry purposes
     */
    var enabledAddonsList by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_enabled_addons_list),
        default = "",
    )

    private var savedLoginsSortingStrategyString by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_saved_logins_sorting_strategy),
        default = SavedLoginsSortingStrategyMenu.Item.AlphabeticallySort.strategyString,
    )

    val savedLoginsMenuHighlightedItem: SavedLoginsSortingStrategyMenu.Item
        get() = SavedLoginsSortingStrategyMenu.Item.fromString(savedLoginsSortingStrategyString)

    var savedLoginsSortingStrategy: SortingStrategy
        get() {
            return when (savedLoginsMenuHighlightedItem) {
                SavedLoginsSortingStrategyMenu.Item.AlphabeticallySort -> SortingStrategy.Alphabetically
                SavedLoginsSortingStrategyMenu.Item.LastUsedSort -> SortingStrategy.LastUsed
            }
        }
        set(value) {
            savedLoginsSortingStrategyString = when (value) {
                is SortingStrategy.Alphabetically ->
                    SavedLoginsSortingStrategyMenu.Item.AlphabeticallySort.strategyString
                is SortingStrategy.LastUsed ->
                    SavedLoginsSortingStrategyMenu.Item.LastUsedSort.strategyString
            }
        }

    var isPullToRefreshEnabledInBrowser by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_website_pull_to_refresh),
        default = true,
    )

    var isDynamicToolbarEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_dynamic_toolbar),
        default = true,
    )

    var isSwipeToolbarToSwitchTabsEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_swipe_toolbar_switch_tabs),
        default = true,
    )

    var addressFeature by featureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_address_feature),
        default = true,
        featureFlag = isAddressFeatureEnabled(appContext),
    )

    /**
     * Show the Addresses autofill feature.
     */
    private fun isAddressFeatureEnabled(context: Context): Boolean {
        val releaseEnabledLanguages = listOf(
            "en-US",
            "en-CA",
            "fr-CA",
        )
        val currentlyEnabledLanguages = if (Config.channel.isNightlyOrDebug) {
            releaseEnabledLanguages + SharedPrefsAddressesDebugLocalesRepository(context)
                .getAllEnabledLocales().map { it.langTag }
        } else {
            releaseEnabledLanguages
        }

        val userLangTag = LocaleManager.getCurrentLocale(context)
            ?.toLanguageTag() ?: LocaleManager.getSystemDefault().toLanguageTag()
        return currentlyEnabledLanguages.contains(userLangTag)
    }

    private val mr2022Sections: Map<Mr2022Section, Boolean>
        get() =
            FxNimbus.features.mr2022.value().sectionsEnabled

    private val cookieBannersSection: Map<CookieBannersSection, Int>
        get() =
            FxNimbus.features.cookieBanners.value().sectionsEnabled

    private val queryParameterStrippingSection: Map<QueryParameterStrippingSection, String>
        get() =
            FxNimbus.features.queryParameterStripping.value().sectionsEnabled

    private val homescreenSections: Map<HomeScreenSection, Boolean>
        get() =
            FxNimbus.features.homescreen.value().sectionsEnabled

    var historyMetadataUIFeature by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_history_metadata_feature),
        default = { homescreenSections[HomeScreenSection.RECENT_EXPLORATIONS] == true },
        featureFlag = true,
    )

    /**
     * Indicates if sync onboarding CFR should be shown.
     */
    var showSyncCFR by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_sync_cfr),
        featureFlag = true,
        default = { mr2022Sections[Mr2022Section.SYNC_CFR] == true },
    )

    /**
     * Indicates if home onboarding dialog should be shown.
     */
    var showHomeOnboardingDialog by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_home_onboarding_dialog),
        featureFlag = true,
        default = { mr2022Sections[Mr2022Section.HOME_ONBOARDING_DIALOG_EXISTING_USERS] == true },
    )

    /**
     * Indicates if the recent tabs functionality should be visible.
     */
    var showRecentTabsFeature by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_recent_tabs),
        featureFlag = true,
        default = { homescreenSections[HomeScreenSection.JUMP_BACK_IN] == true },
    )

    /**
     * Indicates if the recent saved bookmarks functionality should be visible.
     */
    var showBookmarksHomeFeature by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_customization_bookmarks),
        default = { homescreenSections[HomeScreenSection.BOOKMARKS] == true },
        featureFlag = true,
    )

    var signedInFxaAccount by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_fxa_signed_in),
        default = false,
    )

    /**
     * Storing the user choice from the "Payment methods" settings for whether save and autofill cards
     * should be enabled or not.
     * If set to `true` when the user focuses on credit card fields in the webpage an Android prompt letting her
     * select the card details to be automatically filled will appear.
     */
    var shouldAutofillCreditCardDetails by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_credit_cards_save_and_autofill_cards),
        default = true,
    )

    /**
     * Stores the user choice from the "Autofill Addresses" settings for whether
     * save and autofill addresses should be enabled or not.
     * If set to `true` when the user focuses on address fields in a webpage an Android prompt is shown,
     * allowing the selection of an address details to be automatically filled in the webpage fields.
     */
    var shouldAutofillAddressDetails by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_addresses_save_and_autofill_addresses),
        default = true,
    )

    /**
     * Indicates if the Pocket recommended stories homescreen section should be shown.
     */
    var showPocketRecommendationsFeature by lazyFeatureFlagPreference(
        appContext.getPreferenceKey(R.string.pref_key_pocket_homescreen_recommendations),
        featureFlag = FeatureFlags.isPocketRecommendationsFeatureEnabled(appContext),
        default = { homescreenSections[HomeScreenSection.POCKET] == true },
    )

    /**
     * Indicates if the Pocket recommendations homescreen section should also show sponsored stories.
     */
    val showPocketSponsoredStories by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_pocket_sponsored_stories),
        default = { homescreenSections[HomeScreenSection.POCKET_SPONSORED_STORIES] == true },
        featureFlag = FeatureFlags.isPocketSponsoredStoriesFeatureEnabled(appContext),
    )

    /**
     * Get the profile id to use in the sponsored stories communications with the Pocket endpoint.
     */
    val pocketSponsoredStoriesProfileId by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_pocket_sponsored_stories_profile),
        default = UUID.randomUUID().toString(),
        persistDefaultIfNotExists = true,
    )

    /**
     * Whether or not the profile ID used in the sponsored stories communications with the Pocket
     * endpoint has been migrated to the MARS endpoint.
     */
    var hasPocketSponsoredStoriesProfileMigrated by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_pocket_sponsored_stories_profile_migrated),
        default = false,
    )

    /**
     * Indicates if Merino content recommendations should be shown.
     */
    var showContentRecommendations by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_pocket_content_recommendations),
        default = { FxNimbus.features.merinoRecommendations.value().enabled },
        featureFlag = true,
    )

    /**
     *  Whether or not to display the Pocket sponsored stories parameter secret settings.
     */
    var useCustomConfigurationForSponsoredStories by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_custom_sponsored_stories_parameters_enabled),
        default = false,
    )

    /**
     * Site parameter used to set the spoc content.
     */
    var pocketSponsoredStoriesSiteId by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_custom_sponsored_stories_site_id),
        default = "",
    )

    /**
     * Country parameter used to set the spoc content.
     */
    var pocketSponsoredStoriesCountry by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_custom_sponsored_stories_country),
        default = "",
    )

    /**
     * City parameter used to set the spoc content.
     */
    var pocketSponsoredStoriesCity by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_custom_sponsored_stories_city),
        default = "",
    )

    /**
     * Indicates if the MARS API integration is used for sponsored content.
     */
    var marsAPIEnabled by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_mars_api_enabled),
        default = { FxNimbus.features.mars.value().enabled },
        featureFlag = true,
    )

    /**
     * Indicates if the Contile functionality should be visible.
     */
    var showContileFeature by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_contile),
        default = true,
    )

    /**
     * Indicates if the Unified Search feature should be visible.
     */
    val showUnifiedSearchFeature = true

    /**
     * Blocklist used to filter items from the home screen that have previously been removed.
     */
    var homescreenBlocklist by stringSetPreference(
        appContext.getPreferenceKey(R.string.pref_key_home_blocklist),
        default = setOf(),
    )

    /**
     * Returns whether onboarding should be shown to the user.
     *
     * @param featureEnabled Boolean to indicate whether the feature is enabled.
     * @param hasUserBeenOnboarded Boolean to indicate whether the user has been onboarded.
     * @param isLauncherIntent Boolean to indicate whether the app was launched on tapping on the
     * app icon.
     */
    fun shouldShowOnboarding(
        featureEnabled: Boolean = onboardingFeatureEnabled,
        hasUserBeenOnboarded: Boolean,
        isLauncherIntent: Boolean,
    ): Boolean {
        return if (featureEnabled && !hasUserBeenOnboarded && isLauncherIntent) {
            FxNimbus.features.junoOnboarding.recordExposure()
            true
        } else {
            false
        }
    }

    /**
     * Indicates if the onboarding feature is enabled.
     */
    var onboardingFeatureEnabled = FeatureFlags.onboardingFeatureEnabled

    /**
     * Indicates if the marketing onboarding card should be shown to the user.
     */
    var shouldShowMarketingOnboarding by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_marketing_onboarding),
        default = true,
    )

    val feltPrivateBrowsingEnabled by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_should_enable_felt_privacy),
        featureFlag = true,
        default = {
            FxNimbus.features.privateBrowsing.value().feltPrivacyEnabled
        },
    )

    var shouldUseComposableToolbar by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_composable_toolbar),
        default = { FxNimbus.features.composableToolbar.value().enabled },
        featureFlag = true,
    )

    /**
     * Indicates if the navigation bar CFR should be displayed to the user.
     */
    var shouldShowNavigationBarCFR by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_should_navbar_cfr),
        default = true,
    )

    /**
     * Indicates if the search bar CFR should be displayed to the user.
     */
    var shouldShowSearchBarCFR by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_should_searchbar_cfr),
        default = false,
    )

    /**
     * Indicates Navigation Bar's Navigation buttons CFR should be displayed to the user.
     */
    var shouldShowNavigationButtonsCFR by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_toolbar_navigation_cfr),
        default = true,
    )

    /**
     * Indicates whether or not to use remote server search configuration.
     */
    var useRemoteSearchConfiguration by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_use_remote_search_configuration),
        default = FxNimbus.features.remoteSearchConfiguration.value().enabled,
    )

    /**
     * Indicates if the menu CFR should be displayed to the user.
     */
    var shouldShowMenuCFR by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_menu_cfr),
        default = true,
    )

    /**
     * Get the current mode for how https-only is enabled.
     */
    fun getHttpsOnlyMode(): HttpsOnlyMode {
        return if (!shouldUseHttpsOnly) {
            HttpsOnlyMode.DISABLED
        } else if (shouldUseHttpsOnlyInPrivateTabsOnly) {
            HttpsOnlyMode.ENABLED_PRIVATE_ONLY
        } else {
            HttpsOnlyMode.ENABLED
        }
    }

    /**
     * Get the current mode for cookie banner handling
     */
    fun getCookieBannerHandling(): CookieBannerHandlingMode {
        return when (shouldUseCookieBanner) {
            true -> CookieBannerHandlingMode.REJECT_ALL
            false -> {
                CookieBannerHandlingMode.DISABLED
            }
        }
    }

    /**
     * Get the current mode for cookie banner handling
     */
    fun getCookieBannerHandlingPrivateMode(): CookieBannerHandlingMode {
        return when (shouldUseCookieBannerPrivateMode) {
            true -> CookieBannerHandlingMode.REJECT_ALL
            false -> {
                CookieBannerHandlingMode.DISABLED
            }
        }
    }

    var setAsDefaultGrowthSent by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_set_as_default),
        default = false,
    )

    var firstWeekSeriesGrowthSent by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_first_week_series_sent),
        default = false,
    )

    var firstWeekDaysOfUseGrowthData by stringSetPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_first_week_days_of_use),
        default = setOf(),
    )

    var adClickGrowthSent by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_ad_click_sent),
        default = false,
    )

    var usageTimeGrowthData by longPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_usage_time),
        default = -1,
    )

    var usageTimeGrowthSent by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_usage_time_sent),
        default = false,
    )

    var resumeGrowthLastSent by longPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_resume_last_sent),
        default = 0,
    )

    var uriLoadGrowthLastSent by longPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_uri_load_last_sent),
        default = 0,
    )

    /**
     * Indicates if the Compose Top Sites are enabled.
     */
    var enableComposeTopSites by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_compose_top_sites),
        default = FeatureFlags.COMPOSE_TOP_SITES,
    )

    /**
     * Indicates if the Compose Homepage is enabled.
     */
    var enableComposeHomepage by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_compose_homepage),
        default = { FxNimbus.features.composeHomepage.value().enabled },
        featureFlag = true,
    )

    /**
     * Indicates if the menu redesign is enabled.
     */
    var enableMenuRedesign by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_menu_redesign),
        default = { FxNimbus.features.menuRedesign.value().enabled },
        featureFlag = true,
    )

    /**
     * Indicates if the Homepage as a New Tab is enabled.
     */
    var enableHomepageAsNewTab by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_homepage_as_new_tab),
        default = FeatureFlags.HOMEPAGE_AS_NEW_TAB,
    )

    /**
     * Indicates if the Homepage Search Bar is enabled.
     */
    var enableHomepageSearchBar by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_homepage_searchbar),
        default = { FxNimbus.features.homepageSearchBar.value().enabled },
        featureFlag = true,
    )

    /**
     * Indicates if the Unified Trust Panel is enabled.
     */
    var enableUnifiedTrustPanel by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_unified_trust_panel),
        default = FeatureFlags.UNIFIED_TRUST_PANEL,
    )

    /**
     * Indicates if Trending Search Suggestions are enabled.
     */
    var isTrendingSearchesVisible by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_trending_searches),
        default = { FxNimbus.features.trendingSearches.value().enabled },
        featureFlag = true,
    )

    /**
     * Indicates if Recent Search Suggestions are enabled.
     */
    var isRecentSearchesVisible by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_recent_searches),
        default = { FxNimbus.features.recentSearches.value().enabled },
        featureFlag = true,
    )

    /**
     * Adjust Activated User sent
     */
    var growthUserActivatedSent by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_user_activated_sent),
        default = false,
    )

    /**
     * Font List Telemetry Ping Sent
     */
    var numFontListSent by intPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_num_font_list_sent),
        default = 0,
    )

    /**
     * Indicates how many days in the first week user opened the app.
     */
    val growthEarlyUseCount = counterPreference(
        appContext.getPreferenceKey(R.string.pref_key_growth_early_browse_count),
        maxCount = 3,
    )

    var growthEarlyUseCountLastIncrement by longPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_early_browse_count_last_increment),
        default = 0L,
    )

    /**
     * Indicates how many days in the first week user searched in the app.
     */
    var growthEarlySearchUsed by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_growth_early_search),
        default = false,
    )

    /**
     * Indicates if the new Search settings UI is enabled.
     */
    var enableUnifiedSearchSettingsUI: Boolean = showUnifiedSearchFeature && FeatureFlags.UNIFIED_SEARCH_SETTINGS

    /**
     * Indicates if hidden engines were restored due to migration to unified search settings UI.
     * Should be removed once we expect the majority of the users to migrate.
     * Tracking: https://bugzilla.mozilla.org/show_bug.cgi?id=1850767
     */
    var hiddenEnginesRestored: Boolean by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_hidden_engines_restored),
        default = false,
    )

    /**
     * Indicates if Firefox Suggest is enabled.
     */
    var enableFxSuggest by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_fxsuggest),
        default = { FxNimbus.features.fxSuggest.value().enabled },
        featureFlag = FeatureFlags.FX_SUGGEST,
    )

    /**
     * Indicates if boosting AMP/wiki suggestions is enabled.
     */
    val boostAmpWikiSuggestions: Boolean
        get() = FxNimbus.features.fxSuggest.value().boostAmpWiki

    /**
     * Indicates first time engaging with signup
     */
    var isFirstTimeEngagingWithSignup: Boolean by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_first_time_engage_with_signup),
        default = true,
    )

    /**
     * Indicates if the user has chosen to show sponsored search suggestions in the awesomebar.
     * The default value is computed lazily, and based on whether Firefox Suggest is enabled.
     */
    var showSponsoredSuggestions by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_show_sponsored_suggestions),
        default = { enableFxSuggest },
        featureFlag = FeatureFlags.FX_SUGGEST,
    )

    /**
     * Indicates if the user has chosen to show search suggestions for web content in the
     * awesomebar. The default value is computed lazily, and based on whether Firefox Suggest
     * is enabled.
     */
    var showNonSponsoredSuggestions by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_show_nonsponsored_suggestions),
        default = { enableFxSuggest },
        featureFlag = FeatureFlags.FX_SUGGEST,
    )

    /**
     * Indicates that the user does not want warned of a translations
     * model download while in data saver mode and using mobile data.
     */
    var ignoreTranslationsDataSaverWarning by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_ignore_translations_data_saver_warning),
        default = false,
    )

    /**
     * Indicates if the feature to close synced tabs is enabled.
     */
    val enableCloseSyncedTabs: Boolean
        get() = FxNimbus.features.remoteTabManagement.value().closeTabsEnabled

    /**
     * Returns the height of the bottom toolbar.
     *
     * The bottom toolbar can consist of:
     *  - a navigation bar.
     *  - a combination of a navigation and address bar.
     *  - a combination of a navigation and address bar & a microsurvey.
     *  - a combination of address bar & a microsurvey.
     *  - be absent.
     *
     *  @param context to be used for [shouldAddNavigationBar] function
     */
    fun getBottomToolbarHeight(context: Context): Int {
        val isNavbarVisible = context.shouldAddNavigationBar()
        val isMicrosurveyEnabled = shouldShowMicrosurveyPrompt
        val isToolbarAtBottom = toolbarPosition == ToolbarPosition.BOTTOM

        val navbarHeight = appContext.resources.getDimensionPixelSize(R.dimen.browser_navbar_height)
        val microsurveyHeight =
            appContext.resources.getDimensionPixelSize(R.dimen.browser_microsurvey_height)
        val toolbarHeight =
            appContext.resources.getDimensionPixelSize(R.dimen.browser_toolbar_height)

        return when {
            isNavbarVisible && isMicrosurveyEnabled && isToolbarAtBottom ->
                navbarHeight + microsurveyHeight + toolbarHeight

            isNavbarVisible && isMicrosurveyEnabled -> navbarHeight + microsurveyHeight
            isNavbarVisible && isToolbarAtBottom -> navbarHeight + toolbarHeight
            isMicrosurveyEnabled && isToolbarAtBottom -> microsurveyHeight + toolbarHeight

            isNavbarVisible -> navbarHeight
            isMicrosurveyEnabled -> microsurveyHeight
            isToolbarAtBottom -> toolbarHeight

            else -> 0
        }
    }

    /**
     * Returns the height of the top toolbar.
     *
     * @param includeTabStrip If true, the height of the tab strip is included in the calculation.
     */
    fun getTopToolbarHeight(includeTabStrip: Boolean): Int {
        val isToolbarAtTop = toolbarPosition == ToolbarPosition.TOP
        val toolbarHeight = appContext.resources.getDimensionPixelSize(R.dimen.browser_toolbar_height)

        return if (isToolbarAtTop && includeTabStrip) {
            toolbarHeight + appContext.resources.getDimensionPixelSize(R.dimen.tab_strip_height)
        } else if (isToolbarAtTop) {
            toolbarHeight
        } else {
            0
        }
    }

    /**
     * Returns the height of the bottom toolbar container.
     *
     * The bottom toolbar container can consist of a navigation bar, the microsurvey prompt
     * a combination of a navigation and microsurvey prompt, or be absent.
     */
    fun getBottomToolbarContainerHeight(): Int {
        val isNavBarEnabled = navigationToolbarEnabled
        val isMicrosurveyEnabled = shouldShowMicrosurveyPrompt
        val navbarHeight = appContext.resources.getDimensionPixelSize(R.dimen.browser_navbar_height)
        val microsurveyHeight =
            appContext.resources.getDimensionPixelSize(R.dimen.browser_microsurvey_height)

        return when {
            isNavBarEnabled && isMicrosurveyEnabled -> navbarHeight + microsurveyHeight
            isNavBarEnabled -> navbarHeight
            isMicrosurveyEnabled -> microsurveyHeight
            else -> 0
        }
    }

    /**
     * Indicates if the user is shown the new navigation toolbar.
     */
    var navigationToolbarEnabled by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_toolbar_show_navigation_toolbar),
        default = { FxNimbus.features.navigationToolbar.value().enabled },
        featureFlag = true,
    )

    /**
     * Indicates if the microsurvey feature is enabled.
     */
    var microsurveyFeatureEnabled by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_microsurvey_feature_enabled),
        default = { FxNimbus.features.microsurveys.value().enabled },
        featureFlag = true,
    )

    /**
     * Indicates if a microsurvey should be shown to the user.
     */
    var shouldShowMicrosurveyPrompt by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_microsurvey_prompt),
        default = false,
    )

    /**
     * Last time the Set as default Browser prompt has been displayed to the user.
     */
    var lastSetAsDefaultPromptShownTimeInMillis by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_last_set_as_default_prompt_shown_time),
        default = 0L,
    )

    /**
     * Number of times the Set as default Browser prompt has been displayed to the user.
     */
    var numberOfSetAsDefaultPromptShownTimes by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_number_of_set_as_default_prompt_shown_times),
        default = 0,
    )

    /**
     * Indicates if the Set as default Browser prompt was displayed while onboarding.
     */
    var promptToSetAsDefaultBrowserDisplayedInOnboarding by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_app_is_onboarding_set_as_default_displayed),
        default = false,
    )

    /**
     * Number of app cold starts between Set as default Browser prompts.
     */
    var coldStartsBetweenSetAsDefaultPrompts by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_app_cold_start_count),
        default = 0,
    )

    /**
     * Indicates if the Set as default Browser prompt should be displayed to the user.
     */
    val shouldShowSetAsDefaultPrompt: Boolean
        get() =
            (System.currentTimeMillis() - lastSetAsDefaultPromptShownTimeInMillis) >
                DAYS_BETWEEN_DEFAULT_BROWSER_PROMPTS * ONE_DAY_MS &&
                numberOfSetAsDefaultPromptShownTimes < MAX_NUMBER_OF_DEFAULT_BROWSER_PROMPTS &&
                coldStartsBetweenSetAsDefaultPrompts >= APP_COLD_STARTS_TO_SHOW_DEFAULT_PROMPT

    /**
     * Updates the relevant settings when the "Set as Default Browser" prompt is shown.
     *
     * This method increments the count of how many times the prompt has been shown,
     * records the current time as the last time the prompt was shown, and resets
     * the counter for the number of cold starts between prompts.
     */
    fun setAsDefaultPromptCalled() {
        numberOfSetAsDefaultPromptShownTimes += 1
        lastSetAsDefaultPromptShownTimeInMillis = System.currentTimeMillis()
        coldStartsBetweenSetAsDefaultPrompts = 0
    }

    /**
     * A timestamp indicating the end of a deferral period, initiated when users deny submitted a crash,
     * during which we avoid showing the unsubmitted crash dialog.
     */
    var crashReportDeferredUntil by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_crash_reporting_deferred_until),
        default = 0,
    )

    /**
     * A timestamp (in milliseconds) representing the earliest cutoff date for fetching crashes
     * from the database. Crashes that occurred before this timestamp are ignored, ensuring the
     * unsubmitted crash dialog is not displayed for older crashes.
     */
    var crashReportCutoffDate by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_crash_reporting_cutoff_date),
        default = 0,
    )

    /**
     * A user preference indicating that crash reports should always be automatically sent. This can be updated
     * through the unsubmitted crash dialog or through data choice preferences.
     */
    var crashReportAlwaysSend by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_crash_reporting_always_report),
        default = false,
    )

    /**
     * Indicates whether or not we should use the new crash reporter dialog.
     */
    var useNewCrashReporterDialog by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_use_new_crash_reporter),
        default = false,
    )

    /**
     * Indicates whether or not we should use the new bookmarks UI.
     */
    var useNewBookmarks by lazyFeatureFlagPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_use_new_bookmarks_ui),
        default = { true },
        featureFlag = true,
    )

    var bookmarkListSortOrder by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_bookmark_list_sort_order),
        default = "",
    )

    var lastSavedInFolderGuid by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_last_folder_saved_in),
        default = "",
    )

    /**
     * Indicates whether or not we should use the new compose logins UI
     */
    var enableComposeLogins by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_compose_logins),
        default = false,
    )

    /**
     * Indicates whether or not to show the entry point for the DNS over HTTPS settings
     */
    val showDohEntryPoint by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_doh_settings_enabled),
        default = false,
    )

    /**
     * Stores the current DoH mode as an integer preference.
     * - 0: Default mode
     * - 2: Increased protection
     * - 3: Maximum protection
     * - 5: DoH is disabled
     */
    private var trrMode by intPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_doh_settings_mode),
        default = DOH_SETTINGS_DEFAULT,
    )

    /**
     * Stores the URI of the custom DoH provider selected by the user.
     * Defaults to an empty string if no provider is set.
     */
    var dohProviderUrl by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_doh_provider_uri),
        default = "",
    )

    /**
     * Stores the URI of the default DoH provider.
     * Bug 1946867 - Currently "hardcoded" to "https://mozilla.cloudflare-dns.com/dns-query"
     */
    val dohDefaultProviderUrl by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_doh_default_provider_uri),
        default = CLOUDFLARE_URI,
    )

    /**
     * Stores a set of domains that are excluded from using DNS over HTTPS.
     */
    var dohExceptionsList by stringSetPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_doh_exceptions_list_string),
        default = emptySet(),
    )

    /**
     * Retrieves the current DohSettingsMode based on trrMode
     */
    fun getDohSettingsMode(): Engine.DohSettingsMode {
        return when (trrMode) {
            DOH_SETTINGS_DEFAULT -> Engine.DohSettingsMode.DEFAULT
            DOH_SETTINGS_INCREASED -> Engine.DohSettingsMode.INCREASED
            DOH_SETTINGS_MAX -> Engine.DohSettingsMode.MAX
            DOH_SETTINGS_OFF -> Engine.DohSettingsMode.OFF
            else -> Engine.DohSettingsMode.DEFAULT
        }
    }

    /**
     * Updates trrMode by converting the given DohSettingsMode
     */
    fun setDohSettingsMode(mode: Engine.DohSettingsMode) {
        trrMode = when (mode) {
            Engine.DohSettingsMode.DEFAULT -> DOH_SETTINGS_DEFAULT
            Engine.DohSettingsMode.INCREASED -> DOH_SETTINGS_INCREASED
            Engine.DohSettingsMode.MAX -> DOH_SETTINGS_MAX
            Engine.DohSettingsMode.OFF -> DOH_SETTINGS_OFF
        }
    }

    /**
     * Indicates if the user has completed the setup step for choosing the toolbar location
     */
    var hasCompletedSetupStepToolbar by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_setup_step_toolbar),
        default = false,
    )

    /**
     * Indicates if the user has completed the setup step for choosing the theme
     */
    var hasCompletedSetupStepTheme by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_setup_step_theme),
        default = false,
    )

    /**
     * Indicates if the user has completed the setup step for exploring extensions
     */
    var hasCompletedSetupStepExtensions by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_setup_step_extensions),
        default = false,
    )

    /**
     * Indicates if this is the default browser.
     */
    var isDefaultBrowser by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_default_browser),
        default = false,
    )

    /**
     * Indicates whether or not to show the checklist feature.
     */
    var showSetupChecklist by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_setup_checklist_complete),
        default = FxNimbus.features.setupChecklist.value().enabled,
    )
}
