/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.lib.state.Middleware
import org.junit.Assert.assertEquals
import org.junit.Test

class MoveGroupsHandlerTest {
    private val groups = CustomizeMenuGroups(shown = groups("a", "b"), hidden = groups("c"))
    private val actions = mutableListOf<CustomizableMenuAction>()
    private val captureActions: Middleware<CustomizableMenuState, CustomizableMenuAction> = { _, next, action ->
        actions.add(action)
        next(action)
    }
    private val store =
        CustomizableMenuStore(
            initialState =
                CustomizableMenuState(menuGroups = groups.shown + groups.hidden, hiddenGroupIds = setOf("c")),
            middleware = listOf(captureActions),
        )
    private val handler = MoveGroupsHandler(store = store, groups = groups)

    @Test
    fun `WHEN a dragged group is moved by the list THEN nothing is saved before letting go of it`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onMove(sourceKey = "a", targetKey = "b", placeAfter = true)

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN a dragged group is let go next to another group THEN the new structure of the menu is saved`() {
        handler.onDragStart(sourceKey = "b", preserveSelectMode = false)
        handler.onMove(sourceKey = "b", targetKey = "a", placeAfter = false)
        handler.onDragEnd()

        assertEquals(
            listOf(
                Init,
                MenuStructureChanged(items = groups("b", "a", "c"), hiddenGroupIds = setOf("c"), movedGroupId = "b"),
            ),
            actions,
        )
    }

    @Test
    fun `GIVEN no hidden groups WHEN a dragged group is let go into the empty hidden area THEN it is saved as hidden`() {
        val noHiddenGroups = CustomizeMenuGroups(shown = groups("a", "b"), hidden = emptyList())
        val handler = MoveGroupsHandler(store = store, groups = noHiddenGroups)

        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onMove(sourceKey = "a", targetKey = DROP_AREA, placeAfter = true)
        handler.onDragEnd()

        assertEquals(
            listOf(
                Init,
                MenuStructureChanged(items = groups("b", "a"), hiddenGroupIds = setOf("a"), movedGroupId = "a"),
            ),
            actions,
        )
    }

    @Test
    fun `WHEN a dragged group is let go where it already is THEN nothing is saved`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onMove(sourceKey = "a", targetKey = "b", placeAfter = false)
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN a dragged group is let go where it cannot be dropped THEN nothing is saved`() {
        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN a long pressed group is let go before being dragged THEN nothing is saved`() {
        handler.onMove(sourceKey = "b", targetKey = "a", placeAfter = false)
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `WHEN the drag of a group is cancelled THEN nothing is saved`() {
        handler.onDragStart(sourceKey = "b", preserveSelectMode = false)
        handler.onMove(sourceKey = "b", targetKey = "a", placeAfter = false)
        handler.onDragCancel()
        handler.onDragEnd()

        assertEquals(listOf(Init), actions)
    }

    @Test
    fun `GIVEN a group was let go next to another group WHEN the next dragged group is let go where it cannot be dropped THEN nothing more is saved`() {
        handler.onDragStart(sourceKey = "b", preserveSelectMode = false)
        handler.onMove(sourceKey = "b", targetKey = "a", placeAfter = false)
        handler.onDragEnd()

        handler.onDragStart(sourceKey = "a", preserveSelectMode = false)
        handler.onDragEnd()

        assertEquals(
            listOf(
                Init,
                MenuStructureChanged(items = groups("b", "a", "c"), hiddenGroupIds = setOf("c"), movedGroupId = "b"),
            ),
            actions,
        )
    }

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }
}
