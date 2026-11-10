/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.password.parser.csv

import kotlin.test.assertIs
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.fileparser.FileParserException
import mozilla.components.concept.storage.LoginEntry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class CsvPasswordsFileParserTest {

    private val parser = CsvPasswordsFileParser(ioDispatcher = UnconfinedTestDispatcher())

    @Test
    fun `GIVEN a default csv in WHEN parsing input THEN default login entry out`() = runTest {
        CsvFormatType.entries.forEach { csvType ->
            val result = parser.parse(buildCsv(csvType).byteInputStream()).getOrThrow().logins.single()

            assertEquals("format=$csvType", getDefaultLoginEntry(), result)
        }
    }

    @Test
    fun `GIVEN a firefox export mixing form and HTTP auth logins WHEN parsing input THEN each login keeps its target`() =
        runTest {
            val csv =
                buildCsv(
                    CsvFormatType.FIREFOX,
                    MockRow(formActionOrigin = "https://adventure.com/login"),
                    MockRow(
                        url = "https://intranet.example.com",
                        username = "carol",
                        password = "hunter3",
                        httpRealm = "Corp Realm",
                    ),
                )

            val result = parser.parse(csv.byteInputStream()).getOrThrow().logins

            assertEquals(
                listOf(
                    getDefaultLoginEntry(formActionOrigin = "https://adventure.com/login"),
                    getDefaultLoginEntry(
                        origin = "https://intranet.example.com",
                        formActionOrigin = null,
                        httpRealm = "Corp Realm",
                        username = "carol",
                        password = "hunter3",
                    ),
                ),
                result,
            )
        }

    @Test
    fun `GIVEN a comma in the password WHEN parsing input THEN the comma is preserved in the entry`() = runTest {
        val password = "hun,ter2"
        CsvFormatType.entries.forEach { csvType ->
            val csv = buildCsv(csvType, MockRow(password = csvType.quote(password)))

            val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single()

            assertEquals("format=$csvType", getDefaultLoginEntry(password = password), result)
        }
    }

    @Test
    fun `GIVEN a double quote in the password WHEN parsing input THEN the double quote is preserved in the entry`() =
        runTest {
            val password = "hun\"ter2"
            CsvFormatType.entries.forEach { csvType ->
                val csv = buildCsv(csvType, MockRow(password = csvType.quote("hun\"\"ter2")))

                val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single()

                assertEquals("format=$csvType", getDefaultLoginEntry(password = password), result)
            }
        }

    @Test
    fun `GIVEN multi-line note field WHEN parsing input THEN does not throw`() = runTest {
        val multiLineNote = "\"formactionorigin: https://example.com\nguid: {abc}\""
        // the firefox format has no note column
        (CsvFormatType.entries - CsvFormatType.FIREFOX).forEach { csvType ->
            val csv = buildCsv(csvType, MockRow(note = multiLineNote))

            val result = parser.parse(csv.byteInputStream()).getOrThrow()

            assertEquals("format=$csvType", getDefaultLoginEntry(), result.logins.single())
        }
    }

    @Test
    fun `GIVEN multi-row csv WHEN parsing input THEN get multiple login entries`() = runTest {
        val csvRows =
            arrayOf(
                MockRow(),
                MockRow(url = "https://intranet.example.com", username = "carol", password = "hunter3"),
                MockRow(url = "https://example.org", username = "jack", password = "hunter4"),
            )
        val expected = csvRows.map {
            getDefaultLoginEntry(origin = it.url, username = it.username, password = it.password)
        }

        CsvFormatType.entries.forEach { csvType ->
            val csv = buildCsv(csvType, *csvRows)

            val result = parser.parse(csv.byteInputStream()).getOrThrow().logins

            assertEquals("format=$csvType", expected, result)
        }
    }

    @Test
    fun `GIVEN empty httpRealm and filled formActionOrigin WHEN parsing input THEN httpRealm converts to null and formActionOrigin converts to its value`() =
        runTest {
            val httpRealm = ""
            val formActionOrigin = "https://example.com"
            val csv =
                buildCsv(CsvFormatType.FIREFOX, MockRow(httpRealm = httpRealm, formActionOrigin = formActionOrigin))

            val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single()

            assertEquals(getDefaultLoginEntry(httpRealm = null, formActionOrigin = formActionOrigin), result)
        }

    @Test
    fun `GIVEN empty formActionOrigin and filled httpRealm WHEN parsing input THEN formActionOrigin converts to null and httpRealm converts to its value`() =
        runTest {
            val formActionOrigin = ""
            val httpRealm = "Login Required"
            val csv =
                buildCsv(CsvFormatType.FIREFOX, MockRow(httpRealm = httpRealm, formActionOrigin = formActionOrigin))

            val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single()

            assertEquals(getDefaultLoginEntry(formActionOrigin = null, httpRealm = httpRealm), result)
        }

    @Test
    fun `GIVEN both httpRealm and formActionOrigin are empty WHEN parsing input THEN formActionOrigin converts to empty string and httpRealm converts to null`() =
        runTest {
            val formActionOrigin = ""
            val httpRealm = ""
            val csv =
                buildCsv(CsvFormatType.FIREFOX, MockRow(httpRealm = httpRealm, formActionOrigin = formActionOrigin))

            val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single()

            assertEquals(getDefaultLoginEntry(formActionOrigin = formActionOrigin, httpRealm = null), result)
        }

    @Test
    fun `GIVEN both httpRealm and formActionOrigin are NOT empty WHEN parsing input THEN both are converted to its value`() =
        runTest {
            val formActionOrigin = "https://example.com"
            val httpRealm = "Login Required"
            val csv =
                buildCsv(CsvFormatType.FIREFOX, MockRow(httpRealm = httpRealm, formActionOrigin = formActionOrigin))

            val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single()

            assertEquals(getDefaultLoginEntry(formActionOrigin = formActionOrigin, httpRealm = httpRealm), result)
        }

    @Test
    fun `GIVEN CRLF line endings WHEN parsing input THEN do not throw`() = runTest {
        CsvFormatType.entries.forEach { csvType ->
            TRAILING_LINE_BREAK_VARIANTS.forEach { trailing ->
                val csv = (buildCsv(csvType) + trailing).replace("\n", "\r\n")

                val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single()

                assertEquals("format=$csvType, trailing=${trailing.isNotEmpty()}", getDefaultLoginEntry(), result)
            }
        }
    }

    @Test
    fun `GIVEN CR-only line endings WHEN parsing input THEN do not throw`() = runTest {
        CsvFormatType.entries.forEach { csvType ->
            TRAILING_LINE_BREAK_VARIANTS.forEach { trailing ->
                val csv = (buildCsv(csvType) + trailing).replace("\n", "\r")

                val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single()

                assertEquals("format=$csvType, trailing=${trailing.isNotEmpty()}", getDefaultLoginEntry(), result)
            }
        }
    }

    @Test
    fun `GIVEN a UTF-8 BOM at the start of the file WHEN parsing input THEN the BOM is ignored`() = runTest {
        CsvFormatType.entries.forEach { csvType ->
            val csv = "\uFEFF" + buildCsv(csvType)

            val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single()

            assertEquals("format=$csvType", getDefaultLoginEntry(), result)
        }
    }

    @Test
    fun `GIVEN a valid csv with no records WHEN parsing input THEN it does not throw and results in an empty list`() =
        runTest {
            CsvFormatType.entries.forEach { csvType ->
                val result = parser.parse(csvType.header.byteInputStream()).getOrThrow()

                assertEquals("format=$csvType", emptyList<LoginEntry>(), result.logins)
                assertEquals("format=$csvType", 0, result.skippedRowCount)
            }
        }

    @Test
    fun `GIVEN fields with embedded new lines and escaped quotes WHEN parsing input THEN fields are preserved`() =
        runTest {
            val password = "line1\nline2 \"quoted\" text"
            val escapedPassword = "line1\nline2 \"\"quoted\"\" text"
            CsvFormatType.entries.forEach { csvType ->
                val csv = buildCsv(csvType, MockRow(password = csvType.quote(escapedPassword)))

                val result = parser.parse(csv.byteInputStream()).getOrThrow().logins.single().password

                assertEquals("format=$csvType", password, result)
            }
        }

    @Test
    fun `GIVEN conflicting header columns are present at the same time WHEN parsing input THEN throw`() = runTest {
        CsvFormatType.entries.forEach { format ->
            REQUIRED_COLUMN_VARIANTS.flatten().forEach { conflictingColumn ->
                val csv =
                    listOf(
                            // adding a duplicate of a required field, to force a conflict
                            "${format.header},$conflictingColumn",
                            "${format.row(MockRow())},$MOCK_STRING_URL",
                        )
                        .joinToString("\n")

                val result = parser.parse(csv.byteInputStream())

                assertTrue("format=$format, column=$conflictingColumn", result.isFailure)
                assertEquals(
                    "format=$format, column=$conflictingColumn",
                    PasswordsParseError.ConflictingColumns,
                    (result.exceptionOrNull() as? FileParserException)?.errorType,
                )
            }
        }
    }

    @Test
    fun `GIVEN no recognizable login columns WHEN parsing input THEN throw an unsupported content type error`() =
        runTest {
            val csv = "title,author\nAlice in Wonderland,Lewis Carroll"

            val result = parser.parse(csv.byteInputStream())

            assertTrue(result.isFailure)
            assertEquals(
                PasswordsParseError.UnsupportedContentType,
                (result.exceptionOrNull() as? FileParserException)?.errorType,
            )
        }

    @Test
    fun `GIVEN one of the required columns is missing WHEN parsing input THEN throw`() = runTest {
        CsvFormatType.entries.forEach { format ->
            REQUIRED_COLUMN_VARIANTS.forEach { columnVariants ->
                val header =
                    // splitting header into separate elements to replace one of the required columns with a fake one
                    format.header.split(",").joinToString(",") { column ->
                        if (column.trim('"').lowercase() in columnVariants) "cheshire" else column
                    }
                val csv = listOf(header, format.row(MockRow())).joinToString("\n")

                val result = parser.parse(csv.byteInputStream())

                assertTrue("format=$format, columns=$columnVariants", result.isFailure)
                assertEquals(
                    "format=$format, columns=$columnVariants",
                    PasswordsParseError.FileFormat,
                    (result.exceptionOrNull() as? FileParserException)?.errorType,
                )
            }
        }
    }

    @Test
    fun `GIVEN password field is empty WHEN parsing THEN record is skipped`() = runTest {
        CsvFormatType.entries.forEach { format ->
            val csv = buildCsv(format, MockRow(password = MOCK_STRING_EMPTY))

            val result = parser.parse(csv.byteInputStream()).getOrThrow()

            assertTrue("format=$format", result.logins.isEmpty())
            assertEquals("format=$format", 1, result.skippedRowCount)
        }
    }

    @Test
    fun `GIVEN url field is empty WHEN parsing THEN record is skipped`() = runTest {
        CsvFormatType.entries.forEach { format ->
            val csv = buildCsv(format, MockRow(url = MOCK_STRING_EMPTY))

            val result = parser.parse(csv.byteInputStream()).getOrThrow()

            assertTrue("format=$format", result.logins.isEmpty())
            assertEquals("format=$format", 1, result.skippedRowCount)
        }
    }

    @Test
    fun `GIVEN a field is missing a closing quote WHEN parsing input THEN throw UnexpectedError`() = runTest {
        val csv = "url,username,password\nhttps://example.com,\"alice,hunter2"

        val result = parser.parse(csv.byteInputStream())

        assertTrue(result.isFailure)
        assertEquals(PasswordsParseError.Unexpected, (result.exceptionOrNull() as? FileParserException)?.errorType)
    }

    @Test
    fun `GIVEN a field contains characters after a closing quote WHEN parsing input THEN return UnexpectedError`() =
        runTest {
            val csv = "url,username,password\nhttps://example.com,\"alic\"e,hunter2"

            val result = parser.parse(csv.byteInputStream())

            assertTrue(result.isFailure)
            assertEquals(PasswordsParseError.Unexpected, (result.exceptionOrNull() as? FileParserException)?.errorType)
        }

    @Test
    fun `CancellationException from the stream is not swallowed by runCatching`() = runTest {
        val cancelling =
            object : java.io.InputStream() {
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

    private data class MockRow(
        val url: String = MOCK_STRING_URL,
        val username: String = MOCK_STRING_USERNAME,
        val password: String = MOCK_STRING_PASSWORD,
        val httpRealm: String = MOCK_STRING_EMPTY,
        val formActionOrigin: String = MOCK_STRING_EMPTY,
        val name: String = MOCK_STRING_NAME,
        val note: String = MOCK_STRING_NOTE,
    )

    private enum class CsvFormatType(
        val header: String,
        val row: (MockRow) -> String,
    ) {
        // Format taken from an exported Firefox CSV file:
        // "url","username","password","httpRealm","formActionOrigin","guid","timeCreated","timeLastUsed","timePasswordChanged"
        // "https://login.mozilla.com","foxy-the-best@mozilla.com","mzzzzzla","Login Required",,
        // "{3b466874-2350-124a-921a-bc3840432b4f}","1634503989721","1778726471507","1634503989721"
        FIREFOX(
            "\"url\",\"username\",\"password\",\"httpRealm\",\"formActionOrigin\"," +
                "\"guid\",\"timeCreated\",\"timeLastUsed\",\"timePasswordChanged\"",
            {
                "\"${it.url}\",\"${it.username}\",\"${it.password}\",\"${it.httpRealm}\",\"${it.formActionOrigin}\",,,,"
            },
        ),
        // Format source: taken from an exported Chrome CSV file:
        // name,url,username,password,note
        // zoom.us,https://zoom.us/activate,firefox-test,test1234,
        CHROME(
            "name,url,username,password,note",
            { "${it.name},${it.url},${it.username},${it.password},${it.note}" },
        ),
        ONE_PASSWORD(
            "Title,Url,Username,Password,OTPAuth,Favorite,Archived,Tags,Notes",
            { "${it.name},${it.url},${it.username},${it.password},,false,false,$MOCK_STRING_TAG,${it.note}" },
        ),
        BITWARDEN(
            "folder,favorite,type,name,notes,fields,reprompt,archivedDate,login_uri,login_username,login_password,login_totp",
            { ",,login,${it.name},${it.note},,0,,${it.url},${it.username},${it.password}," },
        ),
        // Format source: taken from an exported Dashlane CSV file (on Desktop):
        // username,username2,username3,title,password,note,url,category,otpUrl
        // foxy-the-best@mozilla.com,,,mozilla.com,mzzzzzzla,,https://www.facebook.com,,
        DASHLANE_DESKTOP(
            "username,username2,username3,title,password,note,url,category,otpUrl",
            { "${it.username},,,${it.name},${it.password},${it.note},${it.url},," },
        ),
        // Format source: taken from an exported Dashlane CSV file (on Android):
        // user_name,user_name_2,user_name_3,title,password,note,url,category,otp
        // foxy-the-best@mozilla.com,,,mozilla.com,mzzzzzzla,,https://login.mozilla.com,,null
        DASHLANE_ANDROID(
            "user_name,user_name_2,user_name_3,title,password,note,url,category,otp",
            { "${it.username},,,${it.name},${it.password},${it.note},${it.url},," },
        ),
        LAST_PASS(
            "url,username,password,totp,extra,name,grouping,fav",
            { "${it.url},${it.username},${it.password},,${it.note},${it.name},Email,0" },
        ),
        SAFARI(
            "Title,URL,Username,Password,Notes,OTPAuth",
            { "${it.name},${it.url},${it.username},${it.password},${it.note}," },
        ),
    }

    private fun buildCsv(format: CsvFormatType, vararg rows: MockRow): String =
        (listOf(format.header) + rows.ifEmpty { arrayOf(MockRow()) }.map(format.row)).joinToString("\n")

    // Exporters follow RFC 4180 and wrap a field containing a comma, a double quote or a line break in double quotes,
    // with inner double quotes doubled. The FIREFOX row already wraps every value in double quotes.
    private fun CsvFormatType.quote(escapedValue: String) =
        if (this == CsvFormatType.FIREFOX) escapedValue else "\"$escapedValue\""

    private fun getDefaultLoginEntry(
        origin: String = MOCK_STRING_URL,
        formActionOrigin: String? = MOCK_STRING_EMPTY,
        // formActionOrigin and httpRealm can not both be null, so formActionOrigin defaults to "" when httpRealm is
        // null
        httpRealm: String? = null,
        username: String = MOCK_STRING_USERNAME,
        password: String = MOCK_STRING_PASSWORD,
    ) =
        LoginEntry(
            origin = origin,
            formActionOrigin = formActionOrigin,
            httpRealm = httpRealm,
            username = username,
            password = password,
        )

    companion object {
        const val MOCK_STRING_NAME = "adventure.com"
        const val MOCK_STRING_URL = "https://adventure.com"
        const val MOCK_STRING_USERNAME = "alice"
        const val MOCK_STRING_PASSWORD = "whiterabbit"
        const val MOCK_STRING_NOTE = "Do cats eat bats?"
        const val MOCK_STRING_TAG = "Wonderland"
        const val MOCK_STRING_EMPTY = ""

        // Chrome ends the last row with a line break, Firefox (LoginExport.sys.mjs) does not.
        val TRAILING_LINE_BREAK_VARIANTS = listOf("", "\n")

        // Header column names the parser maps to each of the required origin, username and password fields.
        val REQUIRED_COLUMN_VARIANTS =
            listOf(
                setOf("url", "login_uri"),
                setOf("username", "user_name", "login_username"),
                setOf("password", "login_password"),
            )
    }
}
