/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate.sports

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.mozilla.fenix.components.appstate.AppAction.SportsWidgetAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.home.sports.fake.FakeMatchCardScenario

class SportsWidgetReducerTest {
    @Test
    fun `GIVEN isFeatureEnabled is false WHEN FeatureEnabledChanged is dispatched with true THEN isFeatureEnabled is true`() {
        val updatedState = SportsWidgetReducer.reduce(AppState(), SportsWidgetAction.FeatureEnabledChanged(true))

        assertTrue(updatedState.sportsWidgetState.isFeatureEnabled)
    }

    @Test
    fun `GIVEN isVisible is false WHEN VisibilityChanged is dispatched with true THEN isVisible is true`() {
        val updatedState = SportsWidgetReducer.reduce(AppState(), SportsWidgetAction.VisibilityChanged(true))

        assertTrue(updatedState.sportsWidgetState.isVisible)
    }

    @Test
    fun `GIVEN errorState is set WHEN MatchCardsUpdated is dispatched THEN matchCards is set and errorState is cleared`() {
        val initialState = AppState(sportsWidgetState = SportsWidgetState(errorState = SportCardErrorState.LoadFailed))
        val matchCards = FakeMatchCardScenario.Live.build()

        val updatedState = SportsWidgetReducer.reduce(initialState, SportsWidgetAction.MatchCardsUpdated(matchCards))

        assertEquals(matchCards, updatedState.sportsWidgetState.matchCards)
        assertNull(updatedState.sportsWidgetState.errorState)
    }

    @Test
    fun `GIVEN matchCards is set WHEN ErrorStateChanged is dispatched THEN errorState is set and matchCards is preserved`() {
        val matchCards = FakeMatchCardScenario.Live.build()
        val initialState = AppState(sportsWidgetState = SportsWidgetState(matchCards = matchCards))

        val updatedState =
            SportsWidgetReducer.reduce(
                initialState,
                SportsWidgetAction.ErrorStateChanged(SportCardErrorState.ConnectionInterrupted),
            )

        assertEquals(SportCardErrorState.ConnectionInterrupted, updatedState.sportsWidgetState.errorState)
        assertEquals(matchCards, updatedState.sportsWidgetState.matchCards)
    }

    @Test
    fun `GIVEN isDebugToolVisible is false WHEN DebugToolVisibilityChanged is dispatched with true THEN isDebugToolVisible is true`() {
        val updatedState = SportsWidgetReducer.reduce(AppState(), SportsWidgetAction.DebugToolVisibilityChanged(true))

        assertTrue(updatedState.sportsWidgetState.isDebugToolVisible)
    }

    @Test
    fun `GIVEN isFeatureEnabled and isVisible are true and there are no matchCards or errorState WHEN isShown is checked THEN isShown is false`() {
        val state = SportsWidgetState(isFeatureEnabled = true, isVisible = true)

        assertFalse(state.isShown)
    }

    @Test
    fun `GIVEN isFeatureEnabled and isVisible are true and matchCards is set WHEN isShown is checked THEN isShown is true`() {
        val state =
            SportsWidgetState(
                isFeatureEnabled = true,
                isVisible = true,
                matchCards = FakeMatchCardScenario.Live.build(),
            )

        assertTrue(state.isShown)
    }

    @Test
    fun `GIVEN isFeatureEnabled and isVisible are true and errorState is set WHEN isShown is checked THEN isShown is true`() {
        val state =
            SportsWidgetState(
                isFeatureEnabled = true,
                isVisible = true,
                errorState = SportCardErrorState.LoadFailed,
            )

        assertTrue(state.isShown)
    }

    @Test
    fun `GIVEN isFeatureEnabled is false and matchCards is set WHEN isShown is checked THEN isShown is false`() {
        val state =
            SportsWidgetState(
                isFeatureEnabled = false,
                isVisible = true,
                matchCards = FakeMatchCardScenario.Live.build(),
            )

        assertFalse(state.isShown)
    }

    @Test
    fun `GIVEN isVisible is false and matchCards is set WHEN isShown is checked THEN isShown is false`() {
        val state =
            SportsWidgetState(
                isFeatureEnabled = true,
                isVisible = false,
                matchCards = FakeMatchCardScenario.Live.build(),
            )

        assertFalse(state.isShown)
    }
}
