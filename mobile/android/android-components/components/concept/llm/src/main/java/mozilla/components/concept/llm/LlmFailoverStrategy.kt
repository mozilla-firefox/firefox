/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.llm

/**
 * Determines how an [LlmSession] reacts when the active [LlmProvider] becomes unavailable.
 */
sealed interface LlmFailoverStrategy {
    /**
     * Automatically selects another ready provider using the session's [LlmPicker].
     * If no other provider is ready, the session waits until one becomes available.
     * The switch is reflected in [LlmSession.activeProvider].
     */
    data object AutoFailover : LlmFailoverStrategy

    /**
     * Surfaces the error immediately to the consumer as a flow exception.
     * No automatic switching is attempted.
     */
    data object FailFast : LlmFailoverStrategy

    /**
     * Suspends any pending [LlmSession.send] calls until the consumer explicitly calls
     * [LlmSession.switchProvider] with a ready provider.
     */
    data object ManualFailover : LlmFailoverStrategy
}
