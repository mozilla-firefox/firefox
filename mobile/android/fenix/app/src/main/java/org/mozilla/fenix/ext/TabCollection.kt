/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import android.content.Context
import androidx.annotation.ColorInt
import androidx.core.content.ContextCompat
import kotlin.math.abs
import mozilla.components.feature.tab.collections.TabCollection
import org.mozilla.fenix.R

/** Selects one of the predefined collection icon colors based on the id. */
@ColorInt
fun TabCollection.getIconColor(context: Context): Int {
    val iconColors = context.resources.obtainTypedArray(R.array.collection_icon_colors)
    val index = abs(id % iconColors.length()).toInt()
    val color =
        iconColors.getColor(
            index,
            ContextCompat.getColor(context, R.color.fx_mobile_icon_color_oncolor),
        )
    iconColors.recycle()
    return color
}
