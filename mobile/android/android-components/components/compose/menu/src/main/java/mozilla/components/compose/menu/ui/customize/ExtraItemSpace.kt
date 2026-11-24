/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListItemInfo
import androidx.compose.foundation.lazy.LazyListLayoutInfo

/**
 * Space the items of the customize menu screen take beyond their content, such as for the drop line, as last measured.
 * Space taken away from an item counts as negative. Used to pick where the dragged group is dropped as if this space
 * was not there. Not snapshot state, as it is written while measuring.
 */
internal class ExtraItemSpace {
    private class Measured(val extra: Int, val size: Int)

    // For each item, what was measured of each of its parts, the nodes it is made of.
    private val measured = mutableMapOf<Any, MutableMap<Int, Measured>>()

    /**
     * Record that [part] of the item with [key] was measured [size] pixels tall, [extra] of them beyond its content.
     * [extra] is negative for space taken away from the item.
     */
    fun record(key: Any, extra: Int, size: Int, part: Int = 0) {
        measured.getOrPut(key) { mutableMapOf() }[part] = Measured(extra = extra, size = size)
    }

    /**
     * The extra space [item] takes, or 0 if it is not laid out at the size its parts were measured at. Space added
     * outside of the parts, such as padding applied outside the modifier recording them, makes the sizes differ, so the
     * item is then taken to have no extra space.
     */
    fun of(item: LazyListItemInfo): Int {
        val parts = measured[item.key]?.values ?: return 0
        return if (parts.sumOf { it.size } == item.size) parts.sumOf { it.extra } else 0
    }
}

/** This layout with the [extraSpace] of each visible item removed, and the items after it moved up to match. */
internal fun LazyListLayoutInfo.without(extraSpace: ExtraItemSpace): LazyListLayoutInfo {
    var removed = 0
    var changed = false
    val items = visibleItemsInfo.map { item ->
        val extra = extraSpace.of(item)
        val laidOut =
            if (extra == 0 && removed == 0) {
                item
            } else {
                changed = true
                item.laidOutAt(item.offset - removed, item.size - extra)
            }
        removed += extra
        laidOut
    }
    // Space taken away from an item can make up for extra space before it, moving no item after them.
    if (!changed) return this
    return object : LazyListLayoutInfo by this {
        override val visibleItemsInfo = items
    }
}

private fun LazyListItemInfo.laidOutAt(newOffset: Int, newSize: Int): LazyListItemInfo =
    object : LazyListItemInfo by this {
        override val offset = newOffset
        override val size = newSize
    }
