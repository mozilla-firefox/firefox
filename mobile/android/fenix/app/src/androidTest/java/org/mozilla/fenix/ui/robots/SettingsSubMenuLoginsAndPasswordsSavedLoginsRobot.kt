/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.robots

import android.util.Log
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions
import androidx.test.espresso.assertion.ViewAssertions.matches
import androidx.test.espresso.matcher.RootMatchers
import androidx.test.espresso.matcher.ViewMatchers
import androidx.test.espresso.matcher.ViewMatchers.isDisplayed
import androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.uiautomator.By
import androidx.test.uiautomator.Until
import org.hamcrest.CoreMatchers.containsString
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.snackbar.SNACKBAR_TEST_TAG
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndIndex
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.ext.clearAndSetText
import org.mozilla.fenix.helpers.ext.waitNotNull
import org.mozilla.fenix.settings.logins.ui.LoginsTestingTags.ADD_LOGIN_HOST_NAME_TEXT_FIELD
import org.mozilla.fenix.settings.logins.ui.LoginsTestingTags.ADD_LOGIN_PASSWORD_TEXT_FIELD
import org.mozilla.fenix.settings.logins.ui.LoginsTestingTags.ADD_LOGIN_USER_NAME_TEXT_FIELD
import org.mozilla.fenix.settings.logins.ui.LoginsTestingTags.EDIT_LOGIN_PASSWORD_TEXT_FIELD
import org.mozilla.fenix.settings.logins.ui.LoginsTestingTags.EDIT_LOGIN_USERNAME_TEXT_FIELD
import org.mozilla.fenix.settings.logins.ui.LoginsTestingTags.LOGIN_DETAILS_PASSWORD_TEXT_FIELD
import org.mozilla.fenix.settings.logins.ui.LoginsTestingTags.SAVED_LOGINS_PASSWORD_SEARCH_FIELD

/** Implementation of Robot Pattern for the Privacy Settings > saved logins sub menu */
class SettingsSubMenuLoginsAndPasswordsSavedLoginsRobot(private val composeTestRule: ComposeTestRule) {
    fun verifySecurityPromptForLogins() {
        Log.i(
            TAG,
            "verifySecurityPromptForLogins: Trying to verify that the \"Secure your saved passwords\" dialog is visible",
        )
        onView(withText("Secure your saved passwords"))
            .inRoot(RootMatchers.isDialog())
            .check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifySecurityPromptForLogins: Verified that the \"Secure your saved passwords\" dialog is visible")
    }

    fun verifyEmptySavedLoginsListView() {
        Log.i(
            TAG,
            "verifyEmptySavedLoginsListView: Trying to verify that the saved logins section description is displayed",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.preferences_passwords_saved_logins_description_empty_text_2))
            .assertIsDisplayed()
        Log.i(TAG, "verifyEmptySavedLoginsListView: Verified that the saved logins section description is displayed")
        Log.i(
            TAG,
            "verifyEmptySavedLoginsListView: Trying to verify that the \"Learn more about Sync\" link is displayed",
        )
        composeTestRule
            .onNodeWithContentDescription("Learn more about sync Links available", useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(TAG, "verifyEmptySavedLoginsListView: Verified that the \"Learn more about Sync\" link is displayed")
        Log.i(TAG, "verifyEmptySavedLoginsListView: Verified that the \"Learn more about Sync\" link is displayed")
        Log.i(TAG, "verifyEmptySavedLoginsListView: Trying to verify that the \"Add login\" button is displayed")
        composeTestRule.onNodeWithText(getStringResource(R.string.preferences_logins_add_login_2)).assertIsDisplayed()
        Log.i(TAG, "verifyEmptySavedLoginsListView: Verified that the \"Add login\" button is displayed")
    }

    fun verifySavedLoginsAfterSync() {
        mDevice.waitNotNull(
            Until.findObjects(By.text("https://accounts.google.com")),
            waitingTime,
        )
        Log.i(
            TAG,
            "verifySavedLoginsAfterSync: Trying to verify that the \"https://accounts.google.comn\" login is displayed",
        )
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
        Log.i(TAG, "verifyAddNewLoginView: Trying to verify the \"Add login\" view items")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.preferences_passwords_saved_logins_site), useUnmergedTree = true)
            .assertIsDisplayed()
        composeTestRule.onNodeWithTag(ADD_LOGIN_HOST_NAME_TEXT_FIELD).assertIsDisplayed()
        composeTestRule
            .onNodeWithText(getStringResource(R.string.add_login_hostname_invalid_text_3), useUnmergedTree = true)
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithText(
                getStringResource(R.string.preferences_passwords_saved_logins_username),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        composeTestRule.onNodeWithTag(ADD_LOGIN_USER_NAME_TEXT_FIELD).assertIsDisplayed()
        composeTestRule
            .onNodeWithText(
                getStringResource(R.string.preferences_passwords_saved_logins_password),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        composeTestRule.onNodeWithTag(ADD_LOGIN_PASSWORD_TEXT_FIELD, useUnmergedTree = true).assertIsDisplayed()
        Log.i(TAG, "verifyAddNewLoginView: Verified the \"Add login\" view items")
    }

    fun enterSiteCredentialWhileAddingALogin(website: String) {
        Log.i(TAG, "enterSiteCredentialWhileAddingALogin: Trying to set the \"Site\" text box text to: $website")
        composeTestRule.onNodeWithTag(ADD_LOGIN_HOST_NAME_TEXT_FIELD).performClick()
        composeTestRule.onNodeWithTag(ADD_LOGIN_HOST_NAME_TEXT_FIELD).clearAndSetText(website)
        Log.i(TAG, "enterSiteCredentialWhileAddingALogin: The \"Site\" text box text was set to: $website")
    }

    fun verifyHostnameErrorMessage() {
        Log.i(TAG, "verifyHostnameErrorMessage: Trying to verify that the host name error message is displayed")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.add_login_hostname_invalid_text_2), useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(TAG, "verifyHostnameErrorMessage: Verified that the host name error message is displayed")
    }

    fun verifyPasswordErrorMessage() {
        Log.i(TAG, "verifyPasswordErrorMessage: Trying to verify that the password error message is displayed")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.saved_login_password_required_2), useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(TAG, "verifyPasswordErrorMessage: Verified that the password error message is displayed")
    }

    fun verifyPasswordClearButton() {
        Log.i(TAG, "verifyPasswordClearButton: Trying to verify that the clear password button is displayed")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.saved_logins_clear_password))
            .assertIsDisplayed()
        Log.i(TAG, "verifyPasswordClearButton: Verified that the clear password button is displayed")
    }

    fun verifyHostnameClearButton() {
        Log.i(TAG, "verifyHostnameClearButton: Trying to verify the clear host name button is displayed")
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(R.string.saved_login_clear_hostname),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(TAG, "verifyHostnameClearButton: Verified that the clear host name button is displayed")
    }

    fun clickSearchLoginButton() {
        Log.i(TAG, "clickSearchLoginButton: Trying to click the search logins button")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.preferences_passwords_saved_logins_search_2))
            .performClick()
        Log.i(TAG, "clickSearchLoginButton: Clicked the search logins button")
    }

    fun clickSortPasswordsButton() {
        Log.i(TAG, "clickSortPasswordsButton: Trying to click the \"Saved logins\" sort button")
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(R.string.saved_logins_menu_dropdown_chevron_icon_content_description_2)
            )
            .performClick()
        Log.i(TAG, "clickSortPasswordsButton: Clicked the \"Saved logins\" sort button")
    }

    fun verifyLoginsSortingOptions() {
        Log.i(TAG, "clickSortPasswordsButton: Trying to verify that the logins sorting options are displayed")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.saved_logins_sort_strategy_alphabetically))
            .assertIsDisplayed()
        composeTestRule
            .onNodeWithText(getStringResource(R.string.saved_logins_sort_strategy_last_used))
            .assertIsDisplayed()
        Log.i(TAG, "clickSortPasswordsButton: Verified that the logins sorting options are displayed")
    }

    fun clickLastUsedSortingOption() {
        Log.i(TAG, "clickLastUsedSortingOption: Trying to click the \"Last used\" sorting option")
        composeTestRule.onNodeWithText(getStringResource(R.string.saved_logins_sort_strategy_last_used)).performClick()
        Log.i(TAG, "clickLastUsedSortingOption: Clicked the \"Last used\" sorting option")
        Log.i(TAG, "clickLastUsedSortingOption: Waiting for compose rule to be idle")
        composeTestRule.waitForIdle()
        Log.i(TAG, "clickLastUsedSortingOption: Waited for compose rule to be idle")
    }

    fun verifySortedLogin(position: Int, loginItemUrl: String) {
        waitForAppWindowToBeUpdated()
        assertUIObjectExists(
            itemWithResIdAndIndex(
                "saved.logins.list.item.$loginItemUrl",
                index = position - 1,
            )
        )
    }

    fun searchLogin(searchTerm: String) {
        Log.i(TAG, "searchLogin: Trying to set the search bar text to: $searchTerm")
        composeTestRule.onNodeWithTag(SAVED_LOGINS_PASSWORD_SEARCH_FIELD).performClick()
        composeTestRule.onNodeWithTag(SAVED_LOGINS_PASSWORD_SEARCH_FIELD).clearAndSetText(searchTerm)
        Log.i(TAG, "searchLogin: Search bar text was set to: $searchTerm")
    }

    fun verifySavedLoginsSectionUsername(username: String) = mDevice.waitNotNull(Until.findObjects(By.text(username)))

    fun verifyLoginItemUsername(loginUserName: String) = {
        Log.i(
            TAG,
            "verifyLoginItemUsername: Trying to verify that the login item with user name: $loginUserName is displayed",
        )
        composeTestRule.onNodeWithText(loginUserName, useUnmergedTree = true).assertIsDisplayed()
        Log.i(TAG, "verifyLoginItemUsername: Verified that the login item with user name: $loginUserName is displayed")
    }

    fun verifyNotSavedLoginFromPrompt() {
        Log.i(
            TAG,
            "verifyNotSavedLoginFromPrompt: Trying to verify that \"test@example.com\" does not exist in the saved logins list",
        )
        composeTestRule.onNodeWithText("test@example.com", useUnmergedTree = true).assertIsNotDisplayed()
        Log.i(
            TAG,
            "verifyNotSavedLoginFromPrompt: Verified that \"test@example.com\" does not exist in the saved logins list",
        )
    }

    fun verifyLocalhostExceptionAdded() {
        Log.i(
            TAG,
            "verifyLocalhostExceptionAdded: Trying to verify that \"localhost\" is visible in the exceptions list",
        )
        onView(withText(containsString("localhost")))
            .check(matches(withEffectiveVisibility(ViewMatchers.Visibility.VISIBLE)))
        Log.i(TAG, "verifyLocalhostExceptionAdded: Verified that \"localhost\" is visible in the exceptions list")
    }

    fun viewSavedLoginDetails(loginDetail: String) {
        waitForAppWindowToBeUpdated()
        Log.i(TAG, "viewSavedLoginDetails: Trying to click $loginDetail saved login")
        composeTestRule.onNodeWithText(loginDetail, useUnmergedTree = true).performClick()
        Log.i(TAG, "viewSavedLoginDetails: Clicked $loginDetail saved login")
    }

    fun clickThreeDotButton() {
        Log.i(TAG, "clickThreeDotButton: Trying to click the three dot button")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.login_detail_menu_button_content_description))
            .performClick()
        Log.i(TAG, "clickThreeDotButton: Clicked the three dot button")
    }

    fun clickEditLoginButton() {
        Log.i(TAG, "clickEditLoginButton: Trying to click the \"Edit\" button")
        composeTestRule
            .onNodeWithText(
                getStringResource(R.string.login_detail_menu_edit_button),
                useUnmergedTree = true,
            )
            .performClick()
        Log.i(TAG, "clickEditLoginButton: Clicked the \"Edit\" button")
    }

    fun clickDeleteLoginButton() {
        Log.i(TAG, "clickDeleteLoginButton: Trying to click the \"Delete\" button")
        composeTestRule
            .onNodeWithText(
                getStringResource(R.string.login_detail_menu_delete_button),
                useUnmergedTree = true,
            )
            .performClick()
        Log.i(TAG, "clickDeleteLoginButton: Clicked the \"Delete\" button")
    }

    fun verifyLoginDeletionPrompt() {
        Log.i(TAG, "clickDeleteLoginButton: Trying to verify that the login deletion prompt is displayed")
        composeTestRule
            .onNodeWithText(
                getStringResource(R.string.login_deletion_confirmation_2),
                useUnmergedTree = true,
            )
            .assertIsDisplayed()
        Log.i(TAG, "clickDeleteLoginButton: Verified that the login deletion prompt is displayed")
    }

    fun clickConfirmDeleteLogin() {
        Log.i(TAG, "clickConfirmDeleteLogin: Trying to click the \"Delete\" dialog button")
        composeTestRule.onNodeWithText(getStringResource(R.string.bookmark_menu_delete_button)).performClick()
        Log.i(TAG, "clickConfirmDeleteLogin: Clicked the \"Delete\" dialog button")
    }

    fun clickCancelDeleteLogin() {
        Log.i(TAG, "clickCancelDeleteLogin: Trying to click the \"Cancel\" dialog button")
        composeTestRule.onNodeWithText(getStringResource(R.string.bookmark_delete_negative)).performClick()
        Log.i(TAG, "clickCancelDeleteLogin: Clicked the \"Cancel\" dialog button")
    }

    fun setNewUserNameWhileEditingALogin(userName: String) {
        Log.i(TAG, "setNewUserNameWhileEditingALogin: Trying to set \"Username\" text box to: $userName")
        composeTestRule.onNodeWithTag(EDIT_LOGIN_USERNAME_TEXT_FIELD).performClick()
        composeTestRule.onNodeWithTag(EDIT_LOGIN_USERNAME_TEXT_FIELD).clearAndSetText(userName)
        Log.i(TAG, "setNewUserNameWhileEditingALogin: \"Username\" text box was set to: $userName")
    }

    fun setUserNameWhileAddingANewLogin(userName: String) {
        Log.i(TAG, "setUserNameWhileAddingANewLogin: Trying to set \"Username\" text box to: $userName")
        composeTestRule.onNodeWithTag(ADD_LOGIN_USER_NAME_TEXT_FIELD).performClick()
        composeTestRule.onNodeWithTag(ADD_LOGIN_USER_NAME_TEXT_FIELD).clearAndSetText(userName)
        Log.i(TAG, "setUserNameWhileAddingANewLogin: \"Username\" text box was set to: $userName")
    }

    fun clickClearUserNameButton() {
        Log.i(TAG, "clickClearUserNameButton: Trying to click the clear username button")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.saved_login_clear_username))
            .performClick()
        Log.i(TAG, "clickClearUserNameButton: Clicked the clear username button")
    }

    fun setNewPasswordWhileEditingALogin(password: String) {
        Log.i(TAG, "setNewPasswordWhileEditingALogin: Trying to set \"Password\" text box to: $password")
        composeTestRule.onNodeWithTag(EDIT_LOGIN_PASSWORD_TEXT_FIELD).performClick()
        composeTestRule.onNodeWithTag(EDIT_LOGIN_PASSWORD_TEXT_FIELD).clearAndSetText(password)
        Log.i(TAG, "setNewPasswordWhileEditingALogin: \"Password\" text box was set to: $password")
    }

    fun setNewPasswordWhileAddingANewLogin(password: String) {
        Log.i(TAG, "setNewPasswordWhileAddingANewLogin: Trying to set \"Password\" text box to: $password")
        composeTestRule.onNodeWithTag(ADD_LOGIN_PASSWORD_TEXT_FIELD).performClick()
        composeTestRule.onNodeWithTag(ADD_LOGIN_PASSWORD_TEXT_FIELD).clearAndSetText(password)
        Log.i(TAG, "setNewPasswordWhileAddingANewLogin: \"Password\" text box was set to: $password")
    }

    fun clickClearPasswordButton() {
        Log.i(TAG, "clickClearPasswordButton: Trying to click the clear password button")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.saved_logins_clear_password))
            .performClick()
        Log.i(TAG, "clickClearPasswordButton: Clicked the clear password button")
    }

    fun saveEditedLogin() {
        Log.i(TAG, "saveEditedLogin: Trying to click the toolbar save button")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.edit_login_button_content_description))
            .performClick()
        Log.i(TAG, "saveEditedLogin: Clicked the toolbar save button")
        waitForAppWindowToBeUpdated()
    }

    fun saveNewLogin() {
        Log.i(TAG, "saveNewLogin: Trying to click the toolbar save button")
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(R.string.add_login_save_new_login_button_content_description)
            )
            .performClick()
        Log.i(TAG, "saveNewLogin: Clicked the toolbar save button")
        waitForAppWindowToBeUpdated()
    }

    fun verifySaveLoginButtonIsEnabled(isEnabled: Boolean) {
        if (isEnabled) {
            Log.i(TAG, "verifySaveLoginButtonIsEnabled: Trying to verify that the save login button is enabled")
            composeTestRule
                .onNodeWithContentDescription(getStringResource(R.string.edit_login_button_content_description))
                .assertIsEnabled()
            Log.i(TAG, "verifySaveLoginButtonIsEnabled: Verified that the save login button is enabled")
        } else {
            Log.i(TAG, "verifySaveLoginButtonIsEnabled: Trying to verify that the save login button is not enabled")
            composeTestRule
                .onNodeWithContentDescription(getStringResource(R.string.edit_login_button_content_description))
                .assertIsNotEnabled()
            Log.i(TAG, "verifySaveLoginButtonIsEnabled: Verified that the save login button is not enabled")
        }
    }

    fun revealPassword() {
        Log.i(TAG, "revealPassword: Trying to click the reveal password button")
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(R.string.saved_login_reveal_password),
                useUnmergedTree = true,
            )
            .performClick()
        Log.i(TAG, "revealPassword: Clicked the reveal password button")
    }

    fun verifyPasswordSaved(password: String) {
        Log.i(TAG, "verifyPasswordSaved: Trying to verify that the saved login password is set to: $password")
        composeTestRule.onNodeWithTag(LOGIN_DETAILS_PASSWORD_TEXT_FIELD).assert(hasText(password))
        Log.i(TAG, "verifyPasswordSaved: Verified that the saved login password is set to: $password")
    }

    fun verifyPasswordWhileEditingALogin(password: String) {
        Log.i(
            TAG,
            "verifyPasswordWhileEditingALogin: Trying to verify that the saved login password while editing a login is set to: $password",
        )
        composeTestRule.onNodeWithTag(EDIT_LOGIN_PASSWORD_TEXT_FIELD).assert(hasText(password))
        Log.i(
            TAG,
            "verifyPasswordWhileEditingALogin: Verified that the saved login password while editing a login is set to: $password",
        )
    }

    fun verifyUserNameRequiredErrorMessage() {
        Log.i(
            TAG,
            "verifyUserNameRequiredErrorMessage: Trying to verify the user name required error message is displayed",
        )
        composeTestRule.onNodeWithText(getStringResource(R.string.saved_login_username_required_2)).assertIsDisplayed()
        Log.i(TAG, "verifyUserNameRequiredErrorMessage: Verified the user name required error message is displayed")
    }

    fun verifyPasswordRequiredErrorMessage() {
        Log.i(
            TAG,
            "verifyUserNameRequiredErrorMessage: Trying to verify the password required error message is displayed",
        )
        composeTestRule.onNodeWithText(getStringResource(R.string.saved_login_password_required_2)).assertIsDisplayed()
        Log.i(TAG, "verifyUserNameRequiredErrorMessage: Verified the password required error message is displayed")
    }

    fun clickGoBackButton() {
        Log.i(TAG, "clickGoBackButton: Trying to click the go back button")
        composeTestRule
            .onNodeWithContentDescription(
                getStringResource(R.string.edit_login_navigate_back_button_content_description)
            )
            .performClick()
        Log.i(TAG, "clickGoBackButton: Clicked the go back button")
        waitForAppWindowToBeUpdated()
    }

    fun clickCopyUserNameButton() {
        Log.i(TAG, "clickCopyUserNameButton: Trying to click the copy username button")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.saved_login_copy_username))
            .performClick()
        Log.i(TAG, "clickCopyUserNameButton:Clicked the copy username button")
    }

    fun clickCopyPasswordButton() {
        Log.i(TAG, "clickCopyPasswordButton: Trying to click the copy password button")
        composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.saved_logins_copy_password))
            .performClick()
        Log.i(TAG, "clickCopyPasswordButton:Clicked the copy password button")
    }

    fun verifyCopyUserNameLoginCredentialsSnackBar() {
        Log.i(
            TAG,
            "verifyCopyUserNameLoginCredentialsSnackBar: Trying to verify that the \"Username copied to clipboard\" snackbar is displayed",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.logins_username_copied), useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyCopyUserNameLoginCredentialsSnackBar: Verified that the \"Username copied to clipboard\" snackbar is displayed",
        )
    }

    fun verifyCopyPasswordLoginCredentialsSnackBar() {
        Log.i(
            TAG,
            "verifyCopyPasswordLoginCredentialsSnackBar: Trying to verify that the \"Password copied to clipboard\" snackbar is displayed",
        )
        composeTestRule
            .onNodeWithText(getStringResource(R.string.logins_password_copied), useUnmergedTree = true)
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyCopyPasswordLoginCredentialsSnackBar: Verified that the \"Password copied to clipboard\" snackbar is displayed",
        )
    }

    fun waitUntilCopyLoginCredentialsSnackBarIsGone() {
        Log.i(
            TAG,
            "waitUntilCopyLoginCredentialsSnackBarIsGone: Waiting for $waitingTime until the snackbar does not exist",
        )
        composeTestRule.waitUntilDoesNotExist(hasTestTag(SNACKBAR_TEST_TAG), waitingTime)
        Log.i(
            TAG,
            "waitUntilCopyLoginCredentialsSnackBarIsGone: Waited for $waitingTime until the snackbar does not exist",
        )
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun goBack(
            interact: SettingsSubMenuLoginsAndPasswordRobot.() -> Unit
        ): SettingsSubMenuLoginsAndPasswordRobot.Transition {
            Log.i(TAG, "goBack: Trying to click the navigate up button")
            composeTestRule
                .onNodeWithContentDescription(
                    getStringResource(R.string.logins_navigate_back_button_content_description)
                )
                .performClick()
            Log.i(TAG, "goBack: Clicked the navigate up button")

            SettingsSubMenuLoginsAndPasswordRobot().interact()
            return SettingsSubMenuLoginsAndPasswordRobot.Transition()
        }

        fun goBackToHomeScreen(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "goBackToHomeScreen: Trying to click the navigate up button")
            composeTestRule
                .onNodeWithContentDescription(
                    getStringResource(R.string.logins_navigate_back_button_content_description)
                )
                .performClick()
            Log.i(TAG, "goBackToHomeScreen: Clicked the navigate up button")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        fun goToSavedWebsite(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "goToSavedWebsite: Trying to click the open web site button")
            composeTestRule
                .onNodeWithContentDescription(getStringResource(R.string.saved_login_open_site))
                .performClick()
            Log.i(TAG, "goToSavedWebsite: Clicked the open web site button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }
    }
}
