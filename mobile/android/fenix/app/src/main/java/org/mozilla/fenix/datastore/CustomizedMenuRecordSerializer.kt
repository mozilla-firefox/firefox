/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.datastore

import androidx.datastore.core.CorruptionException
import androidx.datastore.core.Serializer
import com.google.protobuf.InvalidProtocolBufferException
import java.io.InputStream
import java.io.OutputStream

/** Serializer for [CustomizedMenuRecord] defined in browser_menu_customization.proto. */
object CustomizedMenuRecordSerializer : Serializer<CustomizedMenuRecord> {
    override val defaultValue: CustomizedMenuRecord = CustomizedMenuRecord.getDefaultInstance()

    override suspend fun readFrom(input: InputStream): CustomizedMenuRecord {
        return try {
            CustomizedMenuRecord.parseFrom(input)
        } catch (exception: InvalidProtocolBufferException) {
            throw CorruptionException("Cannot read the menu customization data", exception)
        }
    }

    override suspend fun writeTo(t: CustomizedMenuRecord, output: OutputStream) {
        t.writeTo(output)
    }
}
