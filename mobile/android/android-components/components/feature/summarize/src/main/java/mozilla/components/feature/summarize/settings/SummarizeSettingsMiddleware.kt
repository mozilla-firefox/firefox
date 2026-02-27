/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * Middleware for the summarize settings screen that persists preference changes.
 *
 * @param settings The [SummarizationFeatureSettings] to persist preference changes to.
 * @param onLearnMoreClicked Callback invoked when the learn more link is clicked.
 */
class SummarizeSettingsMiddleware(
    private val settings: SummarizationFeatureSettings,
    private val onLearnMoreClicked: () -> Unit,
) : Middleware<SummarizeSettingsState, SummarizeSettingsAction> {

    override fun invoke(
        store: Store<SummarizeSettingsState, SummarizeSettingsAction>,
        next: (SummarizeSettingsAction) -> Unit,
        action: SummarizeSettingsAction,
    ) {
        // allow the reducer to run first, so that we can accurately
        // update our cache based on the updated state
        next(action)

        when (action) {
            SummarizePagesPreferenceToggled -> {
                settings.summarizePagesEnabled = store.state.summarizePagesEnabled
            }

            ShakeToSummarizePreferenceToggled -> {
                settings.shakeToSummarizeEnabled = store.state.shakeToSummarizeEnabled
            }

            LearnMoreClicked -> {
                onLearnMoreClicked()
            }
        }
    }
}
