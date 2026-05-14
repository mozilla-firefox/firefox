/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

/**
 * Reducer for [SettingsStore] — applies [SettingsAction]s to [SettingsState].
 */
fun settingsReducer(state: SettingsState, action: SettingsAction): SettingsState = when (action) {
    is SettingsAction.AIControlsFeatureStateLoaded -> state.copy(
        aiControlsState = state.aiControlsState.copy(
            featuresEnabled = state.aiControlsState.featuresEnabled + (action.id to action.enabled),
        ),
    )
    is SettingsAction.PageSummariesSettingsLoaded -> state.copy(
        summarizeSettingsState = state.summarizeSettingsState.copy(
            isFeatureEnabled = action.isFeatureEnabled,
            isGestureEnabled = action.isGestureEnabled,
        ),
    )
    SettingsAction.SettingsViewCreated -> state
}
