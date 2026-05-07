/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.grouptabstrip

import android.content.Context
import android.view.Gravity
import android.view.View
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.children
import androidx.core.view.isVisible
import org.mozilla.fenix.R
import org.mozilla.fenix.components.toolbar.ToolbarPosition

/**
 * [CoordinatorLayout.Behavior] that anchors the group tab strip directly above the bottom toolbar
 * (or above the bottom navigation bar, when present) so it always sits at the bottom of the screen
 * regardless of the user's toolbar-position preference.
 *
 * When the toolbar is at the top, the strip is placed at the bottom of the parent.
 *
 * Modeled after [org.mozilla.fenix.browser.AutofillSelectBarBehavior].
 *
 * @param toolbarPosition The user's current toolbar position; determines whether the strip
 * anchors to the toolbar or sits at the bottom of the screen.
 */
class GroupTabStripBehavior<V : View>(
    context: Context,
    toolbarPosition: ToolbarPosition,
) : CoordinatorLayout.Behavior<V>(context, null) {

    private val dependenciesIds: List<Int> = buildList {
        add(R.id.navigation_bar)
        if (toolbarPosition == ToolbarPosition.BOTTOM) {
            add(R.id.toolbar)
            add(R.id.composable_toolbar)
        }
    }

    private var currentAnchorId: Int = View.NO_ID

    override fun layoutDependsOn(
        parent: CoordinatorLayout,
        child: V,
        dependency: View,
    ): Boolean {
        val nextAnchorId = dependenciesIds
            .intersect(parent.children.filter { it.isVisible }.map { it.id }.toSet())
            .firstOrNull() ?: View.NO_ID

        if (nextAnchorId != currentAnchorId) {
            position(child, parent.children.firstOrNull { it.id == nextAnchorId })
        }

        return false
    }

    private fun position(strip: V, dependency: View?) {
        currentAnchorId = dependency?.id ?: View.NO_ID
        strip.post {
            if (dependency == null) placeAtBottom(strip) else placeAboveAnchor(strip, dependency)
        }
    }

    private fun placeAtBottom(strip: View) {
        val params = strip.layoutParams as CoordinatorLayout.LayoutParams
        params.anchorId = View.NO_ID
        params.anchorGravity = Gravity.NO_GRAVITY
        params.gravity = Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL
        strip.layoutParams = params
    }

    private fun placeAboveAnchor(strip: View, anchor: View) {
        val params = strip.layoutParams as CoordinatorLayout.LayoutParams
        params.anchorId = anchor.id
        params.anchorGravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
        params.gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
        strip.layoutParams = params
    }
}
