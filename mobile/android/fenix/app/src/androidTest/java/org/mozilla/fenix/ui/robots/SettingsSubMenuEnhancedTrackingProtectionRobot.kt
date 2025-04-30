/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.recyclerview.widget.RecyclerView
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.Espresso.pressBack
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.contrib.RecyclerViewActions
import androidx.test.espresso.matcher.ViewMatchers.Visibility
import androidx.test.espresso.matcher.ViewMatchers.hasDescendant
import androidx.test.espresso.matcher.ViewMatchers.hasSibling
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withChild
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withParentIndex
import androidx.test.espresso.matcher.ViewMatchers.withResourceName
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.Until
import org.hamcrest.CoreMatchers.allOf
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.scrollToElementByText
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.isChecked
import org.mozilla.fenix.helpers.isEnabled

const val globalPrivacyControlSwitchText = "Tell websites not to share & sell data"

/**
 * Implementation of Robot Pattern for the settings Enhanced Tracking Protection sub menu.
 */
class SettingsSubMenuEnhancedTrackingProtectionRobot {

    fun verifyEnhancedTrackingProtectionSummary() {
        Log.i(TAG, "verifyEnhancedTrackingProtectionSummary: Trying to verify that the ETP summary is visible")
        onView(withText("$appName protects you from many of the most common trackers that follow what you do online."))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyEnhancedTrackingProtectionSummary: Verified that the ETP summary is visible")
    }

    fun verifyLearnMoreText() {
        Log.i(TAG, "verifyLearnMoreText: Trying to verify that the learn more link is visible")
        onView(withText("Learn more")).check(matches(isDisplayed()))
        Log.i(TAG, "verifyLearnMoreText: Verified that the learn more link is visible")
    }

    fun verifyEnhancedTrackingProtectionTextWithSwitchWidget() {
        Log.i(TAG, "verifyEnhancedTrackingProtectionTextWithSwitchWidget: Trying to verify that the ETP toggle is visible")
        onView(
            allOf(
                withParentIndex(1),
                withChild(withText("Enhanced Tracking Protection")),
            ),
        )
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyEnhancedTrackingProtectionTextWithSwitchWidget: Verified that the ETP toggle is visible")
    }

    fun verifyEnhancedTrackingProtectionOptionsEnabled(enabled: Boolean = true) {
        Log.i(TAG, "verifyEnhancedTrackingProtectionOptionsEnabled: Trying to verify that the \"Standard\" ETP option is enabled $enabled")
        onView(withText("Standard (default)"))
            .check(matches(isEnabled(enabled)))
        Log.i(TAG, "verifyEnhancedTrackingProtectionOptionsEnabled: Verified that the \"Standard\" ETP option is enabled $enabled")
        Log.i(TAG, "verifyEnhancedTrackingProtectionOptionsEnabled: Trying to verify that the \"Strict\" ETP option is enabled $enabled")
        onView(withText("Strict"))
            .check(matches(isEnabled(enabled)))
        Log.i(TAG, "verifyEnhancedTrackingProtectionOptionsEnabled: Verified that the \"Strict\" ETP option is enabled $enabled")
        Log.i(TAG, "verifyEnhancedTrackingProtectionOptionsEnabled: Trying to verify that the \"Custom\" ETP option is enabled $enabled")
        onView(withText("Custom"))
            .check(matches(isEnabled(enabled)))
        Log.i(TAG, "verifyEnhancedTrackingProtectionOptionsEnabled: Verified that the \"Custom\" ETP option is enabled $enabled")
    }

    fun verifyTrackingProtectionSwitchEnabled() {
        Log.i(TAG, "verifyTrackingProtectionSwitchEnabled: Trying to verify that the ETP toggle is checked")
        onView(withResourceName("checkbox")).check(
            matches(
                isChecked(
                    true,
                ),
            ),
        )
        Log.i(TAG, "verifyTrackingProtectionSwitchEnabled: Verified that the ETP toggle is checked")
    }

    fun switchEnhancedTrackingProtectionToggle() {
        Log.i(TAG, "switchEnhancedTrackingProtectionToggle: Trying to click the ETP toggle")
        onView(
            allOf(
                withText("Enhanced Tracking Protection"),
                hasSibling(withResourceName("checkbox")),
            ),
        ).click()
        Log.i(TAG, "switchEnhancedTrackingProtectionToggle: Clicked the ETP toggle")
    }

    fun scrollToGCPSettings() {
        Log.i(TAG, "scrollToGCPSettings: Trying to perform scroll to the $globalPrivacyControlSwitchText option")
        onView(withId(R.id.recycler_view)).perform(
            RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                hasDescendant(withText(globalPrivacyControlSwitchText)),
            ),
        )
        Log.i(TAG, "scrollToGCPSettings: Performed scroll to the $globalPrivacyControlSwitchText option")
    }
    fun verifyGPCTextWithSwitchWidget() {
        Log.i(TAG, "verifyGPCTextWithSwitchWidget: Trying to verify that the $globalPrivacyControlSwitchText option is visible")
        onView(
            allOf(
                withChild(withText(globalPrivacyControlSwitchText)),
            ),
        ).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyGPCTextWithSwitchWidget: Verified that the $globalPrivacyControlSwitchText option is visible")
    }

    fun verifyGPCSwitchEnabled(enabled: Boolean) {
        Log.i(TAG, "verifyGPCSwitchEnabled: Trying to verify that the $globalPrivacyControlSwitchText option is checked: $enabled")
        onView(
            allOf(
                withChild(withText(globalPrivacyControlSwitchText)),
            ),
        ).check(matches(isChecked(enabled)))
        Log.i(TAG, "verifyGPCSwitchEnabled: Verified that the $globalPrivacyControlSwitchText option is checked: $enabled")
    }

    fun switchGPCToggle() {
        Log.i(TAG, "switchGPCToggle: Trying to click the $globalPrivacyControlSwitchText option toggle")
        onView(
            allOf(
                withChild(withText(globalPrivacyControlSwitchText)),
            ),
        ).click()
        Log.i(TAG, "switchGPCToggle: Clicked the $globalPrivacyControlSwitchText option toggle")
    }

    fun verifyStandardOptionDescription() {
        Log.i(TAG, "verifyStandardOptionDescription: Trying to verify that the \"Standard\" ETP option summary is displayed")
        onView(withText(R.string.preference_enhanced_tracking_protection_standard_description_5))
            .check(matches(isDisplayed()))
        Log.i(TAG, "verifyStandardOptionDescription: Verified that the \"Standard\" ETP option summary is displayed")
        Log.i(TAG, "verifyStandardOptionDescription: Trying to verify that the \"Standard\" ETP option info button is displayed")
        onView(withContentDescription(R.string.preference_enhanced_tracking_protection_standard_info_button))
            .check(matches(isDisplayed()))
        Log.i(TAG, "verifyStandardOptionDescription: Verify that the \"Standard\" ETP option info button is displayed")
    }

    fun verifyStrictOptionDescription() {
        Log.i(TAG, "verifyStrictOptionDescription: Trying to verify that the \"Strict\" ETP option summary is displayed")
        onView(withText(R.string.preference_enhanced_tracking_protection_strict_description_4))
            .check(matches(isDisplayed()))
        Log.i(TAG, "verifyStrictOptionDescription: Verified that the \"Strict\" ETP option summary is displayed")
        Log.i(TAG, "verifyStrictOptionDescription: Trying to verify that the \"Strict\" ETP option info button is displayed")
        onView(withContentDescription(R.string.preference_enhanced_tracking_protection_strict_info_button))
            .check(matches(isDisplayed()))
        Log.i(TAG, "verifyStrictOptionDescription: Verified that the \"Strict\" ETP option info button is displayed")
    }

    fun verifyCustomTrackingProtectionSettings() {
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Custom\" ETP option summary is displayed")
        onView(withText(R.string.preference_enhanced_tracking_protection_custom_description_2))
            .check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Custom\" ETP option summary is displayed")
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Custom\" ETP option info button is displayed")
        onView(withContentDescription(R.string.preference_enhanced_tracking_protection_custom_info_button))
            .check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Custom\" ETP option info button is displayed")
        scrollToElementByText("Redirect Trackers")
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Cookies\" check box is displayed")
        cookiesCheckbox().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Cookies\" check box is displayed")
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Cookies\" drop down is displayed")
        cookiesDropDownMenuDefault().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Cookies\" drop down is displayed")
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Tracking content\" check box is displayed")
        trackingContentCheckbox().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Tracking content\" check box is displayed")
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Tracking content\" drop down is displayed")
        trackingcontentDropDownDefault().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Tracking content\" drop down is displayed")
        scrollToElementByText(getStringResource(R.string.preferences_tracking_protection_exceptions))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Cryptominers\" check box is displayed")
        cryptominersCheckbox().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Cryptominers\" check box is displayed")
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Known Fingerprinters\" check box is displayed")
        knownFingerprintersCheckbox().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Known Fingerprinters\" check box is displayed")
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Suspected Fingerprinters\" check box is displayed")
        suspectedFingerprintersCheckbox().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Suspected Fingerprinters\" check box is displayed")
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Suspected Fingerprinters\" drop down is displayed")
        suspectedFingerprintersDropDownDefault().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Suspected Fingerprinters\" drop down is displayed")
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Trying to verify that the \"Redirect trackers\" check box is displayed")
        redirectTrackersCheckbox().check(matches(isDisplayed()))
        Log.i(TAG, "verifyCustomTrackingProtectionSettings: Verified that the \"Redirect trackers\" check box is displayed")
    }

    fun verifyWhatsBlockedByStandardETPInfo() {
        Log.i(TAG, "verifyWhatsBlockedByStandardETPInfo: Trying to click the \"Standard\" ETP option info button")
        onView(withContentDescription(R.string.preference_enhanced_tracking_protection_standard_info_button)).click()
        Log.i(TAG, "verifyWhatsBlockedByStandardETPInfo: Clicked the \"Standard\" ETP option info button")
        blockedByStandardETPInfo()
    }

    fun verifyWhatsBlockedByStrictETPInfo() {
        Log.i(TAG, "verifyWhatsBlockedByStrictETPInfo: Trying to click the \"Strict\" ETP option info button")
        onView(withContentDescription(R.string.preference_enhanced_tracking_protection_strict_info_button)).click()
        Log.i(TAG, "verifyWhatsBlockedByStrictETPInfo: Clicked the \"Strict\" ETP option info button")
        // Repeating the info as in the standard option, with one extra point.
        blockedByStandardETPInfo()
        Log.i(TAG, "verifyWhatsBlockedByStrictETPInfo: Trying to verify that the \"Tracking Content\" title is displayed")
        onView(withText("Tracking Content")).check(matches(isDisplayed()))
        Log.i(TAG, "verifyWhatsBlockedByStrictETPInfo: Verified that the \"Tracking Content\" title is displayed")
        Log.i(TAG, "verifyWhatsBlockedByStrictETPInfo: Trying to verify that the \"Tracking Content\" summary is displayed")
        onView(withText("Stops outside ads, videos, and other content from loading that contains tracking code. May affect some website functionality.")).check(matches(isDisplayed()))
        Log.i(TAG, "verifyWhatsBlockedByStrictETPInfo: Verified that the \"Tracking Content\" summary is displayed")
    }

    fun verifyWhatsBlockedByCustomETPInfo() {
        Log.i(TAG, "verifyWhatsBlockedByCustomETPInfo: Trying to click the \"Custom\" ETP option info button")
        onView(withContentDescription(R.string.preference_enhanced_tracking_protection_custom_info_button)).click()
        Log.i(TAG, "verifyWhatsBlockedByCustomETPInfo: Clicked the \"Custom\" ETP option info button")
        // Repeating the info as in the standard option, with one extra point.
        blockedByStandardETPInfo()
        Log.i(TAG, "verifyWhatsBlockedByCustomETPInfo: Trying to verify that the \"Tracking Content\" title is displayed")
        onView(withText("Tracking Content")).check(matches(isDisplayed()))
        Log.i(TAG, "verifyWhatsBlockedByCustomETPInfo: Verified that the \"Tracking Content\" title is displayed")
        Log.i(TAG, "verifyWhatsBlockedByCustomETPInfo: Trying to verify that the \"Tracking Content\" summary is displayed")
        onView(withText("Stops outside ads, videos, and other content from loading that contains tracking code. May affect some website functionality.")).check(matches(isDisplayed()))
        Log.i(TAG, "verifyWhatsBlockedByCustomETPInfo: Verified that the \"Tracking Content\" summary is displayed")
    }

    fun selectTrackingProtectionOption(option: String) {
        Log.i(TAG, "selectTrackingProtectionOption: Trying to click the $option ETP option")
        onView(withText(option)).click()
        Log.i(TAG, "selectTrackingProtectionOption: Clicked the $option ETP option")
    }

    fun verifyEnhancedTrackingProtectionLevelSelected(option: String, checked: Boolean) {
        Log.i(TAG, "verifyEnhancedTrackingProtectionLevelSelected: Waiting for $waitingTime ms until finding the \"Enhanced Tracking Protection\" toolbar")
        mDevice.wait(
            Until.findObject(By.text("Enhanced Tracking Protection")),
            waitingTime,
        )
        Log.i(TAG, "verifyEnhancedTrackingProtectionLevelSelected: Waited for $waitingTime ms until the \"Enhanced Tracking Protection\" toolbar was found")
        Log.i(TAG, "verifyEnhancedTrackingProtectionLevelSelected: Trying to verify that the $option ETP option is checked: $checked")
        onView(withText(option))
            .check(
                matches(
                    hasSibling(
                        allOf(
                            withId(R.id.radio_button),
                            isChecked(checked),
                        ),
                    ),
                ),
            )
        Log.i(TAG, "verifyEnhancedTrackingProtectionLevelSelected: Verified that the $option ETP option is checked: $checked")
    }

    class Transition {
        fun goBackToHomeScreen(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            // To settings
            Log.i(TAG, "goBackToHomeScreen: Trying to click the navigate up toolbar button")
            goBackButton().click()
            Log.i(TAG, "goBackToHomeScreen: Clicked the navigate up toolbar button")
            // To HomeScreen
            Log.i(TAG, "goBackToHomeScreen: Trying to perform press back action")
            pressBack()
            Log.i(TAG, "goBackToHomeScreen: Performed press back action")

            HomeScreenRobot().interact()
            return HomeScreenRobot.Transition()
        }

        fun goBack(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
            Log.i(TAG, "goBack: Trying to click the navigate up toolbar button")
            goBackButton().click()
            Log.i(TAG, "goBack: Clicked the navigate up toolbar button")

            SettingsRobot().interact()
            return SettingsRobot.Transition()
        }

        fun openExceptions(
            interact: SettingsSubMenuEnhancedTrackingProtectionExceptionsRobot.() -> Unit,
        ): SettingsSubMenuEnhancedTrackingProtectionExceptionsRobot.Transition {
            Log.i(TAG, "openExceptions: Trying to perform scroll to the \"Exceptions\" option")
            onView(withId(R.id.recycler_view)).perform(
                RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                    hasDescendant(withText("Exceptions")),
                ),
            )
            Log.i(TAG, "openExceptions: Performed scroll to the \"Exceptions\" option")
            Log.i(TAG, "openExceptions: Trying to click the \"Exceptions\" option")
            openExceptions().click()
            Log.i(TAG, "openExceptions: Clicked the \"Exceptions\" option")

            SettingsSubMenuEnhancedTrackingProtectionExceptionsRobot().interact()
            return SettingsSubMenuEnhancedTrackingProtectionExceptionsRobot.Transition()
        }
    }
}

fun settingsSubMenuEnhancedTrackingProtection(interact: SettingsSubMenuEnhancedTrackingProtectionRobot.() -> Unit): SettingsSubMenuEnhancedTrackingProtectionRobot.Transition {
    SettingsSubMenuEnhancedTrackingProtectionRobot().interact()
    return SettingsSubMenuEnhancedTrackingProtectionRobot.Transition()
}

private fun goBackButton() =
    onView(allOf(withContentDescription("Navigate up")))

private fun openExceptions() =
    onView(allOf(withText("Exceptions")))

private fun cookiesCheckbox() = onView(withText("Cookies"))

private fun cookiesDropDownMenuDefault() = onView(withText(R.string.preference_enhanced_tracking_protection_custom_cookies_5))

private fun trackingContentCheckbox() = onView(withText(R.string.preference_enhanced_tracking_protection_custom_tracking_content))

private fun trackingcontentDropDownDefault() = onView(withText(R.string.preference_enhanced_tracking_protection_custom_tracking_content_1))

private fun cryptominersCheckbox() = onView(withText(R.string.etp_cryptominers_title))

private fun knownFingerprintersCheckbox() = onView(withText(R.string.etp_known_fingerprinters_title))

private fun suspectedFingerprintersCheckbox() = onView(withText(R.string.etp_suspected_fingerprinters_title))

private fun suspectedFingerprintersDropDownDefault() = onView(withText(R.string.preference_enhanced_tracking_protection_custom_tracking_content_2))

private fun redirectTrackersCheckbox() = onView(withText(R.string.etp_redirect_trackers_title))

private fun blockedByStandardETPInfo() {
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Social Media Trackers\" title is displayed")
    onView(withText("Social Media Trackers")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Social Media Trackers\" title is displayed")
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Social Media Trackers\" summary is displayed")
    onView(withText("Limits the ability of social networks to track your browsing activity around the web.")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Social Media Trackers\" summary is displayed")
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Cross-Site Cookies\" title is displayed")
    onView(withText("Cross-Site Cookies")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Cross-Site Cookies\" title is displayed")
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Cross-Site Cookies\" summary is displayed")
    onView(withText("Total Cookie Protection isolates cookies to the site you’re on so trackers like ad networks can’t use them to follow you across sites.")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Cross-Site Cookies\" summary is displayed")
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Cryptominers\" title is displayed")
    onView(withText("Cryptominers")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Cryptominers\" title is displayed")
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Cryptominers\" summary is displayed")
    onView(withText("Prevents malicious scripts gaining access to your device to mine digital currency.")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Cryptominers\" summary is displayed")
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Known Fingerprinters\" title is displayed")
    onView(withText("Known Fingerprinters")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Known Fingerprinters\" title is displayed")
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Fingerprinters\" summary is displayed")
    onView(withText("Stops uniquely identifiable data from being collected about your device that can be used for tracking purposes.")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Fingerprinters\" summary is displayed")
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Redirect Trackers\" title is displayed")
    onView(withText("Redirect Trackers")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Redirect Trackers\" title is displayed")
    Log.i(TAG, "blockedByStandardETPInfo: Trying to verify that the \"Redirect Trackers\" summary is displayed")
    onView(withText("Clears cookies set by redirects to known tracking websites.")).check(matches(isDisplayed()))
    Log.i(TAG, "blockedByStandardETPInfo: Verified that the \"Redirect Trackers\" summary is displayed")
}
