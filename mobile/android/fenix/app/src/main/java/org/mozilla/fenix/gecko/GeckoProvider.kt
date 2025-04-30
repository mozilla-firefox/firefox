/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.gecko

import android.content.Context
import androidx.annotation.VisibleForTesting
import mozilla.components.browser.engine.gecko.autofill.GeckoAutocompleteStorageDelegate
import mozilla.components.browser.engine.gecko.ext.toContentBlockingSetting
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy
import mozilla.components.concept.storage.CreditCardsAddressesStorage
import mozilla.components.concept.storage.LoginsStorage
import mozilla.components.experiment.NimbusExperimentDelegate
import mozilla.components.lib.crash.handler.CrashHandlerService
import mozilla.components.service.sync.autofill.GeckoCreditCardsAddressesStorageDelegate
import mozilla.components.service.sync.logins.GeckoLoginStorageDelegate
import org.mozilla.fenix.Config
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings

object GeckoProvider {
    private var runtime: GeckoRuntime? = null

    @Synchronized
    fun getOrCreateRuntime(
        context: Context,
        autofillStorage: Lazy<CreditCardsAddressesStorage>,
        loginStorage: Lazy<LoginsStorage>,
        trackingProtectionPolicy: TrackingProtectionPolicy,
    ): GeckoRuntime {
        if (runtime == null) {
            runtime =
                createRuntime(context, autofillStorage, loginStorage, trackingProtectionPolicy)
        }

        return runtime!!
    }

    private fun createRuntime(
        context: Context,
        autofillStorage: Lazy<CreditCardsAddressesStorage>,
        loginStorage: Lazy<LoginsStorage>,
        policy: TrackingProtectionPolicy,
    ): GeckoRuntime {
        val runtimeSettings = createRuntimeSettings(context, policy)

        val settings = context.components.settings
        if (!settings.shouldUseAutoSize) {
            runtimeSettings.automaticFontSizeAdjustment = false
            val fontSize = settings.fontSizeFactor
            runtimeSettings.fontSizeFactor = fontSize
        }

        val geckoRuntime = GeckoRuntime.create(context, runtimeSettings)

        geckoRuntime.autocompleteStorageDelegate = GeckoAutocompleteStorageDelegate(
            GeckoCreditCardsAddressesStorageDelegate(
                storage = autofillStorage,
                isCreditCardAutofillEnabled = { context.settings().shouldAutofillCreditCardDetails },
                isAddressAutofillEnabled = { context.settings().shouldAutofillAddressDetails },
            ),
            GeckoLoginStorageDelegate(
                loginStorage = loginStorage,
                isLoginAutofillEnabled = { context.settings().shouldAutofillLogins },
            ),
        )

        return geckoRuntime
    }

    @VisibleForTesting
    internal fun createRuntimeSettings(
        context: Context,
        policy: TrackingProtectionPolicy,
    ): GeckoRuntimeSettings {
        return GeckoRuntimeSettings.Builder()
            .crashHandler(CrashHandlerService::class.java)
            .experimentDelegate(NimbusExperimentDelegate())
            .contentBlocking(
                policy.toContentBlockingSetting(
                    cookieBannerHandlingMode = context.settings().getCookieBannerHandling(),
                    cookieBannerHandlingModePrivateBrowsing = context.settings()
                        .getCookieBannerHandlingPrivateMode(),
                    cookieBannerHandlingDetectOnlyMode =
                    context.settings().shouldEnableCookieBannerDetectOnly,
                    cookieBannerGlobalRulesEnabled =
                    context.settings().shouldEnableCookieBannerGlobalRules,
                    cookieBannerGlobalRulesSubFramesEnabled =
                    context.settings().shouldEnableCookieBannerGlobalRulesSubFrame,
                    queryParameterStripping =
                    context.settings().shouldEnableQueryParameterStripping,
                    queryParameterStrippingPrivateBrowsing =
                    context.settings().shouldEnableQueryParameterStrippingPrivateBrowsing,
                    queryParameterStrippingAllowList =
                    context.settings().queryParameterStrippingAllowList,
                    queryParameterStrippingStripList =
                    context.settings().queryParameterStrippingStripList,
                ),
            )
            .consoleOutput(context.components.settings.enableGeckoLogs)
            .debugLogging(Config.channel.isDebug || context.components.settings.enableGeckoLogs)
            .aboutConfigEnabled(Config.channel.isBeta || Config.channel.isNightlyOrDebug)
            .extensionsProcessEnabled(true)
            .extensionsWebAPIEnabled(true)
            .translationsOfferPopup(context.settings().offerTranslation)
            .disableShip(FxNimbus.features.ship.value().disabled)
            .fissionEnabled(FxNimbus.features.fission.value().enabled)
            .build()
    }
}
