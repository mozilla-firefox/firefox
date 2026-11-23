/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.core.text.HtmlCompat
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.shortAppName
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated

class UnifiedTrustPanelRobot {

    fun verifyUnifiedTrustPanelItems(
        composeTestRule: ComposeTestRule,
        webSite: String,
        shouldWebSiteURLBeDisplayed: Boolean = true,
        webSiteURL: String,
        isTheWebSiteSecure: Boolean,
        isEnhancedTrackingProtectionEnabled: Boolean,
        isTrackerBlockingEnabled: Boolean,
        areTrackersBlocked: Boolean,
    ) {
        waitForAppWindowToBeUpdated()
        Log.i(TAG, "verifyUnifiedTrustPanelItems: Trying to verify that the web site title: $webSite is displayed")
        composeTestRule.onNodeWithTag("unified.trust.panel.website").assert(hasText(webSite))
        Log.i(TAG, "verifyUnifiedTrustPanelItems: Verified that the web site title: $webSite is displayed")
        if (shouldWebSiteURLBeDisplayed) {
            Log.i(TAG, "verifyUnifiedTrustPanelItems: Trying to verify that the web site url: $webSiteURL is displayed")
            composeTestRule.onNodeWithTag("unified.trust.panel.website.url").assert(hasText(webSiteURL))
            Log.i(TAG, "verifyUnifiedTrustPanelItems: Verified that the web site url: $webSiteURL is displayed")
        }
        verifyTheEnhancedTrackingProtectionState(
            composeTestRule,
            isEnhancedTrackingProtectionEnabled,
            isTheWebSiteSecure,
        )
        verifyTheTrackersBlockedOptionState(
            composeTestRule,
            isTheWebSiteSecure,
            isTrackerBlockingEnabled,
            areTrackersBlocked,
        )
        verifyTheSiteSecurityOption(composeTestRule, isTheWebSiteSecure)
        Log.i(
            TAG,
            "verifyUnifiedTrustPanelItems: Trying to verify that the \"Clear cookies and site data\" option is displayed",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.clear_site_data), useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyUnifiedTrustPanelItems: Verified that the \"Clear cookies and site data\" option is displayed",
        )
        Log.i(
            TAG,
            "verifyUnifiedTrustPanelItems: Trying to verify that the \"Privacy settings\" hyperlink is displayed",
        )
        composeTestRule
            .onNodeWithContentDescription("Privacy Settings Links available", useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(TAG, "verifyUnifiedTrustPanelItems: Verified that the \"Privacy settings\" hyperlink is displayed")
    }

    fun verifyTheEnhancedTrackingProtectionState(
        composeTestRule: ComposeTestRule,
        isEnhancedTrackingProtectionEnabled: Boolean,
        isTheWebSiteSecure: Boolean,
    ) {
        if (isEnhancedTrackingProtectionEnabled) {
            Log.i(
                TAG,
                "verifyTheEnhancedTrackingProtectionState: Verified that \"Enhanced tracking protection\" is enabled",
            )
            if (isTheWebSiteSecure) {
                Log.i(TAG, "verifyTheEnhancedTrackingProtectionState: Verified that the website is secure")
                Log.i(
                    TAG,
                    "verifyTheEnhancedTrackingProtectionState: Trying to verify that the \"$shortAppName is on guard\" banner title is displayed",
                )
                composeTestRule
                    .onNodeWithContentDescription(
                        getStringResource(R.string.protection_panel_banner_protected_title, argument = shortAppName),
                        substring = true,
                    )
                    .assertIsDisplayed()
                Log.i(
                    TAG,
                    "verifyTheEnhancedTrackingProtectionState: Verified that the \"$shortAppName is on guard\" banner title is displayed",
                )
            } else {
                Log.i(TAG, "verifyTheEnhancedTrackingProtectionState: Verified that the website is not secure")
                Log.i(
                    TAG,
                    "verifyTheEnhancedTrackingProtectionState: Trying to verify that the \"Be careful on this site\" banner title and message are displayed",
                )
                composeTestRule
                    .onNodeWithContentDescription(
                        getStringResource(R.string.protection_panel_banner_not_secure_title, argument = appName),
                        substring = true,
                    )
                    .assertIsDisplayed()
                composeTestRule
                    .onNodeWithContentDescription(
                        getStringResource(R.string.protection_panel_banner_not_secure_description),
                        substring = true,
                    )
                    .assertIsDisplayed()
                Log.i(
                    TAG,
                    "verifyTheEnhancedTrackingProtectionState: Verified that the \"Be careful on this site\" banner title and message are displayed",
                )
            }
            Log.i(
                TAG,
                "verifyTheEnhancedTrackingProtectionState: Trying to verify that the \"Enhanced tracking protection\" option, message and \"On\" state are displayed",
            )
            composeTestRule
                .onNodeWithText(getStringResource(R.string.protection_panel_etp_toggle_label), useUnmergedTree = true)
                .assertIsDisplayed()
            composeTestRule
                .onNodeWithText(
                    getStringResource(R.string.protection_panel_etp_toggle_enabled_description_2),
                    useUnmergedTree = true,
                )
                .assertIsDisplayed()
            composeTestRule
                .onNodeWithText(getStringResource(R.string.protection_panel_etp_toggle_on), useUnmergedTree = true)
                .assertIsDisplayed()
            Log.i(
                TAG,
                "verifyTheEnhancedTrackingProtectionState: Verified that the \"Enhanced tracking protection\" option, message and \"On\" state are displayed",
            )
        } else {
            Log.i(
                TAG,
                "verifyTheEnhancedTrackingProtectionState: Verified that \"Enhanced tracking protection\" is not enabled",
            )
            if (isTheWebSiteSecure) {
                Log.i(TAG, "verifyTheEnhancedTrackingProtectionState: Verified that the website is secure")
                Log.i(
                    TAG,
                    "verifyTheEnhancedTrackingProtectionState: Trying to verify that the \"You turned off protection\" banner title and message are displayed",
                )
                composeTestRule
                    .onNodeWithContentDescription(
                        getStringResource(R.string.protection_panel_banner_not_protected_title),
                        substring = true,
                    )
                    .assertIsDisplayed()
                composeTestRule
                    .onNodeWithContentDescription(
                        getStringResource(
                            R.string.protection_panel_banner_not_protected_description,
                            argument = shortAppName,
                        ),
                        substring = true,
                    )
                    .assertIsDisplayed()
                Log.i(
                    TAG,
                    "verifyTheEnhancedTrackingProtectionState: Verified that the \"You turned off protection\" banner title and message are displayed",
                )
            } else {
                Log.i(TAG, "verifyTheEnhancedTrackingProtectionState: Verified that the website is not secure")
                Log.i(
                    TAG,
                    "verifyTheEnhancedTrackingProtectionState: Trying to verify that the \"Be careful on this site\" banner title and message are displayed",
                )
                composeTestRule
                    .onNodeWithContentDescription(
                        getStringResource(R.string.protection_panel_banner_not_secure_title, argument = appName),
                        substring = true,
                    )
                    .assertIsDisplayed()
                composeTestRule
                    .onNodeWithContentDescription(
                        getStringResource(R.string.protection_panel_banner_not_secure_description),
                        substring = true,
                    )
                    .assertIsDisplayed()
                Log.i(
                    TAG,
                    "verifyTheEnhancedTrackingProtectionState: Verified that the \"Be careful on this site\" banner title and message are displayed",
                )
            }
            Log.i(
                TAG,
                "verifyTheEnhancedTrackingProtectionState: Trying to verify that the \"Enhanced tracking protection\" option, message and \"Off\" state are displayed",
            )
            composeTestRule
                .onNodeWithText(getStringResource(R.string.protection_panel_etp_toggle_label), useUnmergedTree = true)
                .assertIsDisplayed()
            composeTestRule
                .onNodeWithText(
                    getStringResource(R.string.protection_panel_etp_toggle_disabled_description_2),
                    useUnmergedTree = true,
                )
                .assertIsDisplayed()
            composeTestRule
                .onNodeWithText(getStringResource(R.string.protection_panel_etp_toggle_off), useUnmergedTree = true)
                .assertIsDisplayed()
            Log.i(
                TAG,
                "verifyTheEnhancedTrackingProtectionState: Verified that the \"Enhanced tracking protection\" option, message and \"Off\" state are displayed",
            )
        }
    }

    fun verifyTheTrackersBlockedOptionState(
        composeTestRule: ComposeTestRule,
        isTheWebSiteSecure: Boolean,
        isTrackerBlockingEnabled: Boolean,
        areTrackersBlocked: Boolean,
    ) {
        if (!isTrackerBlockingEnabled) {
            Log.i(TAG, "verifyTheTrackersBlockedOptionState: Verified that the tracker blocking is not enabled")
            Log.i(
                TAG,
                "verifyTheTrackersBlockedOptionState: Trying to verify that the \"Trackers aren't blocked\" option is displayed",
            )
            composeTestRule
                .onNodeWithText(
                    getStringResource(R.string.protection_panel_etp_disabled_no_trackers_blocked),
                    useUnmergedTree = true,
                )
                .assertIsDisplayed()
            Log.i(
                TAG,
                "verifyTheTrackersBlockedOptionState: Verified that the \"Trackers aren't blocked\" option is displayed",
            )
        } else if (isTheWebSiteSecure) {
            Log.i(TAG, "verifyTheTrackersBlockedOptionState: Verified that the tracker blocking is enabled")
            if (areTrackersBlocked) {
                Log.i(TAG, "verifyTheTrackersBlockedOptionState: Verified that trackers are blocked")
                Log.i(
                    TAG,
                    "verifyTheTrackersBlockedOptionState: Trying to verify that the blocked trackers banner description is displayed",
                )
                composeTestRule
                    .onNodeWithContentDescription("blocked on this site", substring = true)
                    .assertIsDisplayed()
                Log.i(
                    TAG,
                    "verifyTheTrackersBlockedOptionState: Verified that the blocked trackers banner description is displayed",
                )
            } else {
                Log.i(
                    TAG,
                    "verifyTheTrackersBlockedOptionState: Trying to verify that the \"You're protected\" banner description is displayed",
                )
                composeTestRule
                    .onNodeWithContentDescription(
                        getStringResource(R.string.protection_panel_banner_protected_no_blocked_trackers_description),
                        substring = true,
                    )
                    .assertIsDisplayed()
                Log.i(
                    TAG,
                    "verifyTheTrackersBlockedOptionState: Verified that the \"You're protected\" banner description is displayed",
                )
            }
        } else {
            Log.i(
                TAG,
                "verifyTheTrackersBlockedOptionState: The website is not secure, the tracker count is not shown in the not-secure banner",
            )
        }
    }

    fun verifyTheSiteSecurityOption(composeTestRule: ComposeTestRule, isTheWebSiteSecure: Boolean) {
        if (isTheWebSiteSecure) {
            Log.i(TAG, "verifyTheSiteSecurityOption: Verified that the web site is secure")
            Log.i(
                TAG,
                "verifyTheSiteSecurityOption: Trying to verify that the \"Secure connection\" option and \"Verified by\" description are displayed",
            )
            composeTestRule
                .onNodeWithText(getStringResource(R.string.connection_security_panel_secure), useUnmergedTree = true)
                .assertIsDisplayed()
            composeTestRule.onNodeWithText("Verified by", useUnmergedTree = true, substring = true).assertIsDisplayed()
            Log.i(
                TAG,
                "verifyTheSiteSecurityOption: Verified that the \"Secure connection\" option and \"Verified by\" description are displayed",
            )
        } else {
            Log.i(TAG, "verifyTheSiteSecurityOption: Verified that the web site is not secure")
            Log.i(
                TAG,
                "verifyTheSiteSecurityOption: Trying to verify that the \"Connection not secure\" option is displayed",
            )
            composeTestRule
                .onNodeWithText(
                    getStringResource(R.string.connection_security_panel_not_secure),
                    useUnmergedTree = true,
                )
                .assertIsDisplayed()
            Log.i(TAG, "verifyTheSiteSecurityOption: Verified that the \"Connection not secure\" option is displayed")
        }
    }

    fun clickTheEnhancedTrackingProtectionOption(composeTestRule: ComposeTestRule) {
        Log.i(TAG, "clickTheEnhancedTrackingProtectionOption: Trying to click the ETP option")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.protection_panel_etp_toggle_label), useUnmergedTree = true)
            .performClick()
        Log.i(TAG, "clickTheEnhancedTrackingProtectionOption: Clicked the ETP option")
        mDevice.waitForIdle()
        composeTestRule.waitForIdle()
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickTheClearCookiesAndSiteDataButton(composeTestRule: ComposeTestRule) {
        Log.i(
            TAG,
            "clickSearchButton: Waiting for $waitingTime for the \"Clear cookies and site data\" button to exist",
        )
        composeTestRule.waitUntilAtLeastOneExists(
            hasContentDescription(getStringResource(R.string.clear_site_data)),
            waitingTime,
        )
        Log.i(TAG, "clickSearchButton: Waited for $waitingTime for the \"Clear cookies and site data\" button to exist")
        Log.i(TAG, "clickTheClearCookiesAndSiteDataButton: Trying to click the \"Clear cookies and site data\" button")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.clear_site_data), useUnmergedTree = true)
            .performClick()
        Log.i(TAG, "clickTheClearCookiesAndSiteDataButton: Clicked the \"Clear cookies and site data\" button")
    }

    fun verifyTheClearCookiesAndSiteDataDialog(composeTestRule: ComposeTestRule, webSite: String) {
        // convert HTML-formatted string resource to plain text
        val clearCookiesAndSiteDataDialogRawDescription =
            getStringResource(R.string.clear_site_data_dialog_description, argument = webSite)
        val clearCookiesAndSiteDataDialogConvertedDescription =
            HtmlCompat.fromHtml(clearCookiesAndSiteDataDialogRawDescription, HtmlCompat.FROM_HTML_MODE_LEGACY)
                .toString()

        Log.i(
            TAG,
            "verifyTheClearCookiesAndSiteDataDialog: Trying to verify that the \"Clear cookies and site data\" dialog title is displayed",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.clear_site_data), useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheClearCookiesAndSiteDataDialog: Verified that the \"Clear cookies and site data\" dialog title is displayed",
        )
        Log.i(
            TAG,
            "verifyTheClearCookiesAndSiteDataDialog: Trying to verify that the \"Clear cookies and site data\" dialog message is displayed",
        )
        composeTestRule
            .onNodeWithText(clearCookiesAndSiteDataDialogConvertedDescription, useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyTheClearCookiesAndSiteDataDialog: Verified that the \"Clear cookies and site data\" dialog message is displayed",
        )
        Log.i(
            TAG,
            "verifyTheClearCookiesAndSiteDataDialog: Trying to verify that the \"Clear\" dialog button is displayed",
        )
        composeTestRule
            .onNodeWithText(
                getStringResource(R.string.clear_site_data_dialog_positive_button_text),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyTheClearCookiesAndSiteDataDialog: Verified that the \"Clear\" dialog button is displayed")
        Log.i(
            TAG,
            "verifyTheClearCookiesAndSiteDataDialog: Trying to verify that the \"Cancel\" dialog button is displayed",
        )
        composeTestRule
            .onNodeWithText(
                getStringResource(R.string.clear_site_data_dialog_negative_button_text),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyTheClearCookiesAndSiteDataDialog: Verified that the \"Cancel\" dialog button is displayed")
    }

    class Transition
}
