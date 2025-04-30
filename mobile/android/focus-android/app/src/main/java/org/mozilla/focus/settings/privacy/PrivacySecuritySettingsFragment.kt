/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import android.content.SharedPreferences
import android.os.Build
import android.os.Bundle
import androidx.core.content.edit
import androidx.preference.Preference
import androidx.preference.SwitchPreferenceCompat
import mozilla.components.lib.auth.canUseBiometricFeature
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.focus.GleanMetrics.CookieBanner
import org.mozilla.focus.GleanMetrics.PrivacySettings
import org.mozilla.focus.GleanMetrics.TrackingProtectionExceptions
import org.mozilla.focus.R
import org.mozilla.focus.cookiebanner.CookieBannerOption
import org.mozilla.focus.engine.EngineSharedPreferencesListener
import org.mozilla.focus.ext.requireComponents
import org.mozilla.focus.ext.settings
import org.mozilla.focus.ext.showToolbar
import org.mozilla.focus.nimbus.FocusNimbus
import org.mozilla.focus.settings.BaseSettingsFragment
import org.mozilla.focus.state.AppAction
import org.mozilla.focus.state.Screen
import org.mozilla.focus.telemetry.GleanMetricsService
import org.mozilla.focus.widget.CookiesPreference

class PrivacySecuritySettingsFragment :
    BaseSettingsFragment(),
    SharedPreferences.OnSharedPreferenceChangeListener {
    override fun onCreatePreferences(p0: Bundle?, p1: String?) {
        addPreferencesFromResource(R.xml.privacy_security_settings)

        val biometricPreference: SwitchPreferenceCompat? =
            findPreference(getString(R.string.pref_key_biometric))
        val appName = getString(R.string.app_name)
        biometricPreference?.summary =
            getString(R.string.preference_security_biometric_summary2, appName)

        // Remove the biometric toggle if the software or hardware do not support it
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M || !requireContext().canUseBiometricFeature()
        ) {
            biometricPreference?.let { preferenceScreen.removePreference(it) }
        }
        if (!FocusNimbus.features.onboarding.value().isCfrEnabled ||
            !requireContext().settings.shouldShowPrivacySecuritySettingsToolTip
        ) {
            val privacySecuritySettingsToolTip: PreferenceToolTipCompose? =
                findPreference(getString(R.string.pref_key_tool_tip))
            privacySecuritySettingsToolTip?.let { preferenceScreen.removePreference(it) }
        }

        val preferencesListener = EngineSharedPreferencesListener(requireContext())

        val cookiesPreference =
            findPreference(getString(R.string.pref_key_performance_enable_cookies)) as? CookiesPreference
        cookiesPreference?.updateSummary()

        val safeBrowsingSwitchPreference =
            findPreference(getString(R.string.pref_key_safe_browsing)) as? SwitchPreferenceCompat
        val javaScriptPreference =
            findPreference(getString(R.string.pref_key_performance_block_javascript)) as? SwitchPreferenceCompat
        val webFontsPreference =
            findPreference(getString(R.string.pref_key_performance_block_webfonts)) as? SwitchPreferenceCompat
        val cookieBannerPreference = findPreference<Preference>(getString(R.string.pref_key_cookie_banner_settings))

        cookiesPreference?.onPreferenceChangeListener = preferencesListener
        safeBrowsingSwitchPreference?.onPreferenceChangeListener = preferencesListener
        javaScriptPreference?.onPreferenceChangeListener = preferencesListener
        webFontsPreference?.onPreferenceChangeListener = preferencesListener

        cookieBannerPreference?.isVisible = requireContext().settings.isCookieBannerEnable
        if (requireContext().settings.getCurrentCookieBannerOptionFromSharePref() ==
            CookieBannerOption.CookieBannerDisabled()
        ) {
            cookieBannerPreference?.summary = getString(R.string.preferences_cookie_banner_summary_off)
        } else {
            cookieBannerPreference?.summary = getString(R.string.preferences_cookie_banner_summary_on)
        }
    }

    override fun onResume() {
        super.onResume()
        updateBiometricsToggleAvailability()
        updateStealthToggleAvailability()
        updateExceptionSettingAvailability()

        updateStudiesLabel()

        preferenceManager.sharedPreferences?.registerOnSharedPreferenceChangeListener(this)

        // Update title and icons when returning to fragments.
        showToolbar(getString(R.string.preference_privacy_and_security_header))
    }

    override fun onPause() {
        preferenceManager.sharedPreferences?.unregisterOnSharedPreferenceChangeListener(this)
        super.onPause()
    }

    override fun onSharedPreferenceChanged(sharedPreferences: SharedPreferences, key: String?) {
        key?.let {
            recordTelemetry(it, sharedPreferences.all[key])
        }
        updateStealthToggleAvailability()
        if (key == getString(R.string.pref_key_telemetry)) {
            updateStudiesLabel()
        }
    }

    private fun updateStudiesLabel() {
        val experimentPreference =
            findPreference<Preference>(getString(R.string.pref_key_studies_v2))
        experimentPreference?.isEnabled = GleanMetricsService.isTelemetryEnabled(requireContext())
    }

    private fun recordTelemetry(key: String, newValue: Any?) {
        when (key) {
            getString(R.string.pref_key_telemetry) -> PrivacySettings.telemetrySettingChanged.record(
                PrivacySettings.TelemetrySettingChangedExtra(newValue as? Boolean),
            )
            getString(R.string.pref_key_safe_browsing) -> PrivacySettings.safeBrowsingSettingChanged.record(
                PrivacySettings.SafeBrowsingSettingChangedExtra(newValue as? Boolean),
            )
            getString(R.string.pref_key_biometric) -> PrivacySettings.unlockSettingChanged.record(
                PrivacySettings.UnlockSettingChangedExtra(newValue as? Boolean),
            )
            getString(R.string.pref_key_secure) -> PrivacySettings.stealthSettingChanged.record(
                PrivacySettings.StealthSettingChangedExtra(newValue as? Boolean),
            )
            getString(R.string.pref_key_performance_enable_cookies) -> PrivacySettings.blockCookiesChanged.record(
                PrivacySettings.BlockCookiesChangedExtra(newValue as? String),
            )
            else -> {
                // Telemetry for the change is recorded elsewhere.
            }
        }
    }

    private fun updateBiometricsToggleAvailability() {
        val switch =
            preferenceScreen.findPreference(resources.getString(R.string.pref_key_biometric))
                as? SwitchPreferenceCompat

        if (!requireContext().canUseBiometricFeature()) {
            switch?.isChecked = false
            switch?.isEnabled = false
            preferenceManager.sharedPreferences?.edit {
                putBoolean(resources.getString(R.string.pref_key_biometric), false)
            }
        } else {
            switch?.isEnabled = true
        }
    }

    private fun updateExceptionSettingAvailability() {
        val exceptionsPreference: Preference? =
            findPreference(getString(R.string.pref_key_screen_exceptions))
        exceptionsPreference?.isEnabled = false

        requireComponents.trackingProtectionUseCases.fetchExceptions.invoke { exceptions ->
            exceptionsPreference?.isEnabled = exceptions.isNotEmpty()
        }
    }

    override fun onPreferenceTreeClick(preference: Preference): Boolean {
        val engineSharedPreferencesListener = EngineSharedPreferencesListener(requireContext())
        when (preference.key) {
            resources.getString(R.string.pref_key_screen_exceptions) -> {
                TrackingProtectionExceptions.allowListOpened.record(NoExtras())

                requireComponents.appStore.dispatch(
                    AppAction.OpenSettings(page = Screen.Settings.Page.PrivacyExceptions),
                )
            }
            resources.getString(R.string.pref_key_secure),
            resources.getString(R.string.pref_key_biometric),
            -> {
                // We need to recreate the activity to apply the SECURE flags.
                requireActivity().recreate()
            }

            resources.getString(R.string.pref_key_privacy_block_social) ->
                engineSharedPreferencesListener.updateTrackingProtectionPolicy(
                    EngineSharedPreferencesListener.ChangeSource.SETTINGS.source,
                    EngineSharedPreferencesListener.TrackerChanged.SOCIAL.tracker,
                )

            resources.getString(R.string.pref_key_privacy_block_ads) ->
                engineSharedPreferencesListener.updateTrackingProtectionPolicy(
                    EngineSharedPreferencesListener.ChangeSource.SETTINGS.source,
                    EngineSharedPreferencesListener.TrackerChanged.ADVERTISING.tracker,
                )

            resources.getString(R.string.pref_key_privacy_block_analytics) ->
                engineSharedPreferencesListener.updateTrackingProtectionPolicy(
                    EngineSharedPreferencesListener.ChangeSource.SETTINGS.source,
                    EngineSharedPreferencesListener.TrackerChanged.ANALYTICS.tracker,
                )

            resources.getString(R.string.pref_key_privacy_block_other3) ->
                engineSharedPreferencesListener.updateTrackingProtectionPolicy(
                    EngineSharedPreferencesListener.ChangeSource.SETTINGS.source,
                    EngineSharedPreferencesListener.TrackerChanged.CONTENT.tracker,
                )
            resources.getString(R.string.pref_key_cookie_banner_settings) -> {
                CookieBanner.visitedSetting.record(NoExtras())
                requireComponents.appStore.dispatch(
                    AppAction.OpenSettings(
                        page = Screen.Settings.Page.CookieBanner,
                    ),
                )
            }
            resources.getString(R.string.pref_key_site_permissions) ->
                requireComponents.appStore.dispatch(
                    AppAction.OpenSettings(page = Screen.Settings.Page.SitePermissions),
                )
            resources.getString(R.string.pref_key_studies_v2) ->
                requireComponents.appStore.dispatch(
                    AppAction.OpenSettings(page = Screen.Settings.Page.Studies),
                )
        }
        return super.onPreferenceTreeClick(preference)
    }

    private fun updateStealthToggleAvailability() {
        val switch =
            preferenceScreen.findPreference(resources.getString(R.string.pref_key_secure)) as? SwitchPreferenceCompat

        val sharedPreferences = preferenceManager.sharedPreferences

        if (sharedPreferences?.getBoolean(
                resources.getString(R.string.pref_key_biometric),
                false,
            ) == true
        ) {
            sharedPreferences
                .edit {
                    putBoolean(resources.getString(R.string.pref_key_secure), true)
                }

            // Disable the stealth switch
            switch?.isChecked = true
            switch?.isEnabled = false
        } else {
            // Enable the stealth switch
            switch?.isEnabled = true
        }
    }
}
