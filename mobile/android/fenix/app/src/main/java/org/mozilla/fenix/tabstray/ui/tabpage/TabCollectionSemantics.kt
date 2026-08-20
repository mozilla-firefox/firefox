/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.ui.tabpage

import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.CollectionItemInfo

/**
 * Maps tab tray item positions onto accessibility collection rows and columns.
 *
 * Positions here count every slot a screen reader can land on, including non-tab slots such as the tab group onboarding
 * card. Reordering works in tab list indices instead - see [TabReorderGeometry].
 *
 * @param itemCount Number of item slots.
 * @param columns Number of columns in the layout (1 for a list).
 */
internal data class TabCollectionSemantics(
    private val itemCount: Int,
    private val columns: Int,
) {
    val collectionInfo =
        CollectionInfo(
            rowCount = (itemCount + columns - 1) / columns,
            columnCount = columns,
        )

    fun itemInfo(position: Int, columnSpan: Int = 1) =
        CollectionItemInfo(
            rowIndex = position / columns,
            rowSpan = 1,
            columnIndex = position % columns,
            columnSpan = columnSpan,
        )
}
