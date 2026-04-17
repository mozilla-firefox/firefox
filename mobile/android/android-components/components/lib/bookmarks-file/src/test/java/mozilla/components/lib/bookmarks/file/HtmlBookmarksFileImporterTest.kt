/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.bookmarks.file.importer

import android.net.Uri
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import java.io.File

@RunWith(RobolectricTestRunner::class)
class HtmlBookmarksFileImporterTest {
    private val testDispatcher = StandardTestDispatcher()
    private lateinit var importer: HtmlBookmarksFileImporter

    @Before
    fun setup() {
        importer = HtmlBookmarksFileImporter(
            contentResolver = RuntimeEnvironment.getApplication().contentResolver,
            dispatcher = testDispatcher,
        )
    }

    @Test
    fun `importBookmarksFromUri returns success with correct count`() = runTest(testDispatcher) {
        val file = File.createTempFile("bookmarks", ".html").apply {
            writeText(
                """
                <DT><A HREF="https://example.com">Example</A>
                <DT><A HREF="https://mozilla.org">Mozilla</A>
                """.trimIndent(),
            )
            deleteOnExit()
        }
        val result = importer.importBookmarksFromUri(Uri.fromFile(file)).getOrThrow()
        testDispatcher.scheduler.advanceUntilIdle()
        assertEquals(2, result.count)
    }

    @Test
    fun `importBookmarksFromUri returns failure for invalid uri`() = runTest(testDispatcher) {
        val result = importer.importBookmarksFromUri(Uri.parse("file:///nonexistent/file.html"))
        testDispatcher.scheduler.advanceUntilIdle()
        assertTrue(result.isFailure)
    }
}
