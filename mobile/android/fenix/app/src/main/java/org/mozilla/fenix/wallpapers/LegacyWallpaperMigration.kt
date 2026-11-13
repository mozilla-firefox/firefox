/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.wallpapers

import java.io.File
import java.io.IOException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.toHexColor
import org.mozilla.fenix.wallpapers.Wallpaper.Companion.AMETHYST
import org.mozilla.fenix.wallpapers.Wallpaper.Companion.BEACH_VIBE
import org.mozilla.fenix.wallpapers.Wallpaper.Companion.CERULEAN
import org.mozilla.fenix.wallpapers.Wallpaper.Companion.SUNRISE

/**
 * Manages the migration of legacy wallpapers to the new paths
 *
 * @param storageRootDirectory The top level app-local storage directory.
 * @param settings Used to update the color of the text shown above wallpapers.
 * @param downloadWallpaper Function used to download assets for legacy drawable wallpapers.
 * @param ioDispatcher The dispatcher to use for IO operations.
 */
class LegacyWallpaperMigration(
    private val storageRootDirectory: File,
    private val settings: Settings,
    private val downloadWallpaper: suspend (Wallpaper) -> Unit,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    /**
     * Migrate the legacy wallpaper to the new path and delete the remaining legacy files.
     *
     * @param wallpaperName Name of the wallpaper to be migrated.
     */
    suspend fun migrateLegacyWallpaper(wallpaperName: String): String =
        withContext(ioDispatcher) {
            // For the legacy wallpapers previously stored as drawables,
            // attempt to download them at startup.
            when (wallpaperName) {
                CERULEAN,
                SUNRISE,
                AMETHYST -> {
                    downloadWallpaper(
                        Wallpaper.Default.copy(
                            name = wallpaperName,
                            collection = Wallpaper.ClassicFirefoxCollection,
                            thumbnailFileState = Wallpaper.ImageFileState.Unavailable,
                            assetsFileState = Wallpaper.ImageFileState.Unavailable,
                        )
                    )
                    return@withContext wallpaperName
                }
            }
            val legacyPortraitFile = File(storageRootDirectory, "wallpapers/portrait/light/$wallpaperName.png")
            val legacyLandscapeFile = File(storageRootDirectory, "wallpapers/landscape/light/$wallpaperName.png")
            // If any of portrait or landscape files of the wallpaper are missing, then we shouldn't
            // migrate it
            if (!legacyLandscapeFile.exists() || !legacyPortraitFile.exists()) {
                return@withContext wallpaperName
            }
            // The V2 name for the "beach-vibe" wallpaper is "beach-vibes".
            val migratedWallpaperName =
                if (wallpaperName == BEACH_VIBE) {
                    "beach-vibes"
                } else {
                    wallpaperName
                }
            // Directory where the legacy wallpaper files should be migrated
            val targetDirectory = "wallpapers/" + migratedWallpaperName.lowercase()

            migrateLegacyWallpaperFiles(
                wallpaperName = wallpaperName,
                legacyPortraitFile = legacyPortraitFile,
                legacyLandscapeFile = legacyLandscapeFile,
                targetDirectory = targetDirectory,
            )

            // Delete the remaining legacy files
            File(storageRootDirectory, "wallpapers/portrait").deleteRecursively()
            File(storageRootDirectory, "wallpapers/landscape").deleteRecursively()

            return@withContext migratedWallpaperName
        }

    private fun migrateLegacyWallpaperFiles(
        wallpaperName: String,
        legacyPortraitFile: File,
        legacyLandscapeFile: File,
        targetDirectory: String,
    ) {
        try {
            // Use the portrait file as thumbnail
            legacyPortraitFile.copyTo(
                File(
                    storageRootDirectory,
                    "$targetDirectory/thumbnail.png",
                )
            )
            // Copy the portrait file
            legacyPortraitFile.copyTo(
                File(
                    storageRootDirectory,
                    "$targetDirectory/portrait.png",
                )
            )
            // Copy the landscape file
            legacyLandscapeFile.copyTo(
                File(
                    storageRootDirectory,
                    "$targetDirectory/landscape.png",
                )
            )

            // If an expired Turning Red wallpaper is successfully migrated
            if (wallpaperName == TURNING_RED_MEI_WALLPAPER_NAME || wallpaperName == TURNING_RED_PANDA_WALLPAPER_NAME) {
                settings.currentWallpaperTextColor = TURNING_RED_WALLPAPER_TEXT_COLOR.toHexColor()
            }
        } catch (e: IOException) {
            Logger.error("Failed to migrate legacy wallpaper", e)
            settings.shouldMigrateLegacyWallpaperCardColors = false
        }
    }

    /** Helper function used to migrate a legacy wallpaper's card colors that previously did not exist. */
    fun migrateExpiredWallpaperCardColors() {
        // The card colors should NOT be migrated if the file migration was ran prior to these
        // changes and it failed. We can verify this by checking [settings.currentWallpaperTextColor],
        // since this is only initialized for legacy wallpapers in
        // [LegacyWallpaperMigration.migrateLegacyWallpaper].
        if (settings.currentWallpaperTextColor != 0L) {
            when (settings.currentWallpaperName) {
                TURNING_RED_MEI_WALLPAPER_NAME -> {
                    settings.currentWallpaperCardColorLight = TURNING_RED_MEI_WALLPAPER_CARD_COLOR_LIGHT.toHexColor()
                    settings.currentWallpaperCardColorDark = TURNING_RED_MEI_WALLPAPER_CARD_COLOR_DARK.toHexColor()
                }
                TURNING_RED_PANDA_WALLPAPER_NAME -> {
                    settings.currentWallpaperCardColorLight = TURNING_RED_PANDA_WALLPAPER_CARD_COLOR_LIGHT.toHexColor()
                    settings.currentWallpaperCardColorDark = TURNING_RED_PANDA_WALLPAPER_CARD_COLOR_DARK.toHexColor()
                }
            }
        }

        settings.shouldMigrateLegacyWallpaperCardColors = false
    }

    companion object {
        const val TURNING_RED_MEI_WALLPAPER_NAME = "mei"
        const val TURNING_RED_PANDA_WALLPAPER_NAME = "panda"
        const val TURNING_RED_WALLPAPER_TEXT_COLOR = "FFFBFBFE"
        const val TURNING_RED_MEI_WALLPAPER_CARD_COLOR_LIGHT = "FFFDE9C3"
        const val TURNING_RED_MEI_WALLPAPER_CARD_COLOR_DARK = "FF532906"
        const val TURNING_RED_PANDA_WALLPAPER_CARD_COLOR_LIGHT = "FFFFEDF1"
        const val TURNING_RED_PANDA_WALLPAPER_CARD_COLOR_DARK = "FF611B28"
    }
}
