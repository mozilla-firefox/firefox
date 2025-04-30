/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.recentvisits.controller

import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.HistoryMetadataAction
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.storage.HistoryMetadataStorage
import mozilla.components.feature.tabs.TabsUseCases.SelectOrAddUseCase
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.RecentSearches
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryGroup
import org.mozilla.fenix.home.recentvisits.RecentlyVisitedItem.RecentHistoryHighlight
import org.mozilla.fenix.library.history.toHistoryMetadata
import org.mozilla.fenix.utils.Settings

/**
 * All possible updates following user interactions with the "Recent visits" section from the Home screen.
 */
interface RecentVisitsController {

    /**
     * Callback for when the "Show all" link is clicked.
     */
    fun handleHistoryShowAllClicked()

    /**
     * Callback for when the user clicks on a specific [RecentHistoryGroup].
     *
     * @param recentHistoryGroup The just clicked [RecentHistoryGroup].
     */
    fun handleRecentHistoryGroupClicked(recentHistoryGroup: RecentHistoryGroup)

    /**
     * Callback for when the user removes a certain [RecentHistoryGroup].
     *
     * @param groupTitle Title of the [RecentHistoryGroup] to remove.
     */
    fun handleRemoveRecentHistoryGroup(groupTitle: String)

    /**
     * Callback for when the user clicks on a specific [RecentHistoryHighlight].
     *
     * @param recentHistoryHighlight The just clicked [RecentHistoryHighlight].
     */
    fun handleRecentHistoryHighlightClicked(recentHistoryHighlight: RecentHistoryHighlight)

    /**
     * Callback for when the user removes a certain [RecentHistoryHighlight].
     *
     * @param highlightUrl Url of the [RecentHistoryHighlight] to remove.
     */
    fun handleRemoveRecentHistoryHighlight(highlightUrl: String)
}

/**
 * The default implementation of [RecentVisitsController].
 */
@Suppress("LongParameterList")
class DefaultRecentVisitsController(
    private val store: BrowserStore,
    private val appStore: AppStore,
    private val settings: Settings,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val selectOrAddTabUseCase: SelectOrAddUseCase,
    private val navController: NavController,
    private val storage: HistoryMetadataStorage,
    private val scope: CoroutineScope,
) : RecentVisitsController {

    /**
     * Shows the history fragment.
     */
    override fun handleHistoryShowAllClicked() {
        navController.navigate(
            HomeFragmentDirections.actionGlobalHistoryFragment(),
        )
    }

    /**
     * Navigates to the history metadata group fragment to display the group.
     *
     * @param recentHistoryGroup The [RecentHistoryGroup] to which to navigate to.
     */
    override fun handleRecentHistoryGroupClicked(recentHistoryGroup: RecentHistoryGroup) {
        navController.navigate(
            HomeFragmentDirections.actionGlobalHistoryMetadataGroup(
                title = recentHistoryGroup.title,
                historyMetadataItems = recentHistoryGroup.historyMetadata
                    .mapIndexed { index, item -> item.toHistoryMetadata(index) }.toTypedArray(),
            ),
        )
    }

    /**
     * Removes a [RecentHistoryGroup] with the given title from the homescreen.
     *
     * @param groupTitle The title of the [RecentHistoryGroup] to be removed.
     */
    override fun handleRemoveRecentHistoryGroup(groupTitle: String) {
        // We want to update the UI right away in response to user action without waiting for the IO.
        // First, dispatch actions that will clean up search groups in the two stores that have
        // metadata-related state.
        store.dispatch(HistoryMetadataAction.DisbandSearchGroupAction(searchTerm = groupTitle))
        appStore.dispatch(AppAction.DisbandSearchGroupAction(searchTerm = groupTitle))
        // Then, perform the expensive IO work of removing search groups from storage.
        scope.launch {
            storage.deleteHistoryMetadata(groupTitle)
        }
        RecentSearches.groupDeleted.record(NoExtras())
    }

    /**
     * Switch to an already open tab for [recentHistoryHighlight] if one exists or
     * create a new tab in which to load this item's URL.
     *
     * @param recentHistoryHighlight the just clicked [RecentHistoryHighlight] to open in browser.
     */
    override fun handleRecentHistoryHighlightClicked(recentHistoryHighlight: RecentHistoryHighlight) {
        if (settings.enableHomepageAsNewTab) {
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = recentHistoryHighlight.url,
                newTab = false,
                private = false,
            )
        } else {
            selectOrAddTabUseCase.invoke(recentHistoryHighlight.url)
        }

        navController.navigate(R.id.browserFragment)
    }

    /**
     * Removes a [RecentHistoryHighlight] with the given title from the homescreen.
     *
     * @param highlightUrl The title of the [RecentHistoryHighlight] to be removed.
     */
    override fun handleRemoveRecentHistoryHighlight(highlightUrl: String) {
        appStore.dispatch(AppAction.RemoveRecentHistoryHighlight(highlightUrl))
        scope.launch {
            storage.deleteHistoryMetadataForUrl(highlightUrl)
        }
    }
}
