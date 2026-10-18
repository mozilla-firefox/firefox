/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections.migration

import androidx.concurrent.futures.await
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.Configuration
import androidx.work.Constraints
import androidx.work.ListenableWorker
import androidx.work.WorkInfo
import androidx.work.WorkManager
import androidx.work.testing.TestListenableWorkerBuilder
import androidx.work.testing.WorkManagerTestInitHelper
import io.mockk.coEvery
import io.mockk.every
import io.mockk.mockk
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.tabgroups.storage.data.TabGroupData
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings

@RunWith(AndroidJUnit4::class)
class CollectionsToTabGroupsMigrationWorkerTest {

    private val settings = mockk<Settings>(relaxed = true)
    private val tabCollectionStorage = mockk<TabCollectionStorage>(relaxed = true)
    private val collectionsMigrationRepository = DefaultCollectionsMigrationRepository(settings)

    private lateinit var workManager: WorkManager

    @Before
    fun setup() {
        WorkManagerTestInitHelper.initializeTestWorkManager(testContext, Configuration.Builder().build())
        workManager = WorkManager.getInstance(testContext)

        every { settings.hasMigratedCollectionsToTabGroups } returns false
        every { settings.migrateCollectionsToTabGroupsEnabled } returns true
        every { settings.tabGroupsEnabled } returns true
        every { settings.migratedCollectionIds } returns emptySet()
        coEvery { tabCollectionStorage.getCollectionsList() } returns emptyList()

        val components = testContext.components
        every { components.settings } returns settings
        every { components.core.tabCollectionStorage } returns tabCollectionStorage
        every { components.core.tabGroupRepository.tabGroupDataFlow } returns flowOf(TabGroupData())
        every { components.collectionsMigrationRepository } returns collectionsMigrationRepository
    }

    @After
    fun teardown() {
        workManager.cancelAllWork()
        WorkManagerTestInitHelper.closeWorkDatabase()
    }

    @Test
    fun `GIVEN the migration completes WHEN the worker runs THEN the work succeeds`() = runTest {
        val worker = TestListenableWorkerBuilder<CollectionsToTabGroupsMigrationWorker>(testContext).build()

        assertEquals(ListenableWorker.Result.success(), worker.startWork().await())
    }

    @Test
    fun `GIVEN the collections cannot be migrated WHEN the worker runs THEN the work is retried`() = runTest {
        coEvery { tabCollectionStorage.getCollectionsList() } throws
            IllegalStateException("Unable to read the collections")

        val worker = TestListenableWorkerBuilder<CollectionsToTabGroupsMigrationWorker>(testContext).build()

        assertEquals(ListenableWorker.Result.retry(), worker.startWork().await())
    }

    @Test
    fun `GIVEN the collections cannot be migrated WHEN the worker runs and the attempts are exhausted THEN the work fails`() =
        runTest {
            coEvery { tabCollectionStorage.getCollectionsList() } throws
                IllegalStateException("Unable to read the collections")

            val worker =
                TestListenableWorkerBuilder<CollectionsToTabGroupsMigrationWorker>(testContext)
                    .setRunAttemptCount(MAX_RUN_ATTEMPTS - 1)
                    .build()

            assertEquals(ListenableWorker.Result.failure(), worker.startWork().await())
        }

    @Test
    fun `GIVEN the migration is needed WHEN the work is enqueued THEN the migration work is scheduled`() = runTest {
        CollectionsToTabGroupsMigrationWorker.enqueueIfNeeded(testContext, collectionsMigrationRepository)

        val workInfos = workManager.getWorkInfosForUniqueWork(MIGRATION_WORK_NAME).await()
        assertEquals(1, workInfos.size)
        assertEquals(WorkInfo.State.ENQUEUED, workInfos.single().state)
        assertEquals(
            Constraints.Builder().setRequiresBatteryNotLow(true).setRequiresStorageNotLow(true).build(),
            workInfos.single().constraints,
        )
    }

    @Test
    fun `GIVEN the migration work is already scheduled WHEN the work is enqueued again THEN it is not duplicated`() =
        runTest {
            CollectionsToTabGroupsMigrationWorker.enqueueIfNeeded(testContext, collectionsMigrationRepository)
            CollectionsToTabGroupsMigrationWorker.enqueueIfNeeded(testContext, collectionsMigrationRepository)

            assertEquals(1, workManager.getWorkInfosForUniqueWork(MIGRATION_WORK_NAME).await().size)
        }

    @Test
    fun `GIVEN the collections were migrated WHEN the work is enqueued THEN no migration work is scheduled`() =
        runTest {
            every { settings.hasMigratedCollectionsToTabGroups } returns true

            CollectionsToTabGroupsMigrationWorker.enqueueIfNeeded(testContext, collectionsMigrationRepository)

            assertTrue(workManager.getWorkInfosForUniqueWork(MIGRATION_WORK_NAME).await().isEmpty())
        }

    @Test
    fun `GIVEN the migration is disabled WHEN the work is enqueued THEN no migration work is scheduled`() = runTest {
        every { settings.migrateCollectionsToTabGroupsEnabled } returns false

        CollectionsToTabGroupsMigrationWorker.enqueueIfNeeded(testContext, collectionsMigrationRepository)

        assertTrue(workManager.getWorkInfosForUniqueWork(MIGRATION_WORK_NAME).await().isEmpty())
    }
}
