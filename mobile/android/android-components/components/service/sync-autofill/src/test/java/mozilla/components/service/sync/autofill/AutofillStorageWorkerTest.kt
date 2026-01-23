/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.ListenableWorker.Result
import androidx.work.testing.TestListenableWorkerBuilder
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.storage.CreditCardsAddressesStorage
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
class AutofillStorageWorkerTest {

    @After
    fun tearDown() {
        GlobalAutofillDependencyProvider.autofillStorage = null
    }

    @Test
    fun `CreditCardsAddressesStorage's runMaintenance is called when worker's startWork is called`() =
        runTest {
            val autofillStorage = mock<CreditCardsAddressesStorage>()
            GlobalAutofillDependencyProvider.initialize(lazy { autofillStorage })
            val worker =
                TestListenableWorkerBuilder<AutofillStorageWorker>(
                    testContext,
                ).build()

            worker.doWork()
            verify(autofillStorage).runMaintenance(AutofillStorageWorker.DB_SIZE_LIMIT_IN_BYTES.toUInt())
        }

    @Test
    fun `CreditCardsAddressesStorage's runMaintenance operation is successful, successful result returned by the worker`() =
        runTest {
            val autofillStorage = mock<CreditCardsAddressesStorage>()
            GlobalAutofillDependencyProvider.initialize(lazy { autofillStorage })
            val worker =
                TestListenableWorkerBuilder<AutofillStorageWorker>(
                    testContext,
                ).build()

            val result = worker.doWork()
            assertEquals(Result.success(), result)
        }

    @Test
    fun `CreditCardsAddressesStorage's runMaintenance is called, exception is thrown and failure result is returned`() =
        runTest {
            val autofillStorage = mock<CreditCardsAddressesStorage>()
            `when`(autofillStorage.runMaintenance(AutofillStorageWorker.DB_SIZE_LIMIT_IN_BYTES.toUInt()))
                .thenThrow(CancellationException())
            GlobalAutofillDependencyProvider.initialize(lazy { autofillStorage })
            val worker =
                TestListenableWorkerBuilder<AutofillStorageWorker>(
                    testContext,
                ).build()

            val result = worker.doWork()
            assertEquals(Result.failure(), result)
        }

    @Test
    fun `CreditCardsAddressesStorage's runMaintenance is called, exception is thrown and active write operations are cancelled`() =
        runTest {
            val autofillStorage = mock<CreditCardsAddressesStorage>()
            `when`(autofillStorage.runMaintenance(AutofillStorageWorker.DB_SIZE_LIMIT_IN_BYTES.toUInt()))
                .thenThrow(CancellationException())
            GlobalAutofillDependencyProvider.initialize(lazy { autofillStorage })
            val worker =
                TestListenableWorkerBuilder<AutofillStorageWorker>(
                    testContext,
                ).build()

            worker.doWork()
            verify(autofillStorage).cancelWrites()
        }
}
