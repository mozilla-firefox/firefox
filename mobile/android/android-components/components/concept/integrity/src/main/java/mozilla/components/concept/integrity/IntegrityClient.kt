/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.integrity

/**
 * A value type describing an Integrity Token returned by an [IntegrityClient]
 */
@JvmInline
value class IntegrityToken(val value: String)

/**
 * Interface used to fetch an integrity token
 */
fun interface IntegrityClient {
    /**
     * Requests an [IntegrityToken]
     */
    suspend fun request(): Result<IntegrityToken>

    companion object {
        val testSuccess: IntegrityClient
            get() = IntegrityClient {
                Result.success(IntegrityToken("my-integrity-token"))
            }
    }
}
