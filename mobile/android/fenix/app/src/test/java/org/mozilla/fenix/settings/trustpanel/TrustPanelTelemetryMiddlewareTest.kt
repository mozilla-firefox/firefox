/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import kotlin.test.assertNotNull
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.TrackingProtection
import org.mozilla.fenix.GleanMetrics.TrustPanel
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.settings.trustpanel.middleware.TRUST_PANEL_TELEMETRY_SOURCE
import org.mozilla.fenix.settings.trustpanel.middleware.TrustPanelTelemetryMiddleware
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelState
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelStore
import org.mozilla.fenix.trackingprotection.TrackingProtectionCategory
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TrustPanelTelemetryMiddlewareTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Test
    fun `GIVEN tracking protection is enabled WHEN toggle tracking protection action is dispatched THEN record tracking protection exception added telemetry`() {
        val store = createStore(trustPanelState = TrustPanelState(isTrackingProtectionEnabled = true))
        assertNull(TrackingProtection.exceptionAdded.testGetValue())

        store.dispatch(TrustPanelAction.ToggleTrackingProtection)

        assertNotNull(TrackingProtection.exceptionAdded.testGetValue())
    }

    @Test
    fun `GIVEN tracking protection is disabled WHEN toggle tracking protection action is dispatched THEN do not record tracking protection exception added telemetry`() {
        val store = createStore(trustPanelState = TrustPanelState(isTrackingProtectionEnabled = false))
        assertNull(TrackingProtection.exceptionAdded.testGetValue())

        store.dispatch(TrustPanelAction.ToggleTrackingProtection)

        assertNull(TrackingProtection.exceptionAdded.testGetValue())
    }

    @Test
    fun `WHEN security certificate action is dispatched THEN record security certificate telemetry`() {
        val store = createStore(trustPanelState = TrustPanelState(isTrackingProtectionEnabled = false))
        assertNull(TrustPanel.securityCertificate.testGetValue())

        store.dispatch(TrustPanelAction.Navigate.SecurityCertificate)

        assertNotNull(TrustPanel.securityCertificate.testGetValue())
    }

    @Test
    fun `WHEN trackers protection dashboard action is dispatched THEN record privacy report shown telemetry with the trust panel source`() {
        val store = createStore(trustPanelState = TrustPanelState(isTrackingProtectionEnabled = false))
        assertNull(TrackingProtection.privacyReportTapped.testGetValue())

        store.dispatch(TrustPanelAction.Navigate.TrackersProtectionDashboard)

        val events = TrackingProtection.privacyReportTapped.testGetValue()
        assertNotNull(events)
        assertEquals(1, events.size)
        assertEquals(
            TRUST_PANEL_TELEMETRY_SOURCE,
            events.single().extra?.get("source"),
        )
    }

    @Test
    fun `WHEN qwac navigate action is dispatched THEN record qwac telemetry`() {
        val store = createStore()
        assertNull(TrustPanel.qwac.testGetValue())

        store.dispatch(TrustPanelAction.Navigate.QWAC)

        assertNotNull(TrustPanel.qwac.testGetValue())
    }

    @Test
    fun `WHEN privacy security settings navigate action is dispatched THEN record panel settings telemetry`() {
        val store = createStore()
        assertNull(TrackingProtection.panelSettings.testGetValue())

        store.dispatch(TrustPanelAction.Navigate.PrivacySecuritySettings)

        assertNotNull(TrackingProtection.panelSettings.testGetValue())
    }

    @Test
    fun `WHEN update detailed tracker category action is dispatched THEN record etp tracker list telemetry`() {
        val store = createStore()
        assertNull(TrackingProtection.etpTrackerList.testGetValue())

        store.dispatch(TrustPanelAction.UpdateDetailedTrackerCategory(TrackingProtectionCategory.SOCIAL_MEDIA_TRACKERS))

        assertNotNull(TrackingProtection.etpTrackerList.testGetValue())
    }

    private fun createStore(trustPanelState: TrustPanelState = TrustPanelState()) =
        TrustPanelStore(
            initialState = trustPanelState,
            middleware = listOf(TrustPanelTelemetryMiddleware()),
        )
}
