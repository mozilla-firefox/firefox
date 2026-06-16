/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")
@file:OptIn(ExperimentalMaterial3Api::class)

package org.mozilla.fenix.bookmarks

import androidx.activity.compose.BackHandler
import androidx.activity.compose.LocalActivity
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.isImeVisible
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onPlaced
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.CollectionItemInfo
import androidx.compose.ui.semantics.collectionInfo
import androidx.compose.ui.semantics.collectionItemInfo
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.graphics.toColorInt
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.state.action.AwesomeBarAction
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.FloatingActionButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.menu.DropdownMenu
import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.snackbar.Snackbar
import mozilla.components.compose.base.snackbar.displaySnackbar
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.textfield.TextField
import mozilla.components.compose.base.utils.BackInvokedHandler
import mozilla.components.compose.browser.awesomebar.AwesomeBar
import mozilla.components.compose.browser.awesomebar.AwesomeBarDefaults
import mozilla.components.compose.browser.awesomebar.AwesomeBarOrientation
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.ui.BrowserToolbarQuery
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.ktx.android.view.hideKeyboard
import org.mozilla.fenix.R
import org.mozilla.fenix.bookmarks.BookmarksTestTag.BOOKMARK_TOOLBAR
import org.mozilla.fenix.bookmarks.BookmarksTestTag.EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD
import org.mozilla.fenix.bookmarks.BookmarksTestTag.EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD
import org.mozilla.fenix.bookmarks.ui.AddFolderScreen
import org.mozilla.fenix.bookmarks.ui.EditFolderScreen
import org.mozilla.fenix.bookmarks.ui.SelectFolderScreen
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.components
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.compose.list.SelectableFaviconListItem
import org.mozilla.fenix.compose.list.SelectableIconListItem
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.getRootView
import org.mozilla.fenix.search.SearchFragmentAction.SuggestionClicked
import org.mozilla.fenix.search.SearchFragmentAction.SuggestionSelected
import org.mozilla.fenix.search.SearchFragmentState
import org.mozilla.fenix.search.SearchFragmentStore
import org.mozilla.fenix.search.awesomebar.DeleteHistoryEntryDelegate
import org.mozilla.fenix.theme.FirefoxTheme
import mozilla.components.ui.icons.R as iconsR

private const val MATERIAL_DESIGN_SCRIM = "#52000000"

/**
 * The UI host for the Bookmarks list screen and related subscreens.
 *
 * @param buildStore A builder function to construct a [BookmarksStore] using the NavController that's local
 * to the nav graph for the Bookmarks view hierarchy.
 * @param appStore [AppStore] for syncing with other application features.
 * @param browserStore [BrowserStore] for checking browser state and dispatching browser action.
 * @param toolbarStore [BrowserToolbarStore] controlling the search toolbar.
 * @param searchStore [SearchFragmentStore] controlling how search results are displayed.
 * @param bookmarksSearchEngine [SearchEngine] the default search engine to use when searching bookmarks.
 * @param profiler app profiler used to access firefox profile features.
 * @param startDestination the screen on which to initialize [BookmarksScreen] with.
 * @param bookmarkToLoad The guid of a bookmark to load when landing on the edit screen.
 */
@Composable
internal fun BookmarksScreen(
    buildStore: (NavHostController) -> BookmarksStore,
    appStore: AppStore = components.appStore,
    browserStore: BrowserStore = components.core.store,
    toolbarStore: BrowserToolbarStore,
    searchStore: SearchFragmentStore,
    bookmarksSearchEngine: SearchEngine?,
    profiler: Profiler? = components.core.engine.profiler,
    startDestination: String = BookmarksDestinations.LIST,
    bookmarkToLoad: String? = null,
) {
    val navController = rememberNavController()
    val store = remember { buildStore(navController) }

    val isPrivateModeLocked by remember {
        appStore.stateFlow.map { appState -> appState.isPrivateScreenLocked }
    }.collectAsState(initial = appStore.state.isPrivateScreenLocked)

    LaunchedEffect(Unit) {
        store.dispatch(ViewAppeared(bookmarkToLoad = bookmarkToLoad))
    }

    LaunchedEffect(isPrivateModeLocked) {
        if (!isPrivateModeLocked) {
            store.dispatch(PrivateBrowsingAuthorized)
        }
    }

    NavHost(
        navController = navController,
        startDestination = startDestination,
    ) {
        composable(route = BookmarksDestinations.LIST) {
            BackHandler { store.dispatch(BackClicked) }
            BookmarksList(
                store = store,
                appStore = appStore,
                browserStore = browserStore,
                toolbarStore = toolbarStore,
                searchStore = searchStore,
                bookmarksSearchEngine = bookmarksSearchEngine,
                profiler = profiler,
            )
        }
        composable(route = BookmarksDestinations.ADD_FOLDER) {
            BackHandler { store.dispatch(BackClicked) }
            AddFolderScreen(store = store)
        }
        composable(route = BookmarksDestinations.EDIT_FOLDER) {
            BackHandler { store.dispatch(BackClicked) }
            EditFolderScreen(store = store)
        }
        composable(route = BookmarksDestinations.EDIT_BOOKMARK) {
            BackHandler { store.dispatch(BackClicked) }
            EditBookmarkScreen(store = store)
        }
        composable(route = BookmarksDestinations.SELECT_FOLDER) {
            BackHandler { store.dispatch(BackClicked) }
            SelectFolderScreen(store = store)
        }
    }
}

internal object BookmarksDestinations {
    const val LIST = "list"
    const val ADD_FOLDER = "add folder"
    const val EDIT_FOLDER = "edit folder"
    const val EDIT_BOOKMARK = "edit bookmark"
    const val SELECT_FOLDER = "select folder"
}

/**
 * The Bookmarks list screen.
 */
@OptIn(ExperimentalLayoutApi::class) // for WindowInsets.isImeVisible
@Suppress("LongMethod", "CognitiveComplexMethod", "CyclomaticComplexMethod")
@Composable
private fun BookmarksList(
    store: BookmarksStore,
    appStore: AppStore,
    browserStore: BrowserStore,
    toolbarStore: BrowserToolbarStore,
    searchStore: SearchFragmentStore,
    bookmarksSearchEngine: SearchEngine?,
    profiler: Profiler?,
) {
    val state by store.stateFlow.collectAsState()
    val searchState = searchStore.observeAsComposableState { it }.value
    val awesomebarBackground = AwesomeBarDefaults.colors().background
    val awesomebarScrim by remember(searchState.query) {
        derivedStateOf {
            when (searchState.query.isNotEmpty()) {
                true -> awesomebarBackground
                else -> Color(MATERIAL_DESIGN_SCRIM.toColorInt())
            }
        }
    }
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    val saveableStateHolder = rememberSaveableStateHolder()

    val view = LocalView.current
    val focusManager = LocalFocusManager.current
    val keyboardController = LocalSoftwareKeyboardController.current

    val snackbarMessage = when (state.bookmarksSnackbarState) {
        BookmarksSnackbarState.CantEditDesktopFolders -> stringResource(R.string.bookmark_cannot_edit_root)
        BookmarksSnackbarState.SelectFolderFailed -> {
            stringResource(R.string.bookmark_error_select_folder)
        }
        is BookmarksSnackbarState.BookmarkMoved -> {
            stringResource(
                R.string.bookmark_moved_single_item,
                formatArgs = arrayOf(
                    (state.bookmarksSnackbarState as BookmarksSnackbarState.BookmarkMoved).from,
                    (state.bookmarksSnackbarState as BookmarksSnackbarState.BookmarkMoved).to,
                ),
            )
        }
        BookmarksSnackbarState.ImportFailed -> stringResource(R.string.bookmark_import_failure_snackbar)
        else -> ""
    }

    val fabHeight = remember { mutableIntStateOf(0) }

    LaunchedEffect(state.bookmarksSnackbarState) {
        when (state.bookmarksSnackbarState) {
            BookmarksSnackbarState.None -> return@LaunchedEffect
            BookmarksSnackbarState.CantEditDesktopFolders -> scope.launch {
                snackbarHostState.displaySnackbar(
                    message = snackbarMessage,
                    onDismissPerformed = { store.dispatch(SnackbarAction.Dismissed) },
                )
            }
            BookmarksSnackbarState.SelectFolderFailed -> scope.launch {
                snackbarHostState.displaySnackbar(
                    message = snackbarMessage,
                    onDismissPerformed = { store.dispatch(SnackbarAction.Dismissed) },
                )
            }
            is BookmarksSnackbarState.BookmarkMoved -> scope.launch {
                snackbarHostState.displaySnackbar(
                    message = snackbarMessage,
                    onDismissPerformed = { store.dispatch(SnackbarAction.Dismissed) },
                )
            }
            BookmarksSnackbarState.ImportFailed -> scope.launch {
                snackbarHostState.displaySnackbar(
                    message = snackbarMessage,
                    onDismissPerformed = { store.dispatch(SnackbarAction.Dismissed) },
                )
            }
        }
    }

    LaunchedEffect(state.isSearching) {
        when (state.isSearching) {
            true -> {
                if (!appStore.state.searchState.isSearchActive) {
                    bookmarksSearchEngine?.let {
                        appStore.dispatch(AppAction.SearchAction.SearchEngineSelected(it, false))
                    }
                    appStore.dispatch(AppAction.SearchAction.SearchStarted())
                }
            }
            false -> {
                appStore.dispatch(AppAction.SearchAction.SearchEnded)
                toolbarStore.dispatch(BrowserEditToolbarAction.SearchQueryUpdated(BrowserToolbarQuery("")))
                browserStore.dispatch(AwesomeBarAction.EngagementFinished(abandoned = true))
                focusManager.clearFocus()
                keyboardController?.hide()
            }
        }
    }

    BackInvokedHandler(state.isSearching) {
        store.dispatch(SearchDismissed)
    }

    WarnDialog(store = store)

    val dialogState = state.bookmarksDeletionDialogState
    if (dialogState is DeletionDialogState.Presenting) {
        AlertDialogDeletionWarning(
            onCancelTapped = { store.dispatch(DeletionDialogAction.CancelTapped) },
            onDeleteTapped = { store.dispatch(DeletionDialogAction.DeleteTapped) },
        )
    }

    Scaffold(
        snackbarHost = {
            Box(modifier = Modifier.fillMaxWidth()) {
                SnackbarHost(
                    hostState = snackbarHostState,
                    modifier = Modifier.align(Alignment.BottomCenter),
                ) {
                    Snackbar(snackbarData = it)
                }
            }
        },
        floatingActionButton = {
            if (!state.isLoading && state.emptyListState() == null) {
                FloatingActionButton(
                    modifier = Modifier.onPlaced { fabHeight.intValue = it.size.height },
                    icon = painterResource(iconsR.drawable.mozac_ic_search_24),
                    contentDescription = stringResource(R.string.bookmark_search_button_content_description),
                    onClick = { store.dispatch(SearchClicked) },
                )
            }
        },
        topBar = {
            if (state.isSearching) {
                BrowserToolbar(toolbarStore)
            } else {
                BookmarksListTopBar(store = store)
            }
        },
    ) { paddingValues ->
        if (state.isLoading) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CircularProgressIndicator()
            }
            return@Scaffold
        }

        val emptyListState = state.emptyListState()
        if (emptyListState != null) {
            EmptyList(
                state = emptyListState,
                dispatcher = store::dispatch,
            )
            return@Scaffold
        }
        val inSelectionMode = state.selectedItems.isNotEmpty()

        saveableStateHolder.SaveableStateProvider(state.currentFolder.guid) {
            LazyColumn(
                modifier = Modifier
                    .padding(paddingValues)
                    .padding(vertical = 16.dp)
                    .semantics {
                        collectionInfo =
                            CollectionInfo(rowCount = state.bookmarkItems.size, columnCount = 1)
                    }
                    .fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                val folders = state.bookmarkItems.folders()
                itemsIndexed(folders) { index, item ->
                    var showMenu by remember { mutableStateOf(false) }
                    val isSelected = item in state.selectedItems

                    if (item.isDesktopFolder) {
                        SelectableIconListItem(
                            label = item.title,
                            isSelected = item in state.selectedItems,
                            description =
                                stringResource(
                                    R.string.bookmarks_folder_description,
                                    item.nestedItemCount.toString(),
                                ),
                            onClick = { store.dispatch(FolderClicked(item)) },
                            beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                            modifier = Modifier
                                .semantics(mergeDescendants = true) {
                                    if (inSelectionMode) {
                                        selected = isSelected
                                    }
                                    collectionItemInfo = CollectionItemInfo(
                                        rowIndex = index,
                                        rowSpan = 1,
                                        columnSpan = 1,
                                        columnIndex = 0,
                                    )
                                }
                                .width(FirefoxTheme.layout.size.containerMaxWidth),
                            labelModifier = Modifier.fillMaxWidth(),
                        )
                    } else {
                        SelectableIconListItem(
                            label = item.title,
                            isSelected = isSelected,
                            description = stringResource(
                                id = R.string.bookmarks_folder_description,
                                item.nestedItemCount.toString(),
                            ),
                            onClick = { store.dispatch(FolderClicked(item)) },
                            onLongClick = { store.dispatch(FolderLongClicked(item)) },
                            beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                            modifier = Modifier
                                .semantics(mergeDescendants = true) {
                                    if (inSelectionMode) {
                                        selected = isSelected
                                    }
                                    collectionItemInfo = CollectionItemInfo(
                                        rowIndex = index,
                                        rowSpan = 1,
                                        columnSpan = 1,
                                        columnIndex = 0,
                                    )
                                }
                                .width(FirefoxTheme.layout.size.containerMaxWidth),
                            labelModifier = Modifier.fillMaxWidth(),
                        ) {
                            Box {
                                IconButton(
                                    onClick = { showMenu = true },
                                    contentDescription = stringResource(
                                        R.string.bookmark_item_menu_button_content_description,
                                        item.title,
                                    ),
                                    modifier = Modifier.size(24.dp),
                                ) {
                                    Icon(
                                        painter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                                        contentDescription = null,
                                    )
                                }

                                BookmarkListFolderMenu(
                                    showMenu = showMenu,
                                    onDismissRequest = { showMenu = false },
                                    folder = item,
                                    store = store,
                                )
                            }
                        }
                    }
                }

                if (folders.isNotEmpty()) {
                    item {
                        HorizontalDivider()
                    }
                }

                itemsIndexed(state.bookmarkItems.bookmarks()) { index, item ->
                    var showMenu by remember { mutableStateOf(false) }
                    val isSelected = item in state.selectedItems

                    SelectableFaviconListItem(
                        label = item.title,
                        url = item.previewImageUrl,
                        isSelected = isSelected,
                        description = item.url,
                        onClick = { store.dispatch(BookmarkClicked(item)) },
                        onLongClick = { store.dispatch(BookmarkLongClicked(item)) },
                        modifier = Modifier
                            .semantics(mergeDescendants = true) {
                                if (inSelectionMode) {
                                    selected = isSelected
                                }
                                collectionItemInfo = CollectionItemInfo(
                                    rowIndex = index,
                                    rowSpan = 1,
                                    columnSpan = 1,
                                    columnIndex = 0,
                                )
                            }
                            .width(FirefoxTheme.layout.size.containerMaxWidth),
                    ) {
                        Box {
                            IconButton(
                                onClick = { showMenu = true },
                                contentDescription = stringResource(
                                    R.string.bookmark_item_menu_button_content_description,
                                    item.title,
                                ),
                                modifier = Modifier.size(24.dp),
                            ) {
                                Icon(
                                    painter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                                    contentDescription = null,
                                )
                            }

                            BookmarkListItemMenu(
                                showMenu = showMenu,
                                onDismissRequest = { showMenu = false },
                                bookmark = item,
                                store = store,
                            )
                        }
                    }
                }

                fabOffsetSpace(fabHeight.intValue)
            }
        }

        if (state.isSearching && searchState.searchSuggestionsProviders.isNotEmpty()) {
            val activity = LocalActivity.current
            val deleteHistoryDelegate = remember(activity?.getRootView(), searchStore) {
                activity?.getRootView()?.let {
                    DeleteHistoryEntryDelegate(it, it.context.components, searchStore)
                }
            }

            Box(
                modifier = Modifier
                    .background(awesomebarScrim)
                    .padding(paddingValues)
                    .fillMaxSize()
                    .pointerInput(WindowInsets.isImeVisible) {
                        detectTapGestures(
                            // Exit search for any touches in the empty area of the awesomebar.
                            onPress = {
                                focusManager.clearFocus()
                                keyboardController?.hide()
                                store.dispatch(SearchDismissed)
                            },
                        )
                    },
            ) {
                if (searchState.shouldShowSearchSuggestions) {
                    AwesomeBar(
                        text = searchState.query,
                        providers = searchState.searchSuggestionsProviders,
                        hiddenSuggestions = searchState.hiddenSuggestions,
                        orientation = AwesomeBarOrientation.TOP,
                        onSuggestionClicked = { suggestion ->
                            searchStore.dispatch(SuggestionClicked(suggestion))
                        },
                        onAutoComplete = { suggestion ->
                            searchStore.dispatch(SuggestionSelected(suggestion))
                        },
                        onRemoveClicked = { suggestion ->
                            deleteHistoryDelegate?.handleDeletingHistoryEntry(suggestion)
                        },
                        onVisibilityStateUpdated = {
                            browserStore.dispatch(AwesomeBarAction.VisibilityStateUpdated(it))
                        },
                        onScroll = { view.hideKeyboard() },
                        profiler = profiler,
                    )
                }
            }
        }
    }
}

private fun LazyListScope.fabOffsetSpace(fabHeight: Int) {
    item {
        val fabHeightDp = with(LocalDensity.current) {
            fabHeight.toDp()
        }
        Spacer(modifier = Modifier.size(fabHeightDp))
    }
}

@Composable
private fun BookmarksListTopBar(
    store: BookmarksStore,
) {
    val selectedItems by remember { store.stateFlow.map { it.selectedItems } }
        .collectAsState(initial = store.state.selectedItems)
    val recursiveCount by remember { store.stateFlow.map { it.recursiveSelectedCount } }
        .collectAsState(initial = store.state.recursiveSelectedCount)
    val folderTitle by remember { store.stateFlow.map { store.state.currentFolder.title } }
        .collectAsState(store.state.currentFolder.title)
    var showMenu by remember { mutableStateOf(false) }
    val showSortMenu by remember { store.stateFlow.map { it.sortMenuShown } }
        .collectAsState(store.state.sortMenuShown)

    val backgroundColor = if (selectedItems.isEmpty()) {
        MaterialTheme.colorScheme.surface
    } else {
        MaterialTheme.colorScheme.primary
    }

    val textColor = if (selectedItems.isEmpty()) {
        MaterialTheme.colorScheme.onSurface
    } else {
        MaterialTheme.colorScheme.onPrimary
    }
    val iconColor = textColor

    Box {
        TopAppBar(
            modifier = Modifier.semantics {
                testTagsAsResourceId = true
                testTag = BOOKMARK_TOOLBAR
            },
            colors = TopAppBarDefaults.topAppBarColors(containerColor = backgroundColor),
            title = {
                Text(
                    color = textColor,
                    style = FirefoxTheme.typography.headline5,
                    text = if (selectedItems.isNotEmpty()) {
                        val total = selectedItems.size + (recursiveCount ?: 0)
                        stringResource(R.string.bookmarks_multi_select_title, total)
                    } else {
                        folderTitle
                    },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
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
                        tint = iconColor,
                    )
                }
            },
            actions = {
                BookmarksListTopBarActions(
                    selectedItems = selectedItems,
                    store = store,
                    iconColor = iconColor,
                    showSortMenu = showSortMenu,
                    showMenu = showMenu,
                    onShowMenuChanged = { showMenu = it },
                )
            },
            windowInsets = WindowInsets(
                top = 0.dp,
                bottom = 0.dp,
            ),
        )
    }
}

@Composable
private fun BookmarksListTopBarActions(
    selectedItems: List<BookmarkItem>,
    store: BookmarksStore,
    iconColor: Color,
    showSortMenu: Boolean,
    showMenu: Boolean,
    onShowMenuChanged: (Boolean) -> Unit,
) {
    val isCurrentFolderDesktopRoot by remember {
        store.stateFlow.map { store.state.currentFolder.isDesktopRoot }
    }.collectAsState(store.state.currentFolder.isDesktopRoot)
    val isCurrentFolderMobileRoot by remember {
        store.stateFlow.map { store.state.currentFolder.isMobileRoot }
    }.collectAsState(store.state.currentFolder.isMobileRoot)

    when {
        selectedItems.isEmpty() -> BookmarksListTopBarActionsNoSelection(
            store = store,
            iconColor = iconColor,
            showSortMenu = showSortMenu,
            isCurrentFolderDesktopRoot = isCurrentFolderDesktopRoot,
            isCurrentFolderMobileRoot = isCurrentFolderMobileRoot,
        )

        selectedItems.any { it is BookmarkItem.Folder } -> BookmarksListTopBarActionsFolderSelections(
            store = store,
            iconColor = iconColor,
            onShowMenuChanged = onShowMenuChanged,
            showMenu = showMenu,
        )

        else -> BookmarksListTopBarActionsSelections(
            selectedItems = selectedItems,
            store = store,
            iconColor = iconColor,
            onShowMenuChanged = onShowMenuChanged,
            showMenu = showMenu,
        )
    }
}

@Composable
private fun BookmarksListTopBarActionsSelections(
    selectedItems: List<BookmarkItem>,
    store: BookmarksStore,
    iconColor: Color,
    onShowMenuChanged: (Boolean) -> Unit,
    showMenu: Boolean,
) {
    if (selectedItems.size == 1) {
        IconButton(
            onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.EditClicked) },
            contentDescription = stringResource(R.string.bookmark_menu_edit_button),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_edit_24),
                contentDescription = null,
                tint = iconColor,
            )
        }
    }
    IconButton(
        onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.MoveClicked) },
        contentDescription = stringResource(R.string.bookmark_menu_move_button),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_folder_arrow_right_24),
            contentDescription = null,
            tint = iconColor,
        )
    }
    Box {
        IconButton(
            onClick = { onShowMenuChanged(true) },
            contentDescription = stringResource(
                R.string.content_description_menu,
            ),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                contentDescription = null,
                tint = iconColor,
            )
        }

        BookmarkListOverflowMenu(
            showMenu = showMenu,
            onDismissRequest = { onShowMenuChanged(false) },
            store = store,
        )
    }
}

@Composable
private fun BookmarksListTopBarActionsFolderSelections(
    store: BookmarksStore,
    iconColor: Color,
    onShowMenuChanged: (Boolean) -> Unit,
    showMenu: Boolean,
) {
    IconButton(
        onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.MoveClicked) },
        contentDescription = stringResource(R.string.bookmark_menu_move_button),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_folder_arrow_right_24),
            contentDescription = null,
            tint = iconColor,
        )
    }

    IconButton(
        onClick = {
            store.dispatch(BookmarksListMenuAction.MultiSelect.DeleteClicked)
        },
        contentDescription = stringResource(R.string.bookmark_menu_delete_button),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_delete_24),
            contentDescription = null,
            tint = iconColor,
        )
    }

    Box {
        IconButton(
            onClick = { onShowMenuChanged(true) },
            contentDescription = stringResource(
                R.string.content_description_menu,
            ),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                contentDescription = null,
                tint = iconColor,
            )
        }

        FolderListOverflowMenu(
            showFolderMenu = showMenu,
            onDismissRequest = { onShowMenuChanged(false) },
            store = store,
        )
    }
}

@Composable
private fun BookmarksListTopBarActionsNoSelection(
    store: BookmarksStore,
    iconColor: Color,
    showSortMenu: Boolean,
    isCurrentFolderDesktopRoot: Boolean,
    isCurrentFolderMobileRoot: Boolean,
) {
    Box {
        IconButton(
            onClick = {
                store.dispatch(BookmarksListMenuAction.SortMenu.SortMenuButtonClicked)
            },
            contentDescription = stringResource(
                R.string.bookmark_sort_menu_content_desc,
            ),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_sort_24),
                contentDescription = null,
                tint = iconColor,
            )
        }

        BookmarkSortOverflowMenu(
            showMenu = showSortMenu,
            onDismissRequest = {
                store.dispatch(BookmarksListMenuAction.SortMenu.SortMenuDismissed)
            },
            store = store,
        )
    }

    if (!isCurrentFolderDesktopRoot) {
        IconButton(
            onClick = { store.dispatch(AddFolderClicked) },
            contentDescription = stringResource(
                R.string.bookmark_add_new_folder_button_content_description,
            ),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_folder_add_24),
                contentDescription = null,
                tint = iconColor,
            )
        }
    }

    if (!isCurrentFolderMobileRoot) {
        IconButton(
            onClick = { store.dispatch(CloseClicked) },
            contentDescription = stringResource(
                R.string.bookmark_close_button_content_description,
            ),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_cross_24),
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurface,
            )
        }
    } else if (store.state.showBookmarksImport) {
        Box {
            IconButton(
                onClick = { store.dispatch(RootOverflowMenuClicked) },
                contentDescription = stringResource(R.string.bookmark_import_bookmarks_button_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurface,
                )
            }

            RootBookmarksOverflowMenu(store)
        }
    }
}

@Composable
private fun WarnDialog(
    store: BookmarksStore,
) {
    val state by remember { store.stateFlow.map { it.openTabsConfirmationDialog } }
        .collectAsState(initial = store.state.openTabsConfirmationDialog)

    val dialog = state
    if (dialog is OpenTabsConfirmationDialog.Presenting) {
        AlertDialog(
            title = {
                Text(
                    text = stringResource(R.string.open_all_warning_title, dialog.numberOfTabs),
                )
            },
            text = {
                Text(
                    text = stringResource(R.string.open_all_warning_message, dialog.numberOfTabs),
                )
            },
            onDismissRequest = { store.dispatch(OpenTabsConfirmationDialogAction.CancelTapped) },
            confirmButton = {
                TextButton(
                    text = stringResource(R.string.open_all_warning_confirm),
                    onClick = { store.dispatch(OpenTabsConfirmationDialogAction.ConfirmTapped) },
                )
            },
            dismissButton = {
                TextButton(
                    text = stringResource(R.string.open_all_warning_cancel),
                    onClick = { store.dispatch(OpenTabsConfirmationDialogAction.CancelTapped) },
                )
            },
        )
    }
}

@Composable
internal fun AlertDialogDeletionWarning(
    onCancelTapped: () -> Unit,
    onDeleteTapped: () -> Unit,
) {
    AlertDialog(
        text = {
            Text(
                text = stringResource(R.string.bookmark_delete_folders_confirmation_dialog),
                style = FirefoxTheme.typography.body2,
            )
        },
        onDismissRequest = onCancelTapped,
        confirmButton = {
            TextButton(
                text = stringResource(R.string.bookmark_menu_delete_button),
                onClick = onDeleteTapped,
            )
        },
        dismissButton = {
            TextButton(
                text = stringResource(R.string.bookmark_delete_negative),
                onClick = onCancelTapped,
            )
        },
    )
}

@Composable
private fun RootBookmarksOverflowMenu(store: BookmarksStore) {
    val showMenu by remember { store.stateFlow.map { store.state.rootMenuShown } }
        .collectAsState(initial = store.state.rootMenuShown)

    val menuItems = listOf(
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_import_menu_button),
            onClick = { store.dispatch(ImportAction.ImportFileClicked.FromMenu) },
        ),
    )

    DropdownMenu(
        menuItems = menuItems,
        expanded = showMenu,
        onDismissRequest = { store.dispatch(RootOverflowMenuDismissed) },
    )
}

private sealed class EmptyListState {
    data object NotAuthenticated : EmptyListState()
    data object Authenticated : EmptyListState()
    data object Folder : EmptyListState()
}

private fun BookmarksState.emptyListState(): EmptyListState? {
    return when {
        bookmarkItems.isNotEmpty() -> null
        currentFolder.guid != BookmarkRoot.Mobile.id -> EmptyListState.Folder
        isSignedIntoSync -> EmptyListState.Authenticated
        !isSignedIntoSync -> EmptyListState.NotAuthenticated
        else -> null
    }
}

@Composable
private fun EmptyList(
    state: EmptyListState,
    dispatcher: (BookmarksAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .fillMaxSize()
            .padding(horizontal = 32.dp),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            modifier = Modifier.width(FirefoxTheme.layout.size.containerMaxWidth),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            when (state) {
                is EmptyListState.NotAuthenticated -> RootEmptyContent(
                    dispatcher,
                    showSignIn = true,
                )
                EmptyListState.Authenticated -> RootEmptyContent(
                    dispatcher,
                    showSignIn = false,
                )
                EmptyListState.Folder -> FolderEmptyContent()
            }
        }
    }
}

@Composable
private fun RootEmptyContent(
    dispatcher: (BookmarksAction) -> Unit,
    showSignIn: Boolean,
) {
    Image(
        painter = painterResource(R.drawable.ic_kit_bookmarks_empty_state),
        modifier = Modifier.width(204.dp).height(182.dp),
        contentDescription = null,
    )

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

    Text(
        text = stringResource(R.string.bookmark_empty_list_root_title),
        color = MaterialTheme.colorScheme.onSurface,
        style = FirefoxTheme.typography.headline6,
        textAlign = TextAlign.Center,
    )

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static300))

    if (showSignIn) {
        Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

        OutlinedButton(
            text = stringResource(R.string.bookmark_empty_list_guest_cta),
            onClick = { dispatcher(SignIntoSyncClicked) },
            modifier = Modifier
                .heightIn(40.dp)
                .fillMaxWidth(),
        )
    }
}

@Composable
private fun FolderEmptyContent() {
    Image(
        painter = painterResource(R.drawable.bookmarks_folder_illustration),
        contentDescription = null,
    )

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static200))

    Text(
        text = stringResource(R.string.bookmark_empty_list_title),
        color = MaterialTheme.colorScheme.onSurface,
        style = FirefoxTheme.typography.headline6,
    )

    Spacer(modifier = Modifier.height(FirefoxTheme.layout.space.static100))

    Text(
        text = stringResource(R.string.bookmark_empty_list_folder_description),
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        style = FirefoxTheme.typography.body2,
        textAlign = TextAlign.Center,
    )
}

@Composable
@Suppress("Deprecation") // https://bugzilla.mozilla.org/show_bug.cgi?id=1927718
private fun BookmarkSortOverflowMenu(
    showMenu: Boolean,
    onDismissRequest: () -> Unit,
    store: BookmarksStore,
) {
    val sortOrder by remember { store.stateFlow.map { store.state.sortOrder } }
        .collectAsState(store.state.sortOrder)

    val menuItems = listOf(
        MenuItem.CheckableItem(
            text = Text.Resource(R.string.bookmark_sort_menu_custom),
            isChecked = sortOrder is BookmarksListSortOrder.Positional,
            onClick = { store.dispatch(BookmarksListMenuAction.SortMenu.CustomSortClicked) },
        ),
        MenuItem.CheckableItem(
            text = Text.Resource(R.string.bookmark_sort_menu_newest),
            isChecked = sortOrder == BookmarksListSortOrder.Created(ascending = true),
            onClick = { store.dispatch(BookmarksListMenuAction.SortMenu.NewestClicked) },
        ),
        MenuItem.CheckableItem(
            text = Text.Resource(R.string.bookmark_sort_menu_oldest),
            isChecked = sortOrder == BookmarksListSortOrder.Created(ascending = false),
            onClick = { store.dispatch(BookmarksListMenuAction.SortMenu.OldestClicked) },
        ),
        MenuItem.CheckableItem(
            text = Text.Resource(R.string.bookmark_sort_menu_a_to_z),
            isChecked = sortOrder == BookmarksListSortOrder.Alphabetical(ascending = true),
            onClick = { store.dispatch(BookmarksListMenuAction.SortMenu.AtoZClicked) },
        ),
        MenuItem.CheckableItem(
            text = Text.Resource(R.string.bookmark_sort_menu_z_to_a),
            isChecked = sortOrder == BookmarksListSortOrder.Alphabetical(ascending = false),
            onClick = { store.dispatch(BookmarksListMenuAction.SortMenu.ZtoAClicked) },
        ),
    )
    DropdownMenu(
        menuItems = menuItems,
        expanded = showMenu,
        onDismissRequest = onDismissRequest,
    )
}

@Composable
@Suppress("Deprecation") // https://bugzilla.mozilla.org/show_bug.cgi?id=1927718
private fun BookmarkListOverflowMenu(
    showMenu: Boolean,
    onDismissRequest: () -> Unit,
    store: BookmarksStore,
) {
    val menuItems = listOf(
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_select_all_bookmarks),
            onClick = { store.dispatch(BookmarksListMenuAction.SelectAll) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_open_in_new_tab_button),
            onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_open_in_private_tab_button),
            onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_share_button),
            onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.ShareClicked) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_delete_button),
            level = MenuItem.FixedItem.Level.Critical,
            onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.DeleteClicked) },
        ),
    )
    DropdownMenu(
        menuItems = menuItems,
        expanded = showMenu,
        onDismissRequest = onDismissRequest,
    )
}

@Composable
private fun FolderListOverflowMenu(
    showFolderMenu: Boolean,
    onDismissRequest: () -> Unit,
    store: BookmarksStore,
) {
    val menuItems = listOf(
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_select_all_bookmarks),
            onClick = { store.dispatch(BookmarksListMenuAction.SelectAll) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_open_in_new_tab_button),
            onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_open_in_private_tab_button),
            onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_delete_button),
            level = MenuItem.FixedItem.Level.Critical,
            onClick = { store.dispatch(BookmarksListMenuAction.MultiSelect.DeleteClicked) },
        ),
    )
    DropdownMenu(
        menuItems = menuItems,
        expanded = showFolderMenu,
        onDismissRequest = onDismissRequest,
    )
}

@Composable
private fun BookmarkListItemMenu(
    showMenu: Boolean,
    onDismissRequest: () -> Unit,
    bookmark: BookmarkItem.Bookmark,
    store: BookmarksStore,
) {
    val menuItems = listOf(
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_select_button),
            onClick = { store.dispatch(BookmarksListMenuAction.Bookmark.SelectClicked(bookmark)) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_edit_button),
            onClick = { store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmark)) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_move_button),
            onClick = {
                store.dispatch(BookmarksListMenuAction.Bookmark.MoveClicked(bookmark))
            },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_share_button),
            onClick = { store.dispatch(BookmarksListMenuAction.Bookmark.ShareClicked(bookmark)) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_open_in_new_tab_button),
            onClick = { store.dispatch(BookmarksListMenuAction.Bookmark.OpenInNormalTabClicked(bookmark)) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_open_in_private_tab_button),
            onClick = { store.dispatch(BookmarksListMenuAction.Bookmark.OpenInPrivateTabClicked(bookmark)) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_delete_button),
            level = MenuItem.FixedItem.Level.Critical,
            onClick = { store.dispatch(BookmarksListMenuAction.Bookmark.DeleteClicked(bookmark)) },
        ),
    )
    DropdownMenu(
        menuItems = menuItems,
        expanded = showMenu,
        onDismissRequest = onDismissRequest,
    )
}

@Composable
private fun BookmarkListFolderMenu(
    showMenu: Boolean,
    onDismissRequest: () -> Unit,
    folder: BookmarkItem.Folder,
    store: BookmarksStore,
) {
    val menuItems = listOf(
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_select_button),
            onClick = { store.dispatch(BookmarksListMenuAction.Folder.SelectClicked(folder)) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_edit_button),
            onClick = { store.dispatch(BookmarksListMenuAction.Folder.EditClicked(folder)) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_move_button),
            onClick = {
                store.dispatch(BookmarksListMenuAction.Folder.MoveClicked(folder))
            },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_open_all_in_tabs_button),
            onClick = { store.dispatch(BookmarksListMenuAction.Folder.OpenAllInNormalTabClicked(folder)) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_open_all_in_private_tabs_button),
            onClick = { store.dispatch(BookmarksListMenuAction.Folder.OpenAllInPrivateTabClicked(folder)) },
        ),
        MenuItem.TextItem(
            text = Text.Resource(R.string.bookmark_menu_delete_button),
            level = MenuItem.FixedItem.Level.Critical,
            onClick = { store.dispatch(BookmarksListMenuAction.Folder.DeleteClicked(folder)) },
        ),
    )
    DropdownMenu(
        menuItems = menuItems,
        expanded = showMenu,
        onDismissRequest = onDismissRequest,
    )
}

@Composable
private fun EditBookmarkScreen(
    store: BookmarksStore,
) {
    val state by remember { store.stateFlow.map { it.bookmarksEditBookmarkState } }
        .collectAsState(initial = store.state.bookmarksEditBookmarkState)

    val bookmark = state?.bookmark ?: return
    val folder = state?.folder ?: return

    Scaffold(
        topBar = {
            EditBookmarkTopBar(
                onBackClick = { store.dispatch(BackClicked) },
                onDeleteClicked = { store.dispatch(EditBookmarkAction.DeleteClicked) },
            )
        },
    ) { paddingValues ->
        Box(
            modifier = Modifier
                .padding(paddingValues)
                .padding(all = 16.dp)
                .fillMaxWidth(),
            contentAlignment = Alignment.TopCenter,
        ) {
            Column(
                modifier = Modifier.width(FirefoxTheme.layout.size.containerMaxWidth),
            ) {
                BookmarkEditor(
                    bookmarkItem = bookmark,
                    onTitleChanged = { store.dispatch(EditBookmarkAction.TitleChanged(it)) },
                    onURLChanged = { store.dispatch(EditBookmarkAction.URLChanged(it)) },
                )
                Spacer(Modifier.height(24.dp))
                FolderInfo(
                    folderTitle = folder.title,
                    onFolderClicked = { store.dispatch(EditBookmarkAction.FolderClicked) },
                )
            }
        }
    }
}

@Composable
private fun BookmarkEditor(
    bookmarkItem: BookmarkItem.Bookmark,
    onTitleChanged: (String) -> Unit,
    onURLChanged: (String) -> Unit,
) {
    val focusRequester = remember { FocusRequester() }
    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
    }
    Row(
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Favicon(url = bookmarkItem.previewImageUrl, size = 64.dp)

        Column(
            Modifier
                .semantics {
                    testTagsAsResourceId = true
                },
        ) {
            ClearableTextField(
                value = bookmarkItem.title,
                onValueChange = onTitleChanged,
                placeholder = stringResource(R.string.bookmark_name_label_normal_case),
                modifier =
                    Modifier
                        .semantics {
                            testTag = EDIT_BOOKMARK_ITEM_TITLE_TEXT_FIELD
                        }
                        .focusRequester(focusRequester),
            )

            Spacer(modifier = Modifier.height(16.dp))

            ClearableTextField(
                value = bookmarkItem.url,
                onValueChange = onURLChanged,
                placeholder = stringResource(R.string.bookmark_url_label),
                modifier =
                Modifier
                    .semantics {
                        testTag = EDIT_BOOKMARK_ITEM_URL_TEXT_FIELD
                    },
            )
        }
    }
}

@Composable
private fun FolderInfo(
    folderTitle: String,
    onFolderClicked: () -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
        CompositionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.onSurface) {
            Text(
                text = stringResource(R.string.bookmark_save_in_label),
                style = FirefoxTheme.typography.body2,
            )

            Row(
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .height(40.dp)
                    .fillMaxWidth()
                    .clickable { onFolderClicked() },
            ) {
                Icon(
                    painter = painterResource(id = iconsR.drawable.mozac_ic_folder_24),
                    contentDescription = "",
                )

                Text(
                    text = folderTitle,
                    style = FirefoxTheme.typography.body2,
                )
            }
        }
    }
}

@Composable
private fun ClearableTextField(
    placeholder: String,
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    var isFocused by remember { mutableStateOf(false) }

    TextField(
        value = value,
        onValueChange = onValueChange,
        placeholder = placeholder,
        errorText = "",
        modifier = modifier.onFocusChanged { isFocused = it.isFocused },
        trailingIcon = {
            if (isFocused && value.isNotEmpty()) {
                CrossTextFieldButton { onValueChange("") }
            }
        },
    )
}

@Composable
private fun EditBookmarkTopBar(
    onBackClick: () -> Unit,
    onDeleteClicked: () -> Unit,
) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.edit_bookmark_fragment_title),
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
                onClick = onDeleteClicked,
                contentDescription = stringResource(R.string.bookmark_delete_bookmark_content_description),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_delete_24),
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurface,
                )
            }
        },
        windowInsets = WindowInsets(
            top = 0.dp,
            bottom = 0.dp,
        ),
    )
}

@Composable
@FlexibleWindowLightDarkPreview
private fun EditBookmarkScreenPreview() {
    val store = BookmarksStore(
        initialState = BookmarksState(
            bookmarkItems = listOf(),
            selectedItems = listOf(),
            rootMenuShown = false,
            showBookmarksImport = true,
            sortMenuShown = false,
            sortOrder = BookmarksListSortOrder.default,
            recursiveSelectedCount = null,
            currentFolder = BookmarkItem.Folder(
                guid = BookmarkRoot.Mobile.id,
                title = "Bookmarks",
                position = null,
            ),
            isSignedIntoSync = true,
            openTabsConfirmationDialog = OpenTabsConfirmationDialog.None,
            bookmarksDeletionDialogState = DeletionDialogState.None,
            bookmarksSnackbarState = BookmarksSnackbarState.None,
            bookmarksAddFolderState = null,
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = BookmarkItem.Bookmark(
                    url = "https://www.whoevenmakeswebaddressesthislonglikeseriously1.com",
                    title = "this is a very long bookmark title that should overflow 1",
                    previewImageUrl = "",
                    guid = "1",
                    position = null,
                ),
                folder = BookmarkItem.Folder("folder 1", guid = "1", null),
            ),
            bookmarksSelectFolderState = null,
            bookmarksEditFolderState = null,
            bookmarksMultiselectMoveState = null,
            isLoading = false,
            isSearching = false,
        ),
    )

    FirefoxTheme {
        EditBookmarkScreen(store = store)
    }
}

@Composable
@FlexibleWindowLightDarkPreview
private fun BookmarksScreenPreview() {
    val bookmarkItems = List(PREVIEW_BOOKMARKS_SIZE) {
        if (it % 2 == 0) {
            BookmarkItem.Bookmark(
                url = "https://www.whoevenmakeswebaddressesthislonglikeseriously$it.com",
                title = "this is a very long bookmark title that should overflow $it",
                previewImageUrl = "",
                guid = "$it",
                position = null,
            )
        } else {
            BookmarkItem.Folder("folder $it", guid = "$it", position = null)
        }
    }

    val store = { _: NavHostController ->
        BookmarksStore(
            initialState = BookmarksState(
                bookmarkItems = bookmarkItems,
                selectedItems = listOf(),
                rootMenuShown = false,
                showBookmarksImport = true,
                sortMenuShown = false,
                sortOrder = BookmarksListSortOrder.default,
                recursiveSelectedCount = null,
                currentFolder = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks with really really really really really really long name",
                    position = null,
                ),
                isSignedIntoSync = false,
                openTabsConfirmationDialog = OpenTabsConfirmationDialog.None,
                bookmarksDeletionDialogState = DeletionDialogState.None,
                bookmarksSnackbarState = BookmarksSnackbarState.None,
                bookmarksAddFolderState = null,
                bookmarksEditBookmarkState = null,
                bookmarksSelectFolderState = null,
                bookmarksEditFolderState = null,
                bookmarksMultiselectMoveState = null,
                isLoading = false,
                isSearching = false,
            ),
        )
    }

    FirefoxTheme {
        BookmarksScreen(
            buildStore = store,
            appStore = AppStore(),
            browserStore = BrowserStore(),
            toolbarStore = BrowserToolbarStore(),
            searchStore = SearchFragmentStore(SearchFragmentState.EMPTY),
            bookmarksSearchEngine = null,
            profiler = null,
        )
    }
}

@Composable
@FlexibleWindowLightDarkPreview
private fun EmptyBookmarksScreenPreview() {
    val store = { _: NavHostController ->
        BookmarksStore(
            initialState = BookmarksState(
                bookmarkItems = listOf(),
                selectedItems = listOf(),
                rootMenuShown = false,
                showBookmarksImport = true,
                sortMenuShown = false,
                sortOrder = BookmarksListSortOrder.default,
                recursiveSelectedCount = null,
                currentFolder = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks",
                    position = null,
                ),
                isSignedIntoSync = false,
                openTabsConfirmationDialog = OpenTabsConfirmationDialog.None,
                bookmarksDeletionDialogState = DeletionDialogState.None,
                bookmarksSnackbarState = BookmarksSnackbarState.None,
                bookmarksAddFolderState = null,
                bookmarksEditBookmarkState = null,
                bookmarksSelectFolderState = null,
                bookmarksEditFolderState = null,
                bookmarksMultiselectMoveState = null,
                isLoading = false,
                isSearching = false,
            ),
        )
    }

    FirefoxTheme {
        BookmarksScreen(
            buildStore = store,
            appStore = AppStore(),
            browserStore = BrowserStore(),
            toolbarStore = BrowserToolbarStore(),
            searchStore = SearchFragmentStore(SearchFragmentState.EMPTY),
            bookmarksSearchEngine = null,
            profiler = null,
        )
    }
}

private const val PREVIEW_BOOKMARKS_SIZE = 20
