/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.passwords.file

import android.net.Uri
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.password.parser.PasswordsFileParser
import mozilla.components.concept.storage.Login
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import java.io.ByteArrayInputStream
import java.io.IOException
import kotlin.test.assertIs

@RunWith(RobolectricTestRunner::class)
class CsvPasswordsFileImporterTest {
    private val testUri = Uri.EMPTY

    @Test
    fun `importPasswordsFromUri inserts each parsed entry and returns total count`() = runTest {
        val entries = threeLogins()
        val storage: LoginsStorage = mock()
        whenever(storage.addMany(any())).thenAnswer { invocation ->
            invocation.getArgument<List<LoginEntry>>(0).map { Result.success(stubLogin(it)) }
        }
        val importer = createImporter(
            parser = PasswordsFileParser.fakeSuccess(entries),
            loginsStorage = storage,
        )

        val result = importer.importPasswordsFromUri(testUri).getOrThrow()

        assertEquals(3, result.count)
    }

    @Test
    fun `importPasswordsFromUri returns failure when uri cannot be opened`() = runTest {
        val importer = createImporter(
            uriOpener = { Result.failure(IOException("cannot open")) },
        )

        val result = importer.importPasswordsFromUri(testUri)

        assertTrue(result.isFailure)
        assertIs<IOException>(result.exceptionOrNull())
    }

    @Test
    fun `importPasswordsFromUri returns failure when parser fails`() = runTest {
        val importer = createImporter(parser = PasswordsFileParser.fakeFailure())

        val result = importer.importPasswordsFromUri(testUri)

        assertTrue(result.isFailure)
    }

    @Test
    fun `importPasswordsFromUri counts only successful storage writes`() = runTest {
        val entries = threeLogins()
        val storage: LoginsStorage = mock()
        whenever(storage.addMany(entries)).thenReturn(
            listOf(
                Result.success(stubLogin(entries[0])),
                Result.failure(RuntimeException("duplicate")),
                Result.success(stubLogin(entries[2])),
            ),
        )
        val importer = createImporter(
            parser = PasswordsFileParser.fakeSuccess(entries),
            loginsStorage = storage,
        )

        val result = importer.importPasswordsFromUri(testUri).getOrThrow()

        assertEquals(2, result.count)
    }

    private fun threeLogins() = listOf(
        LoginEntry(origin = "https://a.example", username = "a", password = "p1"),
        LoginEntry(origin = "https://b.example", username = "b", password = "p2"),
        LoginEntry(origin = "https://c.example", username = "c", password = "p3"),
    )

    private fun stubLogin(entry: LoginEntry) = Login(
        guid = entry.origin,
        username = entry.username,
        password = entry.password,
        origin = entry.origin,
    )

    private fun createImporter(
        uriOpener: UriOpener = UriOpener { Result.success(ByteArrayInputStream(ByteArray(0))) },
        parser: PasswordsFileParser = PasswordsFileParser.fakeSuccess(),
        loginsStorage: LoginsStorage = mock(),
    ) = CsvPasswordsFileImporter(
        uriOpener = uriOpener,
        parser = parser,
        loginsStorage = loginsStorage,
    )
}
