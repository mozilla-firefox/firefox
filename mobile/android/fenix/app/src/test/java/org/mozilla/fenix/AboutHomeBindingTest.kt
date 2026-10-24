/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import androidx.navigation.NavController
import androidx.navigation.NavDestination
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AboutHomeBindingTest {
    private val testDispatcher = StandardTestDispatcher()

    private lateinit var browserStore: BrowserStore
    private lateinit var tabId: String
    private lateinit var navController: NavController

    private val tab: TabSessionState
        get() = browserStore.state.tabs.find { it.id == tabId }!!

    @Before
    fun setUp() {
        navController = mockk(relaxed = true)

        val tab =
            createTab(url = "https://www.mozilla.org").also {
                tabId = it.id
            }
        browserStore =
            BrowserStore(
                initialState =
                    BrowserState(
                        tabs = listOf(tab),
                        selectedTabId = tabId,
                    )
            )
    }

    @Test
    fun `WHEN URL is updated to ABOUT_HOME_URL THEN navigate to the homepage`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.browserFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()

            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tab.id,
                    url = ABOUT_HOME_URL,
                )
            )

            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(ABOUT_HOME_URL, tab.content.url)

            verify { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `GIVEN menu dialog is shown WHEN URL is updated to ABOUT_HOME_URL THEN navigate to the homepage`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.menuDialogFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()

            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tab.id,
                    url = ABOUT_HOME_URL,
                )
            )

            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(ABOUT_HOME_URL, tab.content.url)

            verify { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `GIVEN menu fragment is shown WHEN URL is updated to ABOUT_HOME_URL THEN navigate to the homepage`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.menuFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()

            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tab.id,
                    url = ABOUT_HOME_URL,
                )
            )

            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(ABOUT_HOME_URL, tab.content.url)

            verify { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `GIVEN homepage is the currently shown WHEN URL is updated to ABOUT_HOME_URL THEN do not navigate to the homepage`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.homeFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()

            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tabId,
                    url = ABOUT_HOME_URL,
                )
            )

            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(ABOUT_HOME_URL, tab.content.url)

            verify(exactly = 0) { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `GIVEN onboarding is the currently shown WHEN URL is updated to ABOUT_HOME_URL THEN do not navigate to the homepage`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.onboardingFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()

            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tabId,
                    url = ABOUT_HOME_URL,
                )
            )

            // Wait for ContentAction.UpdateUrlAction
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(ABOUT_HOME_URL, tab.content.url)

            verify(exactly = 0) { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `GIVEN the tabs tray is the currently shown WHEN URL is updated to ABOUT_HOME_URL THEN do not navigate to the homepage`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.tabManagementFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()

            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tabId,
                    url = ABOUT_HOME_URL,
                )
            )

            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(ABOUT_HOME_URL, tab.content.url)

            verify(exactly = 0) { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `GIVEN the unlock tabs tray screen is the currently shown WHEN URL is updated to ABOUT_HOME_URL THEN do not navigate to the homepage`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.unlockPrivateTabsFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()

            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tabId,
                    url = ABOUT_HOME_URL,
                )
            )

            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(ABOUT_HOME_URL, tab.content.url)

            verify(exactly = 0) { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `GIVEN the settings screen is currently shown WHEN URL is updated to ABOUT_HOME_URL THEN do not navigate to the homepage`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.settingsFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()

            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tabId,
                    url = ABOUT_HOME_URL,
                )
            )

            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(ABOUT_HOME_URL, tab.content.url)

            verify(exactly = 0) { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `WHEN URL is updated to a URL that is not ABOUT_HOME_URL THEN do not navigate to the homepage`() =
        runTest(testDispatcher) {
            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                )

            binding.start()

            val newUrl = "https://www.firefox.com"
            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tabId,
                    url = newUrl,
                )
            )

            // Wait for ContentAction.UpdateUrlAction
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(newUrl, tab.content.url)

            verify(exactly = 0) { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `GIVEN the menu dialog is shown and the selected tab is ABOUT_HOME_URL WHEN the binding is restarted THEN do not navigate to the homepage again`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.menuDialogFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()
            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tabId,
                    url = ABOUT_HOME_URL,
                )
            )
            testDispatcher.scheduler.advanceUntilIdle()
            verify(exactly = 1) { navController.navigate(NavGraphDirections.actionGlobalHome()) }

            binding.stop()
            binding.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 1) { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `WHEN the URL is updated to ABOUT_HOME_URL while the binding is stopped THEN navigate to the homepage once the binding is restarted`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.browserFragment
            every { navController.currentDestination } returns mockDestination

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()
            testDispatcher.scheduler.advanceUntilIdle()
            binding.stop()

            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tabId,
                    url = ABOUT_HOME_URL,
                )
            )
            testDispatcher.scheduler.advanceUntilIdle()
            verify(exactly = 0) { navController.navigate(NavGraphDirections.actionGlobalHome()) }

            binding.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 1) { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }

    @Test
    fun `GIVEN the selected tab is ABOUT_HOME_URL WHEN the binding is started for the first time THEN navigate to the homepage`() =
        runTest(testDispatcher) {
            val mockDestination: NavDestination = mockk()
            every { mockDestination.id } returns R.id.browserFragment
            every { navController.currentDestination } returns mockDestination
            browserStore.dispatch(
                ContentAction.UpdateUrlAction(
                    sessionId = tabId,
                    url = ABOUT_HOME_URL,
                )
            )

            val binding =
                AboutHomeBinding(
                    browserStore = browserStore,
                    navController = navController,
                    mainDispatcher = testDispatcher,
                )

            binding.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 1) { navController.navigate(NavGraphDirections.actionGlobalHome()) }
        }
}
