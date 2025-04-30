/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import mozilla.components.concept.fetch.Client
import mozilla.components.service.glean.net.ConceptFetchHttpUploader
import mozilla.components.support.base.log.logger.Logger
import mozilla.telemetry.glean.Glean
import mozilla.telemetry.glean.config.Configuration
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.Config
import org.mozilla.fenix.GleanMetrics.GleanBuildInfo
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.ext.getCustomGleanServerUrlIfAvailable
import org.mozilla.fenix.ext.setCustomEndpointIfAvailable
import org.mozilla.fenix.nimbus.FxNimbus

/**
 * Helper function to initialize Glean.
 *
 * [applicationContext] the application context required for glean initialization.
 * [logger] the logger to send logs about initializing Glean.
 * [isTelemetryUploadEnabled] indicate if telemetry should be enabled to be uploaded.
 * [client] an instance of [Client] used to upload metrics.
 */
fun initializeGlean(applicationContext: Context, logger: Logger, isTelemetryUploadEnabled: Boolean, client: Client) {
    logger.debug("Initializing Glean (uploadEnabled=$isTelemetryUploadEnabled})")

    // for performance reasons, this is only available in Nightly or Debug builds
    val customEndpoint = if (Config.channel.isNightlyOrDebug) {
        // for testing, if custom glean server url is set in the secret menu, use it to initialize Glean
        getCustomGleanServerUrlIfAvailable(applicationContext)
    } else {
        null
    }

    val configuration = Configuration(
        channel = BuildConfig.BUILD_TYPE,
        httpClient = ConceptFetchHttpUploader(
            lazy(LazyThreadSafetyMode.NONE) { client },
            supportsOhttp = true,
        ),
        enableEventTimestamps = FxNimbus.features.glean.value().enableEventTimestamps,
        delayPingLifetimeIo = FxNimbus.features.glean.value().delayPingLifetimeIo,
        pingLifetimeThreshold = FxNimbus.features.glean.value().pingLifetimeThreshold,
        pingLifetimeMaxTime = FxNimbus.features.glean.value().pingLifetimeMaxTime,
    )

    // Since Glean v63.0.0, custom pings need to be registered prior to Glean init
    // in order to ensure they are enabled and able to collect data.
    // See https://bugzilla.mozilla.org/1934931 for more information.
    Glean.registerPings(Pings)

    // Set the metric configuration from Nimbus.
    Glean.applyServerKnobsConfig(FxNimbus.features.glean.value().metricsEnabled.toString())

    Glean.initialize(
        applicationContext = applicationContext,
        configuration = configuration.setCustomEndpointIfAvailable(customEndpoint),
        uploadEnabled = isTelemetryUploadEnabled,
        buildInfo = GleanBuildInfo.buildInfo,
    )
}
