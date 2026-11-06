/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.storage.sync

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.concurrent.TimeUnit
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.appservices.places.BookmarkRoot
import mozilla.appservices.places.uniffi.PlacesApiException
import mozilla.components.concept.storage.BookmarkInfo
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.concept.storage.bookmarks.InsertableBookmarkTreeNode
import mozilla.components.concept.storage.bookmarks.InsertableBookmarkTreeRoot
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class PlacesBookmarksStorageTest {

    private lateinit var bookmarks: PlacesBookmarksStorage

    @Before
    fun setup() = runTest {
        // Query 100ms in the future so the maxAge recency assertions are deterministic against
        // bookmarks inserted moments earlier in the same test.
        bookmarks = PlacesBookmarksStorage(testContext, currentTimeMillis = { System.currentTimeMillis() + 100 })
        // There's a database on disk which needs to be cleaned up between tests.
        bookmarks.writer.deleteEverything()
    }

    @After
    @Suppress("DEPRECATION")
    fun cleanup() = runTest {
        bookmarks.cleanup()
    }

    @Test
    fun `get bookmarks tree by root, recursive or not`() = runTest {
        val tree = bookmarks.getTree(BookmarkRoot.Root.id).getOrNull()
        assertEquals(BookmarkRoot.Root.id, tree!!.guid)
        assertNotNull(tree.children)
        assertEquals(4, tree.children!!.size)

        var children = tree.children!!.map { it.guid }
        assertTrue(BookmarkRoot.Mobile.id in children)
        assertTrue(BookmarkRoot.Unfiled.id in children)
        assertTrue(BookmarkRoot.Toolbar.id in children)
        assertTrue(BookmarkRoot.Menu.id in children)

        // Non-recursive means children of children aren't fetched.
        for (child in tree.children!!) {
            assertNull(child.children)
            assertEquals(BookmarkRoot.Root.id, child.parentGuid)
            assertEquals(BookmarkNodeType.FOLDER, child.type)
        }

        val deepTree = bookmarks.getTree(BookmarkRoot.Root.id, true).getOrNull()
        assertEquals(BookmarkRoot.Root.id, deepTree!!.guid)
        assertNotNull(deepTree.children)
        assertEquals(4, deepTree.children!!.size)

        children = deepTree.children!!.map { it.guid }
        assertTrue(BookmarkRoot.Mobile.id in children)
        assertTrue(BookmarkRoot.Unfiled.id in children)
        assertTrue(BookmarkRoot.Toolbar.id in children)
        assertTrue(BookmarkRoot.Menu.id in children)

        // Recursive means children of children are fetched.
        for (child in deepTree.children) {
            // For an empty tree, we expect to see empty lists.
            assertEquals(emptyList<BookmarkNode>(), child.children)
            assertEquals(BookmarkRoot.Root.id, child.parentGuid)
            assertEquals(BookmarkNodeType.FOLDER, child.type)
        }
    }

    @Test
    fun `bookmarks replace lone surrogates in titles`() = runTest {
        val itemGuid =
            bookmarks.addItem(BookmarkRoot.Mobile.id, "http://www.mozilla.org", "Mozilla \uD83D", null).getOrNull()!!
        assertEquals("Mozilla \uFFFD", bookmarks.getBookmark(itemGuid).getOrNull()!!.title)

        bookmarks.updateNode(
            itemGuid,
            BookmarkInfo(parentGuid = null, position = null, title = "\uDE00 Moz", url = null),
        )
        assertEquals("\uFFFD Moz", bookmarks.getBookmark(itemGuid).getOrNull()!!.title)

        val folderGuid = bookmarks.addFolder(BookmarkRoot.Mobile.id, "Folder \uDE00", null).getOrNull()!!
        assertEquals("Folder \uFFFD", bookmarks.getBookmark(folderGuid).getOrNull()!!.title)
    }

    @Test
    fun `bookmarks APIs smoke testing - basic operations`() = runTest {
        val url = "http://www.mozilla.org"

        assertEquals(emptyList<BookmarkNode>(), bookmarks.getBookmarksWithUrl(url).getOrNull())
        assertEquals(emptyList<BookmarkNode>(), bookmarks.searchBookmarks("mozilla").getOrNull())

        val insertedItem = bookmarks.addItem(BookmarkRoot.Mobile.id, url, "Mozilla", 5u).getOrNull()!!

        with(bookmarks.getBookmarksWithUrl(url).getOrNull()!!) {
            assertEquals(1, this.size)
            with(this[0]) {
                assertEquals(insertedItem, this.guid)
                assertEquals(BookmarkNodeType.ITEM, this.type)
                assertEquals("Mozilla", this.title)
                assertEquals(BookmarkRoot.Mobile.id, this.parentGuid)
                // Clamped to actual range. 'Mobile' was empty, so we get 0 back.
                assertEquals(0U, this.position)
                assertEquals("http://www.mozilla.org/", this.url)
            }
        }

        val folderGuid = bookmarks.addFolder(BookmarkRoot.Mobile.id, "Test Folder", null).getOrNull()!!
        bookmarks.updateNode(
            insertedItem,
            BookmarkInfo(
                parentGuid = folderGuid,
                title = null,
                position = 9999u,
                url = null,
            ),
        )
        with(bookmarks.getBookmarksWithUrl(url).getOrNull()!!) {
            assertEquals(1, this.size)
            with(this[0]) {
                assertEquals(insertedItem, this.guid)
                assertEquals(BookmarkNodeType.ITEM, this.type)
                assertEquals("Mozilla", this.title)
                assertEquals(folderGuid, this.parentGuid)
                assertEquals(0u, this.position)
                assertEquals("http://www.mozilla.org/", this.url)
            }
        }

        val separatorGuid = bookmarks.addSeparator(folderGuid, 1u).getOrNull()!!
        with(bookmarks.getTree(folderGuid).getOrNull()!!) {
            assertEquals(2, this.children!!.size)
            assertEquals(BookmarkNodeType.SEPARATOR, this.children!![1].type)
        }

        assertTrue(bookmarks.deleteNode(separatorGuid).getOrNull()!!)
        with(bookmarks.getTree(folderGuid).getOrNull()) {
            assertEquals(1, this!!.children!!.size)
            assertEquals(BookmarkNodeType.ITEM, this.children!![0].type)
        }

        with(bookmarks.searchBookmarks("mozilla").getOrNull()!!) {
            assertEquals(1, this.size)
            assertEquals("http://www.mozilla.org/", this[0].url)
        }

        with(bookmarks.getBookmark(folderGuid).getOrNull()) {
            assertEquals(folderGuid, this!!.guid)
            assertEquals("Test Folder", this.title)
            assertEquals(BookmarkRoot.Mobile.id, this.parentGuid)
        }

        with(bookmarks.getRecentBookmarks(1).getOrNull()!!) {
            assertEquals(insertedItem, this[0].guid)
        }

        with(bookmarks.getRecentBookmarks(1, TimeUnit.DAYS.toMillis(1)).getOrNull()!!) {
            assertEquals(insertedItem, this[0].guid)
        }

        with(bookmarks.getRecentBookmarks(1, 99).getOrNull()!!) {
            assertTrue(this.isEmpty())
        }

        val secondInsertedItem = bookmarks.addItem(BookmarkRoot.Unfiled.id, url, "Mozilla", 6u).getOrNull()!!

        with(bookmarks.getRecentBookmarks(2).getOrNull()!!) {
            assertEquals(secondInsertedItem, this[0].guid)
            assertEquals(insertedItem, this[1].guid)
        }

        with(bookmarks.getRecentBookmarks(2, TimeUnit.DAYS.toMillis(1)).getOrNull()!!) {
            assertEquals(secondInsertedItem, this[0].guid)
            assertEquals(insertedItem, this[1].guid)
        }

        with(bookmarks.getRecentBookmarks(2, 99).getOrNull()!!) {
            assertTrue(this.isEmpty())
        }

        assertTrue(bookmarks.deleteNode(secondInsertedItem).getOrNull()!!)
        assertTrue(bookmarks.deleteNode(folderGuid).getOrNull()!!)

        for (root in
            listOf(
                BookmarkRoot.Mobile,
                BookmarkRoot.Root,
                BookmarkRoot.Menu,
                BookmarkRoot.Toolbar,
                BookmarkRoot.Unfiled,
            )) {
            try {
                if (bookmarks.deleteNode(root.id).isSuccess) {
                    fail("Expected root deletion for ${root.id} to fail")
                }
            } catch (e: PlacesApiException.InvalidBookmarkOperation) {
                // Expected
            }
        }

        with(bookmarks.searchBookmarks("mozilla").getOrDefault(listOf())) {
            assertTrue(this.isEmpty())
        }
    }

    @Test
    fun `insertTree inserts a folder with items and reads them back`() = runTest {
        val tree =
            InsertableBookmarkTreeRoot(
                parentGuid = BookmarkRoot.Mobile.id,
                rootFolder =
                    InsertableBookmarkTreeNode.Folder(
                        title = "Imported",
                        dateAddedTimestamp = 0L,
                        lastModifiedTimestamp = 0L,
                        position = null,
                        children =
                            listOf(
                                InsertableBookmarkTreeNode.Item(
                                    title = "Mozilla",
                                    url = "https://www.mozilla.org/",
                                    dateAddedTimestamp = 0L,
                                    lastModifiedTimestamp = 0L,
                                    position = 0u,
                                ),
                                InsertableBookmarkTreeNode.Item(
                                    title = "Firefox",
                                    url = "https://www.firefox.com/",
                                    dateAddedTimestamp = 0L,
                                    lastModifiedTimestamp = 0L,
                                    position = 1u,
                                ),
                            ),
                    ),
            )

        val rootGuid = bookmarks.insertTree(tree).getOrThrow()
        assertNotNull(rootGuid)

        val insertedFolder = bookmarks.getTree(rootGuid).getOrNull()
        assertNotNull(insertedFolder)
        assertEquals("Imported", insertedFolder.title)
        assertEquals(BookmarkNodeType.FOLDER, insertedFolder.type)
        assertEquals(BookmarkRoot.Mobile.id, insertedFolder.parentGuid)
        assertEquals(2, insertedFolder.children!!.size)

        val firstChild = insertedFolder.children!![0]
        assertEquals("Mozilla", firstChild.title)
        assertEquals("https://www.mozilla.org/", firstChild.url)
        assertEquals(BookmarkNodeType.ITEM, firstChild.type)

        val secondChild = insertedFolder.children!![1]
        assertEquals("Firefox", secondChild.title)
        assertEquals("https://www.firefox.com/", secondChild.url)
        assertEquals(BookmarkNodeType.ITEM, secondChild.type)
    }

    @Test
    fun `insertTree inserts nested folders`() = runTest {
        val tree =
            InsertableBookmarkTreeRoot(
                parentGuid = BookmarkRoot.Unfiled.id,
                rootFolder =
                    InsertableBookmarkTreeNode.Folder(
                        title = "Outer",
                        dateAddedTimestamp = 0L,
                        lastModifiedTimestamp = 0L,
                        position = null,
                        children =
                            listOf(
                                InsertableBookmarkTreeNode.Folder(
                                    title = "Inner",
                                    dateAddedTimestamp = 0L,
                                    lastModifiedTimestamp = 0L,
                                    position = null,
                                    children =
                                        listOf(
                                            InsertableBookmarkTreeNode.Item(
                                                title = "Deep Link",
                                                url = "https://example.com/",
                                                dateAddedTimestamp = 0L,
                                                lastModifiedTimestamp = 0L,
                                                position = null,
                                            )
                                        ),
                                )
                            ),
                    ),
            )

        val rootGuid = bookmarks.insertTree(tree).getOrThrow()
        val outerFolder = bookmarks.getTree(rootGuid, true).getOrNull()
        assertNotNull(outerFolder)
        assertEquals("Outer", outerFolder.title)
        assertEquals(1, outerFolder.children!!.size)

        val innerFolder = outerFolder.children!![0]
        assertEquals("Inner", innerFolder.title)
        assertEquals(BookmarkNodeType.FOLDER, innerFolder.type)
        assertEquals(1, innerFolder.children!!.size)

        val deepItem = innerFolder.children!![0]
        assertEquals("Deep Link", deepItem.title)
        assertEquals("https://example.com/", deepItem.url)
        assertEquals(BookmarkNodeType.ITEM, deepItem.type)
    }

    @Test
    fun `insertTree inserts an empty folder`() = runTest {
        val tree =
            InsertableBookmarkTreeRoot(
                parentGuid = BookmarkRoot.Mobile.id,
                rootFolder =
                    InsertableBookmarkTreeNode.Folder(
                        title = "Empty Folder",
                        dateAddedTimestamp = 0L,
                        lastModifiedTimestamp = 0L,
                        position = null,
                        children = emptyList(),
                    ),
            )

        val rootGuid = bookmarks.insertTree(tree).getOrThrow()
        val folder = bookmarks.getTree(rootGuid).getOrNull()
        assertNotNull(folder)
        assertEquals("Empty Folder", folder.title)
        assertEquals(BookmarkNodeType.FOLDER, folder.type)
        assertEquals(emptyList<BookmarkNode>(), folder.children)
    }

    @Test
    fun `GIVEN bookmarks exist WHEN asked for autocomplete suggestions THEN return the first matching bookmark`() =
        runTest {
            bookmarks.apply {
                addItem(BookmarkRoot.Mobile.id, "https://www.mozilla.org/en-us/firefox", "Mozilla", 5u)
                addItem(BookmarkRoot.Toolbar.id, "https://support.mozilla.org/", "Support", 2u)
            }

            // Try querying for a bookmarks that doesn't exist
            var suggestion = bookmarks.getAutocompleteSuggestion("test")
            assertNull(suggestion)

            // And now for ones that do exist
            suggestion = bookmarks.getAutocompleteSuggestion("moz")
            assertNotNull(suggestion)
            assertEquals("moz", suggestion.input)
            // There are multiple bookmarks from the mozilla host with no guarantee about the read order.
            // Use a smaller URL that would match all.
            assertTrue(suggestion.text.startsWith("mozilla.org/en-us/"))
            assertTrue(suggestion.url.startsWith("https://www.mozilla.org/en-us/"))
            assertEquals(BOOKMARKS_AUTOCOMPLETE_SOURCE_NAME, suggestion.source)
            assertEquals(1, suggestion.totalItems)

            suggestion = bookmarks.getAutocompleteSuggestion("sup")
            assertNotNull(suggestion)
            assertEquals("sup", suggestion.input)
            assertEquals("support.mozilla.org/", suggestion.text)
            assertEquals("https://support.mozilla.org/", suggestion.url)
            assertEquals(BOOKMARKS_AUTOCOMPLETE_SOURCE_NAME, suggestion.source)
            assertEquals(1, suggestion.totalItems)
        }
}
