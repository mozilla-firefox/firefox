/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class RouteTest {

    @Test
    fun `GIVEN the certificate details panel WHEN going back THEN the protection panel is shown`() {
        assertEquals(Route.ProtectionPanel, Route.CertificateDetailsPanel.previousRoute())
    }

    @Test
    fun `GIVEN the trackers panel WHEN going back THEN the protection panel is shown`() {
        assertEquals(Route.ProtectionPanel, Route.TrackersPanel.previousRoute())
    }

    @Test
    fun `GIVEN a trackers sub-page WHEN going back THEN the trackers panel is shown`() {
        assertEquals(Route.TrackersPanel, Route.TrackerCategoryDetailsPanel.previousRoute())
        assertEquals(Route.TrackersPanel, Route.TrackersProtectionDashboard.previousRoute())
    }

    @Test
    fun `GIVEN the protection panel or the clear site data dialog WHEN going back THEN the trust panel is dismissed`() {
        assertNull(Route.ProtectionPanel.previousRoute())
        assertNull(Route.ClearSiteDataDialog.previousRoute())
    }

    @Test
    fun `GIVEN routes that rebuild their content WHEN restored THEN they are kept`() {
        assertEquals(Route.ProtectionPanel, Route.ProtectionPanel.restorableRoute())
        assertEquals(Route.TrackersPanel, Route.TrackersPanel.restorableRoute())
        assertEquals(Route.TrackersProtectionDashboard, Route.TrackersProtectionDashboard.restorableRoute())
        assertEquals(Route.CertificateDetailsPanel, Route.CertificateDetailsPanel.restorableRoute())
    }

    @Test
    fun `GIVEN routes that depend on state set when navigating to them WHEN restored THEN the route they were opened from is shown`() {
        assertEquals(Route.TrackersPanel, Route.TrackerCategoryDetailsPanel.restorableRoute())
        assertEquals(Route.ProtectionPanel, Route.ClearSiteDataDialog.restorableRoute())
    }
}
