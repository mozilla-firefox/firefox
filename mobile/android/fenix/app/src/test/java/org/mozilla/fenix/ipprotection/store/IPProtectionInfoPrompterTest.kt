/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package org.mozilla.fenix.ipprotection.store

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlin.time.Instant
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.DataVolume
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.ProxyStatus
import mozilla.components.feature.ipprotection.store.state.UsageQuota
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.snackbar.SnackbarState
import org.mozilla.fenix.helpers.FenixGleanTestRule

private val RESET_TIME = Instant.parse("2026-10-01T00:00:00Z")
private val NEXT_CYCLE_RESET_TIME = Instant.parse("2026-11-01T00:00:00Z")
private val DATA_LIMIT = DataVolume.roundedGigabytes(50)

@RunWith(AndroidJUnit4::class)
class IPProtectionInfoPrompterTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)
    private val testDispatcher = StandardTestDispatcher()
    private lateinit var appStore: AppStore
    private lateinit var repository: FakeIPProtectionRepository
    private val errorMessages = ErrorMessages(dataLimitReached = "Data limit reached")

    @Before
    fun setup() {
        appStore = AppStore()
        repository = FakeIPProtectionRepository()
    }

    @Test
    fun `GIVEN eligible user with DataLimitReached proxy WHEN eligibility updates THEN shows data limit reached snackbar`() =
        runTest(testDispatcher) {
            val ipProtectionStore = cappedStore(eligibilityStatus = EligibilityStatus.Unknown)
            val prompter = prompter(ipProtectionStore)

            prompter.start()
            assertIs<SnackbarState.None>(appStore.state.snackbarState)
            testDispatcher.scheduler.advanceUntilIdle()

            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible)
            )
            testDispatcher.scheduler.advanceUntilIdle()

            val snackbarState = appStore.state.snackbarState
            assertIs<SnackbarState.IPProtectionShowActionSettingsSnackbar>(snackbarState)
            assertEquals(errorMessages.dataLimitReached, snackbarState.title)
        }

    @Test
    fun `GIVEN eligible user with Active proxy WHEN eligibility updates THEN no snackbar shown`() =
        runTest(testDispatcher) {
            val ipProtectionStore = store(proxyStatus = Authorized.Active)
            val prompter = prompter(ipProtectionStore)

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()

            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible)
            )
            testDispatcher.scheduler.advanceUntilIdle()

            assertIs<SnackbarState.None>(appStore.state.snackbarState)
        }

    @Test
    fun `GIVEN ineligible user with ConnectionError proxy WHEN eligibility updates THEN no snackbar shown`() =
        runTest(testDispatcher) {
            val ipProtectionStore = store(proxyStatus = Authorized.ConnectionError)
            val prompter = prompter(ipProtectionStore)

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()

            ipProtectionStore.dispatch(
                IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Ineligible)
            )
            testDispatcher.scheduler.advanceUntilIdle()

            assertIs<SnackbarState.None>(appStore.state.snackbarState)
        }

    @Test
    fun `GIVEN the notice was never shown WHEN the data limit is reached THEN the notice is shown and the reset time is recorded`() =
        runTest(testDispatcher) {
            val prompter = prompter(cappedStore())

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()

            val snackbarState = appStore.state.snackbarState
            assertIs<SnackbarState.IPProtectionShowActionSettingsSnackbar>(snackbarState)
            assertEquals(errorMessages.dataLimitReached, snackbarState.title)
            assertEquals(RESET_TIME, repository.lastDataLimitNoticeCycleResetTime)
            assertNotNull(Vpn.bandwidthLimitError.testGetValue())
        }

    @Test
    fun `GIVEN the notice was shown for the current cycle WHEN app restarts THEN the notice is not shown again`() =
        runTest(testDispatcher) {
            val prompter = prompter(cappedStore())

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()
            val snackbarState = appStore.state.snackbarState
            assertIs<SnackbarState.IPProtectionShowActionSettingsSnackbar>(snackbarState)
            consumeSnackbar()

            // Simulate restart
            prompter.stop()
            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertIs<SnackbarState.None>(appStore.state.snackbarState)
        }

    @Test
    fun `GIVEN the notice was shown for the previous cycle WHEN the allowance is reset and data limit is reached THEN the notice is shown`() =
        runTest(testDispatcher) {
            repository.lastDataLimitNoticeCycleResetTime = RESET_TIME
            val prompter = prompter(cappedStore(NEXT_CYCLE_RESET_TIME))

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()

            assertIs<SnackbarState.IPProtectionShowActionSettingsSnackbar>(appStore.state.snackbarState)
            assertEquals(NEXT_CYCLE_RESET_TIME, repository.lastDataLimitNoticeCycleResetTime)
            assertNotNull(Vpn.bandwidthLimitError.testGetValue())
        }

    @Test
    fun `GIVEN the notice was shown for the current cycle WHEN the reset time changes and the data limit is reached THEN the notice is shown again`() =
        runTest(testDispatcher) {
            repository.lastDataLimitNoticeCycleResetTime = RESET_TIME
            val ipProtectionStore = cappedStore()
            val prompter = prompter(ipProtectionStore)

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()
            assertIs<SnackbarState.None>(appStore.state.snackbarState)

            ipProtectionStore.dispatch(
                IPProtectionAction.UsageAction.UsageChanged(
                    remaining = 0L,
                    max = DATA_LIMIT.bytes,
                    resetTime = NEXT_CYCLE_RESET_TIME.toString(),
                )
            )
            testDispatcher.scheduler.advanceUntilIdle()

            assertIs<SnackbarState.IPProtectionShowActionSettingsSnackbar>(appStore.state.snackbarState)
            assertEquals(NEXT_CYCLE_RESET_TIME, repository.lastDataLimitNoticeCycleResetTime)
        }

    @Test
    fun `GIVEN the cycle has no reset time WHEN the app starts THEN the notice is not shown`() =
        runTest(testDispatcher) {
            val prompter = prompter(cappedStore(resetTime = null))

            prompter.start()
            testDispatcher.scheduler.advanceUntilIdle()
            assertIs<SnackbarState.None>(appStore.state.snackbarState)
        }

    private fun prompter(ipProtectionStore: IPProtectionStore) =
        IPProtectionInfoPrompter(
            store = ipProtectionStore,
            appStore = appStore,
            errorMessages = errorMessages,
            repository = repository,
            mainDispatcher = testDispatcher,
        )

    private fun store(
        proxyStatus: ProxyStatus,
        eligibilityStatus: EligibilityStatus = EligibilityStatus.Unknown,
        usageQuota: UsageQuota? = null,
    ) =
        IPProtectionStore(
            initialState =
                IPProtectionState(
                    eligibilityStatus = eligibilityStatus,
                    proxyStatus = proxyStatus,
                    serviceStatus = ServiceState.Ready,
                    usageQuota = usageQuota,
                )
        )

    private fun cappedStore(
        resetTime: Instant? = RESET_TIME,
        eligibilityStatus: EligibilityStatus = EligibilityStatus.Eligible,
    ) =
        store(
            eligibilityStatus = eligibilityStatus,
            usageQuota =
                UsageQuota.Metered(
                    max = DATA_LIMIT,
                    remaining = DataVolume.Zero,
                    resetTime = resetTime,
                ),
            proxyStatus = Authorized.DataLimitReached,
        )

    private fun consumeSnackbar() {
        appStore.dispatch(AppAction.SnackbarAction.SnackbarShown)
    }
}
