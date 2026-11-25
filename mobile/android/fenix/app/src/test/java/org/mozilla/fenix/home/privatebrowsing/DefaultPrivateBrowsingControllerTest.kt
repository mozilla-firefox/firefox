/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.privatebrowsing

import androidx.navigation.NavController
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Homepage
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.home.privatebrowsing.controller.DefaultPrivateBrowsingController
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class DefaultPrivateBrowsingControllerTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val appStore: AppStore = mockk(relaxed = true)
    private val navController: NavController = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val browsingModeManager: BrowsingModeManager = mockk(relaxed = true)
    private val fenixBrowserUseCases: FenixBrowserUseCases = mockk(relaxed = true)

    private lateinit var store: BrowserStore
    private lateinit var controller: DefaultPrivateBrowsingController

    @Before
    fun setup() {
        store = BrowserStore()
        controller =
            DefaultPrivateBrowsingController(
                navController = navController,
                browsingModeManager = browsingModeManager,
                fenixBrowserUseCases = fenixBrowserUseCases,
                settings = settings,
            )

        every { appStore.state } returns AppState()

        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }
    }

    @Test
    fun `WHEN private browsing learn more link is clicked THEN open support page in browser`() {
        val learnMoreURL =
            "https://support.mozilla.org/en-US/kb/common-myths-about-private-browsing?as=u&utm_source=inproduct"

        controller.handleLearnMoreClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = learnMoreURL,
                newTab = true,
                private = true,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled  WHEN private browsing learn more link is clicked THEN open support page in browser`() {
        every { settings.enableHomepageAsNewTab } returns true

        val learnMoreURL =
            "https://support.mozilla.org/en-US/kb/common-myths-about-private-browsing?as=u&utm_source=inproduct"

        controller.handleLearnMoreClicked()

        verify {
            navController.navigate(R.id.browserFragment)
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = learnMoreURL,
                newTab = false,
                private = true,
            )
        }
    }

    @Test
    fun `WHEN private mode button is selected from home THEN handle mode change`() {
        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }

        every { settings.incrementNumTimesPrivateModeOpened() } just Runs

        assertNull(Homepage.privateModeIconTapped.testGetValue())

        val newMode = BrowsingMode.Private

        controller.handlePrivateModeButtonClicked(newMode)

        val snapshot = Homepage.privateModeIconTapped.testGetValue()!!
        assertEquals(1, snapshot.size)

        verify {
            browsingModeManager.mode = newMode
            settings.incrementNumTimesPrivateModeOpened()
        }
    }

    @Test
    fun `GIVEN normal browsing mode and homepage as a new tab is enabled WHEN private mode button is selected from home THEN open a new homepage tab in private browsing mode`() {
        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }
        every { settings.enableHomepageAsNewTab } returns true
        every { settings.incrementNumTimesPrivateModeOpened() } just Runs

        assertNull(Homepage.privateModeIconTapped.testGetValue())

        val newMode = BrowsingMode.Normal

        controller.handlePrivateModeButtonClicked(newMode)

        val snapshot = Homepage.privateModeIconTapped.testGetValue()!!
        assertEquals(1, snapshot.size)

        verify {
            browsingModeManager.mode = newMode
            fenixBrowserUseCases.selectOrAddHomepageTab(private = false)
        }
    }

    @Test
    fun `GIVEN private browsing mode and homepage as a new tab is enabled WHEN private mode button is selected from home THEN open a new homepage tab in normal browsing mode`() {
        every { navController.currentDestination } returns
            mockk {
                every { id } returns R.id.homeFragment
            }
        every { settings.enableHomepageAsNewTab } returns true
        every { settings.incrementNumTimesPrivateModeOpened() } just Runs

        assertNull(Homepage.privateModeIconTapped.testGetValue())

        val newMode = BrowsingMode.Private

        controller.handlePrivateModeButtonClicked(newMode)

        val snapshot = Homepage.privateModeIconTapped.testGetValue()!!
        assertEquals(1, snapshot.size)

        verify {
            browsingModeManager.mode = newMode
            fenixBrowserUseCases.selectOrAddHomepageTab(private = true)
            settings.incrementNumTimesPrivateModeOpened()
        }
    }
}
