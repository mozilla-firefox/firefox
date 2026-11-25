/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.content.Intent
import androidx.navigation.NavController
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.Called
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.collections.listOf
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import mozilla.components.browser.state.engine.EngineMiddleware
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import org.junit.Before
import org.junit.runner.RunWith
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.fakes.FakeBrowsingModeManager
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.utils.Settings

@RunWith(AndroidJUnit4::class)
class OpenHomeIntentProcessorTest {
    private lateinit var browserStore: BrowserStore
    private lateinit var fenixBrowserUseCases: FenixBrowserUseCases
    private val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
    private val navController: NavController = mockk(relaxed = true)
    private val settings: Settings = mockk()

    @Before
    fun setup() {
        browserStore =
            BrowserStore(
                initialState = BrowserState(tabs = listOf()),
                middleware = EngineMiddleware.create(mockk()),
            )
        fenixBrowserUseCases =
            FenixBrowserUseCases(
                appStore = mockk(relaxed = true),
                browserStore = browserStore,
                tabsUseCases = TabsUseCases(store = browserStore),
                loadUrlUseCase = mockk(relaxed = true),
                searchUseCases = mockk(relaxed = true),
                homepageTitle = "",
                profiler = mockk(relaxed = true),
            )
    }

    @Test
    fun `WHEN intent does not have OPEN_TO_HOME flag THEN do not process`() {
        val processor =
            OpenHomeIntentProcessor(
                fenixBrowserUseCases = fenixBrowserUseCases,
                browsingModeManager = browsingModeManager,
            )

        val initIntent = Intent()
        val outIntent = Intent()
        assertFalse(
            processor.process(
                intent = initIntent,
                navController = navController,
                out = outIntent,
                settings = settings,
            )
        )

        assertNull(outIntent.extras?.getBoolean(HomeActivity.OPEN_TO_HOME))
        assertFalse(browsingModeManager.hasModeUpdated)
        assertEquals(0, browserStore.state.tabs.size)
        verify { navController wasNot Called }
    }

    @Test
    fun `WHEN intent has OPEN_TO_HOME flag with false value THEN do not process`() {
        val processor =
            OpenHomeIntentProcessor(
                fenixBrowserUseCases = fenixBrowserUseCases,
                browsingModeManager = browsingModeManager,
            )

        val initIntent =
            Intent().apply {
                putExtra(HomeActivity.OPEN_TO_HOME, false)
            }
        val outIntent = Intent()
        assertFalse(
            processor.process(
                intent = initIntent,
                navController = navController,
                out = outIntent,
                settings = settings,
            )
        )

        assertNull(outIntent.extras?.getBoolean(HomeActivity.OPEN_TO_HOME))
        assertFalse(browsingModeManager.hasModeUpdated)
        assertEquals(0, browserStore.state.tabs.size)
        verify { navController wasNot Called }
    }

    @Test
    fun `GIVEN normal browsing mode WHEN intent has OPEN_TO_HOME flag with true value THEN process`() {
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
        val processor =
            OpenHomeIntentProcessor(
                fenixBrowserUseCases = fenixBrowserUseCases,
                browsingModeManager = browsingModeManager,
            )

        val initIntent =
            Intent().apply {
                putExtra(HomeActivity.OPEN_TO_HOME, true)
            }
        val outIntent = Intent()
        assertTrue(
            processor.process(
                intent = initIntent,
                navController = navController,
                out = outIntent,
                settings = settings,
            )
        )

        assertNotNull(outIntent.extras)
        assertFalse(outIntent.extras!!.getBoolean(HomeActivity.OPEN_TO_HOME))
        assertTrue(browsingModeManager.hasModeUpdated)
        assertEquals(1, browserStore.state.tabs.size)
        val tab = browserStore.state.tabs[0]
        assertEquals(false, tab.content.private)
        verify { navController.nav(null, NavGraphDirections.actionGlobalHome()) }
    }

    @Test
    fun `GIVEN private browsing mode WHEN intent has OPEN_TO_HOME flag with true value THEN process`() {
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Private)
        val processor =
            OpenHomeIntentProcessor(
                fenixBrowserUseCases = fenixBrowserUseCases,
                browsingModeManager = browsingModeManager,
            )

        val initIntent =
            Intent().apply {
                putExtra(HomeActivity.OPEN_TO_HOME, true)
            }
        val outIntent = Intent()
        assertTrue(
            processor.process(
                intent = initIntent,
                navController = navController,
                out = outIntent,
                settings = settings,
            )
        )

        assertNotNull(outIntent.extras)
        assertFalse(outIntent.extras!!.getBoolean(HomeActivity.OPEN_TO_HOME))
        assertTrue(browsingModeManager.hasModeUpdated)
        assertEquals(1, browserStore.state.tabs.size)
        val tab = browserStore.state.tabs[0]
        assertEquals(true, tab.content.private)
        verify { navController.nav(null, NavGraphDirections.actionGlobalHome()) }
    }

    @Test
    fun `GIVEN homepage as a new tab is enabled WHEN intent is from the private browsing pinned shortcut THEN process`() {
        every { settings.enableHomepageAsNewTab } returns true
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Private)
        val processor =
            OpenHomeIntentProcessor(
                fenixBrowserUseCases = fenixBrowserUseCases,
                browsingModeManager = browsingModeManager,
            )

        val initIntent =
            Intent().apply {
                putExtra(HomeActivity.PRIVATE_BROWSING_MODE, true)
                putExtra(
                    HomeActivity.OPEN_TO_SEARCH,
                    StartSearchIntentProcessor.PRIVATE_BROWSING_PINNED_SHORTCUT,
                )
            }
        val outIntent = Intent()
        assertTrue(
            processor.process(
                intent = initIntent,
                navController = navController,
                out = outIntent,
                settings = settings,
            )
        )

        assertNotNull(outIntent.extras)
        assertFalse(outIntent.extras!!.getBoolean(HomeActivity.OPEN_TO_HOME))
        assertNull(outIntent.extras!!.getString(HomeActivity.OPEN_TO_SEARCH))
        assertTrue(browsingModeManager.hasModeUpdated)
        assertEquals(1, browserStore.state.tabs.size)
        val tab = browserStore.state.tabs[0]
        assertEquals(true, tab.content.private)
        verify { navController.nav(null, NavGraphDirections.actionGlobalHome()) }
    }

    @Test
    fun `GIVEN homepage as a new tab is disabled WHEN intent is from the private browsing pinned shortcut THEN do not process`() {
        every { settings.enableHomepageAsNewTab } returns false
        val processor =
            OpenHomeIntentProcessor(
                fenixBrowserUseCases = fenixBrowserUseCases,
                browsingModeManager = browsingModeManager,
            )

        val initIntent =
            Intent().apply {
                putExtra(HomeActivity.PRIVATE_BROWSING_MODE, true)
                putExtra(
                    HomeActivity.OPEN_TO_SEARCH,
                    StartSearchIntentProcessor.PRIVATE_BROWSING_PINNED_SHORTCUT,
                )
            }
        val outIntent = Intent()
        assertFalse(
            processor.process(
                intent = initIntent,
                navController = navController,
                out = outIntent,
                settings = settings,
            )
        )

        assertNull(outIntent.extras?.getBoolean(HomeActivity.OPEN_TO_HOME))
        assertFalse(browsingModeManager.hasModeUpdated)
        assertEquals(0, browserStore.state.tabs.size)
        verify { navController wasNot Called }
    }
}
