/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.EmptyHiddenArea
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.InPlace
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.NextTo
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import org.junit.Assert.assertEquals
import org.junit.Test

class HiddenAreaSpaceTest {
    // Laid out as: drag bar, a, b, hidden sections title, c.
    private val groups = CustomizeMenuGroups(shown = groups("a", "b"), hidden = groups("c"))

    @Test
    fun `GIVEN no shown group is dragged THEN no space is kept`() {
        assertEquals(0, fillerOf(dragged = null, dropTarget = NextTo("b", placeAfter = true)))
    }

    @Test
    fun `GIVEN a shown group is dragged before it is dropped anywhere THEN its size is kept`() {
        assertEquals(100, fillerOf(dropTarget = null))
    }

    @Test
    fun `GIVEN a shown group is dragged WHEN the line is next to a shown group THEN its size less the line's space is kept`() {
        assertEquals(84, fillerOf(dropTarget = NextTo("b", placeAfter = true)))
    }

    @Test
    fun `GIVEN a shown group is dragged WHEN the line is before a shown group THEN its size less the line's space is kept`() {
        assertEquals(84, fillerOf(dropTarget = NextTo("b", placeAfter = false)))
    }

    @Test
    fun `GIVEN a shown group is dragged WHEN the line is last among the shown groups THEN its size less the line's space is kept`() {
        assertEquals(84, fillerOf(dropTarget = NextTo(HIDDEN_TITLE, placeAfter = false)))
    }

    @Test
    fun `GIVEN a shown group is dragged WHEN it is dropped back where it was THEN its size less the line's space is kept`() {
        assertEquals(84, fillerOf(dropTarget = InPlace))
    }

    @Test
    fun `GIVEN a shown group is dragged WHEN the line is first among the hidden groups THEN its size is kept`() {
        assertEquals(100, fillerOf(dropTarget = NextTo(HIDDEN_TITLE, placeAfter = true)))
    }

    @Test
    fun `GIVEN a shown group is dragged WHEN the line is next to a hidden group THEN its size is kept`() {
        assertEquals(100, fillerOf(dropTarget = NextTo("c", placeAfter = true)))
    }

    @Test
    fun `GIVEN a shown group is dragged WHEN it is dropped into the empty hidden area THEN its size is kept`() {
        assertEquals(100, fillerOf(dropTarget = EmptyHiddenArea))
    }

    @Test
    fun `GIVEN a shown group smaller than the line's space is dragged WHEN the line is among the shown groups THEN no space is kept`() {
        assertEquals(
            0,
            fillerOf(dragged = DraggedShownItem(index = 1, size = 10), dropTarget = NextTo("b", placeAfter = true)),
        )
    }

    @Test
    fun `GIVEN a shown group is dragged from the first item the list shows WHEN it is dropped back where it was THEN its size less the line's space is kept`() {
        assertEquals(84, fillerOf(dropTarget = InPlace, firstVisibleIndex = 1))
    }

    @Test
    fun `GIVEN a shown group is dragged from above the items the list shows WHEN it is dropped back where it was THEN its size is kept`() {
        assertEquals(100, fillerOf(dropTarget = InPlace, firstVisibleIndex = 2))
    }

    private fun fillerOf(
        dragged: DraggedShownItem? = DraggedShownItem(index = 1, size = 100),
        dropTarget: CustomizeMenuDropTarget?,
        firstVisibleIndex: Int = 0,
    ) = hiddenAreaFillerOf(dragged, lineSpace = 16, dropTarget, groups, firstVisibleIndex = { firstVisibleIndex })

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }
}
