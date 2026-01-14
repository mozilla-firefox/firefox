/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import android.content.ClipData
import android.content.ClipboardManager
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.storage.BookmarkInfo
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.utils.LastSavedFolderCache

private const val WARN_OPEN_ALL_SIZE = 15

/**
 * A middleware for handling side-effects in response to [BookmarksAction]s.
 *
 * @param bookmarksStorage Storage layer for reading and writing bookmarks.
 * @param clipboardManager For copying bookmark URLs.
 * @param addNewTabUseCase For opening tabs from menus.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] used for loading the bookmark URLs.
 * @param useNewSearchUX Whether to use the new integrated search UX or navigate to a separate search screen.
 * @param openBookmarksInNewTab Whether to load bookmark URLs in a new tab.
 * @param getNavController Fetch the NavController for navigating within the local Composable nav graph.
 * @param exitBookmarks Invoked when back is clicked while the navController's backstack is empty.
 * @param navigateToBrowser Invoked when handling [BookmarkClicked] to navigate to the browser.
 * @param navigateToSearch Navigate to search.
 * @param navigateToSignIntoSync Invoked when handling [SignIntoSyncClicked].
 * @param shareBookmarks Invoked when the share option is selected from a menu. Allows sharing of
 * one or more bookmarks
 * @param showTabsTray Invoked after opening tabs from menus.
 * @param resolveFolderTitle Invoked to lookup user-friendly bookmark titles.
 * @param getBrowsingMode Invoked when retrieving the app's current [BrowsingMode].
 * @param saveBookmarkSortOrder Invoked to persist the new sort order.
 * @param lastSavedFolderCache used to cache the last folder you edited a bookmark in.
 * @param reportResultGlobally Invoked when an error occurs that needs to be reported even if the
 * feature goes out of scope.
 * @param ioDispatcher Coroutine dispatcher for IO operations.
 * @param lifecycleScope lifecycle bound CoroutineScope scope used to cancel jobs when leaving bookmarks.
 */
@Suppress("LongParameterList", "LargeClass")
internal class BookmarksMiddleware(
    private val bookmarksStorage: BookmarksStorage,
    private val clipboardManager: ClipboardManager?,
    private val addNewTabUseCase: TabsUseCases.AddNewTabUseCase,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val useNewSearchUX: Boolean,
    private val openBookmarksInNewTab: Boolean,
    private val getNavController: () -> NavController,
    private val exitBookmarks: () -> Unit,
    private val navigateToBrowser: () -> Unit,
    private val navigateToSearch: () -> Unit,
    private val navigateToSignIntoSync: () -> Unit,
    private val shareBookmarks: (List<BookmarkItem.Bookmark>) -> Unit = {},
    private val showTabsTray: (isPrivateMode: Boolean) -> Unit,
    private val resolveFolderTitle: (BookmarkNode) -> String,
    private val getBrowsingMode: () -> BrowsingMode,
    private val saveBookmarkSortOrder: suspend (BookmarksListSortOrder) -> Unit,
    private val lastSavedFolderCache: LastSavedFolderCache,
    private val reportResultGlobally: (BookmarksGlobalResultReport) -> Unit,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    lifecycleScope: CoroutineScope,
) : Middleware<BookmarksState, BookmarksAction> {

    private val scope = CoroutineScope(lifecycleScope.coroutineContext + ioDispatcher)

    @Suppress("LongMethod", "CognitiveComplexMethod", "CyclomaticComplexMethod")
    override fun invoke(
        store: Store<BookmarksState, BookmarksAction>,
        next: (BookmarksAction) -> Unit,
        action: BookmarksAction,
    ) {
        val preReductionState = store.state
        next(action)

        val dialogState = store.state.bookmarksDeletionDialogState
        if (dialogState is DeletionDialogState.LoadingCount) {
            scope.launch {
                val count = bookmarksStorage.countBookmarksInTrees(dialogState.guidsToDelete)

                store.dispatch(DeletionDialogAction.CountLoaded(count.toInt()))
            }
        }

        when (action) {
            Init -> store.tryDispatchLoadFor(BookmarkRoot.Mobile.id)
            is InitEdit -> scope.launch {
                Result.runCatching {
                    val bookmarkNode = bookmarksStorage.getBookmark(action.guid).getOrNull()
                    val bookmark = bookmarkNode?.let {
                        BookmarkItem.Bookmark(it.url!!, it.title ?: "", it.url!!, it.guid, it.position)
                    }
                    val folder = bookmarkNode?.parentGuid
                        ?.let { bookmarksStorage.getBookmark(it).getOrNull() }
                        ?.let {
                            BookmarkItem.Folder(
                                guid = it.guid,
                                title = resolveFolderTitle(it),
                                position = it.position,
                            )
                        }

                    InitEditLoaded(bookmark = bookmark!!, folder = folder!!)
                }.getOrNull()?.also {
                    store.dispatch(it)
                }
            }
            is BookmarkClicked -> {
                if (preReductionState.selectedItems.isNotEmpty()) {
                    store.tryDispatchReceivedRecursiveCountUpdate()
                    return
                }

                fenixBrowserUseCases.loadUrlOrSearch(
                    searchTermOrURL = action.item.url,
                    newTab = openBookmarksInNewTab,
                    private = getBrowsingMode().isPrivate,
                    flags = EngineSession.LoadUrlFlags.select(
                        EngineSession.LoadUrlFlags.ALLOW_JAVASCRIPT_URL,
                    ),
                )
                navigateToBrowser()
            }

            is FolderClicked -> {
                if (preReductionState.selectedItems.isNotEmpty()) {
                    store.tryDispatchReceivedRecursiveCountUpdate()
                    return
                }
                store.tryDispatchLoadFor(action.item.guid)
            }
            is BookmarkLongClicked,
            is FolderLongClicked,
            -> {
                store.tryDispatchReceivedRecursiveCountUpdate()
            }
            SearchClicked -> if (!useNewSearchUX) {
                navigateToSearch()
            }
            AddFolderClicked -> getNavController().navigate(BookmarksDestinations.ADD_FOLDER)
            CloseClicked -> exitBookmarks()
            SignIntoSyncClicked -> navigateToSignIntoSync()
            is EditBookmarkClicked -> getNavController().navigate(BookmarksDestinations.EDIT_BOOKMARK)
            BackClicked -> {
                when {
                    // non-list screen cases need to come first, since we presume if all subscreen
                    // state is null then we are on the list screen
                    preReductionState.bookmarksAddFolderState != null &&
                        store.state.bookmarksAddFolderState == null -> {
                        scope.launch(ioDispatcher) {
                            val newFolderTitle =
                                preReductionState.bookmarksAddFolderState.folderBeingAddedTitle
                            val parentGuid = preReductionState.bookmarksAddFolderState.parent.guid
                            if (newFolderTitle.isNotEmpty()) {
                                val guid = bookmarksStorage.addFolder(
                                    parentGuid = parentGuid,
                                    title = newFolderTitle,
                                ).getOrElse {
                                    reportResultGlobally(BookmarksGlobalResultReport.AddFolderFailed)
                                    return@launch
                                }

                                val position = bookmarksStorage.getBookmark(guid).getOrNull()?.position
                                val folder = BookmarkItem.Folder(
                                    guid = guid,
                                    title = newFolderTitle,
                                    position = position,
                                )

                                store.dispatch(AddFolderAction.FolderCreated(folder))

                                // if we are in the middle of moving items, we consider the end of the
                                // add folder workflow to be terminal, and finish moving the items
                                // into the newly created folder
                                preReductionState.createMovePairs()?.forEach {
                                    val result = bookmarksStorage.updateNode(
                                        it.first,
                                        it.second.copy(parentGuid = guid),
                                    )
                                    if (result.isFailure) {
                                        reportResultGlobally(BookmarksGlobalResultReport.SelectFolderFailed)
                                    }
                                }

                                withContext(Dispatchers.Main) {
                                    if (preReductionState.bookmarksEditBookmarkState != null) {
                                        getNavController().popBackStack(
                                            BookmarksDestinations.EDIT_BOOKMARK,
                                            inclusive = false,
                                        )
                                    } else if (preReductionState.bookmarksSelectFolderState != null) {
                                        getNavController().popBackStack(
                                            BookmarksDestinations.LIST,
                                            false,
                                        )
                                    } else {
                                        getNavController().popBackStack()
                                    }
                                }
                            } else {
                                withContext(Dispatchers.Main) {
                                    getNavController().popBackStack()
                                }
                            }
                            store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
                        }
                    }

                    preReductionState.bookmarksSelectFolderState != null -> {
                        getNavController().popBackStack()
                        preReductionState.bookmarksMultiselectMoveState?.also {
                            if (it.destination == preReductionState.currentFolder.guid) {
                                return@also
                            }
                            scope.launch {
                                val successes = preReductionState.createMovePairs()
                                    ?.mapNotNull { item ->
                                        bookmarksStorage.updateNode(item.first, item.second)
                                            .takeIf { result ->
                                                result.isSuccess
                                            }
                                }
                                if (successes.isNullOrEmpty()) {
                                    store.dispatch(SnackbarAction.SelectFolderFailed)
                                }
                                store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
                            }
                        }
                    }

                    preReductionState.bookmarksEditFolderState != null -> {
                        val editState = preReductionState.bookmarksEditFolderState
                        getNavController().popBackStack()
                        scope.launch(ioDispatcher) {
                            preReductionState.createBookmarkInfo()?.also {
                                val result = bookmarksStorage.updateNode(editState.folder.guid, it)
                                if (result.isFailure) {
                                    reportResultGlobally(BookmarksGlobalResultReport.EditFolderFailed)
                                }
                            }
                            store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
                        }
                    }

                    preReductionState.bookmarksEditBookmarkState != null -> {
                        if (!getNavController().popBackStack()) {
                            exitBookmarks()
                        }
                        scope.launch(ioDispatcher) {
                            preReductionState.createBookmarkInfo()?.also {
                                val result = bookmarksStorage.updateNode(
                                    guid = preReductionState.bookmarksEditBookmarkState.bookmark.guid,
                                    info = it,
                                )
                                if (result.isFailure) {
                                    reportResultGlobally(BookmarksGlobalResultReport.EditBookmarkFailed)
                                } else {
                                    if (preReductionState.bookmarksEditBookmarkState.edited) {
                                        lastSavedFolderCache.setGuid(it.parentGuid)
                                    }
                                }
                            }
                            store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
                        }
                    }
                    // list screen cases
                    preReductionState.selectedItems.isNotEmpty() -> { /* noop */ }
                    // User is clicking back before we've loaded anything
                    preReductionState.currentFolder.guid.isEmpty() -> {
                        exitBookmarks()
                    }
                    preReductionState.currentFolder.guid != BookmarkRoot.Mobile.id -> {
                        scope.launch {
                            val parentFolderGuid = withContext(ioDispatcher) {
                                bookmarksStorage
                                    .getBookmark(preReductionState.currentFolder.guid)
                                    .getOrNull()
                                    ?.parentGuid ?: BookmarkRoot.Mobile.id
                            }
                            store.tryDispatchLoadFor(parentFolderGuid)
                        }
                    }

                    else -> {
                        if (!getNavController().popBackStack()) {
                            exitBookmarks()
                        }
                    }
                }
            }

            EditBookmarkAction.FolderClicked -> {
                getNavController().navigate(BookmarksDestinations.SELECT_FOLDER)
            }

            EditBookmarkAction.DeleteClicked -> {
                // 💡When we're in the browser -> edit flow, we back out to the browser bypassing our
                // snackbar logic. So we have to also do the delete here.
                if (!getNavController().popBackStack()) {
                    scope.launch {
                        preReductionState.bookmarksEditBookmarkState?.also {
                            bookmarksStorage.deleteNode(it.bookmark.guid)
                        }

                        withContext(Dispatchers.Main) {
                            exitBookmarks()
                        }
                    }
                }
            }
            EditFolderAction.ParentFolderClicked,
            AddFolderAction.ParentFolderClicked,
            -> {
                getNavController().navigate(BookmarksDestinations.SELECT_FOLDER)
            }
            SelectFolderAction.ViewAppeared -> {
                if (preReductionState.bookmarksSelectFolderState?.folders.isNullOrEmpty()) {
                    store.tryDispatchLoadSelectableFolders()
                }
            }
            is SelectFolderAction.ChevronClicked -> {
                if (action.folder.expansionState is SelectFolderExpansionState.Closed) {
                    store.tryDispatchAdditionalSelectableFolders(action.folder)
                }
            }
            is BookmarksListMenuAction -> action.handleSideEffects(store, preReductionState)
            SnackbarAction.Dismissed -> when (preReductionState.bookmarksSnackbarState) {
                is BookmarksSnackbarState.UndoDeletion -> scope.launch {
                    if (preReductionState.bookmarksDeletionSnackbarQueueCount <= 1) {
                        preReductionState.bookmarksSnackbarState.guidsToDelete.forEach {
                            bookmarksStorage.deleteNode(it)
                        }
                        lastSavedFolderCache.getGuid()?.let {
                            if (bookmarksStorage.getBookmark(it).getOrNull() == null) {
                                lastSavedFolderCache.setGuid(null)
                            }
                        }
                    }
                }
                else -> {}
            }
            is DeletionDialogAction.DeleteTapped -> {
                scope.launch {
                    preReductionState.bookmarksDeletionDialogState.guidsToDelete.forEach {
                        bookmarksStorage.deleteNode(it)
                    }
                    lastSavedFolderCache.getGuid()?.let {
                        if (bookmarksStorage.getBookmark(it).getOrNull() == null) {
                            lastSavedFolderCache.setGuid(null)
                        }
                    }
                }

                if (preReductionState.bookmarksEditFolderState != null) {
                    getNavController().popBackStack()
                }
            }
            OpenTabsConfirmationDialogAction.ConfirmTapped -> scope.launch {
                val dialog = preReductionState.openTabsConfirmationDialog
                if (dialog is OpenTabsConfirmationDialog.Presenting) {
                    bookmarksStorage.getTree(dialog.guidToOpen).getOrNull()?.also {
                        it.children
                            ?.mapNotNull { it.url }
                            ?.forEach { url ->
                                addNewTabUseCase(
                                    url = url,
                                    private = dialog.isPrivate,
                                )
                            }
                        withContext(Dispatchers.Main) {
                            showTabsTray(dialog.isPrivate)
                        }
                    }
                }
            }
            is FirstSyncCompleted -> {
                store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
            }
            ViewDisposed -> {
                preReductionState.bookmarksSnackbarState.let { snackState ->
                    if (snackState is BookmarksSnackbarState.UndoDeletion) {
                        scope.launch {
                            snackState.guidsToDelete.forEach {
                                bookmarksStorage.deleteNode(it)
                            }
                            lastSavedFolderCache.getGuid()?.let {
                                if (bookmarksStorage.getBookmark(it).getOrNull() == null) {
                                    lastSavedFolderCache.setGuid(null)
                                }
                            }
                        }
                    }
                }
            }
            is SelectFolderAction.SortMenu -> scope.launch {
                store.tryDispatchLoadSelectableFolders()
                saveBookmarkSortOrder(store.state.sortOrder)
            }
            is SelectFolderAction.SearchQueryUpdated -> {
                scope.launch {
                    val state = store.state.bookmarksSelectFolderState
                    val filteredFolders = state?.folders
                        ?.filter {
                            it.title.startsWith(
                                state.searchQuery,
                                ignoreCase = true,
                            )
                        }
                    filteredFolders?.let {
                        store.dispatch(SelectFolderAction.FilteredFoldersLoaded(it))
                    }
                }
            }
            SelectFolderAction.SearchClicked,
            SelectFolderAction.SearchDismissed,
            is InitEditLoaded,
            SnackbarAction.SelectFolderFailed,
            SnackbarAction.Undo,
            is OpenTabsConfirmationDialogAction.Present,
            OpenTabsConfirmationDialogAction.CancelTapped,
            DeletionDialogAction.CancelTapped,
            is RecursiveSelectionCountLoaded,
            is DeletionDialogAction.CountLoaded,
            is EditBookmarkAction.TitleChanged,
            is EditBookmarkAction.URLChanged,
            is BookmarksLoaded,
            is SearchDismissed,
            is EditFolderAction.TitleChanged,
            is AddFolderAction.FolderCreated,
            is AddFolderAction.TitleChanged,
            is SelectFolderAction.FoldersLoaded,
            is SelectFolderAction.FilteredFoldersLoaded,
            is SelectFolderAction.ExpandedFolderLoaded,
            is SelectFolderAction.ItemClicked,
            EditFolderAction.DeleteClicked,
            is ReceivedSyncSignInUpdate,
            PrivateBrowsingAuthorized,
            -> Unit
        }
    }

    private fun Store<BookmarksState, BookmarksAction>.tryDispatchLoadSelectableFolders() =
        scope.launch {
            Result.runCatching {
                if (!bookmarksStorage.hasDesktopBookmarks()) {
                    listOf(
                        loadAsSelectableFolder(guid = BookmarkRoot.Mobile.id, indentation = 0, false)!!,
                    )
                } else {
                    val rootNode = bookmarksStorage.getTree(BookmarkRoot.Root.id).getOrNull()!!
                    val (mobileRootNodes, desktopRootNodes) =
                        rootNode.children!!.partition { it.guid == BookmarkRoot.Mobile.id }
                    // there should only be one of these
                    val mobileNode = mobileRootNodes.first()

                    // we want to order these a specific way on mobile
                    (listOf(mobileNode, rootNode) + desktopRootNodes).mapNotNull { item ->
                        loadAsSelectableFolder(guid = item.guid, indentation = 0, false)
                    }
                }
            }.onSuccess { folders ->
                dispatch(SelectFolderAction.FoldersLoaded(folders))
            }
        }

    private fun Store<BookmarksState, BookmarksAction>.tryDispatchAdditionalSelectableFolders(
        folder: SelectFolderItem,
    ) = scope.launch {
            loadAsSelectableFolder(folder.guid, folder.indentation, true)?.let {
                dispatch(SelectFolderAction.ExpandedFolderLoaded(it))
            }
        }

    /**
     * Load a guid and optionally its immediate children as select folder items.
     */
    private suspend fun loadAsSelectableFolder(
        guid: String,
        indentation: Int,
        shouldOpen: Boolean,
    ): SelectFolderItem? = Result.runCatching {
        val loadedNode = bookmarksStorage.getTree(guid).getOrNull()!!
        if (loadedNode.type != BookmarkNodeType.FOLDER) return null
        SelectFolderItem(
            indentation = indentation,
            folder = BookmarkItem.Folder(
                title = resolveFolderTitle(loadedNode),
                guid = loadedNode.guid,
                position = loadedNode.position,
            ),
            expansionState = when {
                // when we are expanding folders, we need to find all their children that could also be selected
                shouldOpen -> SelectFolderExpansionState.Open(
                    children = loadedNode.children.orEmpty().mapNotNull { node ->
                        loadAsSelectableFolder(node.guid, indentation + 1, false)
                    },
                )
                // only mark folders as expandable if they have children that could potentially be selected
                (loadedNode.children?.any { it.type == BookmarkNodeType.FOLDER } == true) -> {
                    SelectFolderExpansionState.Closed
                }
                else -> SelectFolderExpansionState.None
            },
        )
    }.getOrNull()

    private fun Store<BookmarksState, BookmarksAction>.tryDispatchLoadFor(guid: String) =
        scope.launch {
            bookmarksStorage.getTree(guid).getOrNull()?.let { rootNode ->
                ensureActive()

                val folder = BookmarkItem.Folder(
                    guid = guid,
                    title = resolveFolderTitle(rootNode),
                    position = rootNode.position,
                )

                val items = when (guid) {
                    BookmarkRoot.Root.id -> {
                        rootNode.copy(
                            children = rootNode.children
                                ?.filterNot { it.guid == BookmarkRoot.Mobile.id }
                                ?.map { it.copy(title = resolveFolderTitle(it)) },
                        ).childItems()
                    }
                    BookmarkRoot.Mobile.id -> {
                        if (bookmarksStorage.hasDesktopBookmarks()) {
                            val desktopNode = bookmarksStorage.getTree(BookmarkRoot.Root.id).getOrNull()?.let {
                                it.copy(title = resolveFolderTitle(it))
                            }

                            val mobileRoot = rootNode.copy(
                                children = listOfNotNull(desktopNode) + rootNode.children.orEmpty(),
                            )
                            mobileRoot.childItems()
                        } else {
                            rootNode.childItems()
                        }
                    }
                    else -> rootNode.childItems()
                }

                dispatch(
                    BookmarksLoaded(
                        folder = folder,
                        bookmarkItems = items,
                    ),
                )
            }
        }

    private fun Store<BookmarksState, BookmarksAction>.tryDispatchReceivedRecursiveCountUpdate() {
        scope.launch {
            val count = bookmarksStorage.countBookmarksInTrees(state.selectedItems.map { it.guid })
            dispatch(RecursiveSelectionCountLoaded(count.toInt()))
        }
    }

    private suspend fun BookmarkNode.childItems(): List<BookmarkItem> = this.children
        ?.mapNotNull { node ->
            Result.runCatching {
                when (node.type) {
                    BookmarkNodeType.ITEM -> BookmarkItem.Bookmark(
                        url = node.url!!,
                        title = node.title ?: node.url ?: "",
                        previewImageUrl = node.url!!,
                        dateAdded = node.dateAdded,
                        guid = node.guid,
                        position = node.position,
                    )

                    BookmarkNodeType.FOLDER -> BookmarkItem.Folder(
                        title = node.title ?: "",
                        dateAdded = node.dateAdded,
                        guid = node.guid,
                        position = node.position,
                        nestedItemCount = bookmarksStorage.countBookmarksInTrees(listOf(node.guid)).toInt(),
                    )

                    BookmarkNodeType.SEPARATOR -> null
                }
            }.getOrNull()
        } ?: listOf()

    private suspend fun openSelectedInTabs(
        preReductionState: BookmarksState,
        isPrivate: Boolean,
    ) {
        preReductionState.selectedItems.forEach { item ->
            when (item) {
                is BookmarkItem.Bookmark -> {
                    addNewTabUseCase(item.url, private = isPrivate)
                }
                is BookmarkItem.Folder -> {
                    bookmarksStorage
                        .getTree(
                            guid = item.guid,
                            recursive = true,
                        ).getOrNull()
                        ?.collectUrlsRecursive()
                        ?.forEach {
                            addNewTabUseCase(url = it, private = isPrivate)
                        }
                }
            }
        }
    }

    private fun BookmarkNode.collectUrlsRecursive(): List<String> {
        val urls = mutableListOf<String>()

        this.children?.forEach { node ->
            when (node.type) {
                BookmarkNodeType.ITEM -> node.url?.let { value -> urls.add(value) }
                BookmarkNodeType.FOLDER -> {
                    urls.addAll(node.collectUrlsRecursive())
                }
                BookmarkNodeType.SEPARATOR -> Unit
            }
        }

        return urls
    }

    @Suppress("LongMethod")
    private fun BookmarksListMenuAction.handleSideEffects(
        store: Store<BookmarksState, BookmarksAction>,
        preReductionState: BookmarksState,
    ) {
        when (this) {
            // bookmark menu actions
            is BookmarksListMenuAction.Bookmark.EditClicked -> {
                getNavController().navigate(BookmarksDestinations.EDIT_BOOKMARK)
            }

            is BookmarksListMenuAction.Bookmark.CopyClicked -> {
                val urlClipData = ClipData.newPlainText(bookmark.url, bookmark.url)
                clipboardManager?.setPrimaryClip(urlClipData)
            }

            is BookmarksListMenuAction.Bookmark.ShareClicked -> {
                shareBookmarks(listOf(bookmark))
            }

            is BookmarksListMenuAction.Bookmark.OpenInNormalTabClicked -> {
                // Bug 1919949 — Add undo snackbar to delete action.
                addNewTabUseCase(url = bookmark.url, private = false)
                showTabsTray(false)
            }

            is BookmarksListMenuAction.Bookmark.OpenInPrivateTabClicked -> {
                addNewTabUseCase(url = bookmark.url, private = true)
                showTabsTray(true)
            }

            // folder menu actions
            is BookmarksListMenuAction.Folder.EditClicked -> {
                getNavController().navigate(BookmarksDestinations.EDIT_FOLDER)
            }

            is BookmarksListMenuAction.Folder.OpenAllInNormalTabClicked -> scope.launch {
                bookmarksStorage.getTree(folder.guid).getOrNull()?.also {
                    val count = it.children?.count() ?: 0
                    if (count >= WARN_OPEN_ALL_SIZE) {
                        store.dispatch(OpenTabsConfirmationDialogAction.Present(folder.guid, count, false))
                        return@also
                    }
                    it.children
                        ?.mapNotNull { it.url }
                        ?.forEach { url -> addNewTabUseCase(url = url, private = false) }
                    withContext(Dispatchers.Main) {
                        showTabsTray(false)
                    }
                }
            }

            is BookmarksListMenuAction.Folder.OpenAllInPrivateTabClicked -> scope.launch {
                bookmarksStorage.getTree(folder.guid).getOrNull()?.also {
                    val count = it.children?.count() ?: 0
                    if (count >= WARN_OPEN_ALL_SIZE) {
                        store.dispatch(OpenTabsConfirmationDialogAction.Present(folder.guid, count, true))
                        return@also
                    }
                    it.children
                        ?.mapNotNull { it.url }
                        ?.forEach { url -> addNewTabUseCase(url = url, private = true) }
                    withContext(Dispatchers.Main) {
                        showTabsTray(true)
                    }
                }
            }

            // top bar menu actions
            BookmarksListMenuAction.MultiSelect.EditClicked -> {
                getNavController().navigate(BookmarksDestinations.EDIT_BOOKMARK)
            }

            BookmarksListMenuAction.MultiSelect.MoveClicked -> {
                getNavController().navigate(BookmarksDestinations.SELECT_FOLDER)
            }

            BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked -> scope.launch {
                openSelectedInTabs(preReductionState, isPrivate = false)
                withContext(Dispatchers.Main) {
                    showTabsTray(false)
                }
            }

            BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked -> scope.launch {
                openSelectedInTabs(preReductionState, isPrivate = true)
                withContext(Dispatchers.Main) {
                    showTabsTray(true)
                }
            }

            BookmarksListMenuAction.MultiSelect.ShareClicked -> {
                val selectedItems = preReductionState.selectedItems.filterIsInstance<BookmarkItem.Bookmark>()
                shareBookmarks(selectedItems)
            }
            is BookmarksListMenuAction.SortMenu -> scope.launch {
                saveBookmarkSortOrder(store.state.sortOrder)
            }
            is BookmarksListMenuAction.SelectAll -> store.tryDispatchReceivedRecursiveCountUpdate()
            is BookmarksListMenuAction.MultiSelect.DeleteClicked,
            is BookmarksListMenuAction.Folder.DeleteClicked,
            is BookmarksListMenuAction.Bookmark.DeleteClicked,
            is BookmarksListMenuAction.Folder.SelectClicked,
            is BookmarksListMenuAction.Bookmark.SelectClicked,
            -> { }
        }
    }
}

private suspend fun BookmarksStorage.hasDesktopBookmarks(): Boolean {
    return countBookmarksInTrees(
        listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id),
    ) > 0u
}

private fun BookmarksState.createMovePairs(): List<Pair<String, BookmarkInfo>>? {
    val moveState = bookmarksMultiselectMoveState ?: return null

    return moveState.guidsToMove.mapNotNull { guid ->
        val bookmarkItem = bookmarkItems.firstOrNull { it.guid == guid } ?: return@mapNotNull null

        guid to BookmarkInfo(
            moveState.destination,
            // Setting position to 'null' is treated as a 'move to the end' by the storage API.
            null,
            bookmarkItem.title,
            if (bookmarkItem is BookmarkItem.Bookmark) bookmarkItem.url else null,
        )
    }
}

private fun BookmarksState.createBookmarkInfo() = when {
    bookmarksEditFolderState != null -> bookmarksEditFolderState.let { state ->
        BookmarkInfo(
            parentGuid = state.parent.guid,
            position = bookmarkItems.firstOrNull { it.guid == state.folder.guid }?.position,
            title = state.folder.title.ifEmpty {
                bookmarkItems.firstOrNull { it.guid == state.folder.guid }?.title
            },
            url = null,
        )
    }
    bookmarksEditBookmarkState != null -> bookmarksEditBookmarkState.let { state ->
        BookmarkInfo(
            parentGuid = state.folder.guid,
            position = bookmarkItems.firstOrNull { it.guid == state.bookmark.guid }?.position,
            title = state.bookmark.title.ifEmpty {
                bookmarkItems.firstOrNull { it.guid == state.bookmark.guid }?.title
            },
            url = state.bookmark.url,
        )
    }
    else -> null
}
