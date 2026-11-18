/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.navigation

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.search.SearchAccessPoint
import mozilla.components.support.test.any
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mock
import org.mockito.Mockito.never
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoMoreInteractions
import org.mockito.junit.MockitoJUnitRunner
import org.mozilla.focus.state.AppState
import org.mozilla.focus.state.Screen

@RunWith(MockitoJUnitRunner::class)
class NavigatorTest {

    @Mock private lateinit var navigation: AppNavigation

    private lateinit var navigator: Navigator

    private val testDispatcher = StandardTestDispatcher()
    private lateinit var navigatorTestScope: CoroutineScope

    private lateinit var stateFlow: MutableStateFlow<AppState>

    private val homeScreen = Screen.Home()
    private val browserScreen = Screen.Browser(tabId = "tab1", false)
    private val settingsScreen = Screen.Settings(Screen.Settings.Page.Start)
    private val otherBrowserScreen = Screen.Browser(tabId = "tab2", false)

    @Before
    fun setUp() {
        navigatorTestScope = CoroutineScope(testDispatcher + Job())

        stateFlow = MutableStateFlow(AppState(screen = homeScreen))

        navigator =
            Navigator(
                stateFlow = stateFlow,
                navigation = navigation,
                scope = navigatorTestScope,
            )
    }

    @After
    fun tearDown() {
        navigatorTestScope.coroutineContext[Job]?.cancel()
    }

    @Test
    fun `navigator starts and navigates to initial screen`() =
        runTest(testDispatcher.scheduler) {
            navigator.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(navigation).navigateToHome()

            verifyNoMoreInteractions(navigation)
        }

    @Test
    fun `navigates to browser screen when app state changes`() =
        runTest(testDispatcher.scheduler) {
            navigator.start()
            testDispatcher.scheduler.advanceUntilIdle()
            verify(navigation).navigateToHome()

            stateFlow.value = AppState(screen = browserScreen)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(navigation).navigateToBrowser(browserScreen.tabId)
            verifyNoMoreInteractions(navigation)
        }

    @Test
    fun `does not navigate if screen ID remains the same`() =
        runTest(testDispatcher.scheduler) {
            stateFlow.value = AppState(screen = browserScreen) // Set initial for this test
            navigator.start()
            testDispatcher.scheduler.advanceUntilIdle()
            verify(navigation).navigateToBrowser(browserScreen.tabId)

            stateFlow.value = AppState(screen = Screen.Browser(tabId = browserScreen.tabId, false))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(navigation, times(1)).navigateToBrowser(browserScreen.tabId)
            verifyNoMoreInteractions(navigation)
        }

    @Test
    fun `does not navigate again when only the home screen access point changes`() =
        runTest(testDispatcher.scheduler) {
            stateFlow.value = AppState(screen = homeScreen)
            navigator.start()
            testDispatcher.scheduler.advanceUntilIdle()
            verify(navigation).navigateToHome()

            stateFlow.value = AppState(screen = Screen.Home(accessPoint = SearchAccessPoint.WIDGET))
            testDispatcher.scheduler.advanceUntilIdle()

            verify(navigation, times(1)).navigateToHome()
            verifyNoMoreInteractions(navigation)
        }

    @Test
    fun `navigates when screen ID changes even if type is same`() =
        runTest(testDispatcher.scheduler) {
            stateFlow.value = AppState(screen = browserScreen) // browser_id_1
            navigator.start()
            testDispatcher.scheduler.advanceUntilIdle()
            verify(navigation).navigateToBrowser(browserScreen.tabId)

            stateFlow.value = AppState(screen = otherBrowserScreen) // browser_id_2
            testDispatcher.scheduler.advanceUntilIdle()

            verify(navigation).navigateToBrowser(otherBrowserScreen.tabId)
            verifyNoMoreInteractions(navigation)
        }

    @Test
    fun `navigates through multiple different screens`() =
        runTest(testDispatcher.scheduler) {
            navigator.start() // Collection starts

            stateFlow.value = AppState(screen = homeScreen)
            testDispatcher.scheduler.advanceUntilIdle()
            verify(navigation).navigateToHome()

            stateFlow.value = AppState(screen = browserScreen)
            testDispatcher.scheduler.advanceUntilIdle()
            verify(navigation).navigateToBrowser(browserScreen.tabId)

            stateFlow.value = AppState(screen = settingsScreen)
            testDispatcher.scheduler.advanceUntilIdle()
            verify(navigation).navigateToSettings(settingsScreen.page)

            verifyNoMoreInteractions(navigation)
        }

    @Test
    fun `stop cancels observation and no further navigations occur`() =
        runTest(testDispatcher.scheduler) {
            navigator.start()
            stateFlow.value = AppState(screen = homeScreen)
            testDispatcher.scheduler.advanceUntilIdle()
            verify(navigation).navigateToHome()

            navigator.stop() // This should cancel the job in navigatorTestScope
            testDispatcher.scheduler.advanceUntilIdle() // Allow cancellation to process

            stateFlow.value = AppState(screen = browserScreen)
            testDispatcher.scheduler.advanceUntilIdle()

            verify(navigation, never()).navigateToBrowser(any())
            verify(navigation, times(1)).navigateToHome() // From before stop
            verifyNoMoreInteractions(navigation)
        }

    @Test
    fun `start does not restart if already active`() =
        runTest(testDispatcher.scheduler) {
            navigator.start()
            stateFlow.value = AppState(screen = homeScreen)
            testDispatcher.scheduler.advanceUntilIdle() // Initial collection
            verify(navigation, times(1)).navigateToHome()

            // Try to start again
            navigator.start()
            stateFlow.value = AppState(screen = browserScreen) // Emit a new state
            testDispatcher.scheduler.advanceUntilIdle()

            // Should not have restarted the collection, so homeScreen should not be re-emitted to navigator
            // And browserScreen should be the next navigation
            verify(navigation, times(1)).navigateToHome() // Still only once
            verify(navigation, times(1)).navigateToBrowser(browserScreen.tabId)
            verifyNoMoreInteractions(navigation)
        }
}
