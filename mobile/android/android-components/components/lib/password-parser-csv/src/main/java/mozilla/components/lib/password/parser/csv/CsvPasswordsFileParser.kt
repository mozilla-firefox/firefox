/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.password.parser.csv

import java.io.InputStream
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.concept.fileparser.FileParser
import mozilla.components.concept.fileparser.FileParserException
import mozilla.components.concept.storage.LoginEntry

/** A [FileParser] that reads CSV exports of password data. */
class CsvPasswordsFileParser(private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO) :
    FileParser<PasswordsParseResult> {

    override suspend fun parse(inputStream: InputStream): Result<PasswordsParseResult> = runCatching {
        withContext(ioDispatcher) {
            val text = inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
            parseCsvText(stripBom(text))
        }
    }
        .onFailure {
            if (it is CancellationException) throw it
        }

    private fun parseCsvText(text: String): PasswordsParseResult {
        val rows =
            try {
                readRows(text)
            } catch (e: ParsingFailedException) {
                throw FileParserException(
                    errorType = PasswordsParseError.Unexpected,
                    message = "Malformed CSV",
                    cause = e,
                )
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

            // This follows the validation rules from AS login.rs:
            // https://github.com/mozilla/application-services/blob/af81da7208a53c47e3ada8b17a4a172a25bd9d87/components/logins/src/login.rs#L862-L868
            // Exactly one of `httpRealm` and `formActionOrigin` must be present to be a valid login entry.
            // If both are null, the record will throw. To avoid that, we set `formActionOrigin` to an empty string.
            // It matches any form action on a webpage, and follows the desktop logic found in LoginHelper.sys.mjs:
            // https://searchfox.org/firefox-main/rev/cf103b9362d64aa14c7adc0c0e4b9469ccbf6caa/toolkit/components/passwordmgr/LoginHelper.sys.mjs#296
            // If both are present, the `httpRealm` will be cleared by the Rust code.
            // So we should convert empty records to null and not to an empty string.
            val httpRealm = row.cellAt(httpRealmIdx).ifEmpty { null }
            val formActionCell = row.cellAt(formActionIdx)
            val formActionOrigin =
                when {
                    formActionCell.isNotEmpty() -> formActionCell
                    httpRealm == null -> ""
                    else -> null
                }

            logins +=
                LoginEntry(
                    origin = origin,
                    username = row.cellAt(usernameIdx),
                    password = password,
                    httpRealm = httpRealm,
                    formActionOrigin = formActionOrigin,
                )
        }
        return PasswordsParseResult(logins = logins, skippedRowCount = skipped)
    }

    private fun headerIndices(header: List<String>, hasRows: Boolean): Map<String, Int> {
        val indices = mutableMapOf<String, Int>()
        for ((index, column) in header.withIndex()) {
            val field = COLUMN_TO_FIELD[column.lowercase()] ?: continue
            if (indices.put(field, index) != null) {
                throw FileParserException(
                    errorType = PasswordsParseError.ConflictingColumns,
                    message = "Two CSV columns map to the same login field: $field",
                )
            }
        }
        validateHeaderIndices(indices, hasRows)
        return indices
    }

    private fun validateHeaderIndices(indices: Map<String, Int>, hasRows: Boolean) {
        if (indices.isEmpty()) {
            throw FileParserException(
                errorType = PasswordsParseError.UnsupportedContentType,
                message = "No recognizable login columns in CSV header",
            )
        }
        if (hasRows) {
            val missing = REQUIRED_FIELDS.filter { it !in indices }
            if (missing.isNotEmpty()) {
                throw FileParserException(
                    errorType = PasswordsParseError.FileFormat,
                    message = "Missing required column(s): $missing",
                )
            }
        }
    }

    companion object {
        // Column-name aliases mirror toolkit/components/passwordmgr/LoginCSVImport.sys.mjs,
        // with an addition of "user_name" to support import from Dashlane that uses that format.
        // Unrecognized columns (including guid + timestamps from a Firefox export) are silently
        // ignored — LoginsStorage.add(LoginEntry) doesn't accept that metadata.
        private val COLUMN_TO_FIELD =
            mapOf(
                "url" to "origin",
                "login_uri" to "origin",
                "username" to "username",
                "user_name" to "username",
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

private fun List<String>.cellAt(index: Int?): String = if (index != null && index in indices) this[index] else ""

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
