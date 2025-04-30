/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.library.bookmarks.ui

import android.content.ClipData
import android.content.ClipboardManager
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.concept.storage.BookmarkInfo
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import mozilla.components.lib.state.Store
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.utils.LastSavedFolderCache

private const val WARN_OPEN_ALL_SIZE = 15

/**
 * A middleware for handling side-effects in response to [BookmarksAction]s.
 *
 * @param bookmarksStorage Storage layer for reading and writing bookmarks.
 * @param clipboardManager For copying bookmark URLs.
 * @param addNewTabUseCase For opening tabs from menus.
 * @param getNavController Fetch the NavController for navigating within the local Composable nav graph.
 * @param exitBookmarks Invoked when back is clicked while the navController's backstack is empty.
 * @param wasPreviousAppDestinationHome Check whether the previous destination before entering bookmarks was home.
 * @param navigateToSearch Navigate to search.
 * @param navigateToSignIntoSync Invoked when handling [SignIntoSyncClicked].
 * @param shareBookmark Invoked when the share option is selected from a menu.
 * @param showTabsTray Invoked after opening tabs from menus.
 * @param resolveFolderTitle Invoked to lookup user-friendly bookmark titles.
 * @param showUrlCopiedSnackbar Invoked when a bookmark url is copied.
 * @param getBrowsingMode Invoked when retrieving the app's current [BrowsingMode].
 * @param openTab Invoked when opening a tab when a bookmark is clicked.
 * @param saveBookmarkSortOrder Invoked to persist the new sort order.
 * @param lastSavedFolderCache used to cache the last folder you edited a bookmark in.
 * @param ioDispatcher Coroutine dispatcher for IO operations.
 */
@Suppress("LongParameterList")
internal class BookmarksMiddleware(
    private val bookmarksStorage: BookmarksStorage,
    private val clipboardManager: ClipboardManager?,
    private val addNewTabUseCase: TabsUseCases.AddNewTabUseCase,
    private val getNavController: () -> NavController,
    private val exitBookmarks: () -> Unit,
    private val wasPreviousAppDestinationHome: () -> Boolean,
    private val navigateToSearch: () -> Unit,
    private val navigateToSignIntoSync: () -> Unit,
    private val shareBookmark: (url: String, title: String) -> Unit,
    private val showTabsTray: (isPrivateMode: Boolean) -> Unit,
    private val resolveFolderTitle: (BookmarkNode) -> String,
    private val showUrlCopiedSnackbar: () -> Unit,
    private val getBrowsingMode: () -> BrowsingMode,
    private val openTab: (url: String, openInNewTab: Boolean) -> Unit,
    private val saveBookmarkSortOrder: suspend (BookmarksListSortOrder) -> Unit,
    private val lastSavedFolderCache: LastSavedFolderCache,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : Middleware<BookmarksState, BookmarksAction> {

    private val scope = CoroutineScope(ioDispatcher)

    @Suppress("LongMethod", "ComplexMethod")
    override fun invoke(
        context: MiddlewareContext<BookmarksState, BookmarksAction>,
        next: (BookmarksAction) -> Unit,
        action: BookmarksAction,
    ) {
        val preReductionState = context.state
        next(action)

        val dialogState = context.state.bookmarksDeletionDialogState
        if (dialogState is DeletionDialogState.LoadingCount) {
            scope.launch {
                val count = bookmarksStorage.countBookmarksInTrees(dialogState.guidsToDelete)

                context.store.dispatch(DeletionDialogAction.CountLoaded(count.toInt()))
            }
        }

        when (action) {
            Init -> context.store.tryDispatchLoadFor(BookmarkRoot.Mobile.id)
            is InitEdit -> scope.launch {
                Result.runCatching {
                    val bookmarkNode = bookmarksStorage.getBookmark(action.guid)
                    val bookmark = bookmarkNode?.let {
                        BookmarkItem.Bookmark(it.url!!, it.title ?: "", it.url!!, it.guid)
                    }
                    val folder = bookmarkNode?.parentGuid
                        ?.let { bookmarksStorage.getBookmark(it) }
                        ?.let { BookmarkItem.Folder(guid = it.guid, title = resolveFolderTitle(it)) }

                    InitEditLoaded(bookmark = bookmark!!, folder = folder!!)
                }.getOrNull()?.also {
                    context.store.dispatch(it)
                }
            }
            is BookmarkClicked -> {
                if (preReductionState.selectedItems.isNotEmpty()) {
                    context.store.tryDispatchReceivedRecursiveCountUpdate()
                    return
                }
                val openInNewTab = wasPreviousAppDestinationHome() ||
                    getBrowsingMode() == BrowsingMode.Private
                openTab(action.item.url, openInNewTab)
            }

            is FolderClicked -> {
                if (preReductionState.selectedItems.isNotEmpty()) {
                    context.store.tryDispatchReceivedRecursiveCountUpdate()
                    return
                }
                context.store.tryDispatchLoadFor(action.item.guid)
            }
            is BookmarkLongClicked,
            is FolderLongClicked,
            -> {
                context.store.tryDispatchReceivedRecursiveCountUpdate()
            }
            SearchClicked -> navigateToSearch()
            AddFolderClicked -> getNavController().navigate(BookmarksDestinations.ADD_FOLDER)
            CloseClicked -> exitBookmarks()
            SignIntoSyncClicked -> navigateToSignIntoSync()
            is EditBookmarkClicked -> getNavController().navigate(BookmarksDestinations.EDIT_BOOKMARK)
            BackClicked -> {
                when {
                    // non-list screen cases need to come first, since we presume if all subscreen
                    // state is null then we are on the list screen
                    preReductionState.bookmarksAddFolderState != null &&
                        context.state.bookmarksAddFolderState == null -> {
                        scope.launch(ioDispatcher) {
                            val newFolderTitle =
                                preReductionState.bookmarksAddFolderState.folderBeingAddedTitle
                            if (newFolderTitle.isNotEmpty()) {
                                val guid = bookmarksStorage.addFolder(
                                    parentGuid = preReductionState.bookmarksAddFolderState.parent.guid,
                                    title = newFolderTitle,
                                )
                                val folder = BookmarkItem.Folder(
                                    guid = guid,
                                    title = newFolderTitle,
                                )

                                context.store.dispatch(AddFolderAction.FolderCreated(folder))

                                withContext(Dispatchers.Main) {
                                    if (preReductionState.bookmarksSelectFolderState != null) {
                                        getNavController().popBackStack(
                                            BookmarksDestinations.EDIT_BOOKMARK,
                                            inclusive = false,
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
                            context.store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
                        }
                    }

                    preReductionState.bookmarksSelectFolderState != null -> {
                        getNavController().popBackStack()
                        preReductionState.bookmarksMultiselectMoveState?.also {
                            if (it.destination == preReductionState.currentFolder.guid) {
                                return@also
                            }
                            scope.launch {
                                preReductionState.createMovePairs()?.forEach {
                                    bookmarksStorage.updateNode(it.first, it.second)
                                }
                                context.store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
                            }
                        }
                    }

                    preReductionState.bookmarksEditFolderState != null -> {
                        val editState = preReductionState.bookmarksEditFolderState
                        getNavController().popBackStack()
                        scope.launch(ioDispatcher) {
                            if (editState.folder.title.isNotEmpty()) {
                                preReductionState.createBookmarkInfo()?.also {
                                    bookmarksStorage.updateNode(editState.folder.guid, it)
                                }
                            }
                            context.store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
                        }
                    }

                    preReductionState.bookmarksEditBookmarkState != null -> {
                        if (!getNavController().popBackStack()) {
                            exitBookmarks()
                        }
                        scope.launch(ioDispatcher) {
                            val newBookmarkTitle = preReductionState.bookmarksEditBookmarkState.bookmark.title
                            if (newBookmarkTitle.isNotEmpty()) {
                                preReductionState.createBookmarkInfo()?.also {
                                    bookmarksStorage.updateNode(
                                        guid = preReductionState.bookmarksEditBookmarkState.bookmark.guid,
                                        info = it,
                                    )
                                    lastSavedFolderCache.setGuid(it.parentGuid)
                                }
                                context.store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
                            }
                        }
                    }
                    // list screen cases
                    preReductionState.selectedItems.isNotEmpty() -> { /* noop */ }
                    preReductionState.currentFolder.guid != BookmarkRoot.Mobile.id -> {
                        scope.launch {
                            val parentFolderGuid = withContext(ioDispatcher) {
                                bookmarksStorage
                                    .getBookmark(preReductionState.currentFolder.guid)
                                    ?.parentGuid ?: BookmarkRoot.Mobile.id
                            }
                            context.store.tryDispatchLoadFor(parentFolderGuid)
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

            SelectFolderAction.ViewAppeared -> context.store.tryDispatchLoadFolders()
            is BookmarksListMenuAction -> action.handleSideEffects(context.store, preReductionState)
            SnackbarAction.Dismissed -> when (preReductionState.bookmarksSnackbarState) {
                is BookmarksSnackbarState.UndoDeletion -> scope.launch {
                    preReductionState.bookmarksSnackbarState.guidsToDelete.forEach {
                        bookmarksStorage.deleteNode(it)
                    }
                }
                else -> {}
            }
            is DeletionDialogAction.DeleteTapped -> {
                scope.launch {
                    preReductionState.bookmarksDeletionDialogState.guidsToDelete.forEach {
                        bookmarksStorage.deleteNode(it)
                    }
                }

                if (preReductionState.bookmarksEditFolderState != null) {
                    getNavController().popBackStack()
                }
            }
            OpenTabsConfirmationDialogAction.ConfirmTapped -> scope.launch {
                val dialog = preReductionState.openTabsConfirmationDialog
                if (dialog is OpenTabsConfirmationDialog.Presenting) {
                    bookmarksStorage.getTree(dialog.guidToOpen)?.also {
                        it.children
                            ?.mapNotNull { it.url }
                            ?.forEach { url -> addNewTabUseCase(url = url, private = dialog.isPrivate) }
                        withContext(Dispatchers.Main) {
                            showTabsTray(dialog.isPrivate)
                        }
                    }
                }
            }
            is FirstSyncCompleted -> {
                context.store.tryDispatchLoadFor(preReductionState.currentFolder.guid)
            }
            ViewDisposed -> {
                preReductionState.bookmarksSnackbarState.let { snackState ->
                    if (snackState is BookmarksSnackbarState.UndoDeletion) {
                        scope.launch {
                            snackState.guidsToDelete.forEach {
                                bookmarksStorage.deleteNode(it)
                            }
                        }
                    }
                }
            }
            is InitEditLoaded,
            SnackbarAction.Undo,
            is OpenTabsConfirmationDialogAction.Present,
            OpenTabsConfirmationDialogAction.CancelTapped,
            DeletionDialogAction.CancelTapped,
            is RecursiveSelectionCountLoaded,
            is DeletionDialogAction.CountLoaded,
            is EditBookmarkAction.TitleChanged,
            is EditBookmarkAction.URLChanged,
            is BookmarksLoaded,
            is EditFolderAction.TitleChanged,
            is AddFolderAction.FolderCreated,
            is AddFolderAction.TitleChanged,
            is SelectFolderAction.FoldersLoaded,
            is SelectFolderAction.ItemClicked,
            EditFolderAction.DeleteClicked,
            is ReceivedSyncSignInUpdate,
            -> Unit
        }
    }

    private fun Store<BookmarksState, BookmarksAction>.tryDispatchLoadFolders() =
        scope.launch {
            val folders = if (bookmarksStorage.hasDesktopBookmarks()) {
                bookmarksStorage.getTree(BookmarkRoot.Root.id, recursive = true)?.let { rootNode ->
                    val excludingMobile =
                        rootNode.children?.filterNot { it.guid == BookmarkRoot.Mobile.id }
                    val desktopRoot = rootNode.copy(children = excludingMobile)
                    rootNode.children?.find { it.guid == BookmarkRoot.Mobile.id }?.let {
                        val newChildren = listOf(desktopRoot) + it.children.orEmpty()
                        it.copy(children = newChildren)
                    }?.let {
                        collectFolders(
                            node = it,
                            comparator = state.sortOrder.comparator,
                            shouldCollect = { node -> !state.isGuidBeingMoved(node.guid) },
                        )
                    }
                }
            } else {
                bookmarksStorage.getTree(BookmarkRoot.Mobile.id, recursive = true)
                    ?.let {
                        collectFolders(
                            node = it,
                            comparator = state.sortOrder.comparator,
                            shouldCollect = { node -> !state.isGuidBeingMoved(node.guid) },
                        )
                    }
            }

            folders?.also { dispatch(SelectFolderAction.FoldersLoaded(it)) }
        }

    private fun Store<BookmarksState, BookmarksAction>.tryDispatchLoadFor(guid: String) =
        scope.launch {
            bookmarksStorage.getTree(guid)?.let { rootNode ->
                val folder = BookmarkItem.Folder(
                    guid = guid,
                    title = resolveFolderTitle(rootNode),
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
                            val desktopNode = bookmarksStorage.getTree(BookmarkRoot.Root.id)?.let {
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

    private fun BookmarkNode.childItems(): List<BookmarkItem> = this.children
        ?.mapNotNull { node ->
            Result.runCatching {
                when (node.type) {
                    BookmarkNodeType.ITEM -> BookmarkItem.Bookmark(
                        url = node.url!!,
                        title = node.title ?: node.url ?: "",
                        previewImageUrl = node.url!!,
                        dateAdded = node.dateAdded,
                        guid = node.guid,
                    )

                    BookmarkNodeType.FOLDER -> BookmarkItem.Folder(
                        title = node.title ?: "",
                        dateAdded = node.dateAdded,
                        guid = node.guid,
                    )

                    BookmarkNodeType.SEPARATOR -> null
                }
            }.getOrNull()
        } ?: listOf()

    private fun collectFolders(
        node: BookmarkNode,
        comparator: Comparator<BookmarkItem>,
        indentation: Int = 0,
        shouldCollect: (BookmarkNode) -> Boolean = { _ -> true },
        folders: MutableList<SelectFolderItem> = mutableListOf(),
    ): List<SelectFolderItem> {
        if (node.type == BookmarkNodeType.FOLDER && shouldCollect(node)) {
            folders.add(
                SelectFolderItem(
                    indentation = indentation,
                    folder = BookmarkItem.Folder(
                        guid = node.guid,
                        title = resolveFolderTitle(node),
                    ),
                ),
            )

            val sortedChildren = node.childItems().folders().sortedWith(comparator)
            sortedChildren.forEach { child ->
                val childNode = node.children!!.first { it.guid == child.guid }
                val children = collectFolders(childNode, comparator, indentation + 1, shouldCollect)
                folders.addAll(children)
            }
        }

        return folders
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
                showUrlCopiedSnackbar()
            }

            is BookmarksListMenuAction.Bookmark.ShareClicked -> {
                shareBookmark(bookmark.url, bookmark.title)
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
                bookmarksStorage.getTree(folder.guid)?.also {
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
                bookmarksStorage.getTree(folder.guid)?.also {
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
                preReductionState.selectedItems
                    .mapNotNull { (it as? BookmarkItem.Bookmark)?.url }
                    .forEach { url -> addNewTabUseCase(url = url, private = false) }
                withContext(Dispatchers.Main) {
                    showTabsTray(false)
                }
            }

            BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked -> scope.launch {
                preReductionState.selectedItems
                    .mapNotNull { (it as? BookmarkItem.Bookmark)?.url }
                    .forEach { url -> addNewTabUseCase(url = url, private = true) }
                withContext(Dispatchers.Main) {
                    showTabsTray(true)
                }
            }

            BookmarksListMenuAction.MultiSelect.ShareClicked -> {
                preReductionState.selectedItems.filterIsInstance<BookmarkItem.Bookmark>()
                    .forEach { shareBookmark(it.url, it.title) }
            }
            is BookmarksListMenuAction.SortMenu -> scope.launch {
                saveBookmarkSortOrder(store.state.sortOrder)
            }
            is BookmarksListMenuAction.MultiSelect.DeleteClicked,
            is BookmarksListMenuAction.Folder.DeleteClicked,
            is BookmarksListMenuAction.Bookmark.DeleteClicked,
            -> { }
        }
    }
}

private suspend fun BookmarksStorage.hasDesktopBookmarks(): Boolean {
    return countBookmarksInTrees(
        listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id),
    ) > 0u
}

private fun BookmarksState.createMovePairs() = bookmarksMultiselectMoveState?.let { moveState ->
    moveState.guidsToMove.map { guid ->
        val bookmarkItem = bookmarkItems.first { it.guid == guid }
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
            position = bookmarkItems.indexOfFirst { it.guid == state.folder.guid }.toUInt(),
            title = state.folder.title,
            url = null,
        )
    }
    bookmarksEditBookmarkState != null -> bookmarksEditBookmarkState.let { state ->
        BookmarkInfo(
            parentGuid = state.folder.guid,
            position = bookmarkItems.indexOfFirst { it.guid == state.bookmark.guid }.toUInt(),
            title = state.bookmark.title,
            url = state.bookmark.url,
        )
    }
    else -> null
}
