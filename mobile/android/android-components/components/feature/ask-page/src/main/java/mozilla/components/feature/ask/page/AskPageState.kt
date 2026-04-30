/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import mozilla.components.concept.llm.Llm
import mozilla.components.lib.state.State

/**
 * The [State] of the [AskPageStore].
 */
sealed class AskPageState : State {

    /** The feature is idle and no conversation is in progress. */
    data object Idle : AskPageState()

    /**
     * A conversation is active.
     *
     * @param messages The ordered list of [Llm.Message]s in the current conversation.
     */
    data class Active(
        val messages: List<Llm.Message> = emptyList(),
    ) : AskPageState()

    /**
     *
     */
    data object Finished : AskPageState()
}
