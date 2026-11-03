/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.store

import androidx.compose.runtime.Composable
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.setup.checklist.SetupChecklistState
import org.mozilla.fenix.components.components
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.ext.shouldShowRecentSyncedTabs
import org.mozilla.fenix.ext.shouldShowRecentTabs
import org.mozilla.fenix.home.bookmarks.Bookmark
import org.mozilla.fenix.home.collections.CollectionsState
import org.mozilla.fenix.home.collections.migration.CollectionsMigrationCardState
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTab
import org.mozilla.fenix.home.recentsyncedtabs.RecentSyncedTabState
import org.mozilla.fenix.home.recenttabs.RecentTab
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem
import org.mozilla.fenix.home.topsites.TopSiteState
import org.mozilla.fenix.termsofuse.store.PrivacyNoticeBannerState
import org.mozilla.fenix.utils.Settings

/** State object that describes the homepage. */
internal sealed class HomepageState {

    /** Data related to the header of the homepage. */
    abstract val headerState: HeaderState

    /** State of the middle search bar on the homepage. */
    abstract val middleSearchState: MiddleSearchState

    /** Flag indicating whether the first frame of the homescreen has been drawn. */
    abstract val firstFrameDrawn: Boolean

    /** Whether search is currently active on the homepage. */
    abstract val isSearchInProgress: Boolean

    /**
     * State type corresponding with private browsing mode.
     *
     * @property headerState State related to the header of the homepage.
     * @property middleSearchState State of the middle search bar on the homepage.
     * @property firstFrameDrawn Flag indicating whether the first frame of the homescreen has been drawn.
     * @property isSearchInProgress Whether search is currently active on the homepage.
     */
    internal data class Private(
        override val headerState: HeaderState,
        override val middleSearchState: MiddleSearchState = MiddleSearchState(),
        override val firstFrameDrawn: Boolean = false,
        override val isSearchInProgress: Boolean,
    ) : HomepageState()

    /**
     * State corresponding with the homepage in normal browsing mode.
     *
     * @property shouldShowPrivacyNoticeBanner If the privacy notice banner should show.
     * @property nimbusMessage Optional message to display.
     * @property topSiteState State of the top sites section to display, or null when the section is hidden.
     * @property recentTabs List of [RecentTab] to display, or null when the recent tabs section is hidden.
     * @property recentSyncedTabSectionState State of the recent synced tab section to display.
     * @property bookmarks List of [Bookmark] to display, or null when the bookmarks section is hidden.
     * @property recentlyVisited List of [RecentlyVisitedItem] to display, or null when the recent history section is
     *   hidden.
     * @property collectionsState State of the collections section to display.
     * @property showTopSitesHeader Whether to show the shortcuts section header and "show all" button.
     * @property showPrivacyReport Whether to show the privacy report section.
     * @property longfoxEnabled Whether the longfox game is enabled.
     * @property showLongfoxAnimation Whether to play the fox peek animation on the privacy report card.
     * @property trackersBlockedCount The number of trackers blocked for the privacy report.
     * @property headerState State related to the header of the homepage.
     * @property middleSearchState State of the middle search bar on the homepage.
     * @property firstFrameDrawn Flag indicating whether the first frame of the homescreen has been drawn.
     * @property setupChecklistState Optional state of the setup checklist feature.
     * @property isSearchInProgress Whether search is currently active on the homepage.
     * @property bottomPadding Amount of padding to display at the bottom of the homepage.
     */
    internal data class Normal(
        val shouldShowPrivacyNoticeBanner: Boolean,
        val nimbusMessage: NimbusMessageState?,
        val topSiteState: TopSiteState? = null,
        val recentTabs: List<RecentTab>? = null,
        val recentSyncedTabSectionState: RecentSyncedTabSectionState = RecentSyncedTabSectionState.Gone,
        val bookmarks: List<Bookmark>? = null,
        val recentlyVisited: List<RecentlyVisitedItem>? = null,
        val collectionsState: CollectionsState,
        val showTopSitesHeader: Boolean,
        val showPrivacyReport: Boolean,
        val longfoxEnabled: Boolean,
        val showLongfoxAnimation: Boolean,
        val trackersBlockedCount: Int,
        override val headerState: HeaderState,
        override val middleSearchState: MiddleSearchState = MiddleSearchState(),
        override val firstFrameDrawn: Boolean = false,
        val setupChecklistState: SetupChecklistState?,
        override val isSearchInProgress: Boolean,
        val bottomPadding: Int,
    ) : HomepageState()

    val browsingMode: BrowsingMode
        get() =
            when (this) {
                is Normal -> BrowsingMode.Normal
                is Private -> BrowsingMode.Private
            }

    companion object {
        private const val BOTTOM_PADDING_TOP_TOOLBAR = 68
        private const val BOTTOM_PADDING_BOTTOM_TOOLBAR = 32

        /**
         * Builds a new [HomepageState] from the current [AppState] and [Settings].
         *
         * @param appState State to build the [HomepageState] from.
         * @param privacyNoticeBannerState State of the privacy notice banner.
         * @param collectionsMigrationCardState State of the collections migration card.
         * @param browsingModeManager Manager holding current state of whether the browser is in private mode or not.
         * @param settings [Settings] corresponding to how the homepage should be displayed.
         */
        @Composable
        internal fun build(
            appState: AppState,
            privacyNoticeBannerState: PrivacyNoticeBannerState,
            collectionsMigrationCardState: CollectionsMigrationCardState,
            browsingModeManager: BrowsingModeManager,
            settings: Settings,
        ): HomepageState {
            return if (browsingModeManager.mode.isPrivate) {
                buildPrivateState(
                    appState = appState,
                    settings = settings,
                )
            } else {
                buildNormalState(
                    appState = appState,
                    privacyNoticeBannerState = privacyNoticeBannerState,
                    collectionsMigrationCardState = collectionsMigrationCardState,
                    settings = settings,
                )
            }
        }

        /**
         * Builds a new [HomepageState.Private] from the current [AppState] and [Settings].
         *
         * @param appState State to build the [HomepageState.Private] from.
         * @param settings [Settings] to build the [HomepageState.Private] from.
         */
        @Composable
        private fun buildPrivateState(
            appState: AppState,
            settings: Settings,
        ) =
            with(appState) {
                Private(
                    headerState = buildPrivateHeaderState(settings = settings),
                    firstFrameDrawn = firstFrameDrawn,
                    isSearchInProgress = searchState.isSearchActive,
                    middleSearchState = buildMiddleSearchState(appState = appState, settings = settings),
                )
            }

        /**
         * Builds a new [HomepageState.Normal] from the current [AppState] and [Settings].
         *
         * @param appState State to build the [HomepageState.Normal] from.
         * @param privacyNoticeBannerState State of the privacy notice banner.
         * @param collectionsMigrationCardState State of the collections migration card.
         * @param settings [Settings] corresponding to how the homepage should be displayed.
         */
        @Composable
        private fun buildNormalState(
            appState: AppState,
            privacyNoticeBannerState: PrivacyNoticeBannerState,
            collectionsMigrationCardState: CollectionsMigrationCardState,
            settings: Settings,
        ) =
            with(appState) {
                Normal(
                    shouldShowPrivacyNoticeBanner = privacyNoticeBannerState.visible,
                    nimbusMessage = NimbusMessageState.build(appState, privacyNoticeBannerState),
                    topSiteState = TopSiteState.build(appState = appState, settings = settings),
                    recentTabs = recentTabs.takeIf { shouldShowRecentTabs(settings) },
                    recentSyncedTabSectionState = buildRecentSyncedTabSectionState(settings),
                    bookmarks = bookmarks.takeIf { settings.showBookmarksHomeFeature && it.isNotEmpty() },
                    recentlyVisited =
                        recentHistory.takeIf {
                            settings.historyMetadataUIFeature && it.isNotEmpty()
                        },
                    collectionsState =
                        CollectionsState.build(
                            appState = appState,
                            browserState = components.core.store.state,
                            showCollections = settings.collections,
                            shouldShowCollectionsMigrationCard = collectionsMigrationCardState.visible,
                        ),
                    showTopSitesHeader = !settings.showMoreShortcuts,
                    showPrivacyReport = settings.showPrivacyReportFeature,
                    longfoxEnabled = settings.longfoxEnabled,
                    showLongfoxAnimation = settings.longfoxEnabled && longfoxEntryPointReady,
                    trackersBlockedCount = blockedTrackersState.trackersBlockedCount,
                    headerState = buildHeaderState(settings = settings),
                    middleSearchState = buildMiddleSearchState(appState = appState, settings = settings),
                    firstFrameDrawn = firstFrameDrawn,
                    setupChecklistState = setupChecklistState,
                    isSearchInProgress = searchState.isSearchActive,
                    bottomPadding =
                        if (settings.toolbarPosition == ToolbarPosition.TOP) {
                            BOTTOM_PADDING_TOP_TOOLBAR
                        } else {
                            BOTTOM_PADDING_BOTTOM_TOOLBAR
                        },
                )
            }
    }
}

private fun buildHeaderState(settings: Settings) =
    HeaderState(
        showStoriesButton = settings.showPocketRecommendationsFeature,
        showButtons = !settings.isHomepageMiddleSearchEnabled,
    )

@Composable
private fun buildMiddleSearchState(appState: AppState, settings: Settings) =
    MiddleSearchState(
        searchBarVisible = shouldShowSearchBar(appState = appState),
        searchBarEnabled = settings.isHomepageMiddleSearchEnabled,
    )

private fun buildPrivateHeaderState(settings: Settings) =
    HeaderState(
        showStoriesButton = false,
        showButtons = !settings.isHomepageMiddleSearchEnabled,
    )

/**
 * State required for the homepage header in both normal and private mode. The header's colors are derived from the
 * wallpaper at render time (see `HomepageHeader`), so no colors are held here.
 *
 * @property showStoriesButton Whether to show the stories button.
 * @property showButtons Whether to show the stories and private browsing buttons.
 */
internal data class HeaderState(
    val showStoriesButton: Boolean,
    val showButtons: Boolean = true,
)

/**
 * State of the middle search bar on the homepage.
 *
 * @property searchBarVisible Whether the middle search bar should be visible or not.
 * @property searchBarEnabled Whether the middle search bar is enabled or not.
 */
internal data class MiddleSearchState(
    val searchBarVisible: Boolean = false,
    val searchBarEnabled: Boolean = false,
) {
    /** Whether the middle search bar should be shown, i.e. it is both enabled and visible. */
    val isShown: Boolean
        get() = searchBarEnabled && searchBarVisible
}

/** Represents the state of the recent synced tab section on the homepage. */
internal sealed class RecentSyncedTabSectionState {

    /** The section is not shown. */
    data object Gone : RecentSyncedTabSectionState()

    /** The section is shown while the synced tab is still being resolved and a placeholder is displayed. */
    data object Loading : RecentSyncedTabSectionState()

    /**
     * The section is shown with a synced tab to display.
     *
     * @property tab The [RecentSyncedTab] to display.
     */
    data class Visible(val tab: RecentSyncedTab) : RecentSyncedTabSectionState()
}

/** Builds the [RecentSyncedTabSectionState] for the homepage from the current [AppState] and [Settings]. */
private fun AppState.buildRecentSyncedTabSectionState(settings: Settings): RecentSyncedTabSectionState {
    if (!(shouldShowRecentSyncedTabs(settings))) {
        return RecentSyncedTabSectionState.Gone
    }

    val tab = (recentSyncedTabState as? RecentSyncedTabState.Success)?.tabs?.firstOrNull()
    return tab?.let { RecentSyncedTabSectionState.Visible(it) } ?: RecentSyncedTabSectionState.Loading
}

/**
 * Returns whether the search bar should be shown. Only show if search is not active, and the user does not have their
 * toolbar set to be on the bottom, and the screen is not in landscape mode. This is in addition to logic in the view
 * layer which hides the middle search bar when the users scrolls down. This is separate from the middle search bar
 * being enabled in settings since the toolbar address bar needs to react to the middle search bar's visibility.
 */
private fun shouldShowSearchBar(appState: AppState) = !appState.searchState.isSearchActive
