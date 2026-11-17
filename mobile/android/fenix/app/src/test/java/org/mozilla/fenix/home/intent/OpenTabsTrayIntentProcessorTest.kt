/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.content.Intent
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import androidx.navigation.NavOptions
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.Called
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.fakes.FakeBrowsingModeManager
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.utils.Settings

@RunWith(AndroidJUnit4::class)
class OpenTabsTrayIntentProcessorTest {
    private lateinit var navController: NavController
    private lateinit var out: Intent
    private val settings: Settings = mockk()

    @Before
    fun setup() {
        navController = mockk(relaxed = true)
        out = mockk(relaxed = true)
    }

    @Test
    fun `GIVEN intent does not have OPEN_TO_TABS_TRAY flag WHEN the intent is processed THEN do not process`() {
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
        val processor = OpenTabsTrayIntentProcessor(browsingModeManager)

        assertFalse(processor.process(Intent(), navController, out, settings))

        assertFalse(browsingModeManager.hasModeUpdated)
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `GIVEN OPEN_TO_TABS_TRAY flag is false WHEN the intent is processed THEN do not process`() {
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
        val processor = OpenTabsTrayIntentProcessor(browsingModeManager)
        val intent = Intent().apply { putExtra(HomeActivity.OPEN_TO_TABS_TRAY, false) }

        assertFalse(processor.process(intent, navController, out, settings))

        assertFalse(browsingModeManager.hasModeUpdated)
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `GIVEN private browsing mode WHEN an intent to open the tabs tray is processed THEN navigate to the private tabs page`() {
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Private)
        val processor = OpenTabsTrayIntentProcessor(browsingModeManager)
        val intent = Intent().apply { putExtra(HomeActivity.OPEN_TO_TABS_TRAY, true) }

        assertTrue(processor.process(intent, navController, out, settings))

        assertTrue(browsingModeManager.hasModeUpdated)
        verify {
            navController.navigate(NavGraphDirections.actionGlobalTabManagementFragment(page = Page.PrivateTabs))
        }
        verify { out.removeExtra(HomeActivity.OPEN_TO_TABS_TRAY) }
    }

    @Test
    fun `GIVEN normal browsing mode WHEN an intent to open the tabs tray is processed THEN navigate to the normal tabs page`() {
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
        val processor = OpenTabsTrayIntentProcessor(browsingModeManager)
        val intent = Intent().apply { putExtra(HomeActivity.OPEN_TO_TABS_TRAY, true) }

        assertTrue(processor.process(intent, navController, out, settings))

        assertTrue(browsingModeManager.hasModeUpdated)
        verify {
            navController.navigate(NavGraphDirections.actionGlobalTabManagementFragment(page = Page.NormalTabs))
        }
        verify { out.removeExtra(HomeActivity.OPEN_TO_TABS_TRAY) }
    }

    @Test
    fun `GIVEN normal browsing mode WHEN an intent to open the tabs tray in private mode is processed THEN switch to private mode and navigate to the private tabs page`() {
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
        val processor = OpenTabsTrayIntentProcessor(browsingModeManager)
        val intent =
            Intent().apply {
                putExtra(HomeActivity.OPEN_TO_TABS_TRAY, true)
                putExtra(HomeActivity.PRIVATE_BROWSING_MODE, true)
            }

        assertTrue(processor.process(intent, navController, out, settings))

        assertEquals(BrowsingMode.Private, browsingModeManager.mode)
        verify {
            navController.navigate(NavGraphDirections.actionGlobalTabManagementFragment(page = Page.PrivateTabs))
        }
        verify { out.removeExtra(HomeActivity.OPEN_TO_TABS_TRAY) }
    }

    @Test
    fun `GIVEN the tabs tray is already displayed WHEN an intent to open the tabs tray is processed THEN do not navigate again`() {
        val browsingModeManager = FakeBrowsingModeManager(BrowsingMode.Normal)
        val processor = OpenTabsTrayIntentProcessor(browsingModeManager)
        val intent =
            Intent().apply {
                putExtra(HomeActivity.OPEN_TO_TABS_TRAY, true)
                putExtra(HomeActivity.PRIVATE_BROWSING_MODE, true)
            }
        every { navController.currentDestination?.id } returns R.id.tabManagementFragment

        assertTrue(processor.process(intent, navController, out, settings))

        assertEquals(BrowsingMode.Private, browsingModeManager.mode)
        verify(exactly = 0) { navController.navigate(any<NavDirections>(), any<NavOptions>()) }
        verify { out.removeExtra(HomeActivity.OPEN_TO_TABS_TRAY) }
    }
}
