/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.Context
import android.os.Bundle
import androidx.annotation.VisibleForTesting
import androidx.core.content.edit
import androidx.navigation.findNavController
import androidx.navigation.fragment.navArgs
import androidx.preference.CheckBoxPreference
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import org.mozilla.fenix.GleanMetrics.CustomizeHome
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.R
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.navigateWithBreadcrumb
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.home.pocket.ContentRecommendationsFeatureHelper
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.view.addToRadioGroup

/**
 * A [PreferenceFragmentCompat] that displays settings for customizing the Firefox home screen.
 *
 * User interactions with these preferences are persisted in [Settings] and may trigger telemetry events via
 * [CustomizeHome] metrics.
 */
class HomeSettingsFragment : PreferenceFragmentCompat(), SystemInsetsPaddedFragment {

    private val args by navArgs<HomeSettingsFragmentArgs>()

    @VisibleForTesting internal var customizeHomeMetrics: CustomizeHome = CustomizeHome

    @VisibleForTesting
    internal var contentRecommendationsHelper: ContentRecommendationsFeatureHelper = ContentRecommendationsFeatureHelper

    @VisibleForTesting internal lateinit var fenixSettings: Settings

    @VisibleForTesting internal lateinit var fenixComponents: Components

    override fun onAttach(context: Context) {
        super.onAttach(context)
        if (!::fenixComponents.isInitialized) {
            fenixComponents = context.components
        }
        if (!::fenixSettings.isInitialized) {
            fenixSettings = fenixComponents.settings
        }
    }

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(R.xml.home_preferences, rootKey)
        setupPreferences()
    }

    override fun onResume() {
        super.onResume()
        showToolbar(getString(R.string.preferences_home_2))
        args.preferenceToScrollTo?.let {
            scrollToPreferenceWithHighlight(it)
        }
    }

    private fun setupPreferences() {
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_top_sites).apply {
            isChecked = fenixSettings.showTopSitesFeature
            onPreferenceChangeListener = createMetricPreferenceChangeListener("most_visited_sites")
        }

        requirePreference<CheckBoxPreference>(R.string.pref_key_enable_contile).apply {
            isChecked = fenixSettings.showContileFeature
            onPreferenceChangeListener = createMetricPreferenceChangeListener("contile")
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_privacy_report).apply {
            if (fenixSettings.longfoxEnabled) title = resources.getString(R.string.help_catch_trackers)
            isChecked = fenixSettings.showPrivacyReportFeature
            onPreferenceChangeListener =
                createMetricPreferenceChangeListener(
                    metricKey = getString(R.string.pref_key_privacy_report_metric),
                    recordEventsToggle = true,
                )
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_recent_tabs).apply {
            isChecked = fenixSettings.showRecentTabsFeature
            onPreferenceChangeListener = createMetricPreferenceChangeListener("jump_back_in")
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_customization_bookmarks).apply {
            isChecked = fenixSettings.showBookmarksHomeFeature
            onPreferenceChangeListener = createMetricPreferenceChangeListener("bookmarks")
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_pocket_homescreen_recommendations).apply {
            isVisible = contentRecommendationsHelper.isContentRecommendationsFeatureEnabled(requireContext())
            isChecked = fenixSettings.showPocketRecommendationsFeature
            onPreferenceChangeListener = createMetricPreferenceChangeListener("pocket")
        }

        requirePreference<CheckBoxPreference>(R.string.pref_key_pocket_sponsored_stories).apply {
            isVisible = contentRecommendationsHelper.isPocketSponsoredStoriesFeatureEnabled(requireContext())
            isChecked = fenixSettings.showPocketSponsoredStories
            onPreferenceChangeListener = Preference.OnPreferenceChangeListener { preference, newValue ->
                val newBooleanValue = newValue as? Boolean ?: return@OnPreferenceChangeListener false

                when (newBooleanValue) {
                    true -> {
                        fenixComponents.core.pocketStoriesService.startPeriodicSponsoredContentsRefresh()
                    }
                    false -> {
                        fenixComponents.core.pocketStoriesService.deleteUser()

                        fenixComponents.appStore.dispatch(
                            AppAction.ContentRecommendationsAction.SponsoredContentsChange(
                                sponsoredContents = emptyList()
                            )
                        )
                    }
                }

                fenixSettings.preferences.edit { putBoolean(preference.key, newBooleanValue) }
                true
            }
        }

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_history_metadata_feature).apply {
            isChecked = fenixSettings.historyMetadataUIFeature
            onPreferenceChangeListener = createMetricPreferenceChangeListener("recently_visited")
        }

        requirePreference<Preference>(R.string.pref_key_wallpapers).apply {
            setOnPreferenceClickListener {
                view
                    ?.findNavController()
                    ?.navigateWithBreadcrumb(
                        directions =
                            HomeSettingsFragmentDirections.actionHomeSettingsFragmentToWallpaperSettingsFragment(),
                        navigateFrom = "HomeSettingsFragment",
                        navigateTo = "ActionHomeSettingsFragmentToWallpaperSettingsFragment",
                        crashReporter = fenixComponents.analytics.crashReporter,
                    )
                true
            }
        }

        setupOpeningScreenPreferences()
        setupWeatherPreference()
        setupWrexhamWidgetPreference()
    }

    private fun createMetricPreferenceChangeListener(
        metricKey: String,
        recordEventsToggle: Boolean = false,
    ): Preference.OnPreferenceChangeListener {
        return Preference.OnPreferenceChangeListener { preference, newValue ->
            val newBooleanValue = newValue as? Boolean ?: return@OnPreferenceChangeListener false

            customizeHomeMetrics.preferenceToggled.record(
                CustomizeHome.PreferenceToggledExtra(
                    newBooleanValue,
                    metricKey,
                )
            )

            if (recordEventsToggle) {
                Events.preferenceToggled.record(
                    Events.PreferenceToggledExtra(
                        newBooleanValue,
                        metricKey,
                    )
                )
            }

            fenixSettings.preferences.edit { putBoolean(preference.key, newBooleanValue) }

            true
        }
    }

    private fun setupOpeningScreenPreferences() {
        val openingScreenRadioHomepage =
            requirePreference<RadioButtonPreference>(R.string.pref_key_start_on_home_always).apply {
                setDefaultValue(fenixSettings.alwaysOpenTheHomepageWhenOpeningTheApp)
            }
        val openingScreenLastTab =
            requirePreference<RadioButtonPreference>(R.string.pref_key_start_on_home_never).apply {
                setDefaultValue(fenixSettings.alwaysOpenTheLastTabWhenOpeningTheApp)
            }
        val openingScreenAfterFourHours =
            requirePreference<RadioButtonPreference>(R.string.pref_key_start_on_home_after_four_hours).apply {
                setDefaultValue(fenixSettings.openHomepageAfterFourHoursOfInactivity)
            }

        addToRadioGroup(
            openingScreenRadioHomepage,
            openingScreenLastTab,
            openingScreenAfterFourHours,
        )
    }

    private fun setupWeatherPreference() {
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_homepage_weather_widget).apply {
            isVisible = fenixSettings.enableHomepageWeatherWidget
            isChecked = fenixSettings.showHomepageWeatherWidget
            onPreferenceChangeListener = createMetricPreferenceChangeListener("weather")
        }
    }

    private fun setupWrexhamWidgetPreference() {
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_homepage_wrexham_widget).apply {
            isVisible = fenixSettings.enableHomepageWrexhamWidget
            isChecked = fenixSettings.showHomepageWrexhamWidget
            val metricListener = createMetricPreferenceChangeListener("wrexham_widget")
            onPreferenceChangeListener = Preference.OnPreferenceChangeListener { preference, newValue ->
                val handled = metricListener.onPreferenceChange(preference, newValue)
                if (handled) {
                    fenixComponents.appStore.dispatch(
                        AppAction.SportsWidgetAction.VisibilityChanged(isVisible = newValue as Boolean)
                    )
                }
                handled
            }
        }
    }
}
