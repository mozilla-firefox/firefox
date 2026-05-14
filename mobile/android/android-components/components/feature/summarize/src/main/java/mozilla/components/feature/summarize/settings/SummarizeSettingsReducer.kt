/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

/**
 * Reducer for [SummarizeSettingsAction]s on [SummarizeSettingsState].
 */
fun summarizeSettingsReducer(
    state: SummarizeSettingsState,
    action: SummarizeSettingsAction,
) = when (action) {
    is SettingsLoaded -> {
        state.copy(isFeatureEnabled = action.isFeatureEnabled, isGestureEnabled = action.isGestureEnabled)
    }

    SummarizePagesPreferenceToggled -> {
        state.copy(isFeatureEnabled = !state.isFeatureEnabled)
    }

    ShakeToSummarizePreferenceToggled -> {
        state.copy(isGestureEnabled = !state.isGestureEnabled)
    }

    ViewAppeared,
    SettingsPreLoaded,
    LearnMoreClicked,
    -> state
}
