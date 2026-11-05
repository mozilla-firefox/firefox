/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.gecko

import android.content.Context
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.CoroutineScope
import mozilla.components.browser.engine.gecko.autofill.GeckoAutocompleteStorageDelegate
import mozilla.components.browser.engine.gecko.crash.GeckoCrashPullDelegate
import mozilla.components.browser.engine.gecko.ext.toContentBlockingSetting
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.experiment.NimbusExperimentDelegate
import mozilla.components.lib.crash.handler.CrashHandlerService
import mozilla.components.lib.crash.store.CrashAction
import mozilla.components.service.sync.autofill.GeckoCreditCardsAddressesStorageDelegate
import mozilla.components.service.sync.logins.GeckoLoginStorageDelegate
import org.mozilla.fenix.Config
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings

object GeckoProvider {
    private var runtime: GeckoRuntime? = null

    /**
     * Gets the existing [GeckoRuntime] instance or creates a new one if it hasn't been initialized. This method is
     * synchronized to ensure that only a single instance of the runtime is created.
     *
     * @param context The application context used to initialize the runtime and its settings.
     * @param autofillStorage Lazy provider for credit card and address storage.
     * @param loginStorage Lazy provider for login and password storage.
     * @param trackingProtectionPolicy The policy defining how tracking protection should be configured.
     * @param applicationScope The [CoroutineScope] used for background operations within the autocomplete delegate.
     * @return The singleton [GeckoRuntime] instance.
     */
    @Synchronized
    fun getOrCreateRuntime(
        context: Context,
        autofillStorage: Lazy<CreditCardsAddressesStorage>,
        loginStorage: Lazy<LoginsStorage>,
        trackingProtectionPolicy: TrackingProtectionPolicy,
        applicationScope: CoroutineScope,
    ): GeckoRuntime {
        if (runtime == null) {
            runtime = createRuntime(context, autofillStorage, loginStorage, trackingProtectionPolicy, applicationScope)
        }

        return runtime!!
    }

    private fun createRuntime(
        context: Context,
        autofillStorage: Lazy<CreditCardsAddressesStorage>,
        loginStorage: Lazy<LoginsStorage>,
        policy: TrackingProtectionPolicy,
        applicationScope: CoroutineScope,
    ): GeckoRuntime {
        val runtimeSettings = createRuntimeSettings(context, policy)

        val settings = context.components.settings
        if (!settings.shouldUseAutoSize) {
            runtimeSettings.automaticFontSizeAdjustment = false
            val fontSize = settings.fontSizeFactor
            runtimeSettings.fontSizeFactor = fontSize
        }

        val geckoRuntime = GeckoRuntime.create(context, runtimeSettings)

        geckoRuntime.autocompleteStorageDelegate =
            GeckoAutocompleteStorageDelegate(
                GeckoCreditCardsAddressesStorageDelegate(
                    storage = autofillStorage,
                    isCreditCardAutofillEnabled = { context.components.settings.shouldAutofillCreditCardDetails },
                    isAddressAutofillEnabled = { context.components.settings.shouldAutofillAddressDetails },
                ),
                GeckoLoginStorageDelegate(
                    loginStorage = loginStorage,
                    isLoginAutofillEnabled = { context.components.settings.shouldAutofillLogins },
                ),
                applicationScope = applicationScope,
            )

        geckoRuntime.crashPullDelegate =
            GeckoCrashPullDelegate(
                dispatcher = { crashIDs ->
                    context.components.appStore.dispatch(
                        AppAction.CrashActionWrapper(CrashAction.CheckDeferred(crashIDs.toList()))
                    )
                }
            )

        return geckoRuntime
    }

    @VisibleForTesting
    internal fun createRuntimeSettings(
        context: Context,
        policy: TrackingProtectionPolicy,
    ): GeckoRuntimeSettings {
        val builder =
            GeckoRuntimeSettings.Builder()
                .proguardUuid(context.components.buildMetadata.proguardUuid)
                .crashHandler(CrashHandlerService::class.java)
                .experimentDelegate(NimbusExperimentDelegate())
                .contentBlocking(
                    policy.toContentBlockingSetting(
                        queryParameterStripping = false,
                        queryParameterStrippingPrivateBrowsing = false,
                        queryParameterStrippingAllowList = "",
                        queryParameterStrippingStripList = "",
                        allowListBaselineTrackingProtection =
                            context.components.settings.strictAllowListBaselineTrackingProtection,
                        allowListConvenienceTrackingProtection =
                            context.components.settings.strictAllowListConvenienceTrackingProtection,
                        safeBrowsingGlobalCacheEnabled = Config.channel.isNightlyOrDebug,
                        safeBrowsingRealTimeEnabled = Config.channel.isNightlyOrDebug,
                        safeBrowsingRealTimeSimulationEnabled = Config.channel.isNightlyOrDebug,
                        safeBrowsingRealTimeSimulationHitProbability = 5,
                        safeBrowsingRealTimeSimulationCacheTTLSec = 300,
                        safeBrowsingRealTimeSimulationNegativeCacheEnabled = false,
                        safeBrowsingRealTimeSimulationNegativeCacheTTLSec = 300,
                    )
                )
                .consoleOutput(context.components.settings.enableGeckoLogs)
                .debugLogging(Config.channel.isDebug || context.components.settings.enableGeckoLogs)
                .aboutConfigEnabled(Config.channel.isBeta || Config.channel.isNightlyOrDebug)
                .extensionsProcessEnabled(true)
                .extensionsWebAPIEnabled(true)
                .translationsOfferPopup(context.components.settings.offerTranslation)
                .crashPullNeverShowAgain(context.components.settings.crashPullNeverShowAgain)
                .setSameDocumentNavigationOverridesLoadType(
                    FxNimbus.features.sameDocumentNavigationOverridesLoadType.value().enabled
                )
                .setSameDocumentNavigationOverridesLoadTypeForceDisable(
                    FxNimbus.features.sameDocumentNavigationOverridesLoadType.value().forceDisableUri
                )
                .isolatedProcessEnabled(context.components.settings.isIsolatedProcessEnabled)
                .appZygoteProcessEnabled(context.components.settings.isAppZygoteEnabled)

        if (FxNimbus.features.fission.value().shouldUseNimbus) {
            builder.fissionEnabled(FxNimbus.features.fission.value().enabled)
        }

        return builder.build()
    }
}
