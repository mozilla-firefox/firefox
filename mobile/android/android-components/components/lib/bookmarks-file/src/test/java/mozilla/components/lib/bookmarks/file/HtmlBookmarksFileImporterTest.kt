/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.bookmarks.file

import android.net.Uri
import java.io.ByteArrayInputStream
import java.io.IOException
import kotlin.test.assertIs
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.concept.fileimporter.FileImporterException
import mozilla.components.concept.fileparser.FileParser
import mozilla.components.concept.fileparser.FileParserException
import mozilla.components.concept.storage.bookmarks.BookmarkInserter
import mozilla.components.concept.storage.bookmarks.InsertableBookmarkTreeNode
import mozilla.components.concept.storage.bookmarks.InsertableBookmarkTreeRoot
import mozilla.components.lib.bookmark.parser.jsoup.BookmarksParseError
import mozilla.components.lib.bookmark.parser.jsoup.BookmarksParseResult
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@OptIn(ExperimentalCoroutinesApi::class) // advanceTimeBy
@RunWith(RobolectricTestRunner::class)
class HtmlBookmarksFileImporterTest {
    private val testUri = Uri.EMPTY

    @Test
    fun `importFromUri returns correct count`() = runTest {
        val importer = createImporter()

        val result = importer.importFromUri(testUri).getOrThrow()

        assertEquals(3, result.count)
    }

    @Test
    fun `importFromUri returns guid from inserter`() = runTest {
        val importer = createImporter(inserter = fakeInserter(guid = "root-guid"))

        val result = importer.importFromUri(testUri).getOrThrow()

        assertEquals("root-guid", result.guid)
    }

    @Test
    fun `importFromUri passes parsed tree to inserter`() = runTest {
        var insertedTree: InsertableBookmarkTreeRoot? = null
        val importer = createImporter(inserter = fakeInserter { insertedTree = it })

        importer.importFromUri(testUri).getOrThrow()

        assertEquals("parentGuid", insertedTree!!.parentGuid)
    }

    @Test
    fun `importFromUri returns file read error when uriOpener fails`() = runTest {
        val importer = createImporter(uriOpener = { Result.failure(IOException("cannot open")) })

        val result = importer.importFromUri(testUri)

        assertTrue("Expected the result to be failure, but got success", result.isFailure)
        assertEquals(FileImporterError.ReadError, (result.exceptionOrNull() as? FileImporterException)?.errorType)
    }

    @Test
    fun `importFromUri returns file parsing error when parser fails`() = runTest {
        val importer = createImporter(parser = FileParser.fakeFailure())

        val result = importer.importFromUri(testUri)

        assertTrue("Expected the result to be failure, but got success", result.isFailure)
        assertEquals(
            FileImporterError.ParseError(BookmarksParseError.Unexpected),
            (result.exceptionOrNull() as? FileImporterException)?.errorType,
        )
    }

    @Test
    fun `GIVEN the parser fails with a FileParserException WHEN importing from a uri THEN the parse error type is preserved`() =
        runTest {
            val importer =
                createImporter(parser = FileParser.fakeFailure(FileParserException(BookmarksParseError.InvalidFormat)))

            val result = importer.importFromUri(testUri)

            assertTrue(result.isFailure)
            assertEquals(
                FileImporterError.ParseError(BookmarksParseError.InvalidFormat),
                (result.exceptionOrNull() as? FileImporterException)?.errorType,
            )
        }

    @Test
    fun `importFromUri returns bookmarks insert error when inserter fails`() = runTest {
        val importer =
            createImporter(inserter = fakeInserter(result = Result.failure(RuntimeException("insert failed"))))

        val result = importer.importFromUri(testUri)

        assertTrue("Expected the result to be failure, but got success", result.isFailure)
        assertEquals(FileImporterError.SaveError, (result.exceptionOrNull() as? FileImporterException)?.errorType)
    }

    @Test
    fun `importFromUri does not swallow cancellation when the job is cancelled before the result`() = runTest {
        // Given that the parsing takes 10 seconds
        val importer =
            createImporter(
                uriOpener =
                    UriOpener {
                        Result.success(
                            """
                            <!DOCTYPE NETSCAPE-Bookmark-file-1>
                            <HTML>
                            <DL><p>
                              <DT><A HREF="https://example.com" ADD_DATE="1000" LAST_MODIFIED="2000">Example</A>
                            </DL>
                            """
                                .trimIndent()
                                .byteInputStream()
                        )
                    },
                parser =
                    FileParser {
                        delay(10.seconds)
                        Result.failure(Throwable("Unable to parse"))
                    },
                inserter =
                    BookmarkInserter {
                        Result.failure(Throwable("Unable to insert"))
                    },
            )
        var caughtException: Throwable? = null

        // When we import bookmarks
        val job =
            launch(UnconfinedTestDispatcher(testScheduler)) {
                try {
                    importer.importFromUri(testUri)
                } catch (e: Throwable) {
                    caughtException = e
                }
            }

        // And the job is canceled 1 second into execution
        advanceTimeBy(1.seconds)
        job.cancel()
        advanceUntilIdle()

        // Then verify that expectedException is a cancellation exception
        assertIs<CancellationException>(caughtException)
    }

    private val fakeParseResult =
        BookmarksParseResult(
            foldersCount = 1,
            bookmarksCount = 3,
            folder =
                InsertableBookmarkTreeNode.Folder(
                    title = "Bookmarks",
                    position = 0u,
                    dateAddedTimestamp = 0L,
                    lastModifiedTimestamp = 0L,
                    children = emptyList(),
                ),
        )

    private fun fakeInserter(
        guid: String = "guid",
        result: Result<String>? = null,
        onInsert: (InsertableBookmarkTreeRoot) -> Unit = {},
    ) = BookmarkInserter { tree ->
        onInsert(tree)
        result ?: Result.success(guid)
    }

    private fun createImporter(
        parentGuid: String = "parentGuid",
        uriOpener: UriOpener = UriOpener { Result.success(ByteArrayInputStream(ByteArray(0))) },
        parser: FileParser<BookmarksParseResult> = FileParser.fakeSuccess(fakeParseResult),
        inserter: BookmarkInserter = fakeInserter(),
    ) =
        HtmlBookmarksFileImporter(
            parentGuid = parentGuid,
            uriOpener = uriOpener,
            parser = parser,
            inserter = inserter,
        )
}
