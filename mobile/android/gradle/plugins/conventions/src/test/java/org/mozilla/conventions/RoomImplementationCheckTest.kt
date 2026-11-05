/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.conventions

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class RoomImplementationCheckTest {
    private val database = "mozilla.components.feature.top.sites.db.TopSiteDatabase"

    @Test
    fun `Kotlin implementation satisfies the database`() {
        assertEquals(emptyList<String>(), missingRoomImplementations(listOf(database), listOf("TopSiteDatabase_Impl.kt")))
    }

    @Test
    fun `Java implementation satisfies the database`() {
        assertEquals(emptyList<String>(), missingRoomImplementations(listOf(database), listOf("TopSiteDatabase_Impl.java")))
    }

    @Test
    fun `nested database implementation satisfies the database`() {
        assertEquals(
            emptyList<String>(),
            missingRoomImplementations(listOf("org.example.Outer.InnerDatabase"), listOf("Outer_InnerDatabase_Impl.kt")),
        )
    }

    @Test
    fun `database with only DAO implementations is missing`() {
        assertEquals(listOf(database), missingRoomImplementations(listOf(database), listOf("PinnedSiteDao_Impl.kt")))
    }

    @Test
    fun `implementation of a database whose name ends with the same text does not count`() {
        assertEquals(listOf(database), missingRoomImplementations(listOf(database), listOf("OtherTopSiteDatabase_Impl.kt")))
    }

    @Test
    fun `only the database without an implementation is reported`() {
        val crash = "mozilla.components.lib.crash.db.CrashDatabase"
        assertEquals(
            listOf(crash),
            missingRoomImplementations(listOf(database, crash), listOf("TopSiteDatabase_Impl.kt", "CrashDao_Impl.kt")),
        )
    }

    @Test
    fun `no generated output reports every database`() {
        assertEquals(listOf(database), missingRoomImplementations(listOf(database), emptyList()))
    }
}
