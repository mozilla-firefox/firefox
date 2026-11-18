/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.images.fetcher

import java.io.ByteArrayOutputStream
import java.io.IOException
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Headers
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.concept.fetch.isSuccess

/** Fetches image responses with an optional download byte limit. */
interface ImageFetcher {
    /** Fetches [request] using the limits in [config]. */
    fun fetch(request: Request, config: ImageFetcherConfig): Result<ByteArray>

    companion object {

        /** Creates an [ImageFetcher] that uses [client] */
        fun fromClient(client: Client): ImageFetcher {
            return DefaultImageFetcher(client)
        }
    }
}

internal class DefaultImageFetcher(private val client: Client) : ImageFetcher {

    override fun fetch(request: Request, config: ImageFetcherConfig): Result<ByteArray> {
        return try {
            client.fetch(request).use { response ->
                if (!response.isSuccess) {
                    throw ImageFetchException(status = response.status)
                }
                Result.success(response.read(config.maxSizeBytes))
            }
        } catch (exception: ImageTooLargeException) {
            Result.failure(exception)
        } catch (exception: ImageFetchException) {
            Result.failure(exception)
        } catch (exception: IOException) {
            Result.failure(exception)
        }
    }
}

internal const val STREAM_BUFFER_SIZE = DEFAULT_BUFFER_SIZE

/**
 * Reads the response body into a byte array.
 *
 * @throws ImageTooLargeException when the response exceeds [maxSize] bytes.
 */
@Throws(ImageTooLargeException::class)
private fun Response.read(maxSize: Long?): ByteArray {
    val contentLength = headers[Headers.Names.CONTENT_LENGTH]?.toLongOrNull()
    if (maxSize != null && contentLength != null && contentLength > maxSize) {
        throw ImageTooLargeException(contentLength, maxSize)
    }

    return body.useStream { inputStream ->
        val buffer = ByteArray(STREAM_BUFFER_SIZE)
        var bytesRead = 0L

        val streamInitialCapacity = maxSize?.toInt() ?: contentLength?.toInt() ?: STREAM_BUFFER_SIZE
        ByteArrayOutputStream(streamInitialCapacity.coerceAtLeast(0)).use { outputStream ->
            while (true) {
                val chunkSizeBytes = inputStream.read(buffer)
                if (chunkSizeBytes == -1) {
                    break
                }

                val nextSize = bytesRead + chunkSizeBytes
                if (maxSize != null && nextSize > maxSize) {
                    throw ImageTooLargeException(nextSize, maxSize)
                }

                outputStream.write(buffer, 0, chunkSizeBytes)
                bytesRead = nextSize
            }
            outputStream.toByteArray()
        }
    }
}
