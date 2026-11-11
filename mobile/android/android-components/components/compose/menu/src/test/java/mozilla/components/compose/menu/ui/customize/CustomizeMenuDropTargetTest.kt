/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListLayoutInfo
import mozilla.components.compose.draggable.InteractionMode
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.EmptyHiddenArea
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.InPlace
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.NextTo
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CustomizeMenuDropTargetTest {
    // Laid out as: drag bar, a, b, hidden sections title, c. With a or b dragged, taking no space, the hidden area is
    // from 300 to 400.
    private val groups = CustomizeMenuGroups(shown = groups("a", "b"), hidden = groups("c"))
    private val mandatoryGroups = groups.copy(mandatoryIds = setOf("a"))

    // Laid out as: drag bar, a, b, hidden sections title, area to drag groups into. With a or b dragged, taking no
    // space, the hidden area is from 300 to 400.
    private val noHiddenGroups = CustomizeMenuGroups(shown = groups("a", "b"), hidden = emptyList())

    private val aboveHiddenArea = 150f
    private val overHiddenArea = 350f
    private val belowHiddenArea = 450f

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list places it next to a shown group THEN it is dropped there`() {
        assertEquals(
            NextTo("b", placeAfter = true),
            dropTargetOf(found = reorderingNextTo("b", placeAfter = true), draggedCenter = aboveHiddenArea),
        )
    }

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list places it with the hidden groups THEN it is placed as the last visible group`() {
        assertEquals(
            NextTo(HIDDEN_SECTIONS, placeAfter = false),
            dropTargetOf(found = reorderingNextTo("c", placeAfter = false), draggedCenter = aboveHiddenArea),
        )
    }

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list places it in the area to drag groups into THEN it is placed as the last visible group`() {
        assertEquals(
            NextTo(HIDDEN_SECTIONS, placeAfter = false),
            dropTargetOf(
                found = reorderingNextTo(DROP_AREA, placeAfter = false),
                draggedCenter = aboveHiddenArea,
                groups = noHiddenGroups,
            ),
        )
    }

    @Test
    fun `GIVEN a group dragged over the hidden area WHEN the list places it next to a hidden group THEN it is dropped there`() {
        assertEquals(
            NextTo("c", placeAfter = true),
            dropTargetOf(found = reorderingNextTo("c", placeAfter = true), draggedCenter = overHiddenArea),
        )
    }

    @Test
    fun `GIVEN a group dragged over the hidden area WHEN the list places it with the shown groups THEN it is shown at the top of the hidden items`() {
        assertEquals(
            NextTo(HIDDEN_SECTIONS, placeAfter = true),
            dropTargetOf(found = reorderingNextTo(HIDDEN_SECTIONS, placeAfter = false), draggedCenter = overHiddenArea),
        )
    }

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list places it next to where it is THEN it is dropped in place`() {
        assertEquals(
            InPlace,
            dropTargetOf(found = reorderingNextTo("b", placeAfter = false), draggedCenter = aboveHiddenArea),
        )
    }

    @Test
    fun `GIVEN the last shown group dragged above the hidden area WHEN the list places it with the hidden groups THEN it is dropped in place`() {
        assertEquals(
            InPlace,
            dropTargetOf(
                found = reorderingNextTo("c", placeAfter = false, draggedId = "b"),
                draggedCenter = aboveHiddenArea,
                draggedId = "b",
            ),
        )
    }

    @Test
    fun `GIVEN no other hidden groups WHEN a group is dragged over the hidden area THEN it is dropped into the empty hidden area`() {
        assertEquals(
            EmptyHiddenArea,
            dropTargetOf(found = InteractionMode.List.None, draggedCenter = overHiddenArea, groups = noHiddenGroups),
        )
    }

    @Test
    fun `GIVEN no other hidden groups WHEN a group is dragged over the hidden area while the list scrolls THEN it is dropped into the empty hidden area`() {
        assertEquals(
            EmptyHiddenArea,
            dropTargetOf(
                found = InteractionMode.List.Scroll(scroll = 10f),
                draggedCenter = overHiddenArea,
                groups = noHiddenGroups,
            ),
        )
    }

    @Test
    fun `GIVEN the only hidden group is dragged WHEN it is over the hidden area THEN it is dropped into the empty hidden area`() {
        assertEquals(
            EmptyHiddenArea,
            dropTargetOf(
                found = InteractionMode.List.None,
                draggedCenter = 450f,
                draggedId = "c",
                layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_SECTIONS, DROP_AREA, "c", collapsed = "c"),
            ),
        )
    }

    @Test
    fun `GIVEN a mandatory group dragged over the hidden area WHEN the list places it next to a hidden group THEN it is not dropped`() {
        assertNull(
            dropTargetOf(
                found = reorderingNextTo("c", placeAfter = true),
                draggedCenter = overHiddenArea,
                groups = mandatoryGroups,
            )
        )
    }

    @Test
    fun `GIVEN a mandatory group and no other hidden groups WHEN it is dragged over the hidden area THEN it is not dropped`() {
        assertNull(
            dropTargetOf(
                found = InteractionMode.List.None,
                draggedCenter = overHiddenArea,
                groups = noHiddenGroups.copy(mandatoryIds = setOf("a")),
            )
        )
    }

    @Test
    fun `GIVEN a mandatory group dragged above the hidden area WHEN the list places it with the hidden groups THEN it is shown as the last visible group`() {
        assertEquals(
            NextTo(HIDDEN_SECTIONS, placeAfter = false),
            dropTargetOf(
                found = reorderingNextTo("c", placeAfter = false),
                draggedCenter = aboveHiddenArea,
                groups = mandatoryGroups,
            ),
        )
    }

    @Test
    fun `GIVEN a group dragged below the hidden area THEN it is not dropped`() {
        assertNull(dropTargetOf(found = reorderingNextTo("c", placeAfter = true), draggedCenter = belowHiddenArea))
    }

    @Test
    fun `GIVEN a reset button WHEN a group is dragged over it THEN it is not dropped`() {
        assertNull(
            dropTargetOf(
                found = reorderingNextTo("c", placeAfter = true),
                draggedCenter = belowHiddenArea,
                layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_SECTIONS, "c", RESET_BUTTON, collapsed = "a"),
            )
        )
    }

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list does not place it THEN it is not dropped`() {
        assertNull(dropTargetOf(found = InteractionMode.List.None, draggedCenter = aboveHiddenArea))
    }

    @Test
    fun `GIVEN the hidden sections title below what is shown of the list WHEN a group is dragged THEN it is dropped where the list places it`() {
        assertEquals(
            NextTo("b", placeAfter = true),
            dropTargetOf(
                found = reorderingNextTo("b", placeAfter = true),
                draggedCenter = 1_000f,
                layout = layoutOf(DRAG_BAR, "a", "b", totalItemsCount = 5, collapsed = "a"),
            ),
        )
    }

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list scrolls THEN it keeps scrolling`() {
        val scroll = InteractionMode.List.Scroll(scroll = 10f)

        assertEquals(
            scroll,
            interactionModeOf(dragged("a", aboveHiddenArea), scroll, layoutOf(groups, collapsed = "a"), groups),
        )
    }

    @Test
    fun `GIVEN the list still lays out the dragged group taking space WHEN it places it THEN it is not dropped`() {
        assertNull(
            dropTargetOf(
                found = reorderingNextTo("b", placeAfter = false),
                draggedCenter = aboveHiddenArea,
                layout = layoutOf(groups),
            )
        )
    }

    @Test
    fun `GIVEN the dragged group is not shown in the list WHEN the list places it THEN it is dropped there`() {
        assertEquals(
            NextTo("b", placeAfter = true),
            dropTargetOf(
                found = reorderingNextTo("b", placeAfter = true),
                draggedCenter = 50f,
                layout = layoutOf("b", HIDDEN_SECTIONS, "c", firstIndex = 2, totalItemsCount = 5),
            ),
        )
    }

    @Test
    fun `WHEN a group is dropped into the empty hidden area THEN it is placed as the first hidden item`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("b"), hidden = groups("a", "c")),
            groups.drop("a", EmptyHiddenArea),
        )
    }

    @Test
    fun `WHEN a group is dropped next to another group THEN it is moved next to it`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("b", "a"), hidden = groups("c")),
            groups.drop("a", NextTo("b", placeAfter = true)),
        )
    }

    @Test
    fun `WHEN a group is dropped in place THEN no group is moved`() {
        assertEquals(groups, groups.drop("a", InPlace))
    }

    /**
     * Where the group with [draggedId], with its center at [draggedCenter] in [layout], is dropped when the list found
     * [found] for it.
     */
    private fun dropTargetOf(
        found: InteractionMode.List,
        draggedCenter: Float,
        groups: CustomizeMenuGroups = this.groups,
        draggedId: String = "a",
        layout: LazyListLayoutInfo = layoutOf(groups, collapsed = draggedId),
    ) = dropTargetOf(draggedId, interactionModeOf(dragged(draggedId, draggedCenter), found, layout, groups), groups)

    private fun reorderingNextTo(key: Any, placeAfter: Boolean, draggedId: String = "a") =
        InteractionMode.List.Reordering(
            source = InteractionState.List.Active(index = 0, key = draggedId, initialOffset = 0f, size = 0),
            target = InteractionState.List.Active(index = 1, key = key, initialOffset = 0f, size = 0),
            placeAfter = placeAfter,
        )

    /**
     * Layout of the list showing [groups], with the area to drag groups into if none is hidden, and the group with the
     * [collapsed] id taking no space, as it does while dragged.
     */
    private fun layoutOf(groups: CustomizeMenuGroups, collapsed: Any? = null): LazyListLayoutInfo {
        val dropArea = if (groups.hidden.isEmpty()) listOf(DROP_AREA) else emptyList()
        val shown = listOf<Any>(DRAG_BAR) + groups.shown.map { it.id }
        val keys = shown + HIDDEN_SECTIONS + dropArea + groups.hidden.map { it.id }
        return layoutOf(*keys.toTypedArray(), collapsed = collapsed)
    }

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }
}
