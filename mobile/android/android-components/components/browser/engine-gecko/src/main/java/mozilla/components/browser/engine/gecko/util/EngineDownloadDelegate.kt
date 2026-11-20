/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package mozilla.components.browser.engine.gecko.util

import android.content.Context
import androidx.core.net.toUri
import mozilla.components.concept.engine.DownloadDelegate
import mozilla.components.support.ktx.android.net.getFileNameOrNullForContentUris
import mozilla.components.support.ktx.kotlin.isContentUrl
import mozilla.components.support.utils.DefaultDownloadFileUtils

/** This class acts as a bridge between the engine's download requests and the application's file utility logic. */
class EngineDownloadDelegate(
    context: Context,
    downloadLocation: () -> String,
) : DownloadDelegate {

    private val contentResolver = context.contentResolver

    val downloadFileUtils =
        DefaultDownloadFileUtils(
            context = context,
            downloadLocation = downloadLocation,
        )

    /** Guess the name of the file that should be downloaded. */
    override fun guessFileName(
        contentDisposition: String?,
        url: String?,
        mimeType: String?,
    ): String {
        return downloadFileUtils.guessFileName(contentDisposition, url, mimeType)
    }

    /** Get the filename a local file has from its content provider. */
    override fun getContentFileName(uri: String?): String? {
        if (uri != null && uri.isContentUrl()) {
            return uri.toUri().getFileNameOrNullForContentUris(contentResolver)
        }
        return null
    }
}
