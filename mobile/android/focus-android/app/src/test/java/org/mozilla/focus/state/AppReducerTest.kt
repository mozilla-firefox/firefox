/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.state

import mozilla.components.browser.state.search.SearchAccessPoint
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotSame
import org.junit.Assert.assertSame
import org.junit.Test

class AppReducerTest {

    @Test
    fun `test showFirstRun() should return the correct state when in other screen than FirstRun`() {
        val initialState = AppState(screen = Screen.Home())
        val expectedState = showFirstRun(initialState)

        assertNotSame(initialState, expectedState)
        assertEquals(Screen.FirstRun, expectedState.screen)
    }

    @Test
    fun `test showFirstRun() should return the correct state when already in FirstRun`() {
        val initialState = AppState(screen = Screen.FirstRun)
        val expectedState = showFirstRun(initialState)

        assertSame(initialState, expectedState)
        assertEquals(Screen.FirstRun, expectedState.screen)
    }

    @Test
    fun `test showOnBoardingSecondScreen() should return the correct state when in other screen than OnboardingSecondScreen`() {
        val initialState = AppState(screen = Screen.Home())
        val expectedState = showOnBoardingSecondScreen(initialState)

        assertNotSame(initialState, expectedState)
        assertEquals(Screen.OnboardingSecondScreen, expectedState.screen)
    }

    @Test
    fun `test showOnBoardingSecondScreen() should return the correct state when already in OnboardingSecondScreen`() {
        val initialState = AppState(screen = Screen.OnboardingSecondScreen)
        val expectedState = showOnBoardingSecondScreen(initialState)

        assertSame(initialState, expectedState)
        assertEquals(Screen.OnboardingSecondScreen, expectedState.screen)
    }

    @Test
    fun `test showHomeScreen() should return the correct state when in other screen than Home`() {
        val initialState = AppState(screen = Screen.Browser(tabId = "tab1", showTabs = true))
        val expectedState = showHomeScreen(initialState, SearchAccessPoint.DEFAULT)

        assertNotSame(initialState, expectedState)
        assertEquals(Screen.Home(), expectedState.screen)
    }

    @Test
    fun `test showHomeScreen() should return the correct state when already in Home with the same access point`() {
        val initialState = AppState(screen = Screen.Home(SearchAccessPoint.DEFAULT))
        val expectedState = showHomeScreen(initialState, SearchAccessPoint.DEFAULT)

        assertSame(initialState, expectedState)
        assertEquals(Screen.Home(SearchAccessPoint.DEFAULT), expectedState.screen)
    }

    @Test
    fun `test showHomeScreen() should return the correct state when already in Home with a different access point`() {
        val initialState = AppState(screen = Screen.Home(SearchAccessPoint.WIDGET))
        val actualState = showHomeScreen(initialState, SearchAccessPoint.DEFAULT)

        assertNotSame(initialState, actualState)
        assertEquals(Screen.Home(SearchAccessPoint.DEFAULT), actualState.screen)
    }

    @Test
    fun `test lock() should return the correct state when in other screen than Locked`() {
        val initialState = AppState(screen = Screen.Home())
        val action = AppAction.Lock()
        val expectedState = lock(initialState, action)

        assertNotSame(initialState, expectedState)
        assertEquals(Screen.Locked(bundle = null), expectedState.screen)
    }

    @Test
    fun `test lock() should return the correct state when already in Locked`() {
        val initialState = AppState(screen = Screen.Locked())
        val action = AppAction.Lock()
        val expectedState = lock(initialState, action)

        assertSame(initialState, expectedState)
        assertEquals(Screen.Locked(), expectedState.screen)
    }

    @Test
    fun `GIVEN the home screen was opened from the widget WHEN the app is locked and unlocked without a tab THEN show the home screen with the widget access point`() {
        val lockedState = AppReducer(AppState(screen = Screen.Home(SearchAccessPoint.WIDGET)), AppAction.Lock())

        assertEquals(Screen.Locked(homeAccessPoint = SearchAccessPoint.WIDGET), lockedState.screen)

        val unlockedState = AppReducer(lockedState, AppAction.Unlock(tabId = null))

        assertEquals(Screen.Home(SearchAccessPoint.WIDGET), unlockedState.screen)
    }

    @Test
    fun `GIVEN the app was locked on the home screen opened from the widget WHEN unlocked with a tab THEN show the tab`() {
        val lockedState = AppState(screen = Screen.Locked(homeAccessPoint = SearchAccessPoint.WIDGET))

        val unlockedState = AppReducer(lockedState, AppAction.Unlock(tabId = "tab"))

        assertEquals(Screen.Browser("tab", showTabs = false), unlockedState.screen)
    }

    @Test
    fun `GIVEN the app was locked on the home screen opened from the widget WHEN the access point is reset and the app unlocked THEN show the home screen with the default access point`() {
        val lockedState = AppState(screen = Screen.Locked(homeAccessPoint = SearchAccessPoint.WIDGET))

        val resetState = AppReducer(lockedState, AppAction.ResetHomeAccessPoint)

        assertEquals(Screen.Locked(), resetState.screen)

        val unlockedState = AppReducer(resetState, AppAction.Unlock(tabId = null))

        assertEquals(Screen.Home(), unlockedState.screen)
    }

    @Test
    fun `GIVEN the home screen was opened from the widget WHEN the access point is reset THEN show the home screen with the default access point`() {
        val initialState = AppState(screen = Screen.Home(SearchAccessPoint.WIDGET))

        val actualState = resetHomeAccessPoint(initialState)

        assertEquals(Screen.Home(), actualState.screen)
        assertEquals(initialState.screen.id, actualState.screen.id)
    }

    @Test
    fun `GIVEN the home screen has the default access point WHEN the access point is reset THEN the state is unchanged`() {
        val initialState = AppState(screen = Screen.Home())

        assertSame(initialState, resetHomeAccessPoint(initialState))
    }

    @Test
    fun `GIVEN the browser screen is shown WHEN the access point is reset THEN the state is unchanged`() {
        val initialState = AppState(screen = Screen.Browser("tab", showTabs = false))

        assertSame(initialState, resetHomeAccessPoint(initialState))
    }
}
