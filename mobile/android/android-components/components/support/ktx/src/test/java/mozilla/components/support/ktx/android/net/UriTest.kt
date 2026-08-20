/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.ktx.android.net

import android.content.ContentResolver
import android.database.Cursor
import android.database.MatrixCursor
import android.database.StaleDataException
import android.provider.OpenableColumns
import androidx.core.net.toUri
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyInt
import org.mockito.Mockito.any
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.doThrow

@RunWith(AndroidJUnit4::class)
class UriTest {

    @Test
    fun hostWithoutCommonPrefixes() {
        assertEquals(
            "mozilla.org",
            "https://www.mozilla.org".toUri().hostWithoutCommonPrefixes,
        )

        assertEquals(
            "twitter.com",
            "https://mobile.twitter.com/home".toUri().hostWithoutCommonPrefixes,
        )

        assertNull("".toUri().hostWithoutCommonPrefixes)

        assertEquals(
            "",
            "http://".toUri().hostWithoutCommonPrefixes,
        )

        assertEquals(
            "facebook.com",
            "https://m.facebook.com/".toUri().hostWithoutCommonPrefixes,
        )

        assertEquals(
            "github.com",
            "https://github.com/mozilla-mobile/android-components".toUri().hostWithoutCommonPrefixes,
        )
    }

    @Test
    fun testIsHttpOrHttps() {
        // No value
        assertFalse("".toUri().isHttpOrHttps)

        // Garbage
        assertFalse("lksdjflasuf".toUri().isHttpOrHttps)

        // URLs with http/https
        assertTrue("https://www.google.com".toUri().isHttpOrHttps)
        assertTrue("http://www.facebook.com".toUri().isHttpOrHttps)
        assertTrue("https://mozilla.org/en-US/firefox/products/".toUri().isHttpOrHttps)

        // IP addresses
        assertTrue("https://192.168.0.1".toUri().isHttpOrHttps)
        assertTrue("http://63.245.215.20/en-US/firefox/products".toUri().isHttpOrHttps)

        // Other protocols
        assertFalse("ftp://people.mozilla.org".toUri().isHttpOrHttps)
        assertFalse("javascript:window.google.com".toUri().isHttpOrHttps)
        assertFalse("tel://1234567890".toUri().isHttpOrHttps)

        // No scheme
        assertFalse("google.com".toUri().isHttpOrHttps)
        assertFalse("git@github.com:mozilla/gecko-dev.git".toUri().isHttpOrHttps)
    }

    @Test
    fun testIsInScope() {
        val url = "https://mozilla.github.io/my-app/".toUri()
        val prefix = "https://mozilla.github.io/prefix-of/resource.html".toUri()
        assertFalse(url.isInScope(emptyList()))
        assertTrue(url.isInScope(listOf("https://mozilla.github.io/my-app/".toUri())))
        assertFalse(url.isInScope(listOf("https://firefox.com/out-of-scope/".toUri())))
        assertFalse(url.isInScope(listOf("https://mozilla.github.io/my-app-almost-in-scope".toUri())))
        assertTrue(prefix.isInScope(listOf("https://mozilla.github.io/prefix".toUri())))
        assertTrue(prefix.isInScope(listOf("https://mozilla.github.io/prefix-of/".toUri())))
    }

    @Test
    fun testSameSchemeAndHostAs() {
        // Host mismatch.
        assertFalse("https://foo.bar".toUri().sameSchemeAndHostAs("https://foo.baz".toUri()))
        // Scheme mismatch.
        assertFalse("http://127.0.0.1".toUri().sameSchemeAndHostAs("https://127.0.0.1".toUri()))
        // Port mismatch.
        assertTrue("https://foo.bar:444".toUri().sameSchemeAndHostAs("https://foo.bar:555".toUri()))
        // Port OK but scheme different.
        assertFalse("https://foo.bar:443".toUri().sameSchemeAndHostAs("ftp://foo.bar:443".toUri()))

        assertTrue("https://foo.bar/bobo".toUri().sameSchemeAndHostAs("https://foo.bar:443/obob".toUri()))
    }

    @Test
    fun testSameOriginAs() {
        // Host mismatch.
        assertFalse("https://foo.bar".toUri().sameOriginAs("https://foo.baz".toUri()))
        // Scheme mismatch.
        assertFalse("http://127.0.0.1".toUri().sameOriginAs("https://127.0.0.1".toUri()))
        // Port mismatch.
        assertFalse("https://foo.bar:444".toUri().sameOriginAs("https://foo.bar:555".toUri()))
        // Port OK but scheme different.
        assertFalse("https://foo.bar:443".toUri().sameOriginAs("ftp://foo.bar:443".toUri()))

        assertTrue("https://foo.bar:443/bobo".toUri().sameOriginAs("https://foo.bar:443/obob".toUri()))
        assertTrue("https://foo.bar:333".toUri().sameOriginAs("https://foo.bar:333".toUri()))
    }

    @Test
    fun testGenerateFileName() {
        val fileExtension = "txt"
        var fileName = generateFileName(fileExtension)

        assertTrue(fileName.contains(fileExtension))

        fileName = generateFileName()

        assertFalse(fileName.contains("."))
    }

    @Test
    fun testGetFileExtension() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()

        doReturn("text/plain").`when`(resolver).getType(any())

        assertEquals("txt", uri.getFileExtension(resolver))
    }

    @Test
    fun `GIVEN an available display name WHEN getFileNameForContentUris is called THEN that name is used`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()
        val cursor =
            MatrixCursor(arrayOf(OpenableColumns.DISPLAY_NAME)).apply {
                addRow(arrayOf("myFile.txt"))
            }

        doReturn("text/plain").`when`(resolver).getType(any())
        doReturn(cursor).`when`(resolver).query(any(), any(), any(), any(), any())

        assertEquals("myFile.txt", uri.getFileNameForContentUris(resolver))
    }

    @Test
    fun `GIVEN a missing DISPLAY_NAME column WHEN getFileNameForContentUris is called THEN a name is generated`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()
        val cursor = mock<Cursor>()

        doReturn("text/plain").`when`(resolver).getType(any())
        doReturn(cursor).`when`(resolver).query(any(), any(), any(), any(), any())
        doReturn(-1).`when`(cursor).getColumnIndex(any())

        val fileName = uri.getFileNameForContentUris(resolver)

        assertTrue(fileName.endsWith(".txt"))
    }

    @Test
    fun `GIVEN a null display name WHEN getFileNameForContentUris is called THEN a name is generated`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()
        val cursor =
            MatrixCursor(arrayOf(OpenableColumns.DISPLAY_NAME)).apply {
                addRow(arrayOf(null))
            }

        doReturn("text/plain").`when`(resolver).getType(any())
        doReturn(cursor).`when`(resolver).query(any(), any(), any(), any(), any())

        val fileName = uri.getFileNameForContentUris(resolver)

        assertTrue(fileName.endsWith(".txt"))
    }

    @Test
    fun `GIVEN an empty cursor WHEN getFileNameForContentUris is called THEN a name is generated`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()
        val cursor = MatrixCursor(arrayOf(OpenableColumns.DISPLAY_NAME))

        doReturn("text/plain").`when`(resolver).getType(any())
        doReturn(cursor).`when`(resolver).query(any(), any(), any(), any(), any())

        val fileName = uri.getFileNameForContentUris(resolver)

        assertTrue(fileName.endsWith(".txt"))
    }

    @Test
    fun `GIVEN a failing query WHEN getFileNameForContentUris is called THEN a name is generated`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()

        doReturn("text/plain").`when`(resolver).getType(any())
        doThrow(IllegalArgumentException("Invalid URI")).`when`(resolver).query(any(), any(), any(), any(), any())

        val fileName = uri.getFileNameForContentUris(resolver)

        assertTrue(fileName.endsWith(".txt"))
    }

    @Test
    fun `GIVEN a failing cursor read WHEN getFileNameForContentUris is called THEN a name is generated`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()
        val cursor = mock<Cursor>()

        doReturn("text/plain").`when`(resolver).getType(any())
        doReturn(cursor).`when`(resolver).query(any(), any(), any(), any(), any())
        doReturn(0).`when`(cursor).getColumnIndex(any())
        doReturn(true).`when`(cursor).moveToFirst()
        doThrow(StaleDataException("Attempting to access a closed CursorWindow.")).`when`(cursor).getString(anyInt())

        val fileName = uri.getFileNameForContentUris(resolver)

        assertTrue(fileName.endsWith(".txt"))
    }

    @Test
    fun `GIVEN a null cursor WHEN getFileNameForContentUris is called THEN a name is generated`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()

        doReturn("text/plain").`when`(resolver).getType(any())
        doReturn(null).`when`(resolver).query(any(), any(), any(), any(), any())

        val fileName = uri.getFileNameForContentUris(resolver)

        assertTrue(fileName.endsWith(".txt"))
    }

    @Test
    fun `getFileName for file uri schemes`() {
        val resolver = mock<ContentResolver>()
        val uri = "file:///home/user/myfile.html".toUri()

        assertEquals("myfile.html", uri.getFileName(resolver))
    }

    @Test
    fun `getFileName for content uri schemes`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()
        val cursor = mock<Cursor>()

        doReturn("text/plain").`when`(resolver).getType(any())

        doReturn(cursor).`when`(resolver).query(any(), any(), any(), any(), any())
        doReturn(1).`when`(cursor).getColumnIndex(any())
        doReturn(null).`when`(cursor).getString(anyInt())

        val fileName = uri.getFileName(resolver)

        assertTrue(fileName.contains(".txt"))
        assertTrue(fileName.isNotEmpty())
    }

    @Test
    fun `getFileName for UNKNOWN uri schemes will generate file name`() {
        val resolver = mock<ContentResolver>()
        val uri = "UNKNOWN://media/external/file/37162".toUri()

        doReturn("text/plain").`when`(resolver).getType(any())

        val fileName = uri.getFileName(resolver)

        assertTrue(fileName.contains(".txt"))
        assertTrue(fileName.isNotEmpty())
    }

    @Test
    fun `GIVEN content resolver finds a result WHEN checking readability THEN returns true`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()
        val cursor = mock<Cursor>()

        doReturn(cursor).`when`(resolver).query(any(), any(), any(), any(), any())

        val result = uri.isReadable(resolver)
        assertTrue(result)
    }

    @Test
    fun `GIVEN content resolver query returns null WHEN checking readability THEN returns false`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()

        doReturn(null).`when`(resolver).query(any(), any(), any(), any(), any())

        val result = uri.isReadable(resolver)
        assertFalse(result)
    }

    @Test
    fun `GIVEN content resolver query throws SecurityException WHEN checking readability THEN returns false`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()

        doThrow(SecurityException("Permission denied"))
            .`when`(resolver)
            .query(any(), any<Array<String>>(), any(), any(), any())

        val result = uri.isReadable(resolver)
        assertFalse(result)
    }

    @Test
    fun `GIVEN content resolver query throws IllegalStateException WHEN checking readability THEN return false`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()

        doThrow(IllegalStateException("Must call PhenotypeContext.setContext() first"))
            .`when`(resolver)
            .query(any(), any<Array<String>>(), any(), any(), any())

        val result = uri.isReadable(resolver)

        assertFalse(result)
    }

    @Test
    fun `GIVEN content resolver query throws IllegalArgumentException WHEN checking readability THEN return false`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()

        doThrow(IllegalArgumentException("Unknown URL"))
            .`when`(resolver)
            .query(any(), any<Array<String>>(), any(), any(), any())

        val result = uri.isReadable(resolver)

        assertFalse(result)
    }

    @Test
    fun `GIVEN content resolver query throws NullPointerException WHEN checking readability THEN return false`() {
        val resolver = mock<ContentResolver>()
        val uri = "content://media/external/file/37162".toUri()

        doThrow(NullPointerException("Attempt to invoke interface method on a null object reference"))
            .`when`(resolver)
            .query(any(), any<Array<String>>(), any(), any(), any())

        val result = uri.isReadable(resolver)

        assertFalse(result)
    }
}
