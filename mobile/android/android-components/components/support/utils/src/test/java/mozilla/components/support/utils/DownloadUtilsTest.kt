/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class DownloadUtilsTest {

    @Test
    fun sanitizeMimeType() {
        assertEquals("application/pdf", DownloadUtils.sanitizeMimeType("application/pdf; qs=0.001"))
        assertEquals("application/pdf", DownloadUtils.sanitizeMimeType("application/pdf"))
        assertEquals(null, DownloadUtils.sanitizeMimeType(null))
    }

    @Test
    fun makePdfContentDisposition() {
        assertEquals("attachment; filename=\"foo.pdf\";", DownloadUtils.makePdfContentDisposition("foo"))
        assertEquals("attachment; filename=\"foo.html.pdf\";", DownloadUtils.makePdfContentDisposition("foo.html"))
        assertEquals("attachment; filename=\"foo.pdf\";", DownloadUtils.makePdfContentDisposition("foo.pdf"))
        assertEquals("attachment; filename=\"foo.pdf\";", DownloadUtils.makePdfContentDisposition("foo.PDF"))
        assertEquals(
            "attachment; filename=\"${"a".repeat(251)}.pdf\";",
            DownloadUtils.makePdfContentDisposition("a".repeat(260)),
        )
        assertEquals(
            "attachment; filename=\"${"a".repeat(251)}.pdf\";",
            DownloadUtils.makePdfContentDisposition("a".repeat(260) + ".pdf"),
        )
        assertEquals("attachment; filename=\"foo bar.pdf\";", DownloadUtils.makePdfContentDisposition("foo bar"))
        assertEquals("attachment; filename=\"a;b.pdf\";", DownloadUtils.makePdfContentDisposition("a;b"))
        assertEquals("attachment; filename=\"a\\\"b.pdf\";", DownloadUtils.makePdfContentDisposition("a\"b"))
        assertEquals(
            "a;b.pdf",
            DownloadUtils.extractFileNameFromUrl(DownloadUtils.makePdfContentDisposition("a;b.pdf"), url = null),
        )
        assertEquals(
            "a'b.pdf",
            DownloadUtils.extractFileNameFromUrl(DownloadUtils.makePdfContentDisposition("a'b.pdf"), url = null),
        )
        assertEquals(
            "utf-8''notes; draft.pdf",
            DownloadUtils.extractFileNameFromUrl(
                DownloadUtils.makePdfContentDisposition("utf-8''notes; draft.pdf"),
                url = null,
            ),
        )
    }

    @Test
    fun `WHEN no usable filename is found THEN extractFileNameFromUrl returns the default filename`() {
        val noUsableFilename = "attachment; filename=\".pdf\""
        assertEquals(
            DownloadUtils.DEFAULT_FILE_NAME,
            DownloadUtils.extractFileNameFromUrl(noUsableFilename, url = null),
        )
        assertNull(DownloadUtils.extractFileNameFromUrlOrNull(noUsableFilename, url = null))
        assertNull(DownloadUtils.extractFileNameFromUrlOrNull(contentDisposition = null, url = null))
    }

    @Test
    fun truncateFileNameTest() {
        val path = "/storage/emulated/0/Download"
        val customPath = "/storage/emulated/0/MyCustomFolder"
        // Construct long file names with a specific number of characters
        val longBaseName = "a".repeat(300)

        // Standard cases
        assertEquals(Pair("foo", ".txt"), DownloadUtils.truncateFileName("foo", ".txt", path))
        assertEquals(Pair("foo", ""), DownloadUtils.truncateFileName("foo", "", path))
        assertEquals(Pair("foo.txt", "txt"), DownloadUtils.truncateFileName("foo.txt", "txt", path))
        assertEquals(
            Pair("a".repeat(30), "b".repeat(30)),
            DownloadUtils.truncateFileName("a".repeat(30), "b".repeat(30), customPath),
        )

        // When the base filename is too long
        assertEquals(
            Pair("a".repeat(218), "txt"),
            DownloadUtils.truncateFileName(longBaseName, "txt", path),
        )
        assertEquals(
            Pair("a".repeat(222), ""),
            DownloadUtils.truncateFileName(longBaseName, "", path),
        )
        assertEquals(
            Pair("a".repeat(212), "txt"),
            DownloadUtils.truncateFileName(longBaseName, "txt", customPath),
        )

        // Multi dot name cases
        // Keep the full filename if it is not too long
        assertEquals(
            Pair("multi.dot.filename", "superlongextension"),
            DownloadUtils.truncateFileName("multi.dot.filename", "superlongextension", path),
        )
        // When a full multi-dot file name length is too long and the extension is too long, only keep what is before
        // the first dot
        assertEquals(
            Pair("a".repeat(100), ""),
            DownloadUtils.truncateFileName("${"a".repeat(100)}.${"b".repeat(100)}", "c".repeat(100), customPath),
        )

        // When the extension is too long and the base file name exceeds total characters allowed,
        // remove the extension and truncate the base file name
        assertEquals(
            Pair("a".repeat(222), ""),
            DownloadUtils.truncateFileName(longBaseName, "b".repeat(50), path),
        )
        assertEquals(
            Pair("a".repeat(216), ""),
            DownloadUtils.truncateFileName(longBaseName, "b".repeat(50), customPath),
        )
    }

    @Test
    fun `WHEN calling createFileName with any set of parameters THEN it constructs the fileName correctly`() {
        assertEquals("a", DownloadUtils.createFileName("a"))
        assertEquals("a.c", DownloadUtils.createFileName("a", null, "c"))
        assertEquals("a(1).c", DownloadUtils.createFileName("a", 1, "c"))
        assertEquals("a(1)", DownloadUtils.createFileName("a", 1))
        assertEquals("a(1)", DownloadUtils.createFileName("a", 1, ""))
    }
}
