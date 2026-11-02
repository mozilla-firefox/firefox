/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.flow.MutableStateFlow
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.store.MenuEvent
import org.junit.Test
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Settings
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row
import org.mozilla.fenix.components.menu.configuration.MenuSectionConfiguration
import org.mozilla.fenix.components.menu.fake.FakeMenuHost

class MenuItemEventRouterTest {
    private val host = FakeMenuHost()

    @Test
    fun `GIVEN no provider handles an event WHEN routing it THEN say so and don't use the menu`() {
        val registry = createRegistry(findInPage = FakeProvider(handling = OtherEvent))

        assertFalse(createRouter(registry).route(TestEvent))

        assertTrue(registry.fakeProviderOf(FindInPage).events.isEmpty())
        assertFalse(host.isUsed)
    }

    @Test
    fun `GIVEN a provider handles an event WHEN routing it THEN let that provider react to it right away`() {
        val registry = createRegistry(findInPage = FakeProvider(handling = TestEvent, react = { it.dismiss() }))

        assertTrue(createRouter(registry).route(TestEvent))

        assertEquals(listOf<MenuEvent>(TestEvent), registry.fakeProviderOf(FindInPage).events)
        assertTrue(host.isDismissed)
    }

    @Test
    fun `GIVEN providers handling different events WHEN routing one THEN let only its provider react to it`() {
        val registry =
            createRegistry(findInPage = FakeProvider(handling = TestEvent), settings = FakeProvider(OtherEvent))

        createRouter(registry).route(OtherEvent)

        assertTrue(registry.fakeProviderOf(FindInPage).events.isEmpty())
        assertEquals(listOf<MenuEvent>(OtherEvent), registry.fakeProviderOf(Settings).events)
    }

    @Test
    fun `GIVEN more providers handle the same event WHEN routing it THEN let only the first one configured react`() {
        val registry = createRegistry(findInPage = FakeProvider(TestEvent), settings = FakeProvider(TestEvent))

        assertTrue(createRouter(registry).route(TestEvent))

        assertEquals(listOf<MenuEvent>(TestEvent), registry.fakeProviderOf(FindInPage).events)
        assertTrue(registry.fakeProviderOf(Settings).events.isEmpty())
    }

    private fun createRegistry(
        findInPage: FakeProvider = FakeProvider(handling = null),
        settings: FakeProvider = FakeProvider(handling = null),
    ) =
        MenuItemsRegistry(
            configuration =
                listOf(
                    MenuSectionConfiguration(
                        id = "section",
                        presentationMode = Row,
                        items = listOf(FindInPage, Settings),
                    )
                ),
            resolver = { item -> if (item == FindInPage) findInPage else settings },
        )

    private fun createRouter(registry: MenuItemsRegistry) = MenuItemEventRouter(registry = registry, host = host)

    private fun MenuItemsRegistry.fakeProviderOf(item: FenixMenuItem) = get(item) as FakeProvider

    private class FakeProvider(
        private val handling: MenuEvent?,
        private val react: (MenuHost) -> Unit = {},
    ) : MenuItemProvider {
        override val itemsFlow = MutableStateFlow<List<MenuItem>>(emptyList())

        val events = mutableListOf<MenuEvent>()

        override fun handles(event: MenuEvent) = event == handling

        override fun onEvent(event: MenuEvent, menu: MenuHost) {
            events += event
            react(menu)
        }
    }

    private data object TestEvent : MenuEvent

    private data object OtherEvent : MenuEvent
}
