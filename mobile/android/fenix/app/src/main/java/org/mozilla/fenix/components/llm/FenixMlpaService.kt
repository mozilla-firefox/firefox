/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.llm

import mozilla.components.concept.fetch.Client
import mozilla.components.lib.llm.mlpa.service.AuthenticationService
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.lib.llm.mlpa.service.ChatService
import mozilla.components.lib.llm.mlpa.service.FetchClientMlpaService
import mozilla.components.lib.llm.mlpa.service.MlpaConfig
import mozilla.components.lib.llm.mlpa.service.MlpaService

/**
 * Temporary class for toggling between prod and nonprod MLPA environment
 */
class FenixMlpaService(
    client: Client,
    var useProd: Boolean = true,
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
