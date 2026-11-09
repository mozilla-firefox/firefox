/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.util

/** The name the PDF viewer gives a PDF when no filename is known. Used as a fallback when no good names are known. */
internal const val DEFAULT_PDF_FILE_NAME = "document.pdf"

/** Default PDF extension. */
private const val PDF_EXTENSION = ".pdf"

/** The PDF viewer starts its title with this when the PDF has unsaved changes, such as a signature. */
private const val UNSAVED_CHANGES_TITLE_PREFIX = "* "

/** Ensures the file ends in [PDF_EXTENSION]. */
private fun String.withPdfExtension() = if (endsWith(PDF_EXTENSION, ignoreCase = true)) this else this + PDF_EXTENSION

/**
 * Chooses the best filename for downloading a PDF depending on what is available:
 *
 * 1. The PDF's own filename
 * 2. The title the PDF presents itself as
 * 3. A default of [DEFAULT_PDF_FILE_NAME]
 *
 * @param filename The filename associated with the PDF, or possibly null when a page is saved as PDF.
 * @param title The title of the PDF or page.
 * @return The recommended filename for the PDF. Will always end in .pdf.
 */
internal fun makePdfDownloadFilename(filename: String?, title: String?): String {
    // We are interested in filenames that were determined by the server or URL.
    // [DEFAULT_PDF_FILE_NAME] means the PDF viewer assigned it a filename.
    val specificFilename = filename?.takeIf { it.isNotBlank() && it != DEFAULT_PDF_FILE_NAME }
    val usableTitle = title?.removePrefix(UNSAVED_CHANGES_TITLE_PREFIX)?.takeIf { it.isNotBlank() }

    val name =
        when {
            specificFilename != null -> specificFilename
            usableTitle != null -> usableTitle
            else -> DEFAULT_PDF_FILE_NAME
        }
    return name.withPdfExtension()
}
