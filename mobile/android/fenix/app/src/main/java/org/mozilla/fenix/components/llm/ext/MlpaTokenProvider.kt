/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.llm.ext

import mozilla.components.lib.llm.mlpa.MlpaTokenProvider
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken

internal class FxaMissingAccessToken : IllegalStateException("Unable to get access token from FxaAccessTokenProvider")

/** Convenience interface for getting an fxa access token. */
fun interface FxaAccessTokenProvider {
    /** Returns an access token or null */
    suspend fun provide(): String?
}

/** Implementation of [MlpaTokenProvider] that takes the first successful token it receives.
 * When every provider fails, the last provider's failure is propagated as-is so its
 * provider-specific error reaches logs, telemetry, and error services.
 * @param tokenProviders a list of [MlpaTokenProvider].
 * @return an [MlpaTokenProvider].
 */
fun MlpaTokenProvider.Companion.choose(vararg tokenProviders: MlpaTokenProvider) = MlpaTokenProvider {
    var lastResult = Result.failure<AuthorizationToken>(
        IllegalStateException("choose() called with no token providers"),
    )
    tokenProviders.firstNotNullOfOrNull { provider ->
        provider.fetchToken()
            .also { lastResult = it }
            .takeIf { it.isSuccess }
    } ?: lastResult
}

/** Implementation of [MlpaTokenProvider] that tries to fetch an fxa access token.
 * @param tokenProvider a list of [FxaAccessTokenProvider].
 * @return an [MlpaTokenProvider].
 */
fun MlpaTokenProvider.Companion.fxaTokenProvider(tokenProvider: FxaAccessTokenProvider) = MlpaTokenProvider {
    tokenProvider.provide()?.let {
        Result.success(AuthorizationToken.Fxa(it))
    } ?: Result.failure(FxaMissingAccessToken())
}
