/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.bookmark.parser.jsoup

import android.os.Parcel
import mozilla.components.concept.fileparser.FileParserError
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class BookmarksParseErrorTest {

    @Test
    fun `GIVEN each error WHEN written to and read from a parcel THEN the same error is restored`() {
        val errors: List<FileParserError> =
            listOf(
                BookmarksParseError.UnsupportedContentType,
                BookmarksParseError.InvalidFormat,
                BookmarksParseError.Unexpected,
            )

        errors.forEach { error -> assertEquals(error, error.roundTrip()) }
    }

    private fun FileParserError.roundTrip(): FileParserError? {
        val parcel = Parcel.obtain()
        try {
            parcel.writeParcelable(this, 0)
            parcel.setDataPosition(0)
            return parcel.readParcelable(javaClass.classLoader, FileParserError::class.java)
        } finally {
            parcel.recycle()
        }
    }
}
