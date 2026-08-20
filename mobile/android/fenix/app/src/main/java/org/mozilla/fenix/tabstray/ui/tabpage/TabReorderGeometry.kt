/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabpage

/**
 * Which directions a tab can be reordered in, and how far each direction moves it.
 *
 * Positions here are indices into the backing tab list, not accessibility collection positions. The two diverge when
 * the tab group onboarding card is showing: the card occupies collection slots but is absent from the tab list, so it
 * is never counted here.
 *
 * @param tabCount Number of tabs in the backing tab list.
 * @param columns Number of columns in the layout (1 for a list).
 */
internal data class TabReorderGeometry(
    private val tabCount: Int,
    private val columns: Int,
) {
    /**
     * Valid directions to reorder the tab at [tabIndex]. Left and right are never offered in a single column layout.
     *
     * @param tabIndex The tab's index in the backing tab list.
     * @param isRtl Whether the layout direction is right to left, which mirrors left and right.
     */
    fun reorderDirections(tabIndex: Int, isRtl: Boolean): List<ReorderDirection> {
        // The previous tab in the tab order is displayed to the left in Ltr and to the right in
        // Rtl, so which visual direction each neighbour sits in is mirrored.
        val towardPreviousTab = if (isRtl) ReorderDirection.RIGHT else ReorderDirection.LEFT
        val towardNextTab = if (isRtl) ReorderDirection.LEFT else ReorderDirection.RIGHT

        return buildList {
            if (canMoveUp(tabIndex)) {
                add(ReorderDirection.UP)
            }
            if (canMoveDown(tabIndex)) {
                add(ReorderDirection.DOWN)
            }
            // Both in-row checks are always false when there is a single column, so a list
            // layout never offers left or right
            if (canMoveToPreviousTabInRow(tabIndex)) {
                add(towardPreviousTab)
            }
            if (canMoveToNextTabInRow(tabIndex)) {
                add(towardNextTab)
            }
        }
    }

    /**
     * How far [direction] moves a tab within the tab order. E.g. in a grid layout, moving a tab up/down 1 row moves it
     * by a full row of tabs. In a list layout, it is always +1 or -1.
     *
     * @param direction The direction the tab is moved in.
     * @param isRtl Whether the layout direction is right to left, which mirrors left and right.
     */
    fun reorderOffset(direction: ReorderDirection, isRtl: Boolean): Int =
        when (direction) {
            ReorderDirection.UP -> -columns
            ReorderDirection.DOWN -> columns
            ReorderDirection.LEFT -> if (isRtl) 1 else -1
            ReorderDirection.RIGHT -> if (isRtl) -1 else 1
        }

    // Moving down a row requires adjusting the index by the number of columns
    private fun canMoveDown(tabIndex: Int): Boolean = tabIndex + columns < tabCount

    // Moving up a row requires adjusting the index by the number of columns
    private fun canMoveUp(tabIndex: Int): Boolean = tabIndex >= columns

    // A tab can move to the previous tab in its row if it isn't the first tab in that row
    private fun canMoveToPreviousTabInRow(tabIndex: Int): Boolean = tabIndex % columns != 0

    // A tab can move to the next tab in its row if it isn't the last tab in that row and,
    // in the case of a partial row, isn't the last tab overall
    private fun canMoveToNextTabInRow(tabIndex: Int): Boolean {
        val notLastTab = tabIndex < tabCount - 1
        val notLastTabInRow = tabIndex % columns != columns - 1
        return notLastTabInRow && notLastTab
    }
}

/** A direction a tab can be reordered in without dragging it. */
internal enum class ReorderDirection {
    UP,
    DOWN,
    LEFT,
    RIGHT,
}
