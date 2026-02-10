/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.lib.llm.mlpa.service.AuthenticationService
import mozilla.components.lib.llm.mlpa.service.AuthenticationService.Request
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.lib.llm.mlpa.service.UserId

/**
 * Provides a stable [UserId] representing the current user.
 * This should be the same value that is passed to the [GooglePlayIntegrityClient] through its
 * [RequestHashProvider].
 */
fun interface UserIdProvider {
    /**
     * Returns the current user's [UserId].
     *
     * Implementations are expected to return a consistent identifier for
     * the active user context.
     */
    fun getUserId(): UserId
}

/**
 * Abstraction responsible for retrieving an MLPA [AuthorizationToken].
 *
 * Implementations encapsulate the strategy used to obtain a token, such as:
 * - Returning a static/preconfigured token.
 * - Performing an integrity handshake followed by server-side verification.
 */
fun interface MlpaTokenProvider {
    /**
     * Fetches an [AuthorizationToken].
     *
     * @return [Result.success] containing the token if retrieval succeeds,
     * or [Result.failure] if token acquisition fails.
     */
    suspend fun fetchToken(): Result<AuthorizationToken>

    companion object {
        /**
         * Creates a [MlpaTokenProvider] that always returns the given [token].
         *
         * This is useful for testing, local development, or scenarios where
         * token acquisition has already been performed externally.
         *
         * @param token The precomputed [AuthorizationToken] to return.
         */
        fun static(token: AuthorizationToken) = MlpaTokenProvider {
            Result.success(token)
        }

        /**
         * Creates a [MlpaTokenProvider] that performs an MLPA integrity handshake.
         *
         * The flow is:
         * 1. Request an integrity token from [integrityClient].
         * 2. On success, construct a [Request] containing:
         *    - The current user ID from [userIdProvider].
         *    - The integrity token.
         * 3. Send the request to [authenticationService.verify].
         * 4. Map the successful response to its [AuthorizationToken].
         *
         * If any step fails, the failure is propagated as a [Result.failure].
         *
         * @param integrityClient Client responsible for obtaining an integrity token.
         * @param authenticationService Service that verifies the integrity token
         * and exchanges it for an access token.
         * @param userIdProvider Supplies the current user's [UserId].
         */
        fun mlpaIntegrityHandshake(
            integrityClient: IntegrityClient,
            authenticationService: AuthenticationService,
            userIdProvider: UserIdProvider,
        ) = MlpaTokenProvider {
            integrityClient.request().fold(
                onSuccess = { token ->
                    val request = Request(
                        userId = userIdProvider.getUserId(),
                        integrityToken = token,
                    )

                    authenticationService.verify(request).map { it.accessToken }
                },
                onFailure = { Result.failure(it) },
            )
        }
    }
}
