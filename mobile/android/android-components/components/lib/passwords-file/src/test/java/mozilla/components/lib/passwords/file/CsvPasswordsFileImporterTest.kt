/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.passwords.file

import android.net.Uri
import java.io.ByteArrayInputStream
import java.io.IOException
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.concept.fileimporter.FileImporterException
import mozilla.components.concept.fileparser.FileParser
import mozilla.components.concept.fileparser.FileParserException
import mozilla.components.concept.storage.Login
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.lib.password.parser.csv.PasswordsParseError
import mozilla.components.lib.password.parser.csv.PasswordsParseResult
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class CsvPasswordsFileImporterTest {
    private val testUri = Uri.EMPTY

    @Test
    fun `importFromUri inserts each parsed entry and returns total count`() = runTest {
        val entries = threeLogins()
        val storage: LoginsStorage = mock()
        whenever(storage.addMany(any())).thenAnswer { invocation ->
            invocation.getArgument<List<LoginEntry>>(0).map { Result.success(stubLogin(it)) }
        }
        val importer =
            createImporter(
                parser = FileParser.fakeSuccess(PasswordsParseResult(logins = entries, skippedRowCount = 0)),
                loginsStorage = storage,
            )

        val result = importer.importFromUri(testUri).getOrThrow()

        assertEquals(3, result.count)
    }

    @Test
    fun `importFromUri returns failure when uri cannot be opened`() = runTest {
        val importer = createImporter(uriOpener = { Result.failure(IOException("cannot open")) })

        val result = importer.importFromUri(testUri)

        assertTrue(result.isFailure)
        assertEquals(FileImporterError.ReadError, (result.exceptionOrNull() as? FileImporterException)?.errorType)
    }

    @Test
    fun `importFromUri returns failure when parser fails`() = runTest {
        val importer = createImporter(parser = FileParser.fakeFailure())

        val result = importer.importFromUri(testUri)

        assertTrue(result.isFailure)
        assertEquals(
            FileImporterError.ParseError(PasswordsParseError.Unexpected),
            (result.exceptionOrNull() as? FileImporterException)?.errorType,
        )
    }

    @Test
    fun `importFromUri counts only successful storage writes`() = runTest {
        val entries = threeLogins()
        val storage: LoginsStorage = mock()
        whenever(storage.addMany(entries))
            .thenReturn(
                listOf(
                    Result.success(stubLogin(entries[0])),
                    Result.failure(RuntimeException("duplicate")),
                    Result.success(stubLogin(entries[2])),
                )
            )
        val importer =
            createImporter(
                parser = FileParser.fakeSuccess(PasswordsParseResult(logins = entries, skippedRowCount = 0)),
                loginsStorage = storage,
            )

        val result = importer.importFromUri(testUri).getOrThrow()

        assertEquals(2, result.count)
    }

    @Test
    fun `GIVEN the parser fails with a FileParserException WHEN importing THEN the parser error is reported`() =
        runTest {
            val importer =
                createImporter(
                    parser = FileParser.fakeFailure(FileParserException(PasswordsParseError.ConflictingColumns))
                )

            val result = importer.importFromUri(testUri)

            assertEquals(
                FileImporterError.ParseError(PasswordsParseError.ConflictingColumns),
                (result.exceptionOrNull() as? FileImporterException)?.errorType,
            )
        }

    @Test
    fun `GIVEN storage fails WHEN importing THEN a save error is reported`() = runTest {
        val storage: LoginsStorage = mock()
        whenever(storage.addMany(any())).thenThrow(RuntimeException("storage failed"))
        val importer = createImporter(loginsStorage = storage)

        val result = importer.importFromUri(testUri)

        assertEquals(FileImporterError.SaveError, (result.exceptionOrNull() as? FileImporterException)?.errorType)
    }

    private fun threeLogins() =
        listOf(
            LoginEntry(origin = "https://a.example", username = "a", password = "p1"),
            LoginEntry(origin = "https://b.example", username = "b", password = "p2"),
            LoginEntry(origin = "https://c.example", username = "c", password = "p3"),
        )

    private fun stubLogin(entry: LoginEntry) =
        Login(
            guid = entry.origin,
            username = entry.username,
            password = entry.password,
            origin = entry.origin,
        )

    private fun createImporter(
        uriOpener: UriOpener = UriOpener { Result.success(ByteArrayInputStream(ByteArray(0))) },
        parser: FileParser<PasswordsParseResult> =
            FileParser.fakeSuccess(PasswordsParseResult(logins = emptyList(), skippedRowCount = 0)),
        loginsStorage: LoginsStorage = mock(),
    ) =
        CsvPasswordsFileImporter(
            uriOpener = uriOpener,
            parser = parser,
            loginsStorage = loginsStorage,
        )
}
