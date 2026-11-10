/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.fileparser

import android.os.Parcelable

/**
 * Parcelable description of an error that happened while parsing a file.
 *
 * Each parser defines its own specific errors implementing this interface.
 */
interface FileParserError : Parcelable

/**
 * Exception thrown by a [FileParser], carrying the parcelable [errorType] that describes it.
 *
 * @property errorType The [FileParserError] describing what went wrong.
 */
class FileParserException(
    val errorType: FileParserError,
    message: String? = null,
    cause: Throwable? = null,
) : RuntimeException(message, cause)
