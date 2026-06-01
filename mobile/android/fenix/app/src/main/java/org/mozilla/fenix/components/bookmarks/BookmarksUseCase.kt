/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.bookmarks

import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext
import mozilla.appservices.places.BookmarkRoot
import mozilla.appservices.places.uniffi.PlacesApiException
import mozilla.components.concept.storage.BookmarkInfo
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.concept.storage.HistoryStorage
import org.mozilla.fenix.home.bookmarks.Bookmark
import java.util.concurrent.TimeUnit

/**
 * Use cases that allow for modifying and retrieving bookmarks.
 */
class BookmarksUseCase(
    bookmarksStorage: BookmarksStorage,
    historyStorage: HistoryStorage,
    lastSavedFolderCache: LastSavedFolderCache,
) {

    /**
     * Use case for adding a new bookmark.
     *
     * @param storage [BookmarksStorage] used to add and retrieve bookmark data.
     * @param lastSavedFolderCache Caches the folder the user last saved a bookmark in, used to
     * pick the default parent folder when the caller does not specify one.
     */
    class AddBookmarksUseCase internal constructor(
        private val storage: BookmarksStorage,
        private val lastSavedFolderCache: LastSavedFolderCache,
    ) {

        /**
         * The outcome of an attempted add.
         *
         * @property guidToEdit The guid of the newly added bookmark, or null when a bookmark with
         * the same url already existed or the add otherwise failed.
         * @property parentNode The resolved parent folder the bookmark was (or would have been)
         * added under. Useful for snackbar/UX consumers that need the folder name. May be null
         * if even the Mobile root could not be fetched.
         */
        data class Result(
            val guidToEdit: String?,
            val parentNode: BookmarkNode?,
        )

        /**
         * Adds a new bookmark with the provided [url] and [title].
         *
         * When [parentGuid] is null, the parent folder is resolved from [LastSavedFolderCache]
         * (falling back to [BookmarkRoot.Mobile] and clearing a stale cache entry if the cached
         * folder no longer exists). An explicit [parentGuid] is honored as-is and does not
         * interact with the cache.
         */
        suspend operator fun invoke(
            url: String,
            title: String,
            position: UInt? = null,
            parentGuid: String? = null,
        ): Result {
            val (resolvedGuid, parentNode) = resolveParent(parentGuid)

            val guidToEdit = try {
                val alreadyExists = storage
                    .getBookmarksWithUrl(url)
                    .getOrDefault(listOf())
                    .any { it.url == url }
                if (alreadyExists) {
                    null
                } else {
                    storage.addItem(
                        parentGuid = resolvedGuid,
                        url = url,
                        title = title,
                        position = position,
                    ).getOrNull()
                }
            } catch (e: PlacesApiException.UrlParseFailed) {
                null
            }
            return Result(guidToEdit, parentNode)
        }

        private suspend fun resolveParent(explicit: String?): Pair<String, BookmarkNode?> {
            if (explicit != null) {
                return explicit to storage.getBookmark(explicit).getOrNull()
            }
            val cachedGuid = lastSavedFolderCache.getGuid() ?: BookmarkRoot.Mobile.id
            val parentNode = storage.getBookmark(cachedGuid).getOrNull()
                ?: storage.getBookmark(BookmarkRoot.Mobile.id).getOrNull()
            val finalGuid = parentNode?.guid ?: BookmarkRoot.Mobile.id
            if (cachedGuid != finalGuid) {
                lastSavedFolderCache.setGuid(null)
            }
            return finalGuid to parentNode
        }
    }

    /**
     * Use case for editing an existing bookmark.
     *
     * @param storage [BookmarksStorage] used to persist the edit.
     * @param lastSavedFolderCache Caches the folder the user last saved a bookmark in. Updated
     * when an edit changes a real field on the bookmark, so subsequent adds default to the same
     * folder.
     */
    class EditBookmarkUseCase internal constructor(
        private val storage: BookmarksStorage,
        private val lastSavedFolderCache: LastSavedFolderCache,
    ) {
        /**
         * Commits an edit to the bookmark identified by [guid]. The storage write and cache
         * update are performed atomically with respect to caller cancellation: if the caller's
         * scope is cancelled mid-call, both still complete.
         *
         * @param guid The guid of the bookmark to update.
         * @param info The new fields to persist.
         * @param edited Whether the edit changed any user-visible field. When true and the
         * update succeeds, the parent folder is remembered for the next add.
         * @return true if storage reported a successful update, false otherwise.
         */
        suspend operator fun invoke(
            guid: String,
            info: BookmarkInfo,
            edited: Boolean,
        ): Boolean = withContext(NonCancellable) {
            val result = storage.updateNode(guid, info)
            if (result.isSuccess && edited) {
                lastSavedFolderCache.setGuid(info.parentGuid)
            }
            result.isSuccess
        }
    }

    /**
     * Uses for retrieving recently added bookmarks.
     *
     * @param bookmarksStorage [BookmarksStorage] to retrieve the bookmark data.
     * @param historyStorage Optional [HistoryStorage] to retrieve the preview image of a visited
     * page associated with a bookmark.
     */
    class RetrieveRecentBookmarksUseCase internal constructor(
        private val bookmarksStorage: BookmarksStorage,
        private val historyStorage: HistoryStorage? = null,
    ) {
        /**
         * Retrieves a list of recently added bookmarks, if any, up to maximum.
         *
         * @param count The number of recent bookmarks to return.
         * @param previewImageMaxAgeMs The maximum age (ms) to search history for preview image URLs.
         * @return a list of [Bookmark]s if any, up to a number specified by [count].
         */
        suspend operator fun invoke(
            count: Int = DEFAULT_BOOKMARKS_TO_RETRIEVE,
            previewImageMaxAgeMs: Long = TimeUnit.DAYS.toMillis(DEFAULT_BOOKMARKS_LENGTH_DAYS_PREVIEW_IMAGE_SEARCH),
        ): List<Bookmark> {
            val currentTime = System.currentTimeMillis()

            // Fetch visit information within the time range of now and the specified maximum age.
            val history = historyStorage?.getDetailedVisits(
                start = currentTime - previewImageMaxAgeMs,
                end = currentTime,
            )

            return bookmarksStorage
                .getRecentBookmarks(count)
                .getOrDefault(listOf())
                .map { bookmark ->
                    Bookmark(
                        title = bookmark.title,
                        url = bookmark.url,
                        previewImageUrl = history?.find { bookmark.url == it.url }?.previewImageUrl,
                    )
                }
        }
    }

    val addBookmark by lazy { AddBookmarksUseCase(bookmarksStorage, lastSavedFolderCache) }
    val editBookmark by lazy { EditBookmarkUseCase(bookmarksStorage, lastSavedFolderCache) }
    val retrieveRecentBookmarks by lazy {
        RetrieveRecentBookmarksUseCase(
            bookmarksStorage,
            historyStorage,
        )
    }

    companion object {
        // Number of recent bookmarks to retrieve.
        const val DEFAULT_BOOKMARKS_TO_RETRIEVE = 8

        // The maximum age in days of a recent bookmarks to retrieve.
        const val DEFAULT_BOOKMARKS_LENGTH_DAYS_PREVIEW_IMAGE_SEARCH = 10L
    }
}
