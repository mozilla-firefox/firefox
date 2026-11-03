/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.coEvery
import io.mockk.coVerify
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.feature.top.sites.TopSite
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.ui.icons.R as iconsR
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ShortcutAction
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.fake.reachableEvents
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.home.topsites.TopSitesConfigConstants.TOP_SITES_MAX_COUNT

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class ShortcutMenuItemProviderTest {
    private val addPinnedSite: TopSitesUseCases.AddPinnedSiteUseCase = mockk(relaxed = true)
    private val removeTopSite: TopSitesUseCases.RemoveTopSiteUseCase = mockk(relaxed = true)
    private val topSitesUseCases: TopSitesUseCases = mockk {
        every { addPinnedSites } returns addPinnedSite
        every { removeTopSites } returns removeTopSite
    }
    private val appStore: AppStore = mockk(relaxed = true)
    private val menu = FakeMenuHost()

    @Test
    fun `GIVEN the current page is not a shortcut WHEN providing the item THEN offer adding it to shortcuts`() =
        runTest {
            val provider = provider(pinnedSiteStorage = storageWith(shortcut = null))

            assertEquals(addShortcutItem, provider.itemsFlow.value.single())
        }

    @Test
    fun `GIVEN the current page is a shortcut WHEN providing the item THEN offer removing it from shortcuts`() =
        runTest {
            val provider = provider(pinnedSiteStorage = storageWith(shortcut = shortcut))

            // Adding the page is offered before reading from disk, removing it once it is known to be a shortcut.
            assertEquals(addShortcutItem, provider.itemsFlow.value.single())

            runCurrent()

            assertEquals(removeShortcutItem, provider.itemsFlow.value.single())
        }

    @Test
    fun `GIVEN another page is a shortcut WHEN providing the item THEN keep offering adding the current one`() =
        runTest {
            val otherShortcut = shortcut.copy(url = "https://example.org")
            val provider = provider(pinnedSiteStorage = storageWith(shortcut = otherShortcut))

            runCurrent()

            assertEquals(addShortcutItem, provider.itemsFlow.value.single())
        }

    @Test
    fun `GIVEN shortcuts are disabled WHEN providing the item THEN don't show it`() = runTest {
        val provider = provider(pinnedSiteStorage = storageWith(shortcut = shortcut), areShortcutsEnabled = false)

        runCurrent()

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `GIVEN there is no page shown WHEN providing the item THEN don't show it`() = runTest {
        val provider = provider(browserStore = BrowserStore(), pinnedSiteStorage = storageWith(shortcut = shortcut))

        runCurrent()

        assertTrue(provider.itemsFlow.value.isEmpty())
    }

    @Test
    fun `WHEN adding the current page to shortcuts THEN pin it, tell about it and close the menu`() = runTest {
        val provider = provider(pinnedSiteStorage = storageWith(shortcut = null))

        provider.onEvent(MenuAction.AddShortcut, menu)
        runCurrent()

        coVerify { addPinnedSite(title = TEST_TITLE, url = TEST_URL) }
        verify {
            appStore.dispatch(
                ShortcutAction.ShortcutAdded(
                    url = TEST_URL,
                    source = AddShortcutSource.MANUAL,
                    entryPoint = AddShortcutEntryPoint.PAGE_MENU,
                )
            )
        }
        assertTrue(menu.isDismissed)
    }

    @Test
    fun `GIVEN there are TOP_SITES_MAX_COUNT number of pinned sites WHEN adding another one THEN still pin it`() =
        runTest {
            val shortcuts =
                List(TOP_SITES_MAX_COUNT) { index ->
                    TopSite.Pinned(
                        id = index.toLong(),
                        title = "Example",
                        url = "https://example.org/$index",
                        createdAt = 0,
                    )
                }
            val provider = provider(pinnedSiteStorage = mockk { coEvery { getPinnedSites() } returns shortcuts })

            provider.onEvent(MenuAction.AddShortcut, menu)
            runCurrent()

            coVerify { addPinnedSite(title = TEST_TITLE, url = TEST_URL) }
            verify {
                appStore.dispatch(
                    ShortcutAction.ShortcutAdded(
                        url = TEST_URL,
                        source = AddShortcutSource.MANUAL,
                        entryPoint = AddShortcutEntryPoint.PAGE_MENU,
                    )
                )
            }
            assertTrue(menu.isDismissed)
        }

    @Test
    fun `GIVEN the current page already is a shortcut WHEN adding it again THEN don't add it`() = runTest {
        val provider = provider(pinnedSiteStorage = storageWith(shortcut = shortcut))

        provider.onEvent(MenuAction.AddShortcut, menu)
        runCurrent()

        coVerify(exactly = 0) { addPinnedSite(any(), any(), any()) }
    }

    @Test
    fun `WHEN removing the current page from shortcuts THEN remove it and close the menu`() = runTest {
        val provider = provider(pinnedSiteStorage = storageWith(shortcut = shortcut))

        provider.onEvent(MenuAction.RemoveShortcut, menu)
        runCurrent()

        coVerify { removeTopSite(topSite = shortcut) }
        assertTrue(menu.isDismissed)
    }

    @Test
    fun `WHEN the page is a shortcut or not THEN handle all events the item can dispatch and no others`() = runTest {
        listOf(null, shortcut).forEach { shortcut ->
            val provider = provider(pinnedSiteStorage = storageWith(shortcut = shortcut))
            runCurrent()

            val events = provider.itemsFlow.value.single().reachableEvents()

            assertTrue(events.all { provider.handles(it) }, "Not all of $events are handled")
            assertFalse(provider.handles(MenuAction.AddBookmark))
        }
    }

    private fun TestScope.provider(
        browserStore: BrowserStore = browserStoreWithSelectedTab(),
        pinnedSiteStorage: PinnedSiteStorage,
        areShortcutsEnabled: Boolean = true,
        target: MenuTarget = MenuTarget.BrowserTab,
    ) =
        ShortcutMenuItemProvider(
            browserStore = browserStore,
            target = target,
            pinnedSiteStorage = pinnedSiteStorage,
            areShortcutsEnabled = areShortcutsEnabled,
            topSitesUseCases = topSitesUseCases,
            appStore = appStore,
            // The shortcuts are read and changed on this scope, which runTest runs and then cancels.
            scope = this,
            applicationScope = this,
        )

    private fun storageWith(shortcut: TopSite?): PinnedSiteStorage = mockk {
        coEvery { getPinnedSites() } returns listOfNotNull(shortcut)
    }

    private fun browserStoreWithSelectedTab() =
        BrowserStore(
            BrowserState(
                tabs = listOf(createTab(url = TEST_URL, title = TEST_TITLE, id = TAB_ID)),
                selectedTabId = TAB_ID,
            )
        )

    private companion object {
        const val TEST_URL = "https://mozilla.org"
        const val TEST_TITLE = "Mozilla"
        const val TAB_ID = "tab1"

        val shortcut = TopSite.Pinned(id = 1, title = TEST_TITLE, url = TEST_URL, createdAt = 0)

        val addShortcutItem =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_add_to_shortcuts)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_pin_24),
                onClickEvent = MenuAction.AddShortcut,
            )

        val removeShortcutItem =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_remove_from_shortcuts)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_pin_fill_24),
                onClickEvent = MenuAction.RemoveShortcut,
                state = MenuItemState.ACTIVE,
            )
    }
}
