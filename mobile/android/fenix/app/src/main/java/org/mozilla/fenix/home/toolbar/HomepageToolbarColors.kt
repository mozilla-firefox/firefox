/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.colorResource
import org.mozilla.fenix.R

/**
 * Returns the wallpaper and browsing mode derived colors for home content.
 *
 * @param isPrivateMode Whether private browsing is enabled.
 * @param shouldUseEdgeToEdgeColors Whether the edge-to-edge wallpaper colors should be used.
 */
@Composable
fun homepageToolbarColors(
    isPrivateMode: Boolean,
    shouldUseEdgeToEdgeColors: Boolean,
): ColorScheme {
    val colors = MaterialTheme.colorScheme

    return when {
        isPrivateMode ->
            colors.copy(outlineVariant = colorResource(R.color.homepage_tab_edge_to_edge_private_toolbar_outline))

        shouldUseEdgeToEdgeColors -> colors.withEdgeToEdgeToolbarColors()

        else -> colors
    }
}

/**
 * Overrides the surface and divider colors of this [ColorScheme] with the transparent ones needed by toolbars drawn on
 * top of an edge-to-edge wallpaper.
 */
@Composable
internal fun ColorScheme.withEdgeToEdgeToolbarColors(): ColorScheme =
    copy(
        surface = colorResource(R.color.homepage_tab_edge_to_edge_toolbar_background),
        outlineVariant = colorResource(R.color.homepage_tab_edge_to_edge_toolbar_outline),
    )

/**
 * Returns the background color for the clipboard suggestion bar.
 *
 * When Edge2Edge background is enabled, the surrounding homepage toolbar surface is transparent, so the clipboard bar
 * needs its own color to stay legible on top of the wallpaper. In private mode, we defer to the theme-aware
 * [MaterialTheme] surface, which honors the private color scheme.
 *
 * @param shouldUseEdgeToEdgeColors Whether the edge-to-edge wallpaper colors should be used.
 * @param isPrivateMode Whether private browsing is enabled.
 * @return The [Color] to be used for the clipboard bar background.
 */
@Composable
@ReadOnlyComposable
fun edgeToEdgeClipboardBarBackground(
    shouldUseEdgeToEdgeColors: Boolean,
    isPrivateMode: Boolean,
): Color =
    if (shouldUseEdgeToEdgeColors && !isPrivateMode) {
        colorResource(R.color.fx_mobile_surface)
    } else {
        MaterialTheme.colorScheme.surface
    }
