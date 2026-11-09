/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package org.mozilla.fenix.ipprotection.store

import io.mockk.mockk
import io.mockk.verify
import java.time.Clock
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneOffset
import java.time.format.DateTimeParseException
import kotlin.test.AfterTest
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mozilla.fenix.ipprotection.FakeIPProtectionPromptRepository
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptState.PromptDetails
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.nimbus.IpProtection

class IPProtectionOnboardingPromptTest {
    private val testDispatcher = StandardTestDispatcher()

    private val fakeClock = Clock.fixed(Instant.parse("2026-06-10T00:00:00Z"), ZoneOffset.UTC)
    private val crashReporter: CrashReporting = mockk(relaxed = true)

    private val accountReadyStatuses =
        listOf(
            AccountStatus.NoAccount,
            AccountStatus.NeedsAuthorization,
            AccountStatus.NeedsAuthentication,
            AccountStatus.Authenticated,
            AccountStatus.EnrolledAndEntitled,
        )

    private val accountInitializingStatuses =
        listOf(
            AccountStatus.Uninitialized,
            AccountStatus.WarmingUp,
        )

    @AfterTest
    fun tearDown() {
        FxNimbus.features.ipProtection.withCachedValue(
            IpProtection(promoDeadline = "2026-06-11", dataLimitGigabyte = 50)
        )
    }

    @Test
    fun `WHEN the nimbus promo deadline is in the future THEN the promo takes precedence over the data limit`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-11", dataLimitGigabyte = 50)
            )
            val repository = FakeIPProtectionPromptRepository()
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                repository = repository,
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(PromptDetails.UnlimitedDataPromo(LocalDate.of(2026, 6, 11)), actualPromptDetails)
            verify(exactly = 0) { crashReporter.submitCaughtException(any()) }
        }

    @Test
    fun `WHEN the nimbus promo deadline is today THEN the promo is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-10", dataLimitGigabyte = 50)
            )
            val repository = FakeIPProtectionPromptRepository()
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                repository = repository,
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(PromptDetails.UnlimitedDataPromo(LocalDate.of(2026, 6, 10)), actualPromptDetails)
        }

    @Test
    fun `WHEN the nimbus promo is active and the data limit is zero THEN the promo is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-11", dataLimitGigabyte = 0)
            )
            val repository = FakeIPProtectionPromptRepository()
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                repository = repository,
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(PromptDetails.UnlimitedDataPromo(LocalDate.of(2026, 6, 11)), actualPromptDetails)
        }

    @Test
    fun `WHEN the promo deadline has passed THEN a regular prompt is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-09", dataLimitGigabyte = 55)
            )
            val repository = FakeIPProtectionPromptRepository()
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                repository = repository,
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(PromptDetails.Regular(55), actualPromptDetails)
        }

    @Test
    fun `WHEN the nimbus promo deadline is unexpected format THEN a regular prompt is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                // unexpected date format
                IpProtection(promoDeadline = "2026-06-10T00:00:00Z", dataLimitGigabyte = 55)
            )
            val repository = FakeIPProtectionPromptRepository()
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                repository = repository,
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(PromptDetails.Regular(55), actualPromptDetails)
        }

    @Test
    fun `WHEN the nimbus promo deadline is empty THEN the regular prompt is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(IpProtection(promoDeadline = "", dataLimitGigabyte = 50))

            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                repository = FakeIPProtectionPromptRepository(),
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(PromptDetails.Regular(50), actualPromptDetails)
        }

    @Test
    fun `GIVEN there is no active promo WHEN nimbus data limit is invalid THEN no prompt is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-09", dataLimitGigabyte = -1)
            )
            val repository = FakeIPProtectionPromptRepository()
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                repository = repository,
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertNull(actualPromptDetails)
        }

    @Test
    fun `WHEN the nimbus promo deadline is malformed and there is no data limit THEN exception is reported without showing a prompt`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "not-a-date", dataLimitGigabyte = 0)
            )
            val repository = FakeIPProtectionPromptRepository()
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            val reportedExceptions = mutableListOf<Throwable>()
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                repository = repository,
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertNull(actualPromptDetails)
            verify { crashReporter.submitCaughtException(capture(reportedExceptions)) }

            val parsingException = reportedExceptions.filterIsInstance<DateTimeParseException>().first()
            assertEquals("not-a-date", parsingException.parsedString)

            val invalidNimbusException =
                reportedExceptions.filterIsInstance<MalformedIPProtectionNimbusConfigException>().first()
            assertEquals(
                "Invalid nimbus configuration for onboarding prompt: promoDeadline=not-a-date, dataLimit=0",
                invalidNimbusException.message,
            )
        }

    @Test
    fun `GIVEN repository allows the prompt WHEN eligibility becomes Eligible AND account is ready THEN onShowOnboarding is invoked`() =
        runTest(testDispatcher) {
            accountReadyStatuses.forEach { status ->
                FxNimbus.features.ipProtection.withCachedValue(
                    IpProtection(promoDeadline = "2026-06-11", dataLimitGigabyte = 50)
                )
                val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = true)
                var shownCount = 0
                val store = buildStore(status)

                startBinding(repository, store, onShowOnboarding = { shownCount++ })

                store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible))
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(1, shownCount)
            }
        }

    @Test
    fun `GIVEN repository does not allow the prompt WHEN eligibility becomes Eligible AND account is ready THEN onAlreadySatisfied is invoked instead of onShowOnboarding`() =
        runTest(testDispatcher) {
            accountReadyStatuses.forEach { status ->
                val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = false)
                var shownCount = 0
                var alreadySatisfiedCount = 0
                val store = buildStore(status)

                startBinding(
                    repository,
                    store,
                    onShowOnboarding = { shownCount++ },
                    onAlreadySatisfied = { alreadySatisfiedCount++ },
                )

                store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible))
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(0, shownCount)
                assertEquals(1, alreadySatisfiedCount)
            }
        }

    @Test
    fun `GIVEN repository does not allow the prompt WHEN eligibility becomes Ineligible or UnsupportedRegion THEN onIneligible is invoked and onAlreadySatisfied is not`() =
        runTest(testDispatcher) {
            listOf(EligibilityStatus.Ineligible, EligibilityStatus.UnsupportedRegion).forEach { ineligibleStatus ->
                val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = false)
                var ineligibleCount = 0
                var alreadySatisfiedCount = 0
                val store = buildStore()

                startBinding(
                    repository,
                    store,
                    onShowOnboarding = {},
                    onIneligible = { ineligibleCount++ },
                    onAlreadySatisfied = { alreadySatisfiedCount++ },
                )

                store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(ineligibleStatus))
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(1, ineligibleCount)
                assertEquals(0, alreadySatisfiedCount)
            }
        }

    @Test
    fun `GIVEN repository does not allow the prompt WHEN eligibility is Eligible AND account is not ready THEN onAlreadySatisfied is not invoked`() =
        runTest(testDispatcher) {
            accountInitializingStatuses.forEach { status ->
                val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = false)
                var alreadySatisfiedCount = 0
                val store = buildStore(eligibilityStatus = EligibilityStatus.Eligible)

                startBinding(repository, store, onShowOnboarding = {}, onAlreadySatisfied = { alreadySatisfiedCount++ })

                store.dispatch(IPProtectionAction.AccountAction.AccountStateChanged(status))
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(0, alreadySatisfiedCount)
            }
        }

    @Test
    fun `GIVEN repository allows the prompt WHEN eligibility becomes Eligible AND account is ready THEN onAlreadySatisfied is not invoked`() =
        runTest(testDispatcher) {
            val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = true)
            var alreadySatisfiedCount = 0
            val store = buildStore(AccountStatus.NoAccount)

            startBinding(repository, store, onShowOnboarding = {}, onAlreadySatisfied = { alreadySatisfiedCount++ })

            store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(0, alreadySatisfiedCount)
        }

    @Test
    fun `WHEN eligibility is not Eligible THEN onShowOnboarding is not invoked`() =
        runTest(testDispatcher) {
            (accountReadyStatuses + accountInitializingStatuses).forEach { status ->
                val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = true)
                var shownCount = 0
                val store = buildStore(status)

                startBinding(repository, store, onShowOnboarding = { shownCount++ })

                store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Ineligible))
                store.dispatch(
                    IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.UnsupportedRegion)
                )
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(0, shownCount)
            }
        }

    @Test
    fun `WHEN eligibility becomes Ineligible or UnsupportedRegion THEN onIneligible is invoked`() =
        runTest(testDispatcher) {
            listOf(EligibilityStatus.Ineligible, EligibilityStatus.UnsupportedRegion).forEach { ineligibleStatus ->
                val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = true)
                var ineligibleCount = 0
                val store = buildStore()

                startBinding(repository, store, onShowOnboarding = {}, onIneligible = { ineligibleCount++ })

                store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(ineligibleStatus))
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(1, ineligibleCount)
            }
        }

    @Test
    fun `WHEN eligibility is Eligible THEN onIneligible is not invoked`() =
        runTest(testDispatcher) {
            val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = true)
            var ineligibleCount = 0
            val store = buildStore()

            startBinding(repository, store, onShowOnboarding = {}, onIneligible = { ineligibleCount++ })

            store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(0, ineligibleCount)
        }

    @Test
    fun `WHEN eligibility becomes Unknown THEN onIneligible is not invoked`() =
        runTest(testDispatcher) {
            val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = true)
            var ineligibleCount = 0
            val store = buildStore(eligibilityStatus = EligibilityStatus.Eligible)

            startBinding(repository, store, onShowOnboarding = {}, onIneligible = { ineligibleCount++ })

            store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Unknown))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(0, ineligibleCount)
        }

    @Test
    fun `GIVEN eligibility is Eligible WHEN account is not ready THEN onShowOnboarding is not invoked`() =
        runTest(testDispatcher) {
            accountInitializingStatuses.forEach { status ->
                val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = true)
                var shownCount = 0
                val store = buildStore(eligibilityStatus = EligibilityStatus.Eligible)

                startBinding(repository, store, onShowOnboarding = { shownCount++ })

                store.dispatch(IPProtectionAction.AccountAction.AccountStateChanged(status))
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(0, shownCount)
            }
        }

    @Test
    fun `GIVEN eligibility is Eligible WHEN account is ready THEN onShowOnboarding is invoked`() =
        runTest(testDispatcher) {
            accountReadyStatuses.forEach { status ->
                val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = true)
                var shownCount = 0
                val store = buildStore(eligibilityStatus = EligibilityStatus.Eligible)

                startBinding(repository, store, onShowOnboarding = { shownCount++ })

                store.dispatch(IPProtectionAction.AccountAction.AccountStateChanged(status))
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(1, shownCount)
            }
        }

    @Test
    fun `GIVEN showOnboardingBottomSheet is true and account is ready WHEN eligibility becomes Eligible THEN onShowOnboarding is invoked`() =
        runTest(testDispatcher) {
            val repository =
                FakeIPProtectionPromptRepository(
                    canShowIPProtectionPrompt = true,
                    showOnboardingBottomSheet = true,
                )
            var shownCount = 0
            val store = buildStore(accountStatus = AccountStatus.NoAccount)

            startBinding(repository, store, onShowOnboarding = { shownCount++ })

            store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(1, shownCount)
        }

    @Test
    fun `GIVEN showOnboardingBottomSheet is false WHEN eligibility becomes Eligible THEN onShowOnboarding is not invoked`() =
        runTest(testDispatcher) {
            val repository =
                FakeIPProtectionPromptRepository(
                    canShowIPProtectionPrompt = false,
                    showOnboardingBottomSheet = false,
                )
            var shownCount = 0
            val store = IPProtectionStore()

            startBinding(repository, store, onShowOnboarding = { shownCount++ })

            store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(EligibilityStatus.Eligible))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(0, shownCount)
        }

    private fun startBinding(
        repository: FakeIPProtectionPromptRepository,
        store: IPProtectionStore,
        onShowOnboarding: (PromptDetails?) -> Unit = {},
        onIneligible: () -> Unit = {},
        onAlreadySatisfied: () -> Unit = {},
    ) {
        IPProtectionOnboardingPrompt(
                repository = repository,
                crashReporter = crashReporter,
                onShowOnboarding = onShowOnboarding,
                onIneligible = onIneligible,
                onAlreadySatisfied = onAlreadySatisfied,
                clock = fakeClock,
                mainDispatcher = testDispatcher,
                store = store,
            )
            .start()
        testDispatcher.scheduler.advanceUntilIdle()
    }

    private fun buildStore(
        accountStatus: AccountStatus = AccountStatus.Uninitialized,
        eligibilityStatus: EligibilityStatus = EligibilityStatus.Unknown,
    ): IPProtectionStore {
        return IPProtectionStore(
            initialState =
                IPProtectionState(
                    eligibilityStatus = eligibilityStatus,
                    accountState = AccountState(accountStatus),
                )
        )
    }
}
