/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.deletebrowsingdata

import io.mockk.Runs
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.just
import io.mockk.mockk
import io.mockk.slot
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.ui.icons.R as iconsR
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction

@OptIn(ExperimentalCoroutinesApi::class)
class QuitMenuItemProviderTest {
    private val deleteBrowsingDataController: DeleteBrowsingDataController = mockk()
    private var quitCount = 0
    private val menu = FakeMenuHost()

    @Test
    fun `GIVEN the user wants their data deleted on quit WHEN providing the item THEN offer quitting`() {
        val provider = createProvider(deletesBrowsingDataOnQuit = true)

        assertEquals(
            StandardMenuItem(
                title =
                    MenuItemTitle(Text.Resource(R.string.browser_menu_delete_browsing_data_on_quit, listOf(APP_NAME))),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_cross_circle_fill_24),
                onClickEvent = MenuAction.DeleteBrowsingDataAndQuit,
                state = MenuItemState.WARNING,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN the user keeps their data when quitting WHEN providing the item THEN don't show it`() {
        val provider = createProvider(deletesBrowsingDataOnQuit = false)

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN customize menu target AND deletesBrowsingDataOnQuit is true WHEN providing the item THEN show default item`() {
        val provider = createProvider(deletesBrowsingDataOnQuit = true, target = MenuTarget.CustomizeMenu)

        assertEquals(
            StandardMenuItem(
                title =
                    MenuItemTitle(Text.Resource(R.string.browser_menu_delete_browsing_data_on_quit, listOf(APP_NAME))),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_cross_circle_fill_24),
                state = MenuItemState.WARNING,
            ),
            provider.itemsFlow.value.single(),
        )
    }

    @Test
    fun `GIVEN customize menu target AND deletesBrowsingDataOnQuit is false WHEN providing the item THEN don't show it`() {
        val provider = createProvider(deletesBrowsingDataOnQuit = false, target = MenuTarget.CustomizeMenu)

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `WHEN clicking the item THEN close the menu and quit only after deleting the browsing data`() = runTest {
        val onDeletionComplete = slot<() -> Unit>()
        coEvery { deleteBrowsingDataController.clearBrowsingDataOnQuit(capture(onDeletionComplete)) } just Runs
        val provider = createProvider(deletesBrowsingDataOnQuit = true, applicationScope = this)

        provider.onEvent(MenuAction.DeleteBrowsingDataAndQuit, menu)
        runCurrent()

        assertTrue(menu.isDismissed)
        coVerify { deleteBrowsingDataController.clearBrowsingDataOnQuit(any()) }
        assertEquals(0, quitCount)
        onDeletionComplete.captured()
        assertEquals(1, quitCount)
    }

    @Test
    fun `WHEN building the item THEN handle all events it can dispatch and no others`() {
        val provider = createProvider(deletesBrowsingDataOnQuit = true)

        val events = provider.itemsFlow.value.single().reachableEvents()

        assertTrue(events.all { provider.handles(it) }, "Not all of $events are handled")
        assertFalse(provider.handles(MenuAction.Navigate.Settings))
    }

    private fun createProvider(
        deletesBrowsingDataOnQuit: Boolean,
        applicationScope: CoroutineScope = CoroutineScope(Dispatchers.Unconfined),
        target: MenuTarget = MenuTarget.BrowserTab,
    ) =
        QuitMenuItemProvider(
            appName = APP_NAME,
            deletesBrowsingDataOnQuit = deletesBrowsingDataOnQuit,
            deleteBrowsingDataController = { deleteBrowsingDataController },
            quitApplicationDelegate = { quitCount++ },
            applicationScope = applicationScope,
            target = target,
        )

    private companion object {
        const val APP_NAME = "Firefox"
    }
}
