/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf

import android.view.Gravity
import android.view.View
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.children
import androidx.core.view.isVisible
import androidx.core.view.marginBottom
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import org.mozilla.fenix.R

/**
 * A [CoordinatorLayout.Behavior] that keeps a PDF overlay clear of the browser chrome.
 *
 * @param isAddressBarAtBottom Whether the address bar is at the bottom of the screen.
 * @param isAnchoredToTop Whether the overlay anchors to the top edge of the parent.
 */
internal class PdfOverlayBehavior(
    isAddressBarAtBottom: Boolean,
    private val isAnchoredToTop: (CoordinatorLayout) -> Boolean,
) : CoordinatorLayout.Behavior<View>() {

    private val addressBarIds = listOf(R.id.toolbar, R.id.composable_toolbar)

    // When the address bar is at the bottom, then the top edge is the tab strip or nothing.
    // When the address bar is at the top, then the address bar is always the top bottom edge (tab strip is above it).
    private val topEdgeIds = if (isAddressBarAtBottom) listOf(R.id.tabstrip) else addressBarIds

    private val bottomEdgeIds =
        if (isAddressBarAtBottom) addressBarIds + R.id.navigation_bar else listOf(R.id.navigation_bar)

    private fun trackedIds(parent: CoordinatorLayout) = if (isAnchoredToTop(parent)) topEdgeIds else bottomEdgeIds

    private fun trackedChrome(parent: CoordinatorLayout): List<View> {
        val ids = trackedIds(parent)
        return parent.children.filter { it.isVisible && it.id in ids }.toList()
    }

    /**
     * Each piece of chrome is translated away independently, so the space vacated along the overlay's edge is the total
     * of their translations. Once all of it is hidden this equals the chrome height, leaving the overlay flush with the
     * edge.
     */
    private fun List<View>.translationY(): Float = map { it.translationY }.sum()

    override fun layoutDependsOn(parent: CoordinatorLayout, child: View, dependency: View): Boolean =
        dependency.id in trackedIds(parent)

    override fun onMeasureChild(
        parent: CoordinatorLayout,
        child: View,
        parentWidthMeasureSpec: Int,
        widthUsed: Int,
        parentHeightMeasureSpec: Int,
        heightUsed: Int,
    ): Boolean {
        val params = child.layoutParams as? CoordinatorLayout.LayoutParams ?: return false
        val chrome = trackedChrome(parent)
        val chromeHeight = chrome.sumOf { it.measuredHeight }

        if (isAnchoredToTop(parent)) {
            params.gravity = Gravity.TOP
            params.topMargin = chromeHeight
            params.bottomMargin = 0
        } else {
            params.gravity = Gravity.BOTTOM
            params.topMargin = 0
            params.bottomMargin = chromeHeight + chrome.sumOf { it.marginBottom }
        }

        return false
    }

    override fun onLayoutChild(parent: CoordinatorLayout, child: View, layoutDirection: Int): Boolean {
        child.translationY = trackedChrome(parent).translationY()
        return false
    }

    override fun onDependentViewChanged(parent: CoordinatorLayout, child: View, dependency: View): Boolean {
        val translationY = trackedChrome(parent).translationY()

        if (child.translationY == translationY) {
            return false
        }

        child.translationY = translationY
        return true
    }
}

/**
 * Positions for the PDF tools. The tools will be a top toolbar for tablets and a bottom set of FABs for phones.
 *
 * @param isAddressBarAtBottom Whether the address bar is positioned at the bottom of the screen.
 * @return A behavior that anchors the tools to the top on tablets and to the bottom on phones.
 */
internal fun pdfToolsBehavior(isAddressBarAtBottom: Boolean) =
    PdfOverlayBehavior(isAddressBarAtBottom) { AcornWindowSize.isLargeWindow(it.context) }

/**
 * Positions for the signature dialog. The dialog should anchor to the bottom of the screen on all cases.
 *
 * @param isAddressBarAtBottom Whether the address bar is positioned at the bottom of the screen.
 * @return A behavior that anchors the dialog to the bottom.
 */
internal fun signatureDialogBehavior(isAddressBarAtBottom: Boolean) = PdfOverlayBehavior(isAddressBarAtBottom) { false }
