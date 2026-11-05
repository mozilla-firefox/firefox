/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.strip

import kotlin.test.assertFalse
import kotlin.test.assertTrue
import mozilla.components.concept.engine.utils.ABOUT_HOME_URL
import org.junit.Test

class SelectedTabNavigationTest {

    @Test
    fun `GIVEN on the homepage WHEN a non-homepage tab is selected THEN it navigates`() {
        assertTrue(shouldNavigate(url = "https://example.com", currentDestinationIsHomepage = true))
    }

    @Test
    fun `GIVEN on the homepage WHEN a homepage tab is selected THEN it does not navigate`() {
        assertFalse(shouldNavigate(url = ABOUT_HOME_URL, currentDestinationIsHomepage = true))
    }

    @Test
    fun `GIVEN on the browser WHEN a homepage tab is selected THEN it navigates`() {
        assertTrue(shouldNavigate(url = ABOUT_HOME_URL, currentDestinationIsHomepage = false))
    }

    @Test
    fun `GIVEN on the browser WHEN a non-homepage tab is selected THEN it does not navigate`() {
        assertFalse(shouldNavigate(url = "https://example.com", currentDestinationIsHomepage = false))
    }
}
