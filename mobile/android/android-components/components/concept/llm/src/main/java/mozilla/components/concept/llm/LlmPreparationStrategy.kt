/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.llm

/**
 * Determines which [LlmProvider]s an [LlmSession] prepares when launched and when switching.
 */
sealed interface LlmPreparationStrategy {
    /**
     * Only prepares the initially selected provider at launch time, and any provider
     * subsequently passed to [LlmSession.switchProvider].
     */
    data object ActiveOnly : LlmPreparationStrategy

    /**
     * Prepares all providers in [LlmSessionConfig.providers] at launch time.
     */
    data object All : LlmPreparationStrategy
}
