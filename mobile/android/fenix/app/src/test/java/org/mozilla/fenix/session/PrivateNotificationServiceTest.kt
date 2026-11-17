/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.session

import android.content.ComponentName
import android.content.Intent
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertNotNull
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.privatemode.notification.AbstractPrivateNotificationService.Companion.ACTION_ERASE
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.HomeActivity.Companion.OPEN_TO_TABS_TRAY
import org.mozilla.fenix.HomeActivity.Companion.PRIVATE_BROWSING_MODE
import org.mozilla.fenix.HomeActivity.Companion.START_IN_RECENTS_SCREEN
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings
import org.robolectric.Robolectric
import org.robolectric.Shadows.shadowOf
import org.robolectric.android.controller.ServiceController

@RunWith(AndroidJUnit4::class)
class PrivateNotificationServiceTest {

    private lateinit var controller: ServiceController<PrivateNotificationService>

    @Before
    fun setup() {
        controller =
            Robolectric.buildService(
                PrivateNotificationService::class.java,
                Intent(ACTION_ERASE),
            )
        setAppInBackground(false)
    }

    private fun setAppInBackground(inBackground: Boolean) {
        controller.get().finishAndRemoveTaskIfInBackground = { inBackground }
    }

    @Test
    fun `service opens home activity in private mode if app is in private mode`() {
        val selectedPrivateTab = createTab("https://mozilla.org", private = true)
        val store = BrowserStore(BrowserState(tabs = listOf(selectedPrivateTab), selectedTabId = selectedPrivateTab.id))

        every { testContext.components.core.store } returns store
        every { testContext.components.useCases.tabsUseCases } returns TabsUseCases(store)
        every { testContext.components.settings.enableHomepageAsNewTab } returns false

        val service = shadowOf(controller.get())
        controller.startCommand(0, 0)

        val intent = service.nextStartedActivity
        assertNotNull(intent)
        assertEquals(ComponentName(testContext, HomeActivity::class.java), intent.component)
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK, intent.flags)
        assertEquals(true, intent.extras?.getBoolean(PRIVATE_BROWSING_MODE))
        assertEquals(false, intent.extras?.getBoolean(OPEN_TO_TABS_TRAY))
    }

    @Test
    fun `WHEN homepage as a new tab is enabled AND app is in private mode in the foreground THEN erasing private tabs opens the home activity to the private tabs tray`() {
        val selectedPrivateTab = createTab("https://mozilla.org", private = true)
        val store = BrowserStore(BrowserState(tabs = listOf(selectedPrivateTab), selectedTabId = selectedPrivateTab.id))
        val fenixBrowserUseCases = mockk<FenixBrowserUseCases>(relaxed = true)
        val settings = mockk<Settings>(relaxed = true)
        val appStore = mockk<AppStore>(relaxed = true)

        every { testContext.components.core.store } returns store
        every { testContext.components.useCases.tabsUseCases } returns TabsUseCases(store)
        every { testContext.components.useCases.fenixBrowserUseCases } returns fenixBrowserUseCases
        every { testContext.components.settings } returns settings
        every { testContext.components.appStore } returns appStore
        every { settings.enableHomepageAsNewTab } returns true

        val service = shadowOf(controller.get())
        controller.startCommand(0, 0)

        verify(exactly = 0) { fenixBrowserUseCases.addNewHomepageTab(private = any()) }
        verify { appStore.dispatch(AppAction.UpdateTabsTrayVisibility(true)) }
        verify(exactly = 0) { settings.lastKnownMode = any() }
        assertNull(store.state.selectedTabId)

        val intent = service.nextStartedActivity
        assertNotNull(intent)
        assertEquals(ComponentName(testContext, HomeActivity::class.java), intent.component)
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK, intent.flags)
        assertEquals(true, intent.extras?.getBoolean(PRIVATE_BROWSING_MODE))
        assertEquals(true, intent.extras?.getBoolean(OPEN_TO_TABS_TRAY))
        assertEquals(false, intent.extras?.getBoolean(START_IN_RECENTS_SCREEN))
    }

    @Test
    fun `WHEN homepage as a new tab is enabled AND app is in private mode in the background THEN erasing private tabs switches to normal mode without opening an activity`() {
        val selectedPrivateTab = createTab("https://mozilla.org", private = true, lastAccess = 3L)
        val olderNormalTab = createTab("https://example.com", private = false, lastAccess = 1L)
        val lastNormalTab = createTab("https://firefox.com", private = false, lastAccess = 2L)
        val store =
            BrowserStore(
                BrowserState(
                    tabs = listOf(lastNormalTab, olderNormalTab, selectedPrivateTab),
                    selectedTabId = selectedPrivateTab.id,
                )
            )
        val fenixBrowserUseCases = mockk<FenixBrowserUseCases>(relaxed = true)
        val settings = mockk<Settings>(relaxed = true)
        val appStore = mockk<AppStore>(relaxed = true)

        every { testContext.components.core.store } returns store
        every { testContext.components.useCases.tabsUseCases } returns TabsUseCases(store)
        every { testContext.components.useCases.fenixBrowserUseCases } returns fenixBrowserUseCases
        every { testContext.components.settings } returns settings
        every { testContext.components.appStore } returns appStore
        every { settings.enableHomepageAsNewTab } returns true
        setAppInBackground(true)

        val service = shadowOf(controller.get())
        controller.startCommand(0, 0)

        verify(exactly = 0) { fenixBrowserUseCases.addNewHomepageTab(private = any()) }
        verify { settings.lastKnownMode = BrowsingMode.Normal }
        verify { appStore.dispatch(AppAction.BrowsingModeManagerModeChanged(BrowsingMode.Normal)) }
        verify(exactly = 0) { appStore.dispatch(AppAction.UpdateTabsTrayVisibility(true)) }
        assertEquals(listOf(lastNormalTab.id, olderNormalTab.id), store.state.tabs.map { it.id })
        assertEquals(lastNormalTab.id, store.state.selectedTabId)
        assertNull(service.nextStartedActivity)
    }

    @Test
    fun `WHEN homepage as a new tab is enabled AND app is in private mode in the background with no normal tabs THEN erasing private tabs leaves no selected tab`() {
        val selectedPrivateTab = createTab("https://mozilla.org", private = true)
        val store = BrowserStore(BrowserState(tabs = listOf(selectedPrivateTab), selectedTabId = selectedPrivateTab.id))
        val fenixBrowserUseCases = mockk<FenixBrowserUseCases>(relaxed = true)
        val settings = mockk<Settings>(relaxed = true)
        val appStore = mockk<AppStore>(relaxed = true)

        every { testContext.components.core.store } returns store
        every { testContext.components.useCases.tabsUseCases } returns TabsUseCases(store)
        every { testContext.components.useCases.fenixBrowserUseCases } returns fenixBrowserUseCases
        every { testContext.components.settings } returns settings
        every { testContext.components.appStore } returns appStore
        every { settings.enableHomepageAsNewTab } returns true
        setAppInBackground(true)

        val service = shadowOf(controller.get())
        controller.startCommand(0, 0)

        verify(exactly = 0) { fenixBrowserUseCases.addNewHomepageTab(private = any()) }
        verify { settings.lastKnownMode = BrowsingMode.Normal }
        assertEquals(emptyList<String>(), store.state.tabs.map { it.id })
        assertNull(store.state.selectedTabId)
        assertNull(service.nextStartedActivity)
    }

    @Test
    fun `WHEN homepage as a new tab is enabled AND app is in normal mode THEN erasing private tabs does not reopen a homepage tab or open an activity`() {
        val selectedTab = createTab("https://mozilla.org", private = false)
        val store = BrowserStore(BrowserState(tabs = listOf(selectedTab), selectedTabId = selectedTab.id))
        val fenixBrowserUseCases = mockk<FenixBrowserUseCases>(relaxed = true)

        every { testContext.components.core.store } returns store
        every { testContext.components.useCases.tabsUseCases } returns TabsUseCases(store)
        every { testContext.components.useCases.fenixBrowserUseCases } returns fenixBrowserUseCases
        every { testContext.components.settings.enableHomepageAsNewTab } returns true

        val service = shadowOf(controller.get())
        controller.startCommand(0, 0)

        verify(exactly = 0) { fenixBrowserUseCases.addNewHomepageTab(private = any()) }
        assertNull(service.nextStartedActivity)
    }

    @Test
    fun `WHEN homepage as a new tab is disabled AND app is in private mode in the background THEN erasing private tabs opens the home activity in private mode`() {
        val selectedPrivateTab = createTab("https://mozilla.org", private = true)
        val store = BrowserStore(BrowserState(tabs = listOf(selectedPrivateTab), selectedTabId = selectedPrivateTab.id))

        every { testContext.components.core.store } returns store
        every { testContext.components.useCases.tabsUseCases } returns TabsUseCases(store)
        every { testContext.components.settings.enableHomepageAsNewTab } returns false
        setAppInBackground(true)

        val service = shadowOf(controller.get())
        controller.startCommand(0, 0)

        val intent = service.nextStartedActivity
        assertNotNull(intent)
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK, intent.flags)
        assertEquals(true, intent.extras?.getBoolean(PRIVATE_BROWSING_MODE))
        assertEquals(false, intent.extras?.getBoolean(OPEN_TO_TABS_TRAY))
        assertEquals(true, intent.extras?.getBoolean(START_IN_RECENTS_SCREEN))
    }

    @Test
    fun `WHEN homepage as a new tab is disabled AND app is in private mode in the foreground THEN erasing private tabs opens the home activity in private mode`() {
        val selectedTab = createTab("https://mozilla.org", private = true)
        val store = BrowserStore(BrowserState(tabs = listOf(selectedTab), selectedTabId = selectedTab.id))
        val fenixBrowserUseCases = mockk<FenixBrowserUseCases>(relaxed = true)

        every { testContext.components.core.store } returns store
        every { testContext.components.useCases.tabsUseCases } returns TabsUseCases(store)
        every { testContext.components.useCases.fenixBrowserUseCases } returns fenixBrowserUseCases
        every { testContext.components.settings.enableHomepageAsNewTab } returns false

        val service = shadowOf(controller.get())
        controller.startCommand(0, 0)

        val intent = service.nextStartedActivity
        assertNotNull(intent)
        assertEquals(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK, intent.flags)
        assertEquals(true, intent.extras?.getBoolean(PRIVATE_BROWSING_MODE))
        assertEquals(false, intent.extras?.getBoolean(OPEN_TO_TABS_TRAY))
        assertEquals(false, intent.extras?.getBoolean(START_IN_RECENTS_SCREEN))
    }

    @Test
    fun `WHEN app is in normal mode THEN erasing private tabs does not finish and remove the app tasks`() {
        val selectedTab = createTab("https://mozilla.org", private = false)
        val privateTab = createTab("https://firefox.com", private = true)
        val store = BrowserStore(BrowserState(tabs = listOf(selectedTab, privateTab), selectedTabId = selectedTab.id))
        var backgroundCheckCount = 0

        every { testContext.components.core.store } returns store
        every { testContext.components.useCases.tabsUseCases } returns TabsUseCases(store)
        every { testContext.components.settings.enableHomepageAsNewTab } returns true
        controller.get().finishAndRemoveTaskIfInBackground = {
            backgroundCheckCount++
            true
        }

        val service = shadowOf(controller.get())
        controller.startCommand(0, 0)

        assertEquals(0, backgroundCheckCount)
        assertEquals(listOf(selectedTab.id), store.state.tabs.map { it.id })
        assertNull(service.nextStartedActivity)
    }

    @Test
    fun `service starts no activity if app is in normal mode`() {
        val selectedPrivateTab = createTab("https://mozilla.org", private = false)
        val store = BrowserStore(BrowserState(tabs = listOf(selectedPrivateTab), selectedTabId = selectedPrivateTab.id))

        every { testContext.components.core.store } returns store
        every { testContext.components.useCases.tabsUseCases } returns TabsUseCases(store)
        every { testContext.components.settings.enableHomepageAsNewTab } returns false

        val service = shadowOf(controller.get())
        controller.startCommand(0, 0)

        assertNull(service.nextStartedActivity)
    }
}
