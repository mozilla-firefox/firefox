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
import io.mockk.verifyOrder
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.utils.SwitchTabIntent
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class OpenSpecificTabIntentProcessorTest {
    private lateinit var activity: HomeActivity
    private lateinit var navController: NavController
    private lateinit var out: Intent
    private lateinit var processor: OpenSpecificTabIntentProcessor
    private val settings: Settings = mockk()

    @Before
    fun setup() {
        activity = mockk(relaxed = true)
        navController = mockk(relaxed = true)
        out = mockk()
        processor = OpenSpecificTabIntentProcessor(activity)
    }

    @Test
    fun `GIVEN a blank intent WHEN it is processed THEN nothing should happen`() {
        assertFalse(processor.process(Intent(), navController, out, settings))

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `GIVEN an intent with wrong action WHEN it is processed THEN nothing should happen`() {
        val intent =
            Intent().apply {
                action = TEST_WRONG_ACTION
            }

        assertFalse(processor.process(intent, navController, out, settings))

        verify { activity wasNot Called }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `GIVEN an intent with null extra string WHEN it is processed THEN openToBrowser should not be called`() {
        val intent =
            Intent().apply {
                action = SwitchTabIntent.ACTION_SWITCH_TAB
            }

        val store =
            BrowserStore(BrowserState(tabs = listOf(createTab(id = TEST_SESSION_ID, url = "https:mozilla.org"))))
        val tabUseCases: TabsUseCases = mockk(relaxed = true)
        every { activity.components.core.store } returns store
        every { activity.components.useCases.tabsUseCases } returns tabUseCases

        assertFalse(processor.process(intent, navController, out, settings))

        verify(exactly = 0) { activity.openToBrowser(BrowserDirection.FromGlobal) }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `GIVEN an intent with correct action and extra string WHEN it is processed THEN session should be selected and openToBrowser should be called`() {
        val intent =
            Intent().apply {
                action = SwitchTabIntent.ACTION_SWITCH_TAB
                putExtra(SwitchTabIntent.EXTRA_TAB_ID, TEST_SESSION_ID)
            }
        val store =
            BrowserStore(BrowserState(tabs = listOf(createTab(id = TEST_SESSION_ID, url = "https:mozilla.org"))))
        val tabUseCases: TabsUseCases = mockk(relaxed = true)
        every { activity.components.core.store } returns store
        every { activity.components.useCases.tabsUseCases } returns tabUseCases

        assertTrue(processor.process(intent, navController, out, settings))

        verifyOrder {
            tabUseCases.selectTab(TEST_SESSION_ID)
            activity.openToBrowser(BrowserDirection.FromGlobal)
        }
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    companion object {
        const val TEST_WRONG_ACTION = "test-action"
        const val TEST_SESSION_ID = "test-session-id"
    }
}
