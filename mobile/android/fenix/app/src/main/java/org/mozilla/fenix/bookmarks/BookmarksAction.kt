/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import mozilla.components.lib.state.Action

/**
 * Actions relating to the Bookmarks list screen and its various subscreens.
 */
internal sealed interface BookmarksAction : Action

internal data class BookmarkToEditLoaded(
    val bookmark: BookmarkItem.Bookmark,
    val folder: BookmarkItem.Folder,
) : BookmarksAction

/**
 * Dispatched when the bookmark view appears.
 *
 * @property bookmarkToLoad The guid of the bookmark to load in the edit state.
 */
internal data class ViewAppeared(val bookmarkToLoad: String? = null) : BookmarksAction

/**
 * Bookmarks have been loaded from the storage layer.
 *
 * @property folder The loaded [BookmarkItem.Folder]
 * @property bookmarkItems The bookmark items loaded, transformed into a displayable type.
 */
internal data class BookmarksLoaded(
    val folder: BookmarkItem.Folder,
    val bookmarkItems: List<BookmarkItem>,
) : BookmarksAction

internal data class RecursiveSelectionCountLoaded(
    val count: Int,
) : BookmarksAction

internal sealed class BookmarksListMenuAction : BookmarksAction {
    internal data object SelectAll : BookmarksListMenuAction()

    internal sealed class Bookmark : BookmarksListMenuAction() {
        data class SelectClicked(val bookmark: BookmarkItem.Bookmark) : Bookmark()
        data class EditClicked(val bookmark: BookmarkItem.Bookmark) : Bookmark()
        data class ShareClicked(val bookmark: BookmarkItem.Bookmark) : Bookmark()
        data class OpenInNormalTabClicked(val bookmark: BookmarkItem.Bookmark) : Bookmark()
        data class OpenInPrivateTabClicked(val bookmark: BookmarkItem.Bookmark) : Bookmark()
        data class DeleteClicked(val bookmark: BookmarkItem.Bookmark) : Bookmark()
        data class MoveClicked(val bookmark: BookmarkItem.Bookmark) : Bookmark()
    }
    internal sealed class Folder : BookmarksListMenuAction() {
        data class SelectClicked(val folder: BookmarkItem.Folder) : Folder()
        data class EditClicked(val folder: BookmarkItem.Folder) : Folder()
        data class OpenAllInNormalTabClicked(val folder: BookmarkItem.Folder) : Folder()
        data class OpenAllInPrivateTabClicked(val folder: BookmarkItem.Folder) : Folder()
        data class DeleteClicked(val folder: BookmarkItem.Folder) : Folder()
        data class MoveClicked(val folder: BookmarkItem.Folder) : Folder()
    }
    internal sealed class MultiSelect : BookmarksListMenuAction() {
        data object EditClicked : MultiSelect()
        data object MoveClicked : MultiSelect()
        data object DeleteClicked : MultiSelect()
        data object OpenInNormalTabsClicked : MultiSelect()
        data object OpenInPrivateTabsClicked : MultiSelect()
        data object ShareClicked : MultiSelect()
    }

    internal sealed class SortMenu : BookmarksListMenuAction() {
        data object SortMenuButtonClicked : SortMenu()
        data object SortMenuDismissed : SortMenu()
        data object CustomSortClicked : SortMenu()
        data object NewestClicked : SortMenu()
        data object OldestClicked : SortMenu()
        data object AtoZClicked : SortMenu()
        data object ZtoAClicked : SortMenu()
    }
}

internal data class FolderClicked(val item: BookmarkItem.Folder) : BookmarksAction
internal data class FolderLongClicked(val item: BookmarkItem.Folder) : BookmarksAction
internal data class BookmarkClicked(val item: BookmarkItem.Bookmark) : BookmarksAction
internal data class BookmarkLongClicked(val item: BookmarkItem.Bookmark) : BookmarksAction
internal data object SearchClicked : BookmarksAction
internal data object SearchDismissed : BookmarksAction
internal data object AddFolderClicked : BookmarksAction
internal data object CloseClicked : BookmarksAction
internal data object BackClicked : BookmarksAction
internal data object SignIntoSyncClicked : BookmarksAction
internal data class EditBookmarkClicked(val bookmark: BookmarkItem.Bookmark) : BookmarksAction
internal data class ReceivedSyncSignInUpdate(val isSignedIn: Boolean) : BookmarksAction
internal data object FirstSyncCompleted : BookmarksAction
internal data object PrivateBrowsingAuthorized : BookmarksAction

/**
 * Actions specific to the Add Folder screen.
 */
internal sealed class AddFolderAction : BookmarksAction {
    data class TitleChanged(val updatedText: String) : AddFolderAction()
    data object ParentFolderClicked : AddFolderAction()
    data class FolderCreated(val folder: BookmarkItem.Folder) : AddFolderAction()
}

/**
 * Actions specific to the Edit Folder screen.
 */
internal sealed class EditFolderAction : BookmarksAction {
    data class TitleChanged(val updatedText: String) : EditFolderAction()
    data object ParentFolderClicked : EditFolderAction()
    data object DeleteClicked : EditFolderAction()
}

internal sealed class EditBookmarkAction : BookmarksAction {
    data class TitleChanged(val title: String) : EditBookmarkAction()
    data class URLChanged(val url: String) : EditBookmarkAction()
    data object FolderClicked : EditBookmarkAction()
    data object DeleteClicked : EditBookmarkAction()
}

internal sealed class SelectFolderAction : BookmarksAction {
    data object ViewAppeared : SelectFolderAction()
    data class FoldersLoaded(val folders: List<SelectFolderItem>) : SelectFolderAction()
    data class FilteredFoldersLoaded(val folders: List<SelectFolderItem>) : SelectFolderAction()
    data class ExpandedFolderLoaded(val folder: SelectFolderItem) : SelectFolderAction()
    data class ChevronClicked(val folder: SelectFolderItem) : SelectFolderAction()
    data class ItemClicked(val folder: SelectFolderItem) : SelectFolderAction()
    data object SearchClicked : SelectFolderAction()
    data object SearchDismissed : SelectFolderAction()
    data class SearchQueryUpdated(val query: String) : SelectFolderAction()

    internal sealed class SortMenu : SelectFolderAction() {
        data object SortMenuButtonClicked : SortMenu()
        data object SortMenuDismissed : SortMenu()
        data object CustomSortClicked : SortMenu()
        data object NewestClicked : SortMenu()
        data object OldestClicked : SortMenu()
        data object AtoZClicked : SortMenu()
        data object ZtoAClicked : SortMenu()
    }
}

internal sealed class OpenTabsConfirmationDialogAction : BookmarksAction {
    data class Present(
        val guid: String,
        val count: Int,
        val isPrivate: Boolean,
    ) : OpenTabsConfirmationDialogAction()
    data object ConfirmTapped : OpenTabsConfirmationDialogAction()
    data object CancelTapped : OpenTabsConfirmationDialogAction()
}

internal sealed class DeletionDialogAction : BookmarksAction {
    data class CountLoaded(val count: Int) : DeletionDialogAction()
    data object CancelTapped : DeletionDialogAction()
    data object DeleteTapped : DeletionDialogAction()
}

internal sealed class SnackbarAction : BookmarksAction {
    data object Dismissed : SnackbarAction()
    data object SelectFolderFailed : SnackbarAction()
    data object ImportFailed : SnackbarAction()
}

internal data object RootOverflowMenuClicked : BookmarksAction
internal data object RootOverflowMenuDismissed : BookmarksAction
internal sealed class ImportAction : BookmarksAction {
    internal data object ImportFailed : ImportAction()
    internal data class ImportSucceeded(val count: Int) : ImportAction()
    internal sealed class ImportFileClicked : ImportAction() {
        internal data object FromMenu : ImportFileClicked()
    }
}
