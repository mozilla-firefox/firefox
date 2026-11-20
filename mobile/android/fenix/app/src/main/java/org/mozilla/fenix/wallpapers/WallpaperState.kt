/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.wallpapers

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.graphics.Color

/**
 * Represents all state related to the Wallpapers feature.
 *
 * @property currentWallpaper The currently selected [Wallpaper].
 * @property availableWallpapers The full list of wallpapers that can be selected.
 */
data class WallpaperState(
    val currentWallpaper: Wallpaper,
    val availableWallpapers: List<Wallpaper>,
) {
    companion object {
        val default =
            WallpaperState(
                currentWallpaper = Wallpaper.Solid,
                availableWallpapers = listOf(),
            )
    }

    /**
     * [Color] to use for a card background color against the current wallpaper.
     *
     * @return The appropriate light or dark wallpaper card [Color], if available, otherwise a default.
     */
    val cardBackgroundColor: Color
        @Composable
        @ReadOnlyComposable
        get() =
            when {
                currentWallpaper.cardColorLight != null && currentWallpaper.cardColorDark != null -> {
                    if (isSystemInDarkTheme()) {
                        Color(currentWallpaper.cardColorDark)
                    } else {
                        Color(currentWallpaper.cardColorLight)
                    }
                }
                else -> MaterialTheme.colorScheme.surfaceBright
            }

    /** [Color] to use for a button background color on the current wallpaper. */
    val buttonBackgroundColor: Color
        @Composable
        get() =
            if (isCurrentWallpaperSolid()) {
                ButtonDefaults.buttonColors().containerColor
            } else {
                MaterialTheme.colorScheme.surface
            }

    /** [Color] to use for button text on the current wallpaper. */
    val buttonTextColor: Color
        @Composable
        get() =
            if (isCurrentWallpaperSolid()) {
                ButtonDefaults.buttonColors().contentColor
            } else {
                MaterialTheme.colorScheme.onSurface
            }

    /** [Color] to use for text on the current wallpaper. */
    val textColor: Color
        @Composable get() = currentWallpaper.textColor?.let { Color(it) } ?: MaterialTheme.colorScheme.onSurface

    private fun isCurrentWallpaperSolid(): Boolean = Wallpaper.nameIsSolid(currentWallpaper.name)

    /** Run the Composable [run] block only if the current wallpaper's card colors are available. */
    @Composable
    fun ComposeRunIfWallpaperCardColorsAreAvailable(
        run: @Composable (cardColorLight: Color, cardColorDark: Color) -> Unit
    ) {
        if (currentWallpaper.cardColorLight != null && currentWallpaper.cardColorDark != null) {
            run(Color(currentWallpaper.cardColorLight), Color(currentWallpaper.cardColorDark))
        }
    }
}
