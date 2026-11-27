/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import mozilla.components.service.nimbus.messaging.Message
import org.mozilla.fenix.onboarding.DismissedMethod
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder.Companion.ET_CARD_CLOSE_BUTTON
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder.Companion.ET_SECONDARY_BUTTON
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingFeature.Companion.IP_PROTECTION_SEQUENCE_ID
import org.mozilla.fenix.onboarding.continuous.ContinuousOnboardingFeature.Companion.NOTIFICATION_SEQUENCE_ID
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData

private const val SEQUENCE_POSITION = "0"
private val DEFAULT_BROWSER_SEQUENCE_ID = OnboardingPageUiData.Type.DEFAULT_BROWSER.telemetryId
private val SYNC_SEQUENCE_ID = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId

/**
 * Records the same onboarding telemetry as `ContinuousOnboardingFeature` for the continuous onboarding messages, on top
 * of the Messaging System's own telemetry. Messages are recognised by their id.
 */
class ContinuousOnboardingMessageTelemetry(private val recorder: OnboardingTelemetryRecorder) {
    private val Message.isNotifications get() = id.startsWith("co-notifications")
    private val Message.isSync get() = id.startsWith("co-sync")

    /** Records that the default browser role request returned with the role granted. */
    fun onDefaultBrowserRoleGranted() {
        recorder.onSetToDefaultClick(sequenceId = DEFAULT_BROWSER_SEQUENCE_ID, sequencePosition = SEQUENCE_POSITION)
    }

    /** Records an impression of the card for [message]. */
    fun onImpression(message: Message) {
        when {
            message.isNotifications ->
                recorder.onNotificationCardImpression(
                    sequenceId = NOTIFICATION_SEQUENCE_ID,
                    sequencePosition = SEQUENCE_POSITION,
                )
            message.isSync ->
                recorder.onImpression(
                    sequenceId = SYNC_SEQUENCE_ID,
                    pageType = OnboardingPageUiData.Type.SYNC_SIGN_IN,
                    sequencePosition = SEQUENCE_POSITION,
                )
        }
    }

    /** Records a click on the primary button of the card for [message]. */
    fun onPrimaryClick(message: Message) {
        when {
            message.isNotifications ->
                recorder.onNotificationPermissionClick(
                    sequenceId = NOTIFICATION_SEQUENCE_ID,
                    sequencePosition = SEQUENCE_POSITION,
                )
            message.isSync -> {
                recorder.onSyncSignInClick(sequenceId = SYNC_SEQUENCE_ID, sequencePosition = SEQUENCE_POSITION)
                recorder.onOnboardingComplete(sequenceId = SYNC_SEQUENCE_ID, sequencePosition = SEQUENCE_POSITION)
            }
        }
    }

    /** Records a click on the secondary button of the card for [message]. */
    fun onSecondaryClick(message: Message) = onSkip(message, elementType = ET_SECONDARY_BUTTON)

    /** Records a click on the close button of the card for [message]. */
    fun onCloseClick(message: Message) = onSkip(message, elementType = ET_CARD_CLOSE_BUTTON)

    /**
     * Records the telemetry `ContinuousOnboardingFeature` sends when a stage is skipped, for a stage-completed
     * [message].
     */
    fun onStageCompleted(message: Message) {
        when (message.id) {
            "co-day-5-skipped" ->
                recorder.onOnboardingComplete(sequenceId = SYNC_SEQUENCE_ID, sequencePosition = SEQUENCE_POSITION)
            "co-day-7-ineligible" ->
                recorder.onOnboardingComplete(
                    sequenceId = IP_PROTECTION_SEQUENCE_ID,
                    sequencePosition = SEQUENCE_POSITION,
                    dismissedMethod = DismissedMethod.SKIPPED,
                )
            "co-day-7-satisfied" ->
                recorder.onOnboardingComplete(
                    sequenceId = IP_PROTECTION_SEQUENCE_ID,
                    sequencePosition = SEQUENCE_POSITION,
                )
        }
    }

    private fun onSkip(message: Message, elementType: String) {
        when {
            message.isNotifications ->
                recorder.onSkipTurnOnNotificationsClick(
                    sequenceId = NOTIFICATION_SEQUENCE_ID,
                    sequencePosition = SEQUENCE_POSITION,
                    elementType = elementType,
                )
            message.isSync -> {
                recorder.onSkipSignInClick(
                    sequenceId = SYNC_SEQUENCE_ID,
                    sequencePosition = SEQUENCE_POSITION,
                    elementType = elementType,
                )
                recorder.onOnboardingComplete(
                    sequenceId = SYNC_SEQUENCE_ID,
                    sequencePosition = SEQUENCE_POSITION,
                    dismissedMethod = DismissedMethod.SKIPPED,
                )
            }
        }
    }
}
