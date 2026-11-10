/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.fileimporter

import android.net.Uri

/** The result of a file import. */
interface ImportResult {
    /** The number of items imported. */
    val count: Int
}

/** An interface for importing browser data from a file. */
interface FileImporter<T : ImportResult> {
    /** MIME type of the files this importer can read, used to filter the file picker. */
    val mimeType: String

    /**
     * Imports data from the file at the given [uri].
     *
     * @param uri The URI of the file to import data from.
     * @return A [Result] containing [T] data on success or an exception on failure.
     */
    suspend fun importFromUri(uri: Uri): Result<T>

    companion object
}
