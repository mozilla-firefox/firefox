/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.images.compose.loader

import android.graphics.Bitmap
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Request
import mozilla.components.support.images.DesiredSize
import mozilla.components.support.images.decoder.AndroidImageDecoder
import mozilla.components.support.images.fetcher.ImageFetcher
import mozilla.components.support.images.fetcher.ImageFetcherConfig

/**
 * Loads an image from [url] using [Client] and makes it available in the inner [ImageLoaderScope].
 *
 * The loaded image will be available via the [WithImage] composable. While the image is still loading [Placeholder]
 * will get rendered. If loading or decoding the image failed then [Fallback] will get rendered.
 *
 * @param client The [Client] implementation that should be used for loading the image.
 * @param url The URL of the image that should get loaded.
 * @param private Whether or not this is a private request. Like in private browsing mode, private requests will not
 *   cache anything on disk and not send any cookies shared with the browser.
 * @param connectTimeout A timeout to be used when connecting to the remote server. If the timeout expires before the
 *   connection can be established then [Fallback] will get rendered.
 * @param readTimeout A timeout to be used when reading from the remote server. If the timeout expires before there is
 *   data available for read then [Fallback] will get rendered.
 * @param targetSize The target image size, the loaded image should be scaled to.
 * @param minSize The minimum size before an image will be considered too small and [Fallback] will get rendered
 *   instead.
 * @param maxSize The maximum size before an image will be considered too large and [Fallback] will get rendered
 *   instead.
 * @param maxScaleFactor The maximum factor a loaded image will be scaled up or down by until [Fallback] will get
 *   rendered.
 */
@Composable
fun ImageLoader(
    client: Client,
    url: String,
    private: Boolean = true,
    connectTimeout: Pair<Long, TimeUnit> = Pair(DEFAULT_CONNECT_TIMEOUT, TimeUnit.SECONDS),
    readTimeout: Pair<Long, TimeUnit> = Pair(DEFAULT_READ_TIMEOUT, TimeUnit.SECONDS),
    targetSize: Dp = defaultTargetSize,
    minSize: Dp = targetSize / DEFAULT_MIN_MAX_MULTIPLIER,
    maxSize: Dp = targetSize * DEFAULT_MIN_MAX_MULTIPLIER,
    maxScaleFactor: Float = DEFAULT_MAXIMUM_SCALE_FACTOR,
    content: @Composable ImageLoaderScope.() -> Unit,
) {
    val desiredSize =
        with(LocalDensity.current) {
            DesiredSize(
                targetSize = targetSize.roundToPx(),
                minSize = minSize.roundToPx(),
                maxSize = maxSize.roundToPx(),
                maxScaleFactor = maxScaleFactor,
            )
        }

    val scope =
        remember(url) {
            InternalImageLoaderScope(
                client,
                url,
                private,
                connectTimeout,
                readTimeout,
                desiredSize,
            )
        }

    LaunchedEffect(scope.url) {
        scope.load()
    }

    scope.content()
}

private suspend fun InternalImageLoaderScope.load() {
    val bitmap =
        withContext(Dispatchers.IO) {
            fetchAndDecode(
                client,
                url,
                private,
                connectTimeout,
                readTimeout,
                desiredSize,
            )
        }

    if (bitmap != null) {
        loaderState.value = ImageLoaderState.Image(BitmapPainter(bitmap.asImageBitmap()))
    } else {
        loaderState.value = ImageLoaderState.Failed
    }
}

private suspend fun fetchAndDecode(
    client: Client,
    url: String,
    private: Boolean,
    connectTimeout: Pair<Long, TimeUnit>,
    readTimeout: Pair<Long, TimeUnit>,
    desiredSize: DesiredSize,
): Bitmap? =
    withContext(Dispatchers.IO) {
        val data =
            fetch(
                client,
                url,
                private,
                connectTimeout,
                readTimeout,
            ) ?: return@withContext null

        decoders.firstNotNullOfOrNull { decoder ->
            decoder.decode(data, desiredSize)
        }
    }

private fun fetch(
    client: Client,
    url: String,
    private: Boolean,
    connectTimeout: Pair<Long, TimeUnit>,
    readTimeout: Pair<Long, TimeUnit>,
): ByteArray? {
    val request =
        Request(
            url = url.trim(),
            method = Request.Method.GET,
            cookiePolicy =
                if (private) {
                    Request.CookiePolicy.OMIT
                } else {
                    Request.CookiePolicy.INCLUDE
                },
            connectTimeout = connectTimeout,
            readTimeout = readTimeout,
            redirect = Request.Redirect.FOLLOW,
            useCaches = true,
            private = private,
            conservative = true,
        )

    return ImageFetcher.fromClient(client)
        .fetch(
            request = request,
            config = ImageFetcherConfig(maxSizeBytes = MAX_DOWNLOAD_BYTES),
        )
        .getOrNull()
}

private const val MAX_DOWNLOAD_BYTES = 1024 * 1024L
private const val DEFAULT_CONNECT_TIMEOUT = 2L // Seconds
private const val DEFAULT_READ_TIMEOUT = 10L // Seconds
private const val DEFAULT_MAXIMUM_SCALE_FACTOR = 2.0f
private const val DEFAULT_MIN_MAX_MULTIPLIER = 3

private val defaultTargetSize = 100.dp

private val decoders = listOf(AndroidImageDecoder())
