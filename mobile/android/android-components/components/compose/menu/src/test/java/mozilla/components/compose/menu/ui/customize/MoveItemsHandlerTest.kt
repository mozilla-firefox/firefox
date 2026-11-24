/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import mozilla.components.lib.state.Middleware
import org.junit.Assert.assertEquals
import org.junit.Test

class MoveItemsHandlerTest {
    private val items = CustomizeMenuItems(shown = items("a", "b"), hidden = items("c"), expandedIds = emptySet())
    private val actions = mutableListOf<CustomizableMenuItemsAction>()
    private val captureActions: Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction> =
        { _, next, action ->
            actions.add(action)
            next(action)
        }
    private val store =
        CustomizableMenuItemsStore(
            initialState = CustomizableMenuItemsState(items = items.shown, hiddenItems = items.hidden),
            middleware = listOf(captureActions),
        )
    private val handler = MoveItemsHandler(store = store, items = items)

    @Test
    fun `WHEN a dragged item is moved by the list THEN nothing is saved before letting go of it`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onMove(sourceKey = "a", targetKey = "b", placeAfter = true)

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN a dragged item is let go next to another item THEN the new order of the items is saved`() {
        handler.onDragStart(sourceKey = "b", preserveSelectMode = false)
        handler.onMove(sourceKey = "b", targetKey = "a", placeAfter = false)
        handler.onDragEnd()

        assertEquals(listOf(Init, ItemsChanged(items = items("b", "a"), hiddenItems = items("c"))), actions)
    }

    @Test
    fun `WHEN a dragged item is let go where it already is THEN nothing is saved`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onMove(sourceKey = "a", targetKey = "b", placeAfter = false)
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN a dragged item is let go where it cannot be dropped THEN nothing is saved`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onMove(sourceKey = "a", targetKey = HIDDEN_TITLE, placeAfter = true)
        handler.onMove(sourceKey = "a", targetKey = DROP_AREA, placeAfter = true)
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN a dragged item is let go without being moved THEN nothing is saved`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN the drag of an item is cancelled THEN nothing is saved`() {
        handler.onDragStart(sourceKey = "b", preserveSelectMode = false)
        handler.onMove(sourceKey = "b", targetKey = "a", placeAfter = false)
        handler.onDragCancel()
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN a long pressed item is let go before being dragged THEN nothing is saved`() {
        handler.onMove(sourceKey = "b", targetKey = "a", placeAfter = false)
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    private fun items(vararg ids: String) = ids.map {
        CustomizableMenuItem(id = it, item = StandardMenuItem(title = MenuItemTitle(Text.String(it))))
    }
}
