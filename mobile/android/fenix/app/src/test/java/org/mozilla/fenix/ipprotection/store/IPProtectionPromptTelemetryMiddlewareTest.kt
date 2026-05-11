/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import io.mockk.mockk
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class IPProtectionPromptTelemetryMiddlewareTest {

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    @Test
    fun `WHEN the OnImpression action THEN the expected telemetry is recorded`() {
        assertNull(Vpn.onboardingShown.testGetValue())

        invokeMiddlewareWith(IPProtectionPromptAction.OnImpression(Surface.HOMEPAGE))

        assertNotNull(Vpn.onboardingShown.testGetValue())
        assertEventExtraData(Vpn.onboardingShown.testGetValue()!!.last().extra!!, Surface.HOMEPAGE)
    }

    @Test
    fun `WHEN the OnGetStartedClicked action THEN the expected telemetry is recorded`() {
        assertNull(Vpn.getStartedTapped.testGetValue())

        invokeMiddlewareWith(IPProtectionPromptAction.OnGetStartedClicked(Surface.BROWSER))

        assertNotNull(Vpn.getStartedTapped.testGetValue())
        assertEventExtraData(Vpn.getStartedTapped.testGetValue()!!.last().extra!!, Surface.BROWSER)
    }

    @Test
    fun `WHEN the OnNotNowClicked action THEN the expected telemetry is recorded`() {
        assertNull(Vpn.onboardingNotNowTapped.testGetValue())

        invokeMiddlewareWith(IPProtectionPromptAction.OnNotNowClicked(Surface.HOMEPAGE))

        assertNotNull(Vpn.onboardingNotNowTapped.testGetValue())
        assertEventExtraData(Vpn.onboardingNotNowTapped.testGetValue()!!.last().extra!!, Surface.HOMEPAGE)
    }

    @Test
    fun `WHEN the OnPromptManuallyDismissed action THEN the expected telemetry is recorded`() {
        assertNull(Vpn.onboardingDismissed.testGetValue())

        invokeMiddlewareWith(IPProtectionPromptAction.OnPromptManuallyDismissed(Surface.BROWSER))

        assertNotNull(Vpn.onboardingDismissed.testGetValue())
        assertEventExtraData(Vpn.onboardingDismissed.testGetValue()!!.last().extra!!, Surface.BROWSER)
    }

    @Test
    fun `WHEN the OnBrowseWithExtraProtectionClicked action THEN the expected telemetry is recorded`() {
        assertNull(Vpn.onboardingBrowseWithProtectionTapped.testGetValue())

        invokeMiddlewareWith(IPProtectionPromptAction.OnBrowseWithExtraProtectionClicked(Surface.HOMEPAGE))

        assertNotNull(Vpn.onboardingBrowseWithProtectionTapped.testGetValue())
        assertEventExtraData(
            Vpn.onboardingBrowseWithProtectionTapped.testGetValue()!!.last().extra!!,
            Surface.HOMEPAGE,
        )
    }

    @Test
    fun `WHEN the OnPromptCreated action THEN no telemetry is recorded`() {
        invokeMiddlewareWith(IPProtectionPromptAction.OnPromptCreated)

        assertNull(Vpn.onboardingShown.testGetValue())
        assertNull(Vpn.getStartedTapped.testGetValue())
        assertNull(Vpn.onboardingNotNowTapped.testGetValue())
        assertNull(Vpn.onboardingDismissed.testGetValue())
        assertNull(Vpn.onboardingBrowseWithProtectionTapped.testGetValue())
    }

    @Test
    fun `WHEN the OnPromptDismissed action THEN no telemetry is recorded`() {
        invokeMiddlewareWith(IPProtectionPromptAction.OnPromptDismissed)

        assertNull(Vpn.onboardingShown.testGetValue())
        assertNull(Vpn.getStartedTapped.testGetValue())
        assertNull(Vpn.onboardingNotNowTapped.testGetValue())
        assertNull(Vpn.onboardingDismissed.testGetValue())
        assertNull(Vpn.onboardingBrowseWithProtectionTapped.testGetValue())
    }

    private fun invokeMiddlewareWith(action: IPProtectionPromptAction) {
        IPProtectionPromptTelemetryMiddleware()(
            store = mockk(),
            next = {},
            action = action,
        )
    }

    private fun assertEventExtraData(eventExtraData: Map<String, String>, surface: Surface) {
        assertEquals(surface.metricLabel, eventExtraData["entrypoint"]!!)
    }
}
