/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import android.graphics.Rect
import android.view.TouchDelegate
import android.view.View
import androidx.annotation.Dimension
import androidx.annotation.Dimension.Companion.DP
import androidx.annotation.VisibleForTesting
import androidx.coordinatorlayout.widget.CoordinatorLayout
import mozilla.components.support.ktx.android.util.dpToPx

fun View.increaseTapArea(@Dimension(unit = DP) extraDps: Int) {
    val extraPx = extraDps.dpToPx(resources.displayMetrics)
    increaseTapAreaInternal(extraPx)
}

@VisibleForTesting
internal fun View.increaseTapAreaInternal(extraPx: Int) {
    val parent = this.parent as View
    parent.post {
        val touchRect = Rect()
        getHitRect(touchRect)
        touchRect.inset(-extraPx, -extraPx)
        parent.touchDelegate = TouchDelegate(touchRect, this)
    }
}

fun View.removeTouchDelegate() {
    val parent = this.parent as View
    parent.post {
        parent.touchDelegate = null
    }
}

/**
 * Fills a [Rect] with data about a view's location in the screen.
 *
 * @see android.view.View.getLocationOnScreen
 * @see mozilla.components.support.ktx.android.view.getRectWithViewLocation for a version of this that is relative to a
 *   window
 */
fun View.getRectWithScreenLocation(): Rect {
    val locationOnScreen = IntArray(2).apply { getLocationOnScreen(this) }
    return Rect(
        locationOnScreen[0],
        locationOnScreen[1],
        locationOnScreen[0] + width,
        locationOnScreen[1] + height,
    )
}

/** Used to get and set CoordinatorLayout Behavior on a View. */
var View.behavior: CoordinatorLayout.Behavior<*>?
    get() = (layoutParams as? CoordinatorLayout.LayoutParams)?.behavior
    set(value) {
        (layoutParams as? CoordinatorLayout.LayoutParams)?.behavior = value
    }
