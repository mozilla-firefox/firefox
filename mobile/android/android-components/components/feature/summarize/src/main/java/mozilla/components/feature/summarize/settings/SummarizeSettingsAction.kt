/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import mozilla.components.lib.state.Action

/**
 * Actions for the summarize settings screen.
 */
sealed interface SummarizeSettingsAction : Action

/**
 * The user toggled the summarize pages preference.
 */
data object SummarizePagesPreferenceToggled : SummarizeSettingsAction

/**
 * The user toggled the shake to summarize preference.
 */
data object ShakeToSummarizePreferenceToggled : SummarizeSettingsAction

/**
 * The user clicked the learn more link.
 */
data object LearnMoreClicked : SummarizeSettingsAction
