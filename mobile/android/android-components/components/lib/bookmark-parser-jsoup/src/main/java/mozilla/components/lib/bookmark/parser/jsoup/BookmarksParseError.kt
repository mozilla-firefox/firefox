/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.bookmark.parser.jsoup

import kotlinx.parcelize.Parcelize
import mozilla.components.concept.fileparser.FileParserError

/** An error type representing possible errors while parsing a file for bookmarks */
@Parcelize
sealed interface BookmarksParseError : FileParserError {

    /** Wrong file type - Netscape bookmark HTML is expected. */
    data object UnsupportedContentType : BookmarksParseError

    /** A bookmark entry has no `href` attribute, so its URL cannot be found. */
    data object InvalidFormat : BookmarksParseError

    /** Some other uncaught exception happened during parsing. */
    data object Unexpected : BookmarksParseError
}
