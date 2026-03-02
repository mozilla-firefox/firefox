/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import mozilla.components.concept.fetch.Client
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.lib.llm.mlpa.MlpaLlmProvider
import mozilla.components.lib.llm.mlpa.MlpaTokenProvider
import mozilla.components.lib.llm.mlpa.UserIdProvider
import mozilla.components.lib.llm.mlpa.service.FetchClientMlpaService
import mozilla.components.lib.llm.mlpa.service.MlpaConfig
import mozilla.components.lib.llm.mlpa.service.PackageName
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.perf.lazyMonitored

/**
 * Component group for LLM services.
 */
class Llm(
    private val client: Client,
    private val integrityClient: IntegrityClient,
    private val userIdProvider: UserIdProvider,
) {
    val mlpaProvider: MlpaLlmProvider by lazyMonitored {
        MlpaLlmProvider(
            MlpaTokenProvider.mlpaIntegrityHandshake(
                integrityClient = integrityClient,
                authenticationService = FetchClientMlpaService(client, MlpaConfig.live),
                userIdProvider = userIdProvider,
                packageName = PackageName(BuildConfig.APPLICATION_ID),
            ),
            FetchClientMlpaService(client, MlpaConfig.live),
        )
    }
}
