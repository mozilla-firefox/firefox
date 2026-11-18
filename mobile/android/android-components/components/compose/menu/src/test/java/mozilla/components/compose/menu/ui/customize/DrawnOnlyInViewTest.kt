/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DrawnOnlyInViewTest {
    private val layout = layoutOf("MovingOutItem", "DisplayedItem1", "DisplayedItem2", firstIndex = 1)

    @Test
    fun `GIVEN no group is dragged or dropped THEN an item is drawn without reading the list's layout`() {
        assertTrue(
            isDrawnInView(
                key = "DisplayedItem1",
                draggedKey = null,
                droppedKey = null,
                layoutInfo = { error("The layout is read") },
                firstVisibleIndex = { error("The first item shown is read") },
            )
        )
    }

    @Test
    fun `GIVEN a group is dragged THEN an item the list shows is drawn`() {
        assertTrue(
            isDrawn(key = "DisplayedItem1", draggedKey = "OutOfViewItem", droppedKey = null, firstVisibleIndex = 2)
        )
    }

    @Test
    fun `GIVEN a group is dragged THEN an item the list does not show is not drawn`() {
        assertFalse(
            isDrawn(key = "NotDisplayedItem", draggedKey = "OutOfViewItem", droppedKey = null, firstVisibleIndex = 2)
        )
    }

    @Test
    fun `GIVEN a group is dragged out of view THEN it is drawn`() {
        assertTrue(
            isDrawn(key = "OutOfViewItem", draggedKey = "OutOfViewItem", droppedKey = null, firstVisibleIndex = 2)
        )
    }

    @Test
    fun `GIVEN a group let go of moves to its place out of view THEN it is drawn`() {
        assertTrue(
            isDrawn(key = "OutOfViewItem", draggedKey = null, droppedKey = "OutOfViewItem", firstVisibleIndex = 2)
        )
    }

    @Test
    fun `GIVEN a group let go of moves to its place THEN an item the list does not show is not drawn`() {
        assertFalse(
            isDrawn(key = "NotDisplayedItem", draggedKey = null, droppedKey = "OutOfViewItem", firstVisibleIndex = 2)
        )
    }

    @Test
    fun `GIVEN a group let go of moves to its place THEN an item reported as shown before the first one shown is not drawn`() {
        assertFalse(
            isDrawn(key = "MovingOutItem", draggedKey = null, droppedKey = "OutOfViewItem", firstVisibleIndex = 2)
        )
    }

    private fun isDrawn(key: Any, draggedKey: Any?, droppedKey: Any?, firstVisibleIndex: Int) =
        isDrawnInView(
            key = key,
            draggedKey = draggedKey,
            droppedKey = droppedKey,
            layoutInfo = { layout },
            firstVisibleIndex = { firstVisibleIndex },
        )
}
