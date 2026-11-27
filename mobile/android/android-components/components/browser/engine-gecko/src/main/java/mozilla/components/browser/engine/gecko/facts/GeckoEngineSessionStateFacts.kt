/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.facts

import mozilla.components.browser.engine.gecko.facts.GeckoEngineSessionStateFacts.Metadata
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.base.facts.collect

/** Facts emitted for telemetry related to [mozilla.components.browser.engine.gecko.GeckoEngineSessionState]. */
class GeckoEngineSessionStateFacts {
    /** Items that specify what happened to a saved engine state. */
    object Items {
        const val STATE_RESTORED = "engine_state_restored"
    }

    /** Metadata keys of an [Items.STATE_RESTORED] fact. */
    object Metadata {
        /** Number of characters in the engine state. */
        const val ENGINE_STATE_LENGTH = "engine_state_length"
        const val HISTORY_ENTRIES = "history_entries"
    }
}

internal fun emitStateRestoredFact(engineStateLength: Int, historyEntries: Int) =
    Fact(
            Component.BROWSER_ENGINE_GECKO,
            Action.IMPLEMENTATION_DETAIL,
            GeckoEngineSessionStateFacts.Items.STATE_RESTORED,
            metadata =
                mapOf(
                    Metadata.ENGINE_STATE_LENGTH to engineStateLength,
                    Metadata.HISTORY_ENTRIES to historyEntries,
                ),
        )
        .collect()
