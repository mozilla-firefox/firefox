/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.bookmarks.file

import android.content.Context
import android.net.Uri
import java.io.InputStream
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.concept.fileimporter.FileImporter
import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.concept.fileimporter.FileImporterException
import mozilla.components.concept.fileimporter.ImportResult
import mozilla.components.concept.fileparser.FileParser
import mozilla.components.concept.fileparser.FileParserException
import mozilla.components.concept.storage.bookmarks.BookmarkInserter
import mozilla.components.concept.storage.bookmarks.InsertableBookmarkTreeRoot
import mozilla.components.lib.bookmark.parser.jsoup.BookmarksParseError
import mozilla.components.lib.bookmark.parser.jsoup.BookmarksParseResult

/**
 * Creates a [FileImporter] that imports bookmarks from Netscape HTML bookmark files.
 *
 * @param context Used to open an [InputStream] from the provided [Uri] via [Context.getContentResolver].
 * @param parser Parses the [InputStream] into a bookmark tree.
 * @param inserter Inserts the parsed bookmark tree into storage.
 */
fun FileImporter.Companion.htmlImporter(
    context: Context,
    parentGuid: String,
    parser: FileParser<BookmarksParseResult>,
    inserter: BookmarkInserter,
    ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
): FileImporter<BookmarksImportResult> =
    HtmlBookmarksFileImporter(
        parentGuid = parentGuid,
        uriOpener = UriOpener.make(context, ioDispatcher),
        parser = parser,
        inserter = inserter,
    )

/**
 * The result of a bookmarks file import.
 *
 * @property guid The guid of the root of the imported bookmark tree.
 * @property count The number of bookmarks imported.
 */
data class BookmarksImportResult(val guid: String, override val count: Int) : ImportResult

/** Opens an [InputStream] from a [Uri]. */
internal fun interface UriOpener {
    /**
     * @param uri The [Uri] to open.
     * @return A [Result] containing the opened [InputStream], or a failure if the stream could not be opened.
     */
    suspend fun open(uri: Uri): Result<InputStream>

    companion object {
        /** Creates a [UriOpener] that uses the [Context.getContentResolver] to open the [Uri]. */
        fun make(context: Context, ioDispatcher: CoroutineDispatcher) = UriOpener { uri ->
            withContext(ioDispatcher) {
                runCatching {
                    requireNotNull(context.contentResolver.openInputStream(uri))
                }
                    .onFailure {
                        if (it is CancellationException) throw it
                    }
            }
        }
    }
}

internal class HtmlBookmarksFileImporter(
    private val parentGuid: String,
    private val uriOpener: UriOpener,
    private val parser: FileParser<BookmarksParseResult>,
    private val inserter: BookmarkInserter,
) : FileImporter<BookmarksImportResult> {

    override val mimeType = "text/html"

    override suspend fun importFromUri(uri: Uri) = runCatching {
        val inputStream =
            uriOpener.open(uri).getOrThrowError { FileImporterException(FileImporterError.ReadError, cause = it) }
        val parseResult =
            inputStream
                .use { parser.parse(it) }
                .getOrThrowError {
                    val parseError = (it as? FileParserException)?.errorType ?: BookmarksParseError.Unexpected
                    FileImporterException(FileImporterError.ParseError(parseError), cause = it)
                }
        val tree = InsertableBookmarkTreeRoot(parentGuid = parentGuid, rootFolder = parseResult.folder)
        val guid =
            inserter.insertTree(tree).getOrThrowError { FileImporterException(FileImporterError.SaveError, cause = it) }

        BookmarksImportResult(guid = guid, count = parseResult.bookmarksCount)
    }
        .onFailure {
            if (it is CancellationException) throw it
        }

    private inline fun <T> Result<T>.getOrThrowError(map: (Throwable) -> FileImporterException): T {
        return getOrElse {
            if (it is CancellationException || it is FileImporterException) throw it
            throw map(it)
        }
    }
}
