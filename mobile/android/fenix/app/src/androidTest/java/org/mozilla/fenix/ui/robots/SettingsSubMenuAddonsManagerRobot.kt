/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("DEPRECATION")

package org.mozilla.fenix.ui.robots

import android.util.Log
import android.widget.RelativeLayout
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.NoMatchingViewException
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.ViewMatchers.Visibility
import androidx.test.espresso.matcher.ViewMatchers.hasDescendant
import androidx.test.espresso.matcher.ViewMatchers.hasSibling
import androidx.test.espresso.matcher.ViewMatchers.isAssignableFrom
import androidx.test.espresso.matcher.ViewMatchers.isCompletelyDisplayed
import androidx.test.espresso.matcher.ViewMatchers.isDescendantOfA
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withContentDescription
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiScrollable
import androidx.test.uiautomator.UiSelector
import androidx.test.uiautomator.Until
import mozilla.components.feature.addons.R as addonsR
import org.hamcrest.CoreMatchers.allOf
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.RETRY_COUNT
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.Constants.recommendedAddons
import org.mozilla.fenix.helpers.DataGenerationHelper.getRecommendedExtensionTitle
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectIsGone
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithDescription
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.ext.waitNotNull

/** Implementation of Robot Pattern for the Addons Management Settings. */
class SettingsSubMenuAddonsManagerRobot(private val composeTestRule: ComposeTestRule) {
    fun verifyAddonsListIsDisplayed(shouldBeDisplayed: Boolean) =
        assertUIObjectExists(addonsList(), exists = shouldBeDisplayed)

    fun waitForAddonsListProgressBarToBeGone() =
        assertUIObjectIsGone(itemWithResId("$packageName:id/add_ons_progress_bar"), waitingTime = waitingTimeLong)

    fun waitForAddonsDownloadOverlayToBeGone() =
        assertUIObjectIsGone(itemWithResId("$packageName:id/addonProgressOverlay"), waitingTime = waitingTimeLong)

    fun verifyAddonDownloadOverlay() {
        Log.i(
            TAG,
            "verifyAddonDownloadOverlay: Trying to verify that the \"Downloading and verifying extension\" prompt is displayed",
        )
        onView(withText(addonsR.string.mozac_extension_install_progress_caption)).check(matches(isDisplayed()))
        Log.i(
            TAG,
            "verifyAddonDownloadOverlay: Verified that the \"Downloading and verifying extension\" prompt is displayed",
        )
    }

    fun verifyAddonPermissionPrompt(addonName: String) {
        waitForAddonsDownloadOverlayToBeGone()
        assertUIObjectExists(
            itemWithResIdContainingText(
                "$packageName:id/title",
                "Add $addonName",
            ),
            itemWithResIdContainingText(
                "$packageName:id/optional_or_required_text",
                getStringResource(R.string.addons_permissions_heading_required_permissions),
            ),
            itemWithResIdContainingText(
                "$packageName:id/optional_settings_title",
                getStringResource(addonsR.string.mozac_feature_addons_permissions_dialog_heading_optional_settings),
            ),
            itemWithResIdContainingText(
                "$packageName:id/allow_in_private_browsing",
                getStringResource(addonsR.string.mozac_feature_addons_settings_allow_in_private_browsing_2),
            ),
            itemWithResIdContainingText(
                "$packageName:id/learn_more_link",
                getStringResource(addonsR.string.mozac_feature_addons_permissions_dialog_learn_more),
            ),
            itemWithResIdContainingText(
                "$packageName:id/deny_button",
                getStringResource(addonsR.string.mozac_feature_addons_permissions_dialog_cancel),
            ),
            itemWithResIdContainingText(
                "$packageName:id/allow_button",
                getStringResource(addonsR.string.mozac_feature_addons_permissions_dialog_add),
            ),
        )
    }

    fun clickInstallAddon(addonName: String) {
        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "verifyAddonAvailableInMainMenu: Started try #$i")
            try {
                Log.i(TAG, "clickInstallAddon: Waiting for $waitingTime ms for add-ons list to exist")
                addonsList().waitForExists(waitingTime)
                Log.i(TAG, "clickInstallAddon: Waited for $waitingTime ms for add-ons list to exist")
                Log.i(TAG, "clickInstallAddon: Trying to scroll into view the install $addonName button")
                addonsList()
                    .scrollIntoView(
                        mDevice.findObject(
                            UiSelector()
                                .resourceId("$packageName:id/details_container")
                                .childSelector(UiSelector().text(addonName))
                        )
                    )
                Log.i(TAG, "clickInstallAddon: Scrolled into view the install $addonName button")
                Log.i(TAG, "clickInstallAddon: Trying to click the install $addonName button")
                installButtonForAddon(addonName).click()
                Log.i(TAG, "clickInstallAddon: Clicked the install $addonName button")

                break
            } catch (e: NoMatchingViewException) {
                Log.i(TAG, "clickInstallAddon: NoMatchingViewException caught, executing fallback methods")
                addonsMenu(composeTestRule) {}.goBackToHomeScreen {}.openThreeDotMenu {}.clickExtensionsButton {}
            }
        }
    }

    fun verifyAddonInstallCompletedPrompt(addonName: String) {
        // Assigns a more descriptive name to the addon if it is "Bitwarden" or "Tomato Clock", otherwise keeps the
        // original name
        // The name of this extenssion is being displayed differently across the app
        val addonDisplayName =
            when (addonName) {
                "Bitwarden" -> "Bitwarden Password Manager"
                "Tomato Clock" -> "Tomato Clock - A Simple Pomodoro Timer"
                else -> addonName
            }

        assertUIObjectExists(
            itemContainingText("$addonDisplayName was added"),
            itemContainingText("Update permissions and data preferences any time in the extension settings."),
            itemContainingText("OK"),
            waitingTime = waitingTimeLong,
        )
    }

    fun closeAddonInstallCompletePrompt() {
        Log.i(
            TAG,
            "closeAddonInstallCompletePrompt: Trying to click the \"OK\" button from the completed add-on install prompt",
        )
        itemWithResIdContainingText(
                "$packageName:id/confirm_button",
                "OK",
            )
            .click()
        Log.i(
            TAG,
            "closeAddonInstallCompletePrompt: Clicked the \"OK\" button from the completed add-on install prompt",
        )
    }

    fun verifyAddonIsInstalled(addonName: String) {
        // Assigns a more descriptive name to the addon if it is "Bitwarden" or "Tomato Clock", otherwise keeps the
        // original name
        // The name of this extenssion is being displayed differently across the app
        val addonDisplayName =
            when (addonName) {
                "Bitwarden" -> "Bitwarden Password Manager"
                "Tomato Clock" -> "Tomato Clock - A Simple Pomodoro Timer"
                else -> addonName
            }

        scrollToAddon(addonDisplayName)
        Log.i(TAG, "verifyAddonIsInstalled: Trying to verify that the $addonDisplayName add-on was installed")
        onView(
                allOf(
                    withId(addonsR.id.add_button),
                    isDescendantOfA(withId(addonsR.id.add_on_item)),
                    hasSibling(hasDescendant(withText(addonDisplayName))),
                )
            )
            .check(matches(withEffectiveVisibility(Visibility.INVISIBLE)))
        Log.i(TAG, "verifyAddonIsInstalled: Verified that the $addonDisplayName add-on was installed")
    }

    fun verifyEnabledTitleDisplayed() {
        Log.i(TAG, "verifyEnabledTitleDisplayed: Trying to verify that the \"Enabled\" heading is displayed")
        onView(withText("Enabled")).check(matches(isCompletelyDisplayed()))
        Log.i(TAG, "verifyEnabledTitleDisplayed: Verified that the \"Enabled\" heading is displayed")
    }

    fun cancelInstallAddon() = cancelInstall()

    fun acceptPermissionToInstallAddon() = allowPermissionToInstall()

    fun verifyAddonsItems() {
        Log.i(TAG, "verifyAddonsItems: Trying to verify that the \"Recommended\" heading is visible")
        onView(allOf(withId(addonsR.id.title), withText("Recommended")))
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAddonsItems: Verified that the \"Recommended\" heading is visible")
        Log.i(TAG, "verifyAddonsItems: Trying to verify that all uBlock Origin items are completely displayed")
        onView(
                allOf(
                    isAssignableFrom(RelativeLayout::class.java),
                    withId(addonsR.id.add_on_item),
                    hasDescendant(allOf(withId(addonsR.id.add_on_icon), isCompletelyDisplayed())),
                    hasDescendant(
                        allOf(
                            withId(addonsR.id.details_container),
                            hasDescendant(withText("uBlock Origin")),
                            hasDescendant(
                                withText("Finally, an efficient wide-spectrum content blocker. Easy on CPU and memory.")
                            ),
                            hasDescendant(withId(addonsR.id.rating)),
                            hasDescendant(withId(addonsR.id.review_count)),
                        )
                    ),
                    hasDescendant(withId(addonsR.id.add_button)),
                )
            )
            .check(matches(isCompletelyDisplayed()))
        Log.i(TAG, "verifyAddonsItems: Verified that all uBlock Origin items are completely displayed")
    }

    fun verifyAddonCanBeInstalled(addonName: String) {
        scrollToAddon(addonName)
        mDevice.waitNotNull(Until.findObject(By.text(addonName)), waitingTime)
        Log.i(TAG, "verifyAddonCanBeInstalled: Trying to verify that the install $addonName button is visible")
        onView(
                allOf(
                    withId(addonsR.id.add_button),
                    hasSibling(
                        hasDescendant(
                            allOf(
                                withId(addonsR.id.add_on_name),
                                withText(addonName),
                            )
                        )
                    ),
                )
            )
            .check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
        Log.i(TAG, "verifyAddonCanBeInstalled: Verified that the install $addonName button is visible")
    }

    fun selectAllowInPrivateBrowsing() {
        assertUIObjectExists(itemWithText("Allow extension to run in private browsing"), waitingTime = waitingTimeLong)
        Log.i(TAG, "selectAllowInPrivateBrowsing: Trying to click the \"Allow in private browsing\" check box")
        onView(withId(addonsR.id.allow_in_private_browsing)).click()
        Log.i(TAG, "selectAllowInPrivateBrowsing: Clicked the \"Allow in private browsing\" check box")
    }

    fun installAddon(addonName: String) {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickExtensionsButton {
                waitForAddonsListProgressBarToBeGone()
                clickInstallAddon(addonName)
                verifyAddonPermissionPrompt(addonName)
                acceptPermissionToInstallAddon()
                verifyAddonInstallCompletedPrompt(addonName)
            }
    }

    fun installAddonInPrivateMode(addonName: String) {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickExtensionsButton {
                waitForAddonsListProgressBarToBeGone()
                clickInstallAddon(addonName)
                verifyAddonPermissionPrompt(addonName)
                selectAllowInPrivateBrowsing()
                acceptPermissionToInstallAddon()
                verifyAddonInstallCompletedPrompt(addonName)
            }
    }

    fun verifyRecommendedAddonsViewFromRedesignedMainMenu(composeTestRule: ComposeTestRule) {
        verifyTheRecommendedAddons(composeTestRule)
        Log.i(
            TAG,
            "verifyRecommendedAddonsViewFromRedesignedMainMenu: Trying to verify that that the \"Discover more extensions\" button is displayed",
        )
        composeTestRule
            .onNode(
                hasText(getStringResource(R.string.browser_menu_discover_more_extensions)),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyRecommendedAddonsViewFromRedesignedMainMenu: Verified that that the \"Discover more extensions\" button is displayed",
        )
    }

    fun verifyTheRecommendedAddons(composeTestRule: ComposeTestRule) {
        var verifiedCount = 0

        for (i in 1..RETRY_COUNT) {
            Log.i(TAG, "verifyTheRecommendedAddons: Started try #$i")
            try {
                recommendedAddons.forEach { addon ->
                    if (verifiedCount == 3) return
                    try {
                        waitForAppWindowToBeUpdated()
                        Log.i(
                            TAG,
                            "verifyTheRecommendedAddons: Trying to verify that addon: $addon is recommended and displayed",
                        )
                        composeTestRule.onNode(hasText(addon, substring = true)).assertIsDisplayed()
                        Log.i(
                            TAG,
                            "verifyTheRecommendedAddons: Verified that addon: $addon is recommended and displayed",
                        )

                        Log.i(
                            TAG,
                            "verifyTheRecommendedAddons: Trying to verify that addon: $addon install button is displayed",
                        )
                        composeTestRule
                            .onNode(hasContentDescription("Add $addon", substring = true))
                            .assertIsDisplayed()
                        Log.i(TAG, "verifyTheRecommendedAddons: Verify that addon: $addon install button is displayed")

                        verifiedCount++
                    } catch (e: AssertionError) {
                        Log.i(TAG, "verifyTheRecommendedAddons: Addon: $addon is not displayed, moving to the next one")
                    }
                }
                if (verifiedCount < 3) {
                    throw AssertionError(
                        "$TAG, verifyTheRecommendedAddons: Less than 3 addons were verified. Only $verifiedCount addons were verified."
                    )
                }

                break
            } catch (e: AssertionError) {
                Log.i(TAG, "verifyTheRecommendedAddons: AssertionError caught, executing fallback methods")
                if (i == RETRY_COUNT) {
                    throw e
                } else {
                    Log.i(
                        TAG,
                        "verifyTheRecommendedAddons: Trying to click device back button to dismiss the main menu",
                    )
                    mDevice.pressBack()
                    Log.i(TAG, "verifyTheRecommendedAddons: Clicked device back button to dismiss the main menu")
                    waitForAppWindowToBeUpdated()
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {
                            verifyTryRecommendedExtensionButton()
                        }
                        .clickExtensionsButton {}
                }
            }
        }
    }

    fun installRecommendedAddon(composeTestRule: ComposeTestRule): String {
        var recommendedExtensionTitle = ""

        repeat(RETRY_COUNT) { i ->
            val attempt = i + 1
            Log.i(TAG, "installRecommendedAddon: Started try #$attempt")
            try {
                recommendedExtensionTitle = getRecommendedExtensionTitle(composeTestRule)
                waitForAppWindowToBeUpdated()
                assertUIObjectExists(itemContainingText(recommendedExtensionTitle))
                Log.i(TAG, "installRecommendedAddon: Trying to click addon: $recommendedExtensionTitle install button")
                itemWithDescription("Add $recommendedExtensionTitle").click()
                Log.i(TAG, "installRecommendedAddon: Clicked addon: $recommendedExtensionTitle install button")
                assertUIObjectExists(
                    itemWithResIdContainingText(
                        "$packageName:id/allow_button",
                        getStringResource(addonsR.string.mozac_feature_addons_permissions_dialog_add),
                    )
                )

                return recommendedExtensionTitle
            } catch (e: AssertionError) {
                Log.i(TAG, "installRecommendedAddon: AssertionError caught, executing fallback methods")
                if (attempt == RETRY_COUNT) {
                    throw e
                } else {
                    Log.i(TAG, "installRecommendedAddon: Trying to click device back button to dismiss the main menu")
                    mDevice.pressBack()
                    Log.i(TAG, "installRecommendedAddon: Clicked device back button to dismiss the main menu")
                    waitForAppWindowToBeUpdated()
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {
                            verifyTryRecommendedExtensionButton()
                        }
                        .clickExtensionsButton {
                            recommendedExtensionTitle = getRecommendedExtensionTitle(composeTestRule)
                        }
                }
            }
        }
        return recommendedExtensionTitle
    }

    fun verifyManageExtensionsButtonFromRedesignedMainMenu(composeTestRule: ComposeTestRule, isDisplayed: Boolean) {
        if (isDisplayed) {
            Log.i(
                TAG,
                "verifyManageExtensionsButtonFromRedesignedMainMenu: Trying to verify that the \"Manage extensions\" button is displayed",
            )
            composeTestRule
                .onNodeWithText(getStringResource(R.string.browser_menu_manage_extensions), useUnmergedTree = true)
                .assertIsDisplayed()
            Log.i(
                TAG,
                "verifyManageExtensionsButtonFromRedesignedMainMenu: Verified that the \"Manage extensions\" button is displayed",
            )
        } else {
            Log.i(
                TAG,
                "verifyManageExtensionsButtonFromRedesignedMainMenu: Trying to verify that the \"Manage extensions\" button is not displayed",
            )
            composeTestRule
                .onNodeWithText(getStringResource(R.string.browser_menu_manage_extensions), useUnmergedTree = true)
                .assertIsNotDisplayed()
            Log.i(
                TAG,
                "verifyManageExtensionsButtonFromRedesignedMainMenu: Verified that the \"Manage extensions\" button is not displayed",
            )
        }
    }

    fun verifyExtensionsButtonWithInstalledExtension(extensionTitle: String) {
        composeTestRule.onNode(extensionsItem().and(hasText(extensionTitle, substring = true))).assertExists()
    }

    fun clickManageExtensionsButtonFromRedesignedMainMenu(composeTestRule: ComposeTestRule) {
        waitForAppWindowToBeUpdated()
        Log.i(TAG, "clickManageExtensionsButtonFromRedesignedMainMenu: Trying to click the manage extensions button")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.browser_menu_manage_extensions), useUnmergedTree = true)
            .performClick()
        Log.i(TAG, "clickManageExtensionsButtonFromRedesignedMainMenu: Clicked the manage extensions button")
    }

    fun verifyDiscoverMoreExtensionsButton(composeTestRule: ComposeTestRule, isDisplayed: Boolean) {
        if (isDisplayed) {
            Log.i(
                TAG,
                "verifyDiscoverMoreExtensionsButton: Trying to verify that the \"Discover more\" button is displayed",
            )
            composeTestRule
                .onNode(
                    hasText(getStringResource(R.string.browser_menu_discover_more_extensions)),
                    useUnmergedTree = true,
                )
                .assertIsDisplayed()
            Log.i(TAG, "verifyDiscoverMoreExtensionsButton: Verified that the \"Discover more\" button is displayed")
        } else {
            Log.i(
                TAG,
                "verifyDiscoverMoreExtensionsButton: Trying to verify that the \"Discover more\" button is not displayed",
            )
            composeTestRule
                .onNode(
                    hasText(getStringResource(R.string.browser_menu_discover_more_extensions)),
                    useUnmergedTree = true,
                )
                .assertIsNotDisplayed()
            Log.i(
                TAG,
                "verifyDiscoverMoreExtensionsButton: Verified that the \"Discover more\" button is not displayed",
            )
        }
    }

    fun verifyInstalledExtension(composeTestRule: ComposeTestRule, extensionTitle: String) {
        Log.i(TAG, "verifyInstalledExtension: Trying to verify that extension: $extensionTitle is displayed")
        composeTestRule.onNode(hasText(extensionTitle, substring = true).and(!extensionsItem())).assertIsDisplayed()
        Log.i(TAG, "verifyInstalledExtension: Verified that extension: $extensionTitle is displayed")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun goBackToHomeScreen(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "goBackToHomeScreen: Trying to click navigate up toolbar button")
            onView(allOf(withContentDescription("Navigate up"))).click()
            Log.i(TAG, "goBackToHomeScreen: Clicked the navigate up toolbar button")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun goBackToBrowser(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "goBackToBrowser: Trying to click navigate up toolbar button")
            onView(allOf(withContentDescription("Navigate up"))).click()
            Log.i(TAG, "goBackToBrowser: Clicked the navigate up toolbar button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun openDetailedMenuForAddon(
            addonName: String,
            interact: SettingsSubMenuAddonsManagerAddonDetailedMenuRobot.() -> Unit,
        ): SettingsSubMenuAddonsManagerAddonDetailedMenuRobot.Transition {
            // Assigns a more descriptive name to the addon if it is "Bitwarden" or "Tomato Clock", otherwise keeps the
            // original name
            // The name of this extenssion is being displayed differently across the app
            val addonDisplayName =
                when (addonName) {
                    "Bitwarden" -> "Bitwarden Password Manager"
                    "Tomato Clock" -> "Tomato Clock - A Simple Pomodoro Timer"
                    else -> addonName
                }

            scrollToAddon(addonDisplayName)
            Log.i(TAG, "openDetailedMenuForAddon: Trying to verify that the $addonDisplayName add-on is visible")
            addonItem(addonDisplayName).check(matches(withEffectiveVisibility(Visibility.VISIBLE)))
            Log.i(TAG, "openDetailedMenuForAddon: Verified that the $addonDisplayName add-on is visible")
            Log.i(TAG, "openDetailedMenuForAddon: Trying to click the $addonDisplayName add-on")
            addonItem(addonDisplayName).perform(click())
            Log.i(TAG, "openDetailedMenuForAddon: Clicked the $addonDisplayName add-on")

            SettingsSubMenuAddonsManagerAddonDetailedMenuRobot().interact()
            return SettingsSubMenuAddonsManagerAddonDetailedMenuRobot.Transition(composeTestRule)
        }

        fun clickExtensionsPromotionBannerLearnMoreLink(
            composeTestRule: ComposeTestRule,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(TAG, "clickExtensionsPromotionBannerLearnMoreLink: Trying to click the \"Learn more\" link")
            composeTestRule.onNode(hasContentDescription("Learn more Links available")).performClick()
            Log.i(TAG, "clickExtensionsPromotionBannerLearnMoreLink: Clicked the \"Learn more\" link")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickDiscoverMoreExtensionsButton(
            composeTestRule: ComposeTestRule,
            interact: BrowserRobot.() -> Unit,
        ): BrowserRobot.Transition {
            Log.i(TAG, "clickDiscoverMoreExtensionsButton: Trying to click the \"Discover more extensions\" link")
            mDevice.waitForIdle()
            composeTestRule.waitUntil(waitingTimeLong) {
                composeTestRule
                    .onAllNodes(
                        hasText(getStringResource(R.string.browser_menu_discover_more_extensions)),
                        useUnmergedTree = true,
                    )
                    .fetchSemanticsNodes(atLeastOneRootRequired = false)
                    .isNotEmpty()
            }
            composeTestRule
                .onNode(
                    hasText(getStringResource(R.string.browser_menu_discover_more_extensions)),
                    useUnmergedTree = true,
                )
                .performClick()
            Log.i(TAG, "clickDiscoverMoreExtensionsButton: Clicked the \"Discover more extensions\" link")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }
    }

    private fun installButtonForAddon(addonName: String) =
        onView(
            allOf(
                withContentDescription("Install $addonName"),
                isDescendantOfA(withId(addonsR.id.add_on_item)),
                hasSibling(hasDescendant(withText(addonName))),
            )
        )

    private fun cancelInstall() {
        Log.i(TAG, "cancelInstall: Trying to verify that the \"Cancel\" button is completely displayed")
        onView(allOf(withId(addonsR.id.deny_button), withText("Cancel"))).check(matches(isCompletelyDisplayed()))
        Log.i(TAG, "cancelInstall: Verified that the \"Cancel\" button is completely displayed")
        Log.i(TAG, "cancelInstall: Trying to click the \"Cancel\" button")
        onView(allOf(withId(addonsR.id.deny_button), withText("Cancel"))).perform(click())
        Log.i(TAG, "cancelInstall: Clicked the \"Cancel\" button")
    }

    private fun allowPermissionToInstall() {
        // PermissionsDialogFragment disables the "Add" button for ~1s after the dialog is shown.
        Log.i(TAG, "allowPermissionToInstall: Waiting for the \"Add\" button to be enabled")
        val allowButton =
            mDevice.wait(
                Until.findObject(By.res("$packageName:id/allow_button").enabled(true)),
                waitingTime,
            )
        Log.i(TAG, "allowPermissionToInstall: Trying to click the \"Add\" button")
        allowButton.click()
        Log.i(TAG, "allowPermissionToInstall: Clicked the \"Add\" button")
    }

    fun clickCollapseExtensionsChevronFromMainMenu(composeTestRule: ComposeTestRule) {
        Log.i(
            TAG,
            "clickExtensionsChevronFromMainMenu: Trying to click the \"Extensions chevron\" button from the new main menu design.",
        )
        composeTestRule.onNode(extensionsItem()).performClick()
        Log.i(
            TAG,
            "clickExtensionsChevronFromMainMenu: Clicked the \"Extensions chevron\" button from the new main menu design.",
        )
    }

    fun verifyExtensionsMainMenuOptionIsCollapsed(composeTestRule: ComposeTestRule, areExtensionsInstalled: Boolean) {
        if (areExtensionsInstalled) {
            Log.i(
                TAG,
                "verifyExtensionsMainMenuOptionIsCollapsed: Trying to verify that the \"Manage extensions\" button is not displayed",
            )
            composeTestRule
                .onNode(
                    hasText(getStringResource(R.string.browser_menu_manage_extensions)),
                    useUnmergedTree = true,
                )
                .assertDoesNotExist()
            Log.i(
                TAG,
                "verifyExtensionsMainMenuOptionIsCollapsed: Verified that the \"Manage extensions\" button is not displayed",
            )
        } else {
            Log.i(
                TAG,
                "verifyExtensionsMainMenuOptionIsCollapsed: Trying to verify that the \"Discover more extensions\" button is not displayed",
            )
            composeTestRule
                .onNode(
                    hasText(getStringResource(R.string.browser_menu_discover_more_extensions)),
                    useUnmergedTree = true,
                )
                .assertDoesNotExist()
            Log.i(
                TAG,
                "verifyExtensionsMainMenuOptionIsCollapsed: Verified that the \"Discover more extensions\" button is not displayed",
            )
        }
    }
}

fun addonsMenu(
    composeTestRule: ComposeTestRule,
    interact: SettingsSubMenuAddonsManagerRobot.() -> Unit,
): SettingsSubMenuAddonsManagerRobot.Transition {
    SettingsSubMenuAddonsManagerRobot(composeTestRule).interact()
    return SettingsSubMenuAddonsManagerRobot.Transition(composeTestRule)
}

private fun scrollToAddon(addonName: String) {
    Log.i(TAG, "scrollToAddon: Trying to scroll into view add-on: $addonName")
    addonsList()
        .scrollIntoView(
            itemWithResIdContainingText(
                resourceId = "$packageName:id/add_on_name",
                text = addonName,
            )
        )
    Log.i(TAG, "scrollToAddon: Scrolled into view add-on: $addonName")
}

private fun addonItem(addonName: String) =
    onView(
        allOf(
            withId(addonsR.id.add_on_item),
            hasDescendant(
                allOf(
                    withId(addonsR.id.add_on_name),
                    withText(addonName),
                )
            ),
        )
    )

private fun addonsList() = UiScrollable(UiSelector().resourceId("$packageName:id/add_ons_list")).setAsVerticalList()

private fun extensionsItem() = hasText(getStringResource(R.string.browser_menu_extensions))
