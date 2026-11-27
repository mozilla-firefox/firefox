/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.continuous

import io.mockk.mockk
import io.mockk.verify
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.experiments.nimbus.NimbusEventStore
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ContinuousOnboardingStartTest {
    private lateinit var settings: Settings
    private val eventStore: NimbusEventStore = mockk(relaxed = true)

    @Before
    fun setUp() {
        settings = Settings(testContext)
    }

    @Test
    fun `GIVEN an existing user WHEN nothing has started continuous onboarding THEN it is not driven by messaging`() {
        assertFalse(settings.continuousOnboardingViaMessaging)
    }

    @Test
    fun `GIVEN messaging is enabled WHEN continuous onboarding starts THEN it is driven by messaging and the event is recorded`() {
        settings.startContinuousOnboarding(eventStore, messagingSystemEnabled = true)

        assertTrue(settings.continuousOnboardingViaMessaging)
        verify { eventStore.recordEvent(count = 1, eventId = ONBOARDING_COMPLETED_EVENT_ID) }
    }

    @Test
    fun `GIVEN messaging is disabled WHEN continuous onboarding starts THEN it is driven by the existing feature`() {
        settings.startContinuousOnboarding(eventStore, messagingSystemEnabled = false)

        assertFalse(settings.continuousOnboardingViaMessaging)
        verify { eventStore.recordEvent(count = 1, eventId = ONBOARDING_COMPLETED_EVENT_ID) }
    }
}
