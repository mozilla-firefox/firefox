/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.catch
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.concept.llm.CloudLlmProvider.State
import mozilla.components.concept.llm.ErrorCode
import mozilla.components.concept.llm.Llm
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.lib.llm.mlpa.service.ChatService
import mozilla.components.lib.llm.mlpa.service.ChatServiceError
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
    val storage: MlpaTokenStorage,
    val mlpaService: MlpaService,
) : CloudLlmProvider {
    override val info = LlmProvider.Info(nameRes = R.string.mlpa_llm_provider_name, iconRes = R.drawable.firefox_icon)
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
     * - On success, updates [state] to [State.Ready] with a newly created [MlpaLlm].
     * - On failure, updates [state] to [State.Unavailable].
     */
    override suspend fun prepare() {
        tokenProvider.fetchToken()
            .onSuccess { _state.value = State.Ready(MlpaLlm(chatService, it)) }
            .onFailure {
                _state.value = State.Unavailable(
                it as? Llm.Exception
                    ?: Llm.Exception(
                        message = it.message ?: "missing token provider error",
                        errorCode = unknownTokenProviderError,
                    ),
                )
            }
    }

    /**
     * Wraps the [ChatService]
     */
    private val chatService = ChatService { token, request ->
        mlpaService.completion(token, request)
            .catch { throwable ->
                if (throwable is ChatServiceError.InvalidToken) {
                    storage.clear()
                    _state.value = State.Available
                } else {
                    _state.value = State.Unavailable(
                        throwable as? Llm.Exception
                            ?: Llm.Exception(
                                message = throwable.message ?: "missing chat service error",
                                errorCode = unknownChatServiceError,
                            ),
                    )
                }

                // Re-throw the error so downstream consumers can handle the error
                throw throwable
            }
    }

    private val unknownTokenProviderError = ErrorCode(1010)
    private val unknownChatServiceError = ErrorCode(1011)
}
