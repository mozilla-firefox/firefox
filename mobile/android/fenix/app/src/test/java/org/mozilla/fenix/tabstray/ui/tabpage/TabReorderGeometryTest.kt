/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabpage

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class TabReorderGeometryTest {

    @Test
    fun `GIVEN a single column layout in Ltr Mode WHEN reorder directions are requested THEN only UP and DOWN are offered`() {
        val geometry = TabReorderGeometry(tabCount = 3, columns = 1)

        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.DOWN),
            actual = geometry.reorderDirections(tabIndex = 1, isRtl = false),
        )
    }

    @Test
    fun `GIVEN a single column layout in Rtl Mode WHEN reorder directions are requested THEN only UP and DOWN are offered`() {
        val geometry = TabReorderGeometry(tabCount = 3, columns = 1)

        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.DOWN),
            actual = geometry.reorderDirections(tabIndex = 1, isRtl = true),
        )
    }

    @Test
    fun `GIVEN a single item in a grid in Ltr WHEN reorder directions are requested THEN no directions are offered`() {
        val grid = TabReorderGeometry(tabCount = 1, columns = 3)

        assertEquals(expected = 0, actual = grid.reorderDirections(tabIndex = 0, isRtl = false).size)
    }

    @Test
    fun `GIVEN a single item in a grid in Rtl WHEN reorder directions are requested THEN no directions are offered`() {
        val grid = TabReorderGeometry(tabCount = 1, columns = 3)

        assertEquals(expected = 0, actual = grid.reorderDirections(tabIndex = 0, isRtl = true).size)
    }

    @Test
    fun `GIVEN a single item in a list in Ltr mode WHEN reorder directions are requested THEN no directions are offered`() {
        val list = TabReorderGeometry(tabCount = 1, columns = 1)

        assertEquals(expected = 0, actual = list.reorderDirections(tabIndex = 0, isRtl = false).size)
    }

    @Test
    fun `GIVEN a single item in a list in Rtl mode WHEN reorder directions are requested THEN no directions are offered`() {
        val list = TabReorderGeometry(tabCount = 1, columns = 1)

        assertEquals(expected = 0, actual = list.reorderDirections(tabIndex = 0, isRtl = true).size)
    }

    @Test
    fun `GIVEN a grid in Ltr mode WHEN an item is in the first row THEN up is not offered`() {
        val geometry = TabReorderGeometry(tabCount = 6, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.DOWN, ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 0, isRtl = false),
        )
        assertEquals(
            expected = listOf(ReorderDirection.DOWN, ReorderDirection.LEFT, ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 1, isRtl = false),
        )
        assertEquals(
            expected = listOf(ReorderDirection.DOWN, ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 2, isRtl = false),
        )
    }

    @Test
    fun `GIVEN a grid in Rtl mode WHEN an item is in the first row THEN up is not offered`() {
        val geometry = TabReorderGeometry(tabCount = 6, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.DOWN, ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 0, isRtl = true),
        )
        assertEquals(
            expected = listOf(ReorderDirection.DOWN, ReorderDirection.RIGHT, ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 1, isRtl = true),
        )
        assertEquals(
            expected = listOf(ReorderDirection.DOWN, ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 2, isRtl = true),
        )
    }

    @Test
    fun `GIVEN a grid in Ltr mode WHEN an item is in the last row THEN down is not offered`() {
        val geometry = TabReorderGeometry(tabCount = 6, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 3, isRtl = false),
        )
        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.LEFT, ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 4, isRtl = false),
        )
        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 5, isRtl = false),
        )
    }

    @Test
    fun `GIVEN a grid in Rtl mode WHEN an item is in the last row THEN down is not offered`() {
        val geometry = TabReorderGeometry(tabCount = 6, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 3, isRtl = true),
        )
        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.RIGHT, ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 4, isRtl = true),
        )
        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 5, isRtl = true),
        )
    }

    @Test
    fun `GIVEN a grid in Ltr mode WHEN an item in the first item in a row, RIGHT is offered`() {
        val geometry = TabReorderGeometry(tabCount = 3, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 0, isRtl = false),
        )
    }

    @Test
    fun `GIVEN a grid in Rtl mode WHEN an item in the first item in a row, LEFT is offered`() {
        val geometry = TabReorderGeometry(tabCount = 3, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 0, isRtl = true),
        )
    }

    @Test
    fun `GIVEN a grid in Ltr mode WHEN an item in the last item in a row, LEFT is offered`() {
        val geometry = TabReorderGeometry(tabCount = 3, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 2, isRtl = false),
        )
    }

    @Test
    fun `GIVEN a grid in Rtl mode WHEN an item in the last item in a row, RIGHT is offered`() {
        val geometry = TabReorderGeometry(tabCount = 3, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 2, isRtl = true),
        )
    }

    @Test
    fun `GIVEN a grid in Ltr mode WHEN an item in the middle of a row, LEFT and RIGHT are offered`() {
        val geometry = TabReorderGeometry(tabCount = 3, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.LEFT, ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 1, isRtl = false),
        )
    }

    @Test
    fun `GIVEN a grid in Rtl mode WHEN an item in the middle of a row, RIGHT and LEFT are offered`() {
        val geometry = TabReorderGeometry(tabCount = 3, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.RIGHT, ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 1, isRtl = true),
        )
    }

    @Test
    fun `GIVEN a grid in Ltr mode WHEN an item is the last of a partial row THEN up and left are offered, but not right`() {
        val geometry = TabReorderGeometry(tabCount = 5, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.LEFT),
            actual = geometry.reorderDirections(tabIndex = 4, isRtl = false),
        )
    }

    @Test
    fun `GIVEN a grid in Rtl mode WHEN an item is the last of a partial row THEN up and right are offered, but not left`() {
        val geometry = TabReorderGeometry(tabCount = 5, columns = 3)

        assertEquals(
            expected = listOf(ReorderDirection.UP, ReorderDirection.RIGHT),
            actual = geometry.reorderDirections(tabIndex = 4, isRtl = true),
        )
    }

    @Test
    fun `GIVEN a grid in Ltr WHEN an item is surrounded THEN all directions are offered`() {
        val geometry = TabReorderGeometry(tabCount = 9, columns = 3)

        assertEquals(
            expected =
                listOf(
                    ReorderDirection.UP,
                    ReorderDirection.DOWN,
                    ReorderDirection.LEFT,
                    ReorderDirection.RIGHT,
                ),
            actual = geometry.reorderDirections(tabIndex = 4, isRtl = false),
        )
    }

    @Test
    fun `GIVEN a grid in Rtl WHEN an item is surrounded THEN all directions are offered`() {
        val geometry = TabReorderGeometry(tabCount = 9, columns = 3)

        assertEquals(
            expected =
                listOf(
                    ReorderDirection.UP,
                    ReorderDirection.DOWN,
                    ReorderDirection.RIGHT,
                    ReorderDirection.LEFT,
                ),
            actual = geometry.reorderDirections(tabIndex = 4, isRtl = true),
        )
    }

    @Test
    fun `GIVEN a list WHEN a UP or DOWN is requested THEN the tab moves by a single position`() {
        val geometry = TabReorderGeometry(tabCount = 5, columns = 1)

        assertEquals(expected = -1, actual = geometry.reorderOffset(ReorderDirection.UP, isRtl = false))
        assertEquals(expected = 1, actual = geometry.reorderOffset(ReorderDirection.DOWN, isRtl = false))
    }

    @Test
    fun `GIVEN a grid WHEN a UP or DOWN is requested THEN the tab moves by a full row`() {
        val columnCount = 3
        val geometry = TabReorderGeometry(tabCount = 9, columns = columnCount)

        assertEquals(expected = -columnCount, actual = geometry.reorderOffset(ReorderDirection.UP, isRtl = false))
        assertEquals(expected = columnCount, actual = geometry.reorderOffset(ReorderDirection.DOWN, isRtl = false))
    }

    @Test
    fun `GIVEN a grid WHEN RIGHT or LEFT is requested then the tab moves by a single position`() {
        val geometry = TabReorderGeometry(tabCount = 9, columns = 3)

        assertEquals(expected = -1, actual = geometry.reorderOffset(ReorderDirection.LEFT, isRtl = false))
        assertEquals(expected = 1, actual = geometry.reorderOffset(ReorderDirection.RIGHT, isRtl = false))
    }

    @Test
    fun `GIVEN a right to left layout WHEN a RIGHT or LEFT is requested THEN the offset is mirrored`() {
        val geometry = TabReorderGeometry(tabCount = 9, columns = 3)

        assertEquals(expected = 1, actual = geometry.reorderOffset(ReorderDirection.LEFT, isRtl = true))
        assertEquals(expected = -1, actual = geometry.reorderOffset(ReorderDirection.RIGHT, isRtl = true))
    }

    @Test
    fun `GIVEN a right to left layout, the UP or DOWN offset is the same as left to right`() {
        val geometry = TabReorderGeometry(tabCount = 9, columns = 3)

        assertEquals(
            expected = geometry.reorderOffset(ReorderDirection.UP, isRtl = false),
            actual = geometry.reorderOffset(ReorderDirection.UP, isRtl = true),
        )
        assertEquals(
            expected = geometry.reorderOffset(ReorderDirection.DOWN, isRtl = false),
            actual = geometry.reorderOffset(ReorderDirection.DOWN, isRtl = true),
        )
    }

    @Test
    fun `GIVEN any layout WHEN a direction is offered THEN its offset lands on a real tab`() {
        for (tabCount in 1..10) {
            for (columns in 1..4) {
                val geometry = TabReorderGeometry(tabCount = tabCount, columns = columns)

                for (tabIndex in 0 until tabCount) {
                    for (isRtl in listOf(false, true)) {
                        geometry.reorderDirections(tabIndex, isRtl).forEach { direction ->
                            val target = tabIndex + geometry.reorderOffset(direction, isRtl)

                            assertTrue(
                                actual = target in 0 until tabCount,
                                message =
                                    "$direction offered at index $tabIndex of $tabCount " +
                                        "tabs in $columns columns (isRtl=$isRtl) targets $target",
                            )
                        }
                    }
                }
            }
        }
    }
}
