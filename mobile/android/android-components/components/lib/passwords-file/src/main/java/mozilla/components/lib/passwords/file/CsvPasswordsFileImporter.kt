/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.passwords.file

import android.net.Uri
import mozilla.components.concept.passwords.file.PasswordsFileImporter
import mozilla.components.concept.passwords.file.PasswordsFileImporter.ImportResult

/**
 * A [PasswordsFileImporter] for CSV password files.
 *
 * This is a stub implementation. CSV parsing and storage insertion will be added in a follow-up.
 */
class CsvPasswordsFileImporter : PasswordsFileImporter {
    override suspend fun importPasswordsFromUri(uri: Uri): Result<ImportResult> =
        Result.success(ImportResult(count = 0))
}
