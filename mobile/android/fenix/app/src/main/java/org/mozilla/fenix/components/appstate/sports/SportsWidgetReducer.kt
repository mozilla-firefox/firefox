/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate.sports

import org.mozilla.fenix.components.appstate.AppAction.SportsWidgetAction
import org.mozilla.fenix.components.appstate.AppState

/** [AppState] reducer for [SportsWidgetAction]. */
internal object SportsWidgetReducer {
    fun reduce(state: AppState, action: SportsWidgetAction): AppState =
        when (action) {
            is SportsWidgetAction.FeatureEnabledChanged ->
                state.copy(sportsWidgetState = state.sportsWidgetState.copy(isFeatureEnabled = action.isEnabled))

            is SportsWidgetAction.VisibilityChanged ->
                state.copy(sportsWidgetState = state.sportsWidgetState.copy(isVisible = action.isVisible))

            is SportsWidgetAction.MatchCardsUpdated ->
                state.copy(
                    sportsWidgetState = state.sportsWidgetState.copy(matchCards = action.matchCards, errorState = null)
                )

            is SportsWidgetAction.ErrorStateChanged ->
                state.copy(sportsWidgetState = state.sportsWidgetState.copy(errorState = action.errorState))

            is SportsWidgetAction.DebugToolVisibilityChanged ->
                state.copy(sportsWidgetState = state.sportsWidgetState.copy(isDebugToolVisible = action.isVisible))
        }
}
