/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.lib.state.State

/**
 * State for the AI Controls section of the settings screen.
 */
data class AIControlsState(val featuresEnabled: Map<AIFeatureMetadata.FeatureId, Boolean> = mapOf())

/**
 * State for the settings screen.
 */
data class SettingsState(
    val aiControlsState: AIControlsState = AIControlsState(),
    val summarizeSettingsState: SummarizeSettingsState = SummarizeSettingsState(),
) : State
