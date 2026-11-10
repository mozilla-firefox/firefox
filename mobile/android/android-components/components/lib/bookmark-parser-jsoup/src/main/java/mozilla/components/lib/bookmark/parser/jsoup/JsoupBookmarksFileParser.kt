/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.bookmark.parser.jsoup

import java.io.InputStream
import java.io.Reader
import java.util.Stack
import kotlin.coroutines.cancellation.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.withContext
import mozilla.components.concept.fileparser.FileParser
import mozilla.components.concept.fileparser.FileParserException
import mozilla.components.concept.storage.bookmarks.InsertableBookmarkTreeNode
import org.jsoup.nodes.DocumentType
import org.jsoup.nodes.Element
import org.jsoup.parser.Parser
import org.jsoup.parser.StreamParser

/**
 * A [FileParser] that uses jsoup to parse HTML bookmark files in the Netscape Bookmark format.
 *
 * @param ioDispatcher Dispatcher used for parsing.
 */
internal class JsoupBookmarksFileParser(
    private val rootFolderName: String = "root",
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : FileParser<BookmarksParseResult> {

    override suspend fun parse(inputStream: InputStream): Result<BookmarksParseResult> {
        return runCatching {
            inputStream.bufferedReader().parse()
        }
            .onFailure { failure ->
                if (failure is CancellationException) throw failure
            }
            .recoverCatching { error ->
                // rethrow errors already mapped to our domain error
                if (error is FileParserException) throw error

                // wrap the other types of errors as unexpected rethrow them
                throw FileParserException(
                    errorType = BookmarksParseError.Unexpected,
                    message = "Failed to parse bookmarks file",
                    cause = error,
                )
            }
    }

    /**
     * Parses the HTML content into a [BookmarksParseResult] containing some meta information about the tree, and the
     * [InsertableBookmarkTreeNode.Folder] root node.
     *
     * The parsing procedure walks the tree in a depth-first, top-down manner, and continuously builds a tree of
     * [InsertableBookmarkTreeNode]s using a stack to track the current folder scope.
     *
     * The procedure uses an intermediate [MutableFolder] representation to continuously build the current folder we are
     * exploring, to avoid having to do a deep copy of the parsed tree, to add a newly discovered child.
     *
     * 1. We start out by creating a root folder, and then walking the tree. We push this root onto the stack, so we can
     *    track the "last folder" we are in.
     * 2. As we walk the tree, We add any child we discover, to the current folder (the folder at the top of the stack).
     *     - If the child is a bookmark, we extract the url, and create an [InsertableBookmarkTreeNode.Item] object,
     *       then, we add it to the current folder.
     *     - If the child is a separator, we create an [InsertableBookmarkTreeNode.Separator] and add it to the current
     *       folder.
     *     - If the child is a folder, we create an [InsertableBookmarkTreeNode.Folder] and also add it to the current
     *       folder. Then we push the new folder onto the stack, and for the next round, this becomes the current folder
     *       scope, collecting its own children.
     * 3. When we detect the end of the current folder (determined by the `</DL>` closing tag), it means that we must
     *    have explored all the separators, bookmarks and sub-folders of that folder. So we "close" it out. We:
     *     - pop the current folder which we have just completed, from the stack.
     *     - check the parent of that folder (that is the new folder at the top of the stack)
     *     - convert all the collected children to an immutable [InsertableBookmarkTreeNode.Folder],
     *     - add this completed folder as a child of the parent folder.
     */
    private suspend fun Reader.parse(): BookmarksParseResult =
        withContext(ioDispatcher) {
            val root =
                MutableFolder(
                    parentGuid = null,
                    title = rootFolderName,
                    position = 0u,
                    dateAddedTimestamp = 0L,
                    lastModifiedTimestamp = 0L,
                )

            var bookmarksCount = 0
            var foldersCount = 0
            var hasValidDocType = false

            // we use this to track the folders as we explore them.
            // the folder at the top of the stack represents the folder we are currently exploring
            // the folder after it (if any), represents the parent of the current folder, and so on.
            val folders = Stack<MutableFolder>().apply { push(root) }

            val parser = StreamParser(Parser.htmlParser()).parse(this@parse, "")

            parser.use { parser ->
                parser.iterator().forEach { element: Element ->
                    ensureActive()
                    val currentFolder = folders.peek()

                    when {
                        element.isHead -> {
                            hasValidDocType = parser.validDocType()
                        }

                        element.isFolder -> {
                            val newFolder =
                                MutableFolder(
                                    title = element.text(),
                                    position = currentFolder.children.size.toUInt(),
                                    parentGuid = null,
                                    dateAddedTimestamp = element.dateAdded ?: 0L,
                                    lastModifiedTimestamp = element.lastModified ?: 0L,
                                )
                            folders.push(newFolder)
                        }

                        element.isBookmark -> {
                            currentFolder.insertBookmark(element)
                            bookmarksCount++
                        }

                        element.isSeparator -> currentFolder.insertSeparator(element)

                        // DL closing tag marks the end of a folder scope in Netscape HTML
                        // We check for the end of the list to pop the stack
                        element.tagName() == "dl" && element.tag().isSelfClosing.not() -> {
                            if (folders.size > 1) {
                                val completed = folders.pop()

                                foldersCount++
                                // add the newly completed folder to its parent
                                val parent = folders.peek()
                                parent.addFolder(completed.toImmutable())
                            }
                        }
                    }
                }
            }
            ensureActive()

            if (!hasValidDocType) {
                // we did not find a doc type, this is not a valid document
                throw FileParserException(
                    errorType = BookmarksParseError.UnsupportedContentType,
                    message = "Expected a Netscape bookmark file doctype but found none",
                )
            }

            BookmarksParseResult(
                foldersCount = foldersCount,
                bookmarksCount = bookmarksCount,
                folder = root.toImmutable(),
            )
        }

    private fun StreamParser.validDocType(expectedDocType: String = "netscape-bookmark-file-1"): Boolean {
        val doctype = this.document().childNodes().filterIsInstance<DocumentType>().firstOrNull()

        if (doctype?.name() != expectedDocType) {
            throw FileParserException(
                errorType = BookmarksParseError.UnsupportedContentType,
                message = "Expected the $expectedDocType doctype but found ${doctype?.name()}",
            )
        }
        return true
    }

    /** Adds an [InsertableBookmarkTreeNode.Folder] into the current mutable folder which is still being built. */
    private fun MutableFolder.addFolder(folder: InsertableBookmarkTreeNode.Folder) {
        this.children.add(folder)
    }

    /**
     * Converts an [Element] that is guaranteed to be a bookmark, into a [InsertableBookmarkTreeNode.Item] and inserts
     * it into the current folder.
     */
    private fun MutableFolder.insertBookmark(element: Element) {
        val bookmark =
            InsertableBookmarkTreeNode.Item(
                url = element.bookmarkUrl(),
                title = element.text(),
                position = this.children.size.toUInt(),
                dateAddedTimestamp = element.dateAdded ?: 0L,
                lastModifiedTimestamp = element.lastModified ?: 0L,
            )
        this.children.add(bookmark)
    }

    /**
     * Converts an [Element] that is guaranteed to represent a separator, into a [InsertableBookmarkTreeNode.Separator],
     * and inserts it into the current folder
     */
    private fun MutableFolder.insertSeparator(element: Element) {
        val separator =
            InsertableBookmarkTreeNode.Separator(
                position = this.children.size.toUInt(),
                dateAddedTimestamp = element.dateAdded ?: 0L,
                lastModifiedTimestamp = element.lastModified ?: 0L,
            )

        this.children.add(separator)
    }

    private fun Element.bookmarkUrl(): String =
        this.attr("href").ifBlank {
            throw FileParserException(
                errorType = BookmarksParseError.InvalidFormat,
                message = "Expected a non-empty href for but got an empty text",
            )
        }

    private val Element.dateAdded: Long?
        get() = attr("ADD_DATE").toLongOrNull()

    private val Element.lastModified: Long?
        get() = attr("LAST_MODIFIED").toLongOrNull()

    /**
     * In Netscape bookmark format, a folder is denoted by H3 tag
     *
     * See
     * https://learn.microsoft.com/en-us/previous-versions/windows/internet-explorer/ie-developer/platform-apis/aa753582(v=vs.85)
     */
    private val Element.isFolder
        get() = this.isTag("h3")

    /**
     * In Netscape bookmark format, a bookmark is denoted by an A (anchor) tag.
     *
     * See
     * https://learn.microsoft.com/en-us/previous-versions/windows/internet-explorer/ie-developer/platform-apis/aa753582(v=vs.85)
     */
    private val Element.isBookmark
        get() = this.isTag("a")

    /** A separator is represented by an HR tag */
    private val Element.isSeparator
        get() = this.isTag("hr")

    private val Element.isHead
        get() = this.isTag("head")

    private fun Element.isTag(name: String) = this.tagName().equals(name, ignoreCase = true)

    /** Internal mutable representation to handle the tree building */
    private class MutableFolder(
        val parentGuid: String?,
        val title: String,
        val position: UInt,
        val dateAddedTimestamp: Long,
        val lastModifiedTimestamp: Long,
    ) {
        val children = mutableListOf<InsertableBookmarkTreeNode>()

        fun toImmutable(): InsertableBookmarkTreeNode.Folder =
            InsertableBookmarkTreeNode.Folder(
                title = title,
                position = position,
                children = children.toList(),
                dateAddedTimestamp = dateAddedTimestamp,
                lastModifiedTimestamp = lastModifiedTimestamp,
            )
    }
}

/**
 * API to create a [FileParser] that uses jsoup to parse HTML bookmark files in the Netscape Bookmark format.
 *
 * @param ioDispatcher Dispatcher used for I/O work.
 */
fun FileParser.Companion.jsoupParser(
    rootFolderName: String = "root",
    ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
): FileParser<BookmarksParseResult> {
    return JsoupBookmarksFileParser(
        rootFolderName = rootFolderName,
        ioDispatcher = ioDispatcher,
    )
}
