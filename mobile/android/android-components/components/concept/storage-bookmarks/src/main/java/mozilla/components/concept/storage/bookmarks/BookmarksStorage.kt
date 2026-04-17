/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.storage.bookmarks

/**
 * Inserts bookmarks into storage.
 */
fun interface BookmarkInserter {
    /**
     * Inserts the given bookmark node into storage.
     *
     * @param node The [InsertableBookmarkNode] to insert.
     * @return The guid of the head of the inserted bookmark tree.
     */
    suspend fun insert(node: InsertableBookmarkNode): Result<String>
}

/**
 * Represents a bookmark node that can be inserted into storage.
 */
sealed interface InsertableBookmarkNode {
    /**
     * A bookmark item (e.g. a page).
     *
     * @property parentGuid The GUID of the parent folder.
     * @property title The title of the bookmark.
     * @property url The URL of the bookmark.
     * @property position The ordinal position within the parent.
     */
    data class Item(
        val parentGuid: String?,
        val title: String?,
        val url: String,
        val position: UInt?,
    ) : InsertableBookmarkNode

    /**
     * A bookmark folder that can contain other [InsertableBookmarkNode]s.
     *
     * @property parentGuid The GUID of the parent folder.
     * @property title The title of the folder.
     * @property position The ordinal position within the parent.
     * @property children The child nodes contained in this folder.
     */
    data class Folder(
        val parentGuid: String?,
        val title: String?,
        val position: UInt?,
        val children: List<InsertableBookmarkNode>,
    ) : InsertableBookmarkNode

    /**
     * A bookmark separator.
     */
    object Separator : InsertableBookmarkNode
}
