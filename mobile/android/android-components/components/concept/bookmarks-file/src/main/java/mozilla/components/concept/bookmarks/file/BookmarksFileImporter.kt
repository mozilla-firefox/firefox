/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.bookmarks.file.importer

import android.net.Uri

/**
 * An interface for importing bookmarks from a file.
 */
fun interface BookmarksFileImporter {
    /**
     * Represents the number of bookmarks that were imported.
     *
     * @property count The number of bookmarks imported.
     */
    @JvmInline
    value class NumberOfBookmarks(val count: Int)

    /**
     * Imports bookmarks from the file at the given [uri].
     *
     * @param uri The URI of the file to import bookmarks from.
     * @return A [Result] containing [NumberOfBookmarks] on success or an exception on failure.
     */
    suspend fun importBookmarksFromUri(uri: Uri): Result<NumberOfBookmarks>
}
