/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.ktx.android.graphics

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.annotation.Px
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import mozilla.components.support.base.log.logger.Logger

private val logger = Logger("SampledBitmap")

/**
 * Decodes this file into a [Bitmap], subsampling it as part of the decode so that a full resolution bitmap is never
 * allocated. The file is opened twice: once to read the image bounds and once for the actual decode.
 *
 * The result is the largest subsample whose dimensions are still at least [targetWidth] x [targetHeight], so it is
 * never smaller than requested and callers can scale it the rest of the way down for display without visible quality
 * loss.
 *
 * @param targetWidth The minimum width, in pixels, the decoded bitmap should have.
 * @param targetHeight The minimum height, in pixels, the decoded bitmap should have.
 * @return the decoded [Bitmap], or null if the file could not be read or decoded.
 */
fun File.toSampledBitmap(
    @Px targetWidth: Int,
    @Px targetHeight: Int,
): Bitmap? =
    decodeSampled(targetWidth, targetHeight) { options ->
        inputStream().use { BitmapFactory.decodeStream(it, null, options) }
    }

/**
 * Decodes these bytes into a [Bitmap], subsampling them as part of the decode so that a full resolution bitmap is never
 * allocated.
 *
 * The result is the largest subsample whose dimensions are still at least [targetWidth] x [targetHeight], so it is
 * never smaller than requested and callers can scale it the rest of the way down for display without visible quality
 * loss.
 *
 * @param targetWidth The minimum width, in pixels, the decoded bitmap should have.
 * @param targetHeight The minimum height, in pixels, the decoded bitmap should have.
 * @return the decoded [Bitmap], or null if the data could not be decoded.
 */
internal fun ByteArray.toSampledBitmap(
    @Px targetWidth: Int,
    @Px targetHeight: Int,
): Bitmap? =
    decodeSampled(targetWidth, targetHeight) { options ->
        BitmapFactory.decodeByteArray(this, 0, size, options)
    }

/**
 * Decodes this stream into a [Bitmap], subsampling it as part of the decode so that a full resolution bitmap is never
 * allocated.
 *
 * A stream can only be read once, so its contents are buffered in order to read the image bounds before decoding.
 * [maxBytes] bounds that buffer: a stream carrying more than that is rejected rather than read into memory, which
 * matters when the stream is a response from the network. Prefer [File.toSampledBitmap] when the image is already on
 * disk, as that avoids buffering.
 *
 * This does not close the stream.
 *
 * @param targetWidth The minimum width, in pixels, the decoded bitmap should have.
 * @param targetHeight The minimum height, in pixels, the decoded bitmap should have.
 * @param maxBytes The largest image, in bytes, that will be read from the stream. Callers pick this, since only they
 *   know what their source is worth reading; a stream over it is rejected outright rather than truncated, and one at or
 *   below it costs a small multiple of its size to buffer before the decode. A limit of zero or less reads nothing.
 * @return the decoded [Bitmap], or null if the stream could not be read or decoded, or carried more than [maxBytes].
 */
fun InputStream.toSampledBitmap(
    @Px targetWidth: Int,
    @Px targetHeight: Int,
    maxBytes: Int,
): Bitmap? {
    val bytes =
        try {
            readAtMost(maxBytes)
        } catch (e: IOException) {
            logger.warn("Failed to read image data", e)
            null
        } catch (e: OutOfMemoryError) {
            logger.warn("OutOfMemoryError while buffering image data", e)
            null
        } ?: return null

    return bytes.toSampledBitmap(targetWidth, targetHeight)
}

/** Reads up to [max] bytes from this stream, or returns null if it carries more than that. */
private fun InputStream.readAtMost(max: Int): ByteArray? {
    val out = ByteArrayOutputStream()
    val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
    var total = 0

    while (true) {
        val read = read(buffer)
        if (read < 0) {
            return out.toByteArray()
        }
        total += read
        if (total > max) {
            logger.warn("Refusing to decode an image larger than $max bytes")
            return null
        }
        out.write(buffer, 0, read)
    }
}

/**
 * Runs [decode] twice, first to read the image bounds and then to produce a subsampled bitmap.
 *
 * [decode] must be able to produce the same image on each call, which is why callers that read from a stream have to
 * reopen it rather than reuse a single one.
 */
private fun decodeSampled(
    @Px targetWidth: Int,
    @Px targetHeight: Int,
    decode: (BitmapFactory.Options) -> Bitmap?,
): Bitmap? {
    return try {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        decode(bounds)?.recycle()

        // A bounds pass always returns a null bitmap, so the decoded dimensions are what tell
        // us whether the image could be read.
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
            return null
        }

        val options =
            BitmapFactory.Options().apply {
                inSampleSize =
                    computeSampleSize(
                        width = bounds.outWidth,
                        height = bounds.outHeight,
                        targetWidth = targetWidth,
                        targetHeight = targetHeight,
                    )
            }
        decode(options)
    } catch (e: IOException) {
        logger.warn("Failed to read image data", e)
        null
    } catch (e: OutOfMemoryError) {
        logger.warn("OutOfMemoryError while decoding image data", e)
        null
    }
}

/** Returns the largest power-of-two sample size that leaves the image at least [targetWidth] x [targetHeight]. */
private fun computeSampleSize(
    @Px width: Int,
    @Px height: Int,
    @Px targetWidth: Int,
    @Px targetHeight: Int,
): Int {
    if (targetWidth <= 0 || targetHeight <= 0) {
        return 1
    }

    var sampleSize = 1
    while (width / (sampleSize * 2) >= targetWidth && height / (sampleSize * 2) >= targetHeight) {
        sampleSize *= 2
    }
    return sampleSize
}
