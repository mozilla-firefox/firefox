/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.widget.TextView
import androidx.appcompat.widget.AppCompatTextView
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.FadeDirection
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.FadeDirection.FADE_DIRECTION_END
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.FadeDirection.FADE_DIRECTION_START
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.TextGravity
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.TextGravity.TEXT_GRAVITY_END
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Custom [TextView] meant to only be used programatically with special support for longer texts that:
 * - can be faded at the start or end of the screen or not at all
 * - can have the start text shown with the end clipped or the end text shown with the start clipped.
 *
 * @param context [Context] used to instantiate this `View`.
 * @param fadeDirection [FadeDirection] The direction in which the text should be faded.
 * @param textGravity [TextGravity] The gravity of the text.
 */
@SuppressLint("ViewConstructor")
internal class CustomFadeAndGravityTextView(
    context: Context,
    private val fadeDirection: FadeDirection,
    private val textGravity: TextGravity,
) : AppCompatTextView(context) {
    override fun getLeftFadingEdgeStrength() = when (fadeDirection) {
        FADE_DIRECTION_START -> getFadeStrength()
        else -> 0f
    }

    override fun getRightFadingEdgeStrength() = when (fadeDirection) {
        FADE_DIRECTION_END -> getFadeStrength()
        else -> 0f
    }

    override fun draw(canvas: Canvas) {
        if (textGravity == TEXT_GRAVITY_END) {
            scrollToEnd()
        }

        super.draw(canvas)
    }

    private fun getFadeStrength() =
        when (computeHorizontalScrollOffset() + computeHorizontalScrollExtent() < computeHorizontalScrollRange()) {
            true -> FADE_STRENGTH_FULL
            false -> FADE_STRENGTH_NONE
        }

    private fun scrollToEnd() {
        if (layout != null) {
            val textWidth = layout.getLineWidth(0)
            val availableWidth = measuredWidth - paddingLeft - paddingRight
            val textOverflow = max(textWidth - availableWidth - SCROLL_OFFSET_TO_FORCE_FADE, 0f)
            scrollTo(textOverflow.roundToInt(), 0)
        }
    }

    companion object {
        const val FADE_STRENGTH_FULL = 1f
        const val FADE_STRENGTH_NONE = 0f
        const val SCROLL_OFFSET_TO_FORCE_FADE = 2
    }
}
