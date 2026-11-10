/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.bookmark.parser.jsoup

import kotlin.test.assertIs
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.fileparser.FileParserException
import mozilla.components.concept.storage.bookmarks.InsertableBookmarkTreeNode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class JsoupBookmarksFileParserTest {

    private val testDispatcher = StandardTestDispatcher()
    private lateinit var parser: JsoupBookmarksFileParser

    @Before
    fun setup() {
        parser = JsoupBookmarksFileParser(ioDispatcher = testDispatcher)
    }

    // region parse() — public entry point

    @Test
    fun `GIVEN a valid bookmarks file content WHEN parse is called THEN result is success`() =
        runTest(testDispatcher) {
            // given the file with multiple bookmarks
            val fileContentStream = TestData.MULTIPLE_BOOKMARKS.byteInputStream()

            // when parse is called with valid content
            val result = parser.parse(fileContentStream)

            // then result is success
            assertTrue("Expected that the parsing result is a success", result.isSuccess)

            // and the parsed tree matches the expected structure
            assertEquals(
                InsertableBookmarkTreeNode.Folder(
                    title = "root",
                    dateAddedTimestamp = 0L,
                    lastModifiedTimestamp = 0L,
                    position = 0u,
                    children =
                        listOf(
                            InsertableBookmarkTreeNode.Item(
                                title = "One",
                                url = "https://one.com",
                                dateAddedTimestamp = 100,
                                lastModifiedTimestamp = 200,
                                position = 0u,
                            ),
                            InsertableBookmarkTreeNode.Item(
                                title = "Two",
                                url = "https://two.com",
                                dateAddedTimestamp = 300,
                                lastModifiedTimestamp = 400,
                                position = 1u,
                            ),
                            InsertableBookmarkTreeNode.Item(
                                title = "Three",
                                url = "https://three.com",
                                dateAddedTimestamp = 500,
                                lastModifiedTimestamp = 600,
                                position = 2u,
                            ),
                        ),
                ),
                result.getOrThrow().folder,
            )
        }

    @Test
    fun `GIVEN a valid and complex content WHEN parse is called THEN the bookmarks & folder counts are correct`() =
        runTest(testDispatcher) {
            // given the file with multiple bookmarks
            // when parse is called with valid content
            val result =
                parser.parse(inputStream = TestData.MULTIPLE_BOOKMARKS_FOLDERS_SEPARATORS_LEVELS.byteInputStream())
            val parseResult = result.getOrThrow()

            assertEquals(
                "Expected that 5 bookmarks were parsed",
                5,
                parseResult.bookmarksCount,
            )

            assertEquals(
                "Expected that 3 folders were parsed",
                3,
                parseResult.foldersCount,
            )
        }

    @Test
    fun `GIVEN a valid and complex content WHEN parse is called THEN the tree is built accordingly`() =
        runTest(testDispatcher) {
            // given the file with multiple bookmarks
            // when parse is called with valid content
            val result =
                parser.parse(inputStream = TestData.MULTIPLE_BOOKMARKS_FOLDERS_SEPARATORS_LEVELS.byteInputStream())
            val parseResult = result.getOrThrow()
            val tree = parseResult.folder

            assertEquals(
                InsertableBookmarkTreeNode.Folder(
                    title = "root",
                    dateAddedTimestamp = 0L,
                    lastModifiedTimestamp = 0L,
                    position = 0u,
                    children =
                        listOf(
                            InsertableBookmarkTreeNode.Item(
                                title = "Top Bookmark",
                                url = "https://top.com",
                                dateAddedTimestamp = 0L,
                                lastModifiedTimestamp = 0L,
                                position = 0u,
                            ),
                            InsertableBookmarkTreeNode.Separator(
                                dateAddedTimestamp = 0L,
                                lastModifiedTimestamp = 0L,
                                position = 1u,
                            ),
                            InsertableBookmarkTreeNode.Folder(
                                title = "Level 1",
                                dateAddedTimestamp = 0L,
                                lastModifiedTimestamp = 0L,
                                position = 2u,
                                children =
                                    listOf(
                                        InsertableBookmarkTreeNode.Item(
                                            title = "One",
                                            url = "https://one.com",
                                            dateAddedTimestamp = 0L,
                                            lastModifiedTimestamp = 0L,
                                            position = 0u,
                                        ),
                                        InsertableBookmarkTreeNode.Folder(
                                            title = "Level 2",
                                            dateAddedTimestamp = 0L,
                                            lastModifiedTimestamp = 0L,
                                            position = 1u,
                                            children =
                                                listOf(
                                                    InsertableBookmarkTreeNode.Folder(
                                                        title = "Level 3",
                                                        dateAddedTimestamp = 0L,
                                                        lastModifiedTimestamp = 0L,
                                                        position = 0u,
                                                        children =
                                                            listOf(
                                                                InsertableBookmarkTreeNode.Item(
                                                                    title = "Bottom",
                                                                    url = "https://bottom.com",
                                                                    dateAddedTimestamp = 0L,
                                                                    lastModifiedTimestamp = 0L,
                                                                    position = 0u,
                                                                )
                                                            ),
                                                    ),
                                                    InsertableBookmarkTreeNode.Separator(
                                                        dateAddedTimestamp = 0L,
                                                        lastModifiedTimestamp = 0L,
                                                        position = 1u,
                                                    ),
                                                    InsertableBookmarkTreeNode.Item(
                                                        title = "Two",
                                                        url = "https://two.com",
                                                        dateAddedTimestamp = 0L,
                                                        lastModifiedTimestamp = 0L,
                                                        position = 2u,
                                                    ),
                                                ),
                                        ),
                                    ),
                            ),
                            InsertableBookmarkTreeNode.Item(
                                title = "Last Bookmark",
                                url = "https://last.com",
                                dateAddedTimestamp = 0L,
                                lastModifiedTimestamp = 0L,
                                position = 3u,
                            ),
                        ),
                ),
                tree,
            )
        }

    @Test
    fun `GIVEN an invalid html content WHEN parse is called THEN a parsing error is returned`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.INVALID_HTML_CONTENT.byteInputStream())

            assertEquals(
                BookmarksParseError.UnsupportedContentType,
                (result.exceptionOrNull() as? FileParserException)?.errorType,
            )
        }

    @Test
    fun `GIVEN a valid HTML but invalid Netscape file content WHEN parse is called THEN a parsing error is returned `() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.VALID_HTML_BUT_INVALID_BOOKMARK_CONTENT.byteInputStream())

            assertEquals(
                BookmarksParseError.UnsupportedContentType,
                (result.exceptionOrNull() as? FileParserException)?.errorType,
            )
        }

    // endregion

    // region Bookmark items

    @Test
    fun `GIVEN a file with a bookmark WHEN parsed THEN item has correct url, title and position`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.SINGLE_BOOKMARK.byteInputStream())

            val root = result.getOrThrow().folder
            val item = root.children.first()

            assertIs<InsertableBookmarkTreeNode.Item>(item)
            assertEquals("https://example.com", item.url)
            assertEquals("Example", item.title)
            assertEquals(0u, item.position)
        }

    @Test
    fun `GIVEN a bookmark with ADD_DATE and LAST_MODIFIED WHEN parsed THEN timestamps are set`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.SINGLE_BOOKMARK.byteInputStream())

            val root = result.getOrThrow().folder
            val item = root.children.first()

            assertEquals(
                "Expected dateAddedTimestamp to be 1000 but got ${item.dateAddedTimestamp}",
                1000L,
                item.dateAddedTimestamp,
            )
            assertEquals(
                "Expected lastModifiedTimestamp to be 2000 but got ${item.lastModifiedTimestamp}",
                2000L,
                item.lastModifiedTimestamp,
            )
        }

    @Test
    fun `GIVEN a bookmark without timestamps WHEN parsed THEN timestamps default to 0`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.BOOKMARK_WITHOUT_TIMESTAMPS.byteInputStream())

            val root = result.getOrThrow().folder
            val item = root.children.first()

            assertEquals(
                "Expected dateAddedTimestamp to fallback to 0 but got ${item.dateAddedTimestamp}",
                0L,
                item.dateAddedTimestamp,
            )
            assertEquals(
                "Expected lastModifiedTimestamp to fallback to 0 but got ${item.lastModifiedTimestamp}",
                0L,
                item.lastModifiedTimestamp,
            )
        }

    @Test
    fun `GIVEN a bookmark with empty href WHEN parsed THEN result is failure with a parsing error`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.BOOKMARK_EMPTY_HREF.byteInputStream())

            assertTrue(
                "Expected result to be a failure but got ${result.getOrNull()}",
                result.isFailure,
            )
            assertEquals(
                BookmarksParseError.InvalidFormat,
                (result.exceptionOrNull() as? FileParserException)?.errorType,
            )
        }

    @Test
    fun `GIVEN a bookmark with empty text WHEN parsed THEN title is empty string`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.BOOKMARK_EMPTY_TEXT.byteInputStream())

            val root = result.getOrThrow().folder
            val item = root.children.first() as InsertableBookmarkTreeNode.Item

            assertEquals(
                "Expected title to be empty but got ${item.title}",
                "",
                item.title,
            )
        }

    @Test
    fun `GIVEN multiple bookmarks WHEN parsed THEN positions are sequential and 0-based`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.MULTIPLE_BOOKMARKS.byteInputStream())

            val root = result.getOrThrow().folder

            assertEquals(3, root.children.size)
            assertEquals(0u, root.children[0].position)
            assertEquals(1u, root.children[1].position)
            assertEquals(2u, root.children[2].position)
        }

    // endregion

    // region Folders

    @Test
    fun `GIVEN a folder WHEN parsed THEN folder has correct title, timestamps and children`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.FOLDER_WITH_BOOKMARK.byteInputStream())

            val root = result.getOrThrow().folder
            assertEquals(1, root.children.size)

            val folder = root.children.first()
            assertIs<InsertableBookmarkTreeNode.Folder>(folder)
            assertEquals("My Folder", folder.title)
            assertEquals(100L, folder.dateAddedTimestamp)
            assertEquals(200L, folder.lastModifiedTimestamp)
            assertEquals(0u, folder.position)

            assertEquals(1, folder.children.size)
            val item = folder.children.first() as InsertableBookmarkTreeNode.Item
            assertEquals("https://example.com", item.url)
            assertEquals("Example", item.title)
        }

    @Test
    fun `GIVEN nested folders WHEN parsed THEN tree structure is correct`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.NESTED_FOLDERS.byteInputStream())

            val root = result.getOrThrow().folder
            assertEquals(1, root.children.size)

            val outer = root.children.first() as InsertableBookmarkTreeNode.Folder
            assertEquals("Outer", outer.title)
            assertEquals(10L, outer.dateAddedTimestamp)
            assertEquals(20L, outer.lastModifiedTimestamp)
            assertEquals(1, outer.children.size)

            val inner = outer.children.first() as InsertableBookmarkTreeNode.Folder
            assertEquals("Inner", inner.title)
            assertEquals(30L, inner.dateAddedTimestamp)
            assertEquals(40L, inner.lastModifiedTimestamp)
            assertEquals(1, inner.children.size)

            val item = inner.children.first() as InsertableBookmarkTreeNode.Item
            assertEquals("https://deep.com", item.url)
            assertEquals("Deep Link", item.title)
        }

    @Test
    fun `GIVEN deeply nested folders WHEN parsed THEN all levels are represented`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.DEEPLY_NESTED_FOLDERS.byteInputStream())

            val root = result.getOrThrow().folder

            val level1 = root.children.first() as InsertableBookmarkTreeNode.Folder
            assertEquals(
                "Expected level 1 title to be Level 1, but got ${level1.title}",
                "Level 1",
                level1.title,
            )

            val level2 = level1.children.first() as InsertableBookmarkTreeNode.Folder
            assertEquals(
                "Expected level 2 title to be Level 2, but got ${level2.title}",
                "Level 2",
                level2.title,
            )

            val level3 = level2.children.first() as InsertableBookmarkTreeNode.Folder
            assertEquals(
                "Expected level 3 title to be Level 3, but got ${level3.title}",
                "Level 3",
                level3.title,
            )

            val item = level3.children.first() as InsertableBookmarkTreeNode.Item
            assertEquals(
                "Expected item url to be https://bottom.com, but got ${item.url}",
                "https://bottom.com",
                item.url,
            )
            assertEquals(
                "Expected item title to be Bottom, but got ${item.title}",
                "Bottom",
                item.title,
            )
        }

    @Test
    fun `GIVEN an empty folder WHEN parsed THEN folder has no children`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.EMPTY_FOLDER.byteInputStream())

            val root = result.getOrThrow().folder
            val folder = root.children.first() as InsertableBookmarkTreeNode.Folder

            assertEquals("Empty", folder.title)
            assertEquals(0, folder.children.size)
        }

    @Test
    fun `GIVEN a folder without timestamps WHEN parsed THEN timestamps default to 0`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.FOLDER_WITHOUT_TIMESTAMPS.byteInputStream())

            val root = result.getOrThrow().folder
            val folder = root.children.first() as InsertableBookmarkTreeNode.Folder

            assertEquals(
                "Expected dateAddedTimestamp to fallback to 0 but got ${folder.dateAddedTimestamp}",
                0L,
                folder.dateAddedTimestamp,
            )
            assertEquals(
                "Expected lastModifiedTimestamp to fallback to 0 but got ${folder.lastModifiedTimestamp}",
                0L,
                folder.lastModifiedTimestamp,
            )
        }

    // endregion

    // region Separators

    @Test
    fun `GIVEN a separator between bookmarks WHEN parsed THEN separator has correct position`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.SEPARATOR_BETWEEN_BOOKMARKS.byteInputStream())

            val root = result.getOrThrow().folder

            assertEquals(3, root.children.size)
            assertIs<InsertableBookmarkTreeNode.Item>(root.children[0])
            assertEquals(0u, root.children[0].position)

            assertIs<InsertableBookmarkTreeNode.Separator>(root.children[1])
            assertEquals(1u, root.children[1].position)

            assertIs<InsertableBookmarkTreeNode.Item>(root.children[2])
            assertEquals(2u, root.children[2].position)
        }

    @Test
    fun `GIVEN a separator with timestamps WHEN parsed THEN timestamps are set`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.SEPARATOR_WITH_TIMESTAMPS.byteInputStream())

            val root = result.getOrThrow().folder
            val separator = root.children.first()
            assertIs<InsertableBookmarkTreeNode.Separator>(separator)
            assertEquals(
                "Expected the date added timestamp to be 1000 " + "but got ${separator.dateAddedTimestamp}",
                1000L,
                separator.dateAddedTimestamp,
            )
            assertEquals(
                "Expected the last modified timestamp to be 2000 " + "but got ${separator.lastModifiedTimestamp}",
                2000L,
                separator.lastModifiedTimestamp,
            )
        }

    @Test
    fun `GIVEN a separator without timestamps WHEN parsed THEN timestamps falls back to 0`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.SEPARATOR_WITHOUT_TIMESTAMPS.byteInputStream())

            val parseResult = result.getOrThrow()
            val root = parseResult.folder

            val separator = root.children.first()
            assertEquals(
                "Expected the date added to fallback to 0 " + "but got ${separator.dateAddedTimestamp}",
                0L,
                separator.dateAddedTimestamp,
            )
            assertEquals(
                "Expected the last modified timestamp of to fallback to 0 " +
                    "but got ${separator.lastModifiedTimestamp}",
                0L,
                separator.lastModifiedTimestamp,
            )
        }

    // endregion

    // region Mixed content

    @Test
    fun `GIVEN bookmarks, folders and separators at the same level WHEN parsed THEN positions are sequential`() =
        runTest(testDispatcher) {
            val result = parser.parse(inputStream = TestData.MIXED_CONTENT.byteInputStream())

            val parseResult = result.getOrThrow()
            val root = parseResult.folder

            assertTrue(
                "Child at index 0 should have position 0",
                root.children[0].position == 0u,
            )
            assertTrue(
                "Child at index 1 should have position 1",
                root.children[1].position == 1u,
            )
            assertTrue(
                "Child at index 2 should have position 2",
                root.children[2].position == 2u,
            )
            assertTrue(
                "Child at index 3 should have position 3",
                root.children[3].position == 3u,
            )
        }

    // endregion

    // region Test helpers

    private fun assertEquals(expected: UInt, actual: UInt?) {
        assertTrue("Expected $expected, got $actual", actual == expected)
    }

    // endregion
}
