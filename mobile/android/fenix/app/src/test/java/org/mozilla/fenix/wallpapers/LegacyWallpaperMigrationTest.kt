/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.wallpapers

import io.mockk.mockk
import io.mockk.verify
import java.io.File
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.mozilla.fenix.utils.Settings
import org.mozilla.fenix.utils.toHexColor
import org.mozilla.fenix.wallpapers.LegacyWallpaperMigration.Companion.TURNING_RED_MEI_WALLPAPER_NAME
import org.mozilla.fenix.wallpapers.LegacyWallpaperMigration.Companion.TURNING_RED_PANDA_WALLPAPER_NAME
import org.mozilla.fenix.wallpapers.LegacyWallpaperMigration.Companion.TURNING_RED_WALLPAPER_TEXT_COLOR

class LegacyWallpaperMigrationTest {
    @Rule @JvmField val tempFolder = TemporaryFolder()
    private lateinit var settings: Settings
    private lateinit var wallpapersFolder: File
    private lateinit var downloadWallpaper: (Wallpaper) -> Unit
    private lateinit var downloadedWallpapers: MutableList<Wallpaper>
    private lateinit var migrationHelper: LegacyWallpaperMigration
    private lateinit var portraitLightFolder: File
    private lateinit var portraitDarkFolder: File
    private lateinit var landscapeLightFolder: File
    private lateinit var landscapeDarkFolder: File

    @Before
    fun setup() {
        wallpapersFolder = File(tempFolder.root, "wallpapers")
        settings = mockk(relaxed = true)
        downloadedWallpapers = mutableListOf()
        downloadWallpaper = { wallpaper ->
            downloadedWallpapers += wallpaper
        }
        migrationHelper =
            LegacyWallpaperMigration(
                storageRootDirectory = tempFolder.root,
                settings = settings,
                downloadWallpaper,
            )
    }

    @Test
    fun `WHEN the legacy wallpaper is migrated THEN the legacy wallpapers are deleted`() = runTest {
        val wallpaperName = "wallpaper1"

        createAllLegacyFiles(wallpaperName)

        migrationHelper.migrateLegacyWallpaper(wallpaperName)

        assertTrue(getAllFiles(wallpaperName).all { it.exists() })
        assertFalse(File(portraitLightFolder, "$wallpaperName.png").exists())
        assertFalse(File(portraitDarkFolder, "$wallpaperName.png").exists())
        assertFalse(File(landscapeLightFolder, "$wallpaperName.png").exists())
        assertFalse(File(landscapeDarkFolder, "$wallpaperName.png").exists())
        assertTrue(downloadedWallpapers.isEmpty())
    }

    @Test
    fun `GIVEN landscape legacy wallpaper is missing WHEN the wallpapers are migrated THEN the wallpaper is not migrated`() =
        runTest {
            val portraitOnlyWallpaperName = "portraitOnly"
            val completeWallpaperName = "legacy"
            createAllLegacyFiles(completeWallpaperName)
            File(landscapeLightFolder, "$portraitOnlyWallpaperName.png").apply {
                createNewFile()
            }
            File(landscapeDarkFolder, "$portraitOnlyWallpaperName.png").apply {
                createNewFile()
            }

            migrationHelper.migrateLegacyWallpaper(portraitOnlyWallpaperName)
            migrationHelper.migrateLegacyWallpaper(completeWallpaperName)

            assertTrue(getAllFiles(completeWallpaperName).all { it.exists() })
            assertFalse(getAllFiles(portraitOnlyWallpaperName).any { it.exists() })
        }

    @Test
    fun `GIVEN portrait legacy wallpaper is missing WHEN the wallpapers are migrated THEN the wallpaper is not migrated`() =
        runTest {
            val landscapeOnlyWallpaperName = "portraitOnly"
            val completeWallpaperName = "legacy"
            createAllLegacyFiles(completeWallpaperName)
            File(portraitLightFolder, "$landscapeOnlyWallpaperName.png").apply {
                createNewFile()
            }
            File(portraitDarkFolder, "$landscapeOnlyWallpaperName.png").apply {
                createNewFile()
            }

            migrationHelper.migrateLegacyWallpaper(landscapeOnlyWallpaperName)
            migrationHelper.migrateLegacyWallpaper(completeWallpaperName)

            assertTrue(getAllFiles(completeWallpaperName).all { it.exists() })
            assertFalse(getAllFiles(landscapeOnlyWallpaperName).any { it.exists() })
        }

    @Test
    fun `GIVEN a Turning Red wallpaper WHEN it is successfully migrated THEN set a matching text color`() {
        runTest {
            createAllLegacyFiles(TURNING_RED_MEI_WALLPAPER_NAME)
            migrationHelper.migrateLegacyWallpaper(TURNING_RED_MEI_WALLPAPER_NAME)
            assertTrue(getAllFiles(TURNING_RED_MEI_WALLPAPER_NAME).all { it.exists() })
            verify(exactly = 1) {
                settings.currentWallpaperTextColor = TURNING_RED_WALLPAPER_TEXT_COLOR.toHexColor()
            }

            createAllLegacyFiles(TURNING_RED_PANDA_WALLPAPER_NAME)
            migrationHelper.migrateLegacyWallpaper(TURNING_RED_PANDA_WALLPAPER_NAME)
            assertTrue(getAllFiles(TURNING_RED_PANDA_WALLPAPER_NAME).all { it.exists() })
            verify(exactly = 2) {
                settings.currentWallpaperTextColor = TURNING_RED_WALLPAPER_TEXT_COLOR.toHexColor()
            }
        }
    }

    @Test
    fun `GIVEN a Turning Red wallpaper WHEN it can't be migrated THEN don't set a matching text color`() {
        runTest {
            migrationHelper.migrateLegacyWallpaper(TURNING_RED_MEI_WALLPAPER_NAME)
            migrationHelper.migrateLegacyWallpaper(TURNING_RED_PANDA_WALLPAPER_NAME)

            assertFalse(getAllFiles(TURNING_RED_MEI_WALLPAPER_NAME).all { it.exists() })
            assertFalse(getAllFiles(TURNING_RED_PANDA_WALLPAPER_NAME).all { it.exists() })
            verify(exactly = 0) {
                settings.currentWallpaperTextColor = TURNING_RED_WALLPAPER_TEXT_COLOR.toHexColor()
            }
        }
    }

    @Test
    fun `GIVEN legacy wallpapers different than Turning Red WHEN they are tried to be migrated THEN don't set a matching text color`() {
        runTest {
            val wallpaper1 = "wallpaper1"
            val wallpaper2 = "wallpaper2"

            migrationHelper.migrateLegacyWallpaper(wallpaper1)
            assertFalse(getAllFiles(wallpaper1).all { it.exists() })
            verify(exactly = 0) {
                settings.currentWallpaperTextColor = TURNING_RED_WALLPAPER_TEXT_COLOR.toHexColor()
            }

            createAllLegacyFiles(wallpaper2)
            migrationHelper.migrateLegacyWallpaper(wallpaper2)
            assertTrue(getAllFiles(wallpaper2).all { it.exists() })
            verify(exactly = 0) {
                settings.currentWallpaperTextColor = TURNING_RED_WALLPAPER_TEXT_COLOR.toHexColor()
            }
        }
    }

    @Test
    fun `WHEN the beach-vibe legacy wallpaper is migrated THEN the legacy wallpapers destination is beach-vibes`() =
        runTest {
            val wallpaperName = Wallpaper.BEACH_VIBE

            createAllLegacyFiles(wallpaperName)

            val migratedWallpaperName = migrationHelper.migrateLegacyWallpaper(wallpaperName)

            assertEquals("beach-vibes", migratedWallpaperName)
            assertTrue(getAllFiles("beach-vibes").all { it.exists() })
        }

    @Test
    fun `WHEN a drawable legacy wallpaper is migrated THEN the respective V2 wallpaper is downloaded`() = runTest {
        var migratedWallpaperName = migrationHelper.migrateLegacyWallpaper(Wallpaper.CERULEAN)

        assertEquals(Wallpaper.CERULEAN, migratedWallpaperName)
        assertTrue(
            downloadedWallpapers.any {
                it.name == Wallpaper.CERULEAN && it.collection == Wallpaper.ClassicFirefoxCollection
            }
        )

        migratedWallpaperName = migrationHelper.migrateLegacyWallpaper(Wallpaper.SUNRISE)

        assertEquals(Wallpaper.SUNRISE, migratedWallpaperName)
        assertTrue(
            downloadedWallpapers.any {
                it.name == Wallpaper.SUNRISE && it.collection == Wallpaper.ClassicFirefoxCollection
            }
        )

        migratedWallpaperName = migrationHelper.migrateLegacyWallpaper(Wallpaper.AMETHYST)

        assertEquals(Wallpaper.AMETHYST, migratedWallpaperName)
        assertTrue(
            downloadedWallpapers.any {
                it.name == Wallpaper.AMETHYST && it.collection == Wallpaper.ClassicFirefoxCollection
            }
        )
    }

    private fun createAllLegacyFiles(name: String) {
        if (!this::portraitLightFolder.isInitialized || !portraitLightFolder.exists()) {
            portraitLightFolder = tempFolder.newFolder("wallpapers", "portrait", "light")
            portraitDarkFolder = tempFolder.newFolder("wallpapers", "portrait", "dark")
            landscapeLightFolder = tempFolder.newFolder("wallpapers", "landscape", "light")
            landscapeDarkFolder = tempFolder.newFolder("wallpapers", "landscape", "dark")
        }

        File(portraitLightFolder, "$name.png").apply {
            createNewFile()
        }
        File(landscapeLightFolder, "$name.png").apply {
            createNewFile()
        }
        File(portraitDarkFolder, "$name.png").apply {
            createNewFile()
        }
        File(landscapeDarkFolder, "$name.png").apply {
            createNewFile()
        }
    }

    private fun getAllFiles(name: String): List<File> {
        val folder = File(wallpapersFolder, name)
        return listOf(
            folder,
            File(folder, "portrait.png"),
            File(folder, "landscape.png"),
            File(folder, "thumbnail.png"),
        )
    }
}
