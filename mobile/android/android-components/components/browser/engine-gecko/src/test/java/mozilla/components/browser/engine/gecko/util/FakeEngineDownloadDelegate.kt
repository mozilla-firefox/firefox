/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.util

import mozilla.components.concept.engine.DownloadDelegate

/** A fake implementation of [DownloadDelegate] for testing purposes. */
class FakeEngineDownloadDelegate(
    private val guessFileName:
        (
            contentDisposition: String?,
            url: String?,
            mimeType: String?,
        ) -> String =
        { _, _, _ ->
            "fileName"
        },
    private val getContentFileName: (uri: String?) -> String? = { null },
) : DownloadDelegate {
    override fun guessFileName(
        contentDisposition: String?,
        url: String?,
        mimeType: String?,
    ): String {
        return guessFileName.invoke(
            contentDisposition,
            url,
            mimeType,
        )
    }

    override fun getContentFileName(uri: String?): String? = getContentFileName.invoke(uri)
}
