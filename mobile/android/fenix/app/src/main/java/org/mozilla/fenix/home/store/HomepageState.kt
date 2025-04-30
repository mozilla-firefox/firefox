/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.store

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.feature.top.sites.TopSite
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.setup.checklist.SetupChecklistState
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.shouldShowRecentSyncedTabs
import org.mozilla.fenix.ext.shouldShowRecentTabs
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.collections.CollectionsState
import org.mozilla.fenix.home.pocket.PocketState
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabState
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.home.topsites.TopSiteColors
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.HomeScreenSection
import org.mozilla.fenix.search.SearchDialogFragment
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.wallpapers.WallpaperState

/**
 * State object that describes the homepage.
 */
internal sealed class HomepageState {

    /**
     * Height in [Dp] for the bottom of the scrollable view, based on
     * what's currently visible on the screen.
     */
    abstract val bottomSpacerHeight: Dp

    /**
     * State type corresponding with private browsing mode.
     *
     * @property feltPrivateBrowsingEnabled Whether felt private browsing is enabled.
     * @property bottomSpacerHeight Height in [Dp] for the bottom of the scrollable view, based on
     * what's currently visible on the screen.
     */
    internal data class Private(
        val feltPrivateBrowsingEnabled: Boolean,
        override val bottomSpacerHeight: Dp,
    ) : HomepageState()

    /**
     * State corresponding with the homepage in normal browsing mode.
     *
     * @property nimbusMessage Optional message to display.
     * @property topSites List of [TopSite] to display.
     * @property recentTabs List of [RecentTab] to display.
     * @property syncedTab The [RecentSyncedTab] to display.
     * @property bookmarks List of [Bookmark] to display.
     * @property recentlyVisited List of [RecentlyVisitedItem] to display.
     * @property collectionsState State of the collections section to display.
     * @property pocketState State of the pocket section to display.
     * @property showTopSites Whether to show top sites or not.
     * @property showRecentTabs Whether to show recent tabs or not.
     * @property showRecentSyncedTab Whether to show recent synced tab or not.
     * @property showBookmarks Whether to show bookmarks.
     * @property showRecentlyVisited Whether to show recent history section.
     * @property showPocketStories Whether to show the pocket stories section.
     * @property showSearchBar Whether to show the middle search bar.
     * @property searchBarEnabled Whether the middle search bar is enabled or not.
     * @property setupChecklistState Optional state of the setup checklist feature.
     * @property topSiteColors The color set defined by [TopSiteColors] used to style a top site.
     * @property cardBackgroundColor Background color for card items.
     * @property buttonBackgroundColor Background [Color] for buttons.
     * @property buttonTextColor Text [Color] for buttons.
     * @property customizeHomeButtonBackgroundColor Background [Color] for customize home button.
     * @property bottomSpacerHeight Height in [Dp] for the bottom of the scrollable view, based on
     * what's currently visible on the screen.
     */
    internal data class Normal(
        val nimbusMessage: NimbusMessageState?,
        val topSites: List<TopSite>,
        val recentTabs: List<RecentTab>,
        val syncedTab: RecentSyncedTab?,
        val bookmarks: List<Bookmark>,
        val recentlyVisited: List<RecentlyVisitedItem>,
        val collectionsState: CollectionsState,
        val pocketState: PocketState,
        val showTopSites: Boolean,
        val showRecentTabs: Boolean,
        val showRecentSyncedTab: Boolean,
        val showBookmarks: Boolean,
        val showRecentlyVisited: Boolean,
        val showPocketStories: Boolean,
        val showSearchBar: Boolean,
        val searchBarEnabled: Boolean,
        val setupChecklistState: SetupChecklistState?,
        val topSiteColors: TopSiteColors,
        val cardBackgroundColor: Color,
        val buttonBackgroundColor: Color,
        val buttonTextColor: Color,
        val customizeHomeButtonBackgroundColor: Color,
        override val bottomSpacerHeight: Dp,
    ) : HomepageState() {

        /**
         * Whether to show customize home button.
         */
        val showCustomizeHome: Boolean
            get() = showTopSites || showRecentTabs || showBookmarks || showRecentlyVisited || showPocketStories
    }

    val browsingMode: BrowsingMode
        get() = when (this) {
            is Normal -> BrowsingMode.Normal
            is Private -> BrowsingMode.Private
        }

    companion object {

        /**
         * Builds a new [HomepageState] from the current [AppState] and [Settings].
         *
         * @param appState State to build the [HomepageState] from.
         * @param browsingModeManager Manager holding current state of whether the browser is in private mode or not.
         * @param settings [Settings] corresponding to how the homepage should be displayed.
         */
        @Composable
        internal fun build(
            appState: AppState,
            browsingModeManager: BrowsingModeManager,
            settings: Settings,
        ): HomepageState {
            return with(appState) {
                if (browsingModeManager.mode.isPrivate) {
                    Private(
                        feltPrivateBrowsingEnabled = settings.feltPrivateBrowsingEnabled,
                        bottomSpacerHeight = getBottomSpace(),
                    )
                } else {
                    Normal(
                        nimbusMessage = NimbusMessageState.build(appState),
                        topSites = topSites,
                        recentTabs = recentTabs,
                        syncedTab = when (recentSyncedTabState) {
                            RecentSyncedTabState.None,
                            RecentSyncedTabState.Loading,
                            -> null

                            is RecentSyncedTabState.Success -> recentSyncedTabState.tabs.firstOrNull()
                        },
                        bookmarks = bookmarks,
                        recentlyVisited = recentHistory,
                        collectionsState = CollectionsState.build(
                            appState = appState,
                            browserState = components.core.store.state,
                            browsingModeManager = browsingModeManager,
                        ),
                        pocketState = PocketState.build(appState, settings),
                        showTopSites = settings.showTopSitesFeature && topSites.isNotEmpty(),
                        showRecentTabs = shouldShowRecentTabs(settings),
                        showBookmarks = settings.showBookmarksHomeFeature && bookmarks.isNotEmpty(),
                        showRecentSyncedTab = shouldShowRecentSyncedTabs() && showSyncedTab,
                        showRecentlyVisited = settings.historyMetadataUIFeature && recentHistory.isNotEmpty(),
                        showPocketStories = settings.showPocketRecommendationsFeature &&
                            recommendationState.pocketStories.isNotEmpty() && firstFrameDrawn,
                        showSearchBar = shouldShowSearchBar(appState = appState),
                        searchBarEnabled = settings.enableHomepageSearchBar &&
                            settings.toolbarPosition == ToolbarPosition.TOP,
                        setupChecklistState = setupChecklistState,
                        topSiteColors = TopSiteColors.colors(wallpaperState = wallpaperState),
                        cardBackgroundColor = wallpaperState.cardBackgroundColor,
                        buttonBackgroundColor = wallpaperState.buttonBackgroundColor,
                        buttonTextColor = wallpaperState.buttonTextColor,
                        customizeHomeButtonBackgroundColor = wallpaperState.customizeHomeButtonBackgroundColor(),
                        bottomSpacerHeight = getBottomSpace(),
                    )
                }
            }
        }
    }
}

private val showSyncedTab: Boolean
    get() = FxNimbus.features.homescreen.value().sectionsEnabled[HomeScreenSection.SYNCED_TABS] == true

@Composable
private fun WallpaperState.customizeHomeButtonBackgroundColor(): Color {
    var buttonColor: Color = FirefoxTheme.colors.actionTertiary

    composeRunIfWallpaperCardColorsAreAvailable { cardColorLight, cardColorDark ->
        buttonColor = if (isSystemInDarkTheme()) {
            cardColorDark
        } else {
            cardColorLight
        }
    }

    return buttonColor
}

@Composable
private fun getBottomSpace(): Dp {
    val toolbarHeight = LocalContext.current.settings().getBottomToolbarContainerHeight().dp
    // We need this 88 dp because of this bug: https://github.com/mozilla-mobile/fenix/issues/20833
    val extraSpace = 88.dp

    return toolbarHeight + extraSpace + HOME_APP_BAR_HEIGHT
}

/**
 * Returns whether the search bar should be shown. Only show if the search dialog
 * [SearchDialogFragment] is not visible, and the user does not have their toolbar set to be on the
 * bottom, and the screen is not in landscape mode. This is in addition to logic in the view layer
 * which hides the middle search bar when the users scrolls down. This is separate from the middle
 * search bar being enabled in settings since the toolbar address bar needs to react to the middle
 * search bar's visibility.
 */
private fun shouldShowSearchBar(appState: AppState) =
    !appState.isSearchDialogVisible

private val HOME_APP_BAR_HEIGHT = 48.dp
