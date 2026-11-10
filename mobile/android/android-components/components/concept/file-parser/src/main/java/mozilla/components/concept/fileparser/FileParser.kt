/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.fileparser

import java.io.InputStream

/** An abstract definition of a component that parses files into a result of type [T]. */
fun interface FileParser<T> {

    /**
     * Parses the content in an [inputStream] and returns the parsed result.
     *
     * @param inputStream [InputStream] representing the file content.
     * @return A [Result] containing the parsed [T] or a failure if the file could not be parsed.
     */
    suspend fun parse(inputStream: InputStream): Result<T>

    companion object {

        /** Returns a [FileParser] that always succeeds, returning [result]. */
        fun <T> fakeSuccess(result: T): FileParser<T> = FileParser {
            Result.success(result)
        }

        /** Returns a [FileParser] that always fails with [exception]. */
        fun <T> fakeFailure(exception: Exception = IllegalStateException("couldn't parse it")): FileParser<T> =
            FileParser {
                Result.failure(exception)
            }
    }
}
