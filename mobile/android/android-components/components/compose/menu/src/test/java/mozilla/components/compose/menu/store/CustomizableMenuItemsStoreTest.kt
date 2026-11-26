/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ExpansionToggled
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Update
import mozilla.components.lib.state.Middleware
import org.junit.Assert.assertEquals
import org.junit.Test

class CustomizableMenuItemsStoreTest {
    @Test
    fun `WHEN the store is created THEN tell the middleware that the screen was initialized`() {
        val actions = mutableListOf<CustomizableMenuItemsAction>()
        val captureActions: Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction> = { _, next, action ->
            actions.add(action)
            next(action)
        }

        CustomizableMenuItemsStore(middleware = listOf(captureActions))

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN the items are shown for the first time THEN the shown expandable items with items inside are expanded and the others and the hidden ones are collapsed`() {
        val store = CustomizableMenuItemsStore()

        store.dispatch(
            Update(
                items = listOf(item("a"), expandable("more", item("b")), expandable("other")),
                hiddenItems = listOf(expandable("hidden more", item("c")), item("d")),
            )
        )

        assertEquals(setOf("more"), store.state.expandedItemIds)
        assertEquals(listOf("a", "more", "other"), store.state.items.map { it.id })
        assertEquals(listOf("hidden more", "d"), store.state.hiddenItems.map { it.id })
    }

    @Test
    fun `WHEN the items are shown for the first time and an expandable item has only items the app does not show THEN it is collapsed`() {
        val store = CustomizableMenuItemsStore()

        store.dispatch(
            Update(
                items = listOf(expandable("more", CustomizableMenuItem(id = "b", item = null))),
                hiddenItems = emptyList(),
            )
        )

        assertEquals(emptySet<String>(), store.state.expandedItemIds)
    }

    @Test
    fun `WHEN the last item is taken out of an expanded shown expandable item THEN it is collapsed`() {
        val store = CustomizableMenuItemsStore()
        store.dispatch(Update(items = listOf(item("a"), expandable("more", item("b"))), hiddenItems = emptyList()))

        store.dispatch(
            ItemsChanged(items = listOf(item("a"), expandable("more"), item("b")), hiddenItems = emptyList())
        )

        assertEquals(emptySet<String>(), store.state.expandedItemIds)
    }

    @Test
    fun `WHEN the last item is taken out of an expanded hidden expandable item THEN it is collapsed`() {
        val store = CustomizableMenuItemsStore()
        store.dispatch(Update(items = listOf(item("a")), hiddenItems = listOf(expandable("hidden", item("b")))))
        store.dispatch(ExpansionToggled("hidden"))

        store.dispatch(ItemsChanged(items = listOf(item("a"), item("b")), hiddenItems = listOf(expandable("hidden"))))

        assertEquals(emptySet<String>(), store.state.expandedItemIds)
    }

    @Test
    fun `GIVEN an empty expandable item expanded by the user WHEN other items are moved THEN it stays expanded`() {
        val store = CustomizableMenuItemsStore()
        store.dispatch(Update(items = listOf(item("a"), item("b"), expandable("more")), hiddenItems = emptyList()))
        store.dispatch(ExpansionToggled("more"))

        store.dispatch(
            ItemsChanged(items = listOf(item("b"), item("a"), expandable("more")), hiddenItems = emptyList())
        )

        assertEquals(setOf("more"), store.state.expandedItemIds)
    }

    @Test
    fun `GIVEN the items were shown WHEN they are shown again THEN keep the expanded items`() {
        val store = CustomizableMenuItemsStore()
        val update = Update(items = listOf(expandable("more", item("a"))), hiddenItems = listOf(expandable("hidden")))
        store.dispatch(update)
        store.dispatch(ExpansionToggled("more"))
        store.dispatch(ExpansionToggled("hidden"))

        store.dispatch(update)

        assertEquals(setOf("hidden"), store.state.expandedItemIds)
    }

    @Test
    fun `WHEN the user expands and collapses an item THEN it is added to and removed from the expanded items`() {
        val store = CustomizableMenuItemsStore()

        store.dispatch(ExpansionToggled("more"))
        assertEquals(setOf("more"), store.state.expandedItemIds)

        store.dispatch(ExpansionToggled("other"))
        assertEquals(setOf("more", "other"), store.state.expandedItemIds)

        store.dispatch(ExpansionToggled("more"))
        assertEquals(setOf("other"), store.state.expandedItemIds)
    }

    @Test
    fun `WHEN items are moved THEN the shown and hidden items are the moved ones and the expanded items are kept`() {
        val store = CustomizableMenuItemsStore()
        store.dispatch(
            Update(items = listOf(item("a"), expandable("more", item("b"))), hiddenItems = listOf(item("c")))
        )

        store.dispatch(
            ItemsChanged(items = listOf(expandable("more", item("b")), item("a")), hiddenItems = emptyList())
        )

        assertEquals(listOf("more", "a"), store.state.items.map { it.id })
        assertEquals(emptyList<String>(), store.state.hiddenItems.map { it.id })
        assertEquals(setOf("more"), store.state.expandedItemIds)
    }

    private fun item(id: String) =
        CustomizableMenuItem(id = id, item = StandardMenuItem(title = MenuItemTitle(Text.String(id))))

    private fun expandable(id: String, vararg subItems: CustomizableMenuItem) =
        CustomizableMenuItem(
            id = id,
            item =
                ExpandableMenuItem(
                    title = MenuItemTitle(Text.String(id)),
                    onClickEvent = object : MenuEvent {},
                    subMenuItems = emptyList(),
                ),
            subItems = subItems.toList(),
        )
}
