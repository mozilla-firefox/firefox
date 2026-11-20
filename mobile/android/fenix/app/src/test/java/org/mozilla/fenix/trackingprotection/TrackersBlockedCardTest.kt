/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.trackingprotection

import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.theme.Theme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class TrackersBlockedCardTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `tapping the pill opens the privacy report`() {
        var privacyReportTaps = 0
        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                TrackersBlockedCard(
                    trackersBlockedCount = 3,
                    onPrivacyReportTapped = { privacyReportTaps++ },
                )
            }
        }

        composeTestRule.onNodeWithTag(PROTECTION_STATUS_PILL_TEST_TAG).performClick()

        assertEquals(1, privacyReportTaps)
    }
}
