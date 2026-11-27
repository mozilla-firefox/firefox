/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import io.mockk.confirmVerified
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.service.nimbus.messaging.Message
import org.junit.Test
import org.mozilla.fenix.onboarding.DismissedMethod
import org.mozilla.fenix.onboarding.OnboardingTelemetryRecorder
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData

class ContinuousOnboardingMessageTelemetryTest {
    private val recorder: OnboardingTelemetryRecorder = mockk(relaxed = true)
    private val telemetry = ContinuousOnboardingMessageTelemetry(recorder)

    private val notifications = message("co-notifications-2")
    private val sync = message("co-sync")
    private val syncId = OnboardingPageUiData.Type.SYNC_SIGN_IN.telemetryId

    @Test
    fun `WHEN the default browser role is granted THEN the set to default click is recorded`() {
        telemetry.onDefaultBrowserRoleGranted()

        verify { recorder.onSetToDefaultClick(OnboardingPageUiData.Type.DEFAULT_BROWSER.telemetryId, "0") }
        confirmVerified(recorder)
    }

    @Test
    fun `WHEN the notification card is interacted with THEN the notification events are recorded`() {
        telemetry.onImpression(notifications)
        telemetry.onPrimaryClick(notifications)
        telemetry.onSecondaryClick(notifications)
        telemetry.onCloseClick(notifications)

        verify {
            recorder.onNotificationCardImpression("notification", "0")
            recorder.onNotificationPermissionClick("notification", "0")
            recorder.onSkipTurnOnNotificationsClick("notification", "0", "secondary_button")
            recorder.onSkipTurnOnNotificationsClick("notification", "0", "card_close_button")
        }
        confirmVerified(recorder)
    }

    @Test
    fun `WHEN the sync card is shown and its primary button is clicked THEN the sign in events are recorded`() {
        telemetry.onImpression(sync)
        telemetry.onPrimaryClick(sync)

        verify {
            recorder.onImpression(syncId, OnboardingPageUiData.Type.SYNC_SIGN_IN, "0")
            recorder.onSyncSignInClick(syncId, "0")
            recorder.onOnboardingComplete(syncId, "0", DismissedMethod.COMPLETED)
        }
        confirmVerified(recorder)
    }

    @Test
    fun `WHEN the sync card is skipped THEN the skip and the skipped completion are recorded`() {
        telemetry.onSecondaryClick(sync)
        telemetry.onCloseClick(sync)

        verify {
            recorder.onSkipSignInClick(syncId, "0", "secondary_button")
            recorder.onSkipSignInClick(syncId, "0", "card_close_button")
        }
        verify(exactly = 2) { recorder.onOnboardingComplete(syncId, "0", DismissedMethod.SKIPPED) }
        confirmVerified(recorder)
    }

    @Test
    fun `WHEN a stage is skipped by a stage-completed message THEN its completion is recorded`() {
        telemetry.onStageCompleted(message("co-day-5-skipped"))
        telemetry.onStageCompleted(message("co-day-7-ineligible"))
        telemetry.onStageCompleted(message("co-day-7-satisfied"))

        verify {
            recorder.onOnboardingComplete(syncId, "0", DismissedMethod.COMPLETED)
            recorder.onOnboardingComplete("ip_protection", "0", DismissedMethod.SKIPPED)
            recorder.onOnboardingComplete("ip_protection", "0", DismissedMethod.COMPLETED)
        }
        confirmVerified(recorder)
    }

    @Test
    fun `WHEN days 2 and 3 are completed by a stage-completed message THEN nothing is recorded`() {
        telemetry.onStageCompleted(message("co-day-2-complete"))
        telemetry.onStageCompleted(message("co-day-3-complete"))

        confirmVerified(recorder)
    }

    private fun message(id: String): Message = mockk { every { this@mockk.id } returns id }
}
