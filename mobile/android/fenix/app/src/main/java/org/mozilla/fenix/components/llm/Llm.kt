/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.llm

import mozilla.components.concept.fetch.Client
import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.lib.llm.mlpa.MlpaLlmProvider
import mozilla.components.lib.llm.mlpa.MlpaTokenProvider
import mozilla.components.lib.llm.mlpa.MlpaTokenStorage
import mozilla.components.lib.llm.mlpa.UserIdProvider
import mozilla.components.lib.llm.mlpa.service.PackageName
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.components.llm.ext.FxaAccessTokenProvider
import org.mozilla.fenix.components.llm.ext.choose
import org.mozilla.fenix.components.llm.ext.fxaTokenProvider
import org.mozilla.fenix.perf.lazyMonitored

/**
 * Component group for LLM services.
 */
class Llm(
    private val client: Client,
    private val storage: MlpaTokenStorage,
    private val fxaTokenProvider: FxaAccessTokenProvider,
    private val integrityClient: IntegrityClient,
    private val userIdProvider: UserIdProvider,
) {

    val fenixMlpaService by lazyMonitored { FenixMlpaService(client) }

    val mlpaProvider: MlpaLlmProvider by lazyMonitored {
        MlpaLlmProvider(
            MlpaTokenProvider.choose(
                MlpaTokenProvider.fxaTokenProvider(fxaTokenProvider),
                MlpaTokenProvider.mlpaIntegrityHandshake(
                    integrityClient = integrityClient,
                    authenticationService = fenixMlpaService,
                    userIdProvider = userIdProvider,
                    storage = storage,
                    packageName = PackageName(BuildConfig.APPLICATION_ID),
                ),
            ),
            fenixMlpaService,
        )
    }
}
