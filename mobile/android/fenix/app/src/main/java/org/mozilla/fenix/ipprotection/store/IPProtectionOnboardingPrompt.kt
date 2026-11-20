/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import java.time.Clock
import java.time.LocalDate
import java.time.format.DateTimeParseException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.ipprotection.CurrentAuthProvider
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.EligibilityStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.helpers.AbstractBinding
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptState.PromptDetails
import org.mozilla.fenix.nimbus.FxNimbus

/**
 * Triggers the IP protection onboarding bottom sheet when the user first becomes eligible and meets our required
 * heuristic provided by [IPProtectionPromptRepository].
 *
 * @param repository Source of truth for whether the onboarding prompt is still allowed to appear (feature flag, install
 *   age, prior dismissals, prior VPN usage).
 * @param crashReporter Reports invalid promo configuration.
 * @param onShowOnboarding Callback invoked when the prompt should be presented to the user.
 * @param onIneligible Callback invoked when the user is not eligible for the IP Protection feature.
 * @param onAlreadySatisfied Callback invoked when the user is eligible but [repository] no longer allows the prompt,
 *   e.g. it was already shown or the user has already used the VPN.
 * @param clock Clock used to decide whether a promo is still active.
 * @param mainDispatcher [CoroutineDispatcher] on which [onShowOnboarding] is invoked.
 * @param store the singleton instance of [IPProtectionStore].
 */
@OptIn(ExperimentalAndroidComponentsApi::class) // ServiceState
class IPProtectionOnboardingPrompt(
    private val repository: IPProtectionPromptRepository,
    private val crashReporter: CrashReporting,
    private val onShowOnboarding: (PromptDetails?) -> Unit,
    private val onIneligible: () -> Unit = {},
    private val onAlreadySatisfied: () -> Unit = {},
    private val clock: Clock = Clock.systemDefaultZone(),
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    store: IPProtectionStore,
) : AbstractBinding<IPProtectionState>(store, mainDispatcher) {
    override suspend fun onState(flow: Flow<IPProtectionState>) {
        flow
            .map {
                PromptCriteria(
                    eligibilityStatus = it.eligibilityStatus,
                    accountStatus = it.accountState.status,
                    serviceStatus = it.serviceStatus,
                    currentAuthProvider = it.currentAuthProvider,
                )
            }
            .distinctUntilChanged()
            .collect { criteria ->
                if (criteria.isIneligible()) {
                    onIneligible()
                    return@collect
                }

                if (!criteria.hasResolvedEligibilityAndAccount()) return@collect

                if (!repository.canShowIPProtectionPrompt()) {
                    onAlreadySatisfied()
                    return@collect
                }

                if (criteria.canSelectPrompt()) {
                    onShowOnboarding(determinePromptDetails(criteria))
                }
            }
    }

    private fun determinePromptDetails(criteria: PromptCriteria): PromptDetails? {
        val feature = FxNimbus.features.ipProtection.value()
        val promoDeadline = feature.promoDeadline.takeIf { it.isNotBlank() }?.let(::parsePromoDeadline)
        if (promoDeadline != null && !promoDeadline.isBefore(LocalDate.now(clock))) {
            return PromptDetails.UnlimitedDataPromo(promoDeadline)
        }

        val useGpiPrompt = criteria.isSatisfiedForGpiUse()
        val fxaDataLimit = feature.dataLimitGigabyte
        val gpiDataLimit = feature.gpiDataLimitGigabytes
        if (fxaDataLimit <= 0 || (useGpiPrompt && gpiDataLimit <= 0)) {
            crashReporter.submitCaughtException(
                MalformedIPProtectionNimbusConfigException(
                    promoDeadline = feature.promoDeadline,
                    fxaDataLimit = fxaDataLimit,
                    gpiDataLimit = gpiDataLimit,
                )
            )
            return null
        }

        return if (useGpiPrompt) {
            PromptDetails.GpiPrompt(gpiMaxGigabytes = gpiDataLimit, fxaMaxGigabytes = fxaDataLimit)
        } else {
            PromptDetails.Regular(fxaDataLimit)
        }
    }

    private fun parsePromoDeadline(promoDeadline: String): LocalDate? =
        try {
            LocalDate.parse(promoDeadline)
        } catch (exception: DateTimeParseException) {
            crashReporter.submitCaughtException(exception)
            null
        }

    /** Criteria for determining whether to show the prompt */
    private data class PromptCriteria(
        val eligibilityStatus: EligibilityStatus,
        val accountStatus: AccountStatus,
        val serviceStatus: ServiceState,
        val currentAuthProvider: CurrentAuthProvider,
    ) {
        fun hasResolvedEligibilityAndAccount(): Boolean = eligibilityStatus.isEligible && accountStatus.isResolved

        fun canSelectPrompt(): Boolean = serviceStatus.isAvailableFor(currentAuthProvider)

        /**
         * Determines whether the criteria is determined to be ready for GPI use.
         *
         * The criteria are satisfied for GPI use when the service is ready, the current auth provider is GPI, and there
         * is no account on-device
         */
        fun isSatisfiedForGpiUse(): Boolean {
            return serviceStatus == ServiceState.Ready &&
                currentAuthProvider == CurrentAuthProvider.Gpi &&
                accountStatus == AccountStatus.NoAccount
        }

        /** Whether the user is ineligible to use IPP (ineligible or in an unsupported region). */
        fun isIneligible(): Boolean {
            return eligibilityStatus == EligibilityStatus.Ineligible ||
                eligibilityStatus == EligibilityStatus.UnsupportedRegion ||
                (serviceStatus == ServiceState.Unavailable && currentAuthProvider.isResolved)
        }

        val EligibilityStatus.isEligible: Boolean
            get() = this == EligibilityStatus.Eligible

        /**
         * The service is available for the current auth provider
         *
         * When using Fxa, the service is considered available when it's ready or unauthenticated. When using GPI, the
         * service is considered available when it's ready
         */
        private fun ServiceState.isAvailableFor(authProvider: CurrentAuthProvider): Boolean =
            when (authProvider) {
                CurrentAuthProvider.Unknown -> false
                CurrentAuthProvider.Fxa -> this == ServiceState.Ready || this == ServiceState.Unauthenticated
                CurrentAuthProvider.Gpi -> this == ServiceState.Ready
            }

        /**
         * Whether the account status is resolved enough for us to start evaluating the prompt criteria.
         *
         * For us, as long as the account is not warming up, or initializing, it is enough to evaluate the prompt
         * criteria
         */
        val AccountStatus.isResolved: Boolean
            get() = this != AccountStatus.WarmingUp && this != AccountStatus.Uninitialized

        /** Auth provider is resolved when it is not unknown */
        val CurrentAuthProvider.isResolved: Boolean
            get() = this != CurrentAuthProvider.Unknown
    }
}

/**
 * Reported when we are unable to determine the correct prompt details due to invalid data from our nimbus configuration
 */
internal class MalformedIPProtectionNimbusConfigException(promoDeadline: String, fxaDataLimit: Int, gpiDataLimit: Int) :
    IllegalStateException(
        "Invalid nimbus configuration for onboarding prompt: promoDeadline=$promoDeadline," +
            " fxaDataLimit=$fxaDataLimit, gpiDataLimit=$gpiDataLimit"
    )
