/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.stateIn
import mozilla.components.concept.ai.controls.AIControllableFeature
import mozilla.components.concept.ai.controls.AIFeatureState

/**
 * Wraps an [AIControllableFeature] so that [featureState] (and the derived
 * [mozilla.components.concept.ai.controls.isEnabled]) is exposed as a hot flow.
 */
class CachedEnabledFeature(
    feature: AIControllableFeature,
    scope: CoroutineScope,
) : AIControllableFeature by feature {
    override val featureState: Flow<AIFeatureState> = feature.featureState
        .stateIn(scope, SharingStarted.Eagerly, AIFeatureState.Unknown)
}
