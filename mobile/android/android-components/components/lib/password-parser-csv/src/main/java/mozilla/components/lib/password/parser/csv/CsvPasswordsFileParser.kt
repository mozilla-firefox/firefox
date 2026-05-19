/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.password.parser.csv

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.concept.password.parser.PasswordsFileParser
import mozilla.components.concept.password.parser.PasswordsParseResult
import mozilla.components.concept.password.parser.PasswordsParserError
import mozilla.components.concept.storage.LoginEntry
import java.io.InputStream

/**
 * A [PasswordsFileParser] that reads CSV exports of password data.
 */
class CsvPasswordsFileParser(
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : PasswordsFileParser {

    override suspend fun parse(inputStream: InputStream): Result<PasswordsParseResult> =
        runCatching {
            withContext(ioDispatcher) {
                val text = inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
                parseCsvText(stripBom(text))
            }
        }.onFailure {
            if (it is CancellationException) throw it
        }

    private fun parseCsvText(text: String): PasswordsParseResult {
        val rows = try {
            readRows(text)
        } catch (e: ParsingFailedException) {
            throw PasswordsParserError.UnexpectedError("Malformed CSV", e)
        }
        val header = rows.firstOrNull().orEmpty()
        val dataRows = if (rows.size > 1) rows.subList(1, rows.size) else emptyList()
        val indices = headerIndices(header, hasRows = dataRows.isNotEmpty())
        val originIdx = indices["origin"]
        val usernameIdx = indices["username"]
        val passwordIdx = indices["password"]
        val httpRealmIdx = indices["httpRealm"]
        val formActionIdx = indices["formActionOrigin"]

        var skipped = 0
        val logins = ArrayList<LoginEntry>(dataRows.size)
        for (row in dataRows) {
            val origin = row.cellAt(originIdx)
            val password = row.cellAt(passwordIdx)
            if (origin.isEmpty() || password.isEmpty()) {
                // Username value can be empty; origin and password cannot.
                skipped++
                continue
            }
            logins += LoginEntry(
                origin = origin,
                username = row.cellAt(usernameIdx),
                password = password,
                // Empty httpRealm signals a form login (matches desktop LoginCSVImport).
                httpRealm = row.cellAt(httpRealmIdx).ifEmpty { null },
                formActionOrigin = formActionIdx?.let { row.cellAt(it) },
            )
        }
        return PasswordsParseResult(logins = logins, skippedRowCount = skipped)
    }

    private fun headerIndices(header: List<String>, hasRows: Boolean): Map<String, Int> {
        val indices = mutableMapOf<String, Int>()
        for ((index, column) in header.withIndex()) {
            val field = COLUMN_TO_FIELD[column.lowercase()] ?: continue
            if (indices.put(field, index) != null) {
                throw PasswordsParserError.ConflictingColumnsError(
                    "Two CSV columns map to the same login field: $field",
                )
            }
        }
        validateHeaderIndices(indices, hasRows)
        return indices
    }

    private fun validateHeaderIndices(indices: Map<String, Int>, hasRows: Boolean) {
        if (indices.isEmpty()) {
            throw PasswordsParserError.FileFormatError("No recognizable login columns in CSV header")
        }
        if (hasRows) {
            val missing = REQUIRED_FIELDS.filter { it !in indices }
            if (missing.isNotEmpty()) {
                throw PasswordsParserError.FileFormatError("Missing required column(s): $missing")
            }
        }
    }

    companion object {
        // Column-name aliases mirror toolkit/components/passwordmgr/LoginCSVImport.sys.mjs.
        // Unrecognized columns (including guid + timestamps from a Firefox export) are silently
        // ignored — LoginsStorage.add(LoginEntry) doesn't accept that metadata.
        private val COLUMN_TO_FIELD = mapOf(
            "url" to "origin",
            "login_uri" to "origin",
            "username" to "username",
            "login_username" to "username",
            "password" to "password",
            "login_password" to "password",
            "httprealm" to "httpRealm",
            "formactionorigin" to "formActionOrigin",
        )

        private val REQUIRED_FIELDS = listOf("origin", "username", "password")

        private const val UTF8_BOM_CODE = 0xFEFF

        private fun stripBom(text: String): String =
            if (text.isNotEmpty() && text[0].code == UTF8_BOM_CODE) text.substring(1) else text
    }
}

// ---------------------------------------------------------------------------
// RFC 4180 CSV reader — port of toolkit/components/passwordmgr/CSV.sys.mjs.
// ---------------------------------------------------------------------------

private class ParsingFailedException(message: String = "Malformed CSV") : RuntimeException(message)

private const val DELIMITER = ','

private fun readRows(text: String): List<List<String>> {
    val cursor = CsvCursor(text)
    val rows = mutableListOf<List<String>>()
    cursor.skipLineBreaks()
    while (!cursor.atEnd()) {
        val row = cursor.readRow()
        if (row.isNotEmpty()) rows.add(row)
    }
    return rows
}

private fun List<String>.cellAt(index: Int?): String =
    if (index != null && index in indices) this[index] else ""

private class CsvCursor(private val text: String) {
    private var i = 0
    private val n = text.length

    fun atEnd(): Boolean = i >= n

    fun skipLineBreaks() {
        while (i < n && (text[i] == '\r' || text[i] == '\n')) i++
    }

    fun readRow(): List<String> {
        val row = mutableListOf<String>()
        while (!atEnd() && !atLineBreak()) {
            row.add(readField())
            if (!atEnd() && text[i] == DELIMITER) {
                i++
                if (atEnd() || atLineBreak()) row.add("")
            }
        }
        skipLineBreaks()
        return if (row.size == 1 && row[0].isEmpty()) emptyList() else row
    }

    private fun atLineBreak(): Boolean = text[i] == '\r' || text[i] == '\n'

    private fun readField(): String {
        if (text[i] == '"') {
            i++
            val quoted = readQuoted()
            // After a quoted value, the only legal next chars are delimiter, line break, or EOF.
            if (!atEnd() && text[i] != DELIMITER && !atLineBreak()) {
                throw ParsingFailedException("Value after closing quote")
            }
            return quoted
        }
        return readUnquoted()
    }

    private fun readQuoted(): String {
        val sb = StringBuilder()
        while (i < n) {
            if (text[i] == '"') {
                if (i + 1 < n && text[i + 1] == '"') {
                    sb.append('"')
                    i += 2
                    continue
                }
                i++
                return sb.toString()
            }
            sb.append(text[i])
            i++
        }
        throw ParsingFailedException("Unterminated quoted value")
    }

    private fun readUnquoted(): String {
        val start = i
        while (i < n && text[i] != DELIMITER && !atLineBreak()) i++
        return text.substring(start, i)
    }
}
