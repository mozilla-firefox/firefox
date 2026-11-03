/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.store.CustomizableMenuAction.CustomizationStatusChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.lib.state.Middleware
import org.junit.Assert.assertEquals
import org.junit.Test

class CustomizableMenuStoreTest {
    @Test
    fun `WHEN the store is created THEN tell the middleware that the screen was initialized`() {
        val actions = mutableListOf<CustomizableMenuAction>()
        val captureActions: Middleware<CustomizableMenuState, CustomizableMenuAction> = { _, next, action ->
            actions.add(action)
            next(action)
        }

        CustomizableMenuStore(middleware = listOf(captureActions))

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN showing the menu structure THEN show all its groups and whether it is customized`() {
        val store = CustomizableMenuStore()

        store.dispatch(
            Update(
                items = groups("a", "b", "c", "d"),
                mandatoryGroupIds = setOf("c"),
                hiddenGroupIds = setOf("d"),
                isCustomized = true,
            )
        )

        assertEquals(
            CustomizableMenuState(
                menuGroups = groups("a", "b", "c", "d"),
                mandatoryGroupIds = setOf("c"),
                hiddenGroupIds = setOf("d"),
                isCustomized = true,
            ),
            store.state,
        )
    }

    @Test
    fun `WHEN the user changes the menu structure THEN show the new order and the removed groups`() {
        val initialState =
            CustomizableMenuState(
                menuGroups = groups("a", "b", "c", "d"),
                mandatoryGroupIds = setOf("d"),
            )
        val store = CustomizableMenuStore(initialState = initialState)

        store.dispatch(MenuStructureChanged(items = groups("b", "a", "d", "c"), hiddenGroupIds = setOf("c")))

        assertEquals(
            initialState.copy(menuGroups = groups("b", "a", "d", "c"), hiddenGroupIds = setOf("c")),
            store.state,
        )
    }

    @Test
    fun `WHEN the menu starts differing from the default one THEN show it as customized`() {
        val initialState = CustomizableMenuState(menuGroups = groups("a", "b"))
        val store = CustomizableMenuStore(initialState = initialState)

        store.dispatch(CustomizationStatusChanged(isCustomized = true))

        assertEquals(initialState.copy(isCustomized = true), store.state)
    }

    @Test
    fun `WHEN the user interacts with a menu item THEN don't change the state`() {
        val initialState = CustomizableMenuState(menuGroups = groups("a", "b"))
        val store = CustomizableMenuStore(initialState = initialState)

        store.dispatch(TestMenuEvent)

        assertEquals(initialState, store.state)
    }

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }

    private object TestMenuEvent : MenuEvent
}
