/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

package org.mozilla.fenix.longfox

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import org.mozilla.fenix.longfox.Direction.DOWN
import org.mozilla.fenix.longfox.Direction.LEFT
import org.mozilla.fenix.longfox.Direction.RIGHT
import org.mozilla.fenix.longfox.Direction.UP
import kotlin.random.Random

/**
 * This class encapsulates the state of the longfox game
 * 🦊🟧🟧🟧🟧🟧
 *
 * It uses the min width of the container and a fixed cell size to determine the best size
 * for a square game grid.
 * Then it allocates some space for a fox and the food it is planning to eat.
 * It moves the fox around and decides whether it lives or dies.
 *
 * @param size The size of the container for the game.
 * @param fox The list of grid points that make up the fox's body.
 * @param food The grid point where the food is located.
 * @param direction The current direction of the fox's movement.
 * @param isGameOver A boolean indicating if the game is over.
 * @param score The player's current score.
 * @param beepNext This is a flag for the audio state machine
 * @param numCells The number of cells in the grid.
 */
data class GameState(
    val size: Size = Size(0f, 0f),
    val fox: List<GridPoint> = listOf(GridPoint(5, 5), GridPoint(5, 4), GridPoint(5, 3), GridPoint(5, 2)),
    val food: GridPoint = GridPoint(8, 8),
    val direction: Direction = DOWN,
    val isGameOver: Boolean = false,
    val score: Int = 0,
    val beepNext: Boolean = true,
    val numCells: Int = 12,
) {

    val numCellsWide = numCells
    val numCellsTall = numCellsWide
    val cellSize = (size.minDimension / numCellsWide).toInt().toFloat()

    /** this is the direction the fox's shoulders are facing */
    val shouldersDirection: Direction = when {
        fox.size < 3 -> direction
        else -> fox[1].directionTo(fox[2])
    }

    /** this is the direction from the fox to its tail */
    val tailDirection: Direction = when {
        fox.size < 3 -> direction
        else -> fox[fox.size - 2].directionTo(fox[fox.size - 3])
    }

    /**
     * This function moves the fox in its current direction.
     * It determines the new state of the fox after it has moved: is it longer? is it dead?
     * or has it just shifted along a space?
     */
    fun moveFox(): GameState {
        val head = fox.first()
        val newHead = when (direction) {
            UP -> head.copy(y = head.y - 1)
            DOWN -> head.copy(y = head.y + 1)
            LEFT -> head.copy(x = head.x - 1)
            RIGHT -> head.copy(x = head.x + 1)
        }

        val collidedWithSelf = newHead in fox.drop(1)
        val collidedWithEdge = !withinBounds(newHead)
        val collidedWithFood = newHead == food
        val isGameOver = collidedWithSelf || collidedWithEdge

        return if (collidedWithFood && !isGameOver) {
            copy(
                food = randomGridPoint(),
                fox = listOf(newHead) + fox,
                isGameOver = false,
                score = score + 1,
            )
        } else {
            copy(
                fox = listOf(newHead) + fox.dropLast(1),
                isGameOver = isGameOver,
            )
        }
    }

    /**
     * Handles the player's tap input.
     * The tap is processed as a direction orthogonal to the current movement - the fox can't
     * reverse its direction so will always seek to turn 90 degrees.
     * @param offset The tap location on the screen in px
     */
    fun onTap(offset: Offset): GameState {
        val (x, y) = offset
        // the tap offset is in px,
        // so we need to multiply by cellSize to get pixel coordinates from grid position
        val headX = fox.first().x * cellSize
        val headY = fox.first().y * cellSize
        val newDirection = when (direction) {
            UP, DOWN -> if (x < headX) LEFT else RIGHT
            LEFT, RIGHT -> if (y < headY) UP else DOWN
        }
        return copy(direction = newDirection)
    }

    private fun randomGridPoint(): GridPoint = GridPoint(
        Random.nextInt(numCellsWide),
        Random.nextInt(numCellsTall),
    )

    private fun withinBounds(point: GridPoint): Boolean =
        point.x in 0 until numCellsWide && point.y in 0 until numCellsTall

}
