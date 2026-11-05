/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.search

import android.appwidget.AppWidgetManager
import android.content.Context
import android.os.Bundle
import androidx.annotation.VisibleForTesting
import androidx.core.content.edit
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import androidx.preference.CheckBoxPreference
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.selectedOrDefaultPrivateSearchEngine
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.support.ktx.android.view.hideKeyboard
import org.mozilla.fenix.R
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.ext.navigateWithBreadcrumb
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.settings.SharedPreferenceUpdater
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.SwitchWithCaptionPreference
import org.mozilla.fenix.settings.requirePreference
import org.mozilla.fenix.settings.scrollToPreferenceWithHighlight
import org.mozilla.fenix.utils.canShowAddSearchWidgetPrompt
import org.mozilla.fenix.utils.maybeShowAddSearchWidgetPrompt
import org.mozilla.gecko.search.SearchWidgetProvider

/** Settings screen allowing users to configure the browser search functionality. */
class SearchEngineFragment : PreferenceFragmentCompat(), SystemInsetsPaddedFragment {
    private val args by navArgs<SearchEngineFragmentArgs>()

    override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
        setPreferencesFromResource(
            R.xml.search_settings_preferences,
            rootKey,
        )

        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_sponsored_suggestions).apply {
            isVisible = context.components.settings.enableFxSuggest
        }
        requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_nonsponsored_suggestions).apply {
            isVisible = context.components.settings.enableFxSuggest
        }
        requirePreference<CheckBoxPreference>(R.string.pref_key_search_optimization_cards).apply {
            isVisible =
                context.components.settings.enableFxSuggest && context.components.settings.isSearchOptimizationEnabled
        }
        requirePreference<Preference>(R.string.pref_key_learn_about_fx_suggest).apply {
            isVisible = context.components.settings.enableFxSuggest
        }
        requirePreference<Preference>(R.string.pref_key_search_widget_installed_2).apply {
            isVisible = canShowAddSearchWidgetPrompt(AppWidgetManager.getInstance(requireContext()))
        }

        view?.hideKeyboard()
    }

    @Suppress("LongMethod")
    override fun onResume() {
        super.onResume()
        view?.hideKeyboard()
        showToolbar(getString(R.string.preferences_search))

        val showVoiceSearchPreference =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_voice_search).apply {
                isChecked = context.components.settings.shouldShowVoiceSearch
            }

        initialiseVoiceSearchPreference(showVoiceSearchPreference)
        updateDefaultSearchEnginePreference()

        val searchSuggestionsPreference =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_search_suggestions).apply {
                isChecked = context.components.settings.shouldShowSearchSuggestions
            }

        val trendingSearchSuggestionsPreference =
            requirePreference<CheckBoxPreference>(R.string.pref_key_show_trending_search_suggestions).apply {
                isChecked = context.components.settings.trendingSearchSuggestionsEnabled
                isEnabled =
                    getSelectedSearchEngine(requireContext())?.trendingUrl != null &&
                        context.components.settings.shouldShowSearchSuggestions
            }

        val recentSearchSuggestionsPreference =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_recent_search_suggestions).apply {
                isChecked = context.components.settings.shouldShowRecentSearchSuggestions
            }

        val autocompleteURLsPreference =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_enable_autocomplete_urls).apply {
                isChecked = context.components.settings.shouldAutocompleteInAwesomebar
            }

        initialiseGoogleLensPreference(requirePreference(R.string.pref_key_google_lens_integration_user_enabled))

        val searchSuggestionsInPrivatePreference =
            requirePreference<CheckBoxPreference>(R.string.pref_key_show_search_suggestions_in_private).apply {
                isChecked = context.components.settings.shouldShowSearchSuggestionsInPrivate
                isEnabled = context.components.settings.shouldShowSearchSuggestions
            }

        val showHistorySuggestions =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_search_browsing_history).apply {
                isChecked = context.components.settings.shouldShowHistorySuggestions
            }

        val showBookmarkSuggestions =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_search_bookmarks).apply {
                isChecked = context.components.settings.shouldShowBookmarkSuggestions
            }

        val showSyncedTabsSuggestions =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_search_synced_tabs).apply {
                isChecked = context.components.settings.shouldShowSyncedTabsSuggestions
            }

        val showClipboardSuggestions =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_clipboard_suggestions).apply {
                isChecked = context.components.settings.shouldShowClipboardSuggestions
            }

        val showSponsoredSuggestionsPreference =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_sponsored_suggestions).apply {
                isChecked = context.components.settings.showSponsoredSuggestions
                summary =
                    getString(
                        R.string.preferences_show_sponsored_suggestions_summary,
                        getString(R.string.app_name),
                    )
            }

        val showNonSponsoredSuggestionsPreference =
            requirePreference<SwitchPreferenceCompat>(R.string.pref_key_show_nonsponsored_suggestions).apply {
                isChecked = context.components.settings.showNonSponsoredSuggestions
                title =
                    getString(
                        R.string.preferences_show_nonsponsored_suggestions,
                        getString(R.string.app_name),
                    )
            }
        val showSuggestionCardsPreference =
            requirePreference<CheckBoxPreference>(R.string.pref_key_search_optimization_cards).apply {
                isChecked = context.components.settings.shouldShowSearchOptimizationCards
            }

        searchSuggestionsPreference.onPreferenceChangeListener = SharedPreferenceUpdater()
        showHistorySuggestions.onPreferenceChangeListener = SharedPreferenceUpdater()
        showBookmarkSuggestions.onPreferenceChangeListener = SharedPreferenceUpdater()
        showSyncedTabsSuggestions.onPreferenceChangeListener = SharedPreferenceUpdater()
        showClipboardSuggestions.onPreferenceChangeListener = SharedPreferenceUpdater()
        searchSuggestionsInPrivatePreference.onPreferenceChangeListener = SharedPreferenceUpdater()
        trendingSearchSuggestionsPreference.onPreferenceChangeListener = SharedPreferenceUpdater()
        recentSearchSuggestionsPreference.onPreferenceChangeListener = SharedPreferenceUpdater()
        autocompleteURLsPreference.onPreferenceChangeListener = SharedPreferenceUpdater()

        searchSuggestionsPreference.setOnPreferenceClickListener {
            searchSuggestionsInPrivatePreference.isEnabled = searchSuggestionsPreference.isChecked
            trendingSearchSuggestionsPreference.isEnabled =
                getSelectedSearchEngine(requireContext())?.trendingUrl != null && searchSuggestionsPreference.isChecked
            true
        }

        showSponsoredSuggestionsPreference.onPreferenceChangeListener = SharedPreferenceUpdater()
        showNonSponsoredSuggestionsPreference.onPreferenceChangeListener = SharedPreferenceUpdater()
        showSuggestionCardsPreference.onPreferenceChangeListener = SharedPreferenceUpdater()
        showNonSponsoredSuggestionsPreference.setOnPreferenceClickListener {
            showSuggestionCardsPreference.isEnabled = showNonSponsoredSuggestionsPreference.isChecked
            true
        }

        scrollToRequestedPreference()
    }

    private fun scrollToRequestedPreference() {
        args.preferenceToScrollTo?.let {
            scrollToPreferenceWithHighlight(it)
        }
    }

    /**
     * Updates the summary of the default search engine preference to display the name of the currently selected search
     * engine.
     */
    @VisibleForTesting
    internal fun updateDefaultSearchEnginePreference() {
        with(requirePreference<Preference>(R.string.pref_key_default_search_engine)) {
            val searchState = requireContext().components.core.store.state.search
            val normalEngine = searchState.selectedOrDefaultSearchEngine
            val privateEngine = searchState.selectedOrDefaultPrivateSearchEngine
            summary =
                if (searchState.userSelectedPrivateSearchEngineId != null && normalEngine != privateEngine) {
                    val privateLabel = getString(R.string.preferences_category_select_private_search_engine)
                    "${normalEngine?.name} / ${privateEngine?.name} ($privateLabel)"
                } else {
                    normalEngine?.name
                }
        }
    }

    /**
     * Updates all search widgets. This is necessary when the visibility of the voice search button changes, as the
     * widget needs to be redrawn.
     *
     * @param context The context.
     */
    @VisibleForTesting
    internal fun updateAllWidgets(context: Context) {
        val appWidgetManager = AppWidgetManager.getInstance(context)
        SearchWidgetProvider.updateAllWidgets(context, appWidgetManager)
    }

    /**
     * Gets the currently selected search engine or the default search engine if none is selected.
     *
     * @param context The application context.
     * @return The selected or default [SearchEngine], or null if no search engines are available.
     */
    @VisibleForTesting
    internal fun getSelectedSearchEngine(context: Context): SearchEngine? {
        return context.components.core.store.state.search.selectedOrDefaultSearchEngine
    }

    /**
     * Initialises the "Show voice search" preference.
     *
     * This preference allows the user to enable or disable the voice search feature. When the preference value changes,
     * it updates the corresponding setting in SharedPreferences and triggers an update for all search widgets.
     *
     * @param showVoiceSearchPreference The [SwitchPreferenceCompat] for the "Show voice search" setting.
     */
    @VisibleForTesting
    internal fun initialiseVoiceSearchPreference(showVoiceSearchPreference: SwitchPreferenceCompat) {
        showVoiceSearchPreference.onPreferenceChangeListener =
            object : Preference.OnPreferenceChangeListener {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    val newBooleanValue = newValue as? Boolean ?: return false
                    requireComponents.settings.preferences.edit {
                        putBoolean(preference.key, newBooleanValue)
                    }
                    updateAllWidgets(requireContext())
                    return true
                }
            }
    }

    /**
     * Initialises the Google Lens search preference.
     *
     * The preference is only visible when the Nimbus-backed [Settings.googleLensIntegrationEnabled] flag is on. Its
     * checked state reflects the local-only [Settings.googleLensIntegrationUserEnabled] override and changes are
     * persisted via [SharedPreferenceUpdater]. Changing it also redraws the search widgets, which show a Lens button
     * only while the setting is on.
     *
     * @param preference The [SwitchWithCaptionPreference] for the Google Lens search setting.
     */
    @VisibleForTesting
    internal fun initialiseGoogleLensPreference(preference: SwitchWithCaptionPreference) {
        preference.isVisible = requireContext().components.settings.googleLensIntegrationEnabled
        preference.caption = getString(R.string.preferences_google_lens_availability_caption)
        preference.isChecked = requireContext().components.settings.googleLensIntegrationUserEnabled
        preference.onPreferenceChangeListener =
            object : SharedPreferenceUpdater() {
                override fun onPreferenceChange(preference: Preference, newValue: Any?): Boolean {
                    val persisted = super.onPreferenceChange(preference, newValue)
                    updateAllWidgets(requireContext())
                    return persisted
                }
            }
    }

    override fun onPreferenceTreeClick(preference: Preference): Boolean {
        when (preference.key) {
            getPreferenceKey(R.string.pref_key_default_search_engine) -> {
                openDefaultEngineSettings()
            }
            getPreferenceKey(R.string.pref_key_manage_search_shortcuts) -> {
                openSearchShortcutsSettings()
            }
            getPreferenceKey(R.string.pref_key_search_widget_installed_2) -> {
                maybeShowAddSearchWidgetPrompt(requireActivity())
            }
            getPreferenceKey(R.string.pref_key_learn_about_fx_suggest) -> {
                openLearnMoreLink()
            }
        }

        return super.onPreferenceTreeClick(preference)
    }

    /**
     * Opens the "Learn More" link for Firefox Suggest in a new browser tab.
     *
     * This function retrieves the appropriate SUMO (support.mozilla.org) URL for the Firefox Suggest topic and
     * instructs the HomeActivity to open this URL in a new browser tab.
     */
    @VisibleForTesting
    internal fun openLearnMoreLink() {
        findNavController().openToBrowser()
        requireComponents.useCases.fenixBrowserUseCases.loadUrlOrSearch(
            searchTermOrURL = SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.FX_SUGGEST),
            newTab = true,
        )
    }

    /**
     * Navigates to the search shortcuts settings screen.
     *
     * This function uses the Navigation Component to navigate from the current fragment (SearchEngineFragment) to the
     * SearchShortcutsFragment. It also logs breadcrumbs for crash reporting.
     */
    @VisibleForTesting
    internal fun openSearchShortcutsSettings() {
        val directions = SearchEngineFragmentDirections.actionSearchEngineFragmentToSearchShortcutsFragment()
        context?.let {
            findNavController()
                .navigateWithBreadcrumb(
                    directions = directions,
                    navigateFrom = "SearchEngineFragment",
                    navigateTo = "ActionSearchEngineFragmentToSearchShortcutsFragment",
                    it.components.analytics.crashReporter,
                )
        }
    }

    @VisibleForTesting
    internal fun openDefaultEngineSettings() {
        val directions = SearchEngineFragmentDirections.actionSearchEngineFragmentToDefaultEngineFragment()
        findNavController().navigate(directions)
    }
}
