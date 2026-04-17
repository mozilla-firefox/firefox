/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.storage

import mozilla.components.concept.storage.bookmarks.BookmarkInserter

/**
 * An interface which defines read/write operations for bookmarks data.
 */
interface BookmarksStorage : Storage, BookmarkInserter {

    /**
     * Produces a bookmarks tree for the given guid string.
     *
     * @param guid The bookmark guid to obtain.
     * @param recursive Whether to recurse and obtain all levels of children.
     * @return The populated root starting from the guid.
     */
    suspend fun getTree(guid: String, recursive: Boolean = false): Result<BookmarkNode?>

    /**
     * Obtains the details of a bookmark without children, if one exists with that guid. Otherwise, null.
     *
     * @param guid The bookmark guid to obtain.
     * @return The bookmark node or null if it does not exist.
     */
    suspend fun getBookmark(guid: String): Result<BookmarkNode?>

    /**
     * Produces a list of all bookmarks with the given URL.
     *
     * @param url The URL string.
     * @return The list of bookmarks that match the URL
     */
    suspend fun getBookmarksWithUrl(url: String): Result<List<BookmarkNode>>

    /**
     * Produces a list of the most recently added bookmarks.
     *
     * @param limit The maximum number of entries to return.
     * @param maxAge Optional parameter used to filter out entries older than this number of milliseconds.
     * @param currentTime Optional parameter for current time. Defaults toSystem.currentTimeMillis()
     * @return The list of bookmarks that have been recently added up to the limit number of items.
     */
    suspend fun getRecentBookmarks(
        limit: Int,
        maxAge: Long? = null,
        currentTime: Long = System.currentTimeMillis(),
    ): Result<List<BookmarkNode>>

    /**
     * Searches bookmarks with a query string.
     *
     * @param query The query string to search.
     * @param limit The maximum number of entries to return.
     * @return The list of matching bookmark nodes up to the limit number of items.
     */
    suspend fun searchBookmarks(query: String, limit: Int = DEFAULT_BOOKMARKS_SEARCH_LIMIT): Result<List<BookmarkNode>>

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
    suspend fun addItem(parentGuid: String, url: String, title: String, position: UInt?): Result<String>

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
    suspend fun addFolder(parentGuid: String, title: String, position: UInt? = null): Result<String>

    /**
     * Adds a new bookmark separator to a given node.
     *
     * Sync behavior: will add new separator to remote devices.
     *
     * @param parentGuid The parent guid of the new node.
     * @param position The optional position to add the new node or null to append.
     * @return The guid of the newly inserted bookmark item.
     */
    suspend fun addSeparator(parentGuid: String, position: UInt?): Result<String>

    /**
     * Edits the properties of an existing bookmark item and/or moves an existing one underneath a new parent guid.
     *
     * Sync behavior: will alter bookmark item on remote devices.
     *
     * @param guid The guid of the item to update.
     * @param info The info to change in the bookmark.
     */
    suspend fun updateNode(guid: String, info: BookmarkInfo): Result<Unit>

    /**
     * Deletes a bookmark node and all of its children, if any.
     *
     * Sync behavior: will remove bookmark from remote devices.
     *
     * @return Whether the bookmark existed or not.
     */
    suspend fun deleteNode(guid: String): Result<Boolean>

    /**
     * Counts the number of bookmarks in the trees under the specified GUIDs.

     * @param guids The guids of folders to query.
     * @return Count of all bookmark items (ie, no folders or separators) in all specified folders
     * recursively. Empty folders, non-existing GUIDs and non-existing items will return zero.
     * The result is implementation dependant if the trees overlap.
     */
    suspend fun countBookmarksInTrees(guids: List<String>): UInt

    companion object {
        const val DEFAULT_BOOKMARKS_SEARCH_LIMIT = 10
    }
}

/**
 * Represents a bookmark record.
 *
 * @property type The [BookmarkNodeType] of this record.
 * @property guid The id.
 * @property parentGuid The id of the parent node in the tree.
 * @property position The position of this node in the tree.
 * @property title A title of the page.
 * @property url The url of the page.
 * @property dateAdded Creation time, in milliseconds since the unix epoch.
 * @property children The list of children of this bookmark node in the tree.
 */
data class BookmarkNode(
    val type: BookmarkNodeType,
    val guid: String,
    val parentGuid: String?,
    val position: UInt?,
    val title: String?,
    val url: String?,
    val dateAdded: Long,
    val lastModified: Long,
    val children: List<BookmarkNode>?,
) {
    /**
     * Removes [children] from [BookmarkNode.children] and returns the new modified [BookmarkNode].
     *
     * DOES NOT delete the bookmarks from storage, so this should only be used where you are
     * batching deletes, or where the deletes are otherwise pending.
     *
     * In the general case you should try and avoid using this - just delete the items from
     * storage then re-fetch the parent node.
     */
    operator fun minus(children: Set<BookmarkNode>): BookmarkNode {
        val removedChildrenGuids = children.map { it.guid }
        return this.copy(children = this.children?.filterNot { removedChildrenGuids.contains(it.guid) })
    }
}

/**
 * Class for making alterations to any bookmark node
 */
data class BookmarkInfo(
    val parentGuid: String?,
    val position: UInt?,
    val title: String?,
    val url: String?,
)

/**
 * The types of bookmark nodes
 */
enum class BookmarkNodeType {
    ITEM, FOLDER, SEPARATOR
}
