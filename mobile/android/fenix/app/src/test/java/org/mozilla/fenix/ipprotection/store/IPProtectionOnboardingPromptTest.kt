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
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountState
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import org.junit.Assert.assertEquals
import org.junit.Before
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

    @Before
    fun setUp() {
        FxNimbus.features.ipProtection.withCachedValue(
            IpProtection(promoDeadline = "2026-06-09", dataLimitGigabyte = 50, gpiDataLimitGigabytes = 5)
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
    fun `GIVEN an active promo WHEN both data limits are zero THEN the promo is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-11", dataLimitGigabyte = 0, gpiDataLimitGigabytes = 0)
            )
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            val prompts = mutableListOf<PromptDetails?>()

            startBinding(store = store, onShowOnboarding = { prompts.add(it) })

            assertEquals(listOf(PromptDetails.UnlimitedDataPromo(LocalDate.of(2026, 6, 11))), prompts)
            verify(exactly = 0) { crashReporter.submitCaughtException(any()) }
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
    fun `WHEN the nimbus promo deadline has an unexpected format THEN a regular prompt is shown and the error is reported`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-10T00:00:00Z", dataLimitGigabyte = 55, gpiDataLimitGigabytes = 5)
            )
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            val prompts = mutableListOf<PromptDetails?>()

            startBinding(store = store, onShowOnboarding = { prompts.add(it) })

            assertEquals(listOf(PromptDetails.Regular(55)), prompts)
            verify(exactly = 1) { crashReporter.submitCaughtException(any<DateTimeParseException>()) }
        }

    @Test
    fun `WHEN the nimbus promo deadline is empty THEN a regular prompt is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(IpProtection(promoDeadline = "", dataLimitGigabyte = 50))
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            val prompts = mutableListOf<PromptDetails?>()

            startBinding(store = store, onShowOnboarding = { prompts.add(it) })

            assertEquals(listOf(PromptDetails.Regular(50)), prompts)
            verify(exactly = 0) { crashReporter.submitCaughtException(any()) }
        }

    @Test
    fun `GIVEN a regular prompt WHEN the FxA data limit is negative THEN the configuration is rejected`() =
        runTest(testDispatcher) {
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            assertRejectsNimbusConfig(store, fxaDataLimit = -1, gpiDataLimit = 5)
        }

    @Test
    fun `GIVEN a regular prompt WHEN the FxA data limit is zero THEN the configuration is rejected`() =
        runTest(testDispatcher) {
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            assertRejectsNimbusConfig(store, fxaDataLimit = 0, gpiDataLimit = 5)
        }

    @Test
    fun `GIVEN a GPI prompt WHEN the GPI data limit is zero THEN the configuration is rejected`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    AccountStatus.NoAccount,
                    EligibilityStatus.Eligible,
                    ServiceState.Ready,
                    CurrentAuthProvider.Gpi,
                )
            assertRejectsNimbusConfig(store, fxaDataLimit = 50, gpiDataLimit = 0)
        }

    @Test
    fun `GIVEN a GPI prompt WHEN the GPI data limit is negative THEN the configuration is rejected`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    AccountStatus.NoAccount,
                    EligibilityStatus.Eligible,
                    ServiceState.Ready,
                    CurrentAuthProvider.Gpi,
                )
            assertRejectsNimbusConfig(store, fxaDataLimit = 50, gpiDataLimit = -1)
        }

    @Test
    fun `GIVEN a GPI prompt WHEN the FxA data limit is zero THEN the configuration is rejected`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    AccountStatus.NoAccount,
                    EligibilityStatus.Eligible,
                    ServiceState.Ready,
                    CurrentAuthProvider.Gpi,
                )
            assertRejectsNimbusConfig(store, fxaDataLimit = 0, gpiDataLimit = 5)
        }

    @Test
    fun `GIVEN GPI is ready WHEN the promo deadline is malformed THEN the no-account prompt is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "not-a-date", dataLimitGigabyte = 50, gpiDataLimitGigabytes = 5)
            )
            val store =
                buildStore(
                    AccountStatus.NoAccount,
                    EligibilityStatus.Eligible,
                    ServiceState.Ready,
                    CurrentAuthProvider.Gpi,
                )
            val prompts = mutableListOf<PromptDetails?>()

            startBinding(store = store, onShowOnboarding = { prompts.add(it) })

            assertEquals(listOf(PromptDetails.GpiPrompt(gpiMaxGigabytes = 5, fxaMaxGigabytes = 50)), prompts)
            verify(exactly = 1) { crashReporter.submitCaughtException(any<DateTimeParseException>()) }
        }

    @Test
    fun `GIVEN promo is not active and ServiceState is Ready, WHEN the auth provider is GPI THEN the no-account prompt is shown with data limits from Nimbus`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-09", dataLimitGigabyte = 50, gpiDataLimitGigabytes = 5)
            )
            val store =
                buildStore(
                    AccountStatus.NoAccount,
                    EligibilityStatus.Eligible,
                    ServiceState.Ready,
                    CurrentAuthProvider.Gpi,
                )

            var actualPromptDetails: PromptDetails? = null

            startBinding(
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(
                PromptDetails.GpiPrompt(gpiMaxGigabytes = 5, fxaMaxGigabytes = 50),
                actualPromptDetails,
            )
        }

    @Test
    fun `GIVEN an eligible signed-out user WHEN the service is uninitialized THEN no onboarding callback is invoked`() =
        runTest(testDispatcher) {
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible, ServiceState.Uninitialized)
            var shownCount = 0
            var alreadySatisfiedCount = 0

            startBinding(
                store = store,
                onShowOnboarding = { shownCount++ },
                onAlreadySatisfied = { alreadySatisfiedCount++ },
            )

            assertEquals(0, shownCount)
            assertEquals(0, alreadySatisfiedCount)
            verify(exactly = 0) { crashReporter.submitCaughtException(any()) }
        }

    @Test
    fun `GIVEN the service is uninitialized WHEN the repository disallows the prompt THEN onAlreadySatisfied is invoked`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    accountStatus = AccountStatus.NoAccount,
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceState = ServiceState.Uninitialized,
                    currentAuthProvider = CurrentAuthProvider.Unknown,
                )
            val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = false)
            var shownCount = 0
            var alreadySatisfiedCount = 0

            startBinding(
                repository = repository,
                store = store,
                onShowOnboarding = { shownCount++ },
                onAlreadySatisfied = { alreadySatisfiedCount++ },
            )

            assertEquals(0, shownCount)
            assertEquals(1, alreadySatisfiedCount)
        }

    @Test
    fun `GIVEN the auth provider is GPI WHEN the service becomes ready THEN only the no-account prompt is shown`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    AccountStatus.NoAccount,
                    EligibilityStatus.Eligible,
                    ServiceState.Uninitialized,
                    CurrentAuthProvider.Gpi,
                )
            val prompts = mutableListOf<PromptDetails?>()

            startBinding(store = store, onShowOnboarding = { prompts.add(it) })

            assertEquals(emptyList<PromptDetails>(), prompts)

            store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Unauthenticated))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(emptyList<PromptDetails>(), prompts)

            store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Ready))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(PromptDetails.GpiPrompt(gpiMaxGigabytes = 5, fxaMaxGigabytes = 50)), prompts)
        }

    @Test
    fun `GIVEN the service is ready WHEN GPI is reported THEN only the no-account prompt is shown`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    AccountStatus.NoAccount,
                    EligibilityStatus.Eligible,
                    ServiceState.Ready,
                    CurrentAuthProvider.Unknown,
                )
            val prompts = mutableListOf<PromptDetails?>()

            startBinding(store = store, onShowOnboarding = { prompts.add(it) })
            assertEquals(emptyList<PromptDetails?>(), prompts)

            store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Gpi))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(PromptDetails.GpiPrompt(gpiMaxGigabytes = 5, fxaMaxGigabytes = 50)), prompts)
        }

    @Test
    fun `WHEN the service falls back to unauthenticated FxA THEN only the regular prompt is shown`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    AccountStatus.NoAccount,
                    EligibilityStatus.Eligible,
                    ServiceState.Uninitialized,
                    CurrentAuthProvider.Unknown,
                )
            val prompts = mutableListOf<PromptDetails?>()

            startBinding(store = store, onShowOnboarding = { prompts.add(it) })

            assertEquals(emptyList<PromptDetails>(), prompts)

            store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Fxa))
            store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Unauthenticated))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(listOf(PromptDetails.Regular(50)), prompts)
        }

    @Test
    fun `GIVEN a promo is active WHEN the auth provider is GPI THEN the promo is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-20", dataLimitGigabyte = 50, gpiDataLimitGigabytes = 5)
            )
            val store =
                buildStore(
                    AccountStatus.NoAccount,
                    EligibilityStatus.Eligible,
                    ServiceState.Ready,
                    CurrentAuthProvider.Gpi,
                )
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(
                PromptDetails.UnlimitedDataPromo(promoDeadline = LocalDate.of(2026, 6, 20)),
                actualPromptDetails,
            )
        }

    @Test
    fun `GIVEN a regular prompt WHEN the GPI data limit is zero THEN the regular prompt is shown`() =
        runTest(testDispatcher) {
            FxNimbus.features.ipProtection.withCachedValue(
                IpProtection(promoDeadline = "2026-06-09", dataLimitGigabyte = 50, gpiDataLimitGigabytes = 0)
            )
            val store = buildStore(AccountStatus.NoAccount, EligibilityStatus.Eligible)
            val prompts = mutableListOf<PromptDetails?>()

            startBinding(store = store, onShowOnboarding = { prompts.add(it) })

            assertEquals(listOf(PromptDetails.Regular(50)), prompts)
            verify(exactly = 0) { crashReporter.submitCaughtException(any()) }
        }

    @Test
    fun `GIVEN a signed-in account WHEN the auth provider is FxA THEN the regular prompt is shown`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    accountStatus = AccountStatus.Authenticated,
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceState = ServiceState.Ready,
                    currentAuthProvider = CurrentAuthProvider.Fxa,
                )
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(PromptDetails.Regular(50), actualPromptDetails)
        }

    @Test
    fun `GIVEN an account is signed in WHEN auth provider is gpi THEN the regular prompt is shown`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    accountStatus = AccountStatus.Authenticated,
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceState = ServiceState.Ready,
                    currentAuthProvider = CurrentAuthProvider.Gpi,
                )
            var actualPromptDetails: PromptDetails? = null

            startBinding(
                store = store,
                onShowOnboarding = { actualPromptDetails = it },
            )

            assertEquals(PromptDetails.Regular(50), actualPromptDetails)
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
                val store = IPProtectionStore()

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
                val store = IPProtectionStore()

                startBinding(repository, store, onShowOnboarding = {}, onIneligible = { ineligibleCount++ })

                store.dispatch(IPProtectionAction.EligibilityAction.EligibilityChanged(ineligibleStatus))
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(1, ineligibleCount)
            }
        }

    @Test
    fun `WHEN the service is unavailable THEN onIneligible waits for the auth provider`() =
        runTest(testDispatcher) {
            val store =
                buildStore(
                    eligibilityStatus = EligibilityStatus.Eligible,
                    serviceState = ServiceState.Uninitialized,
                    currentAuthProvider = CurrentAuthProvider.Unknown,
                )
            var ineligibleCount = 0

            startBinding(store = store, onIneligible = { ineligibleCount++ })

            assertEquals(0, ineligibleCount)

            store.dispatch(IPProtectionAction.ServiceAction.ServiceStateChanged(ServiceState.Unavailable))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(0, ineligibleCount)

            store.dispatch(IPProtectionAction.ServiceAction.CurrentAuthProviderChanged(CurrentAuthProvider.Fxa))
            testDispatcher.scheduler.advanceUntilIdle()

            assertEquals(1, ineligibleCount)
        }

    @Test
    fun `WHEN eligibility is Eligible THEN onIneligible is not invoked`() =
        runTest(testDispatcher) {
            val repository = FakeIPProtectionPromptRepository(canShowIPProtectionPrompt = true)
            var ineligibleCount = 0
            val store = buildStore(accountStatus = AccountStatus.Authenticated)

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

    private fun assertRejectsNimbusConfig(
        store: IPProtectionStore,
        fxaDataLimit: Int,
        gpiDataLimit: Int,
    ) {
        FxNimbus.features.ipProtection.withCachedValue(
            IpProtection(
                promoDeadline = "2026-06-09",
                dataLimitGigabyte = fxaDataLimit,
                gpiDataLimitGigabytes = gpiDataLimit,
            )
        )
        val prompts = mutableListOf<PromptDetails?>()

        startBinding(store = store, onShowOnboarding = { prompts.add(it) })

        assertEquals(listOf<PromptDetails?>(null), prompts)
        verify(exactly = 1) {
            crashReporter.submitCaughtException(any<MalformedIPProtectionNimbusConfigException>())
        }
    }

    private fun startBinding(
        repository: FakeIPProtectionPromptRepository = FakeIPProtectionPromptRepository(),
        store: IPProtectionStore,
        clock: Clock = fakeClock,
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
                clock = clock,
                mainDispatcher = testDispatcher,
                store = store,
            )
            .start()
        testDispatcher.scheduler.advanceUntilIdle()
    }

    private fun buildStore(
        accountStatus: AccountStatus = AccountStatus.Uninitialized,
        eligibilityStatus: EligibilityStatus = EligibilityStatus.Unknown,
        serviceState: ServiceState = ServiceState.Unauthenticated,
        currentAuthProvider: CurrentAuthProvider = CurrentAuthProvider.Fxa,
    ): IPProtectionStore {
        return IPProtectionStore(
            initialState =
                IPProtectionState(
                    eligibilityStatus = eligibilityStatus,
                    accountState = AccountState(accountStatus),
                    serviceStatus = serviceState,
                    currentAuthProvider = currentAuthProvider,
                )
        )
    }
}
