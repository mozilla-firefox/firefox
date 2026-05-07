/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * Middleware for the summarize settings screen that persists preference changes.
 *
 * @param settings The [SummarizationFeatureSettings] to persist preference changes to.
 * @param onLearnMoreClicked Callback invoked when the learn more link is clicked.
 */
class SummarizeSettingsMiddleware(
    private val settings: SummarizationSettings,
    private val onLearnMoreClicked: () -> Unit,
    private val scope: CoroutineScope,
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
            ViewAppeared -> scope.launch {
                store.dispatch(
                    SettingsLoaded(
                        isFeatureEnabled = settings.getFeatureEnabledUserStatus().first(),
                        isGestureEnabled = settings.getGestureEnabledUserStatus().first(),
                    ),
                )
            }

            SummarizePagesPreferenceToggled -> scope.launch {
                settings.setFeatureEnabledUserStatus(store.state.isFeatureEnabled)
            }

            ShakeToSummarizePreferenceToggled -> scope.launch {
                settings.setGestureEnabledUserStatus(store.state.isGestureEnabled)
            }

            LearnMoreClicked -> {
                onLearnMoreClicked()
            }

            is SettingsLoaded -> Unit
        }
    }
}
