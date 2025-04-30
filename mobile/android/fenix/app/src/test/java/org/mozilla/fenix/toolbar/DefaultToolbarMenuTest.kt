/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.toolbar

import android.content.Context
import androidx.lifecycle.LifecycleOwner
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.storage.BookmarksStorage
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.components.toolbar.DefaultToolbarMenu
import org.mozilla.fenix.ext.settings

class DefaultToolbarMenuTest {

    private lateinit var store: BrowserStore
    private lateinit var lifecycleOwner: LifecycleOwner
    private lateinit var toolbarMenu: DefaultToolbarMenu
    private lateinit var context: Context
    private lateinit var bookmarksStorage: BookmarksStorage
    private lateinit var pinnedSiteStorage: PinnedSiteStorage

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    @Before
    fun setUp() {
        lifecycleOwner = mockk(relaxed = true)
        context = mockk(relaxed = true)

        every { context.theme } returns mockk(relaxed = true)

        bookmarksStorage = mockk(relaxed = true)
        pinnedSiteStorage = mockk(relaxed = true)
        store = BrowserStore(
            BrowserState(
                tabs = listOf(
                    createTab(url = "https://firefox.com", id = "1"),
                    createTab(url = "https://getpocket.com", id = "2"),
                ),
                selectedTabId = "1",
            ),
        )
    }

    private fun createMenu() {
        toolbarMenu = spyk(
            DefaultToolbarMenu(
                context = context,
                store = store,
                hasAccountProblem = false,
                onItemTapped = { },
                lifecycleOwner = lifecycleOwner,
                pinnedSiteStorage = pinnedSiteStorage,
                bookmarksStorage = bookmarksStorage,
                isPinningSupported = false,
            ),
        )

        every { toolbarMenu.updateCurrentUrlIsBookmarked(any()) } returns mockk()
        every { toolbarMenu.shouldShowOpenInApp() } returns mockk()
    }

    @Test
    fun `WHEN the bottom toolbar is set THEN the first item in the list is not the navigation`() {
        every { context.settings().shouldUseBottomToolbar } returns true
        createMenu()

        val menuItems = toolbarMenu.coreMenuItems
        assertNotNull(menuItems)

        val firstItem = menuItems[0]
        val newTabItem = toolbarMenu.newTabItem

        assertEquals(newTabItem, firstItem)
    }

    @Test
    fun `WHEN the top toolbar is set THEN the first item in the list is the navigation`() {
        every { context.settings().shouldUseBottomToolbar } returns false
        createMenu()

        val menuItems = toolbarMenu.coreMenuItems
        assertNotNull(menuItems)

        val firstItem = menuItems[0]
        val navToolbar = toolbarMenu.menuToolbar

        assertEquals(navToolbar, firstItem)
    }

    @Test
    fun `WHEN the bottom toolbar is set THEN the nav menu should be the last item`() {
        every { context.settings().shouldUseBottomToolbar } returns true

        createMenu()

        val menuItems = toolbarMenu.coreMenuItems
        assertNotNull(menuItems)

        val lastItem = menuItems[menuItems.size - 1]
        val navToolbar = toolbarMenu.menuToolbar

        assertEquals(navToolbar, lastItem)
    }

    @Test
    fun `WHEN the top toolbar is set THEN settings should be the last item`() {
        every { context.settings().shouldUseBottomToolbar } returns false

        createMenu()

        val menuItems = toolbarMenu.coreMenuItems
        assertNotNull(menuItems)

        val lastItem = menuItems[menuItems.size - 1]
        val settingsItem = toolbarMenu.settingsItem

        assertEquals(settingsItem, lastItem)
    }
}
