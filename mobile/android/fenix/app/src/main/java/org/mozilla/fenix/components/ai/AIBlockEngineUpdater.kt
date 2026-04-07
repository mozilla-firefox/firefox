/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.ai

import mozilla.components.concept.ai.controls.AIFeatureBlock
import mozilla.components.concept.engine.Engine

/**
 * Updates the [Engine] settings in response to changes in the [AIFeatureBlock] state.
 *
 * When the AI feature is blocked, both `extensionsML` and `browserML` engine settings
 * are disabled, and vice versa.
 *
 * @param engine The browser engine whose ML settings will be updated.
 * @param aiFeatureBlock The AI feature block whose blocked state is observed.
 */
class AIBlockEngineUpdater(
    private val engine: Engine,
    private val aiFeatureBlock: AIFeatureBlock,
) {

    /**
     * Starts collecting the [AIFeatureBlock.isBlocked] flow and updates the
     * engine's ML settings accordingly.
     */
    suspend fun start() {
        aiFeatureBlock.isBlocked.collect { isBlocked ->
            engine.settings.extensionsML = !isBlocked
            engine.settings.browserML = !isBlocked
        }
    }
}
