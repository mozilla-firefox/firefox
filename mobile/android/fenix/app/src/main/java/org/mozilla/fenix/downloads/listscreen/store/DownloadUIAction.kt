/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen.store

import mozilla.components.lib.state.Action

/**
 * Actions to dispatch through the `DownloadStore` to modify `DownloadState` through the reducer.
 */
sealed interface DownloadUIAction : Action {
    /**
     * [DownloadUIAction] to initialize the state.
     */
    data object Init : DownloadUIAction

    /**
     * [DownloadUIAction] to exit edit mode.
     */
    data object ExitEditMode : DownloadUIAction

    /**
     * [DownloadUIAction] add an item to the removal list.
     */
    data class AddItemForRemoval(val item: FileItem) : DownloadUIAction

    /**
     * [DownloadUIAction] to add all items to the removal list.
     */
    data object AddAllItemsForRemoval : DownloadUIAction

    /**
     * [DownloadUIAction] to remove an item from the removal list.
     */
    data class RemoveItemForRemoval(val item: FileItem) : DownloadUIAction

    /**
     * [DownloadUIAction] to add a set of [FileItem] IDs to the pending deletion set.
     */
    data class AddPendingDeletionSet(val itemIds: Set<String>) : DownloadUIAction

    /**
     * [DownloadUIAction] to undo a set of [FileItem] IDs from the pending deletion set.
     */
    data class UndoPendingDeletionSet(val itemIds: Set<String>) : DownloadUIAction

    /**
     * [DownloadUIAction] when a file item is deleted successfully.
     */
    data object FileItemDeletedSuccessfully : DownloadUIAction

    /**
     * [DownloadUIAction] to update the list of [FileItem]s.
     */
    data class UpdateFileItems(val items: List<FileItem>) : DownloadUIAction

    /**
     * [DownloadUIAction] to select a content type filter.
     */
    data class ContentTypeSelected(val contentTypeFilter: FileItem.ContentTypeFilter) :
        DownloadUIAction

    /**
     * [DownloadUIAction] to share the URL of a [FileItem].
     */
    data class ShareUrlClicked(val url: String) : DownloadUIAction

    /**
     * [DownloadUIAction] to share the file of a [FileItem].
     */
    data class ShareFileClicked(val filePath: String, val contentType: String?) : DownloadUIAction

    /**
     * [DownloadUIAction] when a search query is entered.
     */
    data class SearchQueryEntered(val searchQuery: String) : DownloadUIAction

    /**
     * [DownloadUIAction] to show or hide the delete confirmation dialog.
     */
    data class UpdateDeleteDialogVisibility(val visibility: Boolean) : DownloadUIAction

    /**
     * [DownloadUIAction] to show the search bar.
     */
    data object SearchBarVisibilityRequest : DownloadUIAction

    /**
     * [DownloadUIAction] to hide the search bar.
     */
    data object SearchBarDismissRequest : DownloadUIAction
}
