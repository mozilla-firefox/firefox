/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/** The initial middleware for the summarization feature */
class SummarizationMiddleware(
    private val settings: SummarizationSettings,
    private val scope: CoroutineScope,
) : Middleware<SummarizationState, SummarizationAction> {
    override fun invoke(
        store: Store<SummarizationState, SummarizationAction>,
        next: (SummarizationAction) -> Unit,
        action: SummarizationAction,
    ) {
        when (action) {
            is ViewAppeared -> checkForShakeConsent(store.state) {
                store.dispatch(ShakeConsentRequested)
            }
            OffDeviceSummarizationShakeConsentAction.AllowClicked -> scope.launch {
                settings.setHasConsentedToShake(true)
            }
        }

        next(action)
    }

    private fun checkForShakeConsent(state: SummarizationState, requiresShakeConsent: () -> Unit) = scope.launch {
        if (state is SummarizationState.Inert && state.initializedWithShake && !settings.getHasConsentedToShake()) {
            requiresShakeConsent()
        }
    }
}
