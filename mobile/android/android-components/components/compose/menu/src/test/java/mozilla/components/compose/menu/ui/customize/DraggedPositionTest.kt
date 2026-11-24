/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListLayoutInfo
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DRAG_BAR
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.DROP_AREA
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON
import mozilla.components.compose.menu.ui.customize.DraggedPosition.ABOVE_HIDDEN_AREA
import mozilla.components.compose.menu.ui.customize.DraggedPosition.BELOW_HIDDEN_AREA
import mozilla.components.compose.menu.ui.customize.DraggedPosition.OVER_HIDDEN_AREA
import org.junit.Assert.assertEquals
import org.junit.Test

class DraggedPositionTest {
    // Laid out as: drag bar, a, b, hidden sections title, then the hidden groups or the area to drag groups into.
    // The hidden area starts at 400.
    private val groups = CustomizeMenuGroups(shown = groups("a", "b"), hidden = groups("c"))
    private val noHiddenGroups = CustomizeMenuGroups(shown = groups("a", "b"), hidden = emptyList())

    @Test
    fun `WHEN a group is dragged above the bottom of the hidden sections title THEN it is above the hidden area`() {
        assertEquals(
            ABOVE_HIDDEN_AREA,
            positionOf(center = 350f, layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, "c", RESET_BUTTON)),
        )
    }

    @Test
    fun `WHEN a group is dragged with its center at the bottom of the hidden sections title THEN it is above the hidden area`() {
        assertEquals(
            ABOVE_HIDDEN_AREA,
            positionOf(center = 400f, layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, "c", RESET_BUTTON)),
        )
    }

    @Test
    fun `WHEN a group is dragged with its center at the top of the reset button THEN it is over the hidden area`() {
        assertEquals(
            OVER_HIDDEN_AREA,
            positionOf(center = 500f, layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, "c", RESET_BUTTON)),
        )
    }

    @Test
    fun `WHEN a group is dragged over the hidden groups THEN it is over the hidden area`() {
        assertEquals(
            OVER_HIDDEN_AREA,
            positionOf(center = 450f, layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, "c", RESET_BUTTON)),
        )
    }

    @Test
    fun `WHEN a group is dragged over the reset button THEN it is below the hidden area`() {
        assertEquals(
            BELOW_HIDDEN_AREA,
            positionOf(center = 550f, layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, "c", RESET_BUTTON)),
        )
    }

    @Test
    fun `GIVEN no reset button WHEN a group is dragged below the last hidden group THEN it is below the hidden area`() {
        assertEquals(
            BELOW_HIDDEN_AREA,
            positionOf(center = 550f, layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, "c")),
        )
    }

    @Test
    fun `GIVEN the last hidden group below what is shown of the list WHEN a group is dragged below the shown hidden groups THEN it is over the hidden area`() {
        assertEquals(
            OVER_HIDDEN_AREA,
            positionOf(
                center = 550f,
                layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, "c", totalItemsCount = 6),
                groups = groups.copy(hidden = groups("c", "d")),
            ),
        )
    }

    @Test
    fun `GIVEN no hidden groups WHEN a group is dragged over the area to drag groups into THEN it is over the hidden area`() {
        assertEquals(
            OVER_HIDDEN_AREA,
            positionOf(
                center = 450f,
                layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, DROP_AREA, RESET_BUTTON),
                groups = noHiddenGroups,
            ),
        )
    }

    @Test
    fun `GIVEN no hidden groups WHEN a group is dragged below the area to drag groups into THEN it is below the hidden area`() {
        assertEquals(
            BELOW_HIDDEN_AREA,
            positionOf(
                center = 550f,
                layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, DROP_AREA, RESET_BUTTON),
                groups = noHiddenGroups,
            ),
        )
    }

    @Test
    fun `GIVEN no hidden groups and the area to drag groups into below what is shown of the list WHEN a group is dragged below the hidden sections title THEN it is over the hidden area`() {
        assertEquals(
            OVER_HIDDEN_AREA,
            positionOf(
                center = 450f,
                layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, totalItemsCount = 5),
                groups = noHiddenGroups,
            ),
        )
    }

    @Test
    fun `GIVEN the only hidden group is dragged WHEN it is below the area to drag groups into but over where it was THEN it is over the hidden area`() {
        assertEquals(
            OVER_HIDDEN_AREA,
            positionOf(
                center = 550f,
                layout = layoutOf(DRAG_BAR, "a", "b", HIDDEN_TITLE, DROP_AREA, "c"),
                draggedId = "c",
            ),
        )
    }

    @Test
    fun `GIVEN the hidden sections title below what is shown of the list WHEN a group is dragged THEN it is above the hidden area`() {
        assertEquals(
            ABOVE_HIDDEN_AREA,
            positionOf(center = 1_000f, layout = layoutOf(DRAG_BAR, "a", "b", totalItemsCount = 6)),
        )
    }

    @Test
    fun `GIVEN the hidden sections title above what is shown of the list WHEN a group is dragged over the hidden groups THEN it is over the hidden area`() {
        assertEquals(
            OVER_HIDDEN_AREA,
            positionOf(
                center = 50f,
                layout = layoutOf("c", "d", firstIndex = 4),
                groups = groups.copy(hidden = groups("c", "d")),
            ),
        )
    }

    @Test
    fun `GIVEN no hidden groups and the hidden sections title above what is shown of the list WHEN a group is dragged over the area to drag groups into THEN it is over the hidden area`() {
        assertEquals(
            OVER_HIDDEN_AREA,
            positionOf(
                center = 50f,
                layout = layoutOf(DROP_AREA, RESET_BUTTON, firstIndex = 4),
                groups = noHiddenGroups,
            ),
        )
    }

    @Test
    fun `GIVEN no hidden groups and the hidden sections title above what is shown of the list WHEN a group is dragged below the area to drag groups into THEN it is below the hidden area`() {
        assertEquals(
            BELOW_HIDDEN_AREA,
            positionOf(
                center = 150f,
                layout = layoutOf(DROP_AREA, RESET_BUTTON, firstIndex = 4),
                groups = noHiddenGroups,
            ),
        )
    }

    @Test
    fun `GIVEN no hidden groups and only the reset button shown of the list WHEN a group is dragged THEN it is below the hidden area`() {
        assertEquals(
            BELOW_HIDDEN_AREA,
            positionOf(center = 50f, layout = layoutOf(RESET_BUTTON, firstIndex = 5), groups = noHiddenGroups),
        )
    }

    /** Where the group with [draggedId], with its center at [center] in [layout], is relative to the hidden area. */
    private fun positionOf(
        center: Float,
        layout: LazyListLayoutInfo,
        groups: CustomizeMenuGroups = this.groups,
        draggedId: String = "a",
    ) = draggedPositionOf(dragged(draggedId, center), layout, groups)

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }
}
