/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import android.view.ViewGroup
import androidx.core.view.updateLayoutParams
import mozilla.components.support.ktx.android.view.pixelSizeFor
import org.mozilla.focus.R
import org.mozilla.focus.databinding.BottomSheetDragHandleBinding

/**
 * Sets the space under the drag handle at the top of a panel, which is larger when the website presents a Qualified
 * Website Authentication Certificate (QWAC).
 *
 * @param isQwac Whether the current website is known to present a QWAC.
 */
internal fun BottomSheetDragHandleBinding.updateSpacing(isQwac: Boolean) {
    dragHandleBar.updateLayoutParams<ViewGroup.MarginLayoutParams> {
        bottomMargin =
            root.pixelSizeFor(
                if (isQwac) {
                    R.dimen.bottom_sheet_drag_handle_bottom_margin_qwac
                } else {
                    R.dimen.bottom_sheet_drag_handle_bottom_margin
                }
            )
    }
}
