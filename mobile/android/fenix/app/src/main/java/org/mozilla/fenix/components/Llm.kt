/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import mozilla.components.concept.fetch.Client
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.lib.llm.mlpa.MlpaLlmProvider
import mozilla.components.lib.llm.mlpa.MlpaTokenProvider
import mozilla.components.lib.llm.mlpa.UserIdProvider
import mozilla.components.lib.llm.mlpa.service.AuthenticationService
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.lib.llm.mlpa.service.ChatService
import mozilla.components.lib.llm.mlpa.service.FetchClientMlpaService
import mozilla.components.lib.llm.mlpa.service.MlpaConfig
import mozilla.components.lib.llm.mlpa.service.MlpaService
import mozilla.components.lib.llm.mlpa.service.PackageName
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.perf.lazyMonitored

/**
 * Temporary class for toggling between prod and nonprod MLPA environment
 */
class FenixMlpaService(
    client: Client,
    var useProd: Boolean = false,
) : MlpaService {
    private val nonProd = FetchClientMlpaService(client, MlpaConfig.nonProd)
    private val prod = FetchClientMlpaService(client, MlpaConfig.prodProd)
    private val service get() = if (useProd) prod else nonProd

    override suspend fun verify(request: AuthenticationService.Request) = service.verify(request)
    override suspend fun completion(
        authorizationToken: AuthorizationToken,
        request: ChatService.Request,
    ) = service.completion(authorizationToken, request)
}

/**
 * Component group for LLM services.
 */
class Llm(
    private val client: Client,
    private val integrityClient: IntegrityClient,
    private val userIdProvider: UserIdProvider,
) {

    val fenixMlpaService by lazyMonitored { FenixMlpaService(client) }
    val mlpaProvider: MlpaLlmProvider by lazyMonitored {
        MlpaLlmProvider(
            MlpaTokenProvider.mlpaIntegrityHandshake(
                integrityClient = integrityClient,
                authenticationService = fenixMlpaService,
                userIdProvider = userIdProvider,
                packageName = PackageName(BuildConfig.APPLICATION_ID),
            ),
            fenixMlpaService,
        )
    }
}
