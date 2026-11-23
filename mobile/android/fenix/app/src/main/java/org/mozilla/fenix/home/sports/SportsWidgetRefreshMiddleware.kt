/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.service.merino.sports.MerinoSportsUpdater
import mozilla.components.support.utils.RunWhenReadyQueue
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SportsWidgetAction
import org.mozilla.fenix.components.appstate.AppState

/**
 * [Middleware] that refreshes the sports widget matches in the background while the widget is enabled and visible.
 *
 * The widget is visible when it is turned on in the homepage settings, even while the homepage is not shown. The
 * background refresh keeps running while the app is in the background so that the stored matches are up to date when
 * the homepage is shown again.
 *
 * @param merinoSportsUpdater [MerinoSportsUpdater] used to schedule the background refresh of the matches.
 * @param visualCompletenessQueue [RunWhenReadyQueue] used to defer the work until the app is visually complete.
 * @param coroutineScope [CoroutineScope] used to start and stop the background refresh.
 */
class SportsWidgetRefreshMiddleware(
    private val merinoSportsUpdater: Lazy<MerinoSportsUpdater>,
    private val visualCompletenessQueue: RunWhenReadyQueue,
    private val coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.IO.limitedParallelism(1)),
) : Middleware<AppState, AppAction> {

    override fun invoke(
        store: Store<AppState, AppAction>,
        next: (AppAction) -> Unit,
        action: AppAction,
    ) {
        next(action)

        when (action) {
            is AppAction.AppLifecycleAction.StartAction ->
                visualCompletenessQueue.runIfReadyOrQueue { updateBackgroundRefresh(store.state) }

            is SportsWidgetAction.FeatureEnabledChanged,
            is SportsWidgetAction.VisibilityChanged -> updateBackgroundRefresh(store.state)

            else -> Unit
        }
    }

    private fun updateBackgroundRefresh(state: AppState) {
        val isActive = state.sportsWidgetState.isFeatureEnabled && state.sportsWidgetState.isVisible
        coroutineScope.launch {
            if (isActive) {
                merinoSportsUpdater.value.startPeriodicWork()
            } else {
                merinoSportsUpdater.value.stopPeriodicWork()
            }
        }
    }
}
