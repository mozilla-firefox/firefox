/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.util

import android.webkit.MimeTypeMap
import java.io.File
import mozilla.components.concept.engine.DownloadDelegate
import mozilla.components.support.ktx.kotlin.isContentUrl
import mozilla.components.support.ktx.kotlin.sanitizeFileName
import mozilla.components.support.utils.DownloadUtils

/** The name the PDF viewer gives a PDF when no filename is known. Used as a fallback when no good names are known. */
internal const val DEFAULT_PDF_FILE_NAME = "document.pdf"

/** Default PDF extension. */
private const val PDF_EXTENSION = ".pdf"

/** The PDF viewer starts its title with this when the PDF has unsaved changes, such as a signature. */
private const val UNSAVED_CHANGES_TITLE_PREFIX = "* "

/** Ensures the file ends in [PDF_EXTENSION]. */
private fun String.withPdfExtension() = if (endsWith(PDF_EXTENSION, ignoreCase = true)) this else this + PDF_EXTENSION

/** Removes the last extension when it is a known one other than [PDF_EXTENSION], such as `.html`. */
private fun String.removeNonPdfExtensions(): String {
    if (endsWith(PDF_EXTENSION, ignoreCase = true)) {
        return this
    }
    val file = File(this)
    if (MimeTypeMap.getSingleton().hasExtension(file.extension)) {
        return file.nameWithoutExtension
    }
    return this
}

/** Makes the name safe for the filesystem without a non-PDF extension, or null if nothing usable is left. */
private fun String.toSanitizedPdfFilename(): String? =
    trim()
        // ".pdf" alone is only an extension, not a name.
        .takeUnless { it.equals(PDF_EXTENSION, ignoreCase = true) }
        ?.removeNonPdfExtensions()
        ?.sanitizeFileName()
        // Sanitizing can still leave only an extension, such as "..pdf" becoming ".pdf".
        ?.takeUnless { it.startsWith(".") }
        ?.takeIf { it.isNotBlank() }

/**
 * Chooses the best filename for downloading a PDF.
 *
 * Will select in the following order:
 * 1. Content URI filename: If the file is local on the system, then the filesystem content provider name will be used.
 * 2. Header filename: If the file has a Content-Disposition with a valid name, then that name will be used.
 * 3. Title of document: If the document has a presentation title, then that name will be used.
 * 4. Default: If there are no good choices, then [DEFAULT_PDF_FILE_NAME] will be used.
 *
 * Each name is sanitized first, and skipped if nothing usable is left.
 *
 * @param downloadDelegate The delegate to use to find local filenames.
 * @param url The URL of the tab showing the PDF or page.
 * @param contentDisposition The 'Content-Disposition' header of the PDF, if available. Could be from the server or from
 *   the PDF viewer.
 * @param title The title of the PDF or page.
 * @return The recommended filename for the PDF. Will always end in .pdf.
 */
internal fun findBestPdfFilename(
    downloadDelegate: DownloadDelegate?,
    url: String?,
    contentDisposition: String?,
    title: String?,
): String {
    // Find the filesystem name for local `content://` PDFs
    val contentUriFilename =
        url?.takeIf { it.isContentUrl() }
            ?.let { downloadDelegate?.getContentFileName(uri = it) }
            ?.toSanitizedPdfFilename()

    // Find the Content-Disposition header filename
    val headerFilename =
        DownloadUtils.fileNameFromContentDisposition(contentDisposition)
            ?.toSanitizedPdfFilename()
            // [DEFAULT_PDF_FILE_NAME] means the PDF viewer assigned it a filename
            ?.takeIf { it != DEFAULT_PDF_FILE_NAME }

    // The PDF viewer sets the tab title from the document
    val pageTitle = title?.removePrefix(UNSAVED_CHANGES_TITLE_PREFIX)?.toSanitizedPdfFilename()

    val name =
        when {
            contentUriFilename != null -> contentUriFilename
            headerFilename != null -> headerFilename
            pageTitle != null -> pageTitle
            else -> DEFAULT_PDF_FILE_NAME
        }
    return name.withPdfExtension()
}
