/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import mozilla.components.lib.llm.mlpa.service.UserId
import mozilla.components.support.test.fakes.android.FakeSharedPreferences
import org.junit.Assert.assertEquals
import org.junit.Test

class ClientUUIDTest {
    @Test
    fun `that a client uuid will only be generated the first time`() {
        val prefs = FakeSharedPreferences()

        val first = PrefsBackedClientUUID({ prefs }, generateUUID = { "my-generated-uuid" })
        assertEquals(UserId("my-generated-uuid"), first.getUserId())
        assertEquals(UserId("my-generated-uuid"), first.getUserId())

        val second = PrefsBackedClientUUID({ prefs }, generateUUID = {
            throw IllegalStateException("We should not be generating another uuid")
        })
        assertEquals(UserId("my-generated-uuid"), second.getUserId())
        assertEquals(UserId("my-generated-uuid"), second.getUserId())
    }

    @Test
    fun `that generateHash uses the provided hasher`() {
        val prefs = FakeSharedPreferences()

        val clientUUID = PrefsBackedClientUUID(
            getPrefs = { prefs },
            generateUUID = { "my-generated-uuid" },
            hasher = { "This is a hashed value: $it" },
        )

        assertEquals("This is a hashed value: my-generated-uuid", clientUUID.generateHash())
    }
}
