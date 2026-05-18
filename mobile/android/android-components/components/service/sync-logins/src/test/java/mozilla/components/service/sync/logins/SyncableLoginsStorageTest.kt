/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.logins

import android.content.Context
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.appservices.RustComponentsInitializer
import mozilla.components.concept.storage.LoginEntry
import mozilla.components.lib.dataprotect.SecureAbove22Preferences
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class SyncableLoginsStorageTest {

    private val testDispatcher = StandardTestDispatcher()

    private lateinit var storage: SyncableLoginsStorage
    private lateinit var securePrefs: SecureAbove22Preferences

    @Test
    fun `VERIFY cleaning undecryptable logins only happens once`() =
        runTest(testDispatcher) {
            RustComponentsInitializer.init()

            securePrefs = SecureAbove22Preferences(testContext, "logins", forceInsecure = true)
            storage = SyncableLoginsStorage(testContext, lazy { securePrefs }, testDispatcher)

            storage.warmUp()
            testDispatcher.scheduler.advanceUntilIdle()

            // Assert we've never ran the logins cleanup
            assertTrue(
                testContext
                    .getSharedPreferences("sync.logins.prefs", Context.MODE_PRIVATE)
                    .getInt(UNDECRYPTABLE_LOGINS_CLEANED_KEY, 0) == 0,
            )

            // Register with the sync manager to "pretend" we're about to sync
            storage.registerWithSyncManager()
            testDispatcher.scheduler.advanceUntilIdle()
            // Validate we've ran once and set the pref successfully
            assertTrue(
                testContext
                    .getSharedPreferences("sync.logins.prefs", Context.MODE_PRIVATE)
                    .getInt(UNDECRYPTABLE_LOGINS_CLEANED_KEY, 0) == 1,
            )

            storage.registerWithSyncManager()
            testDispatcher.scheduler.advanceUntilIdle()

            // Subsequent calls should not call the method again
            assertTrue(
                testContext
                    .getSharedPreferences("sync.logins.prefs", Context.MODE_PRIVATE)
                    .getInt(UNDECRYPTABLE_LOGINS_CLEANED_KEY, 0) == 1,
            )

            storage.close()
        }

    @Test
    fun `test that we can add many logins`() = runTest(testDispatcher) {
        RustComponentsInitializer.init()

        securePrefs = SecureAbove22Preferences(testContext, "logins", forceInsecure = true)
        storage = SyncableLoginsStorage(testContext, lazy { securePrefs }, testDispatcher)

        storage.warmUp()

        assertTrue(storage.list().isEmpty())

        val loginsToAdd = listOf(
            LoginEntry(
                origin = "https://www.example.org",
                httpRealm = "",
                formActionOrigin = "https://www.example.org/login",
                usernameField = "users_name",
                passwordField = "users_password",
                password = "MyVeryCoolPassword",
                username = "Foobar2001",
            ),
            LoginEntry(
                origin = "https://www.example.org",
                httpRealm = "",
                formActionOrigin = "https://www.example.org/login",
                usernameField = "users_name",
                passwordField = "users_password",
                password = "MyVeryCoolPassword",
                username = "Foobar2002",
            ),
            LoginEntry(
                origin = "https://www.example.org",
                httpRealm = "",
                formActionOrigin = "https://www.example.org/login",
                usernameField = "users_name",
                passwordField = "users_password",
                password = "MyVeryCoolPassword",
                username = "Foobar2003",
            ),
        )

        storage.addMany(loginsToAdd)

        assertEquals(loginsToAdd.size, storage.list().size)

        storage.close()
    }
}
