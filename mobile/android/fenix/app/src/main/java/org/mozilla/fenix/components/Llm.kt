/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import mozilla.components.concept.fetch.Client
import mozilla.components.lib.llm.mlpa.MlpaLlmProvider
import mozilla.components.lib.llm.mlpa.MlpaTokenProvider
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.lib.llm.mlpa.service.FetchClientMlpaService
import mozilla.components.lib.llm.mlpa.service.MlpaConfig
import org.mozilla.fenix.perf.lazyMonitored

/**
 * Component group for LLM services.
 */
class Llm(
    private val client: Client,
) {
    val mlpaProvider: MlpaLlmProvider by lazyMonitored {
        MlpaLlmProvider(
            MlpaTokenProvider.static(authorizationToken),
            FetchClientMlpaService(client, MlpaConfig.live),
        )
    }
}

private val authorizationToken =
    AuthorizationToken("<insert token>")
