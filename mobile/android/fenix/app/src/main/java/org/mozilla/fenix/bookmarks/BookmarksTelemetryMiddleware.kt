/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.BookmarksManagement
import org.mozilla.fenix.components.metrics.MetricsUtils

private val EDIT_SCREEN_METRIC_SOURCE = MetricsUtils.BookmarkAction.Source.BOOKMARK_EDIT_PAGE
private val LIST_SCREEN_METRIC_SOURCE = MetricsUtils.BookmarkAction.Source.BOOKMARK_PANEL

internal class BookmarksTelemetryMiddleware : Middleware<BookmarksState, BookmarksAction> {

    @Suppress("CyclomaticComplexMethod")
    override fun invoke(
        store: Store<BookmarksState, BookmarksAction>,
        next: (BookmarksAction) -> Unit,
        action: BookmarksAction,
    ) {
        val preReductionState = store.state
        next(action)
        handleAction(action, preReductionState)
    }

    private fun handleAction(
        action: BookmarksAction,
        state: BookmarksState,
    ) {
        when (action) {
            is DeletionDialogAction -> state.handleDeleteDialogAction(action)
            is BookmarkClicked -> { recordBookmarkClickedMetrics(state) }
            is BookmarksListMenuAction -> { handleBookmarksListMenuAction(action, state) }
            is SelectFolderAction -> { handleSelectFolderActions(action) }
            SearchClicked -> { BookmarksManagement.searchIconTapped.record(NoExtras()) }
            BackClicked -> state.handleBackClick()
            is ImportAction -> handleImportAction(action)
            EditBookmarkAction.DeleteClicked -> { recordEditDeleteMetrics() }
            RootOverflowMenuClicked,
            RootOverflowMenuDismissed,
            EditBookmarkAction.FolderClicked,
            is EditBookmarkAction.TitleChanged,
            is EditBookmarkAction.URLChanged,
            is AddFolderAction.FolderCreated,
            is AddFolderAction.TitleChanged,
            AddFolderAction.ParentFolderClicked,
            is EditFolderAction.TitleChanged,
            EditFolderAction.DeleteClicked,
            EditFolderAction.ParentFolderClicked,
            is SnackbarAction,
            is BookmarkLongClicked,
            is BookmarksLoaded, is SearchDismissed, is EditBookmarkClicked, is FolderClicked,
            is FolderLongClicked,
            is RecursiveSelectionCountLoaded,
            is OpenTabsConfirmationDialogAction.Present,
            is ViewAppeared,
            is BookmarkToEditLoaded,
            is ReceivedSyncSignInUpdate,
            CloseClicked, AddFolderClicked, SignIntoSyncClicked,
            OpenTabsConfirmationDialogAction.CancelTapped, OpenTabsConfirmationDialogAction.ConfirmTapped,
            FirstSyncCompleted, PrivateBrowsingAuthorized,
                -> Unit
        }
    }

    private fun handleBookmarksListMenuAction(
        action: BookmarksListMenuAction,
        state: BookmarksState,
    ) {
        when (action) {
            is BookmarksListMenuAction.Folder -> handleBookmarksListMenuFolderAction(action)
            is BookmarksListMenuAction.Bookmark -> handleBookmarksListMenuBookmarkAction(action)
            is BookmarksListMenuAction.MultiSelect -> state.handleBookmarksListMenuMultiSelectAction(action)
            is BookmarksListMenuAction.SortMenu -> action.record()
            BookmarksListMenuAction.SelectAll -> Unit
        }
    }

    private fun handleSelectFolderActions(
        action: SelectFolderAction,
    ) {
        when (action) {
            is SelectFolderAction.SearchQueryUpdated,
            is SelectFolderAction.SortMenu,
            is SelectFolderAction.FoldersLoaded,
            is SelectFolderAction.FilteredFoldersLoaded,
            is SelectFolderAction.ExpandedFolderLoaded,
            is SelectFolderAction.ChevronClicked,
            is SelectFolderAction.ItemClicked,
            SelectFolderAction.SearchClicked,
            SelectFolderAction.SearchDismissed,
            SelectFolderAction.ViewAppeared,
                -> Unit
        }
    }

    private fun BookmarksState.handleBookmarksListMenuMultiSelectAction(action: BookmarksListMenuAction.MultiSelect) {
        when (action) {
            BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked -> {
                BookmarksManagement.openInNewTabs.record(NoExtras())
                MetricsUtils.recordBookmarkMetrics(
                    MetricsUtils.BookmarkAction.OPEN,
                    LIST_SCREEN_METRIC_SOURCE,
                )
            }

            BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked -> {
                BookmarksManagement.openInPrivateTabs.record(NoExtras())
                MetricsUtils.recordBookmarkMetrics(
                    MetricsUtils.BookmarkAction.OPEN,
                    LIST_SCREEN_METRIC_SOURCE,
                )
            }

            BookmarksListMenuAction.MultiSelect.ShareClicked -> {
                selectedItems.filterIsInstance<BookmarkItem.Bookmark>()
                    .forEach { _ -> BookmarksManagement.shared.record(NoExtras()) }
            }

            BookmarksListMenuAction.MultiSelect.DeleteClicked,
            BookmarksListMenuAction.MultiSelect.EditClicked,
            BookmarksListMenuAction.MultiSelect.MoveClicked,
                -> Unit
        }
    }

    private fun handleBookmarksListMenuBookmarkAction(action: BookmarksListMenuAction.Bookmark) {
        when (action) {
            is BookmarksListMenuAction.Bookmark.OpenInNormalTabClicked -> {
                BookmarksManagement.openInNewTab.record(NoExtras())
                MetricsUtils.recordBookmarkMetrics(
                    MetricsUtils.BookmarkAction.OPEN,
                    LIST_SCREEN_METRIC_SOURCE,
                )
            }

            is BookmarksListMenuAction.Bookmark.OpenInPrivateTabClicked -> {
                BookmarksManagement.openInPrivateTab.record(NoExtras())
                MetricsUtils.recordBookmarkMetrics(
                    MetricsUtils.BookmarkAction.OPEN,
                    LIST_SCREEN_METRIC_SOURCE,
                )
            }

            is BookmarksListMenuAction.Bookmark.ShareClicked -> {
                BookmarksManagement.shared.record(NoExtras())
            }

            is BookmarksListMenuAction.Bookmark.MoveClicked -> {
                BookmarksManagement.moved.record(NoExtras())
            }

            is BookmarksListMenuAction.Bookmark.DeleteClicked -> { recordListDeleteMetrics() }
            is BookmarksListMenuAction.Bookmark.EditClicked,
            is BookmarksListMenuAction.Bookmark.SelectClicked,
                -> Unit
        }
    }

    private fun handleBookmarksListMenuFolderAction(action: BookmarksListMenuAction.Folder) {
        when (action) {
            is BookmarksListMenuAction.Folder.OpenAllInNormalTabClicked -> {
                BookmarksManagement.openAllInNewTabs.record(NoExtras())
                MetricsUtils.recordBookmarkMetrics(
                    MetricsUtils.BookmarkAction.OPEN,
                    LIST_SCREEN_METRIC_SOURCE,
                )
            }

            is BookmarksListMenuAction.Folder.OpenAllInPrivateTabClicked -> {
                BookmarksManagement.openInPrivateTabs.record(NoExtras())
                MetricsUtils.recordBookmarkMetrics(
                    MetricsUtils.BookmarkAction.OPEN,
                    LIST_SCREEN_METRIC_SOURCE,
                )
            }

            is BookmarksListMenuAction.Folder.MoveClicked -> {
                BookmarksManagement.moved.record(NoExtras())
            }

            is BookmarksListMenuAction.Folder.EditClicked,
            is BookmarksListMenuAction.Folder.DeleteClicked,
            is BookmarksListMenuAction.Folder.SelectClicked,
                -> Unit
        }
    }

    private fun BookmarksState.handleBackClick() {
        when {
            bookmarksEditBookmarkState != null -> {
                BookmarksManagement.edited.record(NoExtras())
                MetricsUtils.recordBookmarkMetrics(
                    MetricsUtils.BookmarkAction.EDIT,
                    EDIT_SCREEN_METRIC_SOURCE,
                )
                if (bookmarksEditBookmarkState.folder != currentFolder) {
                    BookmarksManagement.moved.record(NoExtras())
                }
            }

            bookmarksAddFolderState != null -> {
                if (bookmarksAddFolderState.folderBeingAddedTitle != "") {
                    BookmarksManagement.folderAdd.record(NoExtras())
                }
            }

            bookmarksSelectFolderState != null -> {
                if (bookmarksMultiselectMoveState != null &&
                    bookmarksMultiselectMoveState.destination != currentFolder.guid
                ) {
                    BookmarksManagement.moved.record(NoExtras())
                }
            }
        }
    }

    private fun BookmarksListMenuAction.SortMenu.record() = when (this) {
        BookmarksListMenuAction.SortMenu.SortMenuButtonClicked -> BookmarksManagement.sortMenuClicked.record()
        BookmarksListMenuAction.SortMenu.SortMenuDismissed -> Unit
        BookmarksListMenuAction.SortMenu.CustomSortClicked -> BookmarksManagement.sortByCustom.record()
        BookmarksListMenuAction.SortMenu.NewestClicked -> BookmarksManagement.sortByNewest.record()
        BookmarksListMenuAction.SortMenu.OldestClicked -> BookmarksManagement.sortByOldest.record()
        BookmarksListMenuAction.SortMenu.AtoZClicked -> BookmarksManagement.sortByAToZ.record()
        BookmarksListMenuAction.SortMenu.ZtoAClicked -> BookmarksManagement.sortByZToA.record()
    }

    private fun BookmarksState.handleDeleteDialogAction(action: DeletionDialogAction) {
        when (action) {
            DeletionDialogAction.DeleteTapped -> {
                val deletedItems = bookmarkItems.filter {
                    it.guid in bookmarksDeletionDialogState.guidsToDelete
                }
                if (deletedItems.any { it is BookmarkItem.Folder }) {
                    BookmarksManagement.folderRemove.record(NoExtras())
                }

                if (deletedItems.size > 1) {
                    BookmarksManagement.multiRemoved.record(NoExtras())
                }
            }

            is DeletionDialogAction.CountLoaded,
            DeletionDialogAction.CancelTapped,
                -> Unit
        }
    }

    private fun recordEditDeleteMetrics() {
        BookmarksManagement.removed.record(NoExtras())
        MetricsUtils.recordBookmarkMetrics(
            MetricsUtils.BookmarkAction.DELETE,
            EDIT_SCREEN_METRIC_SOURCE,
        )
    }

    private fun recordListDeleteMetrics() {
        BookmarksManagement.removed.record(NoExtras())
        MetricsUtils.recordBookmarkMetrics(
            MetricsUtils.BookmarkAction.DELETE,
            LIST_SCREEN_METRIC_SOURCE,
        )
    }

    private fun recordBookmarkClickedMetrics(state: BookmarksState) {
        if (state.selectedItems.isEmpty()) {
            BookmarksManagement.open.record(NoExtras())
            MetricsUtils.recordBookmarkMetrics(
                MetricsUtils.BookmarkAction.OPEN,
                LIST_SCREEN_METRIC_SOURCE,
            )
        }
    }

    private fun handleImportAction(action: ImportAction) = when (action) {
        is ImportAction.ImportFailed -> BookmarksManagement.importFailed.record(NoExtras())
        is ImportAction.ImportFileClicked.FromMenu ->
            BookmarksManagement.importFromFileMenuClick.record(NoExtras())
        is ImportAction.ImportSucceeded -> {
            BookmarksManagement.importSuccessful.record(
                extra = BookmarksManagement.ImportSuccessfulExtra(bookmarksCount = action.count),
            )
        }
    }
}
