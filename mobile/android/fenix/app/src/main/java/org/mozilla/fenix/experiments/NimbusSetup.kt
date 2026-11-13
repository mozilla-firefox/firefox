/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.experiments

import android.content.Context
import mozilla.appservices.remotesettings.RemoteSettingsService
import mozilla.components.service.nimbus.NimbusApi
import mozilla.components.service.nimbus.NimbusAppInfo
import mozilla.components.service.nimbus.NimbusBuilder
import mozilla.components.service.nimbus.messaging.FxNimbusMessaging
import mozilla.components.service.nimbus.messaging.NimbusSystem
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.experiments.nimbus.NimbusInterface
import org.mozilla.experiments.nimbus.internal.GeckoPrefHandler
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.mozilla.experiments.nimbus.internal.NimbusServerSettings
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.messaging.CustomAttributeProvider
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.utils.Settings

/**
 * The maximum amount of time the app launch will be blocked to load experiments from disk.
 *
 * ⚠️ This value was decided from analyzing the Focus metrics (nimbus_initial_fetch) for the ideal timeout. We should
 * NOT change this value without collecting more metrics first.
 */
private const val TIME_OUT_LOADING_EXPERIMENT_FROM_DISK_MS = 200L

private val logger = Logger("service/Nimbus")

/** Create the Nimbus singleton object for the Fenix app. */
fun createNimbus(
    context: Context,
    settings: Settings,
    urlString: String?,
    remoteSettingsService: RemoteSettingsService?,
    geckoPrefHandler: GeckoPrefHandler,
): NimbusApi {
    // These values can be used in the JEXL expressions when targeting experiments.
    val customTargetingAttributes = CustomAttributeProvider.getCustomTargetingAttributes(context)

    val isAppFirstRun = settings.isFirstNimbusRun
    if (isAppFirstRun) {
        settings.isFirstNimbusRun = false
    }

    val recordedNimbusContext =
        RecordedNimbusContext.create(
            context = context,
            isFirstRun = isAppFirstRun,
        )

    val serverSettings: NimbusServerSettings? = remoteSettingsService?.let { service ->
        NimbusServerSettings(
            rsService = service,
            collectionName =
                if (settings.nimbusUsePreview) {
                    "nimbus-preview"
                } else {
                    "nimbus-mobile-experiments"
                },
        )
    }

    // The name "fenix" here corresponds to the app_name defined for the family of apps
    // that encompasses all of the channels for the Fenix app.  This is defined upstream in
    // the telemetry system. For more context on where the app_name come from see:
    // https://probeinfo.telemetry.mozilla.org/v2/glean/app-listings
    // and
    // https://github.com/mozilla/probe-scraper/blob/master/repositories.yaml
    val appInfo =
        NimbusAppInfo(
            appName = "fenix",
            // Note: Using BuildConfig.BUILD_TYPE is important here so that it matches the value
            // passed into Glean. `Config.channel.toString()` turned out to be non-deterministic
            // and would mostly produce the value `Beta` and rarely would produce `beta`.
            channel = BuildConfig.BUILD_TYPE.let { if (it == "debug") "developer" else it },
            customTargetingAttributes = customTargetingAttributes,
        )

    return NimbusBuilder(context)
        .apply {
            url = urlString
            errorReporter = context::reportError
            initialExperiments = R.raw.initial_experiments
            timeoutLoadingExperiment = TIME_OUT_LOADING_EXPERIMENT_FROM_DISK_MS
            sharedPreferences = settings.preferences
            isFirstRun = isAppFirstRun
            featureManifest = FxNimbus
            onFetchCallback = {
                settings.nimbusExperimentsFetched = true
            }
            recordedContext = recordedNimbusContext
            this.geckoPrefHandler = geckoPrefHandler
        }
        .build(appInfo, serverSettings)
        .also { nimbusApi ->
            nimbusApi.recordIsReady(FxNimbus.features.nimbusIsReady.value().eventCount)
        }
}

private fun Context.reportError(message: String, e: Throwable) {
    if (BuildConfig.BUILD_TYPE == "debug") {
        logger.error("Nimbus error: $message", e)
    }
    if (e !is NimbusException || e.isReportableError()) {
        @Suppress("TooGenericExceptionCaught")
        try {
            this.components.analytics.crashReporter.submitCaughtException(e)
        } catch (e: Throwable) {
            logger.error(message, e)
        }
    }
}

/**
 * Classifies which errors we should forward to our crash reporter or not. We want to filter out the non-reportable ones
 * if we know there is no reasonable action that we can perform.
 *
 * This fix should be upstreamed as part of: https://github.com/mozilla/application-services/issues/4333
 */
fun NimbusException.isReportableError(): Boolean {
    return when (this) {
        is NimbusException.ClientException -> false
        else -> true
    }
}

/**
 * Call `fetchExperiments` if the time since the last fetch is over a threshold.
 *
 * The threshold is given by the [NimbusSystem] feature object, defined in the `nimbus.fml.yaml` file.
 */
fun NimbusInterface.maybeFetchExperiments(
    settings: Settings,
    feature: NimbusSystem = FxNimbusMessaging.features.nimbusSystem.value(),
    currentTimeMillis: Long = System.currentTimeMillis(),
) {
    if (settings.nimbusUsePreview) {
        settings.nimbusLastFetchTime = 0L
        fetchExperiments()
    } else {
        val minimumPeriodMinutes = feature.refreshIntervalForeground
        val lastFetchTimeMillis = settings.nimbusLastFetchTime
        val minimumPeriodMillis = minimumPeriodMinutes * Settings.ONE_MINUTE_MS

        if (currentTimeMillis - lastFetchTimeMillis >= minimumPeriodMillis) {
            settings.nimbusLastFetchTime = currentTimeMillis
            fetchExperiments()
        }
    }
}
