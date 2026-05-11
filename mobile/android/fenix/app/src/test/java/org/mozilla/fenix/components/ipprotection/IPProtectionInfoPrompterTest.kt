/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.ipprotection

import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.ipprotection.Authorized
import mozilla.components.feature.ipprotection.EligibilityStatus
import mozilla.components.feature.ipprotection.IPProtectionAction
import mozilla.components.feature.ipprotection.IPProtectionState
import mozilla.components.feature.ipprotection.IPProtectionStore
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState
import kotlin.test.assertIs

class IPProtectionInfoPrompterTest {
    private val testDispatcher = StandardTestDispatcher()
    private lateinit var appStore: AppStore
    private val errorMessages = ErrorMessages(
        connectionError = "Connection error",
        dataLimitReached = "Data limit reached",
    )

    @Before
    fun setup() {
        appStore = AppStore()
    }

    @Test
    fun `GIVEN eligible user with ConnectionError proxy WHEN eligibility updates THEN shows connection error snackbar`() =
        runTest(testDispatcher) {
            val ipProtectionStore = IPProtectionStore(
                initialState = IPProtectionState(
                    proxyStatus = Authorized.ConnectionError,
                ),
            )
            val prompter = IPProtectionInfoPrompter(ipProtectionStore, appStore, errorMessages, testDispatcher)

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()

            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible),
            )
            testDispatcher.scheduler.advanceUntilIdle()

            val snackbarState = appStore.state.snackbarState
            assertIs<SnackbarState.ShowSnackbar>(snackbarState)
            assertEquals(errorMessages.connectionError, snackbarState.title)
        }

    @Test
    fun `GIVEN eligible user with DataLimitReached proxy WHEN eligibility updates THEN shows data limit reached snackbar`() =
        runTest(testDispatcher) {
            val ipProtectionStore = IPProtectionStore(
                initialState = IPProtectionState(
                    proxyStatus = Authorized.DataLimitReached,
                ),
            )
            val prompter = IPProtectionInfoPrompter(ipProtectionStore, appStore, errorMessages, testDispatcher)

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()

            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible),
            )
            testDispatcher.scheduler.advanceUntilIdle()

            val snackbarState = appStore.state.snackbarState
            assertIs<SnackbarState.ShowSnackbar>(snackbarState)
            assertEquals(errorMessages.dataLimitReached, snackbarState.title)
        }

    @Test
    fun `GIVEN eligible user with Active proxy WHEN eligibility updates THEN no snackbar shown`() =
        runTest(testDispatcher) {
            val ipProtectionStore = IPProtectionStore(
                initialState = IPProtectionState(
                    proxyStatus = Authorized.Active,
                ),
            )
            val prompter = IPProtectionInfoPrompter(ipProtectionStore, appStore, errorMessages, testDispatcher)

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()

            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityChanged(EligibilityStatus.Eligible),
            )
            testDispatcher.scheduler.advanceUntilIdle()

            assertIs<SnackbarState.None>(appStore.state.snackbarState)
        }

    @Test
    fun `GIVEN ineligible user with ConnectionError proxy WHEN eligibility updates THEN no snackbar shown`() =
        runTest(testDispatcher) {
            val ipProtectionStore = IPProtectionStore(
                initialState = IPProtectionState(
                    proxyStatus = Authorized.ConnectionError,
                ),
            )
            val prompter = IPProtectionInfoPrompter(ipProtectionStore, appStore, errorMessages, testDispatcher)

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()

            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityChanged(EligibilityStatus.Ineligible),
            )
            testDispatcher.scheduler.advanceUntilIdle()

            assertIs<SnackbarState.None>(appStore.state.snackbarState)
        }
}
