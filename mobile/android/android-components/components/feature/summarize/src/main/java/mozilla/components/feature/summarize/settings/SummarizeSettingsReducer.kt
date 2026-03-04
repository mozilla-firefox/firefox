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
    SummarizePagesPreferenceToggled -> {
        state.copy(summarizePagesEnabled = !state.summarizePagesEnabled)
    }

    ShakeToSummarizePreferenceToggled -> {
        state.copy(shakeToSummarizeEnabled = !state.shakeToSummarizeEnabled)
    }

    LearnMoreClicked -> state
}
