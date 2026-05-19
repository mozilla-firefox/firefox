/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.password.parser.csv

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.password.parser.PasswordsParserError
import mozilla.components.concept.storage.LoginEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.test.assertIs

@OptIn(ExperimentalCoroutinesApi::class)
class CsvPasswordsFileParserTest {

    private val parser = CsvPasswordsFileParser(ioDispatcher = UnconfinedTestDispatcher())

    @Test
    fun `parses chrome export`() = runTest {
        val csv = """
            name,url,username,password,note
            Example,https://example.com,alice,hunter2,a note
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(0, result.skippedRowCount)
        assertEquals(
            listOf(
                LoginEntry(
                    origin = "https://example.com",
                    username = "alice",
                    password = "hunter2",
                ),
            ),
            result.logins,
        )
    }

    @Test
    fun `parses multi-row firefox export with form and HTTP-auth logins`() = runTest {
        val csv = "﻿" +
            "\"url\",\"username\",\"password\",\"httpRealm\",\"formActionOrigin\"," +
            "\"guid\",\"timeCreated\",\"timeLastUsed\",\"timePasswordChanged\"\n" +
            "\"https://example.com\",\"alice\",\"hun,ter2\",,\"https://example.com/login\"," +
            "\"{guid-1}\",\"1700\",\"1700\",\"1700\"\n" +
            "\"https://intranet.example.com\",\"carol\",\"hunter3\",\"Corp Realm\",," +
            "\"{guid-2}\",\"1710\",\"1711\",\"1712\""

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(0, result.skippedRowCount)
        assertEquals(
            listOf(
                LoginEntry(
                    origin = "https://example.com",
                    username = "alice",
                    password = "hun,ter2",
                    httpRealm = null,
                    formActionOrigin = "https://example.com/login",
                ),
                LoginEntry(
                    origin = "https://intranet.example.com",
                    username = "carol",
                    password = "hunter3",
                    httpRealm = "Corp Realm",
                    formActionOrigin = "",
                ),
            ),
            result.logins,
        )
    }

    @Test
    fun `parses 1Password export`() = runTest {
        val csv = """
            Title,Url,Username,Password,OTPAuth,Favorite,Archived,Tags,Notes
            Example,https://example.com,alice,"hun""ter2",,false,false,Starter Kit,a note
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(
            LoginEntry(
                origin = "https://example.com",
                username = "alice",
                password = "hun\"ter2",
            ),
            result.logins.single(),
        )
    }

    @Test
    fun `parses Bitwarden export using login_ aliases`() = runTest {
        val csv = """
            folder,favorite,type,name,notes,fields,reprompt,archivedDate,login_uri,login_username,login_password,login_totp
            ,,login,Example,,,0,,https://example.com,alice,hunter2,
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(
            LoginEntry(origin = "https://example.com", username = "alice", password = "hunter2"),
            result.logins.single(),
        )
    }

    @Test
    fun `parses Dashlane export with multi-line quoted note`() = runTest {
        val csv = "username,username2,username3,title,password,note,url,category,otpUrl\n" +
            "alice,,,example.com,hunter2," +
            "\"formactionorigin: https://example.com\nguid: {abc}\",https://example.com,,"

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(
            LoginEntry(origin = "https://example.com", username = "alice", password = "hunter2"),
            result.logins.single(),
        )
    }

    @Test
    fun `parses LastPass export`() = runTest {
        val csv = """
            url,username,password,totp,extra,name,grouping,fav
            https://example.com,alice,hunter2,,,Example,Email,0
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(
            LoginEntry(origin = "https://example.com", username = "alice", password = "hunter2"),
            result.logins.single(),
        )
    }

    @Test
    fun `parses Safari export`() = runTest {
        val csv = """
            Title,URL,Username,Password,Notes,OTPAuth
            Example,https://example.com,alice,hunter2,,
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(
            LoginEntry(origin = "https://example.com", username = "alice", password = "hunter2"),
            result.logins.single(),
        )
    }

    @Test
    fun `empty httpRealm becomes null`() = runTest {
        val csv = """
            url,username,password,httpRealm
            https://example.com,alice,hunter2,
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertNull(result.logins.single().httpRealm)
    }

    @Test
    fun `empty formActionOrigin is preserved as empty string`() = runTest {
        // Matches desktop LoginCSVImport, which only null-coerces httpRealm.
        val csv = """
            url,username,password,formActionOrigin
            https://example.com,alice,hunter2,
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals("", result.logins.single().formActionOrigin)
    }

    @Test
    fun `CRLF line endings are accepted`() = runTest {
        val csv = "url,username,password\r\nhttps://example.com,alice,hunter2\r\n"

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(
            LoginEntry(origin = "https://example.com", username = "alice", password = "hunter2"),
            result.logins.single(),
        )
    }

    @Test
    fun `CR-only line endings are accepted`() = runTest {
        val csv = "url,username,password\rhttps://example.com,alice,hunter2"

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(
            LoginEntry(origin = "https://example.com", username = "alice", password = "hunter2"),
            result.logins.single(),
        )
    }

    @Test
    fun `header-only file is a successful empty import`() = runTest {
        val csv = "url,username,password\n"

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(emptyList<LoginEntry>(), result.logins)
        assertEquals(0, result.skippedRowCount)
    }

    @Test
    fun `quoted fields with embedded newlines and escaped quotes`() = runTest {
        val csv = "url,username,password\nhttps://example.com,alice,\"line1\nline2 \"\"quoted\"\"\""

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals("line1\nline2 \"quoted\"", result.logins.single().password)
    }

    @Test
    fun `conflicting header columns mapped to same field`() = runTest {
        val csv = """
            url,login_uri,username,password
            https://a.example.com,https://b.example.com,alice,hunter2
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream())

        assertTrue(result.isFailure)
        assertIs<PasswordsParserError.ConflictingColumnsError>(result.exceptionOrNull())
    }

    @Test
    fun `missing required column`() = runTest {
        val csv = """
            url,username
            https://example.com,alice
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream())

        assertTrue(result.isFailure)
        assertIs<PasswordsParserError.FileFormatError>(result.exceptionOrNull())
    }

    @Test
    fun `row with empty password is skipped`() = runTest {
        val csv = """
            url,username,password
            https://example.com,alice,hunter2
            https://other.example.com,bob,
        """.trimIndent()

        val result = parser.parse(csv.byteInputStream()).getOrThrow()

        assertEquals(1, result.logins.size)
        assertEquals(1, result.skippedRowCount)
        assertEquals("alice", result.logins.first().username)
    }

    @Test
    fun `malformed CSV returns UnexpectedError`() = runTest {
        val csv = "url,username,password\nhttps://example.com,\"alice,hunter2"

        val result = parser.parse(csv.byteInputStream())

        assertTrue(result.isFailure)
        assertIs<PasswordsParserError.UnexpectedError>(result.exceptionOrNull())
    }

    @Test
    fun `CancellationException from the stream is not swallowed by runCatching`() = runTest {
        val cancelling = object : java.io.InputStream() {
            override fun read(): Int = throw CancellationException("cancelled while reading")
        }
        var caught: Throwable? = null

        try {
            parser.parse(cancelling)
        } catch (e: Throwable) {
            caught = e
        }

        assertIs<CancellationException>(caught)
    }
}
