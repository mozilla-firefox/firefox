/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.logins

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.ListenableWorker.Result
import androidx.work.testing.TestListenableWorkerBuilder
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`
import kotlin.reflect.KVisibility

@RunWith(AndroidJUnit4::class)
class SyncableLoginsStorageWorkerTest {

    @After
    fun tearDown() {
        GlobalLoginsDependencyProvider.loginsStorage = null
    }

    @Test
    fun `PlacesHistoryStorage's runMaintenance is called when worker's startWork is called`() =
        runTest {
            val loginsStorage = mock<LoginsStorage>()
            GlobalLoginsDependencyProvider.initialize(lazy { loginsStorage })
            val worker =
                TestListenableWorkerBuilder<SyncableLoginsStorageWorker>(
                    testContext,
                ).build()

            worker.doWork()
            verify(loginsStorage).runMaintenance(SyncableLoginsStorageWorker.DB_SIZE_LIMIT_IN_BYTES.toUInt())
        }

    @Test
    fun `PlacesHistoryStorage's runMaintenance operation is successful, successful result returned by the worker`() =
        runTest {
            val loginsStorage = mock<LoginsStorage>()
            GlobalLoginsDependencyProvider.initialize(lazy { loginsStorage })
            val worker =
                TestListenableWorkerBuilder<SyncableLoginsStorageWorker>(
                    testContext,
                ).build()

            val result = worker.doWork()
            assertEquals(Result.success(), result)
        }

    @Test
    fun `PlacesHistoryStorage's runMaintenance is called, exception is thrown and failure result is returned`() =
        runTest {
            val loginsStorage = mock<LoginsStorage>()
            `when`(loginsStorage.runMaintenance(SyncableLoginsStorageWorker.DB_SIZE_LIMIT_IN_BYTES.toUInt()))
                .thenThrow(CancellationException())
            GlobalLoginsDependencyProvider.initialize(lazy { loginsStorage })
            val worker =
                TestListenableWorkerBuilder<SyncableLoginsStorageWorker>(
                    testContext,
                ).build()

            val result = worker.doWork()
            assertEquals(Result.failure(), result)
        }

    @Test
    fun `PlacesHistoryStorage's runMaintenance is called, exception is thrown and active write operations are cancelled`() =
        runTest {
            val loginsStorage = mock<LoginsStorage>()
            `when`(loginsStorage.runMaintenance(SyncableLoginsStorageWorker.DB_SIZE_LIMIT_IN_BYTES.toUInt()))
                .thenThrow(CancellationException())
            GlobalLoginsDependencyProvider.initialize(lazy { loginsStorage })
            val worker =
                TestListenableWorkerBuilder<SyncableLoginsStorageWorker>(
                    testContext,
                ).build()

            worker.doWork()
            verify(loginsStorage).cancelWrites()
        }

    @Test
    fun `PlacesHistoryStorageWorker's visibility is internal`() {
        assertEquals(SyncableLoginsStorageWorker::class.visibility, KVisibility.INTERNAL)
    }
}
