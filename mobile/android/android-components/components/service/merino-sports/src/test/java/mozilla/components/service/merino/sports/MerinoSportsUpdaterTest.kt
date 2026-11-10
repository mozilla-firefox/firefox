/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import androidx.concurrent.futures.await
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.Configuration
import androidx.work.WorkInfo
import androidx.work.WorkManager
import androidx.work.testing.WorkManagerTestInitHelper
import java.util.concurrent.TimeUnit
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.components.service.merino.sports.MerinoSportsUpdater.Companion.PERIODIC_WORK_TAG
import mozilla.components.support.base.worker.Frequency
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MerinoSportsUpdaterTest {

    @Before
    fun setUp() {
        WorkManagerTestInitHelper.initializeTestWorkManager(
            testContext,
            Configuration.Builder().build(),
        )
    }

    @After
    fun tearDown() {
        WorkManager.getInstance(testContext).cancelUniqueWork(PERIODIC_WORK_TAG)
        MerinoSportsUseCases.destroy()
    }

    @Test
    fun `WHEN periodic work is started THEN work is queued`() = runTest {
        val updater = createUpdater()
        val workManager = WorkManager.getInstance(testContext)
        var workInfo = workManager.getWorkInfosForUniqueWork(PERIODIC_WORK_TAG).await()

        assertTrue(workInfo.isEmpty())
        assertNull(MerinoSportsUseCases.provider)
        assertNull(MerinoSportsUseCases.storage)

        updater.startPeriodicWork()

        assertNotNull(MerinoSportsUseCases.requireProvider())
        assertNotNull(MerinoSportsUseCases.requireStorage())

        workInfo = workManager.getWorkInfosForUniqueWork(PERIODIC_WORK_TAG).await()
        val work = workInfo.first()

        assertEquals(1, workInfo.size)
        assertEquals(WorkInfo.State.ENQUEUED, work.state)
        assertTrue(work.tags.contains(PERIODIC_WORK_TAG))
    }

    @Test
    fun `GIVEN periodic work is started WHEN periodic work is stopped THEN no work is queued`() = runTest {
        val updater = createUpdater()
        val workManager = WorkManager.getInstance(testContext)

        assertTrue(workManager.getWorkInfosForUniqueWork(PERIODIC_WORK_TAG).await().isEmpty())

        updater.startPeriodicWork()

        assertEquals(1, workManager.getWorkInfosForUniqueWork(PERIODIC_WORK_TAG).await().size)

        updater.stopPeriodicWork()

        val work = workManager.getWorkInfosForUniqueWork(PERIODIC_WORK_TAG).await().first()

        assertNull(MerinoSportsUseCases.provider)
        assertNull(MerinoSportsUseCases.storage)
        assertEquals(WorkInfo.State.CANCELLED, work.state)
    }

    @Test
    fun `WHEN periodic work request is created THEN it contains the correct constraints and interval`() {
        val updater = createUpdater(frequency = Frequency(1, TimeUnit.HOURS))
        val workRequest = updater.createPeriodicWorkRequest()

        assertTrue(workRequest.tags.contains(PERIODIC_WORK_TAG))
        assertEquals(updater.getWorkerConstraints(), workRequest.workSpec.constraints)
        assertEquals(TimeUnit.HOURS.toMillis(1), workRequest.workSpec.intervalDuration)
    }

    private fun createUpdater(frequency: Frequency = Frequency(30, TimeUnit.MINUTES)) =
        MerinoSportsUpdater(
            context = testContext,
            provider = mock(),
            storage = MerinoMatchesStorage.inMemory(),
            frequency = frequency,
        )
}
