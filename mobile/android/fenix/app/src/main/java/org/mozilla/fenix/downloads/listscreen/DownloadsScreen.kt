/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.ChipDefaults
import androidx.compose.material.ExperimentalMaterialApi
import androidx.compose.material.FilterChip
import androidx.compose.material.Icon
import androidx.compose.material.IconButton
import androidx.compose.material.Scaffold
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import mozilla.components.lib.state.ext.observeAsState
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.button.FloatingActionButton
import org.mozilla.fenix.compose.ext.isItemPartiallyVisible
import org.mozilla.fenix.compose.list.ExpandableListHeader
import org.mozilla.fenix.compose.list.SelectableListItem
import org.mozilla.fenix.compose.snackbar.AcornSnackbarHostState
import org.mozilla.fenix.compose.snackbar.SnackbarHost
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.downloads.listscreen.store.CreatedTime
import org.mozilla.fenix.downloads.listscreen.store.DownloadListItem
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIAction
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIState
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIState.Mode
import org.mozilla.fenix.downloads.listscreen.store.DownloadUIStore
import org.mozilla.fenix.downloads.listscreen.store.FileItem
import org.mozilla.fenix.downloads.listscreen.store.HeaderItem
import org.mozilla.fenix.downloads.listscreen.ui.DownloadSearchField
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Downloads screen that displays the list of downloads.
 *
 * @param downloadsStore The [DownloadUIStore] used to manage and access the state of download items.
 * @param onItemClick Callback invoked when a download item is clicked.
 * @param onItemDeleteClick Callback invoked when the delete action is triggered for a single download item.
 * @param onMultipleItemsDeleteClick Callback invoked when the delete action
 * is triggered for multiple selected download items.
 * @param onNavigationIconClick Callback for the back button click in the toolbar.
 */
@Composable
@Suppress("LongMethod")
fun DownloadsScreen(
    downloadsStore: DownloadUIStore,
    onItemClick: (FileItem) -> Unit,
    onItemDeleteClick: (FileItem) -> Unit,
    onMultipleItemsDeleteClick: () -> Unit,
    onNavigationIconClick: () -> Unit,
) {
    val uiState by downloadsStore.observeAsState(initialValue = downloadsStore.state) { it }
    val toolbarConfig = getToolbarConfig(mode = uiState.mode)

    BackHandler(uiState.isSearchFieldVisible) {
        downloadsStore.dispatch(DownloadUIAction.SearchBarDismissRequest)
    }

    if (uiState.isDeleteDialogVisible) {
        DeleteDownloadFileDialog(
            onConfirmDelete = {
                downloadsStore.dispatch(DownloadUIAction.UpdateDeleteDialogVisibility(false))
                onMultipleItemsDeleteClick()
            },
            onCancel = {
                downloadsStore.dispatch(DownloadUIAction.UpdateDeleteDialogVisibility(false))
            },
        )
    }

    Scaffold(
        topBar = {
            DownloadsTopAppBar(
                modifier = Modifier.fillMaxWidth(),
                backgroundColor = toolbarConfig.backgroundColor,
                title = {
                    if (uiState.isSearchFieldVisible) {
                        DownloadSearchField(
                            initialText = uiState.searchQuery,
                            onValueChange = {
                                downloadsStore.dispatch(DownloadUIAction.SearchQueryEntered(it))
                            },
                            onSearchDismissRequest = {
                                downloadsStore.dispatch(DownloadUIAction.SearchBarDismissRequest)
                            },
                        )
                    } else {
                        Text(
                            text = toolbarConfig.title,
                            color = toolbarConfig.textColor,
                            style = FirefoxTheme.typography.headline6,
                        )
                    }
                },
                navigationIcon = {
                    if (!uiState.isSearchFieldVisible) {
                        NavigationIcon(
                            iconColorTint = toolbarConfig.iconColor,
                            showBackIcon = uiState.mode !is Mode.Editing,
                            onNavigationIconClick = onNavigationIconClick,
                        )
                    }
                },
                actions = {
                    if (uiState.mode is Mode.Editing) {
                        ToolbarEditActions(
                            downloadsStore = downloadsStore,
                            toolbarConfig = toolbarConfig,
                            onItemDeleteClick = onItemDeleteClick,
                        )
                    }
                },
            )
        },
        floatingActionButton = {
            if (!uiState.isSearchFieldVisible && uiState.mode is Mode.Normal) {
                FloatingActionButton(
                    icon = painterResource(R.drawable.mozac_ic_search_24),
                    contentDescription = stringResource(R.string.download_search_placeholder),
                    onClick = { downloadsStore.dispatch(DownloadUIAction.SearchBarVisibilityRequest) },
                )
            }
        },
        backgroundColor = FirefoxTheme.colors.layer1,
    ) { paddingValues ->
        DownloadsScreenContent(
            uiState = uiState,
            paddingValues = paddingValues,
            onContentTypeSelected = {
                downloadsStore.dispatch(DownloadUIAction.ContentTypeSelected(it))
            },
            onItemClick = onItemClick,
            onSelectionChange = { item, isSelected ->
                if (isSelected) {
                    downloadsStore.dispatch(DownloadUIAction.AddItemForRemoval(item))
                } else {
                    downloadsStore.dispatch(DownloadUIAction.RemoveItemForRemoval(item))
                }
            },
            onDeleteClick = { onItemDeleteClick(it) },
            onShareUrlClick = { downloadsStore.dispatch(DownloadUIAction.ShareUrlClicked(it.url)) },
            onShareFileClick = {
                downloadsStore.dispatch(
                    DownloadUIAction.ShareFileClicked(
                        it.filePath,
                        it.contentType,
                    ),
                )
            },
        )
    }
}

@Composable
private fun ToolbarEditActions(
    downloadsStore: DownloadUIStore,
    toolbarConfig: ToolbarConfig,
    onItemDeleteClick: (FileItem) -> Unit,
) {
    // IconButton and DropdownMenu in a common parent so the menu position is calculated correctly.
    Row {
        var showMenu by remember { mutableStateOf(false) }

        IconButton(onClick = { showMenu = true }) {
            Icon(
                painter = painterResource(R.drawable.mozac_ic_ellipsis_vertical_24),
                contentDescription = stringResource(
                    R.string.content_description_menu,
                ),
                tint = toolbarConfig.iconColor,
            )
        }

        DropdownMenu(
            expanded = showMenu,
            menuItems = listOf(
                MenuItem.TextItem(
                    text = Text.Resource(R.string.download_select_all_items),
                    level = MenuItem.FixedItem.Level.Default,
                    onClick = { downloadsStore.dispatch(DownloadUIAction.AddAllItemsForRemoval) },
                ),
                MenuItem.TextItem(
                    text = Text.Resource(R.string.download_delete_item),
                    level = MenuItem.FixedItem.Level.Critical,
                    onClick = {
                        when (downloadsStore.state.mode.selectedItems.size) {
                            1 -> {
                                onItemDeleteClick(downloadsStore.state.mode.selectedItems.first())
                                downloadsStore.dispatch(DownloadUIAction.ExitEditMode)
                            }

                            else -> downloadsStore.dispatch(
                                DownloadUIAction.UpdateDeleteDialogVisibility(true),
                            )
                        }
                    },
                ),
            ),
            onDismissRequest = { showMenu = false },
        )
    }
}

/**
 * Content of the screen below the toolbar.
 * @param uiState The UI state of the screen.
 * @param paddingValues The padding values of the screen.
 * @param onContentTypeSelected Callback invoked when a content type filter is selected.
 * @param onItemClick Invoked when a download item is clicked.
 * @param onSelectionChange Invoked when selection state of an item changed.
 * @param onDeleteClick Invoked when delete icon button is clicked.
 * @param onShareUrlClick Invoked when share url button is clicked.
 * @param onShareFileClick Invoked when share file button is clicked.
 */
@SuppressWarnings("LongParameterList")
@Composable
private fun DownloadsScreenContent(
    uiState: DownloadUIState,
    paddingValues: PaddingValues,
    onContentTypeSelected: (FileItem.ContentTypeFilter) -> Unit,
    onItemClick: (FileItem) -> Unit,
    onSelectionChange: (FileItem, Boolean) -> Unit,
    onDeleteClick: (FileItem) -> Unit,
    onShareUrlClick: (FileItem) -> Unit,
    onShareFileClick: (FileItem) -> Unit,
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(FirefoxTheme.colors.layer1)
            .widthIn(max = FirefoxTheme.layout.size.containerMaxWidth)
            .padding(paddingValues),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        if (uiState.filtersToDisplay.isNotEmpty()) {
            Filters(
                selectedContentTypeFilter = uiState.selectedContentTypeFilter,
                contentTypeFilters = uiState.filtersToDisplay,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = FirefoxTheme.layout.space.static200),
                onContentTypeSelected = onContentTypeSelected,
            )
        }

        when (uiState.itemsState) {
            is DownloadUIState.ItemsState.NoItems -> EmptyState(modifier = Modifier.fillMaxSize())
            is DownloadUIState.ItemsState.NoSearchResults -> NoSearchResults(
                modifier = Modifier
                    .fillMaxSize()
                    .imePadding(),
            )

            is DownloadUIState.ItemsState.Items -> DownloadsContent(
                items = uiState.itemsState.items,
                mode = uiState.mode,
                onClick = onItemClick,
                onSelectionChange = onSelectionChange,
                onDeleteClick = onDeleteClick,
                onShareUrlClick = onShareUrlClick,
                onShareFileClick = onShareFileClick,
                modifier = Modifier.fillMaxHeight(),
            )
        }
    }
}

@Composable
private fun NavigationIcon(
    iconColorTint: Color,
    showBackIcon: Boolean = false,
    onNavigationIconClick: () -> Unit,
) {
    IconButton(onClick = onNavigationIconClick) {
        Icon(
            painter = if (showBackIcon) {
                painterResource(R.drawable.mozac_ic_back_24)
            } else {
                painterResource(R.drawable.mozac_ic_cross_24)
            },
            contentDescription = if (showBackIcon) {
                stringResource(R.string.download_navigate_back_description)
            } else {
                stringResource(R.string.download_navigate_up_description)
            },
            tint = iconColorTint,
        )
    }
}

@Composable
@OptIn(ExperimentalFoundationApi::class)
private fun DownloadsContent(
    items: List<DownloadListItem>,
    mode: Mode,
    modifier: Modifier = Modifier,
    onClick: (FileItem) -> Unit,
    onSelectionChange: (FileItem, Boolean) -> Unit,
    onDeleteClick: (FileItem) -> Unit,
    onShareUrlClick: (FileItem) -> Unit,
    onShareFileClick: (FileItem) -> Unit,
) {
    val haptics = LocalHapticFeedback.current

    LazyColumn(
        modifier = modifier,
    ) {
        itemsIndexed(
            items = items,
            contentType = { _, item -> item::class },
            key = { _, item ->
                when (item) {
                    is HeaderItem -> item.createdTime
                    is FileItem -> item.id
                }
            },
        ) { index, listItem ->
            when (listItem) {
                is HeaderItem -> {
                    HeaderListItem(
                        headerItem = listItem,
                        modifier = Modifier.animateItem(),
                    )
                }

                is FileItem -> {
                    FileListItem(
                        fileItem = listItem,
                        isSelected = mode.selectedItems.contains(listItem),
                        isMenuIconVisible = mode is Mode.Normal,
                        onDeleteClick = onDeleteClick,
                        onShareUrlClick = onShareUrlClick,
                        onShareFileClick = onShareFileClick,
                        modifier = modifier
                            .animateItem()
                            .combinedClickable(
                                onClick = {
                                    if (mode is Mode.Normal) {
                                        onClick(listItem)
                                    } else {
                                        onSelectionChange(
                                            listItem,
                                            !mode.selectedItems.contains(listItem),
                                        )
                                    }
                                },
                                onLongClick = {
                                    if (mode is Mode.Normal) {
                                        haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                                        onSelectionChange(listItem, true)
                                    }
                                },
                            )
                            .testTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM}.${listItem.fileName}"),
                    )

                    if (index == items.lastIndex || items[index + 1] is HeaderItem) {
                        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))
                    }
                }
            }
        }
    }
}

@Composable
private fun FileListItem(
    fileItem: FileItem,
    isSelected: Boolean,
    isMenuIconVisible: Boolean,
    modifier: Modifier = Modifier,
    onDeleteClick: (FileItem) -> Unit,
    onShareUrlClick: (FileItem) -> Unit,
    onShareFileClick: (FileItem) -> Unit,
) {
    SelectableListItem(
        label = fileItem.fileName ?: fileItem.url,
        description = fileItem.description,
        isSelected = isSelected,
        icon = fileItem.icon,
        afterListAction = {
            if (isMenuIconVisible) {
                var menuExpanded by remember { mutableStateOf(false) }

                Spacer(modifier = Modifier.width(16.dp))

                IconButton(
                    onClick = { menuExpanded = true },
                    modifier = Modifier
                        .size(24.dp)
                        .testTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM_MENU}.${fileItem.fileName}"),
                ) {
                    Icon(
                        painter = painterResource(id = R.drawable.mozac_ic_ellipsis_vertical_24),
                        contentDescription = stringResource(id = R.string.content_description_menu),
                        tint = FirefoxTheme.colors.iconPrimary,
                    )

                    DropdownMenu(
                        menuItems = listOf(
                            MenuItem.TextItem(
                                text = Text.Resource(R.string.download_share_url),
                                onClick = { onShareUrlClick(fileItem) },
                                level = MenuItem.FixedItem.Level.Default,
                            ),
                            MenuItem.TextItem(
                                text = Text.Resource(R.string.download_share_file),
                                onClick = { onShareFileClick(fileItem) },
                                level = MenuItem.FixedItem.Level.Default,
                            ),
                            MenuItem.TextItem(
                                text = Text.Resource(R.string.download_delete_item),
                                onClick = { onDeleteClick(fileItem) },
                                level = MenuItem.FixedItem.Level.Critical,
                            ),
                        ),
                        expanded = menuExpanded,
                        onDismissRequest = { menuExpanded = false },
                    )
                }
            }
        },
        modifier = modifier,
    )
}

@Composable
private fun HeaderListItem(
    headerItem: HeaderItem,
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier) {
        ExpandableListHeader(
            headerText = stringResource(id = headerItem.createdTime.stringRes),
        )
    }
}

@Composable
private fun Filters(
    selectedContentTypeFilter: FileItem.ContentTypeFilter,
    contentTypeFilters: List<FileItem.ContentTypeFilter>,
    modifier: Modifier = Modifier,
    onContentTypeSelected: (FileItem.ContentTypeFilter) -> Unit,
) {
    val listState = rememberLazyListState()
    LazyRow(
        modifier = modifier,
        state = listState,
        horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static100),
        contentPadding = PaddingValues(horizontal = FirefoxTheme.layout.space.static200),
    ) {
        items(
            items = contentTypeFilters,
            key = { it },
        ) { contentTypeParam ->
            DownloadChip(
                selected = selectedContentTypeFilter == contentTypeParam,
                contentTypeFilter = contentTypeParam,
                onContentTypeSelected = onContentTypeSelected,
            )
        }
    }

    LaunchedEffect(selectedContentTypeFilter) {
        val selectedItemInfo =
            listState.layoutInfo.visibleItemsInfo.firstOrNull { it.key == selectedContentTypeFilter }

        if (selectedItemInfo == null || listState.isItemPartiallyVisible(selectedItemInfo)) {
            listState.animateScrollToItem(contentTypeFilters.indexOf(selectedContentTypeFilter))
        }
    }
}

@OptIn(ExperimentalMaterialApi::class)
@Composable
private fun DownloadChip(
    selected: Boolean,
    contentTypeFilter: FileItem.ContentTypeFilter,
    modifier: Modifier = Modifier,
    onContentTypeSelected: (FileItem.ContentTypeFilter) -> Unit,
) {
    FilterChip(
        selected = selected,
        onClick = { onContentTypeSelected(contentTypeFilter) },
        shape = RoundedCornerShape(16.dp),
        border = if (selected) {
            null
        } else {
            BorderStroke(1.dp, FirefoxTheme.colors.borderPrimary)
        },
        colors = ChipDefaults.filterChipColors(
            selectedBackgroundColor = FirefoxTheme.colors.layerAccentNonOpaque,
            selectedContentColor = FirefoxTheme.colors.textPrimary,
            backgroundColor = FirefoxTheme.colors.layer1,
            contentColor = FirefoxTheme.colors.textPrimary,
        ),
        modifier = modifier.height(36.dp),
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            if (selected) {
                // Custom content is used instead of using the `leadingIcon` parameter as the paddings
                // are different and spec requires a specific padding between the icon and text.
                Icon(
                    painter = painterResource(id = R.drawable.mozac_ic_checkmark_24),
                    contentDescription = null,
                    tint = FirefoxTheme.colors.iconPrimary,
                    modifier = Modifier.size(16.dp),
                )
            }
            Text(
                text = stringResource(id = contentTypeFilter.stringRes),
                style = if (selected) {
                    FirefoxTheme.typography.headline8
                } else {
                    FirefoxTheme.typography.body2
                },
            )
        }
    }
}

@Composable
private fun NoSearchResults(modifier: Modifier = Modifier) {
    Box(
        modifier = modifier,
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = stringResource(id = R.string.download_search_no_results_found),
            color = FirefoxTheme.colors.textSecondary,
            style = FirefoxTheme.typography.body2,
        )
    }
}

@Composable
private fun EmptyState(modifier: Modifier = Modifier) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = stringResource(id = R.string.download_empty_message_2),
            color = FirefoxTheme.colors.textPrimary,
            style = FirefoxTheme.typography.headline7,
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = stringResource(id = R.string.download_empty_description),
            color = FirefoxTheme.colors.textPrimary,
            style = FirefoxTheme.typography.body2,
        )
    }
}

@Composable
private fun getToolbarConfig(mode: Mode): ToolbarConfig {
    return when (mode) {
        is Mode.Editing -> ToolbarConfig(
            title = stringResource(
                R.string.download_multi_select_title,
                mode.selectedItems.size,
            ),
            backgroundColor = FirefoxTheme.colors.layerAccent,
            textColor = FirefoxTheme.colors.textOnColorPrimary,
            iconColor = FirefoxTheme.colors.iconOnColor,
        )

        is Mode.Normal -> ToolbarConfig(
            title = stringResource(R.string.library_downloads),
            backgroundColor = FirefoxTheme.colors.layer1,
            textColor = FirefoxTheme.colors.textPrimary,
            iconColor = FirefoxTheme.colors.iconPrimary,
        )
    }
}

private class DownloadsScreenPreviewModelParameterProvider :
    PreviewParameterProvider<DownloadUIState> {
    override val values: Sequence<DownloadUIState>
        get() = sequenceOf(
            DownloadUIState.INITIAL,
            DownloadUIState(
                items = listOf(
                    FileItem(
                        id = "1",
                        fileName = "File 1",
                        url = "https://example.com/file1",
                        formattedSize = "1.2 MB",
                        displayedShortUrl = "example.com",
                        contentType = "application/pdf",
                        status = DownloadState.Status.COMPLETED,
                        filePath = "/path/to/file1",
                        createdTime = CreatedTime.TODAY,
                    ),
                    FileItem(
                        id = "2",
                        fileName = "File 2",
                        url = "https://example.com/file2",
                        formattedSize = "2.3 MB",
                        displayedShortUrl = "example.com",
                        contentType = "image/png",
                        status = DownloadState.Status.COMPLETED,
                        filePath = "/path/to/file1",
                        createdTime = CreatedTime.TODAY,
                    ),
                    FileItem(
                        id = "3",
                        fileName = "File 3",
                        url = "https://example.com/file3",
                        formattedSize = "3.4 MB",
                        displayedShortUrl = "example.com",
                        contentType = "application/zip",
                        status = DownloadState.Status.COMPLETED,
                        filePath = "/path/to/file1",
                        createdTime = CreatedTime.OLDER,
                    ),
                ),
                mode = Mode.Normal,
                pendingDeletionIds = emptySet(),
                userSelectedContentTypeFilter = FileItem.ContentTypeFilter.All,
            ),
        )
}

@Composable
@FlexibleWindowLightDarkPreview
private fun DownloadsScreenPreviews(
    @PreviewParameter(DownloadsScreenPreviewModelParameterProvider::class) state: DownloadUIState,
) {
    val store = remember { DownloadUIStore(initialState = state) }
    val snackbarHostState = remember { AcornSnackbarHostState() }
    val scope = rememberCoroutineScope()
    FirefoxTheme {
        Box {
            DownloadsScreen(
                downloadsStore = store,
                onItemClick = {
                    scope.launch {
                        snackbarHostState.showSnackbar(
                            SnackbarState(message = "Item ${it.fileName} clicked"),
                        )
                    }
                },
                onMultipleItemsDeleteClick = {},
                onItemDeleteClick = {
                    store.dispatch(DownloadUIAction.UpdateFileItems(store.state.items - it))
                    scope.launch {
                        snackbarHostState.showSnackbar(
                            SnackbarState(
                                message = "Item ${it.fileName} deleted",
                                type = SnackbarState.Type.Warning,
                            ),
                        )
                    }
                },
                onNavigationIconClick = {},
            )
            SnackbarHost(
                snackbarHostState = snackbarHostState,
                modifier = Modifier.align(Alignment.BottomCenter),
            )
        }
    }
}
