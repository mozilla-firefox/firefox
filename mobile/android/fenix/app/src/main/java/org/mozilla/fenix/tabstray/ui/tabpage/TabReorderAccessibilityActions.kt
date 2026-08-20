/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabpage

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.unit.LayoutDirection
import org.mozilla.fenix.R
import org.mozilla.fenix.tabstray.controller.TabInteractionHandler
import org.mozilla.fenix.tabstray.data.TabsTrayItem

/**
 * Builds the custom accessibility actions that let [item] be reordered without dragging it.
 *
 * The source and target keys are resolved when an action is invoked rather than when it is composed, so a reorder
 * always acts on the current tab order.
 *
 * @param item The tab item the actions are attached to.
 * @param tabs The tab items currently displayed in the tab tray.
 * @param tabIndex The tab item's index within [tabs].
 * @param reorderGeometry Geometry of the tab order [item] belongs to.
 * @param enabled Whether reordering is currently possible. No actions are offered when it is not.
 * @param tabInteractionHandler Handler the reorder is dispatched to.
 */
@Composable
internal fun rememberTabReorderActions(
    item: TabsTrayItem,
    tabs: List<TabsTrayItem>,
    tabIndex: Int,
    reorderGeometry: TabReorderGeometry,
    enabled: Boolean,
    tabInteractionHandler: TabInteractionHandler,
): List<CustomAccessibilityAction> {
    val isRtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    val directions = if (enabled) reorderGeometry.reorderDirections(tabIndex, isRtl) else emptyList()
    val labels = reorderActionLabels()

    val currentTabs by rememberUpdatedState(tabs)
    val currentHandler by rememberUpdatedState(tabInteractionHandler)

    return remember(item.id, directions, reorderGeometry, isRtl, labels) {
        directions.map { direction ->
            val offset = reorderGeometry.reorderOffset(direction = direction, isRtl = isRtl)
            CustomAccessibilityAction(label = labels.getValue(direction)) {
                currentHandler.moveRelativeTo(
                    sourceKey = item.id,
                    tabs = currentTabs,
                    offset = offset,
                )
            }
        }
    }
}

@Composable
private fun reorderActionLabels(): Map<ReorderDirection, String> {
    val up = stringResource(R.string.custom_accessibility_action_reorder_tab_up)
    val down = stringResource(R.string.custom_accessibility_action_reorder_tab_down)
    val left = stringResource(R.string.custom_accessibility_action_reorder_tab_left)
    val right = stringResource(R.string.custom_accessibility_action_reorder_tab_right)

    return remember(up, down, left, right) {
        mapOf(
            ReorderDirection.UP to up,
            ReorderDirection.DOWN to down,
            ReorderDirection.LEFT to left,
            ReorderDirection.RIGHT to right,
        )
    }
}

/**
 * Moves the item keyed [sourceKey] [offset] places within [tabs], returning whether it moved.
 *
 * The bounds checks are defensive: [offset] is already validated against the tab order by
 * [TabReorderGeometry.reorderDirections], so a missing source or target means [tabs] changed since the action was
 * composed.
 */
private fun TabInteractionHandler.moveRelativeTo(
    sourceKey: String,
    tabs: List<TabsTrayItem>,
    offset: Int,
): Boolean {
    val sourceIndex = tabs.indexOfFirst { it.id == sourceKey }
    val target = if (sourceIndex < 0) null else tabs.getOrNull(sourceIndex + offset)

    return if (target == null) {
        false
    } else {
        onMove(sourceKey = sourceKey, targetKey = target.id, placeAfter = offset > 0)
        true
    }
}
