/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.unit.dp
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.bookmarks.AlertDialogDeletionWarning
import org.mozilla.fenix.bookmarks.BackClicked
import org.mozilla.fenix.bookmarks.BookmarkItem
import org.mozilla.fenix.bookmarks.BookmarksEditBookmarkState
import org.mozilla.fenix.bookmarks.BookmarksEditFolderState
import org.mozilla.fenix.bookmarks.BookmarksListSortOrder
import org.mozilla.fenix.bookmarks.BookmarksSnackbarState
import org.mozilla.fenix.bookmarks.BookmarksState
import org.mozilla.fenix.bookmarks.BookmarksStore
import org.mozilla.fenix.bookmarks.BookmarksTestTag.EDIT_BOOKMARK_FOLDER_TITLE_TEXT_FIELD
import org.mozilla.fenix.bookmarks.DeletionDialogAction
import org.mozilla.fenix.bookmarks.DeletionDialogState
import org.mozilla.fenix.bookmarks.EditFolderAction
import org.mozilla.fenix.bookmarks.OpenTabsConfirmationDialog
import org.mozilla.fenix.compose.list.IconListItem
import org.mozilla.fenix.theme.FirefoxTheme

/** Top-level composable for the "Edit folder" screen */
@Composable
internal fun EditFolderScreen(
    modifier: Modifier = Modifier,
    store: BookmarksStore,
) {
    val state by store.stateFlow.collectAsState()
    val editState = state.bookmarksEditFolderState ?: return
    val dialogState = state.bookmarksDeletionDialogState

    if (dialogState is DeletionDialogState.Presenting) {
        AlertDialogDeletionWarning(
            onCancelTapped = { store.dispatch(DeletionDialogAction.CancelTapped) },
            onDeleteTapped = { store.dispatch(DeletionDialogAction.DeleteTapped) },
        )
    }

    Scaffold(
        modifier = modifier,
        topBar = {
            EditFolderTopBar(
                onBackClick = { store.dispatch(BackClicked) },
                onDeleteClick = { store.dispatch(EditFolderAction.DeleteClicked) },
            )
        },
    ) { paddingValues ->
        Box(
            modifier = Modifier.padding(paddingValues).fillMaxWidth(),
            contentAlignment = Alignment.TopCenter,
        ) {
            Column(modifier = Modifier.width(FirefoxTheme.layout.size.containerMaxWidth)) {
                TextField(
                    value = editState.folder.title,
                    onValueChange = { newText ->
                        store.dispatch(EditFolderAction.TitleChanged(newText))
                    },
                    placeholder = "",
                    errorText = "",
                    modifier =
                        Modifier.semantics {
                                testTagsAsResourceId = true
                                testTag = EDIT_BOOKMARK_FOLDER_TITLE_TEXT_FIELD
                            }
                            .padding(
                                start = 16.dp,
                                end = 16.dp,
                                top = 32.dp,
                            ),
                    label = stringResource(R.string.bookmark_name_label_normal_case),
                )

                Spacer(modifier = Modifier.height(24.dp))

                Text(
                    stringResource(R.string.bookmark_save_in_label),
                    color = MaterialTheme.colorScheme.onSurface,
                    style = FirefoxTheme.typography.body2,
                    modifier = Modifier.padding(start = 16.dp),
                )

                IconListItem(
                    label = editState.parent.title,
                    beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                    onClick = { store.dispatch(EditFolderAction.ParentFolderClicked) },
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class) // TopAppBar
@Composable
private fun EditFolderTopBar(
    onBackClick: () -> Unit,
    onDeleteClick: () -> Unit,
) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.edit_bookmark_folder_fragment_title),
                style = FirefoxTheme.typography.headline5,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = onBackClick,
                contentDescription = stringResource(R.string.bookmark_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        actions = {
            IconButton(
                onClick = onDeleteClick,
                contentDescription = stringResource(R.string.bookmark_delete_folder_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_delete_24),
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurface,
                )
            }
        },
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
    )
}

@Composable
@FlexibleWindowLightDarkPreview
private fun EditFolderScreenPreview() {
    val store =
        BookmarksStore(
            initialState =
                BookmarksState(
                    bookmarkItems = listOf(),
                    selectedItems = listOf(),
                    rootMenuShown = false,
                    showBookmarksImport = true,
                    sortMenuShown = false,
                    sortOrder = BookmarksListSortOrder.default,
                    recursiveSelectedCount = null,
                    currentFolder =
                        BookmarkItem.Folder(
                            guid = BookmarkRoot.Mobile.id,
                            title = "Bookmarks",
                            position = null,
                        ),
                    isSignedIntoSync = true,
                    openTabsConfirmationDialog = OpenTabsConfirmationDialog.None,
                    bookmarksDeletionDialogState = DeletionDialogState.None,
                    bookmarksSnackbarState = BookmarksSnackbarState.None,
                    bookmarksAddFolderState = null,
                    bookmarksEditBookmarkState =
                        BookmarksEditBookmarkState(
                            bookmark =
                                BookmarkItem.Bookmark(
                                    url = "https://www.whoevenmakeswebaddressesthislonglikeseriously1.com",
                                    title = "this is a very long bookmark title that should overflow 1",
                                    previewImageUrl = "",
                                    guid = "1",
                                    position = null,
                                ),
                            folder = BookmarkItem.Folder("folder 1", guid = "1", position = null),
                        ),
                    bookmarksSelectFolderState = null,
                    bookmarksEditFolderState =
                        BookmarksEditFolderState(
                            parent =
                                BookmarkItem.Folder(
                                    guid = BookmarkRoot.Mobile.id,
                                    title = "Bookmarks",
                                    position = null,
                                ),
                            folder =
                                BookmarkItem.Folder(
                                    guid = BookmarkRoot.Mobile.id,
                                    title = "New folder",
                                    position = null,
                                ),
                        ),
                    bookmarksMultiselectMoveState = null,
                    isLoading = false,
                    searchState = null,
                )
        )

    FirefoxTheme {
        EditFolderScreen(store = store)
    }
}
