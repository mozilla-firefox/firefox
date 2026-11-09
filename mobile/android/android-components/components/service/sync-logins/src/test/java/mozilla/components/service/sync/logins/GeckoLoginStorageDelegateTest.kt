/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.logins

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import org.junit.Test
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.verify

class GeckoLoginStorageDelegateTest {
    private val testDispatcher = StandardTestDispatcher()
    private val scope = TestScope(testDispatcher)

    @Test
    fun `onLoginSave ignores logins with invalid characters`() = scope.runTest {
        val loginStorage: LoginsStorage = mock()
        doAnswer {
                throw InvalidRecordException("Login contains invalid non utf-8 characters")
            }
            .`when`(loginStorage)
            .addOrUpdate(any())
        val delegate = GeckoLoginStorageDelegate(lazy { loginStorage }, scope)
        val entry =
            LoginEntry(
                origin = "https://www.example.org",
                username = "Foobar\uD83D",
                password = "MyVeryCoolPassword",
            )

        delegate.onLoginSave(entry)
        testDispatcher.scheduler.advanceUntilIdle()

        verify(loginStorage).addOrUpdate(entry)
    }
}
