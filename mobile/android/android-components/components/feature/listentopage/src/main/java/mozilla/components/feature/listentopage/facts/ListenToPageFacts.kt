/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.facts

import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.base.facts.collect

/** Facts emitted to measure how the listen to page feature performs. */
class ListenToPageFacts {
    /** Specify which fact occurred. */
    object Items {
        const val CHUNK_SYNTHESIZED = "chunk_synthesized"
        const val OPENING_SYNTHESIS_STARTED = "opening_synthesis_started"
        const val ENGINE_BOUND = "engine_bound"
        const val PLAY_PRESSED = "play_pressed"
        const val PAUSE_PRESSED = "pause_pressed"
        const val SKIP_BACK_PRESSED = "skip_back_pressed"
        const val SKIP_FORWARD_PRESSED = "skip_forward_pressed"
        const val SPEED_SELECTED = "speed_selected"
    }

    /** Keys of the metadata a fact carries. */
    object MetadataKeys {
        const val CHUNK_SYNTHESIS = "chunk_synthesis"
    }

    /**
     * The engine finished making the audio of one chunk.
     *
     * @property index Which chunk of the article it is.
     * @property length The characters in the chunk.
     * @property synthesisMs How long the engine took to make it.
     * @property audioMs How long its audio lasts, or `null` when it could not be measured.
     * @property fileKilobytes How much disk its audio takes, or `null` when the file is empty or missing.
     */
    data class ChunkSynthesis(
        val index: Int,
        val length: Int,
        val synthesisMs: Long,
        val audioMs: Long?,
        val fileKilobytes: Long?,
    )
}

/** We measure this separately since we intentionally find a small opening chunk to decrease time-to-first-audio. */
internal fun emitOpeningSynthesisStartedFact() {
    Fact(
            component = Component.FEATURE_LISTENTOPAGE,
            action = Action.IMPLEMENTATION_DETAIL,
            item = ListenToPageFacts.Items.OPENING_SYNTHESIS_STARTED,
        )
        .collect()
}

/**
 * Reports that a session bound the speech engine.
 *
 * @param enginePackageName The package of the engine.
 */
internal fun emitEngineBoundFact(enginePackageName: String) {
    Fact(
            component = Component.FEATURE_LISTENTOPAGE,
            action = Action.IMPLEMENTATION_DETAIL,
            item = ListenToPageFacts.Items.ENGINE_BOUND,
            value = enginePackageName,
        )
        .collect()
}

internal fun emitChunkSynthesizedFact(chunkSynthesis: ListenToPageFacts.ChunkSynthesis) {
    Fact(
            component = Component.FEATURE_LISTENTOPAGE,
            action = Action.IMPLEMENTATION_DETAIL,
            item = ListenToPageFacts.Items.CHUNK_SYNTHESIZED,
            metadata = mapOf(ListenToPageFacts.MetadataKeys.CHUNK_SYNTHESIS to chunkSynthesis),
        )
        .collect()
}

/**
 * Reports a control pressed in the notification or the lock screen.
 *
 * @param item Which control was pressed, one of the `*_PRESSED` items or [ListenToPageFacts.Items.SPEED_SELECTED].
 * @param value The speed moved to, for [ListenToPageFacts.Items.SPEED_SELECTED].
 */
internal fun emitMediaControlFact(item: String, value: String? = null) {
    Fact(
            component = Component.FEATURE_LISTENTOPAGE,
            action = Action.CLICK,
            item = item,
            value = value,
        )
        .collect()
}
