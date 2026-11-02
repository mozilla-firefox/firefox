/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuAction as MenuStoreAction
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.store.MenuState
import mozilla.components.compose.menu.store.MenuStore
import mozilla.components.lib.state.Middleware
import org.junit.Test
import org.mozilla.fenix.components.menu.MenuBuilder
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemEventRouter
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuItemsRegistry
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeReaderView
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row
import org.mozilla.fenix.components.menu.configuration.MenuSectionConfiguration
import org.mozilla.fenix.components.menu.fake.FakeMenuHost
import org.mozilla.fenix.components.menu.store.MenuAction

class MenuMiddlewareTest {
    private val testDispatcher = StandardTestDispatcher()

    @Test
    fun `WHEN the menu is opened THEN show the configured menu items`() =
        runTest(testDispatcher) {
            val store = createStore()

            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(
                listOf(MenuItemsGroup.Row(id = MENU_GROUP_ID, items = listOf(readerViewItem))),
                store.state.menuGroups,
            )
        }

    @Test
    fun `WHEN one of the menu items changes THEN update the menu`() =
        runTest(testDispatcher) {
            val provider = ReactingMenuItemProvider(reactingTo = null)
            val store = createStore(provider)
            testDispatcher.scheduler.advanceUntilIdle()

            provider.itemsFlow.value = emptyList()
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(emptyList(), store.state.menuGroups)
        }

    @Test
    fun `GIVEN a provider reacts to an event WHEN the event is dispatched THEN let the provider react to it`() {
        val provider = ReactingMenuItemProvider(reactingTo = MenuAction.CustomizeReaderView)
        val store = createStore(provider)

        store.dispatch(MenuAction.CustomizeReaderView)

        assertEquals(listOf<MenuEvent>(MenuAction.CustomizeReaderView), provider.events)
    }

    @Test
    fun `GIVEN no provider reacts to an event WHEN the event is dispatched THEN no provider reacts to it`() {
        val provider = ReactingMenuItemProvider(reactingTo = MenuAction.FindInPage)
        val store = createStore(provider)

        store.dispatch(MenuAction.CustomizeReaderView)

        assertTrue(provider.events.isEmpty())
    }

    @Test
    fun `WHEN an event is dispatched THEN pass it on once whether or not a provider reacted to it`() {
        val passedOn = mutableListOf<MenuStoreAction>()
        val store =
            createStore(
                provider = ReactingMenuItemProvider(reactingTo = MenuAction.CustomizeReaderView),
                downstreamMiddleware =
                    listOf { _, next, action ->
                        passedOn += action
                        next(action)
                    },
            )

        store.dispatch(MenuAction.CustomizeReaderView)
        store.dispatch(MenuAction.FindInPage)

        assertEquals(
            listOf(MenuAction.CustomizeReaderView, MenuAction.FindInPage),
            passedOn.filterIsInstance<MenuEvent>(),
        )
    }

    private fun createStore(
        provider: MenuItemProvider = ReactingMenuItemProvider(reactingTo = null),
        downstreamMiddleware: List<Middleware<MenuState, MenuStoreAction>> = emptyList(),
    ): MenuStore {
        val registry =
            MenuItemsRegistry(
                configuration =
                    listOf(
                        MenuSectionConfiguration(
                            id = MENU_GROUP_ID,
                            presentationMode = Row,
                            items = listOf(CustomizeReaderView),
                        )
                    ),
                resolver = { provider },
            )

        return MenuStore(
            initialState = MenuState(emptyList()),
            middleware =
                listOf(
                    MenuMiddleware(
                        menuBuilder = MenuBuilder(registry),
                        eventRouter = MenuItemEventRouter(registry = registry, host = FakeMenuHost()),
                        scope = CoroutineScope(testDispatcher),
                    )
                ) + downstreamMiddleware,
        )
    }

    private class ReactingMenuItemProvider(private val reactingTo: MenuEvent?) : MenuItemProvider {
        override val itemsFlow = MutableStateFlow<List<MenuItem>>(listOf(readerViewItem))

        val events = mutableListOf<MenuEvent>()

        override fun handles(event: MenuEvent) = event == reactingTo

        override fun onEvent(event: MenuEvent, menu: MenuHost) {
            events += event
        }
    }

    private companion object {
        const val MENU_GROUP_ID = "test"

        val readerViewItem =
            StandardMenuItem(
                title = MenuItemTitle(Text.String("Customize reader view")),
                onClickEvent = MenuAction.CustomizeReaderView,
            )
    }
}
