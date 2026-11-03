/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.datastore

import androidx.datastore.core.CorruptionException
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlinx.coroutines.test.runTest
import org.junit.Test

class CustomizedMenuRecordSerializerTest {
    @Test
    fun `WHEN saving a menu customization THEN read back the same customization`() = runTest {
        val record =
            CustomizedMenuRecord.newBuilder()
                .setSchemaVersion(1)
                .addSections(section("a"))
                .addSections(section("b"))
                .addHiddenSections(section("c"))
                .build()
        val output = ByteArrayOutputStream()

        CustomizedMenuRecordSerializer.writeTo(record, output)

        assertEquals(record, CustomizedMenuRecordSerializer.readFrom(ByteArrayInputStream(output.toByteArray())))
    }

    @Test
    fun `GIVEN the saved data is not a menu customization WHEN reading it THEN report it as corrupted`() = runTest {
        val validData = CustomizedMenuRecord.newBuilder().addSections(section("abc")).build().toByteArray()
        val truncatedData = validData.copyOf(validData.size - 1)

        assertFailsWith<CorruptionException> {
            CustomizedMenuRecordSerializer.readFrom(ByteArrayInputStream(truncatedData))
        }
    }

    private fun section(id: String) = CustomizedMenuRecord.Section.newBuilder().setId(id).build()
}
