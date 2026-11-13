/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.experiments

import android.content.Context
import mozilla.appservices.remotesettings.RemoteSettingsService
import mozilla.components.service.nimbus.NimbusApi
import mozilla.components.service.nimbus.NimbusAppInfo
import mozilla.components.service.nimbus.NimbusBuilder
import mozilla.components.support.base.log.logger.Logger
import org.json.JSONObject
import org.mozilla.experiments.nimbus.NimbusInterface
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.mozilla.experiments.nimbus.internal.NimbusServerSettings
import org.mozilla.focus.BuildConfig
import org.mozilla.focus.R
import org.mozilla.focus.ext.components
import org.mozilla.focus.ext.settings
import org.mozilla.focus.nimbus.FocusNimbus

/**
 * The maximum amount of time the app launch will be blocked to load experiments from disk.
 *
 * ⚠️ This value was decided from analyzing the Focus metrics (nimbus_initial_fetch) for the ideal timeout. We should
 * NOT change this value without collecting more metrics first.
 */
private const val TIME_OUT_LOADING_EXPERIMENT_FROM_DISK_MS = 200L

/** Create the Nimbus singleton object for the Focus/Klar apps. */
fun createNimbus(context: Context, urlString: String?, remoteSettingsService: RemoteSettingsService?): NimbusApi {
    val isAppFirstRun = context.settings.isFirstRun

    // These values can be used in the JEXL expressions when targeting experiments.
    val customTargetingAttributes =
        JSONObject().apply {
            // By convention, we should use snake case.
            put("is_first_run", isAppFirstRun)

            // This camelCase attribute is a boolean value represented as a string.
            // This is left for backwards compatibility.
            put("isFirstRun", isAppFirstRun.toString())
        }

    // The name "focus-android" or "klar-android" here corresponds to the app_name defined
    // for the family of apps that encompasses all of the channels for the Focus app.
    // This is defined upstream in the telemetry system. For more context on where the
    // app_name come from see:
    // https://probeinfo.telemetry.mozilla.org/v2/glean/app-listings
    // and
    // https://github.com/mozilla/probe-scraper/blob/master/repositories.yaml
    val appInfo =
        NimbusAppInfo(
            appName = getNimbusAppName(),
            // Note: Using BuildConfig.BUILD_TYPE is important here so that it matches the value
            // passed into Glean. `Config.channel.toString()` turned out to be non-deterministic
            // and would mostly produce the value `Beta` and rarely would produce `beta`.
            channel = BuildConfig.BUILD_TYPE,
            customTargetingAttributes = customTargetingAttributes,
        )

    val serverSettings: NimbusServerSettings? = remoteSettingsService?.let { service ->
        NimbusServerSettings(
            rsService = service,
            collectionName =
                if (context.settings.shouldUseNimbusPreview) {
                    "nimbus-preview"
                } else {
                    "nimbus-mobile-experiments"
                },
        )
    }

    return NimbusBuilder(context)
        .apply {
            url = urlString
            errorReporter = { message, e ->
                Logger.error("Nimbus error: $message", e)
                if (e !is NimbusException || e.isReportableError()) {
                    context.components.crashReporter.submitCaughtException(e)
                }
            }
            initialExperiments = R.raw.initial_experiments
            timeoutLoadingExperiment = TIME_OUT_LOADING_EXPERIMENT_FROM_DISK_MS
            sharedPreferences = context.settings.preferences
            isFirstRun = isAppFirstRun
            featureManifest = FocusNimbus
        }
        .build(appInfo, serverSettings)
}

internal fun finishNimbusInitialization(experiments: NimbusApi) = experiments.run {
    // We fetch experiments in all cases,
    if (context.settings.isFirstRun) {
        // … however on first run, we immediately apply pending experiments.
        // We also want to measure how long this will take, with Glean.
        register(
            object : NimbusInterface.Observer {
                override fun onExperimentsFetched() {
                    applyPendingExperiments()
                    // Remove lingering observer when we're done fetching experiments on startup.
                    unregister(this)
                }
            }
        )
    }
    fetchExperiments()
}

/** Returns the app name used for Nimbus experimentation. */
fun getNimbusAppName(): String {
    return if (BuildConfig.FLAVOR.contains("focus")) {
        "focus_android"
    } else {
        "klar_android"
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
