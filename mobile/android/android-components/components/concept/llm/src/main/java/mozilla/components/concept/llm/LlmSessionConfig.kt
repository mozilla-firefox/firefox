/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.llm

/**
 * Configuration for an [LlmSession].
 *
 * @property providers The ordered list of providers the session may use. Must be non-empty.
 * @property picker Selects the initial active provider and is re-invoked on automatic failover.
 *   Defaults to [LlmPicker.Default], which picks the first provider in the list.
 * @property systemPrompt Text prepended as a [Llm.Message.System] on every inference call.
 *   Omit or leave blank if no system prompt is required.
 * @property tools Tools the session may invoke on the model's behalf during a turn.
 *   Registered at construction time and fixed for the session's lifetime.
 * @property contextWindowStrategy Determines how conversation history is trimmed when it
 *   exceeds the active model's token limit. Defaults to [ContextWindowStrategy.Default].
 * @property failoverStrategy Determines how the session reacts when the active provider
 *   becomes unavailable. Defaults to [LlmFailoverStrategy.AutoFailover].
 * @property preparationStrategy Determines which providers are prepared at launch time and on
 *   [LlmSession.switchProvider]. Defaults to [LlmPreparationStrategy.ActiveOnly].
 */
data class LlmSessionConfig(
    val providers: List<LlmProvider>,
    val picker: LlmPicker = LlmPicker.Default,
    val systemPrompt: String = "",
    val tools: List<LlmTool> = emptyList(),
    val contextWindowStrategy: ContextWindowStrategy = ContextWindowStrategy.Default,
    val failoverStrategy: LlmFailoverStrategy = LlmFailoverStrategy.AutoFailover,
    val preparationStrategy: LlmPreparationStrategy = LlmPreparationStrategy.ActiveOnly,
)
