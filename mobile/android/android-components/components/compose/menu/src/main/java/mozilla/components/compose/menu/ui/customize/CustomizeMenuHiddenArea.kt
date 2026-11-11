/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.lazy.LazyListLayoutInfo
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.layout.LayoutCoordinates
import mozilla.components.compose.draggable.InteractionState
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.RESET_BUTTON

/** Where a dragged group is relative to the area holding the hidden groups. */
internal enum class DraggedGroupPosition {
    ABOVE_HIDDEN_AREA,
    OVER_HIDDEN_AREA,

    /** Letting go of the group here puts it back to where it was. */
    BELOW_HIDDEN_AREA,
}

/**
 * Where the area holding the hidden groups is shown in the customize menu screen: below the hidden sections title,
 * until the end of the area to drag groups into, the reset button or the last hidden group. Measured where these are
 * placed, to follow them also while they are animated to where they are moved.
 */
@Stable
internal class CustomizeMenuHiddenArea {
    private var listCoordinates: LayoutCoordinates? = null
    private var titleBottom by mutableFloatStateOf(0f)
    private var dropAreaBottom by mutableFloatStateOf(0f)
    private var resetButtonTop by mutableFloatStateOf(0f)
    private var lastHiddenGroupBottom by mutableFloatStateOf(0f)

    /** Called when the list showing the groups is placed. */
    fun onListPositioned(coordinates: LayoutCoordinates) {
        listCoordinates = coordinates
    }

    /** Called when the hidden sections title is placed. */
    fun onTitlePositioned(coordinates: LayoutCoordinates) {
        coordinates.offsetInList(coordinates.size.height)?.let { titleBottom = it }
    }

    /** Called when the hidden sections title, together with the area to drag groups into if shown, is placed. */
    fun onHiddenSectionsPositioned(coordinates: LayoutCoordinates) {
        coordinates.offsetInList(coordinates.size.height)?.let { dropAreaBottom = it }
    }

    /** Called when the reset button is placed, including the space above it. */
    fun onResetButtonPositioned(coordinates: LayoutCoordinates) {
        coordinates.offsetInList(0)?.let { resetButtonTop = it }
    }

    /** Called when the last hidden group is placed, including the space around it. */
    fun onLastHiddenGroupPositioned(coordinates: LayoutCoordinates) {
        coordinates.offsetInList(coordinates.size.height)?.let { lastHiddenGroupBottom = it }
    }

    /**
     * Where [dragged] is relative to the area holding the hidden groups.
     *
     * @param dragged The dragged group.
     * @param layoutInfo Layout of the list showing the groups.
     * @param hiddenSectionsIndex Index in the list of the hidden sections title.
     * @param showsHiddenGroups Whether hidden groups are shown below the title instead of the area to drag groups into.
     */
    fun positionOf(
        dragged: InteractionState.List.Active,
        layoutInfo: LazyListLayoutInfo,
        hiddenSectionsIndex: Int,
        showsHiddenGroups: Boolean,
    ): DraggedGroupPosition {
        val draggedCenter = dragged.initialOffset + dragged.cumulatedOffset + dragged.size / 2f
        val visibleItems = layoutInfo.visibleItemsInfo
        val isHiddenSectionsTitleInView = visibleItems.any { it.key == HIDDEN_SECTIONS }
        // When not shown the title is below or above everything shown.
        val top =
            when {
                isHiddenSectionsTitleInView -> titleBottom
                (visibleItems.firstOrNull()?.index ?: 0) < hiddenSectionsIndex -> Float.POSITIVE_INFINITY
                else -> Float.NEGATIVE_INFINITY
            }
        val bottom =
            when {
                !showsHiddenGroups -> if (isHiddenSectionsTitleInView) dropAreaBottom else Float.NEGATIVE_INFINITY
                visibleItems.any { it.key == RESET_BUTTON } -> resetButtonTop
                // Without a reset button, the area ends with the last hidden group, if shown.
                visibleItems.lastOrNull()?.index == layoutInfo.totalItemsCount - 1 -> lastHiddenGroupBottom
                else -> Float.POSITIVE_INFINITY
            }

        return when {
            draggedCenter <= top -> DraggedGroupPosition.ABOVE_HIDDEN_AREA
            draggedCenter <= bottom -> DraggedGroupPosition.OVER_HIDDEN_AREA
            else -> DraggedGroupPosition.BELOW_HIDDEN_AREA
        }
    }

    private fun LayoutCoordinates.offsetInList(y: Int): Float? =
        listCoordinates?.takeIf { it.isAttached && isAttached }?.localPositionOf(this, Offset(0f, y.toFloat()))?.y
}
