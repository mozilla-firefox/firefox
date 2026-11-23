/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.compose

import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.SheetState
import androidx.compose.material3.SheetValue
import androidx.compose.material3.rememberBottomSheetState
import androidx.compose.runtime.Composable

/**
 * Creates and remembers a [SheetState] for a modal bottom sheet, starting hidden, using the unified
 * [rememberBottomSheetState] API that replaces the deprecated `rememberModalBottomSheetState`. Unlike the deprecated
 * function, a sheet shorter than half the screen keeps its partially expanded value, at the expanded position.
 *
 * @param skipPartiallyExpanded Whether the partially expanded state should be skipped, so the sheet toggles only
 *   between hidden and fully expanded.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun rememberModalSheetState(skipPartiallyExpanded: Boolean = false): SheetState =
    rememberBottomSheetState(
        initialValue = SheetValue.Hidden,
        enabledValues =
            if (skipPartiallyExpanded) {
                setOf(SheetValue.Hidden, SheetValue.Expanded)
            } else {
                setOf(SheetValue.Hidden, SheetValue.PartiallyExpanded, SheetValue.Expanded)
            },
    )
