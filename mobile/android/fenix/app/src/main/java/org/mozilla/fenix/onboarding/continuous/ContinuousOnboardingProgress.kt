/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import mozilla.components.service.nimbus.messaging.FxNimbusMessaging
import mozilla.components.service.nimbus.messaging.use
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.experiments.nimbus.NimbusMessagingHelperInterface
import org.mozilla.experiments.nimbus.internal.NimbusException
import org.mozilla.fenix.utils.Settings

/** The trigger, in `messaging-fenix.fml.yaml`, that is true once continuous onboarding has finished. */
private const val FINISHED_TRIGGER = "CO_FINISHED"

/**
 * Returns whether continuous onboarding is in progress, in which case other prompts should not be shown.
 *
 * For installs on the Messaging System, "finished" is defined in configuration, by the [FINISHED_TRIGGER] trigger.
 *
 * @param createJexlHelper Creates the helper used to evaluate the trigger.
 */
fun Settings.isContinuousOnboardingInProgress(createJexlHelper: () -> NimbusMessagingHelperInterface): Boolean {
    if (!continuousOnboardingFeatureEnabled) return false
    // Remove when ContinuousOnboardingFeature is retired, see Continuous-onboarding-via-messaging.md.
    if (!continuousOnboardingViaMessaging) return !continuousOnboardingCompleted

    val finished = FxNimbusMessaging.features.messaging.value().triggers[FINISHED_TRIGGER] ?: return false
    return try {
        !createJexlHelper().use { it.evalJexl(finished) }
    } catch (e: NimbusException) {
        Logger("ContinuousOnboardingProgress").error("Unable to evaluate $FINISHED_TRIGGER", e)
        false
    }
}
