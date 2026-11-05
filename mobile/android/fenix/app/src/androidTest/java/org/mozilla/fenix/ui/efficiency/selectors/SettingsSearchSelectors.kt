/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection

object SettingsSearchSelectors : SelectorContainer {
    val SETTINGS_SEARCH_TITLE =
        navigationToolbarTitle(
            title = getStringResource(R.string.preferences_search),
            description = "Search toolbar title",
        )

    val DEFAULT_SEARCH_ENGINE_SETTING_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "Default search engine",
            description = "Default search engine option",
            readiness = PageReadinessProfiles.READY_CONTENT,
        )

    val MANAGE_SHORTCUTS_SETTING_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_manage_search_shortcuts_2),
            description = "Manage alternative search engines option",
        )

    // The summary text shown beneath the "Manage alternative search engines" row title.
    val MANAGE_SHORTCUTS_SUMMARY = getStringResource(R.string.preferences_manage_search_shortcuts_summary)

    val SEARCH_ENGINES_SECTION_HEADER =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_search_engines),
            description = "Search engines section header",
        )

    val SEARCH_SUGGESTIONS_SECTION_HEADER =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_search_engines_suggestions),
            description = "Suggestions from search engines section header",
            scrollDirection = SwipeDirection.UP,
        )

    val ADDRESS_BAR_PREFERENCES_SECTION_HEADER =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_settings_address_bar),
            description = "Address bar preferences section header",
            scrollDirection = SwipeDirection.UP,
        )

    val GOOGLE_LENS_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.search_settings_google_lens_title),
            description = "Enable Google Lens search option",
            scrollDirection = SwipeDirection.UP,
        )

    val HOME_SCREEN_WIDGET_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_search_widget_in_home_screen),
            description = "Add search widget to Home screen option",
            scrollDirection = SwipeDirection.UP,
        )

    // Below-fold preference row; clicking the title toggles the switch (mirrors the legacy
    // toggleShowSearchSuggestions). Its scroll direction drives the harness to bring it into view first.
    val SHOW_SEARCH_SUGGESTIONS_TOGGLE =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_show_search_suggestions),
            description = "Show search suggestions toggle",
            scrollDirection = SwipeDirection.UP,
        )

    val SHOW_TRENDING_SUGGESTIONS_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_show_trending_search_suggestions),
            description = "Show trending suggestions option",
            scrollDirection = SwipeDirection.UP,
        )

    val SHOW_RECENT_SEARCHES_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_show_recent_search_suggestions),
            description = "Show recent searches option",
            scrollDirection = SwipeDirection.UP,
        )

    val SHOW_SUGGESTIONS_IN_PRIVATE_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_show_search_suggestions_in_private),
            description = "Show search suggestions in private sessions option",
            scrollDirection = SwipeDirection.UP,
        )

    val ADDRESS_BAR_FX_SUGGEST_HEADER =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preference_search_address_bar_fx_suggest),
            description = "Address bar - Firefox Suggest section header",
            scrollDirection = SwipeDirection.UP,
        )

    val SEARCH_BROWSING_HISTORY_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_search_browsing_history),
            description = "Search browsing history option",
            scrollDirection = SwipeDirection.UP,
        )

    val SEARCH_BOOKMARKS_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_search_bookmarks),
            description = "Search bookmarks option",
            scrollDirection = SwipeDirection.UP,
        )

    val SEARCH_SYNCED_TABS_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_search_synced_tabs),
            description = "Search synced tabs option",
            scrollDirection = SwipeDirection.UP,
        )

    // The non-sponsored row's title is built at runtime from the app name, mirroring
    // SearchEngineFragment's getString(preferences_show_nonsponsored_suggestions, app_name).
    val NONSPONSORED_SUGGESTIONS_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value =
                getStringResource(
                    R.string.preferences_show_nonsponsored_suggestions,
                    getStringResource(R.string.app_name),
                ),
            description = "Suggestions from the web (non-sponsored) option",
            scrollDirection = SwipeDirection.UP,
        )

    val SPONSORED_SUGGESTIONS_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_show_sponsored_suggestions),
            description = "Suggestions from sponsors option",
            scrollDirection = SwipeDirection.UP,
        )

    val LEARN_ABOUT_FX_SUGGEST_LINK =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preference_search_learn_about_fx_suggest),
            description = "Learn more about Firefox Suggest link",
            scrollDirection = SwipeDirection.UP,
        )

    val SHOW_CLIPBOARD_SUGGESTIONS_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_show_clipboard_suggestions),
            description = "Show clipboard suggestions option",
            scrollDirection = SwipeDirection.UP,
        )

    val VOICE_SEARCH_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_show_voice_search),
            description = "Show voice search option",
            scrollDirection = SwipeDirection.UP,
        )

    val AUTOCOMPLETE_URLS_OPTION =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_enable_autocomplete_urls),
            description = "Autocomplete URLs option",
            scrollDirection = SwipeDirection.UP,
        )
}
