/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListLayoutInfo
import org.junit.Assert.assertEquals
import org.junit.Test

class ExtraItemSpaceTest {
    private val extraSpace = ExtraItemSpace()

    @Test
    fun `GIVEN no item takes extra space THEN the items are laid out as they are`() {
        assertEquals(placements("a" to 0, "b" to 100), placementsWithout(layoutOf("a", "b")))
    }

    @Test
    fun `GIVEN an item laid out taking extra space THEN it is laid out without it and the items after it move up`() {
        extraSpace.record("b", extra = 16, size = ITEM_SIZE)

        assertEquals(
            listOf(Triple("a", 0, 100), Triple("b", 100, 84), Triple("c", 184, 100)),
            placementsWithout(layoutOf("a", "b", "c")),
        )
    }

    @Test
    fun `GIVEN items laid out taking extra space THEN the items after them move up by all of it`() {
        extraSpace.record("a", extra = 16, size = ITEM_SIZE)
        extraSpace.record("b", extra = 10, size = ITEM_SIZE)

        assertEquals(
            listOf(Triple("a", 0, 84), Triple("b", 84, 90), Triple("c", 174, 100)),
            placementsWithout(layoutOf("a", "b", "c")),
        )
    }

    @Test
    fun `GIVEN the line showing where a dragged group is dropped below an item or above the next one THEN the items are laid out without it the same way`() {
        val lineBelowA = ExtraItemSpace().apply { record("a", extra = 16, size = ITEM_SIZE + 16) }
        val lineAboveB = ExtraItemSpace().apply { record("b", extra = 16, size = ITEM_SIZE + 16) }
        val withoutLine = placementsWithout(layoutOf("a", "b", "c"))

        assertEquals(withoutLine, placementsWithout(layoutWithItemTaller("a", by = 16), lineBelowA))
        assertEquals(withoutLine, placementsWithout(layoutWithItemTaller("b", by = 16), lineAboveB))
    }

    @Test
    fun `GIVEN an item made of parts taking extra space THEN it is laid out without the extra space of all of them`() {
        extraSpace.record("b", extra = 16, size = 40, part = 0)
        extraSpace.record("b", extra = 10, size = 60, part = 1)

        assertEquals(
            listOf(Triple("a", 0, 100), Triple("b", 100, 74), Triple("c", 174, 100)),
            placementsWithout(layoutOf("a", "b", "c")),
        )
    }

    @Test
    fun `GIVEN an item made of parts laid out with another size than the sum of its parts THEN it is laid out as it is`() {
        extraSpace.record("b", extra = 16, size = 40, part = 0)
        extraSpace.record("b", extra = 10, size = 50, part = 1)

        assertEquals(placements("a" to 0, "b" to 100, "c" to 200), placementsWithout(layoutOf("a", "b", "c")))
    }

    @Test
    fun `GIVEN an item taking extra space and a later item taking as much less THEN only the items between them move up`() {
        extraSpace.record("a", extra = 16, size = ITEM_SIZE)
        extraSpace.record("c", extra = -16, size = ITEM_SIZE)

        assertEquals(
            listOf(Triple("a", 0, 84), Triple("b", 84, 100), Triple("c", 184, 116), Triple("d", 300, 100)),
            placementsWithout(layoutOf("a", "b", "c", "d")),
        )
    }

    @Test
    fun `GIVEN an item laid out with another size than it was measured taking extra space with THEN it is laid out as it is`() {
        extraSpace.record("b", extra = 16, size = ITEM_SIZE + 16)

        assertEquals(placements("a" to 0, "b" to 100, "c" to 200), placementsWithout(layoutOf("a", "b", "c")))
    }

    @Test
    fun `GIVEN an item measured again without extra space THEN it is laid out as it is`() {
        extraSpace.record("b", extra = 16, size = ITEM_SIZE)
        extraSpace.record("b", extra = 0, size = ITEM_SIZE)

        assertEquals(placements("a" to 0, "b" to 100, "c" to 200), placementsWithout(layoutOf("a", "b", "c")))
    }

    @Test
    fun `GIVEN the item taking extra space is not laid out THEN the items are laid out as they are`() {
        extraSpace.record("z", extra = 16, size = ITEM_SIZE)

        assertEquals(placements("a" to 0, "b" to 100), placementsWithout(layoutOf("a", "b")))
    }

    private fun placementsWithout(layout: LazyListLayoutInfo, extraSpace: ExtraItemSpace = this.extraSpace) =
        layout.without(extraSpace).visibleItemsInfo.map { Triple(it.key, it.offset, it.size) }

    /** Layout of a, b and c with the item with [key] taller by [by]. */
    private fun layoutWithItemTaller(key: String, by: Int) =
        layoutOf("a", "b", "c", collapsed = key, collapsedSize = ITEM_SIZE + by)

    private fun placements(vararg offsets: Pair<String, Int>) = offsets.map { (key, offset) ->
        Triple(key, offset, ITEM_SIZE)
    }
}
