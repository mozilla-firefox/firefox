/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.bookmark.fileparser

/**
 * Parses a bookmark file into a [ParsedBookmarkNode] tree.
 */
fun interface BookmarksFileParser {
    /**
     * Parses [input] and returns the resulting bookmark tree, or a failure if the input
     * could not be parsed.
     */
    suspend fun parse(input: String): Result<ParsedBookmarkNode>

    companion object {
        /**
         * Returns a [BookmarksFileParser] that always succeeds, returning [parsedTree] if provided
         * or a default tree otherwise.
         */
        fun fakeSuccess(parsedTree: ParsedBookmarkNode?): BookmarksFileParser =
            FakeSuccessParser(parsedTree)

        /**
         * Returns a [BookmarksFileParser] that always fails with a [RuntimeException].
         */
        fun fakeFailure() = BookmarksFileParser {
            Result.failure(RuntimeException("couldn't parse it"))
        }
    }
}

/**
 * A node in a parsed bookmark tree.
 *
 * @property position The position of this node among its siblings, or null if unspecified.
 * @property dateAddedTimestamp Creation time in milliseconds since the Unix epoch.
 * @property lastModifiedTimestamp Last modification time in milliseconds since the Unix epoch.
 */
sealed interface ParsedBookmarkNode {
    val position: UInt?
    val dateAddedTimestamp: Long
    val lastModifiedTimestamp: Long
}

/**
 * A bookmark folder that can contain other [ParsedBookmarkNode]s.
 *
 * @property title The display name of the folder.
 * @property children The ordered list of child nodes.
 */
data class Folder(
    val title: String,
    val children: List<ParsedBookmarkNode>,
    override val position: UInt?,
    override val dateAddedTimestamp: Long,
    override val lastModifiedTimestamp: Long,
) : ParsedBookmarkNode

/**
 * A bookmark pointing to a URL.
 *
 * @property title The display name of the bookmark, or null if absent.
 * @property url The URL the bookmark points to.
 */
data class Bookmark(
    val title: String?,
    val url: String,
    override val position: UInt?,
    override val dateAddedTimestamp: Long,
    override val lastModifiedTimestamp: Long,
) : ParsedBookmarkNode

/**
 * A visual separator between bookmark nodes.
 */
data class Separator(
    override val position: UInt?,
    override val dateAddedTimestamp: Long,
    override val lastModifiedTimestamp: Long,
) : ParsedBookmarkNode

private class FakeSuccessParser(val returnedTree: ParsedBookmarkNode?) : BookmarksFileParser {
    override suspend fun parse(input: String): Result<ParsedBookmarkNode> = Result.success(
        returnedTree ?: defaultFakeSuccessTree,
    )

    private val defaultFakeSuccessTree: ParsedBookmarkNode = Folder(
        title = "Bookmarks",
        position = 0u,
        dateAddedTimestamp = 0L,
        lastModifiedTimestamp = 0L,
        children = listOf(
            Folder(
                title = "Subfolder",
                position = 0u,
                dateAddedTimestamp = 0L,
                lastModifiedTimestamp = 0L,
                children = listOf(
                    Bookmark(
                        title = "Example",
                        url = "https://example.com",
                        position = null,
                        dateAddedTimestamp = 0L,
                        lastModifiedTimestamp = 0L,
                    ),
                    Separator(
                        position = null,
                        dateAddedTimestamp = 0L,
                        lastModifiedTimestamp = 0L,
                    ),
                    Bookmark(
                        title = "Wikipedia",
                        url = "https://wikipedia.org",
                        position = null,
                        dateAddedTimestamp = 0L,
                        lastModifiedTimestamp = 0L,
                    ),
                ),
            ),
            Bookmark(
                title = "Mozilla",
                url = "https://www.mozilla.org",
                position = 1u,
                dateAddedTimestamp = 0L,
                lastModifiedTimestamp = 0L,
            ),
        ),
    )
}
