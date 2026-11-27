/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedMoveAsSubItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.store.MenuEvent
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

        assertEquals(
            listOf(Init, ItemsChanged(items = items("b", "a"), hiddenItems = items("c"), movedItemId = "b")),
            actions,
        )
    }

    @Test
    fun `WHEN a dragged item is let go where it already is THEN nothing is saved`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onMove(sourceKey = "a", targetKey = "b", placeAfter = false)
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN a dragged item is let go where items are hidden THEN it is saved hidden`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onMove(sourceKey = "a", targetKey = HIDDEN_TITLE, placeAfter = true)
        handler.onDragEnd()

        assertEquals(
            listOf(Init, ItemsChanged(items = items("b"), hiddenItems = items("a", "c"), movedItemId = "a")),
            actions,
        )
    }

    @Test
    fun `WHEN a dragged item is let go in the area to hide items THEN it is saved hidden`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onMove(sourceKey = "a", targetKey = DROP_AREA, placeAfter = true)
        handler.onDragEnd()

        assertEquals(
            listOf(Init, ItemsChanged(items = items("b"), hiddenItems = items("a", "c"), movedItemId = "a")),
            actions,
        )
    }

    @Test
    fun `WHEN the only shown item is let go in the area to hide items THEN nothing is saved`() {
        val last = CustomizeMenuItems(shown = items("a"), hidden = items("c"), expandedIds = emptySet())

        MoveItemsHandler(store = store, items = last).apply {
            onDragStart(sourceKey = "a", preserveSelectMode = false)
            onMove(sourceKey = "a", targetKey = HIDDEN_TITLE, placeAfter = true)
            onDragEnd()
        }

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

    @Test
    fun `WHEN an item which can only be directly in its section is let go inside an expandable item THEN nothing is saved and the user is told once`() {
        val withTopLevelOnly =
            CustomizeMenuItems(
                shown = listOf(topLevelOnly, expandable),
                hidden = emptyList(),
                expandedIds = setOf("more"),
            )

        MoveItemsHandler(store = store, items = withTopLevelOnly).apply {
            onDragStart(sourceKey = "top level only", preserveSelectMode = false)
            onMove(sourceKey = "top level only", targetKey = "sub", placeAfter = false)
            onDragEnd()
        }

        assertEquals(listOf(Init, AttemptedMoveAsSubItem("top level only")), actions)
    }

    @Test
    fun `WHEN an item which can only be directly in its section is let go inside a hidden expandable item THEN nothing is saved and the user is told`() {
        val withTopLevelOnly =
            CustomizeMenuItems(
                shown = listOf(topLevelOnly),
                hidden = listOf(expandable),
                expandedIds = setOf("more"),
            )

        MoveItemsHandler(store = store, items = withTopLevelOnly).apply {
            onDragStart(sourceKey = "top level only", preserveSelectMode = false)
            onMove(sourceKey = "top level only", targetKey = "sub", placeAfter = false)
            onDragEnd()
        }

        assertEquals(listOf(Init, AttemptedMoveAsSubItem("top level only")), actions)
    }

    @Test
    fun `WHEN an item which can only be directly in its section is let go unmoved after a cancelled drag over an expandable item THEN the user is not told`() {
        val withTopLevelOnly =
            CustomizeMenuItems(
                shown = listOf(topLevelOnly, expandable),
                hidden = emptyList(),
                expandedIds = setOf("more"),
            )

        MoveItemsHandler(store = store, items = withTopLevelOnly).apply {
            onDragStart(sourceKey = "top level only", preserveSelectMode = false)
            onMove(sourceKey = "top level only", targetKey = "sub", placeAfter = false)
            onDragCancel()
            onDragStart(sourceKey = "top level only", preserveSelectMode = false)
            onDragEnd()
        }

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN an item which can only be directly in its section is let go at the top level THEN it is saved and the user is not told`() {
        val withTopLevelOnly =
            CustomizeMenuItems(
                shown = listOf(expandable, topLevelOnly),
                hidden = emptyList(),
                expandedIds = setOf("more"),
            )

        MoveItemsHandler(store = store, items = withTopLevelOnly).apply {
            onDragStart(sourceKey = "top level only", preserveSelectMode = false)
            onMove(sourceKey = "top level only", targetKey = "more", placeAfter = false)
            onDragEnd()
        }

        assertEquals(listOf(Init), actions.filter { it !is ItemsChanged })
        assertEquals("top level only", actions.filterIsInstance<ItemsChanged>().single().movedItemId)
    }

    private val topLevelOnly = items("top level only").single().copy(isTopLevelOnly = true)

    private val expandable =
        CustomizableMenuItem(
            id = "more",
            item =
                ExpandableMenuItem(
                    title = MenuItemTitle(Text.String("more")),
                    onClickEvent = object : MenuEvent {},
                    subMenuItems = emptyList(),
                ),
            subItems = items("sub"),
        )

    private fun items(vararg ids: String) = ids.map {
        CustomizableMenuItem(id = it, item = StandardMenuItem(title = MenuItemTitle(Text.String(it))))
    }
}
