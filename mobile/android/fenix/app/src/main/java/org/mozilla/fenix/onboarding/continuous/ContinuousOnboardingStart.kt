/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.utils.Settings

/** Nimbus event recorded when the initial onboarding flow is completed. */
const val ONBOARDING_COMPLETED_EVENT_ID = "onboarding_completed"

// The `messagingSystemEnabled` parameter and the `continuousOnboardingViaMessaging` assignment:
// Remove when ContinuousOnboardingFeature is retired, see Continuous-onboarding-via-messaging.md.
/**
 * Starts continuous onboarding once initial onboarding is completed: records [ONBOARDING_COMPLETED_EVENT_ID], and
 * decides, for good, whether this user's continuous onboarding is driven by the Messaging System.
 *
 * @param eventStore The [NimbusEventStore] to record the event in.
 * @param messagingSystemEnabled Whether continuous onboarding via the Messaging System is enabled.
 */
fun Settings.startContinuousOnboarding(
    eventStore: NimbusEventStore,
    messagingSystemEnabled: Boolean = FxNimbus.features.continuousOnboarding.value().messagingSystemEnabled,
) {
    continuousOnboardingViaMessaging = messagingSystemEnabled
    eventStore.recordEvent(count = 1, eventId = ONBOARDING_COMPLETED_EVENT_ID)
}
