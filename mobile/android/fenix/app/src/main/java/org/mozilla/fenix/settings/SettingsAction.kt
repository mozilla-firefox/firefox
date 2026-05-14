/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import mozilla.components.concept.ai.controls.AIFeatureMetadata
import mozilla.components.lib.state.Action

/**
 * Actions that can be dispatched on the settings [SettingsStore].
 */
sealed class SettingsAction : Action {
    /** Dispatched once when the settings screen is first created. */
    data object SettingsViewCreated : SettingsAction()

    /** Emitted when an AI feature's enabled state has been loaded. */
    data class AIControlsFeatureStateLoaded(
        val enabled: Boolean,
        val id: AIFeatureMetadata.FeatureId,
    ) : SettingsAction()

    /** Emitted when the page-summaries settings have been loaded from disk. */
    data class PageSummariesSettingsLoaded(
        val isFeatureEnabled: Boolean,
        val isGestureEnabled: Boolean,
    ) : SettingsAction()
}
