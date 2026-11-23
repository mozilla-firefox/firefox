/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import java.time.ZoneId
import java.util.Locale
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.service.merino.sports.MerinoMatchesStorage
import mozilla.components.service.merino.sports.MerinoSportsUpdater
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.RunWhenReadyQueue
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SportsWidgetAction
import org.mozilla.fenix.components.appstate.AppState

/**
 * [Middleware] that keeps the sports widget matches up to date while the widget is enabled and visible: it refreshes
 * the matches in the background so that the stored matches are up to date whenever the homepage is shown.
 *
 * @param merinoSportsUpdater [MerinoSportsUpdater] used to schedule the background refresh of the matches.
 * @param merinoMatchesStorage [MerinoMatchesStorage] holding the latest fetched matches.
 * @param is24HourFormat Returns whether the device uses the 24-hour clock to display the match time.
 * @param visualCompletenessQueue [RunWhenReadyQueue] used to defer the work until the app is visually complete.
 * @param zoneId Returns the [ZoneId] used to display the match date and time.
 * @param locale Returns the [Locale] used to format the match date and time.
 * @param coroutineScope [CoroutineScope] used to schedule the background refresh and read the stored matches. It must
 *   run one task at a time so that the changes are applied in the order they happen.
 */
class SportsWidgetRefreshMiddleware(
    private val merinoSportsUpdater: Lazy<MerinoSportsUpdater>,
    private val merinoMatchesStorage: Lazy<MerinoMatchesStorage>,
    private val is24HourFormat: () -> Boolean,
    private val visualCompletenessQueue: RunWhenReadyQueue,
    private val zoneId: () -> ZoneId = { ZoneId.systemDefault() },
    private val locale: () -> Locale = { Locale.getDefault() },
    private val coroutineScope: CoroutineScope = CoroutineScope(Dispatchers.IO.limitedParallelism(1)),
) : Middleware<AppState, AppAction> {

    private val logger = Logger("SportsWidgetRefreshMiddleware")

    private var storedMatchesJob: Job? = null

    override fun invoke(
        store: Store<AppState, AppAction>,
        next: (AppAction) -> Unit,
        action: AppAction,
    ) {
        next(action)

        when (action) {
            is AppAction.AppLifecycleAction.StartAction ->
                visualCompletenessQueue.runIfReadyOrQueue { update(store, restartStoredMatches = true) }

            is SportsWidgetAction.FeatureEnabledChanged,
            is SportsWidgetAction.VisibilityChanged -> update(store)

            else -> Unit
        }
    }

    private fun update(store: Store<AppState, AppAction>, restartStoredMatches: Boolean = false) {
        val isActive = store.state.sportsWidgetState.let { it.isFeatureEnabled && it.isVisible }

        coroutineScope.launch {
            if (isActive) {
                observeStoredMatches(store, restart = restartStoredMatches)
                merinoSportsUpdater.value.startPeriodicWork()
            } else {
                storedMatchesJob?.cancel()
                storedMatchesJob = null
                merinoSportsUpdater.value.stopPeriodicWork()
            }
        }
    }

    /**
     * Observes the stored matches and dispatches them as the [MatchCard]s displayed in the sports widget.
     *
     * The stored matches are formatted again every time the app comes to the foreground, so that a change to the time
     * zone, locale or clock setting is shown.
     *
     * @param store [Store] the match cards are dispatched to.
     * @param restart Whether to restart the observation if it is already running, so that the stored matches are
     *   formatted again.
     */
    private fun observeStoredMatches(store: Store<AppState, AppAction>, restart: Boolean) {
        if (storedMatchesJob?.isActive == true && !restart) return

        storedMatchesJob?.cancel()
        storedMatchesJob = coroutineScope.launch {
            merinoMatchesStorage.value.matches
                .filterNotNull()
                .map { matches ->
                    matches.toMatchCards(zoneId = zoneId(), locale = locale(), is24HourFormat = is24HourFormat())
                }
                .distinctUntilChanged()
                .catch { e -> logger.error("Failed to read the stored matches", e) }
                .collect { matchCards -> store.dispatch(SportsWidgetAction.MatchCardsUpdated(matchCards)) }
        }
    }
}
