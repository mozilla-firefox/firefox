/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.wallpapers

import android.content.res.Configuration
import android.graphics.Bitmap
import android.util.Size
import androidx.annotation.VisibleForTesting
import java.io.File
import java.util.Date
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.concept.fetch.Client
import mozilla.components.support.ktx.android.graphics.toSampledBitmap
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.utils.Settings

/**
 * Contains use cases related to the wallpaper feature.
 *
 * @param settings Used for retrieving and updating wallpaper-related settings like the currently selected wallpaper.
 * @param filesDir Used for loading wallpaper images from disk.
 * @param appStore Will receive dispatches of metadata updates like the currently selected wallpaper.
 * @param client Handles downloading wallpapers and their metadata.
 * @param storageRootDirectory The top level app-local storage directory.
 * @param currentLocale The locale currently being used on the device.
 * @param getDisplaySize Returns the current size of the display, in pixels, that a full screen wallpaper has to cover.
 */
class WallpapersUseCases(
    settings: Settings,
    filesDir: File,
    appStore: AppStore,
    client: Client,
    storageRootDirectory: File,
    currentLocale: String,
    private val getDisplaySize: () -> Size,
) {
    private val downloader = WallpaperDownloader(storageRootDirectory, client)
    private val fileManager = WallpaperFileManager(storageRootDirectory)

    val fetchCurrentWallpaperUseCase: FetchCurrentWallpaperUseCase by lazy {
        DefaultFetchCurrentWallpaperUseCase(settings, appStore)
    }

    // Use case for initializing wallpaper feature. Should usually be called early
    // in the app's lifetime to ensure that any potential long-running tasks can complete quickly.
    val initialize: InitializeWallpapersUseCase by lazy {
        val metadataFetcher = WallpaperMetadataFetcher(client)
        val migrationHelper =
            LegacyWallpaperMigration(
                storageRootDirectory = storageRootDirectory,
                settings = settings,
                selectWallpaper::invoke,
            )
        DefaultInitializeWallpaperUseCase(
            appStore = appStore,
            downloader = downloader,
            fileManager = fileManager,
            metadataFetcher = metadataFetcher,
            migrationHelper = migrationHelper,
            settings = settings,
            currentLocale = currentLocale,
        )
    }

    // Use case for loading specific wallpaper bitmaps.
    val loadBitmap: LoadBitmapUseCase by lazy {
        DefaultLoadBitmapUseCase(getFilesDir = { filesDir }, getDisplaySize = getDisplaySize)
    }

    val loadThumbnail: LoadThumbnailUseCase by lazy {
        DefaultLoadThumbnailUseCase(storageRootDirectory)
    }

    // Use case for selecting a new wallpaper.
    val selectWallpaper: SelectWallpaperUseCase by lazy {
        DefaultSelectWallpaperUseCase(settings, appStore, fileManager, downloader)
    }

    /** Contract for use cases that retrieve the user's currently selected wallpaper. */
    interface FetchCurrentWallpaperUseCase {
        /** Start operation to retrieve user's currently selected wallpaper. */
        suspend operator fun invoke()
    }

    internal class DefaultFetchCurrentWallpaperUseCase(
        private val settings: Settings,
        private val appStore: AppStore,
    ) : FetchCurrentWallpaperUseCase {
        override suspend fun invoke() {
            Wallpaper.getCurrentWallpaperFromSettings(settings)?.let {
                appStore.dispatch(AppAction.WallpaperAction.UpdateCurrentWallpaper(it))
            }
        }
    }

    /** Contract for usecases that initialize the wallpaper feature. */
    interface InitializeWallpapersUseCase {
        /**
         * Start operations that should be down during initialization, like remote metadata retrieval and determining
         * the currently selected wallpaper.
         */
        suspend operator fun invoke()
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal class DefaultInitializeWallpaperUseCase(
        private val appStore: AppStore,
        private val downloader: WallpaperDownloader,
        private val fileManager: WallpaperFileManager,
        private val metadataFetcher: WallpaperMetadataFetcher,
        private val migrationHelper: LegacyWallpaperMigration,
        private val settings: Settings,
        private val currentLocale: String,
    ) : InitializeWallpapersUseCase {
        override suspend fun invoke() {
            val currentWallpaperName =
                if (settings.shouldMigrateLegacyWallpaper) {
                    val migratedWallpaperName = migrationHelper.migrateLegacyWallpaper(settings.currentWallpaperName)
                    settings.currentWallpaperName = migratedWallpaperName
                    settings.shouldMigrateLegacyWallpaper = false
                    migratedWallpaperName
                } else {
                    settings.currentWallpaperName
                }

            if (settings.shouldMigrateLegacyWallpaperCardColors) {
                migrationHelper.migrateExpiredWallpaperCardColors()
            }

            val possibleWallpapers =
                metadataFetcher.downloadWallpaperList().filter {
                    !it.isExpired() && it.isAvailableInLocale()
                }
            val currentWallpaper =
                possibleWallpapers.find { it.name == currentWallpaperName }
                    ?: fileManager.lookupExpiredWallpaper(settings)
                    ?: Wallpaper.getCurrentWallpaperFromSettings(settings)
                    ?: Wallpaper.Default

            // Dispatching this early will make it accessible to the home screen ASAP. This may have
            // been dispatched by FetchCurrentWallpaperUseCase, but this could include additional metadata.
            appStore.dispatch(AppAction.WallpaperAction.UpdateCurrentWallpaper(currentWallpaper))

            fileManager.clean(
                currentWallpaper,
                possibleWallpapers,
            )

            val wallpapersWithUpdatedThumbnailState = possibleWallpapers.map { wallpaper ->
                val result = downloader.downloadThumbnail(wallpaper)
                wallpaper.copy(thumbnailFileState = result)
            }

            val defaultIncluded = defaultWallpapers + wallpapersWithUpdatedThumbnailState
            appStore.dispatch(AppAction.WallpaperAction.UpdateAvailableWallpapers(defaultIncluded))
        }

        private val defaultWallpapers: List<Wallpaper> =
            if (settings.enableHomepageEdgeToEdgeBackgroundFeature) {
                listOf(Wallpaper.EdgeToEdge, Wallpaper.Default)
            } else {
                listOf(Wallpaper.Default)
            }

        private fun Wallpaper.isExpired(): Boolean =
            when (this) {
                Wallpaper.Default -> false
                else -> {
                    val expired = this.collection.endDate?.let { Date().after(it) } ?: false
                    expired && this.name != settings.currentWallpaperName
                }
            }

        private fun Wallpaper.isAvailableInLocale(): Boolean =
            this.collection.availableLocales?.contains(currentLocale) ?: true
    }

    /** Contract for usecase for loading bitmaps related to a specific wallpaper. */
    interface LoadBitmapUseCase {
        /**
         * Load the bitmap for a [wallpaper], if available.
         *
         * @param wallpaper The wallpaper to load a bitmap for.
         * @param orientation The orientation of wallpaper.
         */
        suspend operator fun invoke(wallpaper: Wallpaper, orientation: Int): Bitmap?
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal class DefaultLoadBitmapUseCase(
        private val getFilesDir: suspend () -> File,
        private val getDisplaySize: () -> Size,
    ) : LoadBitmapUseCase {
        override suspend fun invoke(wallpaper: Wallpaper, orientation: Int): Bitmap? =
            loadWallpaperFromDisk(wallpaper, orientation)

        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        internal suspend fun loadWallpaperFromDisk(
            wallpaper: Wallpaper,
            orientation: Int,
        ): Bitmap? =
            try {
                val path = wallpaper.getLocalPathFromContext(orientation)
                withContext(Dispatchers.IO) {
                    val file = File(getFilesDir(), path)
                    val target = getDisplaySize().orientedTo(orientation)
                    file.toSampledBitmap(targetWidth = target.width, targetHeight = target.height)
                }
            } catch (e: CancellationException) {
                // CancellationException must not be swallowed: if the coroutine was canceled while loading,
                // rethrowing ensures the cancellation propagates and callers won't treat a null result as a
                // load failure.
                throw e
            } catch (_: Exception) {
                null
            }

        /**
         * Returns this size with its longer edge assigned to match [orientation]. The asset that gets loaded is chosen
         * from [orientation], but the display metrics are the device's current ones, so the two can disagree in split
         * screen or mid-rotation. Without this the target would be transposed relative to the image and no subsampling
         * would happen.
         */
        private fun Size.orientedTo(orientation: Int): Size {
            val longEdge = maxOf(width, height)
            val shortEdge = minOf(width, height)
            return if (orientation == Configuration.ORIENTATION_LANDSCAPE) {
                Size(longEdge, shortEdge)
            } else {
                Size(shortEdge, longEdge)
            }
        }

        /**
         * Get the expected local path on disk for a wallpaper. This will differ depending on orientation and app theme.
         */
        private fun Wallpaper.getLocalPathFromContext(orientation: Int): String {
            val orientationWallpaper =
                if (orientation == Configuration.ORIENTATION_LANDSCAPE) {
                    Wallpaper.ImageType.Landscape
                } else {
                    Wallpaper.ImageType.Portrait
                }
            return Wallpaper.getLocalPath(name, orientationWallpaper)
        }
    }

    /** Contract for usecase for loading thumbnail bitmaps related to a specific wallpaper. */
    interface LoadThumbnailUseCase {
        /**
         * Load the bitmap for a [wallpaper] thumbnail, if available.
         *
         * @param wallpaper The wallpaper to load a thumbnail for.
         * @param targetSize The size, in pixels, the thumbnail is drawn at, which it is subsampled towards.
         */
        suspend operator fun invoke(wallpaper: Wallpaper, targetSize: Size): Bitmap?
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal class DefaultLoadThumbnailUseCase(private val filesDir: File) : LoadThumbnailUseCase {
        override suspend fun invoke(wallpaper: Wallpaper, targetSize: Size): Bitmap? =
            try {
                withContext(Dispatchers.IO) {
                    val path = Wallpaper.getLocalPath(wallpaper.name, Wallpaper.ImageType.Thumbnail)
                    val file = File(filesDir, path)
                    file.toSampledBitmap(targetWidth = targetSize.width, targetHeight = targetSize.height)
                }
            } catch (e: CancellationException) {
                throw e
            } catch (_: Exception) {
                null
            }
    }

    /** Contract for usecase of selecting a new wallpaper. */
    interface SelectWallpaperUseCase {
        /**
         * Select a new wallpaper.
         *
         * @param wallpaper The selected wallpaper.
         */
        suspend operator fun invoke(wallpaper: Wallpaper): Wallpaper.ImageFileState
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal class DefaultSelectWallpaperUseCase(
        private val settings: Settings,
        private val appStore: AppStore,
        private val fileManager: WallpaperFileManager,
        private val downloader: WallpaperDownloader,
    ) : SelectWallpaperUseCase {
        /**
         * Select a new wallpaper. Storage and the app store will be updated appropriately.
         *
         * @param wallpaper The selected wallpaper.
         */
        override suspend fun invoke(wallpaper: Wallpaper): Wallpaper.ImageFileState {
            return if (
                wallpaper.collection == Wallpaper.DefaultCollection || fileManager.wallpaperImagesExist(wallpaper)
            ) {
                selectWallpaper(wallpaper)
                dispatchDownloadState(wallpaper, Wallpaper.ImageFileState.Downloaded)
                Wallpaper.ImageFileState.Downloaded
            } else {
                dispatchDownloadState(wallpaper, Wallpaper.ImageFileState.Downloading)
                val result = downloader.downloadWallpaper(wallpaper)
                dispatchDownloadState(wallpaper, result)
                if (result == Wallpaper.ImageFileState.Downloaded) {
                    selectWallpaper(wallpaper)
                }
                result
            }
        }

        @VisibleForTesting
        internal fun selectWallpaper(wallpaper: Wallpaper) {
            settings.currentWallpaperName = wallpaper.name
            settings.currentWallpaperTextColor = wallpaper.textColor ?: 0L
            settings.currentWallpaperCardColorLight = wallpaper.cardColorLight ?: 0L
            settings.currentWallpaperCardColorDark = wallpaper.cardColorDark ?: 0L
            appStore.dispatch(AppAction.WallpaperAction.UpdateCurrentWallpaper(wallpaper))
        }

        private fun dispatchDownloadState(wallpaper: Wallpaper, downloadState: Wallpaper.ImageFileState) {
            appStore.dispatch(AppAction.WallpaperAction.UpdateWallpaperDownloadState(wallpaper, downloadState))
        }
    }
}
