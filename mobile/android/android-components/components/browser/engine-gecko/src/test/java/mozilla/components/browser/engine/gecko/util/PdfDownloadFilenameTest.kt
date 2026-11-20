/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.util

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class PdfDownloadFilenameTest {

    private val webPdfUrl = "https://example.com/other.pdf"
    private val localPdfUrl = "content://media/external/downloads/29"

    @Test
    fun `WHEN there is only a title THEN it is sanitized into the filename or the default is used`() {
        mapOf(
                "Mozilla" to "Mozilla.pdf",
                "pdfForm.pdf" to "pdfForm.pdf",
                "Mozilla.PDF" to "Mozilla.PDF",
                "test.html" to "test.pdf",
                "test.html " to "test.pdf",
                "test.jpg" to "test.pdf",
                "Q3.2026 test" to "Q3.2026 test.pdf",
                ".test" to "test.pdf",
                "Mozilla: Title" to "Mozilla_ Title.pdf",
                ".pdf" to DEFAULT_PDF_FILE_NAME,
                " .PDF" to DEFAULT_PDF_FILE_NAME,
                "..pdf" to DEFAULT_PDF_FILE_NAME,
                ". .pdf" to DEFAULT_PDF_FILE_NAME,
                ".pdf.pdf" to DEFAULT_PDF_FILE_NAME,
                ".html" to DEFAULT_PDF_FILE_NAME,
                " " to DEFAULT_PDF_FILE_NAME,
            )
            .forEach { (title, expected) -> assertEquals(title, expected, findPdfFilename(title = title)) }
    }

    @Test
    fun `WHEN a local PDF has a content provider filename THEN it is used over the header and title`() {
        assertEquals(
            "contentUri.pdf",
            findPdfFilename(
                url = localPdfUrl,
                contentFilename = "contentUri.pdf",
                contentDisposition = contentDispositionOf("contentDisposition.pdf"),
                title = "title",
            ),
        )
    }

    @Test
    fun `WHEN a web PDF is named THEN the content provider is not used`() {
        assertEquals(
            "contentDisposition.pdf",
            findPdfFilename(
                url = webPdfUrl,
                contentFilename = "contentUri.pdf",
                contentDisposition = contentDispositionOf("contentDisposition.pdf"),
                title = "title",
            ),
        )
    }

    @Test
    fun `WHEN there is no content provider filename THEN the header filename is used over the title`() {
        assertEquals(
            "contentDisposition.pdf",
            findPdfFilename(
                url = localPdfUrl,
                contentDisposition = contentDispositionOf("contentDisposition.pdf"),
                title = "title",
            ),
        )
        assertEquals(
            "contentDisposition.pdf",
            findBestPdfFilename(
                downloadDelegate = null,
                url = localPdfUrl,
                contentDisposition = contentDispositionOf("contentDisposition.pdf"),
                title = "title",
            ),
        )
        assertEquals(
            "contentDisposition.pdf",
            findPdfFilename(contentDisposition = contentDispositionOf(".contentDisposition"), title = "title"),
        )
    }

    @Test
    fun `WHEN the header filename is not usable THEN the title is used`() {
        assertEquals(
            "title.pdf",
            findPdfFilename(contentDisposition = contentDispositionOf(DEFAULT_PDF_FILE_NAME), title = "title"),
        )
        assertEquals("title.pdf", findPdfFilename(contentDisposition = contentDispositionOf(".pdf"), title = "title"))
        assertEquals("title.pdf", findPdfFilename(contentDisposition = contentDispositionOf(""), title = "title"))
    }

    @Test
    fun `WHEN the title starts with the unsaved changes prefix THEN the prefix is removed`() {
        assertEquals("title.pdf", findPdfFilename(title = "* title"))
    }

    @Test
    fun `WHEN there is no usable filename or title THEN the default is used`() {
        assertEquals(DEFAULT_PDF_FILE_NAME, findPdfFilename())
        assertEquals(DEFAULT_PDF_FILE_NAME, findPdfFilename(title = "* "))
        assertEquals(
            DEFAULT_PDF_FILE_NAME,
            findPdfFilename(contentDisposition = contentDispositionOf(DEFAULT_PDF_FILE_NAME), title = " "),
        )
    }

    private fun contentDispositionOf(filename: String) = "attachment; filename=\"$filename\""

    /** Calls [findBestPdfFilename] with a download delegate that finds [contentFilename] for a content URI. */
    private fun findPdfFilename(
        url: String = webPdfUrl,
        contentFilename: String? = null,
        contentDisposition: String? = null,
        title: String? = null,
    ) =
        findBestPdfFilename(
            downloadDelegate = FakeEngineDownloadDelegate(getContentFileName = { contentFilename }),
            url = url,
            contentDisposition = contentDisposition,
            title = title,
        )
}
