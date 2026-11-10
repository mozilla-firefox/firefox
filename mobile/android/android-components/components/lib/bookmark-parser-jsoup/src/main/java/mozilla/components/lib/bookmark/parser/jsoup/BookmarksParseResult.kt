/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.bookmark.parser.jsoup

import mozilla.components.concept.storage.bookmarks.InsertableBookmarkTreeNode

/**
 * Result of the parsing
 *
 * @property foldersCount The number of folders encountered during parsing
 * @property bookmarksCount The number of bookmarks encountered during parsing
 * @property folder The folder containing the imported bookmarks
 */
data class BookmarksParseResult(
    val foldersCount: Int,
    val bookmarksCount: Int,
    val folder: InsertableBookmarkTreeNode.Folder,
)
