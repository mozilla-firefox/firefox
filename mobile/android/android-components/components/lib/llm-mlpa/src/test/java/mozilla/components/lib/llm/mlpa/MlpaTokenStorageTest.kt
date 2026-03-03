/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa

import kotlinx.coroutines.test.runTest
import mozilla.components.lib.llm.mlpa.service.AuthorizationToken
import mozilla.components.support.test.fakes.android.FakeSharedPreferences
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import kotlin.time.Clock
import kotlin.time.Duration.Companion.seconds
import kotlin.time.Instant

data class FakeClock(var now: Instant = Instant.fromEpochMilliseconds(0L)) : Clock {
    override fun now() = now
}

class MlpaTokenStorageTest {
    @Test
    fun `test that we can set a token and retrieve it if it hasn't expired`() = runTest {
        val clock = FakeClock()
        val storage = SharedPreferencesBackedMlpaStorage(FakeSharedPreferences(), clock).apply {
            setToken(AuthorizationToken.Integrity("my-test-token"), 100.seconds)
        }

        clock.now += 99.seconds
        assertEquals(AuthorizationToken.Integrity("my-test-token"), storage.getToken())
    }

    @Test
    fun `test that if a token has expired it cannot be retrieved`() = runTest {
        val clock = FakeClock(now = Instant.fromEpochMilliseconds(0))
        val storage = SharedPreferencesBackedMlpaStorage(FakeSharedPreferences(), clock).apply {
            setToken(AuthorizationToken.Integrity("my-test-token"), 100.seconds)
        }

        clock.now += 100.seconds
        assertNull(storage.getToken())
    }

    @Test
    fun `test that we can clear the storage`() = runTest {
        val storage = SharedPreferencesBackedMlpaStorage(FakeSharedPreferences(), FakeClock()).apply {
            setToken(AuthorizationToken.Integrity("my-test-token"), 100.seconds)
        }

        assertNotNull(storage.getToken())

        storage.clear()

        assertNull(storage.getToken())
    }
}
