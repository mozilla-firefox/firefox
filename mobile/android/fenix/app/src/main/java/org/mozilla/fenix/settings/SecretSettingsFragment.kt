/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

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
import mozilla.appservices.remotesettings.RemoteSettingsConfig2
import mozilla.appservices.remotesettings.RemoteSettingsServer
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.Config
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.R
import org.mozilla.fenix.debugsettings.data.DefaultDebugSettingsRepository
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.GleanMetrics.DebugDrawer as DebugDrawerMetrics

class SecretSettingsFragment : PreferenceFragmentCompat() {

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_debug_settings))
    }

    @Suppress("LongMethod", "CyclomaticComplexMethod")
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

        requirePreference<SwitchPreference>(R.string.pref_key_toolbar_show_navigation_toolbar).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().navigationToolbarEnabled
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_composable_toolbar).apply {
            isVisible = Config.channel.isDebug
            isChecked = context.settings().shouldUseComposableToolbar
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_compose_top_sites).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().enableComposeTopSites
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_use_new_bookmarks_ui).apply {
            isVisible = true
            isChecked = context.settings().useNewBookmarks
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_compose_logins).apply {
            isChecked = context.settings().enableComposeLogins
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_compose_homepage).apply {
            isChecked = context.settings().enableComposeHomepage
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_menu_redesign).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().enableMenuRedesign
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_homepage_searchbar).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().enableHomepageSearchBar
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_homepage_as_new_tab).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().enableHomepageAsNewTab
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_mars_api_enabled).apply {
            isChecked = context.settings().marsAPIEnabled
            onPreferenceChangeListener = object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    activity?.recreate()
                    return super.onPreferenceChange(preference, newValue)
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_pocket_content_recommendations).apply {
            isChecked = context.settings().showContentRecommendations
            onPreferenceChangeListener = object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    activity?.recreate()
                    return super.onPreferenceChange(preference, newValue)
                }
            }
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_unified_trust_panel).apply {
            isVisible = Config.channel.isNightlyOrDebug
            isChecked = context.settings().enableUnifiedTrustPanel
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_trending_searches).apply {
            isVisible = true
            isChecked = context.settings().isTrendingSearchesVisible
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_recent_searches).apply {
            isVisible = true
            isChecked = context.settings().isRecentSearchesVisible
            onPreferenceChangeListener = SharedPreferenceUpdater()
        }

        requirePreference<SwitchPreference>(R.string.pref_key_enable_shortcuts_suggestions).apply {
            isVisible = true
            isChecked = context.settings().isShortcutSuggestionsVisible
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

        requirePreference<SwitchPreference>(R.string.pref_key_should_enable_felt_privacy).apply {
            isVisible = true
            isChecked = context.settings().feltPrivateBrowsingEnabled
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
                        RemoteSettingsConfig2(
                            server = if (newValue as? Boolean == true) {
                                RemoteSettingsServer.Prod
                            } else {
                                RemoteSettingsServer.Stage
                            },
                        ),
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

        requirePreference<SwitchPreference>(R.string.pref_key_persistent_debug_menu).apply {
            isVisible = true
            // We look up the actual value of the pref, not the `showSecretDebugMenuThisSession` setting because
            // the setting value might be set to `true` by the `SecretDebugMenuTrigger` logic for the duration of
            // the session.
            isChecked = context.settings().preferences.getBoolean(
                context.getPreferenceKey(R.string.pref_key_persistent_debug_menu),
                false,
            )
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
}
