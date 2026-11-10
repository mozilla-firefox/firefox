/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.passwords.file

import android.content.Context
import android.net.Uri
import java.io.InputStream
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.concept.fileimporter.FileImporter
import mozilla.components.concept.fileimporter.ImportResult
import mozilla.components.concept.fileparser.FileParser
import mozilla.components.concept.password.parser.PasswordsParseResult
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.lib.password.parser.csv.CsvPasswordsFileParser

/**
 * Creates a [FileImporter] that imports passwords from CSV password files.
 *
 * @param context Used to open an [InputStream] from the provided [Uri] via [Context.getContentResolver].
 * @param loginsStorage Receives parsed entries via [LoginsStorage.addMany], which inserts all entries in a single call
 *   and reports per-entry success.
 * @param parser Parses the [InputStream] into a list of [mozilla.components.concept.storage.LoginEntry].
 * @param ioDispatcher Dispatcher used for I/O and parsing work.
 */
fun FileImporter.Companion.csvImporter(
    context: Context,
    loginsStorage: LoginsStorage,
    parser: FileParser<PasswordsParseResult> = CsvPasswordsFileParser(),
    ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
): FileImporter<PasswordsImportResult> =
    CsvPasswordsFileImporter(
        uriOpener = UriOpener.make(context, ioDispatcher),
        parser = parser,
        loginsStorage = loginsStorage,
    )

/**
 * Password import result.
 *
 * @property count The number of passwords imported.
 */
data class PasswordsImportResult(override val count: Int) : ImportResult

internal fun interface UriOpener {
    suspend fun open(uri: Uri): Result<InputStream>

    companion object {
        fun make(context: Context, ioDispatcher: CoroutineDispatcher) = UriOpener { uri ->
            withContext(ioDispatcher) {
                runCatching {
                    requireNotNull(context.contentResolver.openInputStream(uri))
                }
            }
        }
    }
}

internal class CsvPasswordsFileImporter(
    private val uriOpener: UriOpener,
    private val parser: FileParser<PasswordsParseResult>,
    private val loginsStorage: LoginsStorage,
) : FileImporter<PasswordsImportResult> {

    override val mimeType = "text/comma-separated-values"

    override suspend fun importFromUri(uri: Uri): Result<PasswordsImportResult> = runCatching {
        val inputStream = uriOpener.open(uri).getOrThrow()
        val parseResult = inputStream.use { parser.parse(it) }.getOrThrow()
        val imported = loginsStorage.addMany(parseResult.logins).count { it.isSuccess }
        PasswordsImportResult(count = imported)
    }
        .onFailure {
            if (it is CancellationException) throw it
        }
}
