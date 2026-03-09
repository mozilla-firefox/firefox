/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.llm.ext

import mozilla.components.lib.llm.mlpa.MlpaTokenProvider
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken

internal class AllTokenProvidersFailed : IllegalStateException("All token providers failed to retrieve a token.")
internal class FxaMissingAccessToken : IllegalStateException("Unable to get access token from FxaAccessTokenProvider")

/** Convenience interface for getting an fxa access token. */
fun interface FxaAccessTokenProvider {
    /** Returns an access token or null */
    suspend fun provide(): String?
}

/** Implementation of [MlpaTokenProvider] that takes the first successful token it receives.
 * @param tokenProviders a list of [MlpaTokenProvider].
 * @return an [MlpaTokenProvider].
 */
fun MlpaTokenProvider.Companion.choose(vararg tokenProviders: MlpaTokenProvider) = MlpaTokenProvider {
    tokenProviders.firstNotNullOfOrNull { provider ->
        provider.fetchToken().takeIf { it.isSuccess }
    } ?: Result.failure(AllTokenProvidersFailed())
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
