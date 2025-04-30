/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.remotesettings

import androidx.concurrent.futures.await
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.Configuration
import androidx.work.WorkInfo
import androidx.work.WorkManager
import androidx.work.testing.WorkManagerTestInitHelper
import junit.framework.TestCase.assertEquals
import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertTrue
import mozilla.components.support.base.worker.Frequency
import mozilla.components.support.remotesettings.RemoteSettingsSyncWorker.Companion.REMOTE_SETTINGS_SYNC_WORKER_TAG
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import mozilla.components.support.test.rule.runTestOnMain
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.TimeUnit

@RunWith(AndroidJUnit4::class)
class DefaultRemoteSettingsSyncSchedulerTest {

    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    @Before
    fun setUp() {
        val configuration = Configuration.Builder().build()
        WorkManagerTestInitHelper.initializeTestWorkManager(testContext, configuration)
    }

    @Test
    fun `WHEN registerForSync is called THEN work is scheduled for syncs`() = runTestOnMain {
        val frequency = Frequency(24, TimeUnit.HOURS)
        val checker = DefaultRemoteSettingsSyncScheduler(testContext, frequency)

        val workId = REMOTE_SETTINGS_SYNC_WORKER_TAG

        val workManager = WorkManager.getInstance(testContext)
        val workData = workManager.getWorkInfosForUniqueWork(workId).await()

        assertTrue(workData.isEmpty())

        checker.registerForSync()

        assertSyncWorkerIsRegistered()

        workManager.cancelUniqueWork(workId)
    }

    @Test
    fun `WHEN unregisterForSync is called THEN work is unscheduled for syncs`() = runTestOnMain {
        val frequency = Frequency(24, TimeUnit.HOURS)
        val checker = DefaultRemoteSettingsSyncScheduler(testContext, frequency)

        val workId = REMOTE_SETTINGS_SYNC_WORKER_TAG

        val workManager = WorkManager.getInstance(testContext)
        var workData = workManager.getWorkInfosForUniqueWork(workId).await()

        assertTrue(workData.isEmpty())

        checker.registerForSync()

        assertSyncWorkerIsRegistered()

        checker.unregisterForSync()

        workData = workManager.getWorkInfosForUniqueWork(workId).await()

        assertEquals(WorkInfo.State.CANCELLED, workData.first().state)
    }

    private suspend fun assertSyncWorkerIsRegistered() {
        val workId = REMOTE_SETTINGS_SYNC_WORKER_TAG
        val workManager = WorkManager.getInstance(testContext)
        val workData = workManager.getWorkInfosForUniqueWork(workId).await()

        assertFalse(workData.isEmpty())

        val work = workData.first()

        assertEquals(WorkInfo.State.ENQUEUED, work.state)
        assertTrue(work.tags.contains(workId))
        assertTrue(work.tags.contains(REMOTE_SETTINGS_SYNC_WORKER_TAG))
    }
}
