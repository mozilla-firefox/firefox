/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.metrics

import android.content.Context
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.RunWhenReadyQueue
import mozilla.telemetry.glean.Glean
import org.mozilla.experiments.nimbus.Nimbus.Pings as NimbusPings
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.ext.components

private class EventWrapper<T : Enum<T>>(
    private val recorder: ((Map<T, String>?) -> Unit),
    private val keyMapper: ((String) -> T)? = null,
) {

    /** Converts snake_case string to camelCase. */
    private fun String.asCamelCase(): String {
        val parts = split("_")
        val builder = StringBuilder()

        for ((index, part) in parts.withIndex()) {
            if (index == 0) {
                builder.append(part)
            } else {
                builder.append(part[0].uppercase())
                builder.append(part.substring(1))
            }
        }

        return builder.toString()
    }

    fun track(event: Event) {
        val extras =
            if (keyMapper != null) {
                event.extras?.mapKeys { (key) ->
                    keyMapper.invoke(key.toString().asCamelCase())
                }
            } else {
                null
            }

        @Suppress("DEPRECATION")
        // FIXME(#19967): Migrate to non-deprecated API.
        this.recorder(extras)
    }
}

@Suppress("DEPRECATION")
// FIXME(#19967): Migrate to non-deprecated API.
private val Event.wrapper: EventWrapper<*>?
    get() = null

/** Service responsible for sending the activation and installation pings. */
class GleanMetricsService(
    private val context: Context,
    private val runWhenReadyQueue: RunWhenReadyQueue = context.components.performance.visualCompletenessQueue,
) : MetricsService {
    override val type = MetricServiceType.Data

    private val logger = Logger("GleanMetricsService")
    private var initialized = false

    private val activationPing = ActivationPing(context)

    override fun start() {
        logger.debug("Enabling Glean.")
        // Initialization of Glean already happened in FenixApplication.
        Glean.setCollectionEnabled(true)

        if (initialized) return
        initialized = true

        // The code below doesn't need to execute immediately, so we'll add them to the visual
        // completeness task queue to be run later.
        runWhenReadyQueue.runIfReadyOrQueue {
            // We have to initialize Glean *on* the main thread, because it registers lifecycle
            // observers. However, the activation ping must be sent *off* of the main thread,
            // because it calls Google ad APIs that must be called *off* of the main thread.
            // These two things actually happen in parallel, but that should be ok because Glean
            // can handle events being recorded before it's initialized.
            Glean.registerPings(Pings)
            Glean.registerPings(NimbusPings.nimbusTargetingContext)

            activationPing.checkAndSend()
        }
    }

    override fun stop() {
        Glean.setCollectionEnabled(false)
    }

    override fun track(event: Event) {
        event.wrapper?.track(event)
    }

    override fun shouldTrack(event: Event): Boolean {
        return event.wrapper != null
    }
}
