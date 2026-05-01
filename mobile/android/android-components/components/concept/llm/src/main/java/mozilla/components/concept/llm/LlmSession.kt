/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.llm

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow

/**
 * A stateful conversation session backed by one or more [LlmProvider]s.
 *
 * The session owns the context window history used for inference and manages provider
 * selection, failover, and context trimming. From the consumer's perspective the
 * interface is intentionally simple: send a message, receive a token stream.
 *
 * Create instances via [LlmSession.Companion.create] (defined in `lib-llm-harness`).
 */
interface LlmSession {
    companion object

    /**
     * The provider currently being used to serve requests.
     * Updated automatically on failover or when [switchProvider] is called.
     */
    val activeProvider: StateFlow<LlmProvider>

    /**
     * Sends [message] to the active LLM and returns a stream of response tokens.
     *
     * The session prepends the configured system prompt, appends [message] to the
     * conversation history, trims the context window if needed, and runs any registered
     * tool calls before emitting the final response tokens.
     *
     * Suspends until an [LlmProvider] is ready if none is available at call time,
     * subject to the configured [LlmFailoverStrategy].
     *
     * @param message The user's input text.
     * @return A cold [Flow] of response tokens. Errors surface as flow exceptions.
     */
    suspend fun send(message: String): Flow<String>

    /**
     * Starts provider observation and preparation coroutines in [scope].
     *
     * Must be called before [send]. The session's internal coroutines are tied to [scope]:
     * cancelling the scope stops all provider observation and frees resources.
     *
     * @param scope The [CoroutineScope] that governs the session's active lifetime.
     */
    fun launchIn(scope: CoroutineScope)

    /**
     * Switches to [provider] and prepares it for use.
     *
     * Returns `false` immediately if [provider] is in a definitively unavailable state,
     * or if preparation completes but the provider is still not ready. On failure the
     * session's [LlmFailoverStrategy] is applied. Any response currently in flight
     * continues with its existing LLM reference and is unaffected.
     *
     * @param provider Must be one of the providers supplied in [LlmSessionConfig.providers].
     * @return `true` if the provider is ready after preparation, `false` otherwise.
     */
    suspend fun switchProvider(provider: LlmProvider): Boolean
}
