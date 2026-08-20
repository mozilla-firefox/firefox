/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import mozilla.components.feature.summarize.settings.SummarizeSettingsAction
import mozilla.components.feature.summarize.settings.SummarizeSettingsMiddleware
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * [Middleware] unwrapping the [SummarizeSettingsAction]s that the [SummarizationStore] carries as
 * [SummarizeSettingsActionWrapper] and passing them to [settingsMiddleware].
 *
 * @param settingsMiddleware A middleware for handling side-effects related to [SummarizeSettingsAction]s.
 * @param fetchInitialSettings Synchronously readable snapshot of the persisted preferences.
 */
class SummarizationSettingsWrapperMiddleware(
    private val settingsMiddleware: SummarizeSettingsMiddleware,
    private val fetchInitialSettings: () -> SummarizeSettingsState,
) : Middleware<SummarizationState, SummarizationAction> {

    override fun invoke(
        store: Store<SummarizationState, SummarizationAction>,
        next: (SummarizationAction) -> Unit,
        action: SummarizationAction,
    ) {
        when (action) {
            is SettingsClicked -> {
                next(action)
                store.dispatch(SettingsLoaded(fetchInitialSettings()))
            }

            is SummarizeSettingsActionWrapper -> {
                // The sub-state only exists while the store is in SummarizationState.Settings,
                // which it always is for a wrapped action.
                val getState = {
                    (store.state as? SummarizationState.Settings)?.settingsState ?: fetchInitialSettings()
                }
                val dispatch: (SummarizeSettingsAction) -> Unit = {
                    store.dispatch(SummarizeSettingsActionWrapper(it))
                }
                settingsMiddleware.invoke(
                    middlewareContext = Pair(getState, dispatch),
                    next = { inner: SummarizeSettingsAction -> next(SummarizeSettingsActionWrapper(inner)) },
                    action = action.inner,
                )
            }

            else -> next(action)
        }
    }
}
