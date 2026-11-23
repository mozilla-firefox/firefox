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
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.map
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.bookmarks.AddFolderAction
import org.mozilla.fenix.bookmarks.BackClicked
import org.mozilla.fenix.bookmarks.BookmarkItem
import org.mozilla.fenix.bookmarks.BookmarksAddFolderState
import org.mozilla.fenix.bookmarks.BookmarksListSortOrder
import org.mozilla.fenix.bookmarks.BookmarksSnackbarState
import org.mozilla.fenix.bookmarks.BookmarksState
import org.mozilla.fenix.bookmarks.BookmarksStore
import org.mozilla.fenix.bookmarks.BookmarksTestTag
import org.mozilla.fenix.bookmarks.DeletionDialogState
import org.mozilla.fenix.bookmarks.OpenTabsConfirmationDialog
import org.mozilla.fenix.compose.list.IconListItem
import org.mozilla.fenix.theme.FirefoxTheme

/** Top-level composable for the "Add folder" screen */
@Composable
internal fun AddFolderScreen(
    modifier: Modifier = Modifier,
    store: BookmarksStore,
) {
    val state by remember {
        store.stateFlow.map { it.bookmarksAddFolderState }
    }
        .collectAsState(initial = store.state.bookmarksAddFolderState)

    Scaffold(
        modifier = modifier,
        topBar = { AddFolderTopBar(onBackClick = { store.dispatch(BackClicked) }) },
    ) { paddingValues ->
        Box(
            modifier = Modifier.padding(paddingValues).fillMaxWidth(),
            contentAlignment = Alignment.TopCenter,
        ) {
            Column(modifier = Modifier.width(FirefoxTheme.layout.size.containerMaxWidth)) {
                TextField(
                    value = state?.folderBeingAddedTitle ?: "",
                    onValueChange = { newText -> store.dispatch(AddFolderAction.TitleChanged(newText)) },
                    placeholder = "",
                    errorText = "",
                    modifier =
                        Modifier.padding(
                                start = 16.dp,
                                end = 16.dp,
                                top = 32.dp,
                            )
                            .semantics {
                                testTagsAsResourceId = true
                                testTag = BookmarksTestTag.ADD_BOOKMARK_FOLDER_NAME_TEXT_FIELD
                            },
                    label = stringResource(R.string.bookmark_name_label_normal_case),
                )

                Spacer(modifier = Modifier.height(24.dp))

                Text(
                    text = stringResource(R.string.bookmark_save_in_label),
                    color = MaterialTheme.colorScheme.onSurface,
                    style = FirefoxTheme.typography.body2,
                    modifier = Modifier.padding(start = 16.dp),
                )

                IconListItem(
                    label = state?.parent?.title ?: "",
                    beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                    onClick = { store.dispatch(AddFolderAction.ParentFolderClicked) },
                )
            }
        }
    }
}

@Composable
private fun AddFolderTopBar(onBackClick: () -> Unit) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.bookmark_add_folder),
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
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
    )
}

@FlexibleWindowLightDarkPreview
@Composable
private fun AddFolderPreview() {
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
                    isSignedIntoSync = false,
                    openTabsConfirmationDialog = OpenTabsConfirmationDialog.None,
                    bookmarksDeletionDialogState = DeletionDialogState.None,
                    bookmarksSnackbarState = BookmarksSnackbarState.None,
                    bookmarksEditBookmarkState = null,
                    bookmarksAddFolderState =
                        BookmarksAddFolderState(
                            parent =
                                BookmarkItem.Folder(
                                    guid = BookmarkRoot.Mobile.id,
                                    title = "Bookmarks",
                                    position = null,
                                ),
                            folderBeingAddedTitle = "Edit me!",
                        ),
                    bookmarksSelectFolderState = null,
                    bookmarksEditFolderState = null,
                    bookmarksMultiselectMoveState = null,
                    isLoading = false,
                    searchState = null,
                )
        )
    FirefoxTheme {
        AddFolderScreen(modifier = Modifier, store = store)
    }
}
