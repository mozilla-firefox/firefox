/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.utils

import android.content.Context
import android.content.SharedPreferences
import android.content.res.Configuration
import androidx.annotation.VisibleForTesting
import androidx.core.content.edit
import androidx.preference.PreferenceManager
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.mediaquery.PreferredColorScheme
import mozilla.components.support.ktx.android.content.PreferencesHolder
import mozilla.components.support.ktx.android.content.booleanPreference
import org.mozilla.focus.R
import org.mozilla.focus.components.EngineProvider.NO_VALUE
import org.mozilla.focus.cookiebanner.CookieBannerOption
import org.mozilla.focus.nimbus.FocusNimbus
import org.mozilla.focus.searchsuggestions.SearchSuggestionsPreferences
import org.mozilla.focus.telemetry.GleanMetricsService

/**
 * A simple wrapper for SharedPreferences that makes reading preference a little bit easier.
 * This class is designed to have a lot of (simple) functions
 */
@Suppress("TooManyFunctions", "LargeClass")
class Settings(
    private val context: Context,
) : PreferencesHolder {

    @Deprecated("This is no longer used. Read search engines from BrowserStore instead")
    val defaultSearchEngineName: String
        get() = preferences.getString(getPreferenceKey(R.string.pref_key_search_engine), "")!!

    val openLinksInExternalApp: Boolean
        get() = preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_open_links_in_external_app),
            false,
        )

    var isExperimentationEnabled: Boolean = false

    var shouldShowCookieBannerCfr: Boolean
        get() = preferences.getBoolean(
            getPreferenceKey(R.string.pref_cfr_visibility_for_cookie_banner),
            true,
        )
        set(value) {
            preferences.edit {
                putBoolean(
                    getPreferenceKey(R.string.pref_cfr_visibility_for_cookie_banner),
                    value,
                )
            }
        }

    var shouldShowCfrForTrackingProtection: Boolean
        get() = preferences.getBoolean(getPreferenceKey(R.string.pref_cfr_visibility_for_tracking_protection), true)
        set(value) {
            preferences.edit {
                putBoolean(getPreferenceKey(R.string.pref_cfr_visibility_for_tracking_protection), value)
            }
        }

    var shouldShowStartBrowsingCfr: Boolean
        get() = preferences.getBoolean(getPreferenceKey(R.string.pref_cfr_visibility_for_start_browsing), true)
        set(value) {
            preferences.edit {
                putBoolean(getPreferenceKey(R.string.pref_cfr_visibility_for_start_browsing), value)
            }
        }

    var isFirstRun: Boolean
        get() = preferences.getBoolean(getPreferenceKey(R.string.firstrun_shown), true)
        set(value) {
            preferences.edit {
                putBoolean(getPreferenceKey(R.string.firstrun_shown), value)
            }
        }

    /**
     * This is needed for GUI Testing. If the value is not set in the sharePref
     * the default value will be the one from Nimbus.
     */
    var isNewOnboardingEnable: Boolean
        get() = preferences.getBoolean(
            getPreferenceKey(R.string.new_onboarding_enabled),
            FocusNimbus.features.onboarding.value().isEnabled,
        )
        set(value) {
            preferences.edit {
                putBoolean(getPreferenceKey(R.string.new_onboarding_enabled), value)
            }
        }

    var shouldShowPrivacySecuritySettingsToolTip: Boolean
        get() = preferences.getBoolean(getPreferenceKey(R.string.pref_tool_tip_privacy_security_settings), true)
        set(value) {
            preferences.edit {
                putBoolean(getPreferenceKey(R.string.pref_tool_tip_privacy_security_settings), value)
            }
        }

    fun shouldEnableRemoteDebugging(): Boolean =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_remote_debugging),
            false,
        )

    fun shouldShowSearchSuggestions(): Boolean =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_show_search_suggestions),
            false,
        )

    fun shouldBlockWebFonts(): Boolean =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_performance_block_webfonts),
            false,
        )

    fun shouldBlockJavaScript(): Boolean =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_performance_block_javascript),
            false,
        )

    var shouldBlockCookiesValue: String
        get() = preferences.getString(
            getPreferenceKey(R.string.pref_key_performance_enable_cookies),
            NO_VALUE,
        ) ?: NO_VALUE
        set(value) {
            preferences.edit {
                putString(
                    getPreferenceKey(R.string.pref_key_performance_enable_cookies),
                    value,
                )
            }
        }

    fun shouldUseBiometrics(): Boolean =
        preferences.getBoolean(getPreferenceKey(R.string.pref_key_biometric), false)

    fun shouldUseSecureMode(): Boolean =
        preferences.getBoolean(getPreferenceKey(R.string.pref_key_secure), false)

    fun setDefaultSearchEngineByName(name: String) {
        preferences.edit {
            putString(getPreferenceKey(R.string.pref_key_search_engine), name)
        }
    }

    fun shouldAutocompleteFromShippedDomainList() =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_autocomplete_preinstalled),
            true,
        )

    fun shouldAutocompleteFromCustomDomainList() =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_autocomplete_custom),
            true,
        )

    fun shouldBlockAdTrackers() =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_privacy_block_ads),
            true,
        )

    /**
     * Determines whether safe browsing should be enabled based on the user's preference.
     */
    fun shouldUseSafeBrowsing() =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_safe_browsing),
            true,
        )

    fun shouldBlockAnalyticTrackers() =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_privacy_block_analytics),
            true,
        )

    fun shouldBlockSocialTrackers() =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_privacy_block_social),
            true,
        )

    fun shouldBlockOtherTrackers() =
        preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_privacy_block_other3),
            false,
        )

    fun userHasToggledSearchSuggestions(): Boolean =
        preferences.getBoolean(SearchSuggestionsPreferences.TOGGLED_SUGGESTIONS_PREF, false)

    fun userHasDismissedNoSuggestionsMessage(): Boolean =
        preferences.getBoolean(SearchSuggestionsPreferences.DISMISSED_NO_SUGGESTIONS_PREF, false)

    fun hasRequestedDesktop() = preferences.getBoolean(
        getPreferenceKey(R.string.has_requested_desktop),
        false,
    )

    fun getAppLaunchCount() = preferences.getInt(
        getPreferenceKey(R.string.app_launch_count),
        0,
    )

    fun getTotalBlockedTrackersCount() = preferences.getInt(
        getPreferenceKey(R.string.pref_key_privacy_total_trackers_blocked_count),
        0,
    )

    fun hasSocialBlocked() = preferences.getBoolean(
        getPreferenceKey(R.string.pref_key_privacy_block_social),
        true,
    )

    fun hasAdvertisingBlocked() = preferences.getBoolean(
        getPreferenceKey(R.string.pref_key_privacy_block_ads),
        true,
    )

    fun hasAnalyticsBlocked() = preferences.getBoolean(
        getPreferenceKey(R.string.pref_key_privacy_block_analytics),
        true,
    )

    var lightThemeSelected by booleanPreference(
        getPreferenceKey(R.string.pref_key_light_theme),
        false,
    )

    var darkThemeSelected by booleanPreference(
        getPreferenceKey(R.string.pref_key_dark_theme),
        false,
    )

    var useDefaultThemeSelected by booleanPreference(
        getPreferenceKey(R.string.pref_key_default_theme),
        false,
    )

    /**
     * Sets Preferred Color scheme based on Dark/Light Theme Settings or Current Configuration
     */
    fun getPreferredColorScheme(): PreferredColorScheme {
        val inDark =
            (context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) ==
                Configuration.UI_MODE_NIGHT_YES
        return when {
            darkThemeSelected -> PreferredColorScheme.Dark
            lightThemeSelected -> PreferredColorScheme.Light
            inDark -> PreferredColorScheme.Dark
            else -> PreferredColorScheme.Light
        }
    }

    var shouldUseNimbusPreview: Boolean
        get() = preferences.getBoolean(getPreferenceKey(R.string.pref_key_use_nimbus_preview), false)
        set(value) {
            preferences.edit(commit = true) {
                putBoolean(getPreferenceKey(R.string.pref_key_use_nimbus_preview), value)
            }
        }

    var useProductionRemoteSettingsServer: Boolean
        get() = preferences.getBoolean(getPreferenceKey(R.string.pref_key_remote_server_prod), true)
        set(value) {
            preferences.edit(commit = true) {
                putBoolean(getPreferenceKey(R.string.pref_key_remote_server_prod), value)
            }
        }

    fun addSearchWidgetInstalled(count: Int) {
        val key = getPreferenceKey(R.string.pref_key_search_widget_installed)
        val newValue = preferences.getInt(key, 0) + count
        preferences.edit {
            putInt(key, newValue)
        }
    }

    val searchWidgetInstalled: Boolean
        get() = 0 < preferences.getInt(
            getPreferenceKey(R.string.pref_key_search_widget_installed),
            0,
        )

    /**
     * This is used for promote search widget dialog to appear only at the first data clearing and
     * at the 5th one.
     */
    fun addClearBrowsingSessions(count: Int) {
        val key = getPreferenceKey(R.string.pref_key_clear_browsing_sessions)
        val newValue = preferences.getInt(key, 0) + count
        preferences.edit {
            putInt(key, newValue)
        }
    }

    fun getClearBrowsingSessions() = preferences.getInt(
        getPreferenceKey(R.string.pref_key_clear_browsing_sessions),
        0,
    )

    fun getHttpsOnlyMode(): Engine.HttpsOnlyMode {
        return if (preferences.getBoolean(getPreferenceKey(R.string.pref_key_https_only), true)) {
            Engine.HttpsOnlyMode.ENABLED
        } else {
            Engine.HttpsOnlyMode.DISABLED
        }
    }

    /**
     * This is needed for GUI Testing. If the value is not set in the sharePref
     * the default value will be the one from Nimbus.
     */
    @VisibleForTesting
    var isCookieBannerEnable: Boolean
        get() = preferences.getBoolean(
            getPreferenceKey(R.string.pref_key_cookie_banner_enabled),
            FocusNimbus.features.cookieBanner.value().isCookieHandlingEnabled,
        )
        set(value) {
            preferences.edit {
                putBoolean(getPreferenceKey(R.string.pref_key_cookie_banner_enabled), value)
            }
        }

    fun saveCurrentCookieBannerOptionInSharePref(
        cookieBannerOption: CookieBannerOption,
    ) {
        preferences.edit {
            putString(
                context.getString(R.string.pref_key_cookie_banner_settings),
                context.getString(cookieBannerOption.prefKeyId),
            )
        }
    }

    fun getCurrentCookieBannerOptionFromSharePref(): CookieBannerOption {
        val optionValue = preferences.getString(
            context.getString(R.string.pref_key_cookie_banner_settings),
            context.getString(CookieBannerOption.CookieBannerRejectAll().prefKeyId),
        )
        return when (optionValue) {
            context.getString(CookieBannerOption.CookieBannerDisabled().prefKeyId) ->
                CookieBannerOption.CookieBannerDisabled()
            context.getString(CookieBannerOption.CookieBannerRejectAll().prefKeyId) ->
                CookieBannerOption.CookieBannerRejectAll()
            else -> CookieBannerOption.CookieBannerDisabled()
        }
    }

    var isDailyUsagePingEnabled by booleanPreference(
        getPreferenceKey(R.string.pref_key_daily_usage_ping),
        default = GleanMetricsService.shouldTelemetryBeEnabledByDefault(context),
        persistDefaultIfNotExists = true,
    )

    private fun getPreferenceKey(resourceId: Int): String =
        context.getString(resourceId)

    override val preferences: SharedPreferences
        get() = PreferenceManager.getDefaultSharedPreferences(context)
}
