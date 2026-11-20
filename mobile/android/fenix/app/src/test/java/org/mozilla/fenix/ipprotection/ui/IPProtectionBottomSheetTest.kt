/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.ui

import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import mozilla.components.compose.base.theme.Theme
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptState
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptState.PromptDetails
import org.mozilla.fenix.theme.FirefoxTheme

@RunWith(AndroidJUnit4::class)
class IPProtectionBottomSheetTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `WHEN non-FxA details are provided THEN the body shows both data limits`() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val expectedBody =
            context.getString(
                R.string.ip_protection_onboarding_body_no_fxa,
                5,
                50,
                context.getString(R.string.ip_protection_onboarding_body_link_promo),
            )

        composeTestRule.setContent {
            FirefoxTheme(theme = Theme.Light) {
                IPProtectionBottomSheet(
                    state =
                        IPProtectionPromptState(
                            PromptDetails.GpiPrompt(
                                gpiMaxGigabytes = 5,
                                fxaMaxGigabytes = 50,
                            )
                        ),
                    onDismiss = {},
                    onDismissRequest = {},
                    onLearnMoreClicked = {},
                    onGetStartedClicked = {},
                    onNotNowClicked = {},
                )
            }
        }

        composeTestRule.onNodeWithContentDescription(expectedBody, substring = true).assertExists()
    }
}
