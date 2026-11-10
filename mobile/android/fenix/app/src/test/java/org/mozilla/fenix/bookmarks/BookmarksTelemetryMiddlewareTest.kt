/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.lib.bookmark.parser.jsoup.BookmarksParseError
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.BookmarksManagement
import org.mozilla.fenix.helpers.FenixGleanTestRule

@RunWith(AndroidJUnit4::class)
class BookmarksTelemetryMiddlewareTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val middleware = BookmarksTelemetryMiddleware()

    @Test
    fun `GIVEN import failed action is received, an import failed event is recorded`() = runTest {
        val store = middleware.makeStore()

        store.dispatch(ImportAction.ImportFailed(error = FileImporterError.ParseError(BookmarksParseError.Unexpected)))

        testScheduler.advanceUntilIdle()

        val events = BookmarksManagement.importFailed.testGetValue() ?: emptyList()

        assertEquals(1, events.size, "Expected 1 import failed event, but got ${events.size}")
        assertEquals(
            expected = ImportErrorCode.PARSE_ERROR_UNKNOWN.toString(),
            actual = events.first().extra?.get("error_code"),
            message = "Expected error_code extra to match the unknown parse error code",
        )
    }

    @Test
    fun `GIVEN import failed actions with each error type, the matching error codes are recorded`() = runTest {
        val store = middleware.makeStore()
        val expectedCodes =
            listOf(
                FileImporterError.ReadError to ImportErrorCode.FILE_READ_ERROR,
                FileImporterError.ParseError(BookmarksParseError.UnsupportedContentType) to
                    ImportErrorCode.PARSE_ERROR_UNSUPPORTED_CONTENT_TYPE,
                FileImporterError.ParseError(BookmarksParseError.InvalidFormat) to
                    ImportErrorCode.PARSE_ERROR_INVALID_FORMAT,
                FileImporterError.ParseError(BookmarksParseError.Unexpected) to ImportErrorCode.PARSE_ERROR_UNKNOWN,
                FileImporterError.SaveError to ImportErrorCode.BOOKMARKS_SAVE_ERROR,
                FileImporterError.UnknownError to ImportErrorCode.UNKNOWN_ERROR,
            )

        expectedCodes.forEach { (error, _) -> store.dispatch(ImportAction.ImportFailed(error = error)) }
        testScheduler.advanceUntilIdle()

        val events = BookmarksManagement.importFailed.testGetValue() ?: emptyList()

        assertEquals(expectedCodes.map { it.second.toString() }, events.map { it.extra?.get("error_code") })
    }

    @Test
    fun `GIVEN import file clicked from menu, an import file button clicked event is recorded`() = runTest {
        val store = middleware.makeStore()

        store.dispatch(ImportAction.ImportFileClicked.FromMenu)

        testScheduler.advanceUntilIdle()

        val events = BookmarksManagement.importFromFileMenuClick.testGetValue() ?: emptyList()

        assertEquals(1, events.size, "Expected 1 import file menu clicked event, but got ${events.size}")
    }

    @Test
    fun `GIVEN import succeeded action is received, an import succeeded event is recorded with the bookmarks count`() =
        runTest {
            val store = middleware.makeStore()

            store.dispatch(ImportAction.ImportSucceeded(count = 2))

            testScheduler.advanceUntilIdle()

            val events = BookmarksManagement.importSuccessful.testGetValue() ?: emptyList()

            assertEquals(1, events.size, "Expected 1 import succeeded event, but got ${events.size}")
            assertEquals(
                expected = "2",
                actual = events.first().extra?.get("bookmarks_count"),
                message = "Expected bookmarks_count extra to be 2",
            )
        }

    @Test
    fun `GIVEN import file started, an import file started event is recorded`() = runTest {
        val store = middleware.makeStore()

        store.dispatch(ImportAction.ImportStarted)

        testScheduler.advanceUntilIdle()

        val events = BookmarksManagement.importStarted.testGetValue() ?: emptyList()

        assertEquals(1, events.size, "Expected 1 import file started event, but got ${events.size}")
    }

    @Test
    fun `GIVEN import file was cancelled, an import file cancelled event is recorded`() = runTest {
        val store = middleware.makeStore()

        store.dispatch(ImportAction.ImportCancelled)

        testScheduler.advanceUntilIdle()

        val events = BookmarksManagement.importCancelled.testGetValue() ?: emptyList()

        assertEquals(1, events.size, "Expected 1 import cancelled event, but got ${events.size}")
    }

    private fun BookmarksTelemetryMiddleware.makeStore(): BookmarksStore {
        return BookmarksStore(
            initialState = BookmarksState.default,
            middleware = listOf(this),
        )
    }
}
