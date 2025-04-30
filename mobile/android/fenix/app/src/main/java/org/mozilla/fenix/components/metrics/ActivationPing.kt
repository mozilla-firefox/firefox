/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.metrics

import android.content.Context
import android.content.SharedPreferences
import androidx.annotation.VisibleForTesting
import androidx.core.content.edit
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.GleanMetrics.Activation
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.components.metrics.MetricsUtils.getHashedIdentifier

class ActivationPing(
    private val context: Context,
    private val backgroundDispatcher: CoroutineDispatcher = Dispatchers.IO,
) {
    private val prefs: SharedPreferences by lazy {
        context.getSharedPreferences(
            "${this.javaClass.canonicalName}.prefs",
            Context.MODE_PRIVATE,
        )
    }

    /**
     * Checks whether or not the activation ping was already
     * triggered by the application.
     *
     * Note that this only tells us that Fenix triggered the
     * ping and then delegated the transmission to Glean. We
     * have no way to tell if it was actually sent or not.
     *
     * @return true if it was already triggered, false otherwise.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun wasAlreadyTriggered(): Boolean {
        return prefs.getBoolean("ping_sent", false)
    }

    /**
     * Marks the "activation" ping as triggered by the application.
     * This ensures the ping is not triggered again at the next app
     * start.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun markAsTriggered() {
        prefs.edit { putBoolean("ping_sent", true) }
    }

    /**
     * Fills the metrics and triggers the 'activation' ping.
     * This is a separate function to simplify unit-testing.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun triggerPing() {
        // Generate the activation_id.
        Activation.activationId.generateAndSet()

        CoroutineScope(backgroundDispatcher).launch {
            val hashedId = getHashedIdentifier(context)
            if (hashedId != null) {
                Logger.info("ActivationPing - generating ping with the hashed id")
                // We have a valid, hashed Google Advertising ID.
                Activation.identifier.set(hashedId)
            }

            Logger.info("ActivationPing - generating ping (has `identifier`: ${hashedId != null})")
            Pings.activation.submit()
            markAsTriggered()
        }
    }

    /**
     * Trigger sending the `activation` ping if it wasn't sent already.
     * Then, mark it so that it doesn't get triggered next time Fenix
     * starts.
     */
    fun checkAndSend() {
        if (wasAlreadyTriggered()) {
            Logger.debug("ActivationPing - already generated")
            return
        }

        triggerPing()
    }
}
