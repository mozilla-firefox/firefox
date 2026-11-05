/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.integrity.googleplay

import android.content.Context
import com.google.android.play.core.integrity.IntegrityManagerFactory
import com.google.android.play.core.integrity.StandardIntegrityException
import com.google.android.play.core.integrity.StandardIntegrityManager
import com.google.android.play.core.integrity.model.StandardIntegrityErrorCode.INTEGRITY_TOKEN_PROVIDER_INVALID
import kotlin.time.Duration.Companion.minutes
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withTimeout
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.integrity.IntegrityToken
import mozilla.components.concept.integrity.RequestHashProvider
import mozilla.components.lib.integrity.googleplay.GleanMetrics.Integrity
import mozilla.components.lib.integrity.googleplay.ext.prepare

/**
 * Provides instances of [StandardIntegrityManager].
 *
 * This interface exists to allow indirection and easier testing when creating integrity managers.
 */
internal fun interface IntegrityManagerProvider {

    /** Creates a new [StandardIntegrityManager] instance. */
    fun create(): StandardIntegrityManager

    companion object {

        /**
         * Creates a default [IntegrityManagerProvider] backed by [IntegrityManagerFactory].
         *
         * @param context The Android [Context] used to initialize the integrity manager.
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
 * Implementations are responsible for requesting and returning an [IntegrityToken].
 */
internal fun interface TokenProvider {

    /**
     * Requests a new [IntegrityToken].
     *
     * A [RequestHashProvider] is supplied to provide a unique hash for the request.
     *
     * @param requestHashProvider Provider used to generate a request hash.
     * @return A [Result] containing the [IntegrityToken] on success, or a failure if the token request could not be
     *   completed.
     */
    suspend fun request(requestHashProvider: RequestHashProvider): Result<IntegrityToken>
}

/** Factory for creating [TokenProvider] instances. */
internal fun interface TokenProviderFactory {

    /**
     * Creates a [TokenProvider].
     *
     * @return A [Result] containing a [TokenProvider] on success, or a failure if the provider could not be created.
     */
    suspend fun create(): Result<TokenProvider>

    companion object {

        /**
         * Returns a concrete [TokenProviderFactory] to be used by [GooglePlayIntegrityClient].
         *
         * @param integrityManagerProvider Provider for creating integrity managers.
         * @param projectNumber The Google Cloud project number to use.
         * @return A [TokenProviderFactory] that yields a working [TokenProvider] when [projectNumber] is non-null, or a
         *   failing one otherwise.
         */
        fun create(
            integrityManagerProvider: IntegrityManagerProvider,
            projectNumber: Long?,
        ) =
            if (projectNumber != null) {
                GooglePlayTokenProviderFactory(integrityManagerProvider, projectNumber)
            } else {
                TokenProviderFactory { Result.failure(InvalidProjectNumber()) }
            }
    }
}

/**
 * Identifies the caller that issued an integrity request, used for telemetry attribution in the
 * `integrity.token_request` event.
 */
@JvmInline
value class IntegrityConsumer(val value: String) {
    companion object {
        val Summarize = IntegrityConsumer("summarize")
        val IpProtection = IntegrityConsumer("ip_protection")
        val Unknown = IntegrityConsumer("unknown")
    }
}

internal class GooglePlayTokenProviderFactory(
    integrityManagerProvider: IntegrityManagerProvider,
    private val projectNumber: Long,
) : TokenProviderFactory {
    private val integrityManager by lazy { integrityManagerProvider.create() }

    override suspend fun create() = integrityManager.prepare(projectNumber)
}

/**
 * An [IntegrityClient] backed by Google Play Integrity.
 *
 * @param tokenProviderFactory Factory used to create [TokenProvider] instances.
 * @param requestHashProvider Provider used to generate per-request hashes.
 * @param currentTimeMillis Injectable wall-clock source, used to measure warmup duration. Defaults to
 *   [System.currentTimeMillis]; override in tests.
 */
class GooglePlayIntegrityClient
internal constructor(
    private val tokenProviderFactory: TokenProviderFactory,
    private val requestHashProvider: RequestHashProvider,
    private val currentTimeMillis: () -> Long = { System.currentTimeMillis() },
) : IntegrityClient {
    internal var tokenProvider: Result<TokenProvider>? = null

    companion object {

        /**
         * Creates a [GooglePlayIntegrityClient] from a project number string and [Context].
         *
         * @param context The Android [Context] used to initialize the integrity manager.
         * @param projectNumberToken String representation of the Google Cloud project number.
         * @param requestHashProvider Provider used to generate per-request hashes.
         */
        fun create(
            context: Context,
            projectNumberToken: String,
            requestHashProvider: RequestHashProvider,
        ) =
            GooglePlayIntegrityClient(
                TokenProviderFactory.create(
                    IntegrityManagerProvider.create(context),
                    projectNumberToken.toLongOrNull(),
                ),
                requestHashProvider,
            )
    }

    /**
     * Eagerly initializes the underlying [TokenProvider], if needed.
     *
     * This method is safe to call multiple times and will only perform work if the provider has not been already
     * created or refreshed. If there is already a token provider, it will return that provider's warm-up result.
     *
     * This method will return `false` if the warm-up does not conclude within a duration specified by [WarmUpTimeout].
     *
     * @return true if the warm up was successful, or false if the warm up did not succeed.
     */
    override suspend fun warmUp(): Boolean {
        if (tokenProvider == null) {
            val start = currentTimeMillis()
            try {
                withTimeout(WarmUpTimeout) {
                    refreshTokenProvider()
                }
            } catch (_: TimeoutCancellationException) {}

            Integrity.warmedUp.record(
                Integrity.WarmedUpExtra(
                    success = tokenProvider?.isSuccess == true,
                    durationMs = (currentTimeMillis() - start).toInt(),
                )
            )
        }
        return tokenProvider?.isSuccess == true
    }

    /**
     * Requests an [IntegrityToken].
     *
     * If no provider is available, or if token creation fails, the error is returned via [Result]. When a token
     * expiration is detected, the provider is refreshed and the request is retried automatically.
     *
     * @return A [Result] containing an [IntegrityToken] on success, or a failure if the request could not be fulfilled.
     */
    override suspend fun request(): Result<IntegrityToken> = request(consumer = IntegrityConsumer.Unknown, retries = 0)

    /**
     * Returns an [IntegrityClient] view that tags every request it issues with [consumer] for telemetry attribution.
     * The returned view delegates to this client and shares its token-provider state, so consumers share the same Phase
     * 1 warmup.
     */
    fun forConsumer(consumer: IntegrityConsumer): IntegrityClient =
        object : IntegrityClient {
            override suspend fun warmUp() = this@GooglePlayIntegrityClient.warmUp()

            override suspend fun request() = request(consumer = consumer, retries = 0)
        }

    private suspend fun request(consumer: IntegrityConsumer, retries: Int): Result<IntegrityToken> = runCatching {
        warmUp()

        val provider =
            checkNotNull(tokenProvider) {
                    "GooglePlayIntegrityClient is missing a token provider"
                }
                .getOrThrow()

        return provider
            .request(requestHashProvider)
            .onFailure {
                if (it.tokenHasExpired) {
                    refreshTokenProvider()
                    return request(consumer = consumer, retries = retries + 1)
                }
            }
            .also { result ->
                Integrity.tokenRequest.record(
                    Integrity.TokenRequestExtra(
                        retries = retries,
                        requestSuccess = result.isSuccess,
                        consumer = consumer.value,
                    )
                )
            }
    }

    private suspend fun refreshTokenProvider() {
        tokenProvider = tokenProviderFactory.create()
    }
}

/**
 * Timeout for warm up request.
 *
 * Even though it is expected that most warmups will complete under 10 seconds, we want to allow for up to 1 minute as
 * advised on
 * [https://developer.android.com/google/play/integrity/standard#prepare-integrity](https://developer.android.com/google/play/integrity/standard#prepare-integrity)
 */
internal val WarmUpTimeout = 1.minutes

/** Thrown when a Google project number is required but invalid. */
class InvalidProjectNumber : IllegalStateException("Google Cloud project number is missing or not a valid number.")

private val Throwable.tokenHasExpired: Boolean
    get() = (this as? StandardIntegrityException)?.tokenProviderHasExpired ?: false

private val StandardIntegrityException.tokenProviderHasExpired: Boolean
    get() = errorCode == INTEGRITY_TOKEN_PROVIDER_INVALID
