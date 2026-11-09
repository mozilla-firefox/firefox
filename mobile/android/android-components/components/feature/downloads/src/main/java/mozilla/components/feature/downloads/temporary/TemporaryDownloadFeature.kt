/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.downloads.temporary

import android.content.Context
import android.webkit.MimeTypeMap
import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PROTECTED
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.URLConnection
import kotlin.math.absoluteValue
import kotlin.random.Random
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Headers
import mozilla.components.concept.fetch.Headers.Names.CONTENT_DISPOSITION
import mozilla.components.concept.fetch.Headers.Names.CONTENT_TYPE
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.ifNullOrEmpty
import mozilla.components.support.ktx.kotlin.sanitizeFileName
import mozilla.components.support.ktx.kotlin.sanitizeURL
import mozilla.components.support.utils.DownloadUtils

/** Default mime type for base64 images data URLs not containing the media type. */
@VisibleForTesting internal const val DEFAULT_IMAGE_EXTENSION = "jpg"

/**
 * Subdirectory of Context.getCacheDir() where the resources to be shared are stored.
 *
 * Location must be kept in sync with the paths our FileProvider can share from.
 */
@VisibleForTesting internal var cacheDirName = "mozac_share_cache"

/**
 * Base class for downloading resources from the internet and storing them in a temporary cache.
 *
 * @property context Android context used for various platform interactions.
 * @property httpClient Client used for downloading internet resources.
 * @property ioDispatcher Coroutine dispatcher used for IO operations like the download operation and cleanup of old
 *   cached files. Defaults to IO.
 */
abstract class TemporaryDownloadFeature(
    private val context: Context,
    private val httpClient: Client,
    private val ioDispatcher: CoroutineDispatcher = IO,
) : LifecycleAwareFeature {

    val logger = Logger("TemporaryDownloadFeature")

    /**
     * At most time to allow for the file to be downloaded and action to be performed.
     *
     * This is used in the subclasses when implementing timeout
     */
    protected val operationTimeoutMs: Long = 20 * 1000L

    @VisibleForTesting(otherwise = VisibleForTesting.PROTECTED) var scope: CoroutineScope? = null

    override fun stop() {
        scope?.cancel()
    }

    init {
        CoroutineScope(ioDispatcher).launch(coroutineExceptionHandler("Cache cleanup")) {
            cleanupCache()
        }
    }

    @VisibleForTesting(otherwise = PROTECTED)
    internal suspend fun download(internetResource: ShareResourceState.InternetResource): File {
        return withContext(ioDispatcher) {
            val request =
                Request(
                    internetResource.url.sanitizeURL(),
                    private = internetResource.private,
                    referrerUrl = internetResource.referrerUrl,
                )
            val response =
                if (internetResource.response == null) {
                    httpClient.fetch(request)
                } else {
                    requireNotNull(internetResource.response)
                }

            persistResponseToTempFile(response)
        }
    }

    @VisibleForTesting(otherwise = PROTECTED)
    internal suspend fun downloadDirectResource(response: Response): File =
        withContext(ioDispatcher) {
            val fileName =
                DownloadUtils.extractFileNameFromUrlOrNull(
                        contentDisposition = response.headers[CONTENT_DISPOSITION],
                        url = null,
                    )
                    ?.sanitizeFileName()
            persistResponseToTempFile(response, fileName)
        }

    private fun persistResponseToTempFile(response: Response, fileName: String? = null): File {
        if (response.status != Response.SUCCESS) {
            response.close()
            // We experienced a problem trying to fetch the file, nothing more we can do.
            throw (RuntimeException("Resource is not available to download"))
        }

        var tempFile: File? = null
        response.body.useStream { input ->
            val fileExtension = '.' + getFileExtension(response.headers, input)
            tempFile =
                if (fileName == null) {
                    getTempFile(fileExtension)
                } else {
                    getNamedTempFile(fileName) ?: getTempFile(fileExtension)
                }
            FileOutputStream(tempFile).use { output -> input.copyTo(output) }
        }

        return tempFile!!
    }

    @VisibleForTesting
    internal fun getFilename(fileExtension: String) = Random.nextInt().absoluteValue.toString() + fileExtension

    @VisibleForTesting
    internal fun getTempFile(fileExtension: String) = File(getMediaShareCacheDirectory(), getFilename(fileExtension))

    private fun getNamedTempFile(fileName: String): File? {
        val randomFolderName = Random.nextInt().absoluteValue.toString()
        val folder = File(getMediaShareCacheDirectory(), randomFolderName)
        folder.mkdirs()
        val file = File(folder, fileName)
        val isCreated =
            try {
                file.createNewFile()
            } catch (_: IOException) {
                logger.debug("Could not create a file with the provided name!")
                false
            }
        if (isCreated) {
            return file
        }
        folder.delete()
        return null
    }

    @VisibleForTesting internal fun getCacheDirectory() = File(context.cacheDir, cacheDirName)

    @VisibleForTesting
    internal fun getFileExtension(responseHeaders: Headers, responseStream: InputStream): String {
        val mimeType = URLConnection.guessContentTypeFromStream(responseStream) ?: responseHeaders[CONTENT_TYPE]

        return MimeTypeMap.getSingleton().getExtensionFromMimeType(mimeType).ifNullOrEmpty { DEFAULT_IMAGE_EXTENSION }
    }

    @VisibleForTesting
    internal fun getMediaShareCacheDirectory(): File {
        val mediaShareCacheDir = getCacheDirectory()
        if (!mediaShareCacheDir.exists()) {
            mediaShareCacheDir.mkdirs()
        }
        return mediaShareCacheDir
    }

    @VisibleForTesting
    internal fun cleanupCache() {
        logger.debug("Deleting previous cache of shared files")
        // Every entry is attempted; one that cannot be deleted must not strand the rest.
        val failures = getCacheDirectory().listFiles()?.count { !it.deleteCacheEntry() } ?: 0
        if (failures > 0) {
            logger.debug("Cleanup incomplete: some files were locked or could not be removed.")
        }
    }

    // Sibling features share this directory. deleteRecursively() asserts on an entry another
    // cleanup removed mid-walk instead of reporting a failure.
    private fun File.deleteCacheEntry() =
        try {
            deleteRecursively()
        } catch (_: AssertionError) {
            !exists()
        }

    protected fun coroutineExceptionHandler(action: String) = CoroutineExceptionHandler { _, throwable ->
        when (throwable) {
            is InterruptedException -> {
                logger.warn("$action failed: operation timeout reached")
            }

            is IOException,
            is RuntimeException -> {
                logger.warn("$action failed: $throwable")
            }

            else -> {
                logger.warn("$action failed unexpectedly: $throwable")
            }
        }
    }
}
