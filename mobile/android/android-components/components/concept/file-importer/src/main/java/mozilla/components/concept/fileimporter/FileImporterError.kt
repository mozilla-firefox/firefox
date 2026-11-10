/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.fileimporter

import android.os.Parcelable
import kotlinx.parcelize.Parcelize
import mozilla.components.concept.fileparser.FileParserError

/** Parcelable description of an error that happened during a file import. */
@Parcelize
sealed interface FileImporterError : Parcelable {
    /** An error occurred while reading the file. */
    data object ReadError : FileImporterError

    /** An error occurred while saving the imported data. */
    data object SaveError : FileImporterError

    /** An unknown error happened while importing the file. */
    data object UnknownError : FileImporterError

    /**
     * An error occurred while parsing the file.
     *
     * @property error The [FileParserError] reported by the parser.
     */
    data class ParseError(val error: FileParserError) : FileImporterError
}

/**
 * Exception thrown during a file import, carrying the parcelable [errorType] that describes it.
 *
 * @property errorType The [FileImporterError] describing what went wrong.
 */
class FileImporterException(
    val errorType: FileImporterError,
    message: String? = null,
    cause: Throwable? = null,
) : RuntimeException(message, cause)
