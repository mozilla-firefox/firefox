/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.edit
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavDirections
import androidx.navigation.findNavController
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import androidx.preference.EditTextPreference
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.Config
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.R
import org.mozilla.fenix.debugsettings.data.DefaultDebugSettingsRepository
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.ext.showToolbarWithIconButton
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.SecretSettingsPrefDefaults

/** Settings screen allowing users to configure options not intended for general release. */
class SecretSettingsFragment : PreferenceFragmentCompat(), SystemInsetsPaddedFragment {

    private val args by navArgs<SecretSettingsFragmentArgs>()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        val context = inflater.context
        val preferencesView =
            checkNotNull(super.onCreateView(inflater, container, savedInstanceState)) {
                "PreferenceFragmentCompat returned null from onCreateView"
            }

        return LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams =
                ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT,
                )

            // View for the list of preferences.
            addView(
                preferencesView,
                LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f),
            )

            // View for the bottom aligned reset button.
            addView(
                ComposeView(context).apply {
                    setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
                    setContent {
                        FirefoxTheme {
                            FilledButton(
                                text = stringResource(R.string.preferences_debug_settings_reset_defaults),
                                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 24.dp),
                                onClick = ::showResetConfirmationDialog,
                            )
                        }
                    }
                },
                LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                ),
            )
        }
    }

    override fun onResume() {
        super.onResume()

        val showSearch = !args.searchInProgress

        if (showSearch) {
            showToolbarWithIconButton(
                title = getString(R.string.preferences_debug_settings),
                contentDescription = getString(R.string.settings_search_button_content_description),
                iconResId = iconsR.drawable.mozac_ic_search_24,
                onClick = {
                    findNavController().navigate(R.id.action_secretSettingsFragment_to_secretSettingsSearchFragment)
                },
            )
        } else {
            showToolbar(getString(R.string.preferences_debug_settings))
        }

        args.preferenceToScrollTo?.let {
            scrollToPreferenceWithHighlight(it)
        }

        requirePreference<Preference>(R.string.pref_key_remote_settings_server).summary =
            requireComponents.settings.getRemoteSettingsServerString()
    }

    @Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        val debugSettingsRepository =
            DefaultDebugSettingsRepository(
                context = requireContext(),
                writeScope = lifecycleScope,
            )
        val settings = requireComponents.settings

        setPreferencesFromResource(R.xml.secret_settings_preferences, rootKey)

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_allow_third_party_root_certs).apply {
            isVisible = true
            isChecked = settings.allowThirdPartyRootCerts
            onPreferenceChangeListener =
                object : SharedPreferenceUpdater() {
                    override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                        context.components.core.engine.settings.enterpriseRootsEnabled = newValue as Boolean
                        return super.onPreferenceChange(preference, newValue)
                    }
                }
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_nimbus_use_preview).apply {
            isVisible = true
            isChecked = settings.nimbusUsePreview
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<Preference>(R.string.pref_key_search_optimization).apply {
            isVisible = Config.channel.isNightlyOrDebug || Config.channel.isBeta
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_use_minimal_bottom_toolbar_while_entering_text)
            .apply {
                isVisible =
                    false // disabled temporarily based on https://bugzilla.mozilla.org/show_bug.cgi?id=1943053#c31
                isChecked = settings.shouldUseMinimalBottomToolbarWhenEnteringText
                onPreferenceChangeListener = SharedPreferenceUpdater()
            }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_use_scroll_data_for_dynamic_toolbar).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.useNewDynamicToolbarBehaviour
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_address_sync).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.isAddressSyncEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_wayback_machine).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.isWaybackMachineEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_hide_collections).apply {
            isChecked = settings.hideCollectionsUi
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_should_show_custom_tab_extensions).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.shouldShowCustomTabExtensions
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_homepage_as_new_tab).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.enableHomepageAsNewTab
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_universal_edge_to_edge_wallpapers).apply {
            isChecked = settings.enableUniversalEdgeToEdgeWallpapers
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_top_sites_pager).apply {
            isChecked = settings.topSitesPager
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_add_shortcuts_improvement).apply {
            isChecked = settings.enableAddShortcutsImprovement
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_more_shortcuts).apply {
            isChecked = settings.showMoreShortcuts
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_merino_client).apply {
            isChecked = settings.enableMerinoClient
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_homepage_weather_widget).apply {
            isChecked = settings.enableHomepageWeatherWidget
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_ads_client_for_stories).apply {
            isChecked = settings.enableAdsClientForStories
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_lna_feature_enabled).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.isLnaFeatureEnabled
            onPreferenceChangeListener =
                object : SharedPreferenceUpdater() {
                    override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                        context.components.core.engine.settings.lnaFeatureEnabled = newValue as Boolean
                        return super.onPreferenceChange(preference, newValue)
                    }
                }
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_lna_blocking_enabled).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.isLnaBlockingEnabled
            onPreferenceChangeListener =
                object : SharedPreferenceUpdater() {
                    override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                        context.components.core.engine.settings.lnaBlockingEnabled = newValue as Boolean
                        return super.onPreferenceChange(preference, newValue)
                    }
                }
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_lna_tracker_blocking_enabled).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.isLnaTrackerBlockingEnabled
            onPreferenceChangeListener =
                object : SharedPreferenceUpdater() {
                    override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                        context.components.core.engine.settings.lnaTrackerBlockingEnabled = newValue as Boolean
                        return super.onPreferenceChange(preference, newValue)
                    }
                }
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_fxsuggest).apply {
            isVisible = FeatureFlags.FX_SUGGEST
            isChecked = settings.enableFxSuggest
            onPreferenceChangeListener =
                object : Preference.OnPreferenceChangeListener {
                    override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                        val newBooleanValue = newValue as? Boolean ?: return false
                        val ingestionScheduler = requireContext().components.fxSuggest.ingestionScheduler
                        if (newBooleanValue) {
                            ingestionScheduler.startPeriodicIngestion()
                        } else {
                            ingestionScheduler.stopPeriodicIngestion()
                        }
                        requireComponents.settings.preferences.edit {
                            putBoolean(preference.key, newBooleanValue)
                        }
                        return true
                    }
                }
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_doh_settings_enabled).apply {
            isVisible = true
            isChecked = settings.showDohEntryPoint
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        lifecycleScope.launch {
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_debug_drawer).apply {
                isVisible = true
                isChecked = debugSettingsRepository.debugDrawerEnabled.first()
                onPreferenceChangeListener = Preference.OnPreferenceChangeListener { _, newValue ->
                    debugSettingsRepository.setDebugDrawerEnabled(enabled = newValue as Boolean)
                    true
                }
            }
        }

        // for performance reasons, this is only available in Nightly or Debug builds
        requirePreference<EditTextPreference>(R.string.pref_key_custom_glean_server_url).apply {
            isVisible = Config.channel.isNightlyOrDebug && BuildConfig.GLEAN_CUSTOM_URL.isNullOrEmpty()
        }

        requirePreference<Preference>(R.string.pref_key_remote_settings_server).apply {
            isVisible = true
            summary = settings.getRemoteSettingsServerString()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_use_remote_search_configuration).apply {
            isVisible = true
            isChecked = settings.useRemoteSearchConfiguration
            onPreferenceChangeListener =
                object : SharedPreferenceUpdater() {
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

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_microsurvey_feature_enabled).apply {
            isVisible = true
            isChecked = settings.microsurveyFeatureEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_shake_to_summarize).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.shakeToSummarizeFeatureFlagEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_listen_to_page).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.listenToPageFeatureFlagEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_import_bookmarks).apply {
            isVisible = Config.channel.isDebug
            isChecked = settings.importBookmarksFeatureFlagEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_ip_protection).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.isIPProtectionEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_ip_protection_locations).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.isIPProtectionLocationsEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_ip_protection_use_gpi).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.ipProtectionUseGpi
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_import_passwords).apply {
            isVisible = Config.channel.isDebug
            isChecked = settings.importPasswordsFeatureFlagEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_uninstall_survey).apply {
            isVisible = true
            isChecked = context.components.settings.uninstallSurveyFeatureFlagEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_oled_theme).apply {
            isChecked = settings.enableOledTheme
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_pdf_tools).apply {
            isChecked = settings.enablePdfTools
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_homepage_customization).apply {
            isChecked = settings.enableHomepageCustomization
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_homepage_trending_recent_search).apply {
            isChecked = settings.enableHomepageTrendingRecentSearch
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_persistent_debug_menu).apply {
            isVisible = true
            isChecked = settings.isDebugMenuPersistentlyRevealed
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_crash_pull_never_show_again).apply {
            isVisible = true
            isChecked = settings.crashPullNeverShowAgain
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_tab_manager_opening_animation).apply {
            isVisible = true
            isChecked = settings.tabManagerOpeningAnimationEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_terms_accepted).apply {
            isVisible = Config.channel.isNightlyOrDebug || Config.channel.isBeta
            isChecked = settings.hasAcceptedTermsOfService
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_terms_latest_date).apply {
            isVisible = Config.channel.isNightlyOrDebug || Config.channel.isBeta
            isChecked = settings.isTermsOfUsePublishedDebugDateEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_debug_terms_trigger_time).apply {
            isVisible = Config.channel.isNightlyOrDebug || Config.channel.isBeta
            isChecked = settings.isDebugTermsOfServiceTriggerTimeEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_isolated_process).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.isIsolatedProcessEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_app_zygote_process).apply {
            isVisible = Config.channel.isNightlyOrDebug && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q
            isChecked = settings.isAppZygoteEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_persistent_onboarding).apply {
            isChecked = settings.enablePersistentOnboarding
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_tab_groups_drag_and_drop).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.tabGroupsDragAndDropEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_tab_groups_live_reorder).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = settings.tabGroupsLiveReorderEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_tab_groups_onboarding).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.components.settings.tabGroupsOnboardingEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_private_mode_and_stories_entry_point).apply {
            isChecked = settings.privateModeAndStoriesEntryPointEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_native_share_sheet).apply {
            isVisible = Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE
            isChecked = settings.nativeShareSheetEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_longfox).apply {
            isChecked = settings.longfoxEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_google_lens_integration).apply {
            isVisible = Config.channel.isNightlyOrDebug || Config.channel.isBeta
            isChecked = settings.googleLensIntegrationEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_voice_search_in_display_toolbar).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.components.settings.showVoiceSearchInDisplayToolbar
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }
    }

    override fun onDisplayPreferenceDialog(preference: Preference) {
        val handled = showCustomEditTextPreferenceDialog(preference)

        if (!handled) {
            super.onDisplayPreferenceDialog(preference)
        }
    }

    private fun showResetConfirmationDialog() {
        MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.preferences_debug_settings_reset_defaults)
            .setMessage(R.string.preferences_debug_settings_reset_defaults_message)
            .setPositiveButton(R.string.preferences_debug_settings_reset_defaults_confirm) { _, _ ->
                SecretSettingsPrefDefaults(requireComponents.settings).resetAll(preferenceScreen)
                reloadPreferenceFragment()
            }
            .setNegativeButton(R.string.preferences_debug_settings_reset_defaults_cancel) { dialog, _ ->
                dialog.cancel()
            }
            .show()
    }

    override fun onPreferenceTreeClick(preference: Preference): Boolean {
        val directions =
            when (preference.key) {
                resources.getString(R.string.pref_key_remote_settings_server) -> {
                    SecretSettingsFragmentDirections.actionSecretSettingsFragmentToRemoteSettingsServerFragment()
                }

                resources.getString(R.string.pref_key_search_optimization) -> {
                    SecretSettingsFragmentDirections.actionSecretSettingsFragmentToSearchOptimizationFragment()
                }

                resources.getString(R.string.pref_key_show_debug_info) -> {
                    SecretSettingsFragmentDirections.actionSecretSettingsFragmentToDebugInfoBottomSheetFragment()
                }

                else -> return super.onPreferenceTreeClick(preference)
            }
        navigateFromSecretSettings(directions)
        return true
    }

    private fun reloadPreferenceFragment() {
        setPreferencesFromResource(R.xml.secret_settings_preferences, null)
        onCreatePreferences(null, null)
    }

    private fun navigateFromSecretSettings(directions: NavDirections) {
        view?.findNavController()?.let { navController ->
            if (navController.currentDestination?.id == R.id.secretSettingsPreference) {
                navController.navigate(directions)
            }
        }
    }
}
