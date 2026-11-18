/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.images.fetcher

/**
 * An exception thrown when the image requested is too large. Specifics of what is considered too large depends on the
 * byte limit passed to [ImageFetcher.fetch].
 *
 * @property actualSizeBytes Actual size we tried to download.
 * @property maxDownloadBytes Allowed max image download size.
 * @property message Error message.
 */
class ImageTooLargeException
private constructor(
    internal val actualSizeBytes: Long,
    internal val maxDownloadBytes: Long,
    override val message: String,
) : RuntimeException(message, null) {

    /** @see ImageTooLargeException */
    internal constructor(
        actualSizeBytes: Long,
        maxDownloadBytes: Long,
    ) : this(
        actualSizeBytes = actualSizeBytes,
        maxDownloadBytes = maxDownloadBytes,
        message = "Requested image is too large. Size is $actualSizeBytes, max allowed is $maxDownloadBytes",
    )
}
