/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.ai.controls

import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.fakes.android.FakePreferencesDataStore
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DataStoreBackedAIFeatureBlockStorageTest {

    @Test
    fun `isBlocked defaults to false`() = runTest {
        val storage = DataStoreBackedAIFeatureBlockStorage(FakePreferencesDataStore())

        assertFalse(storage.isBlocked.first())
    }

    @Test
    fun `setBlocked to true persists blocked state`() = runTest {
        val storage = DataStoreBackedAIFeatureBlockStorage(FakePreferencesDataStore())

        storage.setBlocked(true)

        assertTrue(storage.isBlocked.first())
    }

    @Test
    fun `setBlocked to false persists unblocked state`() = runTest {
        val storage = DataStoreBackedAIFeatureBlockStorage(FakePreferencesDataStore())

        storage.setBlocked(true)
        storage.setBlocked(false)

        assertFalse(storage.isBlocked.first())
    }
}
