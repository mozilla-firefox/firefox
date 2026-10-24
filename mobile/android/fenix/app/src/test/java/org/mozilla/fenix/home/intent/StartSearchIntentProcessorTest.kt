/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.content.Intent
import androidx.navigation.NavController
import io.mockk.Called
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertNotNull
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.SearchWidget
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.browser.browsingmode.fakes.FakeBrowsingModeManager
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class StartSearchIntentProcessorTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val navController: NavController = mockk(relaxed = true)
    private val out: Intent = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private lateinit var browserStore: BrowserStore
    private lateinit var fenixBrowserUseCases: FenixBrowserUseCases

    @Before
    fun setup() {
        browserStore =
            BrowserStore(
                initialState = BrowserState(tabs = listOf()),
                middleware = EngineMiddleware.create(mockk()),
            )
        fenixBrowserUseCases =
            FenixBrowserUseCases(
                tabsUseCases = TabsUseCases(store = browserStore),
                appStore = mockk(relaxed = true),
                loadUrlUseCase = mockk(relaxed = true),
                searchUseCases = mockk(relaxed = true),
                homepageTitle = "",
                profiler = mockk(relaxed = true),
            )
    }

    private fun createProcessor(
        browsingModeManager: BrowsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
    ) =
        StartSearchIntentProcessor(
            fenixBrowserUseCases = fenixBrowserUseCases,
            browsingModeManager = browsingModeManager,
        )

    private fun searchWidgetIntent() =
        Intent().apply {
            putExtra(HomeActivity.OPEN_TO_SEARCH, StartSearchIntentProcessor.SEARCH_WIDGET)
        }

    @Test
    fun `do not process blank intents`() {
        assertEquals(0, browserStore.state.tabs.size)

        val result = createProcessor().process(Intent(), navController, out, settings)

        assertFalse(result)
        verify { navController wasNot Called }
        verify { out wasNot Called }
        assertEquals(0, browserStore.state.tabs.size)
    }

    @Test
    fun `do not process when search extra is absent`() {
        assertEquals(0, browserStore.state.tabs.size)

        val intent =
            Intent().apply {
                removeExtra(HomeActivity.OPEN_TO_SEARCH)
            }
        createProcessor().process(intent, navController, out, settings)

        verify { navController wasNot Called }
        verify { out wasNot Called }
        assertEquals(0, browserStore.state.tabs.size)
    }

    @Test
    fun `process search intents to navigate home with address bar focused`() {
        every { settings.enableHomepageAsNewTab } returns false
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
        assertEquals(0, browserStore.state.tabs.size)
        assertFalse(browsingModeManager.hasModeUpdated)

        createProcessor(browsingModeManager = browsingModeManager)
            .process(searchWidgetIntent(), navController, out, settings)

        assertNotNull(SearchWidget.newTabButton.testGetValue())
        val recordedEvents = SearchWidget.newTabButton.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)

        assertEquals(0, browserStore.state.tabs.size)
        assertFalse(browsingModeManager.hasModeUpdated)
        assertEquals(BrowsingMode.Normal, browsingModeManager.mode)
        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalHome(
                    focusOnAddressBar = true,
                    searchAccessPoint = MetricsUtils.Source.WIDGET,
                ),
                null,
            )
        }
        verify { out.removeExtra(HomeActivity.OPEN_TO_SEARCH) }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled and open links in a private tab is disabled WHEN a search intent is processed THEN add a new homepage tab and focus the address bar`() {
        every { settings.enableHomepageAsNewTab } returns true
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
        assertEquals(0, browserStore.state.tabs.size)
        assertFalse(browsingModeManager.hasModeUpdated)

        createProcessor(browsingModeManager = browsingModeManager)
            .process(searchWidgetIntent(), navController, out, settings)

        assertTrue(browsingModeManager.hasModeUpdated)
        assertEquals(BrowsingMode.Normal, browsingModeManager.mode)
        assertEquals(1, browserStore.state.tabs.size)
        assertFalse(browserStore.state.tabs[0].content.private)
        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalHome(
                    focusOnAddressBar = true,
                    searchAccessPoint = MetricsUtils.Source.WIDGET,
                ),
                null,
            )
        }
        verify { out.removeExtra(HomeActivity.OPEN_TO_SEARCH) }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled and open links in a private tab is enabled WHEN a search intent is processed THEN add a new private homepage tab`() {
        every { settings.enableHomepageAsNewTab } returns true
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
        assertEquals(0, browserStore.state.tabs.size)
        assertFalse(browsingModeManager.hasModeUpdated)

        val intent = searchWidgetIntent().apply { putExtra(HomeActivity.PRIVATE_BROWSING_MODE, true) }

        createProcessor(browsingModeManager = browsingModeManager).process(intent, navController, out, settings)

        assertTrue(browsingModeManager.hasModeUpdated)
        assertEquals(BrowsingMode.Private, browsingModeManager.mode)
        assertEquals(1, browserStore.state.tabs.size)
        assertTrue(browserStore.state.tabs[0].content.private)
        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalHome(
                    focusOnAddressBar = true,
                    searchAccessPoint = MetricsUtils.Source.WIDGET,
                ),
                null,
            )
        }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN the intent is from the private browsing pinned shortcut THEN do not process`() {
        every { settings.enableHomepageAsNewTab } returns true
        val intent =
            Intent().apply {
                putExtra(
                    HomeActivity.OPEN_TO_SEARCH,
                    StartSearchIntentProcessor.PRIVATE_BROWSING_PINNED_SHORTCUT,
                )
            }
        createProcessor().process(intent, navController, out, settings)

        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `GIVEN homepage as a new tab is disabled WHEN the intent is from the private browsing pinned shortcut THEN navigate home with address bar focused`() {
        every { settings.enableHomepageAsNewTab } returns false
        val intent =
            Intent().apply {
                putExtra(
                    HomeActivity.OPEN_TO_SEARCH,
                    StartSearchIntentProcessor.PRIVATE_BROWSING_PINNED_SHORTCUT,
                )
            }
        createProcessor().process(intent, navController, out, settings)

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalHome(
                    focusOnAddressBar = true,
                    searchAccessPoint = MetricsUtils.Source.SHORTCUT,
                ),
                null,
            )
        }
        verify { out.removeExtra(HomeActivity.OPEN_TO_SEARCH) }
    }
}
