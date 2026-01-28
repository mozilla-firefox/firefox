/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.utils

import android.accessibilityservice.AccessibilityServiceInfo.CAPABILITY_CAN_PERFORM_GESTURES
import android.appwidget.AppWidgetManager
import android.content.Context
import android.content.Context.MODE_PRIVATE
import android.content.SharedPreferences
import android.content.pm.ShortcutManager
import android.os.Environment
import android.view.accessibility.AccessibilityManager
import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import androidx.core.content.edit
import androidx.lifecycle.LifecycleOwner
import androidx.preference.PreferenceManager
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.Engine.HttpsOnlyMode
import mozilla.components.concept.engine.EngineSession.CookieBannerHandlingMode
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import mozilla.components.feature.sitepermissions.SitePermissionsRules.Action
import mozilla.components.feature.sitepermissions.SitePermissionsRules.AutoplayAction
import mozilla.components.lib.crash.store.CrashReportOption
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.content.PreferencesHolder
import mozilla.components.support.ktx.android.content.booleanPreference
import mozilla.components.support.ktx.android.content.doesDeviceHaveHinge
import mozilla.components.support.ktx.android.content.floatPreference
import mozilla.components.support.ktx.android.content.intPreference
import mozilla.components.support.ktx.android.content.longPreference
import mozilla.components.support.ktx.android.content.stringPreference
import mozilla.components.support.ktx.android.content.stringSetPreference
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.utils.BrowsersCache
import mozilla.components.support.utils.ext.PackageManagerCompatHelper
import mozilla.components.support.utils.ext.packageManagerCompatHelper
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.Config
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.GleanMetrics.TopSites
import org.mozilla.fenix.R
import org.mozilla.fenix.autofill.address.RegionAddressFeatureGate
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.settings.counterPreference
import org.mozilla.fenix.components.settings.featureFlagBooleanPreference
import org.mozilla.fenix.components.settings.lazyBooleanPreference
import org.mozilla.fenix.components.settings.lazyFeatureFlagBooleanPreference
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.debugsettings.addresses.EmptyAddressesDebugRegionRepository
import org.mozilla.fenix.debugsettings.addresses.SharedPrefsAddressesDebugRegionRepository
import org.mozilla.fenix.ext.TALL_SCREEN_HEIGHT_DP
import org.mozilla.fenix.ext.WIDE_SCREEN_WIDTH_DP
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.ext.pixelSizeFor
import org.mozilla.fenix.home.pocket.ContentRecommendationsFeatureHelper
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.TOP_SITES_MAX_COUNT
import org.mozilla.fenix.nimbus.CookieBannersSection
import org.mozilla.fenix.nimbus.DefaultBrowserPrompt
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.HomeScreenSection
import org.mozilla.fenix.settings.PhoneFeature
import org.mozilla.fenix.settings.ShortcutType
import org.mozilla.fenix.settings.deletebrowsingdata.DeleteBrowsingDataOnQuitType
import org.mozilla.fenix.settings.logins.SavedLoginsSortingStrategyMenu
import org.mozilla.fenix.settings.logins.SortingStrategy
import org.mozilla.fenix.settings.registerOnSharedPreferenceChangeListener
import org.mozilla.fenix.settings.sitepermissions.AUTOPLAY_BLOCK_ALL
import org.mozilla.fenix.settings.sitepermissions.AUTOPLAY_BLOCK_AUDIBLE
import org.mozilla.fenix.tabstray.DefaultTabManagementFeatureHelper
import org.mozilla.fenix.termsofuse.TOU_VERSION
import org.mozilla.fenix.termsofuse.getApplicationInstalledTime
import org.mozilla.fenix.wallpapers.Wallpaper
import java.security.InvalidParameterException
import java.util.UUID
import java.util.concurrent.TimeUnit.MILLISECONDS

private const val AUTOPLAY_USER_SETTING = "AUTOPLAY_USER_SETTING"

/**
 * A simple wrapper for SharedPreferences that makes reading preference a little bit easier.
 *
 * @param appContext Reference to application context.
 * @param packageName Package name of the application.
 * @param packageManagerCompatHelper Helper for accessing [android.content.pm.PackageManager] methods.
 * @param isBenchmarkBuild Boolean that will be true only when the app is built for Baseline Profile or Macrobenchmark.
 */
@Suppress("LargeClass", "TooManyFunctions")
class Settings(
    private val appContext: Context,
    private val packageName: String = appContext.packageName,
    private val packageManagerCompatHelper: PackageManagerCompatHelper = appContext.packageManagerCompatHelper,
    private val isBenchmarkBuild: Boolean = BuildConfig.IS_BENCHMARK_BUILD,
) : PreferencesHolder {
    companion object {
        const val FENIX_PREFERENCES = "fenix_preferences"

        private const val BLOCKED_INT = 0
        private const val ASK_TO_ALLOW_INT = 1
        private const val ALLOWED_INT = 2
        private const val INACTIVE_TAB_MINIMUM_TO_SHOW_AUTO_CLOSE_DIALOG = 20

        const val THIRTY_SECONDS_MS = 30 * 1000L
        const val FOUR_HOURS_MS = 60 * 60 * 4 * 1000L
        const val ONE_MINUTE_MS = 60 * 1000L
        const val ONE_HOUR_MS = 60 * ONE_MINUTE_MS
        const val ONE_DAY_MS = 60 * 60 * 24 * 1000L
        const val TWO_DAYS_MS = 2 * ONE_DAY_MS
        const val THREE_DAYS_MS = 3 * ONE_DAY_MS
        const val FIVE_DAYS_MS = 5 * ONE_DAY_MS
        const val ONE_WEEK_MS = 60 * 60 * 24 * 7 * 1000L
        const val ONE_MONTH_MS = (60 * 60 * 24 * 365 * 1000L) / 12

        /**
         * The minimum number a search groups should contain.
         */
        @VisibleForTesting
        internal var searchGroupMinimumSites: Int = 2

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

    private val logger = Logger("Settings")

    @VisibleForTesting
    internal val isCrashReportEnabledInBuild: Boolean =
        BuildConfig.CRASH_REPORTING && Config.channel.isReleased

    override val preferences: SharedPreferences =
        appContext.getSharedPreferences(FENIX_PREFERENCES, MODE_PRIVATE)

    /**
     * Indicates if the recent saved bookmarks functionality should be visible.
     */
    var showBookmarksHomeFeature by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_customization_bookmarks),
        default = { homescreenSections[HomeScreenSection.BOOKMARKS] == true },
    )

    /**
     * Indicates if the recent tabs functionality should be visible.
     */
    var showRecentTabsFeature by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_recent_tabs),
        default = { homescreenSections[HomeScreenSection.JUMP_BACK_IN] == true },
    )

    /**
     * Indicates if the stories homescreen section should be shown.
     */
    var showPocketRecommendationsFeature by lazyFeatureFlagBooleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_pocket_homescreen_recommendations),
        featureFlag = ContentRecommendationsFeatureHelper.isContentRecommendationsFeatureEnabled(appContext),
        defaultValue = { homescreenSections[HomeScreenSection.POCKET] == true },
    )

    /**
     * Indicates what simple toolbar shortcut key is currently selected.
     */
    var toolbarSimpleShortcutKey: String by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_toolbar_simple_shortcut),
        default = { ShortcutType.NEW_TAB.value },
        persistDefaultIfNotExists = true,
    )

    val toolbarSimpleShortcut: String
        get() = when (shouldShowToolbarCustomization) {
            true -> toolbarSimpleShortcutKey
            false -> ShortcutType.NEW_TAB.value
        }

    /**
     * Indicates what expanded toolbar shortcut key is currently selected.
     */
    var toolbarExpandedShortcutKey: String by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_toolbar_expanded_shortcut),
        default = { ShortcutType.BOOKMARK.value },
        persistDefaultIfNotExists = true,
    )

    val toolbarExpandedShortcut: String
        get() = when (shouldShowToolbarCustomization) {
            true -> toolbarExpandedShortcutKey
            false -> ShortcutType.BOOKMARK.value
        }

    /**
     * Indicates if the Pocket recommendations homescreen section should also show sponsored stories.
     */
    val showPocketSponsoredStories by lazyFeatureFlagBooleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_pocket_sponsored_stories),
        defaultValue = { homescreenSections[HomeScreenSection.POCKET_SPONSORED_STORIES] == true },
        featureFlag = ContentRecommendationsFeatureHelper.isPocketSponsoredStoriesFeatureEnabled(appContext),
    )

    /**
     * Indicates whether or not the "Recently Visited" section should be shown on the home screen.
     */
    var historyMetadataUIFeature by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_history_metadata_feature),
        default = { homescreenSections[HomeScreenSection.RECENT_EXPLORATIONS] == true },
    )

    /**
     * Indicates whether or not the "Synced Tabs" section should be shown on the home screen.
     */
    val showSyncedTabs: Boolean
        get() = FxNimbus.features.homescreen.value().sectionsEnabled[HomeScreenSection.SYNCED_TABS] == true

    /**
     * Indicates whether or not the "Collections" section should be shown on the home screen.
     */
    val collections: Boolean
        get() = FxNimbus.features.homescreen.value().sectionsEnabled[HomeScreenSection.COLLECTIONS] == true

    /**
     * Indicates whether or not the Firefox Japan Guide default site should be shown.
     */
    val showFirefoxJpGuideDefaultSite: Boolean
        get() = FxNimbus.features.firefoxJpGuideDefaultSite.value().enabled

    /**
     * Indicates whether or not the homepage header should be shown.
     */
    var showHomepageHeader by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_enable_homepage_header),
        default = { homescreenSections[HomeScreenSection.HEADER] == true },
    )

    /**
     * Indicates whether or not top sites should be shown on the home screen.
     */
    var showTopSitesFeature by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_top_sites),
        default = { homescreenSections[HomeScreenSection.TOP_SITES] == true },
    )

    private val homescreenSections: Map<HomeScreenSection, Boolean>
        get() = FxNimbus.features.homescreen.value().sectionsEnabled

    /**
     * Indicates if the recent tabs homepage section settings should be visible
     */
    val showHomepageRecentTabsSectionToggle: Boolean
        get() = !enableHomepageSearchBar

    /**
     * Indicates if the bookmarks homepage section settings should be visible
     */
    val showHomepageBookmarksSectionToggle: Boolean
        get() = !enableHomepageSearchBar

    /**
     * Indicates if the recently visited homepage section settings should be visible
     */
    val showHomepageRecentlyVisitedSectionToggle: Boolean
        get() = !enableHomepageSearchBar

    var numberOfAppLaunches by intPreference(
        appContext.getPreferenceKey(R.string.pref_key_times_app_opened),
        default = 0,
    )

    /**
     * In bug 1979885 we switched from manually tracking displaying review prompt
     * to recording it as an event in Nimbus which let's us check if later
     * with a JEXL expression.
     *
     * If a previously tracked value exists then this migrates it to an event.
     */
    fun migrateLastReviewPromptTimePrefIfNeeded(nimbusEventStore: NimbusEventStore) {
        val oldKey = "pref_key_last_review_prompt_shown_time"

        if (!preferences.contains(oldKey)) return

        val lastReviewPromptTimeInMillis = try {
            preferences.getLong(oldKey, 0L)
        } catch (e: ClassCastException) {
            logger.warn("Unexpected pref type when trying to migrate last review prompt time", e)
            0
        }

        preferences.edit { remove(oldKey) }

        if (lastReviewPromptTimeInMillis != 0L) {
            val millisAgo = timeNowInMillis() - lastReviewPromptTimeInMillis
            nimbusEventStore.recordPastEvent(
                eventId = "review_prompt_shown",
                timeAgo = millisAgo,
                timeUnit = MILLISECONDS,
            )
        }
    }

    /**
     * Indicates if the custom review prompt feature should be enabled. `True` if the feature is
     * enabled, `false` otherwise.
     */
    var customReviewPromptFeatureEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_custom_review_prompt_enabled),
        default = { FxNimbus.features.customReviewPrompt.value().enabled },
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
        default = { TopSites.contextId.generateAndSet().toString() },
        persistDefaultIfNotExists = true,
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
    var showWallpaperOnboarding by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_wallpapers_onboarding),
        default = true,
    )

    var openLinksInAPrivateTab by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_open_links_in_a_private_tab),
        default = false,
    )

    val shouldSecureModeBeOverridden
        get() = allowScreenshotsInPrivateMode || allowScreenCaptureInSecureScreens
    var allowScreenshotsInPrivateMode by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_allow_screenshots_in_private_mode),
        default = false,
    )

    var allowScreenCaptureInSecureScreens by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_dev_debug_allow_capture_of_secure_screens),
        default = false,
    )

    val appIconSelection by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_app_icon_selection_enabled),
        default = { FxNimbus.features.appIconSelection.value().enabled },
    )

    var privateBrowsingLockedFeatureEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_private_browsing_locked_enabled),
        default = { FxNimbus.features.privateBrowsingLock.value().enabled },
    )

    var privateBrowsingModeLocked by booleanPreference(
        appContext.getString(R.string.pref_key_private_browsing_locked),
        false,
    )

    var shouldReturnToBrowser by booleanPreference(
        appContext.getString(R.string.pref_key_return_to_browser),
        false,
    )

    var shouldShowMenuBanner by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_show_menu_banner),
        default = { FxNimbus.features.menuRedesign.value().menuBanner },
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

    var crashReportChoice by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_crash_reporting_choice),
        default = CrashReportOption.Ask.toString(),
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
        persistDefaultIfNotExists = true,
    )

    /**
     * The date the user accepted the Terms of Use.
     */
    var termsOfUseAcceptedTimeInMillis by longPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_terms_accepted_date),
        default = {
            if (hasAcceptedTermsOfService) {
                getApplicationInstalledTime(
                    packageManagerCompatHelper = packageManagerCompatHelper,
                    packageName = packageName,
                    logger = logger,
                )
            } else {
                0L
            }
        },
    )

    var isTermsOfUsePublishedDebugDateEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_terms_latest_date),
        default = false,
        persistDefaultIfNotExists = true,
    )

    var privacyNoticeBannerLastDisplayedTimeInMillis by longPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_privacy_notice_banner_last_displayed_time),
        default = 0,
    )

    /**
     * The version of the Terms of Use that the user has accepted.
     */
    var termsOfUseAcceptedVersion by intPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_terms_accepted_version),
        default = { if (hasAcceptedTermsOfService) TOU_VERSION else 0 },
    )

    /**
     * Returns true if the terms of use feature flag is enabled
     */
    var isTermsOfUsePromptEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_terms_prompt_enabled),
        default = { FxNimbus.features.termsOfUsePrompt.value().enabled },
    )

    /**
     * Returns true if the nimbus flag for showing the terms of use drag handle is true.
     */
    var shouldShowTermsOfUsePromptDragHandle by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_terms_prompt_drag_handle_enabled),
        default = { FxNimbus.features.termsOfUsePrompt.value().enableDragToDismiss },
    )

    /**
     * The ID of the content option for the Terms of Use prompt.
     */
    var termsOfUsePromptContentOptionId by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_terms_prompt_content_option),
        default = { FxNimbus.features.termsOfUsePrompt.value().contentOption.name },
    )

    /**
     * The maximum number of times the Terms of Use prompt should be displayed.
     *
     * Use a function to ensure the most up-to-date Nimbus value is retrieved.
     */
    fun getTermsOfUseMaxDisplayCount() = FxNimbus.features.termsOfUsePrompt.value().maxDisplayCount

    /**
     * The total number of times the Terms of Use prompt has been displayed.
     */
    var termsOfUsePromptDisplayedCount by intPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_terms_prompt_displayed_count),
        default = 0,
    )

    /**
     * Timestamp in milliseconds when the terms of use prompt was last shown to the user.
     * A value of 0L indicates that the prompt has never been shown.
     */
    var lastTermsOfUsePromptTimeInMillis: Long by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_terms_last_prompt_time),
        default = 0L,
    )

    /**
     * Users who have not accepted ToS will see a popup asking them to accept.
     * They can select "Not now" to postpone accepting.
     */
    var hasPostponedAcceptingTermsOfUse by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_terms_postponed),
        default = false,
    )

    var isDebugTermsOfServiceTriggerTimeEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_debug_terms_trigger_time),
        default = false,
        persistDefaultIfNotExists = true,
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

    /**
     * This lets us know if the user has disabled experimentation manually so that we know
     * if we should re-enable experimentation if the user disables and re-enables telemetry.
     */
    var hasUserDisabledExperimentation by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_user_disabled_experimentation),
        default = false,
    )

    /**
     * Controls whether the user is opted into rollouts (remote improvements).
     * Rollouts are completely decoupled from telemetry and experiments, so users
     * can receive feature updates regardless of their telemetry or experiment settings.
     */
    var isRolloutsEnabled by lazyBooleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_rollouts),
        defaultValue = { appContext.components.nimbus.sdk.rolloutParticipation },
    )

    var isOverrideTPPopupsForPerformanceTest = false

    // We do not use `booleanPreference` because we only want the "read" part of this setting to be
    // controlled by a shared pref (if any). In the secret settings, there is a toggle switch to enable
    // and disable this pref. Other than that, the `SecretDebugMenuTrigger` should be able to change
    // this setting for the duration of the session only, i.e. `SecretDebugMenuTrigger` should never
    // be able to (indirectly) change the value of the shared pref.
    var showSecretDebugMenuThisSession: Boolean = false
        get() = field || isDebugMenuPersistentlyRevealed

    /**
     * Preference for determining whether the debug menu setting is revealed persistently
     */
    val isDebugMenuPersistentlyRevealed: Boolean by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_persistent_debug_menu),
        Config.channel.isDebug,
    )

    val shouldShowSecurityPinWarningSync: Boolean
        get() = loginsSecureWarningSyncCount.underMaxCount()

    val shouldShowSecurityPinWarning: Boolean
        get() = secureWarningCount.underMaxCount()

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
     * Indicates if the request blocking feature for Local Network / Local Device Access blocking is enabled.
     */
    var isLnaBlockingEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_lna_blocking_enabled),
        default = { FxNimbus.features.lnaBlocking.value().blocking },
    )

    /**
     * Indicates if the Local Network / Local Device Access tracker blocking feature is enabled.
     */
    var isLnaTrackerBlockingEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_lna_tracker_blocking_enabled),
        default = { FxNimbus.features.lnaBlocking.value().blockTrackers },
    )

    /**
     * Indicates if the overall Local Network / Local Device Access feature is enabled.
     *
     * Local Network / Local Device Access blocking refers to whether or not we are blocking or
     * allowing requests that originate from remote origins targeting either localhost addresses or
     * local network addresses.
     */
    var isLnaFeatureEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_lna_feature_enabled),
        default = { FxNimbus.features.lnaBlocking.value().enabled },
    )

    /**
     * Indicates whether isolated content processes are enabled or not.
     */
    var isIsolatedProcessEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_isolated_process),
        default = { FxNimbus.features.isolatedContentProcesses.value().enabled },
    )

    /**
     * Indicates whether app Zygote preloading using isolated content processes are enabled or not.
     */
    var isAppZygoteEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_app_zygote_process),
        default = { FxNimbus.features.isolatedContentProcesses.value().appZygotePreloading },
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

    /**
     * Indicates if the user denies to ever see again the Remote Settings crash
     * pull UI.
     */
    var crashPullNeverShowAgain: Boolean by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_crash_pull_never_show_again),
        default = false,
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

    var whatsappLinkSharingEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_link_sharing),
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

    var shouldUseCookieBannerPrivateMode by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_cookie_banner_private_mode),
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
    var reEngagementNotificationEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_re_engagement_notification_enabled),
        default = { FxNimbus.features.reEngagementNotification.value().enabled },
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

    var strictAllowListBaselineTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_strict_allow_list_baseline),
        true,
    )

    var strictAllowListConvenienceTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_strict_allow_list_convenience),
        false,
    )

    var customAllowListBaselineTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_allow_list_baseline),
        true,
    )

    var customAllowListConvenienceTrackingProtection by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tracking_protection_custom_allow_list_convenience),
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

    /**
     * Indicates if the cookie banners CRF should be shown.
     */
    var shouldShowCookieBannersCFR by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_cookie_banners_action_popup),
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
        default = appContext.getString(R.string.total_protection),
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

    var shouldUseExpandedToolbar by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_toolbar_expanded),
        default = false,
        persistDefaultIfNotExists = true,
    )

    var shouldShowToolbarCustomization by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_toolbar_customization),
        default = { FxNimbus.features.toolbarRedesignOption.value().showCustomization },
    )

    val toolbarPosition: ToolbarPosition
        get() = if (isTabStripEnabled) {
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

    /**
     * Checks if a specific type of browsing data is configured to be deleted on quit.
     *
     * @param type The [DeleteBrowsingDataOnQuitType] to check.
     * @return `true` if the data type is set to be deleted on quit, `false` otherwise.
     */
    fun getDeleteDataOnQuit(type: DeleteBrowsingDataOnQuitType): Boolean =
        preferences.getBoolean(type.getPreferenceKey(appContext), false)

    /**
     * Sets whether a specific type of browsing data should be deleted on quit.
     *
     * This function is used to configure the "Delete browsing data on quit" feature.
     * It writes the user's choice to `SharedPreferences` for the given data type.
     * The value is later retrieved by `getDeleteDataOnQuit`.
     *
     * @param type The [DeleteBrowsingDataOnQuitType] to configure.
     * @param value `true` to enable deletion for this type on quit, `false` to disable it.
     */
    fun setDeleteDataOnQuit(type: DeleteBrowsingDataOnQuitType, value: Boolean) {
        preferences.edit { putBoolean(type.getPreferenceKey(appContext), value) }
    }

    /**
     * Checks if any browsing data type is configured to be deleted on quit.
     *
     * This function provides a quick way to determine if the "Delete browsing data on quit"
     * feature is active in any capacity. It iterates through all possible data types
     * and returns `true` if at least one of them is set for deletion.
     *
     * This is useful for UI components that need to know whether to display a general
     * indicator that the feature is enabled, without needing to know the specific details.
     *
     * @return `true` if one or more data types are set to be deleted on quit, `false` otherwise.
     */
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
    internal var trendingSearchSuggestionsEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_trending_search_suggestions),
        default = true,
    )

    /**
     * Indicates if the user have enabled recent search in the search suggestions setting preference.
     */
    internal var shouldShowRecentSearchSuggestions by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_recent_search_suggestions),
        default = true,
    )

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
            if (!userKnowsAboutPwas) {
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

    var shouldShowLockPbmBanner by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_lock_pbm_banner),
        true,
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
            localDeviceAccess = getSitePermissionsPhoneFeatureAction(PhoneFeature.LOCAL_DEVICE_ACCESS),
            localNetworkAccess = getSitePermissionsPhoneFeatureAction(PhoneFeature.LOCAL_NETWORK_ACCESS),
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
            PhoneFeature.LOCAL_DEVICE_ACCESS,
            PhoneFeature.LOCAL_NETWORK_ACCESS,
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
            // Some applications will not work if custom tab never open links in apps, return true if it's custom tab
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
     * Addons are separated by a comma, e.g. "addon1,addon2,addon3"
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

    var isSettingsSearchEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_allow_settings_search),
        default = { FxNimbus.features.settingsSearch.value().enabled },
    )

    var isTabStripEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_tab_strip_show),
        default = FxNimbus.features.tabStrip.value().enabled &&
                (isTabStripEligible(appContext) || FxNimbus.features.tabStrip.value().allowOnAllDevices),
    )

    var isDynamicToolbarEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_dynamic_toolbar),
        default = true,
    )

    var useNewDynamicToolbarBehaviour by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_use_scroll_data_for_dynamic_toolbar),
        default = false,
    )
    var isSwipeToolbarToSwitchTabsEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_swipe_toolbar_switch_tabs),
        default = true,
    )

    /**
     * Address Sync feature.
     */
    var isAddressSyncEnabled by featureFlagBooleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_address_sync),
        defaultValue = true,
        featureFlag = isAddressFeatureEnabled(appContext),
    )

    var addressFeature by featureFlagBooleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_show_address_feature),
        defaultValue = true,
        featureFlag = isAddressFeatureEnabled(appContext),
    )

    /**
     * Returns true if the the device has the prerequisites to enable the tab strip.
     */
    private fun isTabStripEligible(context: Context): Boolean {
        // Tab Strip is currently disabled on foldable devices, while we work on improving the
        // Homescreen / Toolbar / Browser screen to better support the feature. There is also
        // an emulator bug that causes the doesDeviceHaveHinge check to return true on emulators,
        // causing it to be disabled on emulator tablets for API 34 and below.
        // https://issuetracker.google.com/issues/296162661
        return context.isLargeScreenSize() && !context.doesDeviceHaveHinge()
    }

    /**
     * Show the Addresses autofill feature.
     */
    private fun isAddressFeatureEnabled(context: Context): Boolean {
        val locale = LocaleManager.getCurrentLocale(context) ?: LocaleManager.getSystemDefault()
        val debugRepository = if (Config.channel.isNightlyOrDebug) {
            SharedPrefsAddressesDebugRegionRepository(context)
        } else {
            EmptyAddressesDebugRegionRepository()
        }

        val featureGate = RegionAddressFeatureGate(locale, debugRepository)
        return featureGate.isAddressFeatureEnabled()
    }

    private val cookieBannersSection: Map<CookieBannersSection, Int>
        get() =
            FxNimbus.features.cookieBanners.value().sectionsEnabled

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
     * Stores the user choice from the "Autofill" settings for whether
     * credit cards should be synced across devices or not, when the user is authenticated.
     * If set to `true`, then the credit cards will be synced across devices.
     */
    var shouldSyncCreditCardsAcrossDevices by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_credit_cards_sync_cards_across_devices),
        default = false,
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
     * Stores the user choice from the "Autofill" settings for whether
     * addresses should be synced across devices or not, when the user is authenticated.
     * If set to `true`, then the addresses will be synced across devices.
     */
    var shouldSyncAddressesAcrossDevices by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_addresses_sync_cards_across_devices),
        default = false,
    )

    /**
     * Get the profile id to use in the sponsored stories communications with the Pocket endpoint.
     */
    val pocketSponsoredStoriesProfileId by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_pocket_sponsored_stories_profile),
        default = { UUID.randomUUID().toString() },
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
        val shouldShowByDefaultConditions = featureEnabled && !hasUserBeenOnboarded && isLauncherIntent

        val shouldShow = shouldShowByDefaultConditions || enablePersistentOnboarding

        if (shouldShow) {
            FxNimbus.features.junoOnboarding.recordExposure()
        }

        return shouldShow
    }

    /**
     * Secret setting preference that forces the onboarding flow to be shown every time `HomeActivity`
     * is created. When `true`, onboarding is displayed on each launch; when `false`, onboarding is only
     * shown based on the default conditions.
     *
     * Build specific onboarding cards configuration (as defined in `onboarding.yaml.fml`) still applies.
     */
    var enablePersistentOnboarding by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_enable_persistent_onboarding),
        default = false,
    )

    /**
     * Indicates if the onboarding feature is enabled.
     */
    var onboardingFeatureEnabled = FeatureFlags.onboardingFeatureEnabled

    /**
     * Indicates if the onboarding redesign should be used.
     */
    var useOnboardingRedesign by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_use_onboarding_redesign),
        default = { FxNimbus.features.junoOnboarding.value().useOnboardingRedesign },
    )

    /**
     * Indicates if the marketing onboarding card should be shown to the user.
     */
    var shouldShowMarketingOnboarding by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_should_show_marketing_onboarding),
        default = true,
    )

    var shouldUseComposableToolbar by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_composable_toolbar),
        default = { FxNimbus.features.composableToolbar.value().enabled },
    )

    var shouldUseMinimalBottomToolbarWhenEnteringText by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_use_minimal_bottom_toolbar_while_entering_text),
        default = { FxNimbus.features.minimalAddressbar.value().atBottomWhileEnteringText },
    )

    /**
     * Indicates if the user has access to the toolbar redesign option in settings.
     */
    var toolbarRedesignEnabled by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_enable_toolbar_redesign),
        default = { FxNimbus.features.toolbarRedesignOption.value().showOptions },
    )

    /**
     * Indicates if the search bar CFR should be displayed to the user.
     */
    var shouldShowSearchBarCFR by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_should_searchbar_cfr),
        default = false,
    )

    /**
     * Indicates whether or not to use remote server search configuration.
     */
    var useRemoteSearchConfiguration by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_use_remote_search_configuration),
        default = { FxNimbus.features.remoteSearchConfiguration.value().enabled },
    )

    /**
     * Indicates if the menu CFR should be displayed to the user.
     */
    var shouldShowMenuCFR by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_menu_cfr),
        default = false,
    )

    /**
     * Indicates if the toolbar CFR was displayed to the user.
     */
    var hasSeenBrowserToolbarCFR by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_toolbar_cfr),
        default = isBenchmarkBuild,
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
     * Indicates if the extensions status should be shown in the menu opened for custom tabs.
     */
    var shouldShowCustomTabExtensions by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_should_show_custom_tab_extensions),
        default = false,
    )

    /**
     * Indicates if the Homepage as a New Tab is enabled.
     */
    var enableHomepageAsNewTab by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_homepage_as_new_tab),
        default = { FxNimbus.features.homepageAsNewTab.value().enabled },
    )

    /**
     * Indicates if the Homepage Search Bar is enabled.
     */
    var enableHomepageSearchBar by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_homepage_searchbar),
        default = { FxNimbus.features.homepageSearchBar.value().enabled },
    )

    /**
     * Indicates if the Homepage "Discover more" stories is enabled.
     */
    var enableDiscoverMoreStories by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_discover_more_stories),
        default = FeatureFlags.DISCOVER_MORE_STORIES,
    )

    /**
     * Indicates if the Mozilla Ads Client is enabled.
     */
    var enableMozillaAdsClient by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_mozilla_ads_client),
        default = FeatureFlags.MOZILLA_ADS_CLIENT_ENABLED,
    )

    /**
     * Indicates if Firefox Labs is enabled.
     */
    var enableFirefoxLabs by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_firefox_labs),
        default = FeatureFlags.FIREFOX_LABS,
    )

    /**
     * Indicates if Browser Mode Toggle is enabled.
     */
    var enableBrowserModeToggle by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_browser_mode_toggle),
        default = { FxNimbus.features.browserModeToggle.value().enabled },
    )

    /**
     * Indicates if Merino Client is enabled.
     */
    var enableMerinoClient by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_merino_client),
        default = { FxNimbus.features.merinoClient.value().enabled },
    )

    /**
     * Indicates if the Unified Trust Panel is enabled.
     */
    var enableUnifiedTrustPanel by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_unified_trust_panel),
        default = { FxNimbus.features.unifiedTrustPanel.value().enabled },
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
    var enableFxSuggest by lazyFeatureFlagBooleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_fxsuggest),
        defaultValue = { FxNimbus.features.fxSuggest.value().enabled },
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
    var showSponsoredSuggestions by lazyFeatureFlagBooleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_show_sponsored_suggestions),
        defaultValue = { enableFxSuggest },
        featureFlag = FeatureFlags.FX_SUGGEST,
    )

    /**
     * Indicates if the user has chosen to show search suggestions for web content in the
     * awesomebar. The default value is computed lazily, and based on whether Firefox Suggest
     * is enabled.
     */
    var showNonSponsoredSuggestions by lazyFeatureFlagBooleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_show_nonsponsored_suggestions),
        defaultValue = { enableFxSuggest },
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
     * Indicates whether Email Mask is enabled or not.
     */
    var isEmailMaskFeatureEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_email_masks),
        default = { FxNimbus.features.emailMasks.value().enabled },
    )

    /**
     * Indicates whether we should suggest using Relay email masks.
     *
     * This is separate from [isEmailMaskFeatureEnabled] so turning suggestions off
     * does not hide the feature from Settings. This is controlled by the user.
     */
    var isEmailMaskSuggestionEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_email_mask_suggestion),
        default = true,
    )

    /**
     * Indicates if the feature to close synced tabs is enabled.
     */
    val enableCloseSyncedTabs: Boolean
        get() = FxNimbus.features.remoteTabManagement.value().closeTabsEnabled

    /**
     * Returns the height of the browser toolbar height.
     */
    val browserToolbarHeight: Int
        get() = appContext.pixelSizeFor(
            when (shouldUseComposableToolbar) {
                true -> {
                    val isTallWindow = appContext.resources.configuration.screenHeightDp > TALL_SCREEN_HEIGHT_DP
                    val isWideWindow = appContext.resources.configuration.screenWidthDp > WIDE_SCREEN_WIDTH_DP
                    when (
                        toolbarPosition == ToolbarPosition.BOTTOM && shouldUseExpandedToolbar &&
                                isTallWindow && !isWideWindow
                    ) {
                        true -> R.dimen.composable_browser_toolbar_height_small
                        false -> R.dimen.composable_browser_toolbar_height
                    }
                }
                false -> R.dimen.browser_toolbar_height
            },
        )

    /**
     * Indicates if the microsurvey feature is enabled.
     */
    var microsurveyFeatureEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_microsurvey_feature_enabled),
        default = { FxNimbus.features.microsurveys.value().enabled },
    )

    /**
     * Indicates if the Shake to Summarize feature is enabled.
     */
    var shakeToSummarizeFeatureEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_shake_to_summarize),
        default = Config.channel.isDebug,
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
    fun shouldShowSetAsDefaultPrompt(
        nimbusFeature: DefaultBrowserPrompt = FxNimbus.features.defaultBrowserPrompt.value(),
    ): Boolean {
        if (!nimbusFeature.enabled) return false

        val now = System.currentTimeMillis()

        val daysOk = nimbusFeature.daysBetweenPrompts?.let { intervalDays ->
            (now - lastSetAsDefaultPromptShownTimeInMillis) > intervalDays * ONE_DAY_MS
        } ?: true

        val maxOk = nimbusFeature.maxPromptsShown?.let { max ->
            numberOfSetAsDefaultPromptShownTimes < max
        } ?: true

        val coldStartsOk = nimbusFeature.coldStartsBetweenPrompts?.let { minColdStarts ->
            coldStartsBetweenSetAsDefaultPrompts >= minColdStarts
        } ?: true

        return daysOk && maxOk && coldStartsOk
    }

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
     * Indicates whether or not we should use the new crash reporter flow.
     */
    var useNewCrashReporterFlow by booleanPreference(
        appContext.getPreferenceKey(R.string.pref_key_use_new_crash_reporter),
        default = Config.channel.isNightlyOrDebug || Config.channel.isBeta,
    )

    /**
     * Do not show crash pull dialog before this date.
     * cf browser.crashReports.dontShowBefore on desktop
     */
    var crashPullDontShowBefore by longPreference(
        appContext.getPreferenceKey(R.string.pref_key_crash_pull_dont_show_before),
        default = 0,
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
        default = true,
    )

    var loginsListSortOrder by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_logins_list_sort_order),
        default = "",
    )

    /**
     * Indicates whether or not we should use the new compose autofill settings UI
     */
    var enableComposeAutofillSettings by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_enable_compose_autofill_settings),
        default = false,
    )

    /**
     * Indicates whether or not to show the entry point for the DNS over HTTPS settings
     */
    val showDohEntryPoint by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_doh_settings_enabled),
        default = { FxNimbus.features.doh.value().showUi },
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
     * Indicates if the user has completed the setup step for installing the search widget.
     */
    var hasCompletedSetupStepInstallSearchWidget by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_setup_step_install_search_widget),
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
     * Indicates if the sponsored tiles are suppressed.
     */
    var suppressSponsoredTopSitesEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_suppress_sponsored_tiles),
        default = { FxNimbus.features.suppressSponsoredTopSites.value().enabled },
    )

    /**
     * Indicates whether or not to show the checklist feature.
     */
    var showSetupChecklist by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_setup_checklist_complete),
        default = {
            FxNimbus.features.setupChecklist.value().enabled &&
                    canShowAddSearchWidgetPrompt(AppWidgetManager.getInstance(appContext))
        },
    )

    /**
     * Distribution ID that represents if the app was installed via a distribution deal
     */
    var distributionId by stringPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_distribution_id),
        default = "",
    )

    /**
     * Whether the Tab Manager opening animation is enabled.
     */
    var tabManagerOpeningAnimationEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_tab_manager_opening_animation),
        default = { DefaultTabManagementFeatureHelper.openingAnimationEnabled },
    )

    /**
     * Whether the Tab Search feature is enabled.
     */
    var tabSearchEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_tab_search),
        default = { DefaultTabManagementFeatureHelper.tabSearchEnabled },
    )

    /**
     * Whether the Native Share Sheet feature is enabled.
     */
    var nativeShareSheetEnabled by booleanPreference(
        key = appContext.getPreferenceKey(R.string.pref_key_native_share_sheet),
        default = { FxNimbus.features.nativeShareSheet.value().enabled },
    )

    /**
     * Indicates whether the app should automatically clean up downloaded files.
     */
    fun shouldCleanUpDownloadsAutomatically(): Boolean {
        val sharedPreferences = PreferenceManager.getDefaultSharedPreferences(appContext)
        val cleanupPreferenceKey = appContext.getString(R.string.pref_key_downloads_clean_up_files_automatically)
        return sharedPreferences.getBoolean(cleanupPreferenceKey, false)
    }

    var downloadsDefaultLocation by stringPreference(
        appContext.getPreferenceKey(R.string.pref_key_downloads_default_location),
        default = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).path,
    )
}
