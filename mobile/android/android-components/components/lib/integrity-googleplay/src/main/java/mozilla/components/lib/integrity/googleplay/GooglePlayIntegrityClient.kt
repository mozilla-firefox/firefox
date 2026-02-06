/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.integrity.googleplay

import android.content.Context
import com.google.android.play.core.integrity.IntegrityManagerFactory
import com.google.android.play.core.integrity.StandardIntegrityException
import com.google.android.play.core.integrity.StandardIntegrityManager
import com.google.android.play.core.integrity.model.StandardIntegrityErrorCode.INTEGRITY_TOKEN_PROVIDER_INVALID
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.lib.integrity.googleplay.ext.prepare
import java.util.UUID

/**
 * Represents a Google Cloud project number parsed from an external value (i.e. BuildConfig).
 *
 * This sealed class models the result of attempting to interpret a string
 * as a valid Google project number. It avoids throwing exceptions by
 * explicitly representing invalid input.
 */
sealed class GoogleProjectNumber {

    /**
     * A successfully parsed Google project number.
     *
     * @property value The numeric project number.
     */
    internal data class Valid(val value: Long) : GoogleProjectNumber()

    /**
     * Represents an invalid or unparsable project number.
     */
    internal object Invalid : GoogleProjectNumber()

    companion object {

        /**
         * Attempts to create a [GoogleProjectNumber] from the given string token.
         *
         * The token is considered invalid if it is empty or cannot be parsed
         * as a [Long]. No exceptions are thrown; failures result in [Invalid].
         *
         * @param token The string representation of a Google project number.
         * @return [Valid] if parsing succeeds, otherwise [Invalid].
         */
        fun create(token: String): GoogleProjectNumber {
            if (token.isEmpty()) {
                return Invalid
            }

            return Result.runCatching { token.toLong() }
                .map { Valid(it) }
                .getOrElse { Invalid }
        }
    }
}

/**
 * Provides instances of [StandardIntegrityManager].
 *
 * This interface exists to allow indirection and easier testing
 * when creating integrity managers.
 */
fun interface IntegrityManagerProvider {

    /**
     * Creates a new [StandardIntegrityManager] instance.
     */
    fun create(): StandardIntegrityManager

    companion object {

        /**
         * Creates a default [IntegrityManagerProvider] backed by
         * [IntegrityManagerFactory].
         *
         * @param context The Android [Context] used to initialize the
         * integrity manager.
         * @return A provider that creates [StandardIntegrityManager] instances.
         */
        fun create(context: Context) = IntegrityManagerProvider {
            IntegrityManagerFactory.createStandard(context)
        }
    }
}

/**
 * Provides integrity tokens on demand.
 *
 * Implementations are responsible for requesting and returning an
 * [IntegrityToken].
 */
fun interface TokenProvider {

    /**
     * Requests a new [IntegrityToken].
     *
     * A [RequestHashProvider] is supplied to provide a unique hash for
     * the request.
     *
     * @param requestHashProvider Provider used to generate a request hash.
     * @return A [Result] containing the [IntegrityToken] on success, or
     * a failure if the token request could not be completed.
     */
    suspend fun request(requestHashProvider: RequestHashProvider): Result<IntegrityToken>
}

/**
 * Factory for creating [TokenProvider] instances.
 */
fun interface TokenProviderFactory {

    /**
     * Creates a [TokenProvider].
     *
     * @return A [Result] containing a [TokenProvider] on success, or a failure
     * if the provider could not be created.
     */
    suspend fun create(): Result<TokenProvider>

    companion object {

        /**
         * Returns a concrete [TokenProviderFactory] to be used by [GooglePlayIntegrityClient].
         *
         * @param integrityManagerProvider Provider for creating integrity managers.
         * @param projectNumber The Google Cloud project number to use.
         * @return A [TokenProviderFactory] appropriate for the given configuration.
         */
        fun create(
            integrityManagerProvider: IntegrityManagerProvider,
            projectNumber: GoogleProjectNumber,
        ) = when (projectNumber) {
            is GoogleProjectNumber.Valid ->
                GooglePlayTokenProviderFactory(integrityManagerProvider, projectNumber)

            is GoogleProjectNumber.Invalid ->
                TokenProviderFactory {
                    Result.failure(InvalidProjectNumber())
                }
        }
    }
}

/**
* Generates a hash value to uniquely identify a request.
*
* This functional interface allows the hash generation strategy to be
* customized or mocked, making it suitable for dependency injection
* and testing.
*/
fun interface RequestHashProvider {

    /**
     * Generates a new request hash.
     *
     * Implementations should return a value that is sufficiently unique
     * for the lifetime and scope of a request.
     *
     * @return A newly generated hash string.
     */
    fun generateHash(): String

    companion object {

        /**
         * Creates a [RequestHashProvider] that generates random UUID-based hashes.
         *
         * @return A provider that produces random request hashes.
         */
        fun randomHashProvider() = RequestHashProvider {
            UUID.randomUUID().toString()
        }
    }
}

internal class GooglePlayTokenProviderFactory(
    integrityManagerProvider: IntegrityManagerProvider,
    private val projectNumber: GoogleProjectNumber.Valid,
) : TokenProviderFactory {
    private val integrityManager = integrityManagerProvider.create()

    override suspend fun create() = integrityManager.prepare(projectNumber)
}

/**
 * An [IntegrityClient] backed by Google Play Integrity.
 *
 * @param tokenProviderFactory Factory used to create [TokenProvider] instances.
 * @param requestHashProvider Provider used to generate per-request hashes.
 * @param logger Optional logger for non-fatal errors encountered during
 * provider creation.
 */
class GooglePlayIntegrityClient(
    private val tokenProviderFactory: TokenProviderFactory,
    private val requestHashProvider: RequestHashProvider,
    private val logger: (String) -> Unit = { },
) : IntegrityClient {
    internal var tokenProvider: TokenProvider? = null

    /**
     * Eagerly initializes the underlying [TokenProvider], if needed.
     *
     * This method is safe to call multiple times and will only attempt
     * provider creation once unless the provider is refreshed.
     */
    suspend fun warmUp() {
        if (tokenProvider == null) {
            refreshTokenProvider()
        }
    }

    /**
     * Requests an [IntegrityToken].
     *
     * If no provider is available, or if token creation fails, the error
     * is returned via [Result]. When a token expiration is detected, the
     * provider is refreshed and the request is retried once automatically.
     *
     * @return A [Result] containing an [IntegrityToken] on success, or a
     * failure if the request could not be fulfilled.
     */
    override suspend fun request(): Result<IntegrityToken> {
        warmUp()

        val result =
            tokenProvider?.request(requestHashProvider)
                ?: Result.failure(MissingTokenProvider())

        return result.fold(
            onSuccess = { Result.success(it) },
            onFailure = { throwable ->
                if (throwable.tokenHasExpired) {
                    refreshTokenProvider()
                    return request()
                }

                Result.failure(throwable)
            },
        )
    }

    private suspend fun refreshTokenProvider() {
        tokenProviderFactory.create()
            .onFailure { logger("Failed to create TokenProvider: ${it.message}") }
            .getOrNull()
            ?.also { tokenProvider = it }
    }
}

/**
 * Thrown when a Google project number is required but invalid.
 */
class InvalidProjectNumber :
    IllegalStateException("GoogleProjectNumber is Invalid.")

/**
 * Thrown when a token request is attempted without an initialized
 * [TokenProvider].
 */
class MissingTokenProvider :
    IllegalStateException("GooglePlayIntegrityClient is missing a token provider")

private val Throwable.tokenHasExpired: Boolean
    get() = (this as? StandardIntegrityException)?.tokenProviderHasExpired ?: false

private val StandardIntegrityException.tokenProviderHasExpired: Boolean
    get() = errorCode == INTEGRITY_TOKEN_PROVIDER_INVALID
