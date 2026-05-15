/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.passwords.file

import android.net.Uri
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class CsvPasswordsFileImporterTest {

    @Test
    fun `importPasswordsFromUri returns success with zero count`() = runTest {
        val importer = CsvPasswordsFileImporter()

        val result = importer.importPasswordsFromUri(Uri.EMPTY)

        assertTrue(result.isSuccess)
        assertEquals(0, result.getOrThrow().count)
    }
}
