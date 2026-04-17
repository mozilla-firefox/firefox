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
     */
    suspend fun insert(node: InsertableBookmarkNode)
}

/**
 * Represents a bookmark node that can be inserted into storage.
 */
sealed interface InsertableBookmarkNode {
    /** The ordinal position of this node within its parent. */
    val position: UInt?

    /** The GUID of the parent folder. */
    val parentGuid: String?

    /** The title of the bookmark node. */
    val title: String?

    /**
     * A bookmark item (e.g. a page).
     *
     * @property parentGuid The GUID of the parent folder.
     * @property title The title of the bookmark.
     * @property position The ordinal position within the parent.
     */
    data class Item(
        override val parentGuid: String?,
        override val title: String?,
        override val position: UInt?,
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
        override val parentGuid: String?,
        override val title: String?,
        override val position: UInt?,
        val children: List<InsertableBookmarkNode>,
    ) : InsertableBookmarkNode

    /**
     * A bookmark separator.
     */
    object Separator : InsertableBookmarkNode {
        override val position = null
        override val parentGuid = null
        override val title = null
    }
}
