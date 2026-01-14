/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import android.content.ClipboardManager
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavController
import androidx.navigation.NavDestination
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.storage.BookmarkInfo
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.any
import mozilla.components.support.test.eq
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyList
import org.mockito.ArgumentMatchers.anyString
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.utils.LastSavedFolderCache

@RunWith(AndroidJUnit4::class)
class BookmarksMiddlewareTest {

    @get:Rule
    val coroutineRule = MainCoroutineRule()

    private val testDispatcher = StandardTestDispatcher()

    private lateinit var bookmarksStorage: BookmarksStorage
    private lateinit var clipboardManager: ClipboardManager
    private lateinit var addNewTabUseCase: TabsUseCases.AddNewTabUseCase
    private lateinit var fenixBrowserUseCases: FenixBrowserUseCases
    private lateinit var navController: NavController
    private lateinit var exitBookmarks: () -> Unit
    private lateinit var navigateToBrowser: () -> Unit
    private lateinit var navigateToSearch: () -> Unit
    private lateinit var navigateToSignIntoSync: () -> Unit
    private lateinit var shareBookmarks: (List<BookmarkItem.Bookmark>) -> Unit
    private lateinit var showTabsTray: (Boolean) -> Unit
    private lateinit var getBrowsingMode: () -> BrowsingMode
    private lateinit var lastSavedFolderCache: LastSavedFolderCache
    private lateinit var saveSortOrder: suspend (BookmarksListSortOrder) -> Unit
    private val resolveFolderTitle = { node: BookmarkNode ->
        friendlyRootTitle(
            mock(),
            node,
            true,
            rootTitles = mapOf(
                "root" to "Bookmarks",
                "mobile" to "Bookmarks",
                "menu" to "Bookmarks Menu",
                "toolbar" to "Bookmarks Toolbar",
                "unfiled" to "Other Bookmarks",
            ),
        ) ?: "Bookmarks"
    }

    @Before
    fun setup() {
        bookmarksStorage = mock()
        clipboardManager = mock()
        addNewTabUseCase = mock()
        fenixBrowserUseCases = mock()
        navController = mock()
        exitBookmarks = { }
        navigateToBrowser = { }
        navigateToSearch = { }
        navigateToSignIntoSync = { }
        shareBookmarks = { }
        showTabsTray = { _ -> }
        getBrowsingMode = { BrowsingMode.Normal }
        lastSavedFolderCache = mock()
        saveSortOrder = { }
    }

    @Test
    fun `GIVEN a nested bookmark structure WHEN the store is initialized on create THEN all the folders have nested child counts`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Mobile.id))).thenReturn(30u)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf("folder guid 0"))).thenReturn(8u)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf("folder guid 1"))).thenReturn(7u)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf("folder guid 2"))).thenReturn(3u)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf("folder guid 3"))).thenReturn(9u)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf("folder guid 4"))).thenReturn(1u)

        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        val expectedMap =
            mapOf(
                "folder guid 0" to 8,
                "folder guid 1" to 7,
                "folder guid 2" to 3,
                "folder guid 3" to 9,
                "folder guid 4" to 1,
            )
        assertEquals(expectedMap, store.state.bookmarkItems.folders().associate { it.guid to it.nestedItemCount })
    }

    @Test
    fun `GIVEN a nested bookmark structure WHEN SelectAll is clicked THEN all bookmarks are selected and reflected in state`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()
        `when`(bookmarksStorage.countBookmarksInTrees(store.state.bookmarkItems.map { it.guid })).thenReturn(35u)
        store.dispatch(BookmarksListMenuAction.SelectAll)
        assertEquals(store.state.selectedItems.size, store.state.bookmarkItems.size)
        assertEquals(35, store.state.recursiveSelectedCount)
    }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync WHEN store is initialized THEN bookmarks will be loaded as display format`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(0u)
        val middleware = buildMiddleware()

        val store = middleware.makeStore()

        assertEquals(10, store.state.bookmarkItems.size)
    }

    @Test
    fun `GIVEN bookmarks in storage and navigating directly to the edit screen WHEN store is initialized THEN bookmark to edit will be loaded`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        val parent = generateBookmark("item guid 1", null, "https://mozilla.org", position = 0u)
        val child = generateBookmark("item guid 2", null, "https://mozilla.org", position = 0u).copy(parentGuid = "item guid 1")
        `when`(bookmarksStorage.getBookmark("item guid 1")).thenReturn(Result.success(parent))
        `when`(bookmarksStorage.getBookmark("item guid 2")).thenReturn(Result.success(child))
        val middleware = buildMiddleware()

        val store = middleware.makeStore(bookmarkToLoad = "item guid 2")
        val bookmark = BookmarkItem.Bookmark(
            url = "https://mozilla.org",
            title = "",
            previewImageUrl = "https://mozilla.org",
            guid = "item guid 2",
            position = 0u,
        )
        assertEquals(bookmark, store.state.bookmarksEditBookmarkState?.bookmark)
    }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync WHEN store is initialized THEN bookmarks will be sorted by last modified date`() = runTestOnMain {
        val reverseOrderByModifiedBookmarks = List(5) {
            generateBookmark(
                guid = "$it",
                title = "$it",
                url = "$it",
                position = it.toUInt(),
                lastModified = it.toLong(),
            )
        }
        val root = BookmarkNode(
            type = BookmarkNodeType.FOLDER,
            guid = BookmarkRoot.Mobile.id,
            parentGuid = null,
            position = 0U,
            title = "mobile",
            url = null,
            dateAdded = 0,
            lastModified = 0,
            children = reverseOrderByModifiedBookmarks,
        )
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(root))
        val middleware = buildMiddleware()

        val store = middleware.makeStore()
        assertEquals(5, store.state.bookmarkItems.size)
    }

    @Test
    fun `GIVEN a bookmarks store WHEN SortMenuItem is clicked THEN Save the new sort order`() = runTestOnMain {
        val root = BookmarkNode(
            type = BookmarkNodeType.FOLDER,
            guid = BookmarkRoot.Mobile.id,
            parentGuid = null,
            position = 0U,
            title = "mobile",
            url = null,
            dateAdded = 0,
            lastModified = 0,
            children = listOf(),
        )
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(root))
        var newSortOrder = BookmarksListSortOrder.default
        saveSortOrder = {
            newSortOrder = it
        }
        val middleware = buildMiddleware()

        val store = middleware.makeStore()
        store.dispatch(BookmarksListMenuAction.SortMenu.NewestClicked)
        assertEquals(BookmarksListSortOrder.Created(true), newSortOrder)
    }

    @Test
    fun `GIVEN SelectFolderScreen WHEN SortMenuItem is clicked THEN Save the new sort order`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        var newSortOrder = BookmarksListSortOrder.default
        saveSortOrder = {
            newSortOrder = it
        }
        val bookmark = tree.children?.last { it.type == BookmarkNodeType.ITEM }!!
        val bookmarkItem = BookmarkItem.Bookmark(
            title = bookmark.title!!,
            guid = bookmark.guid,
            url = bookmark.url!!,
            previewImageUrl = "",
            position = bookmark.position,
            dateAdded = bookmark.dateAdded,
        )
        val middleware = buildMiddleware()

        val store = middleware.makeStore()
        store.dispatch(EditBookmarkClicked(bookmarkItem))
        store.dispatch(EditBookmarkAction.FolderClicked)
        store.dispatch(SelectFolderAction.ViewAppeared)
        assertEquals(false, store.state.sortMenuShown)

        store.dispatch(SelectFolderAction.SortMenu.SortMenuButtonClicked)

        assertEquals(true, store.state.sortMenuShown)

        store.dispatch(SelectFolderAction.SortMenu.NewestClicked)
        assertEquals(BookmarksListSortOrder.Created(true), newSortOrder)
    }

    @Test
    fun `GIVEN bookmarks in storage and user has a desktop bookmark WHEN store is initialized THEN bookmarks, including desktop will be loaded as display format`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(1u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Root.id)).thenReturn(Result.success(generateDesktopRootTree()))
        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(1u)
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(isSignedIntoSync = true),
        )

        assertEquals(11, store.state.bookmarkItems.size)
    }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync but has existing desktop bookmarks WHEN store is initialized THEN bookmarks, including desktop will be loaded as display format`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(1u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Root.id)).thenReturn(Result.success(generateDesktopRootTree()))
        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(1u)
        val store = middleware.makeStore()

        assertEquals(11, store.state.bookmarkItems.size)
    }

    @Test
    fun `GIVEN no bookmarks under mobile root WHEN store is initialized THEN list of bookmarks will be empty`() = runTestOnMain {
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(null)
        val middleware = buildMiddleware()

        val store = middleware.makeStore()

        assertEquals(0, store.state.bookmarkItems.size)
    }

    @Test
    fun `GIVEN bookmarks should be open in a new tab WHEN a bookmark is clicked THEN open it as a new tab`() {
        val url = "url"
        val bookmarkItem = BookmarkItem.Bookmark(url, "title", url, guid = "", position = null)
        val openBookmarksInNewTab = true
        getBrowsingMode = { BrowsingMode.Normal }
        var navigated = false
        navigateToBrowser = { navigated = true }

        val middleware = buildMiddleware(openBookmarksInNewTab = openBookmarksInNewTab)
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarkItems = listOf(bookmarkItem),
            ),
        )

        store.dispatch(BookmarkClicked(bookmarkItem))

        verify(fenixBrowserUseCases).loadUrlOrSearch(
            searchTermOrURL = url,
            newTab = openBookmarksInNewTab,
            private = false,
            flags = EngineSession.LoadUrlFlags.select(
                EngineSession.LoadUrlFlags.ALLOW_JAVASCRIPT_URL,
            ),
        )
        assertTrue(navigated)
    }

    @Test
    fun `GIVEN bookmarks should not be open in a new tab WHEN a bookmark is clicked THEN open it in the existing tab`() {
        val url = "url"
        val bookmarkItem = BookmarkItem.Bookmark(url, "title", url, guid = "", position = null)
        val openBookmarksInNewTab = false
        getBrowsingMode = { BrowsingMode.Normal }
        var navigated = false
        navigateToBrowser = { navigated = true }

        val middleware = buildMiddleware(openBookmarksInNewTab = openBookmarksInNewTab)
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarkItems = listOf(bookmarkItem),
            ),
        )

        store.dispatch(BookmarkClicked(bookmarkItem))

        verify(fenixBrowserUseCases).loadUrlOrSearch(
            searchTermOrURL = url,
            newTab = openBookmarksInNewTab,
            private = false,
            flags = EngineSession.LoadUrlFlags.select(
                EngineSession.LoadUrlFlags.ALLOW_JAVASCRIPT_URL,
            ),
        )
        assertTrue(navigated)
    }

    @Test
    fun `WHEN folder is clicked THEN children are loaded and screen title is updated to folder title`() = runTestOnMain {
        val bookmarkTree = generateBookmarkTree()
        val folderNode = bookmarkTree.children!!.first { it.type == BookmarkNodeType.FOLDER }
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.getTree(folderNode.guid))
            .thenReturn(Result.success(generateBookmarkFolder(folderNode.guid, folderNode.title!!, BookmarkRoot.Mobile.id, folderNode.position!!)))

        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default,
        )
        store.dispatch(
            FolderClicked(
                BookmarkItem.Folder(
                    folderNode.title!!,
                    folderNode.guid,
                    position = folderNode.position!!,
                ),
            ),
        )

        assertEquals(folderNode.title, store.state.currentFolder.title)
        assertEquals(5, store.state.bookmarkItems.size)
    }

    @Test
    fun `WHEN search button is clicked THEN navigate to search`() {
        var navigated = false
        navigateToSearch = { navigated = true }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(SearchClicked)

        assertTrue(navigated)
    }

    @Test
    fun `GIVEN new search UX is used WHEN search button is clicked THEN don't navigate to search`() {
        var navigated = false
        navigateToSearch = { navigated = true }
        val middleware = buildMiddleware(useNewSearchUX = true)
        val captorMiddleware = CaptureActionsMiddleware<BookmarksState, BookmarksAction>()
        val store = BookmarksStore(
            initialState = BookmarksState.default,
            middleware = listOf(middleware, captorMiddleware),
        )

        store.dispatch(SearchClicked)

        assertFalse(navigated)
    }

    @Test
    fun `WHEN add folder button is clicked THEN navigate to folder screen`() {
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(AddFolderClicked)

        verify(navController).navigate(BookmarksDestinations.ADD_FOLDER)
    }

    @Test
    fun `GIVEN current screen is add folder WHEN parent folder is clicked THEN navigate to folder selection screen`() {
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(AddFolderAction.ParentFolderClicked)

        verify(navController).navigate(BookmarksDestinations.SELECT_FOLDER)
    }

    @Test
    fun `GIVEN current screen is edit bookmark WHEN folder is clicked THEN navigate to folder selection screen`() {
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(EditBookmarkAction.FolderClicked)

        verify(navController).navigate(BookmarksDestinations.SELECT_FOLDER)
    }

    @Test
    fun `GIVEN current screen is add folder and new folder title is nonempty WHEN back is clicked THEN navigate back, save the new folder, and load the updated tree`() = runTest {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.addFolder(BookmarkRoot.Mobile.id, "test")).thenReturn(Result.success("new-guid"))

        val middleware = buildMiddleware()
        val store = middleware.makeStore()
        val newFolderTitle = "test"

        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.TitleChanged(newFolderTitle))

        assertNotNull(store.state.bookmarksAddFolderState)

        store.dispatch(BackClicked)

        verify(bookmarksStorage).addFolder(store.state.currentFolder.guid, title = newFolderTitle)
        verify(bookmarksStorage, times(2)).getTree(BookmarkRoot.Mobile.id)
        verify(navController).popBackStack()
        assertNull(store.state.bookmarksAddFolderState)
    }

    @OptIn(ExperimentalCoroutinesApi::class) // advanceUntilIdle
    @Test
    fun `GIVEN current screen is add folder and new folder title is empty WHEN back is clicked THEN navigate back to the previous tree and don't save anything`() = runTestOnMain {
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.TitleChanged("test"))
        store.dispatch(AddFolderAction.TitleChanged(""))
        assertNotNull(store.state.bookmarksAddFolderState)

        store.dispatch(BackClicked)
        this.advanceUntilIdle()

        verify(bookmarksStorage, never()).addFolder(parentGuid = store.state.currentFolder.guid, title = "")
        verify(navController).popBackStack()
        assertNull(store.state.bookmarksAddFolderState)
    }

    @Test
    fun `GIVEN current screen is add folder and previous screen is select folder WHEN back is clicked THEN skip selection screen straight back to the edit bookmark screen`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id, recursive = false)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.addFolder(BookmarkRoot.Mobile.id, "i'm a new folder")).thenReturn(Result.success("new-guid"))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        val bookmark = store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
        val newFolderTitle = "i'm a new folder"

        store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmark))
        store.dispatch(EditBookmarkAction.FolderClicked)
        store.dispatch(SelectFolderAction.ViewAppeared)
        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.TitleChanged(newFolderTitle))
        store.dispatch(BackClicked)

        assertNull(store.state.bookmarksSelectFolderState)
        verify(bookmarksStorage, times(3)).getTree(BookmarkRoot.Mobile.id, recursive = false)
        verify(navController, times(1)).popBackStack(BookmarksDestinations.EDIT_BOOKMARK, inclusive = false)
    }

    @Test
    fun `GIVEN current screen is add folder and previous screen is not select folder WHEN back is clicked THEN navigate back`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id, recursive = false)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.addFolder(BookmarkRoot.Mobile.id, "i'm a new folder")).thenReturn(Result.success("new-guid"))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id, recursive = false)).thenReturn(Result.success(generateBookmarkTree()))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.TitleChanged("i'm a new folder"))
        store.dispatch(BackClicked)

        verify(bookmarksStorage, times(2)).getTree(BookmarkRoot.Mobile.id, recursive = false)
        verify(navController, times(1)).popBackStack()
    }

    @Test
    fun `GIVEN current screen is edit folder and new title is nonempty WHEN back is clicked THEN navigate back, save the folder, and load the updated tree`() = runTest {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(0u)
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksEditFolderState = BookmarksEditFolderState(
                    parent = BookmarkItem.Folder("Bookmarks", "guid0", 0u),
                    folder = BookmarkItem.Folder("folder title 0", "folder guid 0", 0u),
                ),
            ),
        )

        val newFolderTitle = "test"

        store.dispatch(EditFolderAction.TitleChanged(newFolderTitle))
        store.dispatch(BackClicked)

        verify(bookmarksStorage).updateNode(
            guid = "folder guid 0",
            info = BookmarkInfo(
                parentGuid = "guid0",
                position = 0u,
                title = "test",
                url = null,
            ),
        )
        verify(bookmarksStorage, times(2)).getTree(BookmarkRoot.Mobile.id)
        verify(navController).popBackStack()
        assertNull(store.state.bookmarksEditFolderState)
    }

    @Test
    fun `GIVEN current screen is edit folder and new title is empty WHEN back is clicked THEN navigate back, without siving the folder, and load the updated tree`() = runTest {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksEditFolderState = BookmarksEditFolderState(
                    parent = BookmarkItem.Folder("Bookmarks", "guid0", 0u),
                    folder = BookmarkItem.Folder("folder title 0", "folder guid 0", 0u),
                ),
            ),
        )

        val newFolderTitle = ""

        store.dispatch(EditFolderAction.TitleChanged(newFolderTitle))
        store.dispatch(BackClicked)

        verify(bookmarksStorage, never()).updateNode(
            guid = "folder guid 0",
            info = BookmarkInfo(
                parentGuid = "guid0",
                position = 0u,
                title = "test",
                url = null,
            ),
        )
        verify(bookmarksStorage, times(2)).getTree(BookmarkRoot.Mobile.id)
        verify(navController).popBackStack()
        assertNull(store.state.bookmarksEditFolderState)
    }

    @Test
    fun `GIVEN current screen is edit folder and new title is empty WHEN the folder location is changed and back is clicked THEN navigate back saves the valid changes and load the updated tree`() = runTest {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(0u)
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksEditFolderState = BookmarksEditFolderState(
                    parent = BookmarkItem.Folder("Bookmarks", "guid0", 0u),
                    folder = BookmarkItem.Folder("folder title 0", "folder guid 0", 0u),
                ),
            ),
        )

        val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
        val newParentItem = BookmarkItem.Folder(title = newParent.title!!, guid = newParent.guid, position = newParent.position)
        val newFolderTitle = ""

        store.dispatch(EditFolderAction.TitleChanged(newFolderTitle))
        store.dispatch(EditFolderAction.ParentFolderClicked)
        store.dispatch(SelectFolderAction.ViewAppeared)
        store.dispatch(SelectFolderAction.ItemClicked(SelectFolderItem(0, newParentItem, SelectFolderExpansionState.None)))
        store.dispatch(BackClicked)
        store.dispatch(BackClicked)

        verify(bookmarksStorage).updateNode(
            guid = "folder guid 0",
            info = BookmarkInfo(
                parentGuid = newParentItem.guid,
                position = 0u,
                title = "folder title 0",
                url = null,
            ),
        )
        verify(bookmarksStorage, times(3)).getTree(BookmarkRoot.Mobile.id)
        assertNull(store.state.bookmarksEditFolderState)
    }

    @Test
    fun `GIVEN current screen is edit bookmark WHEN back is clicked THEN navigate back, save the bookmark, and load the updated tree`() = runTest {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()
        val newBookmarkTitle = "my awesome bookmark"

        val bookmark = store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
        store.dispatch(EditBookmarkClicked(bookmark = bookmark))
        store.dispatch(EditBookmarkAction.TitleChanged(title = newBookmarkTitle))

        assertNotNull(store.state.bookmarksEditBookmarkState)
        store.dispatch(BackClicked)

        val expectedPosition = bookmark.position!!
        verify(bookmarksStorage).updateNode(
            guid = "item guid 0",
            info = BookmarkInfo(
                parentGuid = BookmarkRoot.Mobile.id,
                position = expectedPosition,
                title = "my awesome bookmark",
                url = "item url 0",
            ),
        )
        verify(lastSavedFolderCache).setGuid(BookmarkRoot.Mobile.id)
        verify(bookmarksStorage, times(2)).getTree(BookmarkRoot.Mobile.id)
        verify(navController).popBackStack()
        assertNull(store.state.bookmarksEditBookmarkState)
    }

    @Test
    fun `GIVEN current screen is edit bookmark and the bookmark title is empty WHEN back is clicked THEN navigate back`() = runTest {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()
        val newBookmarkTitle = ""

        val bookmark = store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
        store.dispatch(EditBookmarkClicked(bookmark = bookmark))
        store.dispatch(EditBookmarkAction.TitleChanged(title = newBookmarkTitle))

        assertNotNull(store.state.bookmarksEditBookmarkState)
        store.dispatch(BackClicked)

        verify(bookmarksStorage, never()).updateNode(
            guid = "item guid 0",
            info = BookmarkInfo(
                parentGuid = BookmarkRoot.Mobile.id,
                position = 5u,
                title = "",
                url = "item url 0",
            ),
        )

        verify(navController).popBackStack()
        assertNull(store.state.bookmarksEditBookmarkState)
    }

    @Test
    fun `GIVEN current screen is edit bookmark WHEN the title is set to empty and the url is changed and back is clicked THEN the url saves but the title does not`() = runTest {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()
        val newBookmarkTitle = ""

        val bookmark = store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
        val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
        val newParentItem = BookmarkItem.Folder(title = newParent.title!!, guid = newParent.guid, position = newParent.position)
        store.dispatch(EditBookmarkClicked(bookmark = bookmark))
        store.dispatch(EditBookmarkAction.TitleChanged(title = newBookmarkTitle))
        store.dispatch(EditBookmarkAction.FolderClicked)
        store.dispatch(SelectFolderAction.ItemClicked(SelectFolderItem(0, newParentItem, SelectFolderExpansionState.None)))
        store.dispatch(BackClicked)

        assertNotNull(store.state.bookmarksEditBookmarkState)
        store.dispatch(BackClicked)

        verify(bookmarksStorage).updateNode(
            guid = "item guid 0",
            info = BookmarkInfo(
                parentGuid = newParentItem.guid,
                position = 5u,
                title = bookmark.title,
                url = "item url 0",
            ),
        )

        assertNull(store.state.bookmarksEditBookmarkState)
    }

    @Test
    fun `GIVEN current screen is list and the top-level is loaded WHEN back is clicked THEN exit bookmarks`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        var exited = false
        exitBookmarks = { exited = true }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BackClicked)

        assertTrue(exited)
    }

    @Test
    fun `GIVEN current screen is list and a bookmark is selected WHEN back is clicked THEN clear out selected item`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        var exited = false
        exitBookmarks = { exited = true }
        val middleware = buildMiddleware()
        val item = BookmarkItem.Bookmark("ur", "title", "url", "guid", 0u)
        val parent = BookmarkItem.Folder("title", "guid", 0u)
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarkItems = listOf(item),
                selectedItems = listOf(item),
                currentFolder = parent,
            ),
        )

        store.dispatch(BackClicked)
        assertTrue(store.state.selectedItems.isEmpty())
        assertFalse(exited)
    }

    @Test
    fun `GIVEN current screen is an empty list and the top-level is loaded WHEN sign into sync is clicked THEN navigate to sign into sync `() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        var navigated = false
        navigateToSignIntoSync = { navigated = true }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(SignIntoSyncClicked)

        assertTrue(navigated)
    }

    @Test
    fun `GIVEN current screen is a subfolder WHEN close is clicked THEN exit bookmarks `() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        var navigated = false
        exitBookmarks = { navigated = true }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(CloseClicked)

        assertTrue(navigated)
    }

    @Test
    fun `GIVEN current screen is list and a sub-level folder is loaded WHEN back is clicked THEN load the parent level`() = runTestOnMain {
        val tree = generateBookmarkTree()
        val firstFolderNode = tree.children!!.first { it.type == BookmarkNodeType.FOLDER }
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        `when`(bookmarksStorage.getTree(firstFolderNode.guid)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.getBookmark(firstFolderNode.guid)).thenReturn(Result.success(firstFolderNode))
        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(0u)
        val store = middleware.makeStore()

        store.dispatch(
            FolderClicked(
                BookmarkItem.Folder(
                    title = firstFolderNode.title!!,
                    guid = firstFolderNode.guid,
                    firstFolderNode.position,
                ),
            ),
        )

        assertEquals(firstFolderNode.guid, store.state.currentFolder.guid)
        store.dispatch(BackClicked)

        assertEquals(BookmarkRoot.Mobile.id, store.state.currentFolder.guid)
        assertEquals(tree.children!!.size, store.state.bookmarkItems.size)
    }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync WHEN select folder sub screen view is loaded THEN only mobile root is loaded onto screen`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id, recursive = false)).thenReturn(Result.success(generateBookmarkTree()))
        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(0u)
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
            ),
        )

        store.dispatch(SelectFolderAction.ViewAppeared)

        assertEquals(1, store.state.bookmarksSelectFolderState?.folders?.count())
    }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync WHEN expanding mobile root on select folder screen THEN mobile root children are shown as well`() = runTestOnMain {
        val tree = generateBookmarkTree()
        tree.children?.forEach {
            if (it.type == BookmarkNodeType.FOLDER) {
                `when`(bookmarksStorage.getTree(it.guid, false)).thenReturn(Result.success(generateBookmarkFolder(it.guid, "title", BookmarkRoot.Mobile.id, 0u)))
            }
        }
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id, false)).thenReturn(Result.success(tree))
        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(0u)
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
            ),
        )

        store.dispatch(SelectFolderAction.ViewAppeared)
        store.dispatch(
            SelectFolderAction.ChevronClicked(
                SelectFolderItem(
            indentation = 0,
            folder = BookmarkItem.Folder(
                guid = BookmarkRoot.Mobile.id,
                title = "",
                position = 0u,
            ),
            expansionState = SelectFolderExpansionState.Closed,
        ),
            ),
        )

        assertEquals(6, store.state.bookmarksSelectFolderState?.folders?.flattenToList()?.count())
    }

    @Test
    fun `GIVEN a folder with subfolders WHEN select folder sub screen view is loaded THEN load folders into sub screen state without the selected folder`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        val rootNode = generateBookmarkFolder("parent", "first", BookmarkRoot.Mobile.id, position = 0u).copy(
            children = generateBookmarkFolders("parent"),
        )
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id, recursive = true)).thenReturn(Result.success(rootNode))
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
                bookmarksEditFolderState = BookmarksEditFolderState(
                    parent = BookmarkItem.Folder("Bookmarks", "guid0", 0u),
                    folder = BookmarkItem.Folder("first", "parent", 0u),
                ),
            ),
        )
        `when`(bookmarksStorage.countBookmarksInTrees(anyList())).thenReturn(0u)
        store.dispatch(BookmarksListMenuAction.SelectAll)
        val bookmarkCount = store.state.selectedItems.size
        store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked)
        assertEquals(bookmarkCount, store.state.bookmarksSelectFolderState?.folders?.count())
    }

    @OptIn(ExperimentalCoroutinesApi::class) // advanceUntilIdle
    @Test
    fun `GIVEN selected folder WHEN OpenInNormalTabsClicked THEN urls opened as normal tabs`() = runTest {
        val bookmarkFolder = BookmarkItem.Folder(title = "folder", guid = "1234", position = 0u)
        val bookmarkItem = BookmarkItem.Bookmark(url = "url", title = "title", previewImageUrl = "string", guid = "guid", position = bookmarkFolder.position)

        var trayShown = false
        showTabsTray = { trayShown = true }
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState =
            BookmarksState.default.copy(
                bookmarkItems = listOf(bookmarkFolder, bookmarkItem),
                selectedItems = listOf(bookmarkFolder, bookmarkItem),
            ),
        )

        store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked)
        advanceUntilIdle()
        verify(addNewTabUseCase).invoke(url = "url", private = false)
        assertTrue(trayShown)
    }

    @OptIn(ExperimentalCoroutinesApi::class) // advanceUntilIdle
    @Test
    fun `GIVEN selected folder WHEN OpenInPrivateTabsClicked THEN urls opened as private tabs`() = runTest {
        val bookmarkFolder = BookmarkItem.Folder(title = "folder", guid = "1234", position = 0u)
        val bookmarkItem = BookmarkItem.Bookmark(url = "url", title = "title", previewImageUrl = "string", guid = "guid", position = bookmarkFolder.position)

        var trayShown = false
        showTabsTray = { trayShown = true }
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState =
            BookmarksState.default.copy(
                bookmarkItems = listOf(bookmarkFolder, bookmarkItem),
                selectedItems = listOf(bookmarkFolder, bookmarkItem),
            ),
        )

        store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked)
        advanceUntilIdle()
        verify(addNewTabUseCase).invoke(url = "url", private = true)
        assertTrue(trayShown)
    }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync but have pre-existing desktop bookmarks saved WHEN select folder sub screen view is loaded THEN load folders, including desktop folders into sub screen state`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(1u)
        mockDesktopFoldersForSelectScreen()
        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(1u)
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
            ),
        )

        store.dispatch(SelectFolderAction.ViewAppeared)

        assertEquals(4, store.state.bookmarksSelectFolderState?.folders?.count())
    }

    @Test
    fun `GIVEN bookmarks in storage and has desktop bookmarks WHEN select folder sub screen view is loaded THEN load folders, including desktop folders into sub screen state`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(1u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Root.id, recursive = false)).thenReturn(Result.success(generateDesktopRootTree()))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id, recursive = false)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Menu.id, recursive = false)).thenReturn(Result.success(generateBookmarkFolder(BookmarkRoot.Menu.id, "Menu", BookmarkRoot.Root.id, position = 0u)))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Toolbar.id, recursive = false)).thenReturn(Result.success(generateBookmarkFolder(BookmarkRoot.Toolbar.id, "Toolbar", BookmarkRoot.Root.id, position = 1u)))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Unfiled.id, recursive = false)).thenReturn(Result.success(generateBookmarkFolder(BookmarkRoot.Unfiled.id, "Unfiled", BookmarkRoot.Root.id, position = 2u)))

        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                isSignedIntoSync = true,
                bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
            ),
        )

        store.dispatch(SelectFolderAction.ViewAppeared)

        assertEquals(5, store.state.bookmarksSelectFolderState?.folders?.count())
    }

    @Test
    fun `GIVEN bookmarks in storage and has desktop bookmarks WHEN desktop folder is expanded THEN then load its children`() = runTestOnMain {
        val childFolderGuid = "child folder"
        val childFolder = generateBookmarkFolder(childFolderGuid, "title", BookmarkRoot.Toolbar.id, 5u)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(1u)
        mockDesktopFoldersForSelectScreen()
        `when`(bookmarksStorage.getTree(BookmarkRoot.Toolbar.id, recursive = false))
            .thenReturn(
                Result.success(
                generateBookmarkFolder(BookmarkRoot.Toolbar.id, "Toolbar", BookmarkRoot.Root.id, position = 1u)
                    .let { it.copy(children = it.children!! + listOf(childFolder)) },
            ),
        )
        `when`(bookmarksStorage.getTree("child folder", recursive = false)).thenReturn(Result.success(childFolder))

        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                isSignedIntoSync = true,
                bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
            ),
        )

        store.dispatch(SelectFolderAction.ViewAppeared)
        store.dispatch(
            SelectFolderAction.ChevronClicked(
                SelectFolderItem(
            indentation = 0,
            folder = BookmarkItem.Folder(
                guid = BookmarkRoot.Toolbar.id,
                title = "",
                position = 0u,
            ),
            expansionState = SelectFolderExpansionState.Closed,
        ),
            ),
        )

        assertEquals(5, store.state.bookmarksSelectFolderState?.folders?.count())
        assertEquals(6, store.state.bookmarksSelectFolderState?.folders?.flattenToList()?.count())
    }

    @Test
    fun `GIVEN current screen select folder WHEN back is clicked THEN pop the backstack`() = runTestOnMain {
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
            ),
        )

        store.dispatch(BackClicked)
        verify(navController).popBackStack()
    }

    @Test
    fun `GIVEN current screen select folder while multi-selecting WHEN back is clicked THEN pop the backstack and update the selected bookmark items`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksMultiselectMoveState = MultiselectMoveState(
                    guidsToMove = listOf("item guid 1", "item guid 2"),
                    destination = "folder guid 1",
                ),
                bookmarksSelectFolderState = BookmarksSelectFolderState(
                    outerSelectionGuid = "folder guid 1",
                ),
            ),
        )

        store.dispatch(BackClicked)

        verify(bookmarksStorage, times(2)).getTree(BookmarkRoot.Mobile.id)
        verify(navController).popBackStack()
        verify(bookmarksStorage).updateNode(
            guid = "item guid 1",
            info = BookmarkInfo(
                parentGuid = "folder guid 1",
                position = null,
                title = "item title 1",
                url = "item url 1",
            ),
        )
        verify(bookmarksStorage).updateNode(
            guid = "item guid 2",
            info = BookmarkInfo(
                parentGuid = "folder guid 1",
                position = null,
                title = "item title 2",
                url = "item url 2",
            ),
        )
    }

    @Test
    fun `GIVEN current screen select folder WHEN the search query is updated THEN FilteredFoldersLoaded gets dispatched with the filtered folders`() = runTestOnMain {
        val folders = listOf(
            SelectFolderItem(
                0,
                BookmarkItem.Folder("Bookmarks", "guid0", null),
                SelectFolderExpansionState.Open(
                    listOf(
                        SelectFolderItem(
                            1,
                            BookmarkItem.Folder("Nested One", "guid0", null),
                            SelectFolderExpansionState.Open(
                                listOf(
                                    SelectFolderItem(
                                        2,
                                        BookmarkItem.Folder("Nested Two", "guid0", null),
                                        SelectFolderExpansionState.None,
                                    ),
                                    SelectFolderItem(
                                        2,
                                        BookmarkItem.Folder("Nested Two", "guid0", null),
                                        SelectFolderExpansionState.None,
                                    ),
                                ),
                            ),
                        ),
                        SelectFolderItem(
                            1,
                            BookmarkItem.Folder("Nested One", "guid0", null),
                            SelectFolderExpansionState.Open(
                                listOf(
                                    SelectFolderItem(
                                        2,
                                        BookmarkItem.Folder("Nested Two", "guid1", null),
                                        SelectFolderExpansionState.Open(
                                            listOf(
                                                SelectFolderItem(
                                                    3,
                                                    BookmarkItem.Folder("Nested Three", "guid0", null),
                                                    SelectFolderExpansionState.None,
                                                ),
                                            ),
                                        ),
                                    ),
                                ),
                            ),
                        ),
                    ),
                ),
            ),
            SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "guid0", null), SelectFolderExpansionState.None),
        )

        val bookmarksFolder = folders.first()

        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksSelectFolderState = BookmarksSelectFolderState(
                    outerSelectionGuid = "selection guid",
                    isSearching = true,
                    folders = folders,
                    filteredFolders = folders,
                ),
            ),
        )

        val searchQueryNew = "bookmarks"

        store.dispatch(SelectFolderAction.SearchQueryUpdated(searchQueryNew))
        assertEquals(listOf(bookmarksFolder), store.state.bookmarksSelectFolderState?.filteredFolders)
    }

    @Test
    fun `WHEN edit clicked in bookmark item menu THEN nav to edit screen`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        val bookmark = store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
        store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmark))

        assertEquals(bookmark, store.state.bookmarksEditBookmarkState!!.bookmark)
        verify(navController).navigate(BookmarksDestinations.EDIT_BOOKMARK)
    }

    @Test
    fun `WHEN copy clicked in bookmark item menu THEN copy bookmark url to clipboard`() {
        val url = "url"
        val bookmarkItem = BookmarkItem.Bookmark(url = url, title = "title", previewImageUrl = url, guid = "guid", position = null)
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Bookmark.CopyClicked(bookmarkItem))

        verify(clipboardManager).setPrimaryClip(any())
    }

    @Test
    fun `WHEN share clicked in bookmark item menu THEN share the bookmark`() {
        var sharedBookmarks: List<BookmarkItem.Bookmark> = emptyList()
        shareBookmarks = { shareData ->
            sharedBookmarks = shareData
        }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()
        val url = "url"
        val title = "title"
        val bookmarkItem = BookmarkItem.Bookmark(url = url, title = title, previewImageUrl = url, guid = "guid", position = null)

        store.dispatch(BookmarksListMenuAction.Bookmark.ShareClicked(bookmarkItem))

        assertTrue(
            "Expected only one bookmark is shared. Got ${sharedBookmarks.size} instead",
            sharedBookmarks.size == 1,
        )
        assertEquals(url, sharedBookmarks.first().url)
        assertEquals(title, sharedBookmarks.first().title)
    }

    @Test
    fun `WHEN open in normal tab clicked in bookmark item menu THEN add a normal tab and show the tabs tray in normal mode`() {
        val url = "url"
        val bookmarkItem = BookmarkItem.Bookmark(url = url, title = "title", previewImageUrl = url, guid = "guid", position = null)
        var trayShown = false
        var mode = true
        showTabsTray = { newMode ->
            mode = newMode
            trayShown = true
        }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Bookmark.OpenInNormalTabClicked(bookmarkItem))

        verify(addNewTabUseCase).invoke(url = url, private = false)
        assertTrue(trayShown)
        assertFalse(mode)
    }

    @Test
    fun `WHEN open in private tab clicked in bookmark item menu THEN add a private tab and show the tabs tray in private mode`() {
        val url = "url"
        val bookmarkItem = BookmarkItem.Bookmark(url = url, title = "title", previewImageUrl = url, guid = "guid", position = null)
        var trayShown = false
        var mode = false
        showTabsTray = { newMode ->
            mode = newMode
            trayShown = true
        }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Bookmark.OpenInPrivateTabClicked(bookmarkItem))

        verify(addNewTabUseCase).invoke(url = url, private = true)
        assertTrue(trayShown)
        assertTrue(mode)
    }

    @Test
    fun `GIVEN a state with an undo snackbar WHEN snackbar is dismissed THEN delete all of the guids`() = runTestOnMain {
        val tree = generateBookmarkTree()
        val firstGuid = tree.children!!.first().guid
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val bookmarkItem = BookmarkItem.Bookmark(url = "url", title = "title", previewImageUrl = "url", guid = firstGuid, position = 0u)
        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(0u)
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Bookmark.DeleteClicked(bookmarkItem))
        assertEquals(BookmarksSnackbarState.UndoDeletion(listOf(firstGuid)), store.state.bookmarksSnackbarState)

        val initialCount = store.state.bookmarkItems.size
        store.dispatch(SnackbarAction.Dismissed)
        assertEquals(BookmarksSnackbarState.None, store.state.bookmarksSnackbarState)
        assertEquals(initialCount - 1, store.state.bookmarkItems.size)

        verify(bookmarksStorage).deleteNode(firstGuid)
    }

    @Test
    fun `GIVEN we are in the select folder screen and we are moving multiple bookmarks and bookmark items is empty WHEN the folder is selected and back button is clicked THEN we show a snackbar describing the error`() = runTestOnMain {
        val tree = generateBookmarkTree()
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarkItems = listOf(),
                bookmarksSelectFolderState = BookmarksSelectFolderState(
                    outerSelectionGuid = tree.guid,
                    innerSelectionGuid = tree.children!!.first { it.type == BookmarkNodeType.FOLDER }.guid,
                ),
                bookmarksMultiselectMoveState = MultiselectMoveState(
                    guidsToMove = listOf("item guid 1", "item guid 2"),
                    destination = "Some other location",
                ),
            ),
        )

        assertEquals(BookmarksSnackbarState.None, store.state.bookmarksSnackbarState)

        store.dispatch(BackClicked)

        assertEquals(BookmarksSnackbarState.SelectFolderFailed, store.state.bookmarksSnackbarState)
    }

    @Test
    fun `GIVEN a user is on the edit screen with nothing on the backstack WHEN delete is clicked THEN pop the backstack, delete the bookmark and exit bookmarks`() = runTestOnMain {
        var exited = false
        exitBookmarks = { exited = true }
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                    bookmark = BookmarkItem.Bookmark("ur", "title", "url", "guid", position = 0u),
                    folder = BookmarkItem.Folder("title", "guid", position = 0u),
                ),
            ),
        )
        `when`(navController.popBackStack()).thenReturn(false)
        store.dispatch(EditBookmarkAction.DeleteClicked)
        verify(navController).popBackStack()
        verify(bookmarksStorage).deleteNode("guid")
        assertTrue(exited)
    }

    @Test
    fun `WHEN edit clicked in folder item menu THEN nav to the edit screen`() {
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Folder.EditClicked(folder = store.state.currentFolder))

        verify(navController).navigate(BookmarksDestinations.EDIT_FOLDER)
    }

    @Test
    fun `GIVEN a folder with fewer than 15 items WHEN open all in normal tabs clicked in folder item menu THEN open all the bookmarks as normal tabs and show the tabs tray in normal mode`() = runTestOnMain {
        val guid = "guid"
        val folderItem = BookmarkItem.Folder(title = "title", guid = guid, position = 0u)
        val folder = generateBookmarkFolder(guid = guid, "title", "parentGuid", position = 0u)
        `when`(bookmarksStorage.getTree(guid)).thenReturn(Result.success(folder))
        var trayShown = false
        var mode = true
        showTabsTray = { newMode ->
            mode = newMode
            trayShown = true
        }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Folder.OpenAllInNormalTabClicked(folderItem))

        folder.children!!.forEach { child ->
            verify(addNewTabUseCase).invoke(url = child.url!!, private = false)
        }
        assertTrue(trayShown)
        assertFalse(mode)
    }

    @Test
    fun `GIVEN a folder with 15 or more items WHEN open all in normal tabs clicked in folder item menu THEN show a warning`() = runTestOnMain {
        val guid = "guid"
        val folderItem = BookmarkItem.Folder(title = "title", guid = guid, position = 0u)
        val folder = generateBookmarkFolder(guid = guid, "title", "parentGuid", position = 0u).copy(
            children = List(15) {
                generateBookmark(
                    guid = "bookmark guid $it",
                    title = "bookmark title $it",
                    url = "bookmark urk",
                    position = it.toUInt(),
                )
            },
        )
        `when`(bookmarksStorage.getTree(guid)).thenReturn(Result.success(folder))
        var trayShown = false
        showTabsTray = { _ -> trayShown = true }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Folder.OpenAllInNormalTabClicked(folderItem))
        val expected = OpenTabsConfirmationDialog.Presenting(
            guidToOpen = guid,
            numberOfTabs = 15,
            isPrivate = false,
        )
        assertEquals(expected, store.state.openTabsConfirmationDialog)

        folder.children!!.forEach { child ->
            verify(addNewTabUseCase, never()).invoke(url = child.url!!, private = false)
        }
        assertFalse(trayShown)
    }

    @Test
    fun `GIVEN a folder with fewer than 15 items WHEN open all in private tabs clicked in folder item menu THEN open all the bookmarks as private tabs and show the tabs tray in private mode`() = runTestOnMain {
        val guid = "guid"
        val folderItem = BookmarkItem.Folder(title = "title", guid = guid, position = 0u)
        val folder = generateBookmarkFolder(guid = guid, "title", "parentGuid", position = 0u)
        `when`(bookmarksStorage.getTree(guid)).thenReturn(Result.success(folder))
        var trayShown = false
        var mode = false
        showTabsTray = { newMode ->
            mode = newMode
            trayShown = true
        }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Folder.OpenAllInPrivateTabClicked(folderItem))

        folder.children!!.forEach { child ->
            verify(addNewTabUseCase).invoke(url = child.url!!, private = true)
        }
        assertTrue(trayShown)
        assertTrue(mode)
    }

    @Test
    fun `GIVEN a folder with 15 or more items WHEN open all in private tabs clicked in folder item menu THEN show a warning`() = runTestOnMain {
        val guid = "guid"
        val folderItem = BookmarkItem.Folder(title = "title", guid = guid, position = 0u)
        val folder = generateBookmarkFolder(guid = guid, "title", "parentGuid", position = 0u).copy(
            children = List(15) {
                generateBookmark(
                    guid = "bookmark guid $it",
                    title = "bookmark title $it",
                    url = "bookmark urk",
                    position = it.toUInt(),
                )
            },
        )
        `when`(bookmarksStorage.getTree(guid)).thenReturn(Result.success(folder))
        var trayShown = false
        showTabsTray = { _ -> trayShown = true }
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Folder.OpenAllInPrivateTabClicked(folderItem))
        val expected = OpenTabsConfirmationDialog.Presenting(
            guidToOpen = guid,
            numberOfTabs = 15,
            isPrivate = true,
        )
        assertEquals(expected, store.state.openTabsConfirmationDialog)

        folder.children!!.forEach { child ->
            verify(addNewTabUseCase, never()).invoke(url = child.url!!, private = true)
        }
        assertFalse(trayShown)
    }

    @Test
    fun `WHEN delete clicked in folder item menu THEN present a dialog showing the number of items to be deleted and when delete clicked, delete the selected folder`() = runTestOnMain {
        val tree = generateBookmarkTree()
        val folder = tree.children!!.first { it.type == BookmarkNodeType.FOLDER }
        val folderItem = BookmarkItem.Folder(guid = folder.guid, title = "title", position = folder.position)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(folderItem.guid))).thenReturn(19u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Folder.DeleteClicked(folderItem))
        assertEquals(DeletionDialogState.Presenting(listOf(folderItem.guid), 19), store.state.bookmarksDeletionDialogState)

        store.dispatch(DeletionDialogAction.DeleteTapped)
        assertEquals(DeletionDialogState.None, store.state.bookmarksDeletionDialogState)
        verify(bookmarksStorage).deleteNode(folder.guid)
    }

    @Test
    fun `WHEN delete clicked in folder edit screen THEN present a dialog showing the number of items to be deleted and when delete clicked, delete the selected folder`() = runTestOnMain {
        val tree = generateBookmarkTree()
        val folder = tree.children!!.first { it.type == BookmarkNodeType.FOLDER }
        val folderItem = BookmarkItem.Folder(guid = folder.guid, title = "title", position = folder.position)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(folderItem.guid))).thenReturn(19u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Folder.EditClicked(folderItem))
        store.dispatch(EditFolderAction.DeleteClicked)
        assertEquals(DeletionDialogState.Presenting(listOf(folderItem.guid), 19), store.state.bookmarksDeletionDialogState)

        store.dispatch(DeletionDialogAction.DeleteTapped)
        assertEquals(DeletionDialogState.None, store.state.bookmarksDeletionDialogState)
        verify(bookmarksStorage).deleteNode(folder.guid)
        verify(navController).popBackStack()
    }

    @Test
    fun `WHEN toolbar edit clicked THEN navigate to the edit screen`() = runTestOnMain {
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.MultiSelect.EditClicked)

        verify(navController).navigate(BookmarksDestinations.EDIT_BOOKMARK)
    }

    @Test
    fun `GIVEN selected tabs WHEN multi-select open in normal tabs clicked THEN open selected in new tabs and show tabs tray`() = runTestOnMain {
        var shown = false
        var mode = true
        showTabsTray = { newMode ->
            shown = true
            mode = newMode
        }
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val items = tree.children!!.filter { it.type == BookmarkNodeType.ITEM }.take(2).map {
            BookmarkItem.Bookmark(guid = it.guid, title = it.title!!, url = it.url!!, previewImageUrl = it.url!!, position = it.position!!)
        }
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(selectedItems = items),
        )

        store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked)

        assertTrue(items.size == 2)
        for (item in items) {
            verify(addNewTabUseCase).invoke(item.url, private = false)
        }
        assertTrue(shown)
        assertFalse(mode)
    }

    @Test
    fun `GIVEN selected tabs WHEN multi-select open in private tabs clicked THEN open selected in new private tabs and show tabs tray`() = runTestOnMain {
        var shown = false
        var mode = false
        showTabsTray = { newMode ->
            shown = true
            mode = newMode
        }
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val items = tree.children!!.filter { it.type == BookmarkNodeType.ITEM }.take(2).map {
            BookmarkItem.Bookmark(guid = it.guid, title = it.title!!, url = it.url!!, previewImageUrl = it.url!!, position = it.position!!)
        }
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(selectedItems = items),
        )

        store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked)

        assertTrue(items.size == 2)
        for (item in items) {
            verify(addNewTabUseCase).invoke(item.url, private = true)
        }
        assertTrue(shown)
        assertTrue(mode)
    }

    @Test
    fun `GIVEN selected tabs WHEN multi-select share clicked THEN share all tabs`() = runTestOnMain {
        var sharedBookmarks: List<BookmarkItem.Bookmark> = emptyList()
        shareBookmarks = { shareData ->
            sharedBookmarks = shareData
        }
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val items = tree.children!!.filter { it.type == BookmarkNodeType.ITEM }.take(2).map {
            BookmarkItem.Bookmark(guid = it.guid, title = it.title!!, url = it.url!!, previewImageUrl = it.url!!, position = it.position!!)
        }
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(selectedItems = items),
        )

        store.dispatch(BookmarksListMenuAction.MultiSelect.ShareClicked)

        assertEquals(
            items,
            sharedBookmarks,
        )
    }

    @Test
    fun `GIVEN a single item selected WHEN multi-select delete clicked THEN show snackbar`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val bookmarkItem = tree.children!!.first { it.type == BookmarkNodeType.ITEM }.let {
            BookmarkItem.Bookmark(guid = it.guid, title = it.title!!, url = it.url!!, previewImageUrl = it.url!!, position = it.position!!)
        }

        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(selectedItems = listOf(bookmarkItem)),
        )

        store.dispatch(BookmarksListMenuAction.MultiSelect.DeleteClicked)
        assertEquals(BookmarksSnackbarState.UndoDeletion(listOf(bookmarkItem.guid)), store.state.bookmarksSnackbarState)

        val initialCount = store.state.bookmarkItems.size
        store.dispatch(SnackbarAction.Dismissed)
        assertEquals(BookmarksSnackbarState.None, store.state.bookmarksSnackbarState)
        assertEquals(initialCount - 1, store.state.bookmarkItems.size)

        verify(bookmarksStorage).deleteNode(bookmarkItem.guid)
    }

    @Test
    fun `GIVEN two bookmarks WHEN each is deleted before a snackbar dismiss and undo is clicked after the first dismiss THEN the bookmarks are restored`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val items = tree.children!!.filter { it.type == BookmarkNodeType.ITEM }

        val bookmarkItemOne = items[0].let {
            BookmarkItem.Bookmark(it.guid, it.title!!, it.url!!, it.url!!, it.position!!)
        }

        val bookmarkItemTwo = items[1].let {
            BookmarkItem.Bookmark(it.guid, it.title!!, it.url!!, it.url!!, it.position!!)
        }

        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Bookmark.DeleteClicked(bookmarkItemOne))
        assertEquals(store.state.bookmarksDeletionSnackbarQueueCount, 1)
        store.dispatch(BookmarksListMenuAction.Bookmark.DeleteClicked(bookmarkItemTwo))
        assertEquals(store.state.bookmarksDeletionSnackbarQueueCount, 2)
        store.dispatch(SnackbarAction.Dismissed)
        assertEquals(store.state.bookmarksDeletionSnackbarQueueCount, 1)
        assertEquals(BookmarksSnackbarState.UndoDeletion(listOf(bookmarkItemOne.guid, bookmarkItemTwo.guid)), store.state.bookmarksSnackbarState)

        store.dispatch(SnackbarAction.Undo)

        val initialCount = store.state.bookmarkItems.size
        assertEquals(BookmarksSnackbarState.None, store.state.bookmarksSnackbarState)
        assertEquals(initialCount, store.state.bookmarkItems.size)

        verify(bookmarksStorage, never()).deleteNode(bookmarkItemOne.guid)
    }

    @Test
    fun `GIVEN multiple selected items WHEN multi-select delete clicked THEN show the confirmation dialog`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val items = tree.children!!.filter { it.type == BookmarkNodeType.ITEM }.take(2).map {
            BookmarkItem.Bookmark(guid = it.guid, title = it.title!!, url = it.url!!, previewImageUrl = it.url!!, position = null)
        }
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(selectedItems = items),
        )
        `when`(bookmarksStorage.countBookmarksInTrees(items.map { it.guid })).thenReturn(19u)
        store.dispatch(BookmarksListMenuAction.MultiSelect.DeleteClicked)
        assertEquals(DeletionDialogState.Presenting(items.map { it.guid }, 19), store.state.bookmarksDeletionDialogState)

        val initialCount = store.state.bookmarkItems.size
        store.dispatch(DeletionDialogAction.DeleteTapped)
        assertEquals(DeletionDialogState.None, store.state.bookmarksDeletionDialogState)
        assertEquals(initialCount - 2, store.state.bookmarkItems.size)

        for (item in items) {
            verify(bookmarksStorage).deleteNode(item.guid)
        }
    }

    @Test
    fun `GIVEN selected items in state WHEN a folder is clicked THEN update the recursive state`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                selectedItems = listOf(BookmarkItem.Folder("Folder 1", "guid1", position = 0u)),
            ),
        )
        `when`(bookmarksStorage.countBookmarksInTrees(listOf("guid1", "guid2"))).thenReturn(19u)
        store.dispatch(FolderClicked(BookmarkItem.Folder("Folder2", "guid2", position = 1u)))
        assertEquals(19, store.state.recursiveSelectedCount)
    }

    @Test
    fun `GIVEN selected items in state WHEN move folder is clicked THEN navigate to folder selection`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                selectedItems = listOf(BookmarkItem.Folder("Folder 1", "guid1", position = 0u)),
            ),
        )

        store.dispatch(BookmarksListMenuAction.MultiSelect.MoveClicked)
        verify(navController).navigate(BookmarksDestinations.SELECT_FOLDER)
    }

    @Test
    fun `WHEN first bookmarks sync is complete THEN reload the bookmarks list`() = runTestOnMain {
        val syncedGuid = "sync"
        val tree = generateBookmarkTree()
        val afterSyncTree = tree.copy(children = tree.children?.plus(generateBookmark(guid = syncedGuid, "title", "url", position = (tree.children!!.size + 1).toUInt())))
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id))
            .thenReturn(Result.success(tree))
            .thenReturn(Result.success(afterSyncTree))
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(FirstSyncCompleted)

        assertTrue(store.state.bookmarkItems.any { it.guid == syncedGuid })
    }

    @Test
    fun `GIVEN a bookmark has been deleted WHEN the view is disposed before the snackbar is dismissed THEN commit the deletion`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))

        val middleware = buildMiddleware()
        val store = middleware.makeStore()
        val bookmarkToDelete = store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark

        store.dispatch(BookmarksListMenuAction.Bookmark.DeleteClicked(bookmarkToDelete))
        val snackState = store.state.bookmarksSnackbarState
        assertTrue(snackState is BookmarksSnackbarState.UndoDeletion && snackState.guidsToDelete.first() == bookmarkToDelete.guid)
        store.dispatch(ViewDisposed)

        verify(bookmarksStorage).deleteNode(bookmarkToDelete.guid)
    }

    @Test
    fun `GIVEN adding a folder WHEN selecting a new parent THEN folder is updated`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
        val newParentItem = BookmarkItem.Folder(title = newParent.title!!, guid = newParent.guid, position = newParent.position)
        val newFolderTitle = "newFolder"
        `when`(bookmarksStorage.addFolder(newParent.guid, newFolderTitle)).thenReturn(Result.success("new-guid"))

        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.TitleChanged(newFolderTitle))
        store.dispatch(AddFolderAction.ParentFolderClicked)
        store.dispatch(SelectFolderAction.ViewAppeared)
        store.dispatch(SelectFolderAction.ItemClicked(SelectFolderItem(0, newParentItem, SelectFolderExpansionState.None)))
        store.dispatch(BackClicked)

        assertNull(store.state.bookmarksSelectFolderState)
        assertEquals(newParentItem, store.state.bookmarksAddFolderState?.parent)

        store.dispatch(BackClicked)

        verify(bookmarksStorage).addFolder(parentGuid = newParent.guid, title = newFolderTitle)
    }

    @Test
    fun `GIVEN editing a folder WHEN selecting a new parent THEN folder is updated`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val folder = tree.children?.first { it.type == BookmarkNodeType.FOLDER }!!
        val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
        val folderItem = BookmarkItem.Folder(title = folder.title!!, guid = folder.guid, position = folder.position)
        val newParentItem = BookmarkItem.Folder(title = newParent.title!!, guid = newParent.guid, position = newParent.position)

        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(0u)
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Folder.EditClicked(folderItem))
        store.dispatch(EditFolderAction.ParentFolderClicked)
        store.dispatch(SelectFolderAction.ViewAppeared)
        store.dispatch(SelectFolderAction.ItemClicked(SelectFolderItem(0, newParentItem, SelectFolderExpansionState.None)))
        store.dispatch(BackClicked)

        assertNull(store.state.bookmarksSelectFolderState)
        assertEquals(newParentItem, store.state.bookmarksEditFolderState?.parent)

        store.dispatch(BackClicked)

        verify(bookmarksStorage).updateNode(
            folder.guid,
            BookmarkInfo(
                parentGuid = newParent.guid,
                position = tree.children?.indexOfFirst { it.guid == folder.guid }!!.toUInt(),
                title = folder.title,
                url = null,
            ),
        )
    }

    @Test
    fun `GIVEN editing a bookmark WHEN selecting a new parent THEN user can successfully add a new folder`() = runTestOnMain {
        val tree = generateBookmarkTree()
        val newFolderGuid = "newFolderGuid"
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        `when`(bookmarksStorage.addFolder("folder guid 4", "newFolder")).thenReturn(Result.success(newFolderGuid))
        val bookmark = tree.children?.first { it.type == BookmarkNodeType.ITEM }!!
        val bookmarkItem = BookmarkItem.Bookmark(title = bookmark.title!!, guid = bookmark.guid, url = bookmark.url!!, previewImageUrl = bookmark.url!!, position = bookmark.position)
        val newFolderTitle = "newFolder"
        val parentForNewFolder = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
        val parentForNewFolderItem = BookmarkItem.Folder(title = parentForNewFolder.title!!, guid = parentForNewFolder.guid, position = parentForNewFolder.position)

        val middleware = buildMiddleware()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(any()))).thenReturn(0u)
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmarkItem))
        store.dispatch(EditBookmarkAction.FolderClicked)
        store.dispatch(SelectFolderAction.ViewAppeared)
        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.TitleChanged(newFolderTitle))
        store.dispatch(AddFolderAction.ParentFolderClicked)
        store.dispatch(SelectFolderAction.ItemClicked(SelectFolderItem(0, parentForNewFolderItem, SelectFolderExpansionState.None)))
        store.dispatch(BackClicked)

        assertNotNull(store.state.bookmarksSelectFolderState)
        assertNull(store.state.bookmarksSelectFolderState?.innerSelectionGuid)
        assertEquals(parentForNewFolderItem, store.state.bookmarksAddFolderState?.parent)
        assertEquals(newFolderTitle, store.state.bookmarksAddFolderState?.folderBeingAddedTitle)

        store.dispatch(BackClicked)
        assertNull(store.state.bookmarksAddFolderState)
        verify(bookmarksStorage).addFolder(parentGuid = parentForNewFolder.guid, title = newFolderTitle)

        assertNull(store.state.bookmarksSelectFolderState)
        assertEquals(newFolderGuid, store.state.bookmarksEditBookmarkState?.folder?.guid)
        assertEquals(newFolderTitle, store.state.bookmarksEditBookmarkState?.folder?.title)
    }

    @Test
    fun `GIVEN the last saved folder cache WHEN deleting the folder THEN the value in cache is reset`() =
        runTestOnMain {
            val tree = generateBookmarkTree()
            val folder = tree.children!!.first { it.type == BookmarkNodeType.FOLDER }
            val folderItem =
                BookmarkItem.Folder(guid = folder.guid, title = "title", position = folder.position)
            `when`(
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                ),
            ).thenReturn(0u)
            `when`(bookmarksStorage.countBookmarksInTrees(listOf(folderItem.guid))).thenReturn(19u)
            `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
            `when`(lastSavedFolderCache.getGuid()).thenReturn(folder.guid)

            val middleware = buildMiddleware()
            val store = middleware.makeStore()

            store.dispatch(BookmarksListMenuAction.Folder.DeleteClicked(folderItem))
            assertEquals(
                DeletionDialogState.Presenting(listOf(folderItem.guid), 19),
                store.state.bookmarksDeletionDialogState,
            )

            store.dispatch(DeletionDialogAction.DeleteTapped)
            assertEquals(DeletionDialogState.None, store.state.bookmarksDeletionDialogState)
            verify(bookmarksStorage).deleteNode(folder.guid)
            verify(lastSavedFolderCache).setGuid(null)
        }

    @Test
    fun `GIVEN editing a bookmark WHEN edit fails THAN last saved location does not change`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val bookmark = tree.children?.first { it.type == BookmarkNodeType.ITEM }!!
        val bookmarkItem = BookmarkItem.Bookmark(title = bookmark.title!!, guid = bookmark.guid, url = bookmark.url!!, previewImageUrl = bookmark.url!!, position = bookmark.position)
        `when`(bookmarksStorage.updateNode(eq(bookmark.guid), any())).thenReturn(Result.failure(IllegalStateException()))
        `when`(lastSavedFolderCache.getGuid()).thenReturn(bookmark.parentGuid)

        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmarkItem))
        store.dispatch(EditBookmarkAction.TitleChanged(""))
        store.dispatch(BackClicked)

        verify(lastSavedFolderCache, never()).setGuid(anyString())
    }

    @Test
    fun `GIVEN editing a bookmark WHEN edit fails THEN result is reported`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val bookmark = tree.children?.first { it.type == BookmarkNodeType.ITEM }!!
        val bookmarkItem = BookmarkItem.Bookmark(title = bookmark.title!!, guid = bookmark.guid, url = bookmark.url!!, previewImageUrl = bookmark.url!!, position = bookmark.position)
        `when`(bookmarksStorage.updateNode(eq(bookmark.guid), any())).thenReturn(Result.failure(IllegalStateException()))

        var reported: BookmarksGlobalResultReport? = null
        val middleware = buildMiddleware(reportResultGlobally = { reported = it })
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmarkItem))
        store.dispatch(EditBookmarkAction.TitleChanged("a title with query strings or other failures"))
        store.dispatch(BackClicked)

        assertEquals(reported, BookmarksGlobalResultReport.EditBookmarkFailed)
    }

    @Test
    fun `GIVEN adding a folder WHEN adding fails THEN result is reported`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        val newTitle = "new"
        `when`(bookmarksStorage.addFolder(BookmarkRoot.Mobile.id, newTitle)).thenReturn(Result.failure(IllegalStateException()))

        var reported: BookmarksGlobalResultReport? = null
        val middleware = buildMiddleware(reportResultGlobally = { reported = it })
        val store = middleware.makeStore()

        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.TitleChanged(newTitle))
        store.dispatch(BackClicked)

        store.dispatch(BackClicked)

        assertEquals(BookmarksGlobalResultReport.AddFolderFailed, reported)
    }

    @Test
    fun `GIVEN editing a folder WHEN adding fails THEN result is reported`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))
        `when`(bookmarksStorage.updateNode(any(), any())).thenReturn(Result.failure(IllegalStateException()))
        val folder = tree.children?.first { it.type == BookmarkNodeType.FOLDER }!!
        val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
        val folderItem = BookmarkItem.Folder(title = folder.title!!, guid = folder.guid, position = folder.position)

        var reported: BookmarksGlobalResultReport? = null
        val middleware = buildMiddleware(reportResultGlobally = { reported = it })
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Folder.EditClicked(folderItem))
        store.dispatch(EditFolderAction.TitleChanged("secrets"))
        store.dispatch(BackClicked)

        assertEquals(BookmarksGlobalResultReport.EditFolderFailed, reported)
    }

    @Test
    fun `GIVEN moving a bookmark item WHEN moving fails THEN result is reported`() = runTestOnMain {
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.updateNode(any(), any())).thenReturn(Result.failure(IllegalStateException()))
        val middleware = buildMiddleware()

        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksMultiselectMoveState = MultiselectMoveState(
                    guidsToMove = listOf("item guid 1", "item guid 2"),
                    destination = "folder guid 1",
                ),
                bookmarksSelectFolderState = BookmarksSelectFolderState(
                    outerSelectionGuid = "folder guid 1",
                ),
            ),
        )
        store.dispatch(BackClicked)

        assertEquals(BookmarksSnackbarState.SelectFolderFailed, store.state.bookmarksSnackbarState)
    }

    @Test
    fun `while moving items and adding a new folder, returning from the add folder screen results in the move items being moved to the new folder`() = runTestOnMain {
        val tree = generateBookmarkTree()
        `when`(bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Menu.id, BookmarkRoot.Toolbar.id, BookmarkRoot.Unfiled.id))).thenReturn(0u)
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id)).thenReturn(Result.success(tree))

        val middleware = buildMiddleware()
        val guidsToMove = listOf("item guid 1", "item guid 2")
        val store = middleware.makeStore(
            initialState = BookmarksState.default.copy(
                bookmarksMultiselectMoveState = MultiselectMoveState(
                    guidsToMove = guidsToMove,
                    destination = "folder guid 1",
                ),
                bookmarksSelectFolderState = BookmarksSelectFolderState(
                    outerSelectionGuid = "folder guid 1",
                ),
            ),
        )

        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.ParentFolderClicked)
        store.dispatch(SelectFolderAction.ViewAppeared)

        val parentNodeForNewFolder = tree.children!!.first { it.type == BookmarkNodeType.FOLDER }
        val parentForNewFolder = parentNodeForNewFolder.let {
            BookmarkItem.Folder(
                title = it.title!!,
                guid = it.guid,
                position = it.position,
            )
        }
        val newFolder = BookmarkNode(
            type = BookmarkNodeType.FOLDER,
            guid = "new folder guid",
            title = "new folder title",
            parentGuid = parentForNewFolder.guid,
            position = 10u,
            url = "url",
            dateAdded = 0,
            lastModified = 0,
            children = null,
        )
        val mobileRootSelectableItem = SelectFolderItem(
            indentation = 0,
            folder = BookmarkItem.Folder(
                title = tree.title!!,
                guid = tree.guid,
                position = tree.position!!,
            ),
            expansionState = SelectFolderExpansionState.Closed,
        )
        `when`(bookmarksStorage.addFolder(parentForNewFolder.guid, newFolder.title!!)).thenReturn(Result.success(newFolder.guid))
        `when`(bookmarksStorage.getBookmark(newFolder.guid)).thenReturn(Result.success(newFolder))

        `when`(bookmarksStorage.getTree(parentForNewFolder.guid)).thenReturn(Result.success(parentNodeForNewFolder))
        store.dispatch(SelectFolderAction.ChevronClicked(mobileRootSelectableItem))
        assertTrue((store.state.bookmarksSelectFolderState?.folders?.first()?.expansionState as? SelectFolderExpansionState.Open)?.children?.any { it.guid == parentForNewFolder.guid } == true)

        store.dispatch(SelectFolderAction.ItemClicked(SelectFolderItem(0, parentForNewFolder, SelectFolderExpansionState.None)))
        assertNotNull(store.state.bookmarksSelectFolderState?.innerSelectionGuid)

        store.dispatch(BackClicked)
        assertNull(store.state.bookmarksSelectFolderState?.innerSelectionGuid)
        assertEquals(parentForNewFolder, store.state.bookmarksAddFolderState?.parent)

        store.dispatch(AddFolderAction.TitleChanged(newFolder.title!!))
        store.dispatch(BackClicked)
        assertNull(store.state.bookmarksAddFolderState)
        assertNull(store.state.bookmarksSelectFolderState)
        assertNull(store.state.bookmarksMultiselectMoveState)
        guidsToMove.forEachIndexed { idx, guid ->
            verify(bookmarksStorage).updateNode(
                guid,
                BookmarkInfo(
                newFolder.guid,
                null,
                "item title ${idx + 1}",
                "item url ${idx + 1}",
            ),
            )
        }
        verify(navController).popBackStack(BookmarksDestinations.LIST, false)
    }

    private fun buildMiddleware(
        useNewSearchUX: Boolean = false,
        openBookmarksInNewTab: Boolean = false,
        reportResultGlobally: (BookmarksGlobalResultReport) -> Unit = {},
    ) = BookmarksMiddleware(
        bookmarksStorage = bookmarksStorage,
        clipboardManager = clipboardManager,
        addNewTabUseCase = addNewTabUseCase,
        fenixBrowserUseCases = fenixBrowserUseCases,
        useNewSearchUX = useNewSearchUX,
        openBookmarksInNewTab = openBookmarksInNewTab,
        getNavController = { navController },
        exitBookmarks = exitBookmarks,
        navigateToBrowser = navigateToBrowser,
        navigateToSearch = navigateToSearch,
        navigateToSignIntoSync = navigateToSignIntoSync,
        shareBookmarks = shareBookmarks,
        showTabsTray = showTabsTray,
        resolveFolderTitle = resolveFolderTitle,
        getBrowsingMode = getBrowsingMode,
        ioDispatcher = coroutineRule.testDispatcher,
        saveBookmarkSortOrder = saveSortOrder,
        lastSavedFolderCache = lastSavedFolderCache,
        reportResultGlobally = reportResultGlobally,
        lifecycleScope = CoroutineScope(testDispatcher),
    )

    private fun BookmarksMiddleware.makeStore(
        initialState: BookmarksState = BookmarksState.default,
        bookmarkToLoad: String? = null,
    ) = BookmarksStore(
        initialState = initialState,
        middleware = listOf(this),
        bookmarkToLoad = bookmarkToLoad,
    )

    private fun generateBookmarkFolders(parentGuid: String) = List(5) {
        generateBookmarkFolder(
            guid = "folder guid $it",
            title = "folder title $it",
            parentGuid = parentGuid,
            position = it.toUInt(),
        )
    }

    private fun generateBookmarkItems(num: Int = 5, startingPosition: UInt = 0u) = List(num) {
        generateBookmark("item guid $it", "item title $it", "item url $it", position = startingPosition + it.toUInt())
    }

    private fun generateDesktopRootTree() = BookmarkNode(
        type = BookmarkNodeType.FOLDER,
        guid = BookmarkRoot.Root.id,
        parentGuid = null,
        position = 0U,
        title = "root",
        url = null,
        dateAdded = 0,
        lastModified = 0,
        children = listOf(
            generateBookmarkFolder(BookmarkRoot.Menu.id, "Menu", BookmarkRoot.Root.id, position = 0u),
            generateBookmarkFolder(BookmarkRoot.Toolbar.id, "Toolbar", BookmarkRoot.Root.id, position = 1u),
            generateBookmarkFolder(BookmarkRoot.Unfiled.id, "Unfiled", BookmarkRoot.Root.id, position = 2u),
            generateBookmarkTree(rootPosition = 3u),
        ),
    )

    private fun generateBookmarkTree(rootPosition: UInt = 0u) = BookmarkNode(
        type = BookmarkNodeType.FOLDER,
        guid = BookmarkRoot.Mobile.id,
        parentGuid = null,
        position = rootPosition,
        title = "mobile",
        url = null,
        dateAdded = 0,
        lastModified = 0,
        children = run {
            val folders = generateBookmarkFolders(BookmarkRoot.Mobile.id)
            folders + generateBookmarkItems(startingPosition = folders.size.toUInt())
        },
    )

    private fun generateBookmarkFolder(guid: String, title: String, parentGuid: String, position: UInt) = BookmarkNode(
        type = BookmarkNodeType.FOLDER,
        guid = guid,
        parentGuid = parentGuid,
        position = position,
        title = title,
        url = null,
        dateAdded = 0,
        lastModified = 0,
        children = generateBookmarkItems(startingPosition = 0u),
    )

    private fun generateBookmark(guid: String, title: String?, url: String, position: UInt, lastModified: Long = 0) = BookmarkNode(
        type = BookmarkNodeType.ITEM,
        guid = guid,
        parentGuid = null,
        position = position,
        title = title,
        url = url,
        dateAdded = 0,
        lastModified = lastModified,
        children = listOf(),
    )

    private fun NavController.mockBackstack(expectedId: Int) {
        val destination = mock<NavDestination>()
        val backStackEntry = mock<NavBackStackEntry>()
        `when`(destination.id).thenReturn(expectedId)
        `when`(backStackEntry.destination).thenReturn(destination)
        `when`(previousBackStackEntry).thenReturn(backStackEntry)
    }

    private suspend fun mockDesktopFoldersForSelectScreen() {
        `when`(bookmarksStorage.getTree(BookmarkRoot.Root.id, recursive = false)).thenReturn(Result.success(generateDesktopRootTree()))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Mobile.id, recursive = false)).thenReturn(Result.success(generateBookmarkTree()))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Menu.id, recursive = false)).thenReturn(Result.success(generateBookmarkFolder(BookmarkRoot.Menu.id, "Menu", BookmarkRoot.Root.id, position = 0u)))
        `when`(bookmarksStorage.getTree(BookmarkRoot.Unfiled.id, recursive = false)).thenReturn(Result.success(generateBookmarkFolder(BookmarkRoot.Unfiled.id, "Unfiled", BookmarkRoot.Root.id, position = 2u)))
    }
}
