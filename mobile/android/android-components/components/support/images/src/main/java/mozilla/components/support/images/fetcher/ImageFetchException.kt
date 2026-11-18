/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.images.fetcher

/**
 * An exception reported when the response from the [mozilla.components.concept.fetch.Client] is not within the success
 * status range - [mozilla.components.concept.fetch.Response.SUCCESS_STATUS_RANGE].
 *
 * @property status The response status
 */
class ImageFetchException(val status: Int) : RuntimeException("Image fetch failed with status $status")
