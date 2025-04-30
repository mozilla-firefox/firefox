/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray

import android.view.Window
import android.view.WindowManager
import androidx.fragment.app.Fragment
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.ExperimentalCoroutinesApi
import mozilla.components.support.test.libstate.ext.waitUntilIdle
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.ext.removeSecure
import org.mozilla.fenix.ext.secure
import org.mozilla.fenix.utils.Settings

class SecureTabsTrayBindingTest {

    @OptIn(ExperimentalCoroutinesApi::class)
    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    private val settings: Settings = mockk(relaxed = true)
    private val fragment: Fragment = mockk(relaxed = true)
    private val dialog: TabsTrayDialog = mockk(relaxed = true)
    private val window: Window = mockk(relaxed = true)

    @Before
    fun setup() {
        every { fragment.secure() } just Runs
        every { fragment.removeSecure() } just Runs
        every { dialog.window } returns window
        every { window.addFlags(any()) } just Runs
        every { window.clearFlags(any()) } just Runs
    }

    @Test
    fun `WHEN tab selected page switches to private THEN set fragment to secure`() {
        val tabsTrayStore = TabsTrayStore(TabsTrayState())
        val secureTabsTrayBinding = SecureTabsTrayBinding(
            store = tabsTrayStore,
            settings = settings,
            fragment = fragment,
            dialog = dialog,
        )

        secureTabsTrayBinding.start()
        tabsTrayStore.dispatch(TabsTrayAction.PageSelected(Page.positionToPage(Page.PrivateTabs.ordinal)))
        tabsTrayStore.waitUntilIdle()

        verify { fragment.secure() }
        verify { window.addFlags(WindowManager.LayoutParams.FLAG_SECURE) }
    }

    @Test
    fun `WHEN tab selected page switches to private  and allowScreenshotsInPrivateMode true THEN set fragment to un-secure`() {
        val tabsTrayStore = TabsTrayStore(TabsTrayState())
        val secureTabsTrayBinding = SecureTabsTrayBinding(
            store = tabsTrayStore,
            settings = settings,
            fragment = fragment,
            dialog = dialog,
        )
        every { settings.allowScreenshotsInPrivateMode } returns true

        secureTabsTrayBinding.start()
        tabsTrayStore.dispatch(TabsTrayAction.PageSelected(Page.positionToPage(Page.PrivateTabs.ordinal)))
        tabsTrayStore.waitUntilIdle()

        verify { fragment.removeSecure() }
        verify { window.clearFlags(WindowManager.LayoutParams.FLAG_SECURE) }
    }

    @Test
    fun `GIVEN not in private mode WHEN tab selected page switches to normal tabs from private THEN set fragment to un-secure`() {
        every { settings.lastKnownMode.isPrivate } returns false
        val tabsTrayStore = TabsTrayStore(TabsTrayState())
        val secureTabsTrayBinding = SecureTabsTrayBinding(
            store = tabsTrayStore,
            settings = settings,
            fragment = fragment,
            dialog = dialog,
        )

        secureTabsTrayBinding.start()
        tabsTrayStore.dispatch(TabsTrayAction.PageSelected(Page.positionToPage(Page.NormalTabs.ordinal)))
        tabsTrayStore.waitUntilIdle()

        verify { fragment.removeSecure() }
        verify { window.clearFlags(WindowManager.LayoutParams.FLAG_SECURE) }
    }

    @Test
    fun `GIVEN private mode WHEN tab selected page switches to normal tabs from private THEN do nothing`() {
        every { settings.lastKnownMode.isPrivate } returns true
        val tabsTrayStore = TabsTrayStore(TabsTrayState())
        val secureTabsTrayBinding = SecureTabsTrayBinding(
            store = tabsTrayStore,
            settings = settings,
            fragment = fragment,
            dialog = dialog,
        )

        secureTabsTrayBinding.start()
        tabsTrayStore.dispatch(TabsTrayAction.PageSelected(Page.positionToPage(Page.NormalTabs.ordinal)))
        tabsTrayStore.waitUntilIdle()

        verify(exactly = 0) { fragment.removeSecure() }
        verify(exactly = 0) { window.clearFlags(WindowManager.LayoutParams.FLAG_SECURE) }
    }
}
