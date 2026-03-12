/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

package org.mozilla.fenix.longfox

import org.mozilla.fenix.longfox.Direction.DOWN
import org.mozilla.fenix.longfox.Direction.LEFT
import org.mozilla.fenix.longfox.Direction.RIGHT
import org.mozilla.fenix.longfox.Direction.UP

/**
 * Grid Points represent the game coordinates.
 * Each Grid point is one "square" of the fox.
 * Using this abstraction makes the maths easier and unit testable
 * without having to care about pixels / dp.
 * @param x the grid x coordinate
 * @param y the grid y coordinate
 */
data class GridPoint(val x: Int, val y: Int) {
    fun isAbove(secondPoint: GridPoint): Boolean = y < secondPoint.y
    fun isBelow(secondPoint: GridPoint): Boolean = y > secondPoint.y
    fun isLeftOf(secondPoint: GridPoint): Boolean = x < secondPoint.x
    @Suppress("unused")
    fun isRightOf(secondPoint: GridPoint): Boolean = x > secondPoint.x

    fun directionTo(otherPoint: GridPoint): Direction = when {
        this.isAbove(otherPoint) -> UP
        this.isBelow(otherPoint) -> DOWN
        this.isLeftOf(otherPoint) -> LEFT
        else -> RIGHT
    }
}
