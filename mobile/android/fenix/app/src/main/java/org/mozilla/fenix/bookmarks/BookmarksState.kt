/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.lib.state.State

internal sealed class BookmarksListSortOrder {
    abstract val asString: String
    abstract val comparator: Comparator<BookmarkItem>

    data class Created(val ascending: Boolean) : BookmarksListSortOrder() {
        override val asString: String
            get() = "created-$ascending"

        override val comparator: Comparator<BookmarkItem>
            get() = compareBy<BookmarkItem> { it.dateAdded }.let {
                if (ascending) it.reversed() else it
            }
    }

    data class Alphabetical(val ascending: Boolean) : BookmarksListSortOrder() {
        override val asString: String
            get() = "alphabetical-$ascending"

        override val comparator: Comparator<BookmarkItem>
            get() = compareByDescending<BookmarkItem> { it.title.lowercase() }.let {
                if (ascending) it.reversed() else it
            }
    }

    data object Positional : BookmarksListSortOrder() {
        override val asString: String
            get() = "positional"

        override val comparator: Comparator<BookmarkItem>
            get() = compareBy<BookmarkItem> { it.position }
    }

    companion object {
        val default: BookmarksListSortOrder
            get() = Alphabetical(true)

        fun fromString(value: String, default: BookmarksListSortOrder = Alphabetical(true)): BookmarksListSortOrder {
            return when (value) {
                "positional" -> Positional
                "created-true" -> Created(true)
                "created-false" -> Created(false)
                "alphabetical-true" -> Alphabetical(true)
                "alphabetical-false" -> Alphabetical(false)
                else -> default
            }
        }
    }
}

/**
 * Represents the state of the Bookmarks list screen and its various subscreens.
 *
 * @property bookmarkItems Bookmark items to be displayed in the current list screen.
 * @property selectedItems The bookmark items that are currently selected by the user for bulk actions.
 * @property rootMenuShown Whether the root bookmarks overflow menu is shown.
 * @property launchFilePicker Whether the file picker should be launched for bookmarks import.
 * @property sortMenuShown Whether the bookmark sorting menu is shown.
 * @property sortOrder Describes how to sort the bookmark list.
 * @property recursiveSelectedCount the total number of children of the [selectedItems] found in bookmark storage.
 * @property currentFolder the [BookmarkItem.Folder] that is currently being displayed.
 * @property isSignedIntoSync State representing if the user is currently signed into sync.
 * @property openTabsConfirmationDialog State representing the confirmation dialog state.
 * @property bookmarksDeletionDialogState State representing the deletion dialog state.
 * @property bookmarksSnackbarState State representing which snackbar to show.
 * @property bookmarksAddFolderState State representing the add folder subscreen, if visible.
 * @property bookmarksEditBookmarkState State representing the edit bookmark subscreen, if visible.
 * @property bookmarksSelectFolderState State representing the select folder subscreen, if visible.
 * @property bookmarksEditFolderState State representing the edit folder subscreen, if visible.
 * @property bookmarksMultiselectMoveState State representing multi-select moving.
 * @property isLoading State representing if the initial load has completed.
 * @property isSearching State representing if currently in search mode.
 */
internal data class BookmarksState(
    val bookmarkItems: List<BookmarkItem>,
    val selectedItems: List<BookmarkItem>,
    val rootMenuShown: Boolean,
    val launchFilePicker: Boolean,
    val sortMenuShown: Boolean,
    val sortOrder: BookmarksListSortOrder,
    val recursiveSelectedCount: Int?,
    val currentFolder: BookmarkItem.Folder,
    val isSignedIntoSync: Boolean,
    val openTabsConfirmationDialog: OpenTabsConfirmationDialog,
    val bookmarksDeletionDialogState: DeletionDialogState,
    val bookmarksSnackbarState: BookmarksSnackbarState,
    val bookmarksAddFolderState: BookmarksAddFolderState?,
    val bookmarksEditBookmarkState: BookmarksEditBookmarkState?,
    val bookmarksSelectFolderState: BookmarksSelectFolderState?,
    val bookmarksEditFolderState: BookmarksEditFolderState?,
    val bookmarksMultiselectMoveState: MultiselectMoveState?,
    val isLoading: Boolean,
    val isSearching: Boolean,
) : State {
    val showNewFolderButton: Boolean
        get() = bookmarksSelectFolderState?.innerSelectionGuid == null &&
            bookmarksAddFolderState == null && bookmarksEditFolderState == null

    companion object {
        val default: BookmarksState = BookmarksState(
            bookmarkItems = listOf(),
            selectedItems = listOf(),
            rootMenuShown = false,
            launchFilePicker = false,
            sortMenuShown = false,
            sortOrder = BookmarksListSortOrder.default,
            recursiveSelectedCount = null,
            currentFolder = BookmarkItem.Folder("", "", null),
            isSignedIntoSync = false,
            openTabsConfirmationDialog = OpenTabsConfirmationDialog.None,
            bookmarksSnackbarState = BookmarksSnackbarState.None,
            bookmarksDeletionDialogState = DeletionDialogState.None,
            bookmarksAddFolderState = null,
            bookmarksEditBookmarkState = null,
            bookmarksSelectFolderState = null,
            bookmarksEditFolderState = null,
            bookmarksMultiselectMoveState = null,
            isLoading = true,
            isSearching = false,
        )
    }
}

internal fun BookmarksState.isGuidBeingMoved(guid: String): Boolean {
    return bookmarksMultiselectMoveState?.guidsToMove?.contains(guid) ?: false ||
        bookmarksEditFolderState?.folder?.guid == guid
}

internal data class MultiselectMoveState(
    val guidsToMove: List<String>,
    val destination: String,
)

internal sealed class DeletionDialogState {
    data object None : DeletionDialogState()
    data class LoadingCount(val guidsToDelete: List<String>) : DeletionDialogState()
    data class Presenting(
        val guidsToDelete: List<String>,
        val recursiveCount: Int,
    ) : DeletionDialogState()
}

internal sealed class OpenTabsConfirmationDialog {
    data object None : OpenTabsConfirmationDialog()
    data class Presenting(
        val guidToOpen: String,
        val numberOfTabs: Int,
        val isPrivate: Boolean,
    ) : OpenTabsConfirmationDialog()
}

internal val DeletionDialogState.Presenting.count
    get() = guidsToDelete.size + recursiveCount

internal val DeletionDialogState.guidsToDelete: List<String>
    get() = when (this) {
        DeletionDialogState.None -> listOf()
        is DeletionDialogState.LoadingCount -> guidsToDelete
        is DeletionDialogState.Presenting -> guidsToDelete
    }

internal sealed class BookmarksSnackbarState {
    data object None : BookmarksSnackbarState()
    data object CantEditDesktopFolders : BookmarksSnackbarState()
    data class BookmarkMoved(val from: String, val to: String) : BookmarksSnackbarState()
    data object SelectFolderFailed : BookmarksSnackbarState()
}

internal data class BookmarksEditBookmarkState(
    val bookmark: BookmarkItem.Bookmark,
    val folder: BookmarkItem.Folder,
    val edited: Boolean = false,
)

internal data class BookmarksAddFolderState(
    val parent: BookmarkItem.Folder,
    val folderBeingAddedTitle: String,
)

internal data class BookmarksEditFolderState(
    val parent: BookmarkItem.Folder,
    val folder: BookmarkItem.Folder,
)

internal sealed class SelectFolderExpansionState {
    data object None : SelectFolderExpansionState()
    data object Closed : SelectFolderExpansionState()
    data class Open(val children: List<SelectFolderItem>) : SelectFolderExpansionState()
}

internal data class SelectFolderItem(
    val indentation: Int,
    val folder: BookmarkItem.Folder,
    val expansionState: SelectFolderExpansionState,
) {
    val guid: String
        get() = folder.guid

    val title: String
        get() = folder.title

    val isDesktopRoot: Boolean
        get() = guid == BookmarkRoot.Root.id

    val startPadding: Dp
        get() = (16 * indentation).dp
}

internal fun List<SelectFolderItem>.flattenToList(): List<SelectFolderItem> =
    if (isEmpty()) {
        emptyList()
    } else {
        map {
            listOf(it) + (
                (it.expansionState as? SelectFolderExpansionState.Open)
                ?.children?.flattenToList() ?: listOf()
            )
        }.flatten()
    }

/**
 * State representing the select folder subscreen.
 *
 * @property outerSelectionGuid The currently selected folder guid for the initial select folder screen.
 * Required since there is always at least this property active while the screen is visible.
 * @property innerSelectionGuid If in the select folder -> add folder -> select folder flow,
 * this represents the selection GUID for the nest select screen where the newly added folder is being
 * placed. Optional since this screen may never be displayed.
 * @property folders The folders to display.
 * @property filteredFolders The currently filtered collection of [folders]
 * @property searchQuery The term used to filter the folders displayed.
 * @property isLoading State representing if the initial load or the search has completed.
 * @property isSearching State representing if currently in search mode.
 */
internal data class BookmarksSelectFolderState(
    val outerSelectionGuid: String,
    val innerSelectionGuid: String? = null,
    val folders: List<SelectFolderItem> = listOf(),
    val filteredFolders: List<SelectFolderItem> = listOf(),
    val searchQuery: String = "",
    val isLoading: Boolean = true,
    val isSearching: Boolean = false,
    ) {
    val visibleFolders: List<SelectFolderItem>
        get() = if (isSearching) {
            filteredFolders.map { it.copy(indentation = 0) }
        } else {
            folders
        }
    val selectedGuid: String
        get() = innerSelectionGuid ?: outerSelectionGuid
}

internal val BookmarkItem.Folder.isDesktopFolder: Boolean
    get() = when (guid) {
        BookmarkRoot.Root.id,
        BookmarkRoot.Menu.id,
        BookmarkRoot.Toolbar.id,
        BookmarkRoot.Unfiled.id,
        -> true
        else -> false
    }

internal val BookmarkItem.Folder.isMobileRoot: Boolean
    get() = guid == BookmarkRoot.Mobile.id

internal val BookmarkItem.Folder.isDesktopRoot: Boolean
    get() = guid == BookmarkRoot.Root.id
