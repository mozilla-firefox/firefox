/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.content.Intent
import android.util.Log
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.core.net.toUri
import androidx.recyclerview.widget.RecyclerView
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.contrib.RecyclerViewActions
import androidx.test.espresso.intent.Intents.intended
import androidx.test.espresso.intent.matcher.IntentMatchers.hasAction
import androidx.test.espresso.intent.matcher.IntentMatchers.hasData
import androidx.test.espresso.matcher.ViewMatchers.Visibility
import androidx.test.espresso.matcher.ViewMatchers.hasDescendant
import androidx.test.espresso.matcher.ViewMatchers.hasSibling
import androidx.test.espresso.matcher.ViewMatchers.isChecked
import androidx.test.espresso.matcher.ViewMatchers.isCompletelyDisplayed
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.isNotChecked
import androidx.test.espresso.matcher.ViewMatchers.withClassName
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.By.textContains
import androidx.test.uiautomator.UiObject
import androidx.test.uiautomator.UiScrollable
import androidx.test.uiautomator.UiSelector
import androidx.test.uiautomator.Until
import junit.framework.AssertionFailedError
import org.hamcrest.CoreMatchers
import org.hamcrest.CoreMatchers.endsWith
import org.hamcrest.Matchers.allOf
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.AppAndSystemHelper.forceCloseApp
import org.mozilla.fenix.helpers.AppAndSystemHelper.isPackageInstalled
import org.mozilla.fenix.helpers.Constants.LISTS_MAXSWIPES
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_PLAY_SERVICES
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeShort
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.hasCousin
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.scrollToElementByText
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.ext.waitNotNull
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.settings.account.AccountSettingsTestTag

/** Implementation of Robot Pattern for the settings menu. */
class SettingsRobot {

    // BASICS SECTION
    fun verifyGeneralHeading() {
        scrollToElementByText("General")
        Log.i(TAG, "verifyGeneralHeading: Trying to verify that the \"General\" heading is visible")
        onView(withText("General")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyGeneralHeading: Verified that the \"General\" heading is visible")
    }

    fun verifySearchButton() {
        Log.i(TAG, "verifySearchButton: Waiting for $waitingTime ms until finding the \"Search\" button")
        mDevice.wait(Until.findObject(By.text("Search")), waitingTime)
        Log.i(TAG, "verifySearchButton: Waited for $waitingTime ms until the \"Search\" button was found")
        Log.i(TAG, "verifySearchButton: Trying to verify that the \"Search\" button is visible")
        onView(withText(R.string.preferences_search)).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySearchButton: Verified that the \"Search\" button is visible")
    }

    fun verifyCustomizeButton() {
        Log.i(TAG, "verifyCustomizeButton: Trying to verify that the \"Customize\" button is visible")
        onView(withText("Customize")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyCustomizeButton: Verified that the \"Customize\" button is visible")
    }

    fun verifyAccessibilityButton() {
        Log.i(TAG, "verifyAccessibilityButton: Trying to verify that the \"Accessibility\" button is visible")
        onView(withText("Accessibility")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAccessibilityButton: Verified that the \"Accessibility\" button is visible")
    }

    fun verifySetAsDefaultBrowserButton() {
        scrollToElementByText("Set as default browser")
        Log.i(
            TAG,
            "verifySetAsDefaultBrowserButton: Trying to verify that the \"Set as default browser\" button is visible",
        )
        onView(withText("Set as default browser")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySetAsDefaultBrowserButton: Verified that the \"Set as default browser\" button is visible")
    }

    fun verifyTabsButton() = assertUIObjectExists(itemContainingText(getStringResource(R.string.preferences_tabs)))

    fun verifyHomepageButton() {
        Log.i(TAG, "verifyHomepageButton: Trying to verify that the \"Homepage\" button is visible")
        onView(withText(R.string.preferences_home_2)).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHomepageButton: Verified that the \"Homepage\" button is visible")
    }

    fun verifyAutofillButton() {
        Log.i(TAG, "verifyAutofillButton: Trying to verify that the \"Autofill\" button is visible")
        onView(withText(R.string.preferences_autofill)).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAutofillButton: Verified that the \"Autofill\" button is visible")
    }

    fun verifyLanguageButton(localizedText: String = getStringResource(R.string.preferences_language)) {
        scrollToElementByText(localizedText)
        Log.i(TAG, "verifyLanguageButton: Trying to verify that the \"Language\" button is visible")
        onView(withText(localizedText)).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyLanguageButton: Verified that the \"Language\" button is visible")
    }

    fun verifyDefaultBrowserToggle(isEnabled: Boolean) {
        scrollToElementByText(getStringResource(R.string.preferences_set_as_default_browser))
        Log.i(
            TAG,
            "verifyDefaultBrowserToggle: Trying to verify that the \"Set as default browser\" toggle is enabled: $isEnabled",
        )
        onView(withText(R.string.preferences_set_as_default_browser))
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withId(R.id.switch_widget),
                            if (isEnabled) {
                                isChecked()
                            } else {
                                isNotChecked()
                            },
                        )
                    )
                )
            )
        Log.i(
            TAG,
            "verifyDefaultBrowserToggle: Verified that the \"Set as default browser\" toggle is enabled: $isEnabled",
        )
    }

    fun clickDefaultBrowserSwitch() = toggleDefaultBrowserSwitch()

    fun verifyAndroidDefaultAppsMenuAppears() {
        Log.i(TAG, "verifyAndroidDefaultAppsMenuAppears: Trying to verify that default browser apps dialog appears")
        intended(hasAction(DEFAULT_APPS_SETTINGS_ACTION))
        Log.i(TAG, "verifyAndroidDefaultAppsMenuAppears: Verified that the default browser apps dialog appears")
    }

    // PRIVACY SECTION
    fun verifyPrivacyHeading() {
        scrollToElementByText("Privacy and security")
        Log.i(TAG, "verifyPrivacyHeading: Trying to verify that the \"Privacy and security\" heading is visible")
        onView(withText("Privacy and security")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyPrivacyHeading: Verified that the \"Privacy and security\" heading is visible")
    }

    fun verifyHTTPSOnlyModeButton() {
        scrollToElementByText(getStringResource(R.string.preferences_https_only_title))
        Log.i(TAG, "verifyHTTPSOnlyModeButton: Trying to verify that the \"HTTPS-Only Mode\" button is visible")
        onView(withText(R.string.preferences_https_only_title))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyHTTPSOnlyModeButton: Verified that the \"HTTPS-Only Mode\" button is visible")
    }

    fun verifyEnhancedTrackingProtectionButton() {
        Log.i(
            TAG,
            "verifyEnhancedTrackingProtectionButton: Waiting for $waitingTime ms until finding the \"Privacy and Security\" heading",
        )
        mDevice.wait(Until.findObject(By.text("Privacy and Security")), waitingTime)
        Log.i(
            TAG,
            "verifyEnhancedTrackingProtectionButton: Waited for $waitingTime ms until the \"Privacy and Security\" heading was found",
        )
        Log.i(
            TAG,
            "verifyEnhancedTrackingProtectionButton: Trying to verify that the \"Enhanced Tracking Protection\" button is visible",
        )
        onView(withId(R.id.recycler_view))
            .perform(
                RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                    hasDescendant(withText("Enhanced Tracking Protection"))
                )
            )
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(
            TAG,
            "verifyEnhancedTrackingProtectionButton: Verified that the \"Enhanced Tracking Protection\" button is visible",
        )
    }

    fun verifyLoginsAndPasswordsButton() {
        scrollToElementByText("Passwords")
        Log.i(
            TAG,
            "verifyLoginsAndPasswordsButton: Trying to verify that the \"Logins and passwords\" button is visible",
        )
        onView(withText(R.string.preferences_passwords_logins_and_passwords_2))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyLoginsAndPasswordsButton: Verified that the \"Logins and passwords\" button is visible")
    }

    fun verifyPrivateBrowsingButton() {
        scrollToElementByText("Private browsing")
        Log.i(
            TAG,
            "verifyPrivateBrowsingButton: Waiting for $waitingTime ms until finding the \"Private browsing\" button",
        )
        mDevice.wait(Until.findObject(By.text("Private browsing")), waitingTime)
        Log.i(
            TAG,
            "verifyPrivateBrowsingButton: Waited for $waitingTime ms until the \"Private browsing\" button was found",
        )
        Log.i(TAG, "verifyPrivateBrowsingButton: Trying to verify that the \"Private browsing\" button is visible")
        onView(withText("Private browsing")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyPrivateBrowsingButton: Verified that the \"Private browsing\" button is visible")
    }

    fun verifySiteSettingsButton() {
        scrollToElementByText("Site settings")
        Log.i(TAG, "verifySiteSettingsButton: Trying to verify that the \"Site permissions\" button is visible")
        onView(withText("Site settings")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySiteSettingsButton: Verified that the \"Site permissions\" button is visible")
    }

    fun verifyDeleteBrowsingDataButton() {
        scrollToElementByText("Delete browsing data")
        Log.i(
            TAG,
            "verifyDeleteBrowsingDataButton: Trying to verify that the \"Delete browsing data\" button is visible",
        )
        onView(withText("Delete browsing data")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyDeleteBrowsingDataButton: Verified that the \"Delete browsing data\" button is visible")
    }

    fun verifyDeleteBrowsingDataOnQuitButton() {
        scrollToElementByText("Delete browsing data on quit")
        Log.i(
            TAG,
            "verifyDeleteBrowsingDataOnQuitButton: Trying to verify that the \"Delete browsing data on quit\" button is visible",
        )
        onView(withText("Delete browsing data on quit")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(
            TAG,
            "verifyDeleteBrowsingDataOnQuitButton: Verified that the \"Delete browsing data on quit\" button is visible",
        )
    }

    fun verifyNotificationsButton() {
        scrollToElementByText("Notifications")
        Log.i(TAG, "verifyNotificationsButton: Trying to verify that the \"Notifications\" button is visible")
        onView(withText("Notifications")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyNotificationsButton: Verified that the \"Notifications\" button is visible")
    }

    fun verifyDataCollectionButton() {
        scrollToElementByText("Data collection")
        Log.i(TAG, "verifyDataCollectionButton: Trying to verify that the \"Data collection\" button is visible")
        onView(withText("Data collection")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyDataCollectionButton: Verified that the \"Data collection\" button is visible")
    }

    fun verifyOpenLinksInAppsButton() {
        Log.i(TAG, "verifyOpenLinksInAppsButton: Trying to perform scroll to the \"Open links in apps\" button")
        onView(withId(R.id.recycler_view))
            .perform(
                RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                    hasDescendant(withText(R.string.preferences_open_links_in_apps))
                )
            )
        Log.i(TAG, "verifyOpenLinksInAppsButton: Performed scroll to the \"Open links in apps\" button")
        Log.i(TAG, "verifyOpenLinksInAppsButton: Trying to verify that the \"Open links in apps\" button is visible")
        openLinksInAppsButton().check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyOpenLinksInAppsButton: Verified that the \"Open links in apps\" button is visible")
    }

    fun verifySettingsView() {
        scrollToElementByText("General")
        Log.i(TAG, "verifySettingsView: Trying to verify that the \"General\" heading is visible")
        onView(withText("General")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySettingsView: Verified that the \"General\" heading is visible")
        scrollToElementByText("Privacy and security")
        Log.i(TAG, "verifySettingsView: Trying to verify that the \"Privacy and security\" heading is visible")
        onView(withText("Privacy and security")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySettingsView: Verified that the \"Privacy and security\" heading is visible")

        val extensions = getStringResource(R.string.preferences_extensions)
        Log.i(TAG, "verifySettingsView: Trying to perform scroll to the \"$extensions\" button")
        onView(withId(R.id.recycler_view))
            .perform(
                RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                    hasDescendant(withText(R.string.preferences_extensions))
                )
            )
        Log.i(TAG, "verifySettingsView: Performed scroll to the \"$extensions\" button")
        Log.i(TAG, "verifySettingsView: Trying to verify that the \"$extensions\" button is completely displayed")
        onView(withText(R.string.preferences_extensions)).check(matches(isCompletelyDisplayed()))
        Log.i(TAG, "verifySettingsView: Verified that the \"$extensions\" button is completely displayed")

        Log.i(
            TAG,
            "verifySettingsView: Trying to perform ${LISTS_MAXSWIPES}x a scroll action to the end of the settings list",
        )
        settingsList().scrollToEnd(LISTS_MAXSWIPES)
        Log.i(TAG, "verifySettingsView: Performed ${LISTS_MAXSWIPES}x a scroll action to the end of the settings list")
        Log.i(TAG, "verifySettingsView: Trying to verify that the \"About\" heading is visible")
        onView(withText("About")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySettingsView: Verified that the \"About\" heading is visible")
    }

    fun verifySettingsToolbar() {
        Log.i(TAG, "verifySettingsToolbar: Trying to verify that the navigate up button is visible")
        onView(
                allOf(
                    withId(R.id.navigationToolbar),
                    hasDescendant(withContentDescription(R.string.action_bar_up_description)),
                )
            )
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySettingsToolbar: Verified that the navigate up button is visible")
        Log.i(TAG, "verifySettingsToolbar: Trying to verify that the \"Settings\" toolbar title is visible")
        onView(
                allOf(
                    withId(R.id.navigationToolbar),
                    hasDescendant(withText(R.string.settings_title)),
                )
            )
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySettingsToolbar: Verified that the \"Settings\" toolbar title is visible")
    }

    // ADVANCED SECTION
    fun verifyAdvancedHeading() {
        val extensions = getStringResource(R.string.preferences_extensions)
        Log.i(TAG, "verifyAdvancedHeading: Trying to perform scroll to the \"$extensions\" button")
        onView(withId(R.id.recycler_view))
            .perform(
                RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                    hasDescendant(withText(R.string.preferences_extensions))
                )
            )
        Log.i(TAG, "verifyAdvancedHeading: Performed scroll to the \"$extensions\" button")
        Log.i(TAG, "verifyAdvancedHeading: Trying to verify that the \"$extensions\" button is completely displayed")
        onView(withText(R.string.preferences_extensions)).check(matches(isCompletelyDisplayed()))
        Log.i(TAG, "verifyAdvancedHeading: Verified that the \"$extensions\" button is completely displayed")
    }

    fun verifyAddons() {
        val extensions = getStringResource(R.string.preferences_extensions)
        Log.i(TAG, "verifyAddons: Trying to perform scroll to the \"$extensions\" button")
        onView(withId(R.id.recycler_view))
            .perform(
                RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                    hasDescendant(withText(R.string.preferences_extensions))
                )
            )
        Log.i(TAG, "verifyAddons: Performed scroll to the \"$extensions\" button")
        Log.i(TAG, "verifyAddons: Trying to verify that the \"$extensions\" button is completely displayed")
        addonsManagerButton().check(matches(isCompletelyDisplayed()))
        Log.i(TAG, "verifyAddons: Verified that the \"$extensions\" button is completely displayed")
    }

    fun verifyDownloadsButton() {
        scrollToElementByText(getStringResource(R.string.preferences_downloads_2))
        Log.i(TAG, "verifyExternalDownloadsButton: Trying to verify that the \"Downloads\" button is visible")
        onView(withText(getStringResource(R.string.preferences_downloads_2)))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyExternalDownloadsButton: Verified that the \"Downloads\" button is visible")
    }

    fun verifyLeakCanaryToggle(enabled: Boolean) {
        onView(withId(R.id.recycler_view))
            .perform(
                RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                    hasDescendant(withText(R.string.preference_leakcanary))
                )
            )
        Log.i(TAG, "verifyLeakCanaryToggle: Trying to verify that the \"LeakCanary\" toggle is enabled: $enabled")
        onView(withText(R.string.preference_leakcanary))
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withClassName(endsWith("Switch")),
                            if (enabled) {
                                isChecked()
                            } else {
                                isNotChecked()
                            },
                        )
                    )
                )
            )
        Log.i(TAG, "verifyLeakCanaryToggle: Verified that the \"LeakCanary\" toggle is enabled: $enabled")
    }

    fun verifyRemoteDebuggingToggle(enabled: Boolean) {
        onView(withId(R.id.recycler_view))
            .perform(
                RecyclerViewActions.scrollTo<RecyclerView.ViewHolder>(
                    hasDescendant(withText(R.string.preferences_remote_debugging))
                )
            )
        Log.i(
            TAG,
            "verifyRemoteDebuggingToggle: Trying to verify that the \"Remote debugging via USB\" toggle is enabled: $enabled",
        )
        onView(withText(R.string.preferences_remote_debugging))
            .check(
                matches(
                    hasCousin(
                        allOf(
                            withClassName(endsWith("Switch")),
                            if (enabled) {
                                isChecked()
                            } else {
                                isNotChecked()
                            },
                        )
                    )
                )
            )
        Log.i(
            TAG,
            "verifyRemoteDebuggingToggle: Verified that the \"Remote debugging via USB\" toggle is enabled: $enabled",
        )
    }

    // DEVELOPER TOOLS SECTION
    fun verifyRemoteDebuggingButton() {
        scrollToElementByText("Remote debugging via USB")
        Log.i(
            TAG,
            "verifyRemoteDebuggingButton: Trying to verify that the \"Remote debugging via USB\" button is visible",
        )
        onView(withText("Remote debugging via USB")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyRemoteDebuggingButton: Verified that the \"Remote debugging via USB\" button is visible")
    }

    fun verifyLeakCanaryButton() {
        scrollToElementByText("LeakCanary")
        Log.i(TAG, "verifyLeakCanaryButton: Trying to verify that the \"LeakCanary\" button is visible")
        onView(withText("LeakCanary")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyLeakCanaryButton: Verified that the \"LeakCanary\" button is visible")
    }

    // ABOUT SECTION
    fun verifyAboutHeading() {
        Log.i(
            TAG,
            "verifyAboutHeading: Trying to perform ${LISTS_MAXSWIPES}x a scroll action to the end of the settings list",
        )
        settingsList().scrollToEnd(LISTS_MAXSWIPES)
        Log.i(TAG, "verifyAboutHeading: Performed ${LISTS_MAXSWIPES}x a scroll action to the end of the settings list")
        Log.i(TAG, "verifyAboutHeading: Trying to verify that the \"About\" heading is visible")
        onView(withText("About")).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAboutHeading: Verified that the \"About\" heading is visible")
    }

    fun verifyRateOnGooglePlay() {
        settingsList().scrollToEnd(LISTS_MAXSWIPES)
        assertUIObjectExists(rateOnGooglePlayHeading())
    }

    fun verifyAboutFirefoxPreview() {
        settingsList().scrollToEnd(LISTS_MAXSWIPES)
        assertUIObjectExists(aboutFirefoxHeading())
    }

    fun verifyGooglePlayRedirect(composeTestRule: ComposeTestRule) {
        if (isPackageInstalled(GOOGLE_PLAY_SERVICES)) {
            Log.i(TAG, "verifyGooglePlayRedirect: $GOOGLE_PLAY_SERVICES is installed")
            try {
                Log.i(TAG, "verifyGooglePlayRedirect: Trying to verify intent to: $GOOGLE_PLAY_SERVICES")
                intended(
                    allOf(
                        hasAction(Intent.ACTION_VIEW),
                        hasData(SupportUtils.RATE_APP_URL.toUri()),
                    )
                )
                Log.i(TAG, "verifyGooglePlayRedirect: Verified intent to: $GOOGLE_PLAY_SERVICES")
            } catch (e: AssertionFailedError) {
                Log.i(TAG, "verifyGooglePlayRedirect: AssertionFailedError caught, executing fallback methods")
                BrowserRobot(composeTestRule).verifyRateOnGooglePlayURL()
            } finally {
                forceCloseApp(GOOGLE_PLAY_SERVICES)
            }
        } else {
            BrowserRobot(composeTestRule).verifyRateOnGooglePlayURL()
        }
    }

    fun verifySettingsOptionSummary(setting: String, summary: String) {
        scrollToElementByText(setting)
        Log.i(
            TAG,
            "verifySettingsOptionSummary: Trying to verify that setting: $setting with summary:$summary is visible",
        )
        onView(
                allOf(
                    withText(setting),
                    hasSibling(withText(summary)),
                )
            )
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifySettingsOptionSummary: Verified that setting: $setting with summary:$summary is visible")
    }

    fun verifyPageSummariesButton() {
        scrollToElementByText("Page summaries")
        Log.i(TAG, "verifyPageSummariesButton: Trying to verify that the \"Page summaries\" button is visible")
        onView(withText(R.string.preferences_page_summaries))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyPageSummariesButton: Verified that the \"Page summaries\" button is visible")
    }

    class Transition {
        fun goBack(composeTestRule: ComposeTestRule, interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "goBack: Trying to click the navigate up button")
            goBackButton().click()
            Log.i(TAG, "goBack: Clicked the navigate up button")
            composeTestRule.waitForIdle()
            mDevice.waitForIdle()

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun goBackToOnboardingScreen(
            composeTestRule: ComposeTestRule,
            interact: HomeScreenRobot.() -> Unit,
        ): HomeScreenRobot.Transition {
            Log.i(TAG, "goBackToOnboardingScreen: Trying to click device back button")
            mDevice.pressBack()
            Log.i(TAG, "goBackToOnboardingScreen: Clicked device back button")
            Log.i(TAG, "goBackToOnboardingScreen: Waiting for device to be idle for $waitingTimeShort ms")
            mDevice.waitForIdle(waitingTimeShort)
            Log.i(TAG, "goBackToOnboardingScreen: Device was idle for $waitingTimeShort ms")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun goBackToBrowser(
            composeTestRule: ComposeTestRule,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(TAG, "goBackToBrowser: Trying to click the navigate up button")
            goBackButton().click()
            Log.i(TAG, "goBackToBrowser: Clicked the navigate up button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun openAboutFirefoxPreview(
            interact: SettingsSubMenuAboutRobot.() -> Unit
        ): SettingsSubMenuAboutRobot.Transition {
            Log.i(TAG, "openAboutFirefoxPreview: Trying to click the \"About Firefox\" button")
            aboutFirefoxHeading().click()
            Log.i(TAG, "openAboutFirefoxPreview: Clicked the \"About Firefox\" button")
            SettingsSubMenuAboutRobot().interact()
            return SettingsSubMenuAboutRobot.Transition()
        }

        fun openSearchSubMenu(interact: SettingsSubMenuSearchRobot.() -> Unit): SettingsSubMenuSearchRobot.Transition {
            itemWithText(getStringResource(R.string.preferences_search)).also {
                Log.i(TAG, "openSearchSubMenu: Waiting for $waitingTimeShort ms for the \"Search\" button to exist")
                it.waitForExists(waitingTimeShort)
                Log.i(TAG, "openSearchSubMenu: Waited for $waitingTimeShort ms for the \"Search\" button to exist")
                Log.i(TAG, "openSearchSubMenu: Trying to click the \"Search\" button")
                it.click()
                Log.i(TAG, "openSearchSubMenu: Clicked the \"Search\" button")
            }

            SettingsSubMenuSearchRobot().interact()
            return SettingsSubMenuSearchRobot.Transition()
        }

        fun openCustomizeSubMenu(
            interact: SettingsSubMenuCustomizeRobot.() -> Unit
        ): SettingsSubMenuCustomizeRobot.Transition {
            assertUIObjectExists(itemContainingText(getStringResource(R.string.preferences_customize)))
            Log.i(TAG, "openCustomizeSubMenu: Trying to click the \"Customize\" button")
            itemContainingText(getStringResource(R.string.preferences_customize)).click()
            Log.i(TAG, "openCustomizeSubMenu: Clicked the \"Customize\" button")

            SettingsSubMenuCustomizeRobot().interact()
            return SettingsSubMenuCustomizeRobot.Transition()
        }

        fun openTabsSubMenu(interact: SettingsSubMenuTabsRobot.() -> Unit): SettingsSubMenuTabsRobot.Transition {
            Log.i(TAG, "openTabsSubMenu: Waiting for $waitingTime ms for the \"Tabs\" button to exist")
            val tabsButton =
                mDevice.wait(
                    Until.findObject(By.text(getStringResource(R.string.preferences_tabs))),
                    waitingTime,
                ) ?: throw AssertionError("Tabs settings button not found after $waitingTime ms")
            Log.i(
                TAG,
                "openTabsSubMenu: Trying to click the \"Tabs\" button and wait for $waitingTimeShort ms for a new window",
            )
            tabsButton.clickAndWait(Until.newWindow(), waitingTimeShort)
            Log.i(
                TAG,
                "openTabsSubMenu: Clicked the \"Tabs\" button and waited for $waitingTimeShort ms for a new window",
            )

            SettingsSubMenuTabsRobot().interact()
            return SettingsSubMenuTabsRobot.Transition()
        }

        fun openHomepageSubMenu(
            interact: SettingsSubMenuHomepageRobot.() -> Unit
        ): SettingsSubMenuHomepageRobot.Transition {
            Log.i(TAG, "openHomepageSubMenu: Waiting for $waitingTime ms for the \"Homepage\" button to exist")
            mDevice.findObject(UiSelector().textContains("Homepage")).waitForExists(waitingTime)
            Log.i(TAG, "openHomepageSubMenu: Waited for $waitingTime ms for the \"Homepage\" button to exist")
            Log.i(TAG, "openHomepageSubMenu: Trying to click the \"Homepage\" button")
            onView(withText(R.string.preferences_home_2)).click()
            Log.i(TAG, "openHomepageSubMenu: Clicked the \"Homepage\" button")

            SettingsSubMenuHomepageRobot().interact()
            return SettingsSubMenuHomepageRobot.Transition()
        }

        fun openAutofillSubMenu(
            composeTestRule: ComposeTestRule,
            interact: SettingsSubMenuAutofillRobot.() -> Unit,
        ): SettingsSubMenuAutofillRobot.Transition {
            Log.i(TAG, "openAutofillSubMenu: Waiting for $waitingTime ms for the \"Autofill\" button to exist")
            val autofillButton =
                mDevice.wait(
                    Until.findObject(By.textContains(getStringResource(R.string.preferences_autofill))),
                    waitingTime,
                ) ?: throw AssertionError("Autofill settings button not found after $waitingTime ms")
            Log.i(TAG, "openAutofillSubMenu: Trying to click the \"Autofill\" button")
            autofillButton.click()
            Log.i(TAG, "openAutofillSubMenu: Clicked the \"Autofill\" button")

            SettingsSubMenuAutofillRobot(composeTestRule).interact()
            return SettingsSubMenuAutofillRobot.Transition(composeTestRule)
        }

        fun openAccessibilitySubMenu(
            interact: SettingsSubMenuAccessibilityRobot.() -> Unit
        ): SettingsSubMenuAccessibilityRobot.Transition {
            scrollToElementByText("Accessibility")
            Log.i(TAG, "openAccessibilitySubMenu: Trying to verify that the \"Accessibility\" button is displayed")
            onView(withText("Accessibility")).check(matches(isDisplayed()))
            Log.i(TAG, "openAccessibilitySubMenu: Verified that the \"Accessibility\" button is displayed")
            Log.i(TAG, "openAccessibilitySubMenu: Trying to click the \"Accessibility\" button")
            onView(withText("Accessibility")).click()
            Log.i(TAG, "openAccessibilitySubMenu: Clicked the \"Accessibility\" button")

            SettingsSubMenuAccessibilityRobot().interact()
            return SettingsSubMenuAccessibilityRobot.Transition()
        }

        fun openLanguageSubMenu(
            localizedText: String = getStringResource(R.string.preferences_language),
            interact: SettingsSubMenuLanguageRobot.() -> Unit,
        ): SettingsSubMenuLanguageRobot.Transition {
            Log.i(TAG, "openLanguageSubMenu: Trying to click the $localizedText button")
            mDevice.waitForIdle()
            onView(withId(R.id.recycler_view))
                .perform(
                    RecyclerViewActions.actionOnItem<RecyclerView.ViewHolder>(
                        hasDescendant(withText(localizedText)),
                        ViewActions.click(),
                    )
                )
            Log.i(TAG, "openLanguageSubMenu: Clicked the $localizedText button")

            SettingsSubMenuLanguageRobot().interact()
            return SettingsSubMenuLanguageRobot.Transition()
        }

        fun openSetDefaultBrowserSubMenu(
            interact: SettingsSubMenuSetDefaultBrowserRobot.() -> Unit
        ): SettingsSubMenuSetDefaultBrowserRobot.Transition {
            scrollToElementByText("Set as default browser")
            Log.i(TAG, "openSetDefaultBrowserSubMenu: Trying to click the \"Set as default browser\" button")
            onView(withText("Set as default browser")).click()
            Log.i(TAG, "openSetDefaultBrowserSubMenu: Clicked the \"Set as default browser\" button")

            SettingsSubMenuSetDefaultBrowserRobot().interact()
            return SettingsSubMenuSetDefaultBrowserRobot.Transition()
        }

        fun openEnhancedTrackingProtectionSubMenu(
            interact: SettingsSubMenuEnhancedTrackingProtectionRobot.() -> Unit
        ): SettingsSubMenuEnhancedTrackingProtectionRobot.Transition {
            scrollToElementByText("Enhanced Tracking Protection")
            Log.i(
                TAG,
                "openEnhancedTrackingProtectionSubMenu: Trying to click the \"Enhanced Tracking Protection\" button",
            )
            onView(withText("Enhanced Tracking Protection")).click()
            Log.i(TAG, "openEnhancedTrackingProtectionSubMenu: Clicked the \"Enhanced Tracking Protection\" button")

            SettingsSubMenuEnhancedTrackingProtectionRobot().interact()
            return SettingsSubMenuEnhancedTrackingProtectionRobot.Transition()
        }

        fun openLoginsAndPasswordSubMenu(
            interact: SettingsSubMenuLoginsAndPasswordRobot.() -> Unit
        ): SettingsSubMenuLoginsAndPasswordRobot.Transition {
            scrollToElementByText("Passwords")
            Log.i(TAG, "openLoginsAndPasswordSubMenu: Trying to click the \"Logins and passwords\" button")
            onView(withText("Passwords")).click()
            Log.i(TAG, "openLoginsAndPasswordSubMenu: Clicked the \"Logins and passwords\" button")

            SettingsSubMenuLoginsAndPasswordRobot().interact()
            return SettingsSubMenuLoginsAndPasswordRobot.Transition()
        }

        fun openTurnOnSyncMenu(
            composeTestRule: ComposeTestRule,
            interact: SettingsSignInToSyncRobot.() -> Unit,
        ): SettingsSignInToSyncRobot.Transition {
            // The legacy "Sign in" branch can be removed once accountSettingsNewUi is enabled by default.
            if (appContext.components.settings.accountSettingsNewUi) {
                Log.i(TAG, "openTurnOnSyncMenu: Trying to click the \"Sign in\" promo card")
                composeTestRule.onNodeWithTag(AccountSettingsTestTag.SIGN_IN_PROMO_CARD).performClick()
                Log.i(TAG, "openTurnOnSyncMenu: Clicked the \"Sign in\" promo card")
            } else {
                Log.i(TAG, "openTurnOnSyncMenu: Trying to click the \"Sync and save your data\" button")
                onView(withText("Sign in")).click()
                Log.i(TAG, "openTurnOnSyncMenu: Clicked the \"Sync and save your data\" button")
            }

            SettingsSignInToSyncRobot().interact()
            return SettingsSignInToSyncRobot.Transition(composeTestRule)
        }

        fun openPrivateBrowsingSubMenu(
            interact: SettingsSubMenuPrivateBrowsingRobot.() -> Unit
        ): SettingsSubMenuPrivateBrowsingRobot.Transition {
            scrollToElementByText("Private browsing")
            Log.i(TAG, "openPrivateBrowsingSubMenu: Trying to click the \"Private browsing\" button")
            mDevice.findObject(textContains("Private browsing")).click()
            Log.i(TAG, "openPrivateBrowsingSubMenu: Clicked the \"Private browsing\" button")

            SettingsSubMenuPrivateBrowsingRobot().interact()
            return SettingsSubMenuPrivateBrowsingRobot.Transition()
        }

        fun openSettingsSubMenuSiteSettings(
            interact: SettingsSubMenuSitePermissionsRobot.() -> Unit
        ): SettingsSubMenuSitePermissionsRobot.Transition {
            scrollToElementByText("Site settings")
            Log.i(TAG, "openSettingsSubMenuSiteSettings: Trying to click the \"Site settings\" button")
            mDevice.findObject(textContains("Site settings")).click()
            Log.i(TAG, "openSettingsSubMenuSiteSettings: Clicked the \"Site settings\" button")

            SettingsSubMenuSitePermissionsRobot().interact()
            return SettingsSubMenuSitePermissionsRobot.Transition()
        }

        fun openSettingsSubMenuDeleteBrowsingData(
            interact: SettingsSubMenuDeleteBrowsingDataRobot.() -> Unit
        ): SettingsSubMenuDeleteBrowsingDataRobot.Transition {
            scrollToElementByText("Delete browsing data")
            Log.i(TAG, "openSettingsSubMenuDeleteBrowsingData: Trying to click the \"Delete browsing data\" button")
            mDevice.findObject(textContains("Delete browsing data")).click()
            Log.i(TAG, "openSettingsSubMenuDeleteBrowsingData: Clicked the \"Delete browsing data\" button")

            SettingsSubMenuDeleteBrowsingDataRobot().interact()
            return SettingsSubMenuDeleteBrowsingDataRobot.Transition()
        }

        fun openSettingsSubMenuDeleteBrowsingDataOnQuit(
            interact: SettingsSubMenuDeleteBrowsingDataOnQuitRobot.() -> Unit
        ): SettingsSubMenuDeleteBrowsingDataOnQuitRobot.Transition {
            scrollToElementByText("Delete browsing data on quit")
            Log.i(
                TAG,
                "openSettingsSubMenuDeleteBrowsingDataOnQuit: Trying to click the \"Delete browsing data on quit\" button",
            )
            mDevice.findObject(textContains("Delete browsing data on quit")).click()
            Log.i(
                TAG,
                "openSettingsSubMenuDeleteBrowsingDataOnQuit: Clicked the \"Delete browsing data on quit\" button",
            )

            SettingsSubMenuDeleteBrowsingDataOnQuitRobot().interact()
            return SettingsSubMenuDeleteBrowsingDataOnQuitRobot.Transition()
        }

        fun openSettingsSubMenuNotifications(interact: SystemSettingsRobot.() -> Unit): SystemSettingsRobot.Transition {
            scrollToElementByText("Notifications")
            Log.i(TAG, "openSettingsSubMenuNotifications: Trying to click the \"Notifications\" button")
            mDevice.findObject(textContains("Notifications")).click()
            Log.i(TAG, "openSettingsSubMenuNotifications: Clicked the \"Notifications\" button")

            SystemSettingsRobot().interact()
            return SystemSettingsRobot.Transition()
        }

        fun openSettingsSubMenuDataCollection(
            interact: SettingsSubMenuDataCollectionRobot.() -> Unit
        ): SettingsSubMenuDataCollectionRobot.Transition {
            scrollToElementByText("Data collection")
            Log.i(TAG, "openSettingsSubMenuDataCollection: Trying to click the \"Data collection\" button")
            mDevice.findObject(textContains("Data collection")).click()
            Log.i(TAG, "openSettingsSubMenuDataCollection: Clicked the \"Data collection\" button")

            SettingsSubMenuDataCollectionRobot().interact()
            return SettingsSubMenuDataCollectionRobot.Transition()
        }

        fun openAddonsManagerMenu(
            composeTestRule: ComposeTestRule,
            interact: SettingsSubMenuAddonsManagerRobot.() -> Unit,
        ): SettingsSubMenuAddonsManagerRobot.Transition {
            Log.i(TAG, "openAddonsManagerMenu: Trying to click the \"Add-ons\" button")
            addonsManagerButton().click()
            Log.i(TAG, "openAddonsManagerMenu: Clicked the \"Add-ons\" button")

            SettingsSubMenuAddonsManagerRobot(composeTestRule).interact()
            return SettingsSubMenuAddonsManagerRobot.Transition(composeTestRule)
        }

        fun openOpenLinksInAppsMenu(
            interact: SettingsSubMenuOpenLinksInAppsRobot.() -> Unit
        ): SettingsSubMenuOpenLinksInAppsRobot.Transition {
            Log.i(TAG, "openOpenLinksInAppsMenu: Trying to click the \"Open links in apps\" button")
            openLinksInAppsButton().click()
            Log.i(TAG, "openOpenLinksInAppsMenu: Clicked the \"Open links in apps\" button")

            SettingsSubMenuOpenLinksInAppsRobot().interact()
            return SettingsSubMenuOpenLinksInAppsRobot.Transition()
        }

        fun openHttpsOnlyModeMenu(
            interact: SettingsSubMenuHttpsOnlyModeRobot.() -> Unit
        ): SettingsSubMenuHttpsOnlyModeRobot.Transition {
            scrollToElementByText("HTTPS-Only Mode")
            Log.i(TAG, "openHttpsOnlyModeMenu: Trying to click the \"HTTPS-Only Mode\" button")
            onView(withText(getStringResource(R.string.preferences_https_only_title))).click()
            Log.i(TAG, "openHttpsOnlyModeMenu: Clicked the \"HTTPS-Only Mode\" button")
            mDevice.waitNotNull(
                Until.findObjects(By.res("$packageName:id/https_only_switch")),
                waitingTime,
            )

            SettingsSubMenuHttpsOnlyModeRobot().interact()
            return SettingsSubMenuHttpsOnlyModeRobot.Transition()
        }

        fun openExperimentsMenu(
            interact: SettingsSubMenuExperimentsRobot.() -> Unit
        ): SettingsSubMenuExperimentsRobot.Transition {
            scrollToElementByText("Nimbus Experiments")
            onView(withText(getStringResource(R.string.preferences_nimbus_experiments))).click()

            SettingsSubMenuExperimentsRobot().interact()
            return SettingsSubMenuExperimentsRobot.Transition()
        }

        fun openPageSummariesSubMenu(
            composeTestRule: ComposeTestRule,
            interact: SettingsSubMenuPageSummariesRobot.() -> Unit,
        ): SettingsSubMenuPageSummariesRobot.Transition {
            scrollToElementByText(getStringResource(R.string.preferences_page_summaries))
            assertUIObjectExists(itemContainingText(getStringResource(R.string.preferences_page_summaries)))
            Log.i(TAG, "openPageSummariesSubMenu: Trying to click the \"Page summaries\" button")
            itemContainingText(getStringResource(R.string.preferences_page_summaries)).click()
            Log.i(TAG, "openPageSummariesSubMenu: Clicked the \"Page summaries\" button")

            SettingsSubMenuPageSummariesRobot(composeTestRule).interact()
            return SettingsSubMenuPageSummariesRobot.Transition(composeTestRule)
        }
    }

    companion object {
        const val DEFAULT_APPS_SETTINGS_ACTION = "android.app.role.action.REQUEST_ROLE"
    }
}

fun settingsScreen(interact: SettingsRobot.() -> Unit): SettingsRobot.Transition {
    SettingsRobot().interact()
    return SettingsRobot.Transition()
}

private fun toggleDefaultBrowserSwitch() {
    scrollToElementByText("Privacy and security")
    Log.i(TAG, "toggleDefaultBrowserSwitch: Trying to click the \"Set as default browser\" button")
    onView(withText("Set as default browser")).perform(ViewActions.click())
    Log.i(TAG, "toggleDefaultBrowserSwitch: Clicked the \"Set as default browser\" button")
}

private fun openLinksInAppsButton() = onView(withText(R.string.preferences_open_links_in_apps))

private fun rateOnGooglePlayHeading(): UiObject {
    val rateOnGooglePlay = mDevice.findObject(UiSelector().text("Rate on Google Play"))
    settingsList().scrollToEnd(LISTS_MAXSWIPES)
    rateOnGooglePlay.waitForExists(waitingTime)

    return rateOnGooglePlay
}

private fun aboutFirefoxHeading(): UiObject {
    for (i in 1..RETRY_COUNT) {
        try {
            settingsList().scrollToEnd(LISTS_MAXSWIPES)
            assertUIObjectExists(itemWithText("About $appName"))

            break
        } catch (e: AssertionError) {
            if (i == RETRY_COUNT) {
                throw e
            }
        }
    }
    return itemContainingText("About $appName")
}

fun swipeToBottom() = onView(withId(R.id.recycler_view)).perform(ViewActions.swipeUp())

fun clickRateButtonGooglePlay() {
    rateOnGooglePlayHeading().click()
}

private fun addonsManagerButton() = onView(withText(R.string.preferences_extensions))

private fun goBackButton() = onView(CoreMatchers.allOf(withContentDescription("Navigate up")))

private fun settingsList() = UiScrollable(UiSelector().resourceId("$packageName:id/recycler_view"))
