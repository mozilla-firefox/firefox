/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.draggable.InteractionMode
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.EmptyHiddenArea
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.InPlace
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.NextTo
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS
import mozilla.components.compose.menu.ui.customize.DraggedGroupPosition.ABOVE_HIDDEN_AREA
import mozilla.components.compose.menu.ui.customize.DraggedGroupPosition.BELOW_HIDDEN_AREA
import mozilla.components.compose.menu.ui.customize.DraggedGroupPosition.OVER_HIDDEN_AREA
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CustomizeMenuDropTargetTest {
    private val groups = CustomizeMenuGroups(shown = groups("a", "b"), hidden = groups("c"))
    private val mandatoryGroups = groups.copy(mandatoryIds = setOf("a"))

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list places it next to a shown group THEN it is dropped there`() {
        assertEquals(
            NextTo("b", placeAfter = true),
            dropTargetOf(
                "a",
                reorderingNextTo("b", placeAfter = true),
                ABOVE_HIDDEN_AREA,
                groups,
                hasHiddenGroups = true,
            ),
        )
    }

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list places it with the hidden groups THEN it is placed as the last visible group`() {
        assertEquals(
            NextTo(HIDDEN_SECTIONS, placeAfter = false),
            dropTargetOf(
                "a",
                reorderingNextTo("c", placeAfter = false),
                ABOVE_HIDDEN_AREA,
                groups,
                hasHiddenGroups = true,
            ),
        )
    }

    @Test
    fun `GIVEN a group dragged over the hidden area WHEN the list places it next to a hidden group THEN it is dropped there`() {
        assertEquals(
            NextTo("c", placeAfter = true),
            dropTargetOf(
                "a",
                reorderingNextTo("c", placeAfter = true),
                OVER_HIDDEN_AREA,
                groups,
                hasHiddenGroups = true,
            ),
        )
    }

    @Test
    fun `GIVEN a group dragged over the hidden area WHEN the list places it with the shown groups THEN it is shown at the top of the hidden items`() {
        assertEquals(
            NextTo(HIDDEN_SECTIONS, placeAfter = true),
            dropTargetOf(
                "a",
                reorderingNextTo(HIDDEN_SECTIONS, placeAfter = false),
                OVER_HIDDEN_AREA,
                groups,
                hasHiddenGroups = true,
            ),
        )
    }

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list places it next to where it is THEN it is dropped in place`() {
        assertEquals(
            InPlace,
            dropTargetOf(
                "a",
                reorderingNextTo("b", placeAfter = false),
                ABOVE_HIDDEN_AREA,
                groups,
                hasHiddenGroups = true,
            ),
        )
    }

    @Test
    fun `GIVEN the last shown group dragged above the hidden area WHEN the list places it with the hidden groups THEN it is dropped in place`() {
        assertEquals(
            InPlace,
            dropTargetOf(
                "b",
                reorderingNextTo("c", placeAfter = false),
                ABOVE_HIDDEN_AREA,
                groups,
                hasHiddenGroups = true,
            ),
        )
    }

    @Test
    fun `GIVEN no other hidden groups WHEN a group is dragged over the hidden area THEN it is dropped into the empty hidden area`() {
        assertEquals(EmptyHiddenArea, dropTargetOf("a", null, OVER_HIDDEN_AREA, groups, hasHiddenGroups = false))
    }

    @Test
    fun `GIVEN a mandatory group dragged over the hidden area WHEN the list places it next to a hidden group THEN it is not dropped`() {
        assertNull(
            dropTargetOf(
                "a",
                reorderingNextTo("c", placeAfter = true),
                OVER_HIDDEN_AREA,
                mandatoryGroups,
                hasHiddenGroups = true,
            )
        )
    }

    @Test
    fun `GIVEN a mandatory group and no other hidden groups WHEN it is dragged over the hidden area THEN it is not dropped`() {
        assertNull(dropTargetOf("a", null, OVER_HIDDEN_AREA, mandatoryGroups, hasHiddenGroups = false))
    }

    @Test
    fun `GIVEN a mandatory group dragged above the hidden area WHEN the list places it with the hidden groups THEN it is shown as the last visible group`() {
        assertEquals(
            NextTo(HIDDEN_SECTIONS, placeAfter = false),
            dropTargetOf(
                "a",
                reorderingNextTo("c", placeAfter = false),
                ABOVE_HIDDEN_AREA,
                mandatoryGroups,
                hasHiddenGroups = true,
            ),
        )
    }

    @Test
    fun `GIVEN a group dragged below the hidden area THEN it is not dropped`() {
        assertNull(
            dropTargetOf(
                "a",
                reorderingNextTo("c", placeAfter = true),
                BELOW_HIDDEN_AREA,
                groups,
                hasHiddenGroups = true,
            )
        )
    }

    @Test
    fun `GIVEN a group dragged above the hidden area WHEN the list does not place it THEN it is not dropped`() {
        assertNull(dropTargetOf("a", null, ABOVE_HIDDEN_AREA, groups, hasHiddenGroups = true))
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

    private fun reorderingNextTo(key: Any, placeAfter: Boolean) =
        InteractionMode.List.Reordering(
            source = InteractionState.List.Active(index = 0, key = "a", initialOffset = 0f, size = 0),
            target = InteractionState.List.Active(index = 1, key = key, initialOffset = 0f, size = 0),
            placeAfter = placeAfter,
        )

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }
}
