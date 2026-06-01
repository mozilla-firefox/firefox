/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.bookmarks

import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.slot
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.cancel
import kotlinx.coroutines.test.runTest
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.concept.storage.BookmarkInfo
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarkNodeType
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.concept.storage.HistoryStorage
import mozilla.components.concept.storage.VisitInfo
import mozilla.components.concept.storage.VisitType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.home.bookmarks.Bookmark
import java.util.concurrent.TimeUnit
import kotlin.test.assertNotNull

class BookmarksUseCaseTest {

    private fun folder(guid: String, title: String = "Folder $guid") = BookmarkNode(
        type = BookmarkNodeType.FOLDER,
        guid = guid,
        parentGuid = null,
        position = 0u,
        title = title,
        url = null,
        dateAdded = 0,
        lastModified = 0,
        children = emptyList(),
    )

    @Test
    fun `WHEN adding existing bookmark THEN no new item is stored`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>()
        val historyStorage = mockk<HistoryStorage>()
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val mobileRoot = folder(BookmarkRoot.Mobile.id)
        val existing = mockk<BookmarkNode>()
        val useCase = BookmarksUseCase(bookmarksStorage, historyStorage, lastSavedFolderCache)

        every { existing.url }.answers { "https://mozilla.org" }
        coEvery { lastSavedFolderCache.getGuid() } returns null
        coEvery { bookmarksStorage.getBookmark(BookmarkRoot.Mobile.id) } returns Result.success(mobileRoot)
        coEvery { bookmarksStorage.getBookmarksWithUrl(any()) }.coAnswers { Result.success(listOf(existing)) }

        val result = useCase.addBookmark("https://mozilla.org", "Mozilla")

        assertNull(result.guidToEdit)
        assertSame(mobileRoot, result.parentNode)
    }

    @Test
    fun `GIVEN cache is empty WHEN adding bookmark THEN new item is stored under Mobile root and cache is untouched`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>()
        val historyStorage = mockk<HistoryStorage>(relaxed = true)
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val mobileRoot = folder(BookmarkRoot.Mobile.id)
        val useCase = BookmarksUseCase(bookmarksStorage, historyStorage, lastSavedFolderCache)

        coEvery { lastSavedFolderCache.getGuid() } returns null
        coEvery { bookmarksStorage.getBookmark(BookmarkRoot.Mobile.id) } returns Result.success(mobileRoot)
        coEvery { bookmarksStorage.getBookmarksWithUrl(eq("https://mozilla.org")) }.coAnswers { Result.success(listOf()) }
        coEvery { bookmarksStorage.addItem(any(), any(), any(), any()) } returns Result.success("id")

        val result = useCase.addBookmark("https://mozilla.org", "Mozilla")

        assertEquals("id", result.guidToEdit)
        assertSame(mobileRoot, result.parentNode)
        coVerify { bookmarksStorage.addItem(BookmarkRoot.Mobile.id, "https://mozilla.org", "Mozilla", null) }
        coVerify(exactly = 0) { lastSavedFolderCache.setGuid(any()) }
    }

    @Test
    fun `GIVEN cache holds a valid folder WHEN adding bookmark THEN new item is stored under that folder and cache is untouched`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>()
        val historyStorage = mockk<HistoryStorage>(relaxed = true)
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val cachedFolder = folder("cached-folder", "Cached")
        val useCase = BookmarksUseCase(bookmarksStorage, historyStorage, lastSavedFolderCache)

        coEvery { lastSavedFolderCache.getGuid() } returns "cached-folder"
        coEvery { bookmarksStorage.getBookmark("cached-folder") } returns Result.success(cachedFolder)
        coEvery { bookmarksStorage.getBookmarksWithUrl(eq("https://mozilla.org")) }.coAnswers { Result.success(listOf()) }
        coEvery { bookmarksStorage.addItem(any(), any(), any(), any()) } returns Result.success("id")

        val result = useCase.addBookmark("https://mozilla.org", "Mozilla")

        assertEquals("id", result.guidToEdit)
        assertSame(cachedFolder, result.parentNode)
        coVerify { bookmarksStorage.addItem("cached-folder", "https://mozilla.org", "Mozilla", null) }
        coVerify(exactly = 0) { lastSavedFolderCache.setGuid(any()) }
    }

    @Test
    fun `GIVEN cache holds a folder that no longer exists WHEN adding bookmark THEN falls back to Mobile root and cache is cleared`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>()
        val historyStorage = mockk<HistoryStorage>(relaxed = true)
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val mobileRoot = folder(BookmarkRoot.Mobile.id)
        val useCase = BookmarksUseCase(bookmarksStorage, historyStorage, lastSavedFolderCache)

        coEvery { lastSavedFolderCache.getGuid() } returns "stale-folder"
        coEvery { bookmarksStorage.getBookmark("stale-folder") } returns Result.success(null)
        coEvery { bookmarksStorage.getBookmark(BookmarkRoot.Mobile.id) } returns Result.success(mobileRoot)
        coEvery { bookmarksStorage.getBookmarksWithUrl(eq("https://mozilla.org")) }.coAnswers { Result.success(listOf()) }
        coEvery { bookmarksStorage.addItem(any(), any(), any(), any()) } returns Result.success("id")

        val result = useCase.addBookmark("https://mozilla.org", "Mozilla")

        assertEquals("id", result.guidToEdit)
        assertSame(mobileRoot, result.parentNode)
        coVerify { bookmarksStorage.addItem(BookmarkRoot.Mobile.id, "https://mozilla.org", "Mozilla", null) }
        coVerify(exactly = 1) { lastSavedFolderCache.setGuid(null) }
    }

    @Test
    fun `GIVEN explicit parentGuid WHEN adding bookmark THEN parent is honored and cache is untouched`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>()
        val historyStorage = mockk<HistoryStorage>(relaxed = true)
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val explicitFolder = folder("explicit", "Explicit")
        val useCase = BookmarksUseCase(bookmarksStorage, historyStorage, lastSavedFolderCache)

        coEvery { bookmarksStorage.getBookmark("explicit") } returns Result.success(explicitFolder)
        coEvery { bookmarksStorage.getBookmarksWithUrl(eq("https://mozilla.org")) }.coAnswers { Result.success(listOf()) }
        coEvery { bookmarksStorage.addItem(any(), any(), any(), any()) } returns Result.success("id")

        val result = useCase.addBookmark("https://mozilla.org", "Mozilla", parentGuid = "explicit")

        assertEquals("id", result.guidToEdit)
        assertSame(explicitFolder, result.parentNode)
        coVerify { bookmarksStorage.addItem("explicit", "https://mozilla.org", "Mozilla", null) }
        coVerify(exactly = 0) { lastSavedFolderCache.getGuid() }
        coVerify(exactly = 0) { lastSavedFolderCache.setGuid(any()) }
    }

    @Test
    fun `GIVEN edited is true and update succeeds WHEN editing a bookmark THEN cache is updated to the new parent`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>()
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val info = BookmarkInfo(parentGuid = "new-folder", position = null, title = "t", url = "u")
        val useCase = BookmarksUseCase.EditBookmarkUseCase(bookmarksStorage, lastSavedFolderCache)

        coEvery { bookmarksStorage.updateNode("bm", info) } returns Result.success(Unit)

        val success = useCase(guid = "bm", info = info, edited = true)

        assertTrue(success)
        coVerify(exactly = 1) { lastSavedFolderCache.setGuid("new-folder") }
    }

    @Test
    fun `GIVEN edited is false WHEN editing a bookmark THEN cache is not touched`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>()
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val info = BookmarkInfo(parentGuid = "p", position = null, title = "t", url = "u")
        val useCase = BookmarksUseCase.EditBookmarkUseCase(bookmarksStorage, lastSavedFolderCache)

        coEvery { bookmarksStorage.updateNode("bm", info) } returns Result.success(Unit)

        useCase(guid = "bm", info = info, edited = false)

        coVerify(exactly = 0) { lastSavedFolderCache.setGuid(any()) }
    }

    @Test
    fun `GIVEN update fails WHEN editing a bookmark THEN cache is not touched and false is returned`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>()
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val info = BookmarkInfo(parentGuid = "p", position = null, title = "t", url = "u")
        val useCase = BookmarksUseCase.EditBookmarkUseCase(bookmarksStorage, lastSavedFolderCache)

        coEvery { bookmarksStorage.updateNode("bm", info) } returns Result.failure(IllegalStateException())

        val success = useCase(guid = "bm", info = info, edited = true)

        assertFalse(success)
        coVerify(exactly = 0) { lastSavedFolderCache.setGuid(any()) }
    }

    @Test
    fun `GIVEN caller scope is cancelled mid-update WHEN editing a bookmark THEN cache is still updated`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>()
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val info = BookmarkInfo(parentGuid = "new-folder", position = null, title = "t", url = "u")
        val useCase = BookmarksUseCase.EditBookmarkUseCase(bookmarksStorage, lastSavedFolderCache)

        val updateStarted = CompletableDeferred<Unit>()
        val updateGate = CompletableDeferred<Unit>()
        coEvery { bookmarksStorage.updateNode("bm", info) } coAnswers {
            updateStarted.complete(Unit)
            updateGate.await()
            Result.success(Unit)
        }

        val callerScope = CoroutineScope(coroutineContext + Job())
        val deferred = callerScope.async { useCase(guid = "bm", info = info, edited = true) }

        updateStarted.await()
        callerScope.cancel()
        updateGate.complete(Unit)
        testScheduler.advanceUntilIdle()

        coVerify(exactly = 1) { lastSavedFolderCache.setGuid("new-folder") }
        // The async result itself is cancelled even though the work inside completed.
        assertTrue(deferred.isCancelled)
    }

    @Test
    fun `WHEN saved bookmarks exist THEN retrieve the list from storage using limited history`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>(relaxed = true)
        val historyStorage = mockk<HistoryStorage>(relaxed = true)
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val useCase = BookmarksUseCase(bookmarksStorage, historyStorage, lastSavedFolderCache)
        val historyTimeFrameSlot = slot<Long>()

        val visitInfo = VisitInfo(
            url = "https://www.firefox.com",
            title = "firefox",
            visitTime = 2,
            visitType = VisitType.LINK,
            previewImageUrl = "http://firefox.com/image1",
            isRemote = false,
        )
        val bookmarkNode = BookmarkNode(
            BookmarkNodeType.ITEM,
            "987",
            "123",
            2u,
            "Firefox",
            "https://www.firefox.com",
            0,
            0,
            null,
        )

        coEvery {
            historyStorage.getDetailedVisits(capture(historyTimeFrameSlot), any())
        }.coAnswers { listOf(visitInfo) }

        coEvery {
            bookmarksStorage.getRecentBookmarks(
                any(),
                any(),
                any(),
            )
        }.coAnswers { Result.success(listOf(bookmarkNode)) }

        val result = useCase.retrieveRecentBookmarks(BookmarksUseCase.DEFAULT_BOOKMARKS_TO_RETRIEVE)

        assertEquals(
            listOf(
                Bookmark(
                    title = bookmarkNode.title,
                    url = bookmarkNode.url,
                    previewImageUrl = visitInfo.previewImageUrl,
                ),
            ),
            result,
        )

        val timeNow = System.currentTimeMillis()
        val nineDaysAgo = timeNow - TimeUnit.DAYS.toMillis(9)
        val elevenDaysAgo = timeNow - TimeUnit.DAYS.toMillis(11)
        assertTrue(historyTimeFrameSlot.isCaptured)
        assertTrue(historyTimeFrameSlot.captured in elevenDaysAgo..nineDaysAgo)

        coVerify {
            bookmarksStorage.getRecentBookmarks(
                BookmarksUseCase.DEFAULT_BOOKMARKS_TO_RETRIEVE,
                null,
                any(),
            )
        }
    }

    @Test
    fun `WHEN there are no bookmarks THEN retrieve the empty list from storage`() = runTest {
        val bookmarksStorage = mockk<BookmarksStorage>(relaxed = true)
        val historyStorage = mockk<HistoryStorage>(relaxed = true)
        val lastSavedFolderCache = mockk<LastSavedFolderCache>(relaxed = true)
        val useCase = BookmarksUseCase(bookmarksStorage, historyStorage, lastSavedFolderCache)

        coEvery { bookmarksStorage.getRecentBookmarks(any(), any(), any()) }.coAnswers { Result.success(listOf()) }

        val result = useCase.retrieveRecentBookmarks(BookmarksUseCase.DEFAULT_BOOKMARKS_TO_RETRIEVE)

        assertEquals(listOf<BookmarkNode>(), result)

        coVerify {
            bookmarksStorage.getRecentBookmarks(
                BookmarksUseCase.DEFAULT_BOOKMARKS_TO_RETRIEVE,
                null,
                any(),
            )
        }
    }
}
