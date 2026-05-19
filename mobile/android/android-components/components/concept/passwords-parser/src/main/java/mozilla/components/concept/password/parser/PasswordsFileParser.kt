/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.password.parser

import mozilla.components.concept.storage.LoginEntry
import java.io.InputStream

/**
 * An error type returned when parsing a passwords file fails.
 */
sealed class PasswordsParserError(
    override val message: String,
    override val cause: Throwable? = null,
) : RuntimeException(message, cause) {

    /**
     * The file is missing a required column (origin, username, or password) or could not be
     * recognized as a passwords file at all.
     */
    class FileFormatError(message: String) : PasswordsParserError(message)

    /**
     * Two distinct columns in the header map to the same logical field — for example, a file
     * containing both `url` and `login_uri`. The correct value cannot be chosen unambiguously.
     */
    class ConflictingColumnsError(message: String) : PasswordsParserError(message)

    /**
     * Any other parsing-related error: I/O, malformed CSV, etc.
     */
    class UnexpectedError(message: String, cause: Throwable?) : PasswordsParserError(message, cause)
}

/**
 * Result of parsing a passwords file.
 *
 * @property logins The successfully parsed [LoginEntry] records.
 * @property skippedRowCount The number of rows that were skipped because a required value
 * (origin, username, or password) was empty.
 */
data class PasswordsParseResult(
    val logins: List<LoginEntry>,
    val skippedRowCount: Int,
)

/**
 * An abstract definition of a component that parses password files into a list of [LoginEntry]
 * records.
 */
fun interface PasswordsFileParser {

    /**
     * Parses the content in an [inputStream] and returns a [PasswordsParseResult].
     *
     * @param inputStream The bytes of the passwords file.
     * @return A [Result] containing the parsed [PasswordsParseResult] on success, or a
     * [PasswordsParserError] on failure.
     */
    suspend fun parse(inputStream: InputStream): Result<PasswordsParseResult>

    companion object {

        /**
         * Returns a [PasswordsFileParser] that always succeeds, returning the given [logins].
         */
        fun fakeSuccess(logins: List<LoginEntry> = emptyList()): PasswordsFileParser =
            PasswordsFileParser {
                Result.success(PasswordsParseResult(logins = logins, skippedRowCount = 0))
            }

        /**
         * Returns a [PasswordsFileParser] that always fails.
         */
        fun fakeFailure(): PasswordsFileParser = PasswordsFileParser {
            Result.failure(PasswordsParserError.UnexpectedError("couldn't parse it", null))
        }
    }
}
