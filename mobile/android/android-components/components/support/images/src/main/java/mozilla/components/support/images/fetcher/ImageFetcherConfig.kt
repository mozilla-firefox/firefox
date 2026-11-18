/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.images.fetcher

/**
 * Configuration for [ImageFetcher].
 *
 * @property maxSizeBytes Maximum response size in bytes, or null for no limit.
 */
data class ImageFetcherConfig(val maxSizeBytes: Long? = null)
