/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.CloudLlmProvider.State
import mozilla.components.concept.llm.CloudLlmProvider.State.Ready
import mozilla.components.concept.llm.CloudLlmProvider.State.Unavailable
import mozilla.components.lib.llm.mlpa.service.MlpaService

/**
 * [CloudLlmProvider] implementation backed by MLPA services.
 *
 * This provider is responsible for:
 * - Fetching an authentication token via [MlpaTokenProvider].
 * - Initializing an [MlpaLlm] instance when authentication succeeds.
 * - Exposing availability and readiness through a [StateFlow].
 *
 * The provider starts in [State.Available]. After calling [prepare],
 * the state will transition to:
 * - [Ready] with an initialized [MlpaLlm] instance if token retrieval succeeds.
 * - [Unavailable] if token retrieval fails.
 *
 * @property tokenProvider Responsible for fetching the MLPA authentication token.
 * @property mlpaService Service used to construct the [MlpaLlm] instance once authenticated.
 */
class MlpaLlmProvider(
    val tokenProvider: MlpaTokenProvider,
    val mlpaService: MlpaService,
) : CloudLlmProvider {
    private val _state = MutableStateFlow<State>(State.Available)

    /**
     * The current state.
     */
    override val state: StateFlow<State> = _state

    /**
     * Prepares the provider for use.
     *
     * This function attempts to fetch an authentication token using [tokenProvider].
     *
     * - On success, updates [state] to [Ready] with a newly created [MlpaLlm].
     * - On failure, updates [state] to [Unavailable].
     */
    override suspend fun prepare() {
        tokenProvider.fetchToken()
            .onSuccess { _state.value = Ready(MlpaLlm(mlpaService, it)) }
            .onFailure { _state.value = Unavailable }
    }
}
