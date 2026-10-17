/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import junit.framework.TestCase.assertEquals
import junit.framework.TestCase.assertNotNull
import junit.framework.TestCase.assertTrue
import mozilla.components.support.test.robolectric.testContext
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Onboarding
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class OnboardingTelemetryRecorderTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var telemetryRecorder: OnboardingTelemetryRecorder

    @Before
    fun setup() {
        telemetryRecorder =
            OnboardingTelemetryRecorder(
                onboardingReason = OnboardingReason.NEW_USER,
                installSource = "source",
            )
    }

    @Test
    fun `WHEN terms of service accept button is clicked THEN the onboarding shown event is triggered`() {
        telemetryRecorder.onTermsOfServiceManagerAcceptTermsButtonClick()

        val event = Onboarding.shown.testGetValue()!!
        val installSource = event.single().extra?.getValue("install_source").toString()
        val onboardingReason = event.single().extra?.getValue("onboarding_reason").toString()

        assertNotNull(event)
        assertEquals("source", installSource)
        assertEquals(OnboardingReason.NEW_USER.value, onboardingReason)
    }

    @Test
    fun `WHEN onboarding is complete THEN the onboarding dismissed event is triggered`() {
        telemetryRecorder.onOnboardingComplete("", "")

        val event = Onboarding.dismissed.testGetValue()!!
        val installSource = event.single().extra?.getValue("install_source").toString()
        val onboardingReason = event.single().extra?.getValue("onboarding_reason").toString()

        assertNotNull(event)
        assertEquals("source", installSource)
        assertEquals(OnboardingReason.NEW_USER.value, onboardingReason)
    }

    @Test
    fun `WHEN the notification card impression is recorded THEN the turn on notifications card event is triggered`() {
        telemetryRecorder.onNotificationCardImpression(sequenceId = "notification", sequencePosition = "0")

        val event = Onboarding.turnOnNotificationsCard.testGetValue()!!.single()

        assertEquals("impression", event.extra?.getValue("action"))
        assertEquals("onboarding_card", event.extra?.getValue("element_type"))
        assertEquals("notification", event.extra?.getValue("sequence_id"))
        assertEquals("0", event.extra?.getValue("sequence_position"))
        assertEquals("source", event.extra?.getValue("install_source"))
        assertEquals(OnboardingReason.NEW_USER.value, event.extra?.getValue("onboarding_reason"))
    }

    @Test
    fun `WHEN onboarding is complete THEN the onboarding ping is sent`() {
        var sent = false
        val job =
            Pings.onboarding.testBeforeNextSubmit {
                sent = true
            }

        telemetryRecorder.onOnboardingComplete("", "")

        job.join()
        assertTrue(sent)
    }

    @Test
    fun `WHEN navigating to the next onboarding page THEN the onboarding ping is sent`() {
        var sent = false
        val job =
            Pings.onboarding.testBeforeNextSubmit {
                sent = true
            }

        telemetryRecorder.onNavigatedToNextPage()

        job.join()
        assertTrue(sent)
    }
}
