/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import android.content.ClipboardManager
import androidx.navigation.NavController
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.emptyFlow
import kotlinx.coroutines.test.runTest
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.storage.BookmarkInfo
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.tabs.TabsUseCases
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.bookmarks.BookmarksUseCase
import org.mozilla.fenix.components.bookmarks.LastSavedFolderCache
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import kotlin.test.assertNotNull

class BookmarksMiddlewareTest {

    private lateinit var bookmarksStorage: BookmarksStorage
    private lateinit var clipboardManager: ClipboardManager
    private lateinit var addNewTabUseCase: TabsUseCases.AddNewTabUseCase
    private lateinit var fenixBrowserUseCases: FenixBrowserUseCases
    private lateinit var navController: NavController
    private lateinit var exitBookmarks: () -> Unit
    private lateinit var navigateToBrowser: () -> Unit
    private lateinit var navigateToSignIntoSync: () -> Unit
    private lateinit var navigateToImportDialog: () -> Unit
    private lateinit var shareBookmarks: (List<BookmarkItem.Bookmark>) -> Unit
    private lateinit var showTabsTray: (Boolean) -> Unit
    private lateinit var getBrowsingMode: () -> BrowsingMode
    private lateinit var lastSavedFolderCache: LastSavedFolderCache
    private lateinit var saveSortOrder: suspend (BookmarksListSortOrder) -> Unit
    private val resolveFolderTitle = { node: BookmarkNode ->
        friendlyRootTitle(
            mockk(),
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
        bookmarksStorage = mockk(relaxed = true)

        coEvery { bookmarksStorage.getBookmark(any()) } returns Result.success(
            mockk(relaxed = true) {
                every { position } returns 0u
            },
        )

        coEvery { bookmarksStorage.getTree(any()) } returns Result.success(
                mockk(relaxed = true) {
                    every { position } returns 0u
                },
        )

        coEvery { bookmarksStorage.getTree(any(), any()) } returns Result.success(
            mockk(relaxed = true) {
                every { position } returns 0u
            },
        )

        coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u

        clipboardManager = mockk(relaxed = true)
        addNewTabUseCase = mockk(relaxed = true)
        fenixBrowserUseCases = mockk(relaxed = true)
        navController = mockk(relaxed = true)
        every { navController.popBackStack() } returns true
        exitBookmarks = { }
        navigateToBrowser = { }
        navigateToSignIntoSync = { }
        navigateToImportDialog = { }
        shareBookmarks = { }
        showTabsTray = { _ -> }
        getBrowsingMode = { BrowsingMode.Normal }
        lastSavedFolderCache = mockk(relaxed = true)
        saveSortOrder = { }
    }

    @Test
    fun `GIVEN a nested bookmark structure WHEN the store is initialized on create THEN all the folders have nested child counts`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.countBookmarksInTrees(listOf(BookmarkRoot.Mobile.id)) } returns 30u
            coEvery { bookmarksStorage.countBookmarksInTrees(listOf("folder guid 0")) } returns 8u
            coEvery { bookmarksStorage.countBookmarksInTrees(listOf("folder guid 1")) } returns 7u
            coEvery { bookmarksStorage.countBookmarksInTrees(listOf("folder guid 2")) } returns 3u
            coEvery { bookmarksStorage.countBookmarksInTrees(listOf("folder guid 3")) } returns 9u
            coEvery { bookmarksStorage.countBookmarksInTrees(listOf("folder guid 4")) } returns 1u

            val middleware = buildMiddleware(this)

            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            val expectedMap =
                mapOf(
                    "folder guid 0" to 8,
                    "folder guid 1" to 7,
                    "folder guid 2" to 3,
                    "folder guid 3" to 9,
                    "folder guid 4" to 1,
                )
            assertEquals(
                expectedMap,
                store.state.bookmarkItems.folders().associate { it.guid to it.nestedItemCount },
            )
        }

    @Test
    fun `GIVEN a nested bookmark structure WHEN SelectAll is clicked THEN all bookmarks are selected and reflected in state`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()
            coEvery { bookmarksStorage.countBookmarksInTrees(store.state.bookmarkItems.map { it.guid }) } returns 35u

            store.dispatch(BookmarksListMenuAction.SelectAll)
            testScheduler.advanceUntilIdle()

            assertEquals(store.state.selectedItems.size, store.state.bookmarkItems.size)
            assertEquals(35, store.state.recursiveSelectedCount)
        }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync WHEN store is initialized THEN bookmarks will be loaded as display format`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u
            val middleware = buildMiddleware(this)

            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()
            testScheduler.advanceUntilIdle()

            assertEquals(10, store.state.bookmarkItems.size)
        }

    @Test
    fun `GIVEN bookmarks in storage and navigating directly to the edit screen WHEN store is initialized THEN bookmark to edit will be loaded`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            val parent = generateBookmark("item guid 1", null, "https://mozilla.org", position = 0u)
            val child =
                generateBookmark("item guid 2", null, "https://mozilla.org", position = 0u).copy(
                    parentGuid = "item guid 1",
                )
            coEvery { bookmarksStorage.getBookmark("item guid 1") } returns Result.success(parent)
            coEvery { bookmarksStorage.getBookmark("item guid 2") } returns Result.success(child)
            val middleware = buildMiddleware(this)

            val store = middleware.makeStore(bookmarkToLoad = "item guid 2")
            testScheduler.advanceUntilIdle()

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
    fun `GIVEN bookmarks in storage and not signed into sync WHEN store is initialized THEN bookmarks will be sorted by last modified date`() =
        runTest {
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
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(root)
            val middleware = buildMiddleware(this)

            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()
            assertEquals(5, store.state.bookmarkItems.size)
        }

    @Test
    fun `GIVEN a bookmarks store WHEN SortMenuItem is clicked THEN Save the new sort order`() =
        runTest {
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
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(root)
            var newSortOrder = BookmarksListSortOrder.default
            saveSortOrder = {
                newSortOrder = it
            }
            val middleware = buildMiddleware(this)

            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()
            store.dispatch(BookmarksListMenuAction.SortMenu.NewestClicked)
            testScheduler.advanceUntilIdle()

            assertEquals(BookmarksListSortOrder.Created(true), newSortOrder)
        }

    @Test
    fun `GIVEN SelectFolderScreen WHEN SortMenuItem is clicked THEN Save the new sort order`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
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
            val middleware = buildMiddleware(this)

            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()
            store.dispatch(EditBookmarkClicked(bookmarkItem))
            store.dispatch(EditBookmarkAction.FolderClicked)
            store.dispatch(SelectFolderAction.ViewAppeared)
            assertEquals(false, store.state.sortMenuShown)

            store.dispatch(SelectFolderAction.SortMenu.SortMenuButtonClicked)
            testScheduler.advanceUntilIdle()

            assertEquals(true, store.state.sortMenuShown)

            store.dispatch(SelectFolderAction.SortMenu.NewestClicked)
            testScheduler.advanceUntilIdle()

            assertEquals(BookmarksListSortOrder.Created(true), newSortOrder)
        }

    @Test
    fun `GIVEN bookmarks in storage and user has a desktop bookmark WHEN store is initialized THEN bookmarks, including desktop will be loaded as display format`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 1u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Root.id) } returns Result.success(
                generateDesktopRootTree(),
            )
            val middleware = buildMiddleware(this)
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 1u
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(isSignedIntoSync = true),
            )
            testScheduler.advanceUntilIdle()

            assertEquals(11, store.state.bookmarkItems.size)
        }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync but has existing desktop bookmarks WHEN store is initialized THEN bookmarks, including desktop will be loaded as display format`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 1u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Root.id) } returns Result.success(
                generateDesktopRootTree(),
            )
            val middleware = buildMiddleware(this)
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 1u
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            assertEquals(11, store.state.bookmarkItems.size)
        }

    @Test
    fun `GIVEN no bookmarks under mobile root WHEN store is initialized THEN list of bookmarks will be empty`() =
        runTest {
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(null)
            val middleware = buildMiddleware(this)

            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            assertEquals(0, store.state.bookmarkItems.size)
        }

    @Test
    fun `GIVEN bookmarks should be open in a new tab WHEN a bookmark is clicked THEN open it as a new tab`() =
        runTest {
            val url = "url"
            val bookmarkItem = BookmarkItem.Bookmark(url, "title", url, guid = "", position = null)
            val openBookmarksInNewTab = true
            getBrowsingMode = { BrowsingMode.Normal }
            var navigated = false
            navigateToBrowser = { navigated = true }

            val middleware = buildMiddleware(this, openBookmarksInNewTab = openBookmarksInNewTab)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarkItems = listOf(bookmarkItem),
                ),
            )

            store.dispatch(BookmarkClicked(bookmarkItem))

            coVerify {
                fenixBrowserUseCases.loadUrlOrSearch(
                    searchTermOrURL = url,
                    newTab = openBookmarksInNewTab,
                    private = false,
                    flags = EngineSession.LoadUrlFlags.select(
                        EngineSession.LoadUrlFlags.ALLOW_JAVASCRIPT_URL,
                    ),
                )
            }
            assertTrue(navigated)
        }

    @Test
    fun `GIVEN bookmarks should not be open in a new tab WHEN a bookmark is clicked THEN open it in the existing tab`() =
        runTest {
            val url = "url"
            val bookmarkItem = BookmarkItem.Bookmark(url, "title", url, guid = "", position = null)
            val openBookmarksInNewTab = false
            getBrowsingMode = { BrowsingMode.Normal }
            var navigated = false
            navigateToBrowser = { navigated = true }

            val middleware = buildMiddleware(this, openBookmarksInNewTab = openBookmarksInNewTab)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarkItems = listOf(bookmarkItem),
                ),
            )

            store.dispatch(BookmarkClicked(bookmarkItem))

            coVerify {
                fenixBrowserUseCases.loadUrlOrSearch(
                    searchTermOrURL = url,
                    newTab = openBookmarksInNewTab,
                    private = false,
                    flags = EngineSession.LoadUrlFlags.select(
                        EngineSession.LoadUrlFlags.ALLOW_JAVASCRIPT_URL,
                    ),
                )
            }
            assertTrue(navigated)
        }

    @Test
    fun `WHEN folder is clicked THEN children are loaded and screen title is updated to folder title`() =
        runTest {
            val bookmarkTree = generateBookmarkTree()
            val folderNode = bookmarkTree.children!!.first { it.type == BookmarkNodeType.FOLDER }
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            coEvery { bookmarksStorage.getTree(folderNode.guid) } returns Result.success(
                generateBookmarkFolder(
                    folderNode.guid,
                    folderNode.title!!,
                    BookmarkRoot.Mobile.id,
                    folderNode.position!!,
                ),
            )

            val middleware = buildMiddleware(this)
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
            testScheduler.advanceUntilIdle()

            assertEquals(folderNode.title, store.state.currentFolder.title)
            assertEquals(5, store.state.bookmarkItems.size)
        }

    @Test
    fun `WHEN search button is clicked THEN no navigation callback fires`() = runTest {
        var navigated = false
        navigateToBrowser = { navigated = true }
        val middleware = buildMiddleware(this)
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(SearchClicked)

        assertFalse(navigated)
    }

    @Test
    fun `WHEN add folder button is clicked THEN navigate to folder screen`() = runTest {
        val middleware = buildMiddleware(this)
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(AddFolderClicked)

        verify { navController.navigate(BookmarksDestinations.ADD_FOLDER) }
    }

    @Test
    fun `GIVEN current screen is add folder WHEN parent folder is clicked THEN navigate to folder selection screen`() =
        runTest {
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(AddFolderAction.ParentFolderClicked)

            coVerify { navController.navigate(BookmarksDestinations.SELECT_FOLDER) }
        }

    @Test
    fun `GIVEN current screen is edit bookmark WHEN folder is clicked THEN navigate to folder selection screen`() =
        runTest {
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(EditBookmarkAction.FolderClicked)

            coVerify { navController.navigate(BookmarksDestinations.SELECT_FOLDER) }
        }

    @Test
    fun `GIVEN current screen is add folder and new folder title is nonempty WHEN back is clicked THEN navigate back, save the new folder, and load the updated tree`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            coEvery {
                bookmarksStorage.addFolder(
                    BookmarkRoot.Mobile.id,
                    "test",
                )
            } returns Result.success("new-guid")

            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()
            val newFolderTitle = "test"

            store.dispatch(AddFolderClicked)
            store.dispatch(AddFolderAction.TitleChanged(newFolderTitle))
            testScheduler.advanceUntilIdle()

            assertNotNull(store.state.bookmarksAddFolderState)

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 1) {
                bookmarksStorage.addFolder(
                    store.state.currentFolder.guid,
                    title = newFolderTitle,
                )
            }
            coVerify(exactly = 2) { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) }
            verify(exactly = 1) { navController.popBackStack() }
            assertNull(store.state.bookmarksAddFolderState)
        }

    @Test
    fun `GIVEN current screen is add folder and new folder title is empty WHEN back is clicked THEN navigate back to the previous tree and don't save anything`() =
        runTest {
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(AddFolderClicked)
            store.dispatch(AddFolderAction.TitleChanged("test"))
            store.dispatch(AddFolderAction.TitleChanged(""))
            assertNotNull(store.state.bookmarksAddFolderState)

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 0) {
                bookmarksStorage.addFolder(
                    parentGuid = store.state.currentFolder.guid,
                    title = "",
                )
            }
            verify(exactly = 1) { navController.popBackStack() }
            assertNull(store.state.bookmarksAddFolderState)
        }

    @Test
    fun `GIVEN current screen is add folder and previous screen is select folder WHEN back is clicked THEN skip selection screen straight back to the edit bookmark screen`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Mobile.id,
                    recursive = false,
                )
            } returns Result.success(generateBookmarkTree())
            coEvery {
                bookmarksStorage.addFolder(
                    BookmarkRoot.Mobile.id,
                    "i'm a new folder",
                )
            } returns Result.success("new-guid")
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            val bookmark =
                store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
            val newFolderTitle = "i'm a new folder"

            store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmark))
            store.dispatch(EditBookmarkAction.FolderClicked)
            store.dispatch(SelectFolderAction.ViewAppeared)
            store.dispatch(AddFolderClicked)
            store.dispatch(AddFolderAction.TitleChanged(newFolderTitle))
            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            assertNull(store.state.bookmarksSelectFolderState)
            coVerify(exactly = 3) {
                bookmarksStorage.getTree(
                    BookmarkRoot.Mobile.id,
                    recursive = false,
                )
            }
            verify(exactly = 1) {
                navController.popBackStack(
                    BookmarksDestinations.EDIT_BOOKMARK,
                    inclusive = false,
                )
            }
        }

    @Test
    fun `GIVEN current screen is add folder and previous screen is not select folder WHEN back is clicked THEN navigate back`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Mobile.id,
                    recursive = false,
                )
            } returns Result.success(generateBookmarkTree())
            coEvery {
                bookmarksStorage.addFolder(
                    BookmarkRoot.Mobile.id,
                    "i'm a new folder",
                )
            } returns Result.success("new-guid")
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(AddFolderClicked)
            store.dispatch(AddFolderAction.TitleChanged("i'm a new folder"))
            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 2) {
                bookmarksStorage.getTree(
                    BookmarkRoot.Mobile.id,
                    recursive = false,
                )
            }
            verify(exactly = 1) { navController.popBackStack() }
        }

    @Test
    fun `GIVEN current screen is edit folder and new title is nonempty WHEN back is clicked THEN navigate back, save the folder, and load the updated tree`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            val middleware = buildMiddleware(this)
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarksEditFolderState = BookmarksEditFolderState(
                        parent = BookmarkItem.Folder("Bookmarks", "guid0", 0u),
                        folder = BookmarkItem.Folder("folder title 0", "folder guid 0", 0u),
                    ),
                ),
            )
            testScheduler.advanceUntilIdle()

            val newFolderTitle = "test"

            store.dispatch(EditFolderAction.TitleChanged(newFolderTitle))
            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 1) {
                bookmarksStorage.updateNode(
                    guid = "folder guid 0",
                    info = BookmarkInfo(
                        parentGuid = "guid0",
                        position = 0u,
                        title = "test",
                        url = null,
                    ),
                )
            }
            coVerify(exactly = 2) { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) }
            coVerify(exactly = 1) { navController.popBackStack() }
            assertNull(store.state.bookmarksEditFolderState)
        }

    @Test
    fun `GIVEN current screen is edit folder and new title is empty WHEN back is clicked THEN navigate back, without siving the folder, and load the updated tree`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarksEditFolderState = BookmarksEditFolderState(
                        parent = BookmarkItem.Folder("Bookmarks", "guid0", 0u),
                        folder = BookmarkItem.Folder("folder title 0", "folder guid 0", 0u),
                    ),
                ),
            )
            testScheduler.advanceUntilIdle()

            val newFolderTitle = ""

            store.dispatch(EditFolderAction.TitleChanged(newFolderTitle))
            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 0) {
                bookmarksStorage.updateNode(
                    guid = "folder guid 0",
                    info = BookmarkInfo(
                        parentGuid = "guid0",
                        position = 0u,
                        title = "test",
                        url = null,
                    ),
                )
            }

            coVerify(exactly = 2) { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) }
            verify(exactly = 1) { navController.popBackStack() }
            assertNull(store.state.bookmarksEditFolderState)
        }

    @Test
    fun `GIVEN current screen is edit folder and new title is empty WHEN the folder location is changed and back is clicked THEN navigate back saves the valid changes and load the updated tree`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val middleware = buildMiddleware(this)

            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarksEditFolderState = BookmarksEditFolderState(
                        parent = BookmarkItem.Folder("Bookmarks", "guid0", 0u),
                        folder = BookmarkItem.Folder("folder title 0", "folder guid 0", 0u),
                    ),
                ),
            )

            val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
            val newParentItem = BookmarkItem.Folder(
                title = newParent.title!!,
                guid = newParent.guid,
                position = newParent.position,
            )
            val newFolderTitle = ""

            store.dispatch(EditFolderAction.TitleChanged(newFolderTitle))
            testScheduler.advanceUntilIdle()

            store.dispatch(EditFolderAction.ParentFolderClicked)
            testScheduler.advanceUntilIdle()

            store.dispatch(SelectFolderAction.ViewAppeared)
            testScheduler.advanceUntilIdle()

            store.dispatch(
                SelectFolderAction.ItemClicked(
                    SelectFolderItem(
                        0,
                        newParentItem,
                        SelectFolderExpansionState.None,
                    ),
                ),
            )
            testScheduler.advanceUntilIdle()

            store.dispatch(BackClicked)
            store.dispatch(BackClicked)

            testScheduler.advanceUntilIdle()

            coVerify(exactly = 1) {
                bookmarksStorage.updateNode(
                    guid = "folder guid 0",
                    info = BookmarkInfo(
                        parentGuid = newParentItem.guid,
                        position = 0u,
                        title = "folder title 0",
                        url = null,
                    ),
                )
            }
            coVerify(exactly = 3) { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) }
            assertNull(store.state.bookmarksEditFolderState)
        }

    @Test
    fun `GIVEN current screen is edit bookmark WHEN back is clicked THEN navigate back, save the bookmark, and load the updated tree`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            val middleware = buildMiddleware(this)

            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()
            val newBookmarkTitle = "my awesome bookmark"

            val bookmark =
                store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
            store.dispatch(EditBookmarkClicked(bookmark = bookmark))
            testScheduler.advanceUntilIdle()

            store.dispatch(EditBookmarkAction.TitleChanged(title = newBookmarkTitle))
            testScheduler.advanceUntilIdle()

            assertNotNull(store.state.bookmarksEditBookmarkState)

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            val expectedPosition = bookmark.position!!
            coVerify(exactly = 1) {
                bookmarksStorage.updateNode(
                    guid = "item guid 0",
                    info = BookmarkInfo(
                        parentGuid = BookmarkRoot.Mobile.id,
                        position = expectedPosition,
                        title = "my awesome bookmark",
                        url = "item url 0",
                    ),
                )
            }
            coVerify(exactly = 1) { lastSavedFolderCache.setGuid(BookmarkRoot.Mobile.id) }
            coVerify(exactly = 2) { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) }
            coVerify(exactly = 1) { navController.popBackStack() }
            assertNull(store.state.bookmarksEditBookmarkState)
        }

    @Test
    fun `GIVEN current screen is edit bookmark WHEN the parent folder is changed and back is clicked THEN cache is updated to the new parent`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val middleware = buildMiddleware(this)

            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            val bookmark =
                store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
            val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
            val newParentItem = BookmarkItem.Folder(
                title = newParent.title!!,
                guid = newParent.guid,
                position = newParent.position,
            )

            store.dispatch(EditBookmarkClicked(bookmark = bookmark))
            testScheduler.advanceUntilIdle()

            store.dispatch(EditBookmarkAction.FolderClicked)
            testScheduler.advanceUntilIdle()

            store.dispatch(SelectFolderAction.ViewAppeared)
            testScheduler.advanceUntilIdle()

            store.dispatch(
                SelectFolderAction.ItemClicked(
                    SelectFolderItem(
                        0,
                        newParentItem,
                        SelectFolderExpansionState.None,
                    ),
                ),
            )
            testScheduler.advanceUntilIdle()

            store.dispatch(BackClicked)
            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 1) { lastSavedFolderCache.setGuid(newParent.guid) }
            assertNull(store.state.bookmarksEditBookmarkState)
        }

    @Test
    fun `GIVEN current screen is edit bookmark and nothing was changed WHEN back is clicked THEN cache is not updated`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            val middleware = buildMiddleware(this)

            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            val bookmark =
                store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
            store.dispatch(EditBookmarkClicked(bookmark = bookmark))
            testScheduler.advanceUntilIdle()

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 0) { lastSavedFolderCache.setGuid(any<String>()) }
            assertNull(store.state.bookmarksEditBookmarkState)
        }

    @Test
    fun `GIVEN current screen is edit bookmark and the bookmark title is empty WHEN back is clicked THEN navigate back`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()
            val newBookmarkTitle = ""

            val bookmark =
                store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
            store.dispatch(EditBookmarkClicked(bookmark = bookmark))
            store.dispatch(EditBookmarkAction.TitleChanged(title = newBookmarkTitle))
            testScheduler.advanceUntilIdle()

            assertNotNull(store.state.bookmarksEditBookmarkState)

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 0) {
                bookmarksStorage.updateNode(
                    guid = "item guid 0",
                    info = BookmarkInfo(
                        parentGuid = BookmarkRoot.Mobile.id,
                        position = 5u,
                        title = "",
                        url = "item url 0",
                    ),
                )
            }

            coVerify(exactly = 1) { navController.popBackStack() }
            assertNull(store.state.bookmarksEditBookmarkState)
        }

    @Test
    fun `GIVEN current screen is edit bookmark WHEN the title is set to empty and the url is changed and back is clicked THEN the url saves but the title does not`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()
            val newBookmarkTitle = ""

            val bookmark =
                store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
            val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
            val newParentItem = BookmarkItem.Folder(
                title = newParent.title!!,
                guid = newParent.guid,
                position = newParent.position,
            )
            store.dispatch(EditBookmarkClicked(bookmark = bookmark))
            store.dispatch(EditBookmarkAction.TitleChanged(title = newBookmarkTitle))
            store.dispatch(EditBookmarkAction.FolderClicked)
            store.dispatch(
                SelectFolderAction.ItemClicked(
                    SelectFolderItem(
                        0,
                        newParentItem,
                        SelectFolderExpansionState.None,
                    ),
                ),
            )
            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            assertNotNull(store.state.bookmarksEditBookmarkState)

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 1) {
                bookmarksStorage.updateNode(
                    guid = "item guid 0",
                    info = BookmarkInfo(
                        parentGuid = newParentItem.guid,
                        position = 5u,
                        title = bookmark.title,
                        url = "item url 0",
                    ),
                )
            }

            assertNull(store.state.bookmarksEditBookmarkState)
        }

    @Test
    fun `GIVEN current screen is list and the top-level is loaded WHEN back is clicked THEN exit bookmarks`() =
        runTest {
            every { navController.popBackStack() } returns false

            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u

            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )

            var exited = false
            exitBookmarks = { exited = true }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BackClicked)

            assertTrue(exited)
        }

    @Test
    fun `GIVEN current screen is list and a bookmark is selected WHEN back is clicked THEN clear out selected item`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            var exited = false
            exitBookmarks = { exited = true }
            val middleware = buildMiddleware(this)
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
    fun `GIVEN current screen is an empty list and the top-level is loaded WHEN sign into sync is clicked THEN navigate to sign into sync `() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            var navigated = false
            navigateToSignIntoSync = { navigated = true }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(SignIntoSyncClicked)

            assertTrue(navigated)
        }

    @Test
    fun `GIVEN current screen is a subfolder WHEN close is clicked THEN exit bookmarks `() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            var navigated = false
            exitBookmarks = { navigated = true }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(CloseClicked)

            assertTrue(navigated)
        }

    @Test
    fun `GIVEN current screen is list and a sub-level folder is loaded WHEN back is clicked THEN load the parent level`() =
        runTest {
            val tree = generateBookmarkTree()
            val firstFolderNode = tree.children!!.first { it.type == BookmarkNodeType.FOLDER }
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            coEvery { bookmarksStorage.getTree(firstFolderNode.guid) } returns Result.success(
                generateBookmarkTree(),
            )
            coEvery { bookmarksStorage.getBookmark(firstFolderNode.guid) } returns Result.success(
                firstFolderNode,
            )
            val middleware = buildMiddleware(this)
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(
                FolderClicked(
                    BookmarkItem.Folder(
                        title = firstFolderNode.title!!,
                        guid = firstFolderNode.guid,
                        firstFolderNode.position,
                    ),
                ),
            )
            testScheduler.advanceUntilIdle()

            assertEquals(firstFolderNode.guid, store.state.currentFolder.guid)

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            assertEquals(BookmarkRoot.Mobile.id, store.state.currentFolder.guid)
            assertEquals(tree.children!!.size, store.state.bookmarkItems.size)
        }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync WHEN select folder sub screen view is loaded THEN only mobile root is loaded onto screen`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Mobile.id,
                    recursive = false,
                )
            } returns Result.success(generateBookmarkTree())
            val middleware = buildMiddleware(this)
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
                ),
            )

            store.dispatch(SelectFolderAction.ViewAppeared)
            testScheduler.advanceUntilIdle()

            assertEquals(1, store.state.bookmarksSelectFolderState?.folders?.count())
        }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync WHEN expanding mobile root on select folder screen THEN mobile root children are shown as well`() =
        runTest {
            val tree = generateBookmarkTree()
            tree.children?.forEach {
                if (it.type == BookmarkNodeType.FOLDER) {
                    coEvery { bookmarksStorage.getTree(it.guid, false) } returns Result.success(
                        generateBookmarkFolder(it.guid, "title", BookmarkRoot.Mobile.id, 0u),
                    )
                }
            }
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Mobile.id,
                    false,
                )
            } returns Result.success(tree)
            val middleware = buildMiddleware(this)
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u
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
            testScheduler.advanceUntilIdle()

            assertEquals(
                6,
                store.state.bookmarksSelectFolderState?.folders?.flattenToList()?.count(),
            )
        }

    @Test
    fun `GIVEN select folder screen with created sort order WHEN mobile root is expanded THEN child folders respect the sort order`() =
        runTest {
            val oldestFolder = BookmarkNode(
                type = BookmarkNodeType.FOLDER,
                guid = "a-folder-oldest",
                parentGuid = BookmarkRoot.Mobile.id,
                position = 0u,
                title = "a-folder-oldest",
                url = null,
                dateAdded = 100,
                lastModified = 0,
                children = listOf(),
            )
            val newestFolder = BookmarkNode(
                type = BookmarkNodeType.FOLDER,
                guid = "b-folder-newest",
                parentGuid = BookmarkRoot.Mobile.id,
                position = 1u,
                title = "b-folder-newest",
                url = null,
                dateAdded = 300,
                lastModified = 0,
                children = listOf(),
            )
            val middleFolder = BookmarkNode(
                type = BookmarkNodeType.FOLDER,
                guid = "c-folder",
                parentGuid = BookmarkRoot.Mobile.id,
                position = 2u,
                title = "c-folder",
                url = null,
                dateAdded = 200,
                lastModified = 0,
                children = listOf(),
            )
            val root = BookmarkNode(
                type = BookmarkNodeType.FOLDER,
                guid = BookmarkRoot.Mobile.id,
                parentGuid = null,
                position = 0u,
                title = "mobile",
                url = null,
                dateAdded = 0,
                lastModified = 0,
                children = listOf(newestFolder, oldestFolder, middleFolder),
            )
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Mobile.id,
                    recursive = false,
                )
            } returns Result.success(root)
            coEvery {
                bookmarksStorage.getTree(
                    oldestFolder.guid,
                    recursive = false,
                )
            } returns Result.success(oldestFolder)
            coEvery {
                bookmarksStorage.getTree(
                    newestFolder.guid,
                    recursive = false,
                )
            } returns Result.success(newestFolder)
            coEvery {
                bookmarksStorage.getTree(
                    middleFolder.guid,
                    recursive = false,
                )
            } returns Result.success(middleFolder)
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
                ),
            )
            testScheduler.advanceUntilIdle()

            // Sort by oldest and assert sort order
            store.dispatch(SelectFolderAction.ViewAppeared)
            testScheduler.advanceUntilIdle()

            store.dispatch(SelectFolderAction.SortMenu.OldestClicked)
            testScheduler.advanceUntilIdle()

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
            testScheduler.advanceUntilIdle()

            val children =
                (store.state.bookmarksSelectFolderState?.folders?.first()?.expansionState as SelectFolderExpansionState.Open).children
            assertEquals(
                listOf(oldestFolder.guid, middleFolder.guid, newestFolder.guid),
                children.map { it.guid },
            )

            // Below is a resort to Z to A and re-assertion on sort order
            store.dispatch(SelectFolderAction.ViewAppeared)
            testScheduler.advanceUntilIdle()

            store.dispatch(SelectFolderAction.SortMenu.ZtoAClicked)
            testScheduler.advanceUntilIdle()

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
            testScheduler.advanceUntilIdle()

            val childrenSecondCheck =
                (store.state.bookmarksSelectFolderState?.folders?.first()?.expansionState as SelectFolderExpansionState.Open).children
            assertEquals(
                listOf(middleFolder.guid, newestFolder.guid, oldestFolder.guid),
                childrenSecondCheck.map { it.guid },
            )
        }

    @Test
    fun `GIVEN a folder with subfolders WHEN select folder sub screen view is loaded THEN load folders into sub screen state without the selected folder`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            val rootNode = generateBookmarkFolder(
                "parent",
                "first",
                BookmarkRoot.Mobile.id,
                position = 0u,
            ).copy(
                children = generateBookmarkFolders("parent"),
            )
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Mobile.id,
                    recursive = true,
                )
            } returns Result.success(rootNode)
            val middleware = buildMiddleware(this)
            testScheduler.advanceUntilIdle()

            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
                    bookmarksEditFolderState = BookmarksEditFolderState(
                        parent = BookmarkItem.Folder("Bookmarks", "guid0", 0u),
                        folder = BookmarkItem.Folder("first", "parent", 0u),
                    ),
                ),
            )
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u
            store.dispatch(BookmarksListMenuAction.SelectAll)
            testScheduler.advanceUntilIdle()

            val bookmarkCount = store.state.selectedItems.size
            store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked)
            testScheduler.advanceUntilIdle()

            assertEquals(bookmarkCount, store.state.bookmarksSelectFolderState?.folders?.count())
        }

    @Test
    fun `GIVEN selected folder WHEN OpenInNormalTabsClicked THEN urls opened as normal tabs`() =
        runTest {
            val bookmarkFolder = BookmarkItem.Folder(title = "folder", guid = "1234", position = 0u)
            val bookmarkItem = BookmarkItem.Bookmark(
                url = "url",
                title = "title",
                previewImageUrl = "string",
                guid = "guid",
                position = bookmarkFolder.position,
            )

            var trayShown = false
            showTabsTray = { trayShown = true }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState =
                    BookmarksState.default.copy(
                        bookmarkItems = listOf(bookmarkFolder, bookmarkItem),
                        selectedItems = listOf(bookmarkFolder, bookmarkItem),
                    ),
            )

            store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked)
            testScheduler.advanceUntilIdle()
            verify { addNewTabUseCase.invoke(url = "url", private = false) }
            assertTrue(trayShown)
        }

    @Test
    fun `GIVEN selected folder WHEN OpenInPrivateTabsClicked THEN urls opened as private tabs`() =
        runTest {
            val bookmarkFolder = BookmarkItem.Folder(title = "folder", guid = "1234", position = 0u)
            val bookmarkItem = BookmarkItem.Bookmark(
                url = "url",
                title = "title",
                previewImageUrl = "string",
                guid = "guid",
                position = bookmarkFolder.position,
            )

            var trayShown = false
            showTabsTray = { trayShown = true }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState =
                    BookmarksState.default.copy(
                        bookmarkItems = listOf(bookmarkFolder, bookmarkItem),
                        selectedItems = listOf(bookmarkFolder, bookmarkItem),
                    ),
            )

            store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked)
            testScheduler.advanceUntilIdle()
            verify(exactly = 1) { addNewTabUseCase.invoke(url = "url", private = true) }
            assertTrue(trayShown)
        }

    @Test
    fun `GIVEN bookmarks in storage and not signed into sync but have pre-existing desktop bookmarks saved WHEN select folder sub screen view is loaded THEN load folders, including desktop folders into sub screen state`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 1u
            mockDesktopFoldersForSelectScreen()
            val middleware = buildMiddleware(this)
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 1u
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
                ),
            )

            store.dispatch(SelectFolderAction.ViewAppeared)
            testScheduler.advanceUntilIdle()

            assertEquals(4, store.state.bookmarksSelectFolderState?.folders?.count())
        }

    @Test
    fun `GIVEN bookmarks in storage and has desktop bookmarks WHEN select folder sub screen view is loaded THEN load folders, including desktop folders into sub screen state`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 1u
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Root.id,
                    recursive = false,
                )
            } returns Result.success(generateDesktopRootTree())
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Mobile.id,
                    recursive = false,
                )
            } returns Result.success(generateBookmarkTree())
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Menu.id,
                    recursive = false,
                )
            } returns Result.success(
                generateBookmarkFolder(
                    BookmarkRoot.Menu.id,
                    "Menu",
                    BookmarkRoot.Root.id,
                    position = 0u,
                ),
            )
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Toolbar.id,
                    recursive = false,
                )
            } returns Result.success(
                generateBookmarkFolder(
                    BookmarkRoot.Toolbar.id,
                    "Toolbar",
                    BookmarkRoot.Root.id,
                    position = 1u,
                ),
            )
            coEvery {
                bookmarksStorage.getTree(
                    BookmarkRoot.Unfiled.id,
                    recursive = false,
                )
            } returns Result.success(
                generateBookmarkFolder(
                    BookmarkRoot.Unfiled.id,
                    "Unfiled",
                    BookmarkRoot.Root.id,
                    position = 2u,
                ),
            )

            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    isSignedIntoSync = true,
                    bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
                ),
            )

            store.dispatch(SelectFolderAction.ViewAppeared)
            testScheduler.advanceUntilIdle()

            assertEquals(5, store.state.bookmarksSelectFolderState?.folders?.count())
        }

    @Test
    fun `GIVEN bookmarks in storage and has desktop bookmarks WHEN desktop folder is expanded THEN then load its children`() =
        runTest {
            val childFolderGuid = "child folder"
            val childFolder =
                generateBookmarkFolder(childFolderGuid, "title", BookmarkRoot.Toolbar.id, 5u)
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 1u
            mockDesktopFoldersForSelectScreen()
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Toolbar.id, recursive = false) } returns
                    Result.success(
                        generateBookmarkFolder(
                            BookmarkRoot.Toolbar.id,
                            "Toolbar",
                            BookmarkRoot.Root.id,
                            position = 1u,
                        )
                            .let { it.copy(children = it.children!! + listOf(childFolder)) },
                    )
            coEvery {
                bookmarksStorage.getTree(
                    "child folder",
                    recursive = false,
                )
            } returns Result.success(childFolder)

            val middleware = buildMiddleware(this)
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
            testScheduler.advanceUntilIdle()

            assertEquals(5, store.state.bookmarksSelectFolderState?.folders?.count())
            assertEquals(
                6,
                store.state.bookmarksSelectFolderState?.folders?.flattenToList()?.count(),
            )
        }

    @Test
    fun `GIVEN current screen select folder WHEN back is clicked THEN pop the backstack`() =
        runTest {
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
                ),
            )

            store.dispatch(BackClicked)
            verify(exactly = 1) { navController.popBackStack() }
        }

    @Test
    fun `GIVEN current screen select folder while multi-selecting WHEN back is clicked THEN pop the backstack and update the selected bookmark items`() =
        runTest {
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
                generateBookmarkTree(),
            )
            val middleware = buildMiddleware(this)
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
            testScheduler.advanceUntilIdle()

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 2) { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) }
            verify(exactly = 1) { navController.popBackStack() }
            coVerify(exactly = 1) {
                bookmarksStorage.updateNode(
                    guid = "item guid 1",
                    info = BookmarkInfo(
                        parentGuid = "folder guid 1",
                        position = null,
                        title = "item title 1",
                        url = "item url 1",
                    ),
                )
            }
            coVerify(exactly = 1) {
                bookmarksStorage.updateNode(
                    guid = "item guid 2",
                    info = BookmarkInfo(
                        parentGuid = "folder guid 1",
                        position = null,
                        title = "item title 2",
                        url = "item url 2",
                    ),
                )
            }
        }

    @Test
    fun `GIVEN current screen select folder WHEN the search query is updated THEN FilteredFoldersLoaded gets dispatched with the filtered folders`() =
        runTest {
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
                                                        BookmarkItem.Folder(
                                                            "Nested Three",
                                                            "guid0",
                                                            null,
                                                        ),
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
                SelectFolderItem(
                    0,
                    BookmarkItem.Folder("Nested 0", "guid0", null),
                    SelectFolderExpansionState.None,
                ),
            )

            val bookmarksFolder = folders.first()

            val middleware = buildMiddleware(this)
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
            testScheduler.advanceUntilIdle()

            assertEquals(
                listOf(bookmarksFolder),
                store.state.bookmarksSelectFolderState?.filteredFolders,
            )
        }

    @Test
    fun `WHEN edit clicked in bookmark item menu THEN nav to edit screen`() = runTest {
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
            generateBookmarkTree(),
        )
        val middleware = buildMiddleware(this)
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        val bookmark =
            store.state.bookmarkItems.first { it is BookmarkItem.Bookmark } as BookmarkItem.Bookmark
        store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmark))
        testScheduler.advanceUntilIdle()

        assertEquals(bookmark, store.state.bookmarksEditBookmarkState!!.bookmark)
        coVerify(exactly = 1) { navController.navigate(BookmarksDestinations.EDIT_BOOKMARK) }
    }

    @Test
    fun `WHEN share clicked in bookmark item menu THEN share the bookmark`() = runTest {
        var sharedBookmarks: List<BookmarkItem.Bookmark> = emptyList()
        shareBookmarks = { shareData ->
            sharedBookmarks = shareData
        }
        val middleware = buildMiddleware(this)
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()
        val url = "url"
        val title = "title"
        val bookmarkItem = BookmarkItem.Bookmark(
            url = url,
            title = title,
            previewImageUrl = url,
            guid = "guid",
            position = null,
        )

        store.dispatch(BookmarksListMenuAction.Bookmark.ShareClicked(bookmarkItem))

        assertTrue(
            "Expected only one bookmark is shared. Got ${sharedBookmarks.size} instead",
            sharedBookmarks.size == 1,
        )
        assertEquals(url, sharedBookmarks.first().url)
        assertEquals(title, sharedBookmarks.first().title)
    }

    @Test
    fun `WHEN open in normal tab clicked in bookmark item menu THEN add a normal tab and show the tabs tray in normal mode`() =
        runTest {
            val url = "url"
            val bookmarkItem = BookmarkItem.Bookmark(
                url = url,
                title = "title",
                previewImageUrl = url,
                guid = "guid",
                position = null,
            )
            var trayShown = false
            var mode = true
            showTabsTray = { newMode ->
                mode = newMode
                trayShown = true
            }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Bookmark.OpenInNormalTabClicked(bookmarkItem))

            coVerify(exactly = 1) { addNewTabUseCase.invoke(url = url, private = false) }
            assertTrue(trayShown)
            assertFalse(mode)
        }

    @Test
    fun `WHEN open in private tab clicked in bookmark item menu THEN add a private tab and show the tabs tray in private mode`() =
        runTest {
            val url = "url"
            val bookmarkItem = BookmarkItem.Bookmark(
                url = url,
                title = "title",
                previewImageUrl = url,
                guid = "guid",
                position = null,
            )
            var trayShown = false
            var mode = false
            showTabsTray = { newMode ->
                mode = newMode
                trayShown = true
            }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Bookmark.OpenInPrivateTabClicked(bookmarkItem))

            coVerify(exactly = 1) { addNewTabUseCase.invoke(url = url, private = true) }
            assertTrue(trayShown)
            assertTrue(mode)
        }

    @Test
    fun `GIVEN we are in the select folder screen and we are moving multiple bookmarks and bookmark items is empty WHEN the folder is selected and back button is clicked THEN we show a snackbar describing the error`() =
        runTest {
            val tree = generateBookmarkTree()
            val middleware = buildMiddleware(this)
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
            testScheduler.advanceUntilIdle()

            assertEquals(
                BookmarksSnackbarState.SelectFolderFailed,
                store.state.bookmarksSnackbarState,
            )
        }

    @Test
    fun `GIVEN a user is on the edit screen with nothing on the backstack WHEN delete is clicked THEN pop the backstack, delete the bookmark and exit bookmarks`() =
        runTest {
            var exited = false
            exitBookmarks = { exited = true }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                        bookmark = BookmarkItem.Bookmark(
                            "ur",
                            "title",
                            "url",
                            "guid",
                            position = 0u,
                        ),
                        folder = BookmarkItem.Folder("title", "guid", position = 0u),
                    ),
                ),
            )
            every { navController.popBackStack() } returns false
            store.dispatch(EditBookmarkAction.DeleteClicked)
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 1) { navController.popBackStack() }
            coVerify(exactly = 1) { bookmarksStorage.deleteNode("guid") }
            assertTrue(exited)
        }

    @Test
    fun `WHEN edit clicked in folder item menu THEN nav to the edit screen`() = runTest {
        val middleware = buildMiddleware(this)
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(BookmarksListMenuAction.Folder.EditClicked(folder = store.state.currentFolder))

        coVerify(exactly = 1) { navController.navigate(BookmarksDestinations.EDIT_FOLDER) }
    }

    @Test
    fun `GIVEN a folder with fewer than 15 items WHEN open all in normal tabs clicked in folder item menu THEN open all the bookmarks as normal tabs and show the tabs tray in normal mode`() =
        runTest {
            val guid = "guid"
            val folderItem = BookmarkItem.Folder(title = "title", guid = guid, position = 0u)
            val folder = generateBookmarkFolder(guid = guid, "title", "parentGuid", position = 0u)
            coEvery { bookmarksStorage.getTree(guid) } returns Result.success(folder)
            var trayShown = false
            var mode = true
            showTabsTray = { newMode ->
                mode = newMode
                trayShown = true
            }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Folder.OpenAllInNormalTabClicked(folderItem))
            testScheduler.advanceUntilIdle()

            folder.children!!.forEach { child ->
                coVerify(exactly = 1) {
                    addNewTabUseCase.invoke(
                        url = child.url!!,
                        private = false,
                    )
                }
            }
            assertTrue(trayShown)
            assertFalse(mode)
        }

    @Test
    fun `GIVEN a folder with 15 or more items WHEN open all in normal tabs clicked in folder item menu THEN show a warning`() =
        runTest {
            val guid = "guid"
            val folderItem = BookmarkItem.Folder(title = "title", guid = guid, position = 0u)
            val folder =
                generateBookmarkFolder(guid = guid, "title", "parentGuid", position = 0u).copy(
                    children = List(15) {
                        generateBookmark(
                            guid = "bookmark guid $it",
                            title = "bookmark title $it",
                            url = "bookmark urk",
                            position = it.toUInt(),
                        )
                    },
                )
            coEvery { bookmarksStorage.getTree(guid) } returns Result.success(folder)
            var trayShown = false
            showTabsTray = { _ -> trayShown = true }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Folder.OpenAllInNormalTabClicked(folderItem))
            testScheduler.advanceUntilIdle()

            val expected = OpenTabsConfirmationDialog.Presenting(
                guidToOpen = guid,
                numberOfTabs = 15,
                isPrivate = false,
            )
            assertEquals(expected, store.state.openTabsConfirmationDialog)

            folder.children!!.forEach { child ->
                coVerify(exactly = 0) {
                    addNewTabUseCase.invoke(
                        url = child.url!!,
                        private = false,
                    )
                }
            }
            assertFalse(trayShown)
        }

    @Test
    fun `GIVEN a folder with fewer than 15 items WHEN open all in private tabs clicked in folder item menu THEN open all the bookmarks as private tabs and show the tabs tray in private mode`() =
        runTest {
            val guid = "guid"
            val folderItem = BookmarkItem.Folder(title = "title", guid = guid, position = 0u)
            val folder = generateBookmarkFolder(guid = guid, "title", "parentGuid", position = 0u)
            coEvery { bookmarksStorage.getTree(guid) } returns Result.success(folder)
            var trayShown = false
            var mode = false
            showTabsTray = { newMode ->
                mode = newMode
                trayShown = true
            }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Folder.OpenAllInPrivateTabClicked(folderItem))
            testScheduler.advanceUntilIdle()

            folder.children!!.forEach { child ->
                coVerify(exactly = 1) { addNewTabUseCase.invoke(url = child.url!!, private = true) }
            }
            assertTrue(trayShown)
            assertTrue(mode)
        }

    @Test
    fun `GIVEN a folder with 15 or more items WHEN open all in private tabs clicked in folder item menu THEN show a warning`() =
        runTest {
            val guid = "guid"
            val folderItem = BookmarkItem.Folder(title = "title", guid = guid, position = 0u)
            val folder =
                generateBookmarkFolder(guid = guid, "title", "parentGuid", position = 0u).copy(
                    children = List(15) {
                        generateBookmark(
                            guid = "bookmark guid $it",
                            title = "bookmark title $it",
                            url = "bookmark urk",
                            position = it.toUInt(),
                        )
                    },
                )
            coEvery { bookmarksStorage.getTree(guid) } returns Result.success(folder)
            var trayShown = false
            showTabsTray = { _ -> trayShown = true }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Folder.OpenAllInPrivateTabClicked(folderItem))
            testScheduler.advanceUntilIdle()

            val expected = OpenTabsConfirmationDialog.Presenting(
                guidToOpen = guid,
                numberOfTabs = 15,
                isPrivate = true,
            )
            assertEquals(expected, store.state.openTabsConfirmationDialog)

            folder.children!!.forEach { child ->
                verify(exactly = 0) {
                    addNewTabUseCase.invoke(url = child.url!!, private = true)
                }
            }
            assertFalse(trayShown)
        }

    @Test
    fun `WHEN delete clicked in folder item menu THEN present a dialog showing the number of items to be deleted and when delete clicked, delete the selected folder`() =
        runTest {
            val tree = generateBookmarkTree()
            val folder = tree.children!!.first { it.type == BookmarkNodeType.FOLDER }
            val folderItem =
                BookmarkItem.Folder(guid = folder.guid, title = "title", position = folder.position)
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.countBookmarksInTrees(listOf(folderItem.guid)) } returns 19u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Folder.DeleteClicked(folderItem))
            testScheduler.advanceUntilIdle()

            assertEquals(
                DeletionDialogState.Presenting(listOf(folderItem.guid), 19),
                store.state.bookmarksDeletionDialogState,
            )

            store.dispatch(DeletionDialogAction.DeleteTapped)
            testScheduler.advanceUntilIdle()

            assertEquals(DeletionDialogState.None, store.state.bookmarksDeletionDialogState)
            coVerify { bookmarksStorage.deleteNode(folder.guid) }
        }

    @Test
    fun `WHEN delete clicked in folder edit screen THEN present a dialog showing the number of items to be deleted and when delete clicked, delete the selected folder`() =
        runTest {
            val tree = generateBookmarkTree()
            val folder = tree.children!!.first { it.type == BookmarkNodeType.FOLDER }
            val folderItem =
                BookmarkItem.Folder(guid = folder.guid, title = "title", position = folder.position)
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.countBookmarksInTrees(listOf(folderItem.guid)) } returns 19u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Folder.EditClicked(folderItem))
            store.dispatch(EditFolderAction.DeleteClicked)
            testScheduler.advanceUntilIdle()

            assertEquals(
                DeletionDialogState.Presenting(listOf(folderItem.guid), 19),
                store.state.bookmarksDeletionDialogState,
            )

            store.dispatch(DeletionDialogAction.DeleteTapped)
            testScheduler.advanceUntilIdle()

            assertEquals(DeletionDialogState.None, store.state.bookmarksDeletionDialogState)
            coVerify { bookmarksStorage.deleteNode(folder.guid) }
            verify { navController.popBackStack() }
        }

    @Test
    fun `WHEN toolbar edit clicked THEN navigate to the edit screen`() = runTest {
        val middleware = buildMiddleware(this)
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(BookmarksListMenuAction.MultiSelect.EditClicked)

        verify { navController.navigate(BookmarksDestinations.EDIT_BOOKMARK) }
    }

    @Test
    fun `GIVEN selected tabs WHEN multi-select open in normal tabs clicked THEN open selected in new tabs and show tabs tray`() =
        runTest {
            var shown = false
            var mode = true
            showTabsTray = { newMode ->
                shown = true
                mode = newMode
            }
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val items = tree.children!!.filter { it.type == BookmarkNodeType.ITEM }.take(2).map {
                BookmarkItem.Bookmark(
                    guid = it.guid,
                    title = it.title!!,
                    url = it.url!!,
                    previewImageUrl = it.url!!,
                    position = it.position!!,
                )
            }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(selectedItems = items),
            )

            store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked)
            testScheduler.advanceUntilIdle()

            assertTrue(items.size == 2)
            for (item in items) {
                coVerify(exactly = 1) { addNewTabUseCase.invoke(item.url, private = false) }
            }
            assertTrue(shown)
            assertFalse(mode)
        }

    @Test
    fun `GIVEN selected tabs WHEN multi-select open in private tabs clicked THEN open selected in new private tabs and show tabs tray`() =
        runTest {
            var shown = false
            var mode = false
            showTabsTray = { newMode ->
                shown = true
                mode = newMode
            }
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val items = tree.children!!.filter { it.type == BookmarkNodeType.ITEM }.take(2).map {
                BookmarkItem.Bookmark(
                    guid = it.guid,
                    title = it.title!!,
                    url = it.url!!,
                    previewImageUrl = it.url!!,
                    position = it.position!!,
                )
            }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(selectedItems = items),
            )

            store.dispatch(BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked)
            testScheduler.advanceUntilIdle()

            assertTrue(items.size == 2)
            for (item in items) {
                coVerify(exactly = 1) { addNewTabUseCase.invoke(item.url, private = true) }
            }
            assertTrue(shown)
            assertTrue(mode)
        }

    @Test
    fun `GIVEN selected tabs WHEN multi-select share clicked THEN share all tabs`() = runTest {
        var sharedBookmarks: List<BookmarkItem.Bookmark> = emptyList()
        shareBookmarks = { shareData ->
            sharedBookmarks = shareData
        }
        val tree = generateBookmarkTree()
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
        val items = tree.children!!.filter { it.type == BookmarkNodeType.ITEM }.take(2).map {
            BookmarkItem.Bookmark(
                guid = it.guid,
                title = it.title!!,
                url = it.url!!,
                previewImageUrl = it.url!!,
                position = it.position!!,
            )
        }
        val middleware = buildMiddleware(this)
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
    fun `GIVEN multiple selected items WHEN multi-select delete clicked THEN show the confirmation dialog`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val items = tree.children!!.filter { it.type == BookmarkNodeType.ITEM }.take(2).map {
                BookmarkItem.Bookmark(
                    guid = it.guid,
                    title = it.title!!,
                    url = it.url!!,
                    previewImageUrl = it.url!!,
                    position = null,
                )
            }
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(selectedItems = items),
            )
            coEvery { bookmarksStorage.countBookmarksInTrees(items.map { it.guid }) } returns 19u
            store.dispatch(BookmarksListMenuAction.MultiSelect.DeleteClicked)
            testScheduler.advanceUntilIdle()

            assertEquals(
                DeletionDialogState.Presenting(items.map { it.guid }, 19),
                store.state.bookmarksDeletionDialogState,
            )

            val initialCount = store.state.bookmarkItems.size
            store.dispatch(DeletionDialogAction.DeleteTapped)
            testScheduler.advanceUntilIdle()

            assertEquals(DeletionDialogState.None, store.state.bookmarksDeletionDialogState)
            assertEquals(initialCount - 2, store.state.bookmarkItems.size)

            for (item in items) {
                coVerify(exactly = 1) { bookmarksStorage.deleteNode(item.guid) }
            }
        }

    @Test
    fun `GIVEN selected items in state WHEN a folder is clicked THEN update the recursive state`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    selectedItems = listOf(BookmarkItem.Folder("Folder 1", "guid1", position = 0u)),
                ),
            )
            coEvery { bookmarksStorage.countBookmarksInTrees(listOf("guid1", "guid2")) } returns 19u
            store.dispatch(FolderClicked(BookmarkItem.Folder("Folder2", "guid2", position = 1u)))
            testScheduler.advanceUntilIdle()

            assertEquals(19, store.state.recursiveSelectedCount)
        }

    @Test
    fun `GIVEN selected items in state WHEN move folder is clicked THEN navigate to folder selection`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore(
                initialState = BookmarksState.default.copy(
                    selectedItems = listOf(BookmarkItem.Folder("Folder 1", "guid1", position = 0u)),
                ),
            )

            store.dispatch(BookmarksListMenuAction.MultiSelect.MoveClicked)
            verify(exactly = 1) { navController.navigate(BookmarksDestinations.SELECT_FOLDER) }
        }

    @Test
    fun `GIVEN a folder WHEN move is clicked in overflow menu THEN navigate to folder selection`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u

            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()

            store.dispatch(
                BookmarksListMenuAction.Folder.MoveClicked(
                    BookmarkItem.Folder(
                        "Folder 1",
                        "guid1",
                        position = 0u,
                    ),
                ),
            )
            verify { navController.navigate(BookmarksDestinations.SELECT_FOLDER) }
        }

    @Test
    fun `GIVEN a bookmark WHEN move is clicked in overflow menu THEN navigate to folder selection`() = runTest {
        val tree = generateBookmarkTree()
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery {
            bookmarksStorage.getTree(BookmarkRoot.Mobile.id)
        } returns Result.success(tree)
        val middleware = buildMiddleware(this)
        val store = middleware.makeStore()

        store.dispatch(BookmarksListMenuAction.Bookmark.MoveClicked(BookmarkItem.Bookmark("item url 0", "item title 0", "null", "item guid 0", 0u)))
        verify { navController.navigate(BookmarksDestinations.SELECT_FOLDER) }
    }

    @Test
    fun `WHEN first bookmarks sync is complete THEN reload the bookmarks list`() = runTest {
        val syncedGuid = "sync"
        val tree = generateBookmarkTree()
        val afterSyncTree = tree.copy(
            children = tree.children?.plus(
                generateBookmark(
                    guid = syncedGuid,
                    "title",
                    "url",
                    position = (tree.children!!.size + 1).toUInt(),
                ),
            ),
        )
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returnsMany listOf(
            Result.success(
                tree,
            ),
            Result.success(afterSyncTree),
        )
        val middleware = buildMiddleware(this)
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(FirstSyncCompleted)
        testScheduler.advanceUntilIdle()

        assertTrue(store.state.bookmarkItems.any { it.guid == syncedGuid })
    }

    @Test
    fun `GIVEN adding a folder WHEN selecting a new parent THEN folder is updated`() = runTest {
        val tree = generateBookmarkTree()
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
        val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
        val newParentItem = BookmarkItem.Folder(
            title = newParent.title!!,
            guid = newParent.guid,
            position = newParent.position,
        )
        val newFolderTitle = "newFolder"
        coEvery {
            bookmarksStorage.addFolder(
                newParent.guid,
                newFolderTitle,
            )
        } returns Result.success("new-guid")

        val middleware = buildMiddleware(this)
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.TitleChanged(newFolderTitle))
        store.dispatch(AddFolderAction.ParentFolderClicked)
        store.dispatch(SelectFolderAction.ViewAppeared)
        store.dispatch(
            SelectFolderAction.ItemClicked(
                SelectFolderItem(
                    0,
                    newParentItem,
                    SelectFolderExpansionState.None,
                ),
            ),
        )
        store.dispatch(BackClicked)

        assertNull(store.state.bookmarksSelectFolderState)
        assertEquals(newParentItem, store.state.bookmarksAddFolderState?.parent)

        store.dispatch(BackClicked)
        testScheduler.advanceUntilIdle()

        coVerify(exactly = 1) {
            bookmarksStorage.addFolder(
                parentGuid = newParent.guid,
                title = newFolderTitle,
            )
        }
    }

    @Test
    fun `GIVEN editing a folder WHEN selecting a new parent THEN folder is updated`() = runTest {
        val tree = generateBookmarkTree()
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
        val folder = tree.children?.first { it.type == BookmarkNodeType.FOLDER }!!
        val newParent = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
        val folderItem = BookmarkItem.Folder(
            title = folder.title!!,
            guid = folder.guid,
            position = folder.position,
        )
        val newParentItem = BookmarkItem.Folder(
            title = newParent.title!!,
            guid = newParent.guid,
            position = newParent.position,
        )

        val middleware = buildMiddleware(this)
        coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(BookmarksListMenuAction.Folder.EditClicked(folderItem))
        testScheduler.advanceUntilIdle()

        store.dispatch(EditFolderAction.ParentFolderClicked)
        testScheduler.advanceUntilIdle()

        store.dispatch(SelectFolderAction.ViewAppeared)
        store.dispatch(
            SelectFolderAction.ItemClicked(
                SelectFolderItem(
                    0,
                    newParentItem,
                    SelectFolderExpansionState.None,
                ),
            ),
        )
        testScheduler.advanceUntilIdle()

        store.dispatch(BackClicked)
        testScheduler.advanceUntilIdle()

        assertNull(store.state.bookmarksSelectFolderState)
        assertEquals(newParentItem, store.state.bookmarksEditFolderState?.parent)

        store.dispatch(BackClicked)
        testScheduler.advanceUntilIdle()

        coVerify(exactly = 1) {
            bookmarksStorage.updateNode(
                folder.guid,
                BookmarkInfo(
                    parentGuid = newParent.guid,
                    position = tree.children?.indexOfFirst { it.guid == folder.guid }!!.toUInt(),
                    title = folder.title,
                    url = null,
                ),
            )
        }
    }

    @Test
    fun `GIVEN editing a bookmark WHEN selecting a new parent THEN user can successfully add a new folder`() =
        runTest {
            val tree = generateBookmarkTree()
            val newFolderGuid = "newFolderGuid"
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            coEvery {
                bookmarksStorage.addFolder(
                    "folder guid 4",
                    "newFolder",
                )
            } returns Result.success(newFolderGuid)
            val bookmark = tree.children?.first { it.type == BookmarkNodeType.ITEM }!!
            val bookmarkItem = BookmarkItem.Bookmark(
                title = bookmark.title!!,
                guid = bookmark.guid,
                url = bookmark.url!!,
                previewImageUrl = bookmark.url!!,
                position = bookmark.position,
            )
            val newFolderTitle = "newFolder"
            val parentForNewFolder = tree.children?.last { it.type == BookmarkNodeType.FOLDER }!!
            val parentForNewFolderItem = BookmarkItem.Folder(
                title = parentForNewFolder.title!!,
                guid = parentForNewFolder.guid,
                position = parentForNewFolder.position,
            )

            val middleware = buildMiddleware(this)
            coEvery { bookmarksStorage.countBookmarksInTrees(any()) } returns 0u
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmarkItem))
            store.dispatch(EditBookmarkAction.FolderClicked)
            store.dispatch(SelectFolderAction.ViewAppeared)
            store.dispatch(AddFolderClicked)
            store.dispatch(AddFolderAction.TitleChanged(newFolderTitle))
            store.dispatch(AddFolderAction.ParentFolderClicked)
            store.dispatch(
                SelectFolderAction.ItemClicked(
                    SelectFolderItem(
                        0,
                        parentForNewFolderItem,
                        SelectFolderExpansionState.None,
                    ),
                ),
            )
            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            assertNotNull(store.state.bookmarksSelectFolderState)
            assertNull(store.state.bookmarksSelectFolderState?.innerSelectionGuid)
            assertEquals(parentForNewFolderItem, store.state.bookmarksAddFolderState?.parent)
            assertEquals(newFolderTitle, store.state.bookmarksAddFolderState?.folderBeingAddedTitle)

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            assertNull(store.state.bookmarksAddFolderState)
            coVerify(exactly = 1) {
                bookmarksStorage.addFolder(
                    parentGuid = parentForNewFolder.guid,
                    title = newFolderTitle,
                )
            }

            assertNull(store.state.bookmarksSelectFolderState)
            assertEquals(newFolderGuid, store.state.bookmarksEditBookmarkState?.folder?.guid)
            assertEquals(newFolderTitle, store.state.bookmarksEditBookmarkState?.folder?.title)
        }

    @Test
    fun `GIVEN editing a bookmark WHEN edit fails THAN last saved location does not change`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
            val bookmark = tree.children?.first { it.type == BookmarkNodeType.ITEM }!!
            val bookmarkItem = BookmarkItem.Bookmark(
                title = bookmark.title!!,
                guid = bookmark.guid,
                url = bookmark.url!!,
                previewImageUrl = bookmark.url!!,
                position = bookmark.position,
            )
            coEvery {
                bookmarksStorage.updateNode(
                    eq(bookmark.guid),
                    any(),
                )
            } returns Result.failure(IllegalStateException())
            coEvery { lastSavedFolderCache.getGuid() } returns bookmark.parentGuid

            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmarkItem))
            store.dispatch(EditBookmarkAction.TitleChanged(""))
            store.dispatch(BackClicked)

            coVerify(exactly = 0) { lastSavedFolderCache.setGuid(any<String>()) }
        }

    @Test
    fun `GIVEN editing a bookmark WHEN edit fails THEN result is reported`() = runTest {
        val tree = generateBookmarkTree()
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
        val bookmark = tree.children?.first { it.type == BookmarkNodeType.ITEM }!!
        val bookmarkItem = BookmarkItem.Bookmark(
            title = bookmark.title!!,
            guid = bookmark.guid,
            url = bookmark.url!!,
            previewImageUrl = bookmark.url!!,
            position = bookmark.position,
        )
        coEvery { bookmarksStorage.updateNode(eq(bookmark.guid), any()) } returns Result.failure(
            IllegalStateException(),
        )

        var reported: BookmarksGlobalResultReport? = null
        val middleware = buildMiddleware(this, reportResultGlobally = { reported = it })
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(BookmarksListMenuAction.Bookmark.EditClicked(bookmarkItem))
        store.dispatch(EditBookmarkAction.TitleChanged("a title with query strings or other failures"))
        store.dispatch(BackClicked)
        testScheduler.advanceUntilIdle()

        assertEquals(reported, BookmarksGlobalResultReport.EditBookmarkFailed)
    }

    @Test
    fun `GIVEN adding a folder WHEN adding fails THEN result is reported`() = runTest {
        val tree = generateBookmarkTree()
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
        val newTitle = "new"
        coEvery {
            bookmarksStorage.addFolder(
                BookmarkRoot.Mobile.id,
                newTitle,
            )
        } returns Result.failure(IllegalStateException())

        var reported: BookmarksGlobalResultReport? = null
        val middleware = buildMiddleware(this, reportResultGlobally = { reported = it })
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(AddFolderClicked)
        store.dispatch(AddFolderAction.TitleChanged(newTitle))
        store.dispatch(BackClicked)

        store.dispatch(BackClicked)
        testScheduler.advanceUntilIdle()

        assertEquals(BookmarksGlobalResultReport.AddFolderFailed, reported)
    }

    @Test
    fun `GIVEN editing a folder WHEN adding fails THEN result is reported`() = runTest {
        val tree = generateBookmarkTree()
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)
        coEvery { bookmarksStorage.updateNode(any(), any()) } returns Result.failure(
            IllegalStateException(),
        )
        val folder = tree.children?.first { it.type == BookmarkNodeType.FOLDER }!!
        val folderItem = BookmarkItem.Folder(
            title = folder.title!!,
            guid = folder.guid,
            position = folder.position,
        )

        var reported: BookmarksGlobalResultReport? = null
        val middleware = buildMiddleware(this, reportResultGlobally = { reported = it })
        val store = middleware.makeStore()
        testScheduler.advanceUntilIdle()

        store.dispatch(BookmarksListMenuAction.Folder.EditClicked(folderItem))
        store.dispatch(EditFolderAction.TitleChanged("secrets"))
        store.dispatch(BackClicked)
        testScheduler.advanceUntilIdle()

        assertEquals(BookmarksGlobalResultReport.EditFolderFailed, reported)
    }

    @Test
    fun `GIVEN moving a bookmark item WHEN moving fails THEN result is reported`() = runTest {
        coEvery {
            bookmarksStorage.countBookmarksInTrees(
                listOf(
                    BookmarkRoot.Menu.id,
                    BookmarkRoot.Toolbar.id,
                    BookmarkRoot.Unfiled.id,
                ),
            )
        } returns 0u
        coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(
            generateBookmarkTree(),
        )
        coEvery { bookmarksStorage.updateNode(any(), any()) } returns Result.failure(
            IllegalStateException(),
        )
        val middleware = buildMiddleware(this)

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
        testScheduler.advanceUntilIdle()

        assertEquals(BookmarksSnackbarState.SelectFolderFailed, store.state.bookmarksSnackbarState)
    }

    @Test
    fun `while moving items and adding a new folder, returning from the add folder screen results in the move items being moved to the new folder`() =
        runTest {
            val tree = generateBookmarkTree()
            coEvery {
                bookmarksStorage.countBookmarksInTrees(
                    listOf(
                        BookmarkRoot.Menu.id,
                        BookmarkRoot.Toolbar.id,
                        BookmarkRoot.Unfiled.id,
                    ),
                )
            } returns 0u
            coEvery { bookmarksStorage.getTree(BookmarkRoot.Mobile.id) } returns Result.success(tree)

            val middleware = buildMiddleware(this)
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
            testScheduler.advanceUntilIdle()

            val parentNodeForNewFolder =
                tree.children!!.first { it.type == BookmarkNodeType.FOLDER }
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
            coEvery {
                bookmarksStorage.addFolder(
                    parentForNewFolder.guid,
                    newFolder.title!!,
                )
            } returns Result.success(newFolder.guid)
            coEvery { bookmarksStorage.getBookmark(newFolder.guid) } returns Result.success(
                newFolder,
            )

            coEvery { bookmarksStorage.getTree(parentForNewFolder.guid) } returns Result.success(
                parentNodeForNewFolder,
            )
            store.dispatch(SelectFolderAction.ChevronClicked(mobileRootSelectableItem))
            testScheduler.advanceUntilIdle()

            assertTrue((store.state.bookmarksSelectFolderState?.folders?.first()?.expansionState as? SelectFolderExpansionState.Open)?.children?.any { it.guid == parentForNewFolder.guid } == true)

            store.dispatch(
                SelectFolderAction.ItemClicked(
                    SelectFolderItem(
                        0,
                        parentForNewFolder,
                        SelectFolderExpansionState.None,
                    ),
                ),
            )
            testScheduler.advanceUntilIdle()

            assertNotNull(store.state.bookmarksSelectFolderState?.innerSelectionGuid)

            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            assertNull(store.state.bookmarksSelectFolderState?.innerSelectionGuid)
            assertEquals(parentForNewFolder, store.state.bookmarksAddFolderState?.parent)

            store.dispatch(AddFolderAction.TitleChanged(newFolder.title!!))
            store.dispatch(BackClicked)
            testScheduler.advanceUntilIdle()

            assertNull(store.state.bookmarksAddFolderState)
            assertNull(store.state.bookmarksSelectFolderState)
            assertNull(store.state.bookmarksMultiselectMoveState)
            guidsToMove.forEachIndexed { idx, guid ->
                coVerify(exactly = 1) {
                    bookmarksStorage.updateNode(
                        guid,
                        BookmarkInfo(
                            newFolder.guid,
                            null,
                            "item title ${idx + 1}",
                            "item url ${idx + 1}",
                        ),
                    )
                }
            }
            verify(exactly = 1) { navController.popBackStack(BookmarksDestinations.LIST, false) }
        }

    @Test
    fun `GIVEN a bookmark WHEN DeleteClicked is dispatched THEN delete from storage`() =
        runTest {
            val bookmarkGuid = "test-guid-123"

            val bookmarkItem = BookmarkItem.Bookmark(
                title = "Test Bookmark",
                guid = bookmarkGuid,
                url = "https://test.com",
                previewImageUrl = "",
                position = 0u,
            )

            coEvery { bookmarksStorage.deleteNode(bookmarkGuid) } returns Result.success(true)

            val middleware = buildMiddleware(this)
            val store = middleware.makeStore()
            testScheduler.advanceUntilIdle()

            store.dispatch(BookmarksListMenuAction.Bookmark.DeleteClicked(bookmarkItem))
            testScheduler.advanceUntilIdle()

            coVerify(exactly = 1) { bookmarksStorage.deleteNode(bookmarkGuid) }

            val isStillInList = store.state.bookmarkItems.any { it.guid == bookmarkGuid }
            assertFalse(isStillInList)
        }

    @Test
    fun `GIVEN Edit screen is open WHEN DeleteClicked is dispatched THEN delete bookmark and navigate back`() = runTest {
        val bookmarkGuid = "target-guid-123"

        val bookmark = BookmarkItem.Bookmark(
            title = "Bookmark to Delete",
            guid = bookmarkGuid,
            url = "https://example.com",
            previewImageUrl = "",
            position = 0u,
        )

        val initialState = BookmarksState.default.copy(
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = bookmark,
                folder = BookmarkItem.Folder("title", "guid", position = 0u),
            ),
        )

        coEvery { bookmarksStorage.deleteNode(bookmarkGuid) } returns Result.success(true)
        coEvery { navController.popBackStack() } returns true

        val store = buildMiddleware(this).makeStore(initialState)
        testScheduler.advanceUntilIdle()

        store.dispatch(EditBookmarkAction.DeleteClicked)
        testScheduler.advanceUntilIdle()

        coVerify(exactly = 1) { bookmarksStorage.deleteNode(bookmarkGuid) }
        verify(exactly = 1) { navController.popBackStack() }

        assertNull(store.state.bookmarksEditBookmarkState)
    }

    @Test
    fun `WHEN RootOverflowMenuClicked is dispatched THEN state is updated and no side effects occur`() = runTest {
        val store = buildMiddleware(this).makeStore()
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.rootMenuShown)
        store.dispatch(RootOverflowMenuClicked)
        testScheduler.advanceUntilIdle()

        assertTrue(store.state.rootMenuShown)
    }

    @Test
    fun `WHEN RootOverflowMenuDismissed is dispatched THEN rootMenuShown is set to false`() = runTest {
        val store = buildMiddleware(this).makeStore(BookmarksState.default.copy(rootMenuShown = true))
        testScheduler.advanceUntilIdle()

        store.dispatch(RootOverflowMenuDismissed)
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.rootMenuShown)
    }

    @Test
    fun `WHEN ImportFileClicked is dispatched from menu THEN rootMenuShown is false and launchFilePicker is true`() = runTest {
        val store = buildMiddleware(this).makeStore(BookmarksState.default.copy(rootMenuShown = true))
        testScheduler.advanceUntilIdle()

        store.dispatch(ImportAction.ImportFileClicked.FromMenu)
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.rootMenuShown)
    }

    private fun buildMiddleware(
        scope: CoroutineScope,
        openBookmarksInNewTab: Boolean = false,
        reportResultGlobally: (BookmarksGlobalResultReport) -> Unit = {},
    ) = BookmarksMiddleware(
        bookmarksStorage = bookmarksStorage,
        addNewTabUseCase = addNewTabUseCase,
        fenixBrowserUseCases = fenixBrowserUseCases,
        openBookmarksInNewTab = openBookmarksInNewTab,
        getNavController = { navController },
        exitBookmarks = exitBookmarks,
        navigateToBrowser = navigateToBrowser,
        navigateToSignIntoSync = navigateToSignIntoSync,
        navigateToImportDialog = navigateToImportDialog,
        shareBookmarks = shareBookmarks,
        showTabsTray = showTabsTray,
        resolveFolderTitle = resolveFolderTitle,
        getBrowsingMode = getBrowsingMode,
        saveBookmarkSortOrder = saveSortOrder,
        editBookmarkUseCase = BookmarksUseCase.EditBookmarkUseCase(bookmarksStorage, lastSavedFolderCache),
        reportResultGlobally = reportResultGlobally,
        importResults = { emptyFlow() },
        lifecycleScope = scope,
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
        generateBookmark(
            "item guid $it",
            "item title $it",
            "item url $it",
            position = startingPosition + it.toUInt(),
        )
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
            generateBookmarkFolder(
                BookmarkRoot.Menu.id,
                "Menu",
                BookmarkRoot.Root.id,
                position = 0u,
            ),
            generateBookmarkFolder(
                BookmarkRoot.Toolbar.id,
                "Toolbar",
                BookmarkRoot.Root.id,
                position = 1u,
            ),
            generateBookmarkFolder(
                BookmarkRoot.Unfiled.id,
                "Unfiled",
                BookmarkRoot.Root.id,
                position = 2u,
            ),
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

    private fun generateBookmarkFolder(
        guid: String,
        title: String,
        parentGuid: String,
        position: UInt,
    ) = BookmarkNode(
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

    private fun generateBookmark(
        guid: String,
        title: String?,
        url: String,
        position: UInt,
        lastModified: Long = 0,
    ) = BookmarkNode(
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

    private fun mockDesktopFoldersForSelectScreen() = runTest {
        coEvery {
            bookmarksStorage.getTree(
                BookmarkRoot.Root.id,
                recursive = false,
            )
        } returns Result.success(generateDesktopRootTree())
        coEvery {
            bookmarksStorage.getTree(
                BookmarkRoot.Mobile.id,
                recursive = false,
            )
        } returns Result.success(generateBookmarkTree())
        coEvery {
            bookmarksStorage.getTree(
                BookmarkRoot.Menu.id,
                recursive = false,
            )
        } returns Result.success(
            generateBookmarkFolder(
                BookmarkRoot.Menu.id,
                "Menu",
                BookmarkRoot.Root.id,
                position = 0u,
            ),
        )
        coEvery {
            bookmarksStorage.getTree(
                BookmarkRoot.Unfiled.id,
                recursive = false,
            )
        } returns Result.success(
            generateBookmarkFolder(
                BookmarkRoot.Unfiled.id,
                "Unfiled",
                BookmarkRoot.Root.id,
                position = 2u,
            ),
        )
    }
}
