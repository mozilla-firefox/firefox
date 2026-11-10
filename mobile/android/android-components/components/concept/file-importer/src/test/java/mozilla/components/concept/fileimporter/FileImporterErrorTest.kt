/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.fileimporter

import android.os.Parcel
import kotlinx.parcelize.Parcelize
import mozilla.components.concept.fileparser.FileParserError
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class FileImporterErrorTest {

    @Test
    fun `GIVEN each error WHEN written to and read from a parcel THEN the same error is restored`() {
        val errors: List<FileImporterError> =
            listOf(
                FileImporterError.ReadError,
                FileImporterError.SaveError,
                FileImporterError.UnknownError,
                FileImporterError.ParseError(TestParserError),
            )

        errors.forEach { error -> assertEquals(error, error.parcelRoundTrip()) }
    }

    @Parcelize private data object TestParserError : FileParserError

    private fun FileImporterError.parcelRoundTrip(): FileImporterError? {
        val parcel = Parcel.obtain()
        try {
            parcel.writeParcelable(this, 0)
            parcel.setDataPosition(0)
            return parcel.readParcelable(javaClass.classLoader, FileImporterError::class.java)
        } finally {
            parcel.recycle()
        }
    }
}
