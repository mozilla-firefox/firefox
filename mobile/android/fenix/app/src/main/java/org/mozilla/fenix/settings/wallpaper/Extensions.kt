/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.wallpaper

import kotlin.math.max
import org.mozilla.fenix.onboarding.WallpaperOnboardingDialogFragment.Companion.SEASONAL_WALLPAPERS_COUNT
import org.mozilla.fenix.onboarding.WallpaperOnboardingDialogFragment.Companion.THUMBNAILS_SELECTION_COUNT
import org.mozilla.fenix.wallpapers.Wallpaper

/** The extension function to group wallpapers into the appropriate collections for display. */
fun List<Wallpaper>.groupByDisplayableCollection(): Map<Wallpaper.Collection, List<Wallpaper>> = groupBy {
    if (it.collection == Wallpaper.DefaultCollection) {
        Wallpaper.ClassicFirefoxCollection
    } else {
        it.collection
    }
}
    .map {
        val wallpapers =
            it.value.filter { wallpaper ->
                wallpaper.thumbnailFileState == Wallpaper.ImageFileState.Downloaded
            }
        it.key to wallpapers
    }
    .toMap()

/**
 * Returns a list of wallpapers to display in the wallpaper onboarding.
 *
 * The ideal scenario is to return a list of wallpaper in the following order: 2 local wallpapers, 3 seasonal and 1
 * classic wallpapers, but in case where there are less than 3 seasonal wallpapers, the remaining wallpapers are filled
 * by classic wallpapers. If we have less than 6 wallpapers, return all the available seasonal and classic wallpapers.
 */
fun List<Wallpaper>.getWallpapersForOnboarding(): List<Wallpaper> {
    val (localWallpapers, remoteWallpapers) = this.partition { it.collection.name == Wallpaper.DEFAULT_COLLECTION }

    val (allClassicWallpapers, allSeasonalWallpapers) =
        remoteWallpapers.partition {
            it.collection.name == Wallpaper.CLASSIC_FIREFOX_COLLECTION
        }

    val seasonalWallpapersCount =
        max(
            SEASONAL_WALLPAPERS_COUNT,
            THUMBNAILS_SELECTION_COUNT - localWallpapers.size - allClassicWallpapers.size,
        )
    val seasonalWallpapers = allSeasonalWallpapers.take(seasonalWallpapersCount)

    val classicWallpapers =
        allClassicWallpapers.take(THUMBNAILS_SELECTION_COUNT - localWallpapers.size - seasonalWallpapers.size)

    return localWallpapers + seasonalWallpapers + classicWallpapers
}
