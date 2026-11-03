/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.feature.ipprotection.storage.FakeEligibilityStorage
import mozilla.components.feature.ipprotection.storage.StorageStoreSync
import mozilla.components.feature.ipprotection.storage.buildStore
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalAndroidComponentsApi::class)
class StorageStoreSyncTest {

    @Test
    fun `WHEN initialized THEN the storage is initialized`() = runTest {
        val storage = FakeEligibilityStorage()
        val (ipProtectionStore, _) = buildStore()

        StorageStoreSync(storage, ipProtectionStore, StandardTestDispatcher(testScheduler)).initialize()

        testScheduler.advanceUntilIdle()

        assertTrue(storage.initCalled)
    }

    @Test
    fun `WHEN initialized THEN the initial eligibility status is forwarded`() = runTest {
        val storage = FakeEligibilityStorage()
        storage.emit(EligibilityStatus.Eligible)
        val (ipProtectionStore, captureMiddleware) = buildStore()

        StorageStoreSync(storage, ipProtectionStore, StandardTestDispatcher(testScheduler)).initialize()

        testScheduler.advanceUntilIdle()

        captureMiddleware.assertFirstAction(IPProtectionAction.EligibilityAction.EligibilityChanged::class) {
            assertEquals(EligibilityStatus.Eligible, it.eligibility)
        }
        assertEquals(EligibilityStatus.Eligible, ipProtectionStore.state.eligibilityStatus)
    }

    @Test
    fun `WHEN the eligibility status changes THEN the new status is forwarded`() = runTest {
        // All these cases are the same mappings - and we do not want them to change.
        val cases =
            listOf(
                EligibilityStatus.Unknown,
                EligibilityStatus.Ineligible,
                EligibilityStatus.UnsupportedRegion,
                EligibilityStatus.Eligible,
            )

        cases.forEach { eligibility ->
            val (ipProtectionStore, captureMiddleware) = buildStore()
            val storage = FakeEligibilityStorage()

            StorageStoreSync(storage, ipProtectionStore, StandardTestDispatcher(testScheduler)).initialize()

            storage.emit(eligibility)

            testScheduler.advanceUntilIdle()

            captureMiddleware.assertLastAction(IPProtectionAction.EligibilityAction.EligibilityChanged::class) {
                assertEquals("Unexpected forwarded status for $eligibility", eligibility, it.eligibility)
            }
            assertEquals(
                "Unexpected store state for $eligibility",
                eligibility,
                ipProtectionStore.state.eligibilityStatus,
            )
        }
    }

    @Test
    fun `WHEN the same eligibility status is emitted twice THEN nothing new is forwarded`() = runTest {
        val storage = FakeEligibilityStorage()
        val (ipProtectionStore, captureMiddleware) = buildStore()

        StorageStoreSync(storage, ipProtectionStore, StandardTestDispatcher(testScheduler)).initialize()
        storage.emit(EligibilityStatus.Eligible)

        testScheduler.advanceUntilIdle()
        captureMiddleware.reset()

        storage.emit(EligibilityStatus.Eligible)

        testScheduler.advanceUntilIdle()

        captureMiddleware.assertNotDispatched(IPProtectionAction.EligibilityAction.EligibilityChanged::class)
        assertEquals(EligibilityStatus.Eligible, ipProtectionStore.state.eligibilityStatus)
    }
}
