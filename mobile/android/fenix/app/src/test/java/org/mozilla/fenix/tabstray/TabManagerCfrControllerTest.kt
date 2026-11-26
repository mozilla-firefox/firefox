/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray

import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.tabstray.data.createTab
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.redux.store.TabsTrayStore
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

private val currentTime = System.currentTimeMillis()

@RunWith(RobolectricTestRunner::class)
class TabManagerCfrControllerTest {
    private lateinit var settings: Settings
    private lateinit var tabsTrayStore: TabsTrayStore
    private lateinit var cfrController: TabManagerCfrController

    @Before
    fun setup() {
        settings = Settings(testContext)
        tabsTrayStore =
            TabsTrayStore(
                initialState =
                    TabsTrayState(
                        selectedTabId = "tab1",
                        inactiveTabs = TabsTrayState.InactiveTabsState(showCFR = true),
                        normalTabsState =
                            TabsTrayState.NormalTabsState(
                                items =
                                    listOf(
                                        createTab(id = "tab1", url = ""),
                                        createTab(id = "tab2", url = ""),
                                        createTab(id = "tab3", url = ""),
                                    )
                            ),
                    )
            )
        cfrController =
            TabManagerCfrController(
                settings = settings,
                tabsTrayStore = tabsTrayStore,
                currentTimeProvider = { currentTime },
            )
    }

    @Test
    fun `WHEN onTabAutoCloseBannerDismiss is called THEN mark the banner as shown`() {
        settings.shouldShowAutoCloseTabsBanner = true

        cfrController.onTabAutoCloseBannerDismiss()

        assertFalse(settings.shouldShowAutoCloseTabsBanner)
        assertEquals(currentTime, settings.lastCfrShownTimeInMillis)
    }

    @Test
    fun `WHEN onInactiveTabsCfrClick is called THEN mark the CFR as shown`() {
        settings.shouldShowInactiveTabsOnboardingPopup = true

        cfrController.onInactiveTabsCfrClick()

        assertFalse(settings.shouldShowInactiveTabsOnboardingPopup)
        assertEquals(currentTime, settings.lastCfrShownTimeInMillis)
        assertFalse(tabsTrayStore.state.inactiveTabs.showCFR)
    }

    @Test
    fun `WHEN onInactiveTabsCfrDismiss is called THEN mark the CFR as shown`() {
        settings.shouldShowInactiveTabsOnboardingPopup = true

        cfrController.onInactiveTabsCfrDismiss()

        assertFalse(settings.shouldShowInactiveTabsOnboardingPopup)
        assertEquals(currentTime, settings.lastCfrShownTimeInMillis)
        assertFalse(tabsTrayStore.state.inactiveTabs.showCFR)
    }

    @Test
    fun `GIVEN a list of tabs WHEN a tab is present with an ID THEN the index is returned`() {
        val tab1 = createTab(id = "tab1", url = "")
        val tab2 = createTab(id = "tab2", url = "")
        val tab3 = createTab(id = "tab3", url = "")

        val result = cfrController.getTabPositionFromId(listOf(tab1, tab2, tab3), "tab2")

        assertEquals(1, result)
    }

    @Test
    fun `GIVEN a list of tabs WHEN no tab matches the given ID THEN minus one is returned`() {
        val tab1 = createTab(id = "tab1", url = "")

        val result = cfrController.getTabPositionFromId(listOf(tab1), "missing")

        assertEquals(-1, result)
    }

    @Test
    fun `GIVEN an adjacent tab WHEN maybeMarkTabSwipeCfrReady is called THEN shouldShowTabSwipeCfr is set`() {
        settings.hasShownTabSwipeCFR = false
        settings.isTabStripEnabled = false
        settings.isSwipeToolbarToSwitchTabsEnabled = true
        settings.shouldShowTabSwipeCFR = false
        val adjacentTab = createTab(id = "tab2", url = "")

        cfrController.maybeMarkTabSwipeCfrReady(adjacentTab)

        assertTrue(settings.shouldShowTabSwipeCFR)
    }
}
