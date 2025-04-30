/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen.store

import mozilla.components.lib.state.State
import org.mozilla.fenix.Config
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIState.Mode

/**
 * The state of the Download screen.
 *
 * @property items List of [FileItem] to display.
 * @property mode Current [Mode] of the Download screen.
 * @property pendingDeletionIds Set of [FileItem] IDs that are waiting to be deleted.
 * @property isDeleteDialogVisible Flag indicating whether the delete confirmation dialog is currently visible.
 * @property searchQuery The search query entered by the user. This is used to filter the list of items.
 * @param isSearchFieldRequested Indicates whether the search field is requested to be shown.
 * @param isSearchEnabled Feature flag for search functionality.
 * @param userSelectedContentTypeFilter The user selected [FileItem.ContentTypeFilter].
 */
data class DownloadUIState(
    val items: List<FileItem>,
    val mode: Mode,
    val pendingDeletionIds: Set<String>,
    val isDeleteDialogVisible: Boolean = false,
    val searchQuery: String = "",
    private val isSearchFieldRequested: Boolean = false,
    private val isSearchEnabled: Boolean = Config.channel.isNightlyOrDebug,
    private val userSelectedContentTypeFilter: FileItem.ContentTypeFilter = FileItem.ContentTypeFilter.All,
) : State {

    /**
     * The ungrouped list of items, excluding any items that are pending deletion.
     */
    private val itemsNotPendingDeletion = items.filter { it.id !in pendingDeletionIds }

    /**
     * The content type filter that is actually used to filter the items. This overrides the user
     * selected content type filter to [FileItem.ContentTypeFilter.All] if there are no items that
     * matches the user selected filter.
     */
    val selectedContentTypeFilter: FileItem.ContentTypeFilter
        get() {
            val selectedTypeContainsItems = itemsNotPendingDeletion
                .any { download -> userSelectedContentTypeFilter.predicate(download.contentType) }

            return if (selectedTypeContainsItems) {
                userSelectedContentTypeFilter
            } else {
                FileItem.ContentTypeFilter.All
            }
        }

    private val searchQueryPredicate: (FileItem) -> Boolean = {
        if (isSearchEnabled) {
            it.stringToMatchForSearchQuery.contains(searchQuery, ignoreCase = true)
        } else {
            true
        }
    }

    /**
     * The list of items to display grouped by the created time of the item.
     * The ungrouped list of items to display, excluding any items that are pending deletion and
     * that match the selected content type filter and the search query.
     */
    val itemsMatchingFilters = itemsNotPendingDeletion
        .filter { selectedContentTypeFilter.predicate(it.contentType) }
        .filter(searchQueryPredicate)

    /**
     * The list of items to display grouped by the created time of the item.
     */
    private val itemsToDisplay: List<DownloadListItem> = itemsMatchingFilters
        .groupBy { it.createdTime }
        .toSortedMap()
        .flatMap { (key, value) ->
            listOf(HeaderItem(key)) + value
        }

    /**
     * The list of content type filters that have at least one item that matches the filter.
     */
    private val matchingFilters: List<FileItem.ContentTypeFilter> =
        itemsNotPendingDeletion
            .map { it.matchingContentTypeFilter }
            .distinct()
            .sorted()

    /**
     * The list of content type filters to display.
     */
    val filtersToDisplay: List<FileItem.ContentTypeFilter> =
        if (matchingFilters.size > 1) {
            listOf(FileItem.ContentTypeFilter.All) + matchingFilters
        } else {
            emptyList()
        }

    val itemsState: ItemsState = when {
        itemsToDisplay.isEmpty() && searchQuery.isNotEmpty() -> ItemsState.NoSearchResults
        itemsToDisplay.isEmpty() -> ItemsState.NoItems
        else -> ItemsState.Items(itemsToDisplay)
    }

    val isSearchFieldVisible: Boolean
        get() = isSearchEnabled && isSearchFieldRequested && mode is Mode.Normal

    /**
     * @see [DownloadUIState].
     */
    companion object {
        val INITIAL = DownloadUIState(
            items = emptyList(),
            mode = Mode.Normal,
            pendingDeletionIds = emptySet(),
            userSelectedContentTypeFilter = FileItem.ContentTypeFilter.All,
        )
    }

    /**
     * The state of the items to display in the Download screen.
     */
    sealed interface ItemsState {
        /**
         * The state when there are no files to display.
         */
        data object NoItems : ItemsState

        /**
         * The state when there are no results to display based on the search query.
         */
        data object NoSearchResults : ItemsState

        /**
         * The state when there are files to display.
         */
        data class Items(
            val items: List<DownloadListItem>,
        ) : ItemsState
    }

    /**
     * The mode of the Download screen.
     */
    sealed class Mode {
        open val selectedItems = emptySet<FileItem>()

        /**
         * Normal mode for the Download screen.
         */
        data object Normal : Mode()

        /**
         * Editing mode for the Download screen where items can be selected.
         */
        data class Editing(override val selectedItems: Set<FileItem>) : Mode()
    }
}
