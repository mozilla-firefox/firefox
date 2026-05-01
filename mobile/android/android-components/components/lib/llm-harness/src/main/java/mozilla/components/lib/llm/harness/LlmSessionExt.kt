/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.harness

import mozilla.components.concept.llm.LlmSession
import mozilla.components.concept.llm.LlmSessionConfig

/**
 * Creates a new [LlmSession] from [config].
 *
 * The session's lifetime is tied to [LlmSessionConfig.scope]: cancelling the scope tears
 * down all internal coroutines and stops provider observation.
 */
fun LlmSession.Companion.create(config: LlmSessionConfig): LlmSession = LlmSessionImpl(config)
