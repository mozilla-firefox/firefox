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

        val first = ClientUUID(prefs) { "my-generated-uuid" }
        assertEquals(UserId("my-generated-uuid"), first.getUserId())
        assertEquals(UserId("my-generated-uuid"), first.getUserId())

        val second = ClientUUID(prefs) {
            throw IllegalStateException("We should not be generating another uuid")
        }
        assertEquals(UserId("my-generated-uuid"), second.getUserId())
        assertEquals(UserId("my-generated-uuid"), second.getUserId())
    }
}
