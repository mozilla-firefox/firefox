/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.Espresso.openActionBarOverflowOrOptionsMenu
import androidx.test.espresso.action.ViewActions
import androidx.test.espresso.assertion.ViewAssertions
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.RootMatchers
import androidx.test.espresso.matcher.ViewMatchers
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withHint
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiSelector
import androidx.test.uiautomator.Until
import org.hamcrest.CoreMatchers
import org.hamcrest.CoreMatchers.containsString
import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper.assertItemIsEnabledAndVisible
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.checkedItemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithClassNameAndIndex
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.click
import org.mozilla.fenix.helpers.ext.waitNotNull

/**
 * Implementation of Robot Pattern for the Privacy Settings > saved logins sub menu
 */

class SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot {
    fun verifySecurityPromptForLogins() {
        Log.i(TAG, "verifySecurityPromptForLogins: Trying to verify that the \"Secure your saved passwords\" dialog is visible")
        onView(withText("Secure your saved passwords")).inRoot(RootMatchers.isDialog()).check(
            matches(
                withEffectiveVisibility(
                    ViewMatchers.Visibility.VISIBLE,
                ),
            ),
        )
        Log.i(TAG, "verifySecurityPromptForLogins: Verified that the \"Secure your saved passwords\" dialog is visible")
    }

    fun verifyEmptySavedLoginsListView() {
        Log.i(TAG, "verifyEmptySavedLoginsListView: Trying to verify that the saved logins section description is displayed")
        onView(withText(getStringResource(R.string.preferences_passwords_saved_logins_description_empty_text_2)))
            .check(matches(isDisplayed()))
        Log.i(TAG, "verifyEmptySavedLoginsListView: Verified that the saved logins section description is displayed")
        Log.i(TAG, "verifyEmptySavedLoginsListView: Trying to verify that the \"Learn more about Sync\" link is displayed")
        onView(withText(R.string.preferences_passwords_saved_logins_description_empty_learn_more_link_2))
            .check(matches(isDisplayed()))
        Log.i(TAG, "verifyEmptySavedLoginsListView: Verified that the \"Learn more about Sync\" link is displayed")
        Log.i(TAG, "verifyEmptySavedLoginsListView: Trying to verify that the \"Add login\" button is displayed")
        onView(withText(R.string.preferences_logins_add_login_2))
            .check(matches(isDisplayed()))
        Log.i(TAG, "verifyEmptySavedLoginsListView: Verified that the \"Add login\" button is displayed")
    }

    fun verifySavedLoginsAfterSync() {
        mDevice.waitNotNull(
            Until.findObjects(By.text("https://accounts.google.com")),
            waitingTime,
        )
        Log.i(TAG, "verifySavedLoginsAfterSync: Trying to verify that the \"https://accounts.google.comn\" login is displayed")
        onView(withText("https://accounts.google.com")).check(matches(isDisplayed()))
        Log.i(TAG, "verifySavedLoginsAfterSync: Verified that the \"https://accounts.google.comn\" login is displayed")
    }

    fun tapSetupLater() {
        Log.i(TAG, "tapSetupLater: Trying to click the \"Later\" dialog button")
        onView(withText("Later")).perform(ViewActions.click())
        Log.i(TAG, "tapSetupLater: Clicked the \"Later\" dialog button")
    }

    fun clickAddLoginButton() {
        Log.i(TAG, "clickAddLoginButton: Trying to click the \"Add login\" button")
        itemContainingText(getStringResource(R.string.preferences_logins_add_login_2)).click()
        Log.i(TAG, "clickAddLoginButton: Clicked the \"Add login\" button")
    }

    fun verifyAddNewLoginView() {
        assertUIObjectExists(
            siteHeader(),
            siteTextInput(),
            usernameHeader(),
            usernameTextInput(),
            passwordHeader(),
            passwordTextInput(),
            siteDescription(),
        )
        Log.i(TAG, "verifyAddNewLoginView: Trying to verify the \"https://www.example.com\" site text box hint")
        siteTextInputHint().check(matches(withHint(R.string.add_login_hostname_hint_text)))
        Log.i(TAG, "verifyAddNewLoginView: Verified the \"https://www.example.com\" site text box hint")
    }

    fun enterSiteCredential(website: String) {
        Log.i(TAG, "enterSiteCredential: Trying to set the \"Site\" text box text to: $website")
        siteTextInput().setText(website)
        Log.i(TAG, "enterSiteCredential: The \"Site\" text box text was set to: $website")
    }

    fun verifyHostnameErrorMessage() =
        assertUIObjectExists(itemContainingText(getStringResource(R.string.add_login_hostname_invalid_text_2)))

    fun verifyPasswordErrorMessage() =
        assertUIObjectExists(itemContainingText(getStringResource(R.string.saved_login_password_required_2)))

    fun verifyPasswordClearButtonEnabled() =
        assertItemIsEnabledAndVisible(itemWithResId("$packageName:id/clearPasswordTextButton"))

    fun verifyHostnameClearButtonEnabled() =
        assertItemIsEnabledAndVisible(itemWithResId("$packageName:id/clearHostnameTextButton"))

    fun clickSearchLoginButton() {
        Log.i(TAG, "clickSearchLoginButton: Trying to click the search logins button")
        itemWithResId("$packageName:id/search").click()
        Log.i(TAG, "clickSearchLoginButton: Clicked the search logins button")
    }

    fun clickSavedLoginsChevronIcon() {
        Log.i(TAG, "clickSavedLoginsChevronIcon: Trying to click the \"Saved logins\" chevron button")
        itemWithResId("$packageName:id/toolbar_chevron_icon").click()
        Log.i(TAG, "clickSavedLoginsChevronIcon: Clicked the \"Saved logins\" chevron button")
    }

    fun verifyLoginsSortingOptions() {
        assertUIObjectExists(itemContainingText(getStringResource(R.string.saved_logins_sort_strategy_alphabetically)))
        assertUIObjectExists(itemContainingText(getStringResource(R.string.saved_logins_sort_strategy_last_used)))
    }

    fun clickLastUsedSortingOption() {
        Log.i(TAG, "clickLastUsedSortingOption: Trying to click the \"Last used\" sorting option")
        itemContainingText(getStringResource(R.string.saved_logins_sort_strategy_last_used)).click()
        Log.i(TAG, "clickLastUsedSortingOption: Clicked the \"Last used\" sorting option")
    }

    fun verifySortedLogin(position: Int, loginTitle: String) =
        assertUIObjectExists(
            itemWithClassNameAndIndex(className = "android.view.ViewGroup", index = position)
                .getChild(
                    UiSelector()
                        .resourceId("$packageName:id/webAddressView")
                        .textContains(loginTitle),
                ),
        )

    fun searchLogin(searchTerm: String) {
        Log.i(TAG, "searchLogin: Trying to set the search bar text to: $searchTerm")
        itemWithResId("$packageName:id/search").setText(searchTerm)
        Log.i(TAG, "searchLogin: Search bar text was set to: $searchTerm")
    }

    fun verifySavedLoginsSectionUsername(username: String) =
        mDevice.waitNotNull(Until.findObjects(By.text(username)))

    fun verifyLoginItemUsername(username: String) = assertUIObjectExists(itemContainingText(username))

    fun verifyNotSavedLoginFromPrompt() {
        Log.i(TAG, "verifyNotSavedLoginFromPrompt: Trying to verify that \"test@example.com\" does not exist in the saved logins list")
        onView(withText("test@example.com"))
            .check(ViewAssertions.doesNotExist())
        Log.i(TAG, "verifyNotSavedLoginFromPrompt: Verified that \"test@example.com\" does not exist in the saved logins list")
    }

    fun verifyLocalhostExceptionAdded() {
        Log.i(TAG, "verifyLocalhostExceptionAdded: Trying to verify that \"localhost\" is visible in the exceptions list")
        onView(withText(containsString("localhost")))
            .check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyLocalhostExceptionAdded: Verified that \"localhost\" is visible in the exceptions list")
    }

    fun viewSavedLoginDetails(loginUserName: String) {
        Log.i(TAG, "viewSavedLoginDetails: Trying to click $loginUserName saved login")
        onView(withText(loginUserName)).click()
        Log.i(TAG, "viewSavedLoginDetails: Clicked $loginUserName saved login")
    }

    fun clickThreeDotButton(activityTestRule: HomeActivityIntentTestRule) {
        Log.i(TAG, "clickThreeDotButton: Trying to click the three dot button")
        openActionBarOverflowOrOptionsMenu(activityTestRule.activity)
        Log.i(TAG, "clickThreeDotButton: Clicked the three dot button")
    }

    fun clickEditLoginButton() {
        Log.i(TAG, "clickEditLoginButton: Trying to click the \"Edit\" button")
        itemContainingText("Edit").click()
        Log.i(TAG, "clickEditLoginButton: Clicked the \"Edit\" button")
    }

    fun clickDeleteLoginButton() {
        Log.i(TAG, "clickDeleteLoginButton: Trying to click the \"Delete\" button")
        itemContainingText("Delete").click()
        Log.i(TAG, "clickDeleteLoginButton: Clicked the \"Delete\" button")
    }

    fun verifyLoginDeletionPrompt() =
        assertUIObjectExists(itemContainingText(getStringResource(R.string.login_deletion_confirmation_2)))

    fun clickConfirmDeleteLogin() {
        Log.i(TAG, "clickConfirmDeleteLogin: Trying to click the \"Delete\" dialog button")
        onView(withId(android.R.id.button1)).inRoot(RootMatchers.isDialog()).click()
        Log.i(TAG, "clickConfirmDeleteLogin: Clicked the \"Delete\" dialog button")
    }

    fun clickCancelDeleteLogin() {
        Log.i(TAG, "clickCancelDeleteLogin: Trying to click the \"Cancel\" dialog button")
        onView(withId(android.R.id.button2)).inRoot(RootMatchers.isDialog()).click()
        Log.i(TAG, "clickCancelDeleteLogin: Clicked the \"Cancel\" dialog button")
    }

    fun setNewUserName(userName: String) {
        Log.i(TAG, "setNewUserName: Trying to set \"Username\" text box to: $userName")
        usernameTextInput().setText(userName)
        Log.i(TAG, "setNewUserName: \"Username\" text box was set to: $userName")
    }

    fun clickClearUserNameButton() {
        Log.i(TAG, "clickClearUserNameButton: Trying to click the clear username button")
        itemWithResId("$packageName:id/clearUsernameTextButton").click()
        Log.i(TAG, "clickClearUserNameButton: Clicked the clear username button")
    }

    fun setNewPassword(password: String) {
        Log.i(TAG, "setNewPassword: Trying to set \"Password\" text box to: $password")
        passwordTextInput().setText(password)
        Log.i(TAG, "setNewPassword: \"Password\" text box was set to: $password")
    }

    fun clickClearPasswordButton() {
        Log.i(TAG, "clickClearPasswordButton: Trying to click the clear password button")
        itemWithResId("$packageName:id/clearPasswordTextButton").click()
        Log.i(TAG, "clickClearPasswordButton: Clicked the clear password button")
    }

    fun saveEditedLogin() {
        Log.i(TAG, "saveEditedLogin: Trying to click the toolbar save button")
        itemWithResId("$packageName:id/save_login_button").click()
        Log.i(TAG, "saveEditedLogin: Clicked the toolbar save button")
    }

    fun verifySaveLoginButtonIsEnabled(isEnabled: Boolean) =
        assertUIObjectExists(
            checkedItemWithResId("$packageName:id/save_login_button", isChecked = true),
            exists = isEnabled,
        )

    fun revealPassword() {
        Log.i(TAG, "revealPassword: Trying to click the reveal password button")
        onView(withId(R.id.revealPasswordButton)).click()
        Log.i(TAG, "revealPassword: Clicked the reveal password button")
    }

    fun verifyPasswordSaved(password: String) {
        Log.i(TAG, "verifyPasswordSaved: Trying to verify that the \"Password\" text box is set to $password")
        onView(withId(R.id.passwordText)).check(matches(withText(password)))
        Log.i(TAG, "verifyPasswordSaved: Verified that the \"Password\" text box is set to $password")
    }

    fun verifyUserNameRequiredErrorMessage() =
        assertUIObjectExists(itemContainingText(getStringResource(R.string.saved_login_username_required_2)))

    fun verifyPasswordRequiredErrorMessage() =
        assertUIObjectExists(itemContainingText(getStringResource(R.string.saved_login_password_required_2)))

    fun clickGoBackButton() = goBackButton().click()

    fun clickCopyUserNameButton() =
        itemWithResId("$packageName:id/copyUsername").also {
            Log.i(TAG, "clickCopyUserNameButton: Waiting for $waitingTime ms for the copy username button to exist")
            it.waitForExists(waitingTime)
            Log.i(TAG, "clickCopyUserNameButton: Waited for $waitingTime ms for the copy username button to exist")
            Log.i(TAG, "clickCopyUserNameButton:Trying to click the copy username button")
            it.click()
            Log.i(TAG, "clickCopyUserNameButton:Clicked the copy username button")
        }

    fun clickCopyPasswordButton() =
        itemWithResId("$packageName:id/copyPassword").also {
            Log.i(TAG, "clickCopyPasswordButton: Waiting for $waitingTime ms for the copy password button to exist")
            it.waitForExists(waitingTime)
            Log.i(TAG, "clickCopyPasswordButton: Waited for $waitingTime ms for the copy password button to exist")
            Log.i(TAG, "clickCopyPasswordButton:Trying to click the copy password button")
            it.click()
            Log.i(TAG, "clickCopyPasswordButton:Clicked the copy password button")
        }

    class Transition {
        fun goBack(interact: SettingsSubMenuLoginsAndPasswordRobot.() -> Unit): SettingsSubMenuLoginsAndPasswordRobot.Transition {
            Log.i(TAG, "goBack: Trying to click the navigate up button")
            goBackButton().perform(ViewActions.click())
            Log.i(TAG, "goBack: Clicked the navigate up button")

            SettingsSubMenuLoginsAndPasswordRobot().interact()
            return SettingsSubMenuLoginsAndPasswordRobot.Transition()
        }

        fun goBackToHomeScreen(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "goBackToHomeScreen: Trying to click the navigate up button")
            goBackButton().perform(ViewActions.click())
            Log.i(TAG, "goBackToHomeScreen: Clicked the navigate up button")

            HomeScreenRobot().interact()
            return HomeScreenRobot.Transition()
        }

        fun goBackToSavedLogins(interact: SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.() -> Unit): SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.Transition {
            Log.i(TAG, "goBackToSavedLogins: Trying to click the navigate up button")
            goBackButton().perform(ViewActions.click())
            Log.i(TAG, "goBackToSavedLogins: Clicked the navigate up button")

            SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot().interact()
            return SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot.Transition()
        }

        fun goToSavedWebsite(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "goToSavedWebsite: Trying to click the open web site button")
            openWebsiteButton().click()
            Log.i(TAG, "goToSavedWebsite: Clicked the open web site button")

            BrowserRobot().interact()
            return BrowserRobot.Transition()
        }
    }
}

private fun goBackButton() =
    onView(CoreMatchers.allOf(ViewMatchers.withContentDescription("Navigate up")))

private fun openWebsiteButton() = onView(withId(R.id.openWebAddress))

private fun siteHeader() = itemWithResId("$packageName:id/hostnameHeaderText")
private fun siteTextInput() = itemWithResId("$packageName:id/hostnameText")
private fun siteDescription() = itemContainingText(getStringResource(R.string.add_login_hostname_invalid_text_3))
private fun siteTextInputHint() = onView(withId(R.id.hostnameText))
private fun usernameHeader() = itemWithResId("$packageName:id/usernameHeader")
private fun usernameTextInput() = itemWithResId("$packageName:id/usernameText")
private fun passwordHeader() = itemWithResId("$packageName:id/passwordHeader")
private fun passwordTextInput() = itemWithResId("$packageName:id/passwordText")
