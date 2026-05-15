/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.passwords.file

import android.net.Uri

/**
 * An interface for importing passwords from a file.
 */
fun interface PasswordsFileImporter {
    /**
     * The result of a passwords file import.
     *
     * @property count The number of passwords imported.
     */
    data class ImportResult(val count: Int)

    /**
     * Imports passwords from the file at the given [uri].
     *
     * @param uri The URI of the file to import passwords from.
     * @return A [Result] containing [ImportResult] on success or an exception on failure.
     */
    suspend fun importPasswordsFromUri(uri: Uri): Result<ImportResult>

    companion object {
        /**
         * Creates a [PasswordsFileImporter] that always returns a successful [Result].
         *
         * @param result The [ImportResult] to return on every import.
         */
        fun alwaysSuccess(
            result: ImportResult = ImportResult(count = 0),
        ): PasswordsFileImporter = PasswordsFileImporter { Result.success(result) }

        /**
         * Creates a [PasswordsFileImporter] that always returns a failed [Result].
         *
         * @param exception The [Exception] to return on every import.
         */
        fun alwaysFailure(
            exception: Exception = IllegalStateException("Import failed"),
        ): PasswordsFileImporter = PasswordsFileImporter { Result.failure(exception) }
    }
}
