/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.engine

/** Interface defining a delegate for handling download-related operations within the engine. */
interface DownloadDelegate {

    /**
     * Determines a filename based on the given criteria. Will generate a filename if the actual value cannot be found.
     *
     * @param contentDisposition The 'Content-Disposition' HTTP header value, if available.
     * @param url The full URL of the resource being downloaded.
     * @param mimeType The 'Content-Type' (MIME type) of the resource, if available.
     * @return A string representing the suggested filename for the downloaded file.
     */
    fun guessFileName(
        contentDisposition: String?,
        url: String?,
        mimeType: String?,
    ): String

    /**
     * Finds the filename a content provider has for a `content://` URI.
     *
     * @param uri The URI to look up, such as the URL of a tab showing a local file.
     * @return The filename the content provider has, or null if [uri] is not a content URI or it has no name.
     */
    fun getContentFileName(uri: String?): String? = null
}
