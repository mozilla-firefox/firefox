/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import mozilla.components.lib.shake.ShakeSensitivity
import mozilla.components.lib.state.State

/**
 * State for the summarize settings screen.
 *
 * @property isFeatureEnabled Whether page summarization is enabled.
 * @property isGestureEnabled Whether the shake-to-summarize gesture is enabled.
 * @property shakeSensitivity The shake sensitivity of shake-to-summarize
 * @property isLearnMoreRequested Whether the user asked to read more about the feature. The host is expected to open
 *   the support article and acknowledge it with [LearnMoreHandled].
 */
data class SummarizeSettingsState(
    val isFeatureEnabled: Boolean = false,
    val isGestureEnabled: Boolean = false,
    val shakeSensitivity: ShakeSensitivity = ShakeSensitivity.Medium,
    val isLearnMoreRequested: Boolean = false,
) : State
