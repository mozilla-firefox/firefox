/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.os.Build
import android.os.Bundle
import androidx.core.content.edit
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.preference.EditTextPreference
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreference
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.support.remotesettings.RemoteSettingsServer
import mozilla.components.support.remotesettings.RemoteSettingsServerConfig
import mozilla.components.support.remotesettings.into
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.Config
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.R
import org.mozilla.fenix.debugsettings.data.DefaultDebugSettingsRepository
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.GleanMetrics.DebugDrawer as DebugDrawerMetrics

class SecretSettingsFragment : PreferenceFragmentCompat() {

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_debug_settings))
    }

    @Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        val debugSettingsRepository = DefaultDebugSettingsRepository(
            context = requireContext(),
            writeScope = lifecycleScope,
        )

        setPreferencesFromResource(R.xml.secret_settings_preferences, rootKey)

        requirePreference<SwitchPreference>(R.string.pref_key_allow_third_party_root_certs).apply {
            isVisible = true
            isChecked = context.settings().allowThirdPartyRootCerts
            onPreferenceChangeListener = object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    context.components.core.engine.settings.enterpriseRootsEnabled =
                        newValue as Boolean
                    return super.onPreferenceChange(preference, newValue)
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_nimbus_use_preview).apply {
            isVisible = true
            isChecked = context.settings().nimbusUsePreview
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_composable_toolbar).apply {
            isChecked = context.settings().shouldUseComposableToolbar
            onPreferenceChangeListener = Preference.OnPreferenceChangeListener { _, newValue ->
                (newValue as? Boolean)?.let { newOption ->
                    context.settings().shouldUseComposableToolbar = newOption
                    requirePreference<SwitchPreference>(R.string.pref_key_enable_toolbar_redesign).apply {
                        isEnabled = newOption
                        when (newOption) {
                            true -> {
                                summary = null
                            }

                            false -> {
                                isChecked = false
                                summary = getString(R.string.preferences_debug_settings_toolbar_redesign_summary)
                                context.settings().toolbarRedesignEnabled = false
                                context.settings().shouldUseExpandedToolbar = false
                            }
                        }
                    }
                    requirePreference<SwitchPreference>(R.string.pref_key_enable_toolbar_customization).apply {
                        val newOption = context.settings().toolbarRedesignEnabled
                        isEnabled = newOption
                        summary = when (newOption) {
                            true -> null
                            false -> getString(R.string.preferences_debug_settings_toolbar_customization_summary)
                        }
                        if (!newOption && isChecked) {
                            isChecked = false
                            context.settings().shouldShowToolbarCustomization = false
                        }
                    }
                    requirePreference<SwitchPreference>(R.string.pref_key_should_show_custom_tab_extensions).apply {
                        val shouldEnableCustomTabExtensions = newOption
                        isEnabled = shouldEnableCustomTabExtensions
                        when (shouldEnableCustomTabExtensions) {
                            true -> {
                                summary = null
                            }

                            false -> {
                                isChecked = false
                                summary = getString(R.string.preferences_debug_settings_custom_tab_extensions_summary)
                                context.settings().shouldShowCustomTabExtensions = false
                            }
                        }
                    }
                    requirePreference<SwitchPreference>(
                        R.string.pref_key_use_minimal_bottom_toolbar_while_entering_text,
                    ).apply {
                        isEnabled = newOption
                        when (newOption) {
                            true -> {
                                summary = null
                            }

                            false -> {
                                isEnabled = context.settings().shouldUseComposableToolbar
                                summary = when (context.settings().shouldUseComposableToolbar) {
                                    true -> null
                                    false -> getString(R.string.preferences_debug_settings_toolbar_redesign_summary)
                                }
                            }
                        }
                    }
                }
                true
            }
        }
        requirePreference<SwitchPreference>(R.string.pref_key_enable_toolbar_customization).apply {
            isChecked = context.settings().shouldShowToolbarCustomization
            val newOption = context.settings().toolbarRedesignEnabled
            isEnabled = newOption
            summary = when (newOption) {
                true -> null
                false -> getString(R.string.preferences_debug_settings_toolbar_customization_summary)
            }
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_toolbar_redesign).apply {
            isEnabled = context.settings().shouldUseComposableToolbar
            summary = when (context.settings().shouldUseComposableToolbar) {
                true -> null
                false -> getString(R.string.preferences_debug_settings_toolbar_redesign_summary)
            }
            isChecked = context.settings().toolbarRedesignEnabled
            onPreferenceChangeListener = Preference.OnPreferenceChangeListener { _, newValue ->
                (newValue as? Boolean)?.let { newOption ->
                    context.settings().toolbarRedesignEnabled = newOption
                    if (!newOption) {
                        context.settings().shouldUseExpandedToolbar = false
                    }
                    requirePreference<SwitchPreference>(R.string.pref_key_enable_toolbar_customization).apply {
                        isEnabled = newOption
                        summary = when (newOption) {
                            true -> null
                            false -> getString(R.string.preferences_debug_settings_toolbar_customization_summary)
                        }
                        if (!newOption && isChecked) {
                            isChecked = false
                            context.settings().shouldShowToolbarCustomization = false
                        }
                    }
                }
                true
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_use_minimal_bottom_toolbar_while_entering_text).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isEnabled = context.settings().shouldUseComposableToolbar
            isChecked = context.settings().shouldUseMinimalBottomToolbarWhenEnteringText
            summary = when (context.settings().shouldUseComposableToolbar) {
                true -> null
                false -> getString(R.string.preferences_debug_settings_toolbar_redesign_summary)
            }
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_use_scroll_data_for_dynamic_toolbar).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().useNewDynamicToolbarBehaviour
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_address_sync).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().isAddressSyncEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_compose_logins).apply {
            isChecked = context.settings().enableComposeLogins
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_should_show_custom_tab_extensions).apply {
            isVisible = Config.channel.isDebug
            isChecked = context.settings().shouldShowCustomTabExtensions
            val newOption = context.settings().shouldUseComposableToolbar
            isEnabled = newOption
            summary = when (newOption) {
                true -> null
                false -> getString(R.string.preferences_debug_settings_custom_tab_extensions_summary)
            }
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_homepage_searchbar).apply {
            isVisible = true
            isChecked = context.settings().enableHomepageSearchBar
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_homepage_as_new_tab).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().enableHomepageAsNewTab
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_firefox_labs).apply {
            isChecked = context.settings().enableFirefoxLabs
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_browser_mode_toggle).apply {
            isChecked = context.settings().enableBrowserModeToggle
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_merino_client).apply {
            isChecked = context.settings().enableMerinoClient
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_mozilla_ads_client).apply {
            isChecked = context.settings().enableMozillaAdsClient
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_discover_more_stories).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().enableDiscoverMoreStories
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_unified_trust_panel).apply {
            isChecked = context.settings().enableUnifiedTrustPanel
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_lna_feature_enabled).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().isLnaFeatureEnabled
            onPreferenceChangeListener = object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    context.components.core.engine.settings.lnaFeatureEnabled =
                        newValue as Boolean
                    return super.onPreferenceChange(preference, newValue)
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_lna_blocking_enabled).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().isLnaBlockingEnabled
            onPreferenceChangeListener = object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    context.components.core.engine.settings.lnaBlockingEnabled =
                        newValue as Boolean
                    return super.onPreferenceChange(preference, newValue)
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_lna_tracker_blocking_enabled).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().isLnaTrackerBlockingEnabled
            onPreferenceChangeListener = object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    context.components.core.engine.settings.lnaTrackerBlockingEnabled =
                        newValue as Boolean
                    return super.onPreferenceChange(preference, newValue)
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_allow_settings_search).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().isSettingsSearchEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_fxsuggest).apply {
            isVisible = FeatureFlags.FX_SUGGEST
            isChecked = context.settings().enableFxSuggest
            onPreferenceChangeListener = object : Preference.OnPreferenceChangeListener {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    val newBooleanValue = newValue as? Boolean ?: return false
                    val ingestionScheduler =
                        requireContext().components.fxSuggest.ingestionScheduler
                    if (newBooleanValue) {
                        ingestionScheduler.startPeriodicIngestion()
                    } else {
                        ingestionScheduler.stopPeriodicIngestion()
                    }
                    requireContext().settings().preferences.edit {
                        putBoolean(preference.key, newBooleanValue)
                    }
                    return true
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_doh_settings_enabled).apply {
            isVisible = true
            isChecked = context.settings().showDohEntryPoint
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        lifecycleScope.launch {
            requirePreference<SwitchPreference>(R.string.pref_key_enable_debug_drawer).apply {
                isVisible = true
                isChecked = debugSettingsRepository.debugDrawerEnabled.first()
                onPreferenceChangeListener =
                    Preference.OnPreferenceChangeListener { _, newValue ->
                        debugSettingsRepository.setDebugDrawerEnabled(enabled = newValue as Boolean)
                        DebugDrawerMetrics.debugDrawerEnabled.set(newValue)
                        true
                    }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_use_new_crash_reporter).apply {
            isVisible = true
            isChecked = context.settings().useNewCrashReporterFlow
            onPreferenceChangeListener =
                Preference.OnPreferenceChangeListener { _, newValue ->
                    context.settings().useNewCrashReporterFlow = newValue as Boolean
                    true
                }
        }

        // for performance reasons, this is only available in Nightly or Debug builds
        requirePreference<EditTextPreference>(R.string.pref_key_custom_glean_server_url).apply {
            isVisible = Config.channel.isNightlyOrDebug && BuildConfig.GLEAN_CUSTOM_URL.isNullOrEmpty()
        }

        requirePreference<Preference>(R.string.pref_key_custom_sponsored_stories_parameters).apply {
            isVisible = Config.channel.isNightlyOrDebug
        }

        requirePreference<SwitchPreference>(R.string.pref_key_remote_server_prod).apply {
            isVisible = true
            isChecked = context.settings().useProductionRemoteSettingsServer
            onPreferenceChangeListener = object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    val service =
                        context.components.remoteSettingsService.value.remoteSettingsService
                    service.updateConfig(
                        RemoteSettingsServerConfig(
                            server = if (newValue as? Boolean == true) {
                                RemoteSettingsServer.Prod.into()
                            } else {
                                RemoteSettingsServer.Stage.into()
                            },
                        ).into(),
                    )
                    service.sync()
                    return super.onPreferenceChange(preference, newValue)
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_use_remote_search_configuration).apply {
            isVisible = true
            isChecked = context.settings().useRemoteSearchConfiguration
            onPreferenceChangeListener = object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    if (newValue as? Boolean == true) {
                        context.components.remoteSettingsSyncScheduler.registerForSync()
                    } else {
                        context.components.remoteSettingsSyncScheduler.unregisterForSync()
                    }
                    return super.onPreferenceChange(preference, newValue)
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_microsurvey_feature_enabled).apply {
            isVisible = true
            isChecked = context.settings().microsurveyFeatureEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_shake_to_summarize).apply {
            isVisible = Config.channel.isDebug
            isChecked = context.settings().shakeToSummarizeFeatureEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_persistent_debug_menu).apply {
            isVisible = true
            isChecked = context.settings().isDebugMenuPersistentlyRevealed
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_crash_pull_never_show_again).apply {
            isVisible = true
            isChecked = context.settings().crashPullNeverShowAgain
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_tab_manager_opening_animation).apply {
            isVisible = true
            isChecked = context.settings().tabManagerOpeningAnimationEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_terms_accepted).apply {
            isVisible = Config.channel.isNightlyOrDebug || Config.channel.isBeta
            isChecked = context.settings().hasAcceptedTermsOfService
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_terms_latest_date).apply {
            isVisible = Config.channel.isNightlyOrDebug || Config.channel.isBeta
            isChecked = context.settings().isTermsOfUsePublishedDebugDateEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_debug_terms_trigger_time).apply {
            isVisible = Config.channel.isNightlyOrDebug || Config.channel.isBeta
            isChecked = context.settings().isDebugTermsOfServiceTriggerTimeEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_isolated_process).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().isIsolatedProcessEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_app_zygote_process).apply {
            isVisible = Config.channel.isNightlyOrDebug && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
            isChecked = context.settings().isAppZygoteEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_email_masks).apply {
            isVisible = Config.channel.isDebug
            isChecked = context.settings().isEmailMaskFeatureEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_persistent_onboarding).apply {
            isChecked = context.settings().enablePersistentOnboarding
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_tab_search).apply {
            isVisible = true
            isChecked = context.settings().tabSearchEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_native_share_sheet).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().nativeShareSheetEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
    }

    override fun onPreferenceTreeClick(preference: Preference): Boolean {
        when (preference.key) {
            getString(R.string.pref_key_custom_sponsored_stories_parameters) ->
                findNavController().nav(
                    R.id.secretSettingsPreference,
                    SecretSettingsFragmentDirections.actionSecretSettingsFragmentToSponsoredStoriesSettings(),
                )
        }
        return super.onPreferenceTreeClick(preference)
    }

    override fun onDisplayPreferenceDialog(preference: Preference) {
        val handled = showCustomEditTextPreferenceDialog(preference)

        if (!handled) {
            super.onDisplayPreferenceDialog(preference)
        }
    }
}
