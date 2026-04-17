/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.bookmarks.file.importer

import android.content.ContentResolver
import android.net.Uri
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.concept.bookmarks.file.importer.BookmarksFileImporter
import mozilla.components.concept.bookmarks.file.importer.BookmarksFileImporter.NumberOfBookmarks

/**
 * A [BookmarksFileImporter] that parses bookmarks from Netscape HTML bookmark files.
 *
 * @param contentResolver Used to open an [java.io.InputStream] from the provided [Uri].
 * @param dispatcher The [CoroutineDispatcher] for performing I/O work.
 */
class HtmlBookmarksFileImporter(
    private val contentResolver: ContentResolver,
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : BookmarksFileImporter {

    override suspend fun importBookmarksFromUri(uri: Uri) = withContext(dispatcher) {
        runCatching {
            val html = requireNotNull(contentResolver.openInputStream(uri))
                .use { it.bufferedReader().readText() }

            NumberOfBookmarks(countBookmarks(html))
        }
    }

    private fun countBookmarks(html: String): Int = ANCHOR_PATTERN.findAll(html).count()

    companion object {
        private val ANCHOR_PATTERN = Regex("<a\\s", RegexOption.IGNORE_CASE)
    }
}
