/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.state.content

import mozilla.components.concept.fetch.Response

/**
 * Value type that represents a resource selected to be shared.
 *
 * @property url The full url to the content that should be shared.
 * @property contentType Content type (MIME type) to indicate the type of the resource being shared.
 */
sealed class ShareResourceState(
    open val url: String,
    open val contentType: String? = null,
) {
    /**
     * Value type that represents an Internet resource selected to be shared.
     *
     * @property url The full url to the content that should be shared.
     * @property contentType Content type (MIME type) to indicate the media type of the resource.
     * @property private Indicates if the share operation initiated from a private session.
     * @property response A response object associated with this request, when provided can be
     * used instead of performing a manual a download.
     * @property referrerUrl An optional url of the referrer.
     */
    data class InternetResource(
        override val url: String,
        override val contentType: String? = null,
        val private: Boolean = false,
        val response: Response? = null,
        val referrerUrl: String? = null,
    ) : ShareResourceState(url, contentType)

    /**
     * Value type that represents a local resource selected to be shared.
     *
     * @property url The full url to the content that should be shared.
     * @property contentType Content type (MIME type) to indicate the media type of the resource.
     */
    data class LocalResource(
        override val url: String,
        override val contentType: String? = null,
    ) : ShareResourceState(url, contentType)
}
