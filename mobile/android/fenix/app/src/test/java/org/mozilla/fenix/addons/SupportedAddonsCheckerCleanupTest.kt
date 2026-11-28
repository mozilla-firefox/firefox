/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import android.content.Context
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkInfo
import androidx.work.WorkManager
import androidx.work.Worker
import androidx.work.WorkerParameters
import androidx.work.testing.WorkManagerTestInitHelper
import java.util.concurrent.TimeUnit
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SupportedAddonsCheckerCleanupTest {

    @After
    fun tearDown() {
        WorkManagerTestInitHelper.closeWorkDatabase()
    }

    @Test
    fun `WHEN cancelSupportedAddonsChecks is called THEN the work of the removed supported add-ons checker is cancelled`() {
        WorkManagerTestInitHelper.initializeTestWorkManager(testContext)
        val workManager = WorkManager.getInstance(testContext)
        workManager
            .enqueueUniquePeriodicWork(
                SUPPORTED_ADDONS_CHECKER_WORK_NAME,
                ExistingPeriodicWorkPolicy.KEEP,
                PeriodicWorkRequestBuilder<NoOpWorker>(12, TimeUnit.HOURS).build(),
            )
            .result
            .get()

        cancelSupportedAddonsChecks(testContext)

        val states = workManager.getWorkInfosForUniqueWork(SUPPORTED_ADDONS_CHECKER_WORK_NAME).get().map { it.state }
        assertEquals(listOf(WorkInfo.State.CANCELLED), states)
    }
}

internal class NoOpWorker(context: Context, params: WorkerParameters) : Worker(context, params) {
    override fun doWork() = Result.success()
}
