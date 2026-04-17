/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.storage.sync

import android.content.Context
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.withContext
import mozilla.appservices.places.PlacesApi
import mozilla.appservices.places.uniffi.InsertableBookmarkFolder
import mozilla.appservices.places.uniffi.PlacesApiException
import mozilla.components.concept.storage.BookmarkInfo
import mozilla.components.concept.storage.BookmarkNode
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.concept.storage.bookmarks.InsertableBookmarkNode
import mozilla.components.concept.sync.SyncableStore
import mozilla.components.concept.toolbar.AutocompleteProvider
import mozilla.components.concept.toolbar.AutocompleteResult
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.doesUrlStartsWithText
import mozilla.components.support.utils.segmentAwareDomainMatch

@VisibleForTesting
internal const val BOOKMARKS_AUTOCOMPLETE_SOURCE_NAME = "placesBookmarks"

/**
 * How many bookmarks to try and find from which to pick one that can be an autocomplete suggestion.
 */
private const val BOOKMARKS_AUTOCOMPLETE_QUERY_LIMIT = 20

/**
 * Implementation of the [BookmarksStorage] which is backed by a Rust Places lib via [PlacesApi].
 */
open class PlacesBookmarksStorage(
    context: Context,
    override val autocompletePriority: Int = 0,
) : PlacesStorage(context),
    BookmarksStorage,
    SyncableStore,
    AutocompleteProvider {

    override val logger = Logger("PlacesBookmarksStorage")

    /**
     * Produces a bookmarks tree for the given guid string.
     *
     * @param guid The bookmark guid to obtain.
     * @param recursive Whether to recurse and obtain all levels of children.
     * @return The populated root starting from the guid.
     */
    override suspend fun getTree(guid: String, recursive: Boolean): Result<BookmarkNode?> {
        return withContext(readScope.coroutineContext) {
            Result.runCatching { reader.getBookmarksTree(guid, recursive)?.asBookmarkNode() }
        }
    }

    /**
     * Obtains the details of a bookmark without children, if one exists with that guid. Otherwise, null.
     *
     * @param guid The bookmark guid to obtain.
     * @return The bookmark node or null if it does not exist.
     */
    override suspend fun getBookmark(guid: String): Result<BookmarkNode?> {
        return withContext(readScope.coroutineContext) {
            Result.runCatching {
                reader.getBookmark(guid)?.asBookmarkNode()
            }
        }
    }

    /**
     * Produces a list of all bookmarks with the given URL.
     *
     * @param url The URL string.
     * @return The list of bookmarks that match the URL
     */
    override suspend fun getBookmarksWithUrl(url: String): Result<List<BookmarkNode>> {
        return withContext(readScope.coroutineContext) {
            Result.runCatching {
                reader.getBookmarksWithURL(url).map { it.asBookmarkNode() }
            }
        }
    }

    /**
     * Searches bookmarks with a query string.
     *
     * @param query The query string to search.
     * @param limit The maximum number of entries to return.
     * @return The list of matching bookmark nodes up to the limit number of items.
     */
    override suspend fun searchBookmarks(query: String, limit: Int): Result<List<BookmarkNode>> {
        return withContext(readScope.coroutineContext) {
            Result.runCatching {
                reader.searchBookmarks(query, limit).map { it.asBookmarkNode() }
            }
        }
    }

    /**
     * Retrieves a list of recently added bookmarks.
     *
     * @param limit The maximum number of entries to return.
     * @param maxAge Optional parameter used to filter out entries older than this number of milliseconds.
     * @param currentTime Optional parameter for current time. Defaults toSystem.currentTimeMillis()
     * @return The list of matching bookmark nodes up to the limit number of items.
     */
    override suspend fun getRecentBookmarks(
        limit: Int,
        maxAge: Long?,
        @VisibleForTesting currentTime: Long,
    ): Result<List<BookmarkNode>> {
        return withContext(readScope.coroutineContext) {
            val threshold = if (maxAge != null) {
                currentTime - maxAge
            } else {
                0
            }
            Result.runCatching {
                reader.getRecentBookmarks(limit)
                .map { it.asBookmarkNode() }
                .filter { it.dateAdded >= threshold }
            }
        }
    }

    /**
     * Adds a new bookmark item to a given node.
     *
     * Sync behavior: will add new bookmark item to remote devices.
     *
     * @param parentGuid The parent guid of the new node.
     * @param url The URL of the bookmark item to add.
     * @param title The title of the bookmark item to add.
     * @param position The optional position to add the new node or null to append.
     * @return The guid of the newly inserted bookmark item.
     */
    override suspend fun addItem(parentGuid: String, url: String, title: String, position: UInt?): Result<String> {
        return withContext(writeScope.coroutineContext) {
            Result.runCatching {
                writer.createBookmarkItem(parentGuid, url, title, position)
            }
        }
    }

    /**
     * Adds a new bookmark folder to a given node.
     *
     * Sync behavior: will add new separator to remote devices.
     *
     * @param parentGuid The parent guid of the new node.
     * @param title The title of the bookmark folder to add.
     * @param position The optional position to add the new node or null to append.
     * @return The guid of the newly inserted bookmark item.
     */
    override suspend fun addFolder(parentGuid: String, title: String, position: UInt?): Result<String> {
        return withContext(writeScope.coroutineContext) {
            Result.runCatching {
                writer.createFolder(parentGuid, title, position)
            }
        }
    }

    /**
     * Adds a new bookmark separator to a given node.
     *
     * Sync behavior: will add new separator to remote devices.
     *
     * @param parentGuid The parent guid of the new node.
     * @param position The optional position to add the new node or null to append.
     * @return The guid of the newly inserted bookmark item.
     */
    override suspend fun addSeparator(parentGuid: String, position: UInt?): Result<String> {
        return withContext(writeScope.coroutineContext) {
            Result.runCatching {
                writer.createSeparator(parentGuid, position)
            }
        }
    }

    /**
     * Edits the properties of an existing bookmark item and/or moves an existing one underneath a new parent guid.
     *
     * Sync behavior: will alter bookmark item on remote devices.
     *
     * @param guid The guid of the item to update.
     * @param info The info to change in the bookmark.
     */
    override suspend fun updateNode(guid: String, info: BookmarkInfo): Result<Unit> {
        return withContext(writeScope.coroutineContext) {
            InsertableBookmarkFolder()
            Result.runCatching { writer.updateBookmark(guid, info.parentGuid, info.position, info.title, info.url) }
        }
    }

    /**
     * Deletes a bookmark node and all of its children, if any.
     *
     * Sync behavior: will remove bookmark from remote devices.
     *
     * @return Whether the bookmark existed or not.
     */
    override suspend fun deleteNode(guid: String): Result<Boolean> = withContext(writeScope.coroutineContext) {
        Result.runCatching { writer.deleteBookmarkNode(guid) }
    }

    /**
     * Counts the number of items in the bookmark trees under the specified GUIDs.

     * @param guids The guids of folders to query.
     * @return Count of all bookmark items (ie, not folders or separators) in all specified folders
     * recursively. Empty folders, non-existing GUIDs and non-existing items will return zero.
     * The result is implementation dependant if the trees overlap.
     */
    override suspend fun countBookmarksInTrees(guids: List<String>): UInt {
        return withContext(readScope.coroutineContext) {
            try {
                reader.countBookmarksInTrees(guids)
            } catch (e: PlacesApiException) {
                crashReporter?.submitCaughtException(e)
                logger.warn("Ignoring PlacesApiException while running countBookmarksInTrees", e)
                0U
            }
        }
    }

    override fun registerWithSyncManager() {
        places.registerWithSyncManager()
    }

    override suspend fun getAutocompleteSuggestion(query: String): AutocompleteResult? =
        searchBookmarks(query, BOOKMARKS_AUTOCOMPLETE_QUERY_LIMIT).getOrNull()?.let { bookmarks ->
            val bookmarkUrl = bookmarks
                .mapNotNull { it.url }
                .firstOrNull { doesUrlStartsWithText(it, query) }
                ?: return null

            val resultText = segmentAwareDomainMatch(query, arrayListOf(bookmarkUrl))
            resultText?.let {
                AutocompleteResult(
                    input = query,
                    text = it.matchedSegment,
                    url = it.url,
                    source = BOOKMARKS_AUTOCOMPLETE_SOURCE_NAME,
                    totalItems = 1,
                )
            }
        }

    override suspend fun insert(node: InsertableBookmarkNode) {
        TODO("Not yet implemented")
    }
}
