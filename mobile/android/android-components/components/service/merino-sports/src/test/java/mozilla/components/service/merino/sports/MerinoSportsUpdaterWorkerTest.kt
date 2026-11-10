/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import androidx.concurrent.futures.await
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.ListenableWorker
import androidx.work.testing.TestListenableWorkerBuilder
import kotlin.coroutines.cancellation.CancellationException
import kotlin.test.assertFailsWith
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MerinoSportsUpdaterWorkerTest {

    @After
    fun cleanup() {
        MerinoSportsUseCases.destroy()
    }

    @Test
    fun `WHEN worker fetches matches THEN store them and return a success result`() = runTest {
        val provider: MerinoSportsProvider = mock()
        val storage = MerinoMatchesStorage.inMemory()
        val matches = MerinoMatches()
        val worker = TestListenableWorkerBuilder<MerinoSportsUpdaterWorker>(testContext).build()

        MerinoSportsUseCases.initialize(provider, storage)

        whenever(provider.getMatches()).thenReturn(matches)

        val result = worker.startWork().await()

        assertEquals(ListenableWorker.Result.success(), result)
        assertEquals(matches, storage.matches.first())
    }

    @Test
    fun `WHEN worker fails to fetch matches THEN return a retry result`() = runTest {
        val provider: MerinoSportsProvider = mock()
        val worker = TestListenableWorkerBuilder<MerinoSportsUpdaterWorker>(testContext).build()

        MerinoSportsUseCases.initialize(provider, MerinoMatchesStorage.inMemory())

        whenever(provider.getMatches()).thenReturn(null)

        val result = worker.startWork().await()

        assertEquals(ListenableWorker.Result.retry(), result)
    }

    @Test
    fun `GIVEN the last retry is left WHEN worker fails to fetch matches THEN return a retry result`() = runTest {
        val provider: MerinoSportsProvider = mock()
        val worker =
            TestListenableWorkerBuilder<MerinoSportsUpdaterWorker>(testContext)
                .setRunAttemptCount(MerinoSportsUpdaterWorker.MAX_RETRIES - 1)
                .build()

        MerinoSportsUseCases.initialize(provider, MerinoMatchesStorage.inMemory())

        whenever(provider.getMatches()).thenReturn(null)

        assertEquals(ListenableWorker.Result.retry(), worker.startWork().await())
    }

    @Test
    fun `GIVEN the retries are exhausted WHEN worker fails to fetch matches THEN return a failure result`() = runTest {
        val provider: MerinoSportsProvider = mock()
        val worker =
            TestListenableWorkerBuilder<MerinoSportsUpdaterWorker>(testContext)
                .setRunAttemptCount(MerinoSportsUpdaterWorker.MAX_RETRIES)
                .build()

        MerinoSportsUseCases.initialize(provider, MerinoMatchesStorage.inMemory())

        whenever(provider.getMatches()).thenReturn(null)

        assertEquals(ListenableWorker.Result.failure(), worker.startWork().await())
    }

    @Test
    fun `WHEN worker is cancelled while fetching matches THEN the cancellation is not reported as a failure`() =
        runTest {
            val provider: MerinoSportsProvider = mock()
            val worker = TestListenableWorkerBuilder<MerinoSportsUpdaterWorker>(testContext).build()

            MerinoSportsUseCases.initialize(provider, MerinoMatchesStorage.inMemory())

            whenever(provider.getMatches()).thenAnswer { throw CancellationException("stopped") }

            assertFailsWith<CancellationException> { worker.startWork().await() }
        }

    @Test
    fun `GIVEN the use cases are not initialized WHEN worker runs THEN return a failure result`() = runTest {
        val worker = TestListenableWorkerBuilder<MerinoSportsUpdaterWorker>(testContext).build()

        val result = worker.startWork().await()

        assertEquals(ListenableWorker.Result.failure(), result)
    }
}
