/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.util

import org.junit.Assert.assertEquals
import org.junit.Test

class PdfDownloadFilenameTest {

    @Test
    fun `WHEN the PDF has its own filename THEN the filename is used`() {
        assertEquals("filename.pdf", makePdfDownloadFilename(filename = "filename.pdf", title = "Title"))
        assertEquals("Mozilla.PDF", makePdfDownloadFilename(filename = "Mozilla.PDF", title = null))
    }

    @Test
    fun `WHEN the PDF has no filename of its own THEN the title is used with pdf added once`() {
        assertEquals("Mozilla.pdf", makePdfDownloadFilename(filename = DEFAULT_PDF_FILE_NAME, title = "Mozilla"))
        assertEquals("Mozilla.pdf", makePdfDownloadFilename(filename = null, title = "Mozilla"))
        assertEquals("Mozilla.pdf", makePdfDownloadFilename(filename = "", title = "Mozilla"))
        assertEquals("pdfForm.pdf", makePdfDownloadFilename(filename = null, title = "pdfForm.pdf"))
        assertEquals("Mozilla.pdf", makePdfDownloadFilename(filename = null, title = "* Mozilla"))
    }

    @Test
    fun `WHEN there is neither a filename nor a title THEN the default is used`() {
        assertEquals(DEFAULT_PDF_FILE_NAME, makePdfDownloadFilename(filename = null, title = null))
        assertEquals(DEFAULT_PDF_FILE_NAME, makePdfDownloadFilename(filename = DEFAULT_PDF_FILE_NAME, title = " "))
        assertEquals(DEFAULT_PDF_FILE_NAME, makePdfDownloadFilename(filename = null, title = "* "))
    }
}
