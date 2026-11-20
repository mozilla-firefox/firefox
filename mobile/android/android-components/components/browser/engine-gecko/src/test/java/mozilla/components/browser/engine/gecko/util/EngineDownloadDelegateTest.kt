/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.util

import android.content.ContentResolver
import android.content.Context
import android.database.MatrixCursor
import android.provider.OpenableColumns
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.any
import org.mockito.Mockito.doReturn

@RunWith(AndroidJUnit4::class)
class EngineDownloadDelegateTest {

    private val contentResolver = mock<ContentResolver>()
    private val context = mock<Context>().also { whenever(it.contentResolver).thenReturn(contentResolver) }
    private val downloadDelegate = EngineDownloadDelegate(context = context, downloadLocation = { "" })
    private val contentUri = "content://media/external/downloads/29"

    @Test
    fun `GIVEN a content URI WHEN getContentFileName is called THEN the content provider's name is used`() {
        givenDisplayName("test.pdf")

        assertEquals("test.pdf", downloadDelegate.getContentFileName(uri = contentUri))
    }

    @Test
    fun `GIVEN no content provider name for the URI WHEN getContentFileName is called THEN null is returned`() {
        givenDisplayName(null)
        assertNull(downloadDelegate.getContentFileName(uri = contentUri))

        givenDisplayName("other.pdf")
        assertNull(downloadDelegate.getContentFileName(uri = "https://example.com/test.pdf"))
        assertNull(downloadDelegate.getContentFileName(uri = null))
    }

    private fun givenDisplayName(name: String?) {
        val cursor = MatrixCursor(arrayOf(OpenableColumns.DISPLAY_NAME)).apply { addRow(arrayOf(name)) }
        doReturn(cursor).`when`(contentResolver).query(any(), any(), any(), any(), any())
    }
}
