/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks.ui

import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.map
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.utils.BackInvokedHandler
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.Config
import org.mozilla.fenix.R
import org.mozilla.fenix.bookmarks.AddFolderClicked
import org.mozilla.fenix.bookmarks.BackClicked
import org.mozilla.fenix.bookmarks.BookmarkItem
import org.mozilla.fenix.bookmarks.BookmarksAddFolderState
import org.mozilla.fenix.bookmarks.BookmarksListMenuAction
import org.mozilla.fenix.bookmarks.BookmarksListSortOrder
import org.mozilla.fenix.bookmarks.BookmarksSelectFolderState
import org.mozilla.fenix.bookmarks.BookmarksSnackbarState
import org.mozilla.fenix.bookmarks.BookmarksState
import org.mozilla.fenix.bookmarks.BookmarksStore
import org.mozilla.fenix.bookmarks.DeletionDialogState
import org.mozilla.fenix.bookmarks.OpenTabsConfirmationDialog
import org.mozilla.fenix.bookmarks.SelectFolderAction
import org.mozilla.fenix.bookmarks.SelectFolderExpansionState
import org.mozilla.fenix.bookmarks.SelectFolderItem
import org.mozilla.fenix.bookmarks.flattenToList
import org.mozilla.fenix.compose.list.IconListItem
import org.mozilla.fenix.compose.list.SelectableIconListItem
import org.mozilla.fenix.theme.FirefoxTheme

/** Top-level composable for the "Select folder" screen */
@Composable
internal fun SelectFolderScreen(
    modifier: Modifier = Modifier,
    store: BookmarksStore,
) {
    val showNewFolderButton by remember {
        store.stateFlow.map { store.state.showNewFolderButton }
    }
        .collectAsState(initial = store.state.showNewFolderButton)
    val state by remember {
        store.stateFlow.map { it.bookmarksSelectFolderState }
    }
        .collectAsState(initial = store.state.bookmarksSelectFolderState)

    LaunchedEffect(Unit) {
        store.dispatch(SelectFolderAction.ViewAppeared)
    }

    BackInvokedHandler(state?.isSearching ?: false) {
        store.dispatch(SelectFolderAction.SearchDismissed)
    }

    val focusManager = LocalFocusManager.current
    val keyboardController = LocalSoftwareKeyboardController.current

    Scaffold(
        modifier =
            modifier.pointerInput(Unit) {
                detectTapGestures(
                    onTap = {
                        focusManager.clearFocus()
                        keyboardController?.hide()
                        store.dispatch(SelectFolderAction.SearchDismissed)
                    }
                )
            },
        topBar = {
            if (state?.isSearching ?: false) {
                SelectFolderSearchTopBar(store = store)
            } else {
                SelectFolderTopBar(store = store)
            }
        },
    ) { paddingValues ->
        if (state?.isLoading ?: false) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CircularProgressIndicator()
            }
            return@Scaffold
        }
        LazyColumn(
            modifier = Modifier.padding(paddingValues).padding(vertical = 16.dp).fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            items(
                key = { item -> item.folder.guid },
                items = state?.visibleFolders.orEmpty().flattenToList(),
            ) { folder ->
                FolderListItem(
                    folder = folder,
                    isSelected = folder.guid == state?.selectedGuid,
                    showPadding = state?.isSearching ?: true,
                    onClick = { store.dispatch(SelectFolderAction.ItemClicked(folder)) },
                    onChevronClick = { store.dispatch(SelectFolderAction.ChevronClicked(folder)) },
                )
            }

            if (showNewFolderButton) {
                item {
                    NewFolderListItem { store.dispatch(AddFolderClicked) }
                }
            }
        }
    }
}

@Composable
private fun SelectFolderSearchTopBar(store: BookmarksStore) {
    val focusRequester = remember { FocusRequester() }
    var text by remember {
        mutableStateOf(
            TextFieldValue(
                store.state.bookmarksSelectFolderState?.searchQuery.orEmpty(),
                selection = TextRange(store.state.bookmarksSelectFolderState?.searchQuery?.length ?: 0),
            )
        )
    }

    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
        val value = text.text
        text = text.copy(selection = TextRange(value.length))
    }

    TopAppBar(
        title = {
            OutlinedTextField(
                value = text,
                onValueChange = { newValue ->
                    text = newValue
                    store.dispatch(SelectFolderAction.SearchQueryUpdated(newValue.text))
                },
                modifier = Modifier.fillMaxWidth().focusRequester(focusRequester),
                placeholder = {
                    stringResource(R.string.select_bookmark_search_button_content_description)
                },
                leadingIcon = {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_search_24),
                        contentDescription = stringResource(R.string.select_bookmark_search_button_content_description),
                    )
                },
                singleLine = true,
                shape = MaterialTheme.shapes.medium,
            )
        },
        navigationIcon = {},
        actions = {},
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
    )
}

@Composable
private fun FolderListItem(
    folder: SelectFolderItem,
    isSelected: Boolean,
    showPadding: Boolean = true,
    onClick: () -> Unit,
    onChevronClick: () -> Unit,
) {
    if (folder.isDesktopRoot) {
        Box(modifier = Modifier.padding(start = folder.startPadding)) {
            Row(modifier = Modifier.width(FirefoxTheme.layout.size.containerMaxWidth)) {
                Spacer(modifier = Modifier.width(56.dp))
                Text(
                    text = folder.title,
                    color = MaterialTheme.colorScheme.tertiary,
                    style = FirefoxTheme.typography.headline8,
                )
            }
        }
    } else {
        Box(
            modifier =
                Modifier.padding(start = if (!showPadding) folder.startPadding else 0.dp)
                    .width(FirefoxTheme.layout.size.containerMaxWidth)
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                when (folder.expansionState) {
                    is SelectFolderExpansionState.None -> Spacer(modifier = Modifier.size(width = 48.dp, height = 0.dp))
                    is SelectFolderExpansionState.Open -> {
                        IconButton(
                            onClick = onChevronClick,
                            contentDescription =
                                stringResource(
                                    R.string.bookmark_select_folder_close_folder_content_description,
                                    folder.title,
                                ),
                        ) {
                            Icon(
                                painter = painterResource(iconsR.drawable.mozac_ic_chevron_down_24),
                                contentDescription = null,
                            )
                        }
                    }
                    is SelectFolderExpansionState.Closed -> {
                        IconButton(
                            onClick = onChevronClick,
                            contentDescription =
                                stringResource(
                                    R.string.bookmark_select_folder_expand_folder_content_description,
                                    folder.title,
                                ),
                        ) {
                            Icon(
                                painter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                                contentDescription = null,
                            )
                        }
                    }
                }
                SelectableIconListItem(
                    label = folder.title,
                    isSelected = isSelected,
                    beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                    modifier =
                        Modifier.toggleable(
                            value = isSelected,
                            role = Role.RadioButton,
                            onValueChange = { onClick() },
                        ),
                )
            }
        }
    }
}

@Composable
private fun NewFolderListItem(onClick: () -> Unit) {
    IconListItem(
        label = stringResource(R.string.bookmark_select_folder_new_folder_button_title),
        modifier = Modifier.width(FirefoxTheme.layout.size.containerMaxWidth),
        colors = ListItemDefaults.colors(headlineColor = MaterialTheme.colorScheme.tertiary),
        beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_add_24),
        beforeIconTint = MaterialTheme.colorScheme.tertiary,
        onClick = onClick,
    )
}

@Composable
private fun SelectFolderTopBar(store: BookmarksStore) {
    val onNewFolderClick =
        store.state.showNewFolderButton
            .takeIf { it }
            ?.let {
                { store.dispatch(AddFolderClicked) }
            }
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.bookmark_select_folder_fragment_label),
                style = FirefoxTheme.typography.headline5,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = { store.dispatch(BackClicked) },
                contentDescription = stringResource(R.string.bookmark_navigate_back_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                    contentDescription = null,
                )
            }
        },
        actions = {
            SelectFolderTopBarActions(
                store = store,
                onNewFolderClick = onNewFolderClick,
            )
        },
        windowInsets =
            WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
    )
}

@Composable
private fun SelectFolderTopBarActions(
    store: BookmarksStore,
    onNewFolderClick: (() -> Unit)?,
) {
    Box {
        IconButton(
            onClick = {
                store.dispatch(BookmarksListMenuAction.SortMenu.SortMenuButtonClicked)
            },
            contentDescription = stringResource(R.string.bookmark_sort_menu_content_desc),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_sort_24),
                contentDescription = null,
            )
        }

        SelectFolderSortOverflowMenu(store = store)
    }

    // TODO https://bugzilla.mozilla.org/show_bug.cgi?id=2006505
    if (Config.channel.isDebug) {
        IconButton(
            onClick = {
                store.dispatch(SelectFolderAction.SearchClicked)
            },
            contentDescription = stringResource(R.string.select_bookmark_search_button_content_description),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_search_24),
                contentDescription = null,
            )
        }
    }

    if (onNewFolderClick != null) {
        IconButton(
            onClick = { onNewFolderClick() },
            contentDescription = stringResource(R.string.bookmark_add_new_folder_button_content_description),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_folder_add_24),
                contentDescription = null,
            )
        }
    }
}

@Composable
private fun SelectFolderSortOverflowMenu(store: BookmarksStore) {
    val showMenu by remember {
        store.stateFlow.map { store.state.sortMenuShown }
    }
        .collectAsState(initial = store.state.sortMenuShown)
    val sortOrder by remember {
        store.stateFlow.map { store.state.sortOrder }
    }
        .collectAsState(initial = store.state.sortOrder)

    val menuItems =
        listOf(
            MenuItem.CheckableItem(
                text = Text.Resource(R.string.bookmark_sort_menu_custom),
                isChecked = sortOrder is BookmarksListSortOrder.Positional,
                onClick = { store.dispatch(SelectFolderAction.SortMenu.CustomSortClicked) },
            ),
            MenuItem.CheckableItem(
                text = Text.Resource(R.string.bookmark_sort_menu_newest),
                isChecked = sortOrder == BookmarksListSortOrder.Created(ascending = true),
                onClick = { store.dispatch(SelectFolderAction.SortMenu.NewestClicked) },
            ),
            MenuItem.CheckableItem(
                text = Text.Resource(R.string.bookmark_sort_menu_oldest),
                isChecked = sortOrder == BookmarksListSortOrder.Created(ascending = false),
                onClick = { store.dispatch(SelectFolderAction.SortMenu.OldestClicked) },
            ),
            MenuItem.CheckableItem(
                text = Text.Resource(R.string.bookmark_sort_menu_a_to_z),
                isChecked = sortOrder == BookmarksListSortOrder.Alphabetical(ascending = true),
                onClick = { store.dispatch(SelectFolderAction.SortMenu.AtoZClicked) },
            ),
            MenuItem.CheckableItem(
                text = Text.Resource(R.string.bookmark_sort_menu_z_to_a),
                isChecked = sortOrder == BookmarksListSortOrder.Alphabetical(ascending = false),
                onClick = { store.dispatch(SelectFolderAction.SortMenu.ZtoAClicked) },
            ),
        )
    DropdownMenu(
        menuItems = menuItems,
        expanded = showMenu,
        onDismissRequest = { store.dispatch(SelectFolderAction.SortMenu.SortMenuDismissed) },
    )
}

private const val PREVIEW_INDENTATION_0 = 0
private const val PREVIEW_INDENTATION_1 = 1
private const val PREVIEW_INDENTATION_2 = 2
private const val PREVIEW_INDENTATION_3 = 3

@FlexibleWindowLightDarkPreview
@Suppress("detekt.LongMethod")
@Composable
private fun SelectFolderPreview() {
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
                    openTabsConfirmationDialog = OpenTabsConfirmationDialog.None,
                    bookmarksDeletionDialogState = DeletionDialogState.None,
                    bookmarksSnackbarState = BookmarksSnackbarState.None,
                    bookmarksEditFolderState = null,
                    bookmarksSelectFolderState =
                        BookmarksSelectFolderState(
                            outerSelectionGuid = "",
                            innerSelectionGuid = "guid1",
                            folders =
                                listOf(
                                    SelectFolderItem(
                                        indentation = PREVIEW_INDENTATION_0,
                                        folder = BookmarkItem.Folder("Bookmarks", "guid0", null),
                                        expansionState = SelectFolderExpansionState.Closed,
                                    ),
                                    SelectFolderItem(
                                        indentation = PREVIEW_INDENTATION_0,
                                        folder = BookmarkItem.Folder("Bookmarks Menu", BookmarkRoot.Menu.id, null),
                                        expansionState = SelectFolderExpansionState.None,
                                    ),
                                    SelectFolderItem(
                                        indentation = PREVIEW_INDENTATION_0,
                                        folder =
                                            BookmarkItem.Folder(
                                                "Bookmarks Toolbar",
                                                BookmarkRoot.Toolbar.id,
                                                position = null,
                                            ),
                                        expansionState = SelectFolderExpansionState.None,
                                    ),
                                    SelectFolderItem(
                                        indentation = PREVIEW_INDENTATION_1,
                                        folder =
                                            BookmarkItem.Folder(
                                                "Desktop Bookmarks",
                                                BookmarkRoot.Root.id,
                                                position = null,
                                            ),
                                        expansionState = SelectFolderExpansionState.None,
                                    ),
                                    SelectFolderItem(
                                        indentation = PREVIEW_INDENTATION_0,
                                        folder =
                                            BookmarkItem.Folder(
                                                "Bookmarks Unfiled",
                                                BookmarkRoot.Unfiled.id,
                                                position = null,
                                            ),
                                        expansionState =
                                            SelectFolderExpansionState.Open(
                                                listOf(
                                                    SelectFolderItem(
                                                        indentation = PREVIEW_INDENTATION_1,
                                                        folder =
                                                            BookmarkItem.Folder("Nested One", "guid0", position = null),
                                                        expansionState =
                                                            SelectFolderExpansionState.Open(
                                                                listOf(
                                                                    SelectFolderItem(
                                                                        indentation = PREVIEW_INDENTATION_2,
                                                                        folder =
                                                                            BookmarkItem.Folder(
                                                                                "Nested Two",
                                                                                "guid0",
                                                                                position = null,
                                                                            ),
                                                                        expansionState =
                                                                            SelectFolderExpansionState.None,
                                                                    ),
                                                                    SelectFolderItem(
                                                                        indentation = PREVIEW_INDENTATION_2,
                                                                        folder =
                                                                            BookmarkItem.Folder(
                                                                                "Nested Two",
                                                                                "guid0",
                                                                                position = null,
                                                                            ),
                                                                        expansionState =
                                                                            SelectFolderExpansionState.None,
                                                                    ),
                                                                )
                                                            ),
                                                    ),
                                                    SelectFolderItem(
                                                        indentation = PREVIEW_INDENTATION_1,
                                                        folder =
                                                            BookmarkItem.Folder("Nested One", "guid0", position = null),
                                                        expansionState =
                                                            SelectFolderExpansionState.Open(
                                                                listOf(
                                                                    SelectFolderItem(
                                                                        indentation = PREVIEW_INDENTATION_2,
                                                                        folder =
                                                                            BookmarkItem.Folder(
                                                                                "Nested Two",
                                                                                "guid1",
                                                                                position = null,
                                                                            ),
                                                                        expansionState =
                                                                            SelectFolderExpansionState.Open(
                                                                                listOf(
                                                                                    SelectFolderItem(
                                                                                        indentation =
                                                                                            PREVIEW_INDENTATION_3,
                                                                                        folder =
                                                                                            BookmarkItem.Folder(
                                                                                                title = "Nested Three",
                                                                                                guid = "guid0",
                                                                                                position = null,
                                                                                            ),
                                                                                        expansionState =
                                                                                            SelectFolderExpansionState
                                                                                                .None,
                                                                                    )
                                                                                )
                                                                            ),
                                                                    )
                                                                )
                                                            ),
                                                    ),
                                                )
                                            ),
                                    ),
                                ),
                        ),
                    bookmarksMultiselectMoveState = null,
                    isLoading = false,
                    searchState = null,
                )
        )
    FirefoxTheme {
        SelectFolderScreen(modifier = Modifier, store = store)
    }
}
