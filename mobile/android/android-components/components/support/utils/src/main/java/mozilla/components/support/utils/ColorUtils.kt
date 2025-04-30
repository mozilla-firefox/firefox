/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import android.graphics.Color
import androidx.annotation.ColorInt
import androidx.annotation.FloatRange
import androidx.compose.ui.graphics.lerp
import androidx.core.graphics.ColorUtils
import androidx.core.graphics.toColorInt
import androidx.compose.ui.graphics.Color as ComposeColor

/**
 * Default color for disabled views in normal mode for light and dark theme.
 */
private const val LIGHT_GRAY_HEX = "#66FBFBFE"
private const val DARK_GRAY_HEX = "#6615141A"

object ColorUtils {

    /**
     * Get text color (white or black) that is readable on top of the provided background color.
     */
    @JvmStatic
    fun getReadableTextColor(@ColorInt backgroundColor: Int): Int {
        return if (isDark(backgroundColor)) Color.WHITE else Color.BLACK
    }

    /**
     * Get disabled text color (light gray or dark gray) that is readable on top of the provided background color.
     */
    @JvmStatic
    fun getDisabledReadableTextColor(@ColorInt backgroundColor: Int): Int {
        return if (isDark(backgroundColor)) {
            LIGHT_GRAY_HEX.toColorInt()
        } else {
            DARK_GRAY_HEX.toColorInt()
        }
    }

    /**
     * @return true if the given [color] is dark enough that white text should be used on top of it.
     */
    @JvmStatic
    @SuppressWarnings("MagicNumber")
    fun isDark(@ColorInt color: Int): Boolean {
        if (color == Color.TRANSPARENT || ColorUtils.calculateLuminance(color) >= 0.5) {
            return false
        }

        val greyValue = grayscaleFromRGB(color)
        // 186 chosen rather than the seemingly obvious 128 because of gamma.
        return greyValue < 186
    }

    @SuppressWarnings("MagicNumber")
    private fun grayscaleFromRGB(@ColorInt color: Int): Int {
        val red = Color.red(color)
        val green = Color.green(color)
        val blue = Color.blue(color)
        // Magic weighting taken from a stackoverflow post, supposedly related to how
        // humans perceive color.
        return (0.299 * red + 0.587 * green + 0.114 * blue).toInt()
    }

    /**
     * Calculates the alpha value corresponding to the given opacity percentage.
     *
     * @param opacity The desired opacity percentage (0 to 100).
     * @return The alpha value (0 to 255) to be used in a color with the specified opacity.
     */
    @JvmStatic
    @SuppressWarnings("MagicNumber")
    fun calculateAlphaFromPercentage(opacity: Int): Int {
        return (opacity * 255 / 100).coerceIn(0, 255)
    }

    /**
     * Produce a lighter color than this using the given factor.
     *
     * @param factor How much lighter the new color should be.
     * A higher value will produce a lighter color.
     */
    fun ComposeColor.lighten(@FloatRange(from = 0.0, to = 1.0) factor: Float) =
        lerp(this, ComposeColor.White, factor)

    /**
     * Produce a darker color than this using the given factor.
     *
     * @param factor How much darken the new color should be.
     * A higher value will produce a darker color.
     */
    fun ComposeColor.darken(@FloatRange(from = 0.0, to = 1.0) factor: Float) =
        lerp(this, ComposeColor.Black, factor)
}
