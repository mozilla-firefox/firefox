/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.wallpapers

import java.io.File
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.isSuccess
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.wallpapers.Wallpaper.Companion.getLocalPath

/**
 * Can download wallpapers from a remote host.
 *
 * @param storageRootDirectory The top level app-local storage directory.
 * @param client Required for fetching files from network.
 * @param dispatcher Dispatcher used to execute suspending functions. Default parameter should be likely be used except
 *   for when under test.
 */
class WallpaperDownloader(
    private val storageRootDirectory: File,
    private val client: Client,
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    private val remoteHost = BuildConfig.WALLPAPER_URL

    /**
     * Downloads a wallpaper from the network. Will try to fetch 2 versions of each wallpaper: portrait and landscape.
     * These are expected to be found at a remote path in the form: <WALLPAPER_URL>/<collection name>/<wallpaper
     * name>/<orientation>.png and will be stored in the local path: wallpapers/<wallpaper name>/<orientation>.png
     */
    suspend fun downloadWallpaper(wallpaper: Wallpaper): Wallpaper.ImageFileState =
        withContext(dispatcher) {
            val portraitResult = downloadAsset(wallpaper, Wallpaper.ImageType.Portrait)
            val landscapeResult = downloadAsset(wallpaper, Wallpaper.ImageType.Landscape)
            return@withContext if (
                portraitResult == Wallpaper.ImageFileState.Downloaded &&
                    landscapeResult == Wallpaper.ImageFileState.Downloaded
            ) {
                Wallpaper.ImageFileState.Downloaded
            } else {
                Wallpaper.ImageFileState.Error
            }
        }

    /**
     * Downloads a thumbnail for a wallpaper from the network. This is expected to be found remotely at:
     * <WALLPAPER_URL>/<collection name>/<wallpaper name>/thumbnail.png and stored locally at: wallpapers/<wallpaper
     * name>/thumbnail.png
     */
    suspend fun downloadThumbnail(wallpaper: Wallpaper): Wallpaper.ImageFileState =
        withContext(dispatcher) {
            downloadAsset(wallpaper, Wallpaper.ImageType.Thumbnail)
        }

    private suspend fun downloadAsset(
        wallpaper: Wallpaper,
        imageType: Wallpaper.ImageType,
    ): Wallpaper.ImageFileState =
        withContext(dispatcher) {
            val localFile = File(storageRootDirectory, getLocalPath(wallpaper.name, imageType))
            if (localFile.exists()) {
                return@withContext Wallpaper.ImageFileState.Downloaded
            }

            val remotePath = "${wallpaper.collection.name}/${wallpaper.name}/${imageType.lowercase()}.png"
            val request =
                Request(
                    url = "$remoteHost/$remotePath",
                    method = Request.Method.GET,
                    conservative = true,
                )

            return@withContext Result.runCatching {
                    val response = client.fetch(request)
                    if (!response.isSuccess) {
                        response.close()
                        throw IllegalStateException()
                    }
                    localFile.parentFile?.mkdirs()
                    response.body.useStream { input ->
                        input.copyTo(localFile.outputStream())
                    }
                    Wallpaper.ImageFileState.Downloaded
                }
                .getOrElse {
                    // This should clean up any partial downloads
                    localFile.delete()
                    Wallpaper.ImageFileState.Error
                }
        }
}
