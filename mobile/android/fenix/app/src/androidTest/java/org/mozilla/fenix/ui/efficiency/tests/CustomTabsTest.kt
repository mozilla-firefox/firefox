/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import mozilla.components.support.ktx.util.PromptAbuserDetector
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.TestAssetHelper.downloadPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.navigablePageStartAsset
import org.mozilla.fenix.helpers.TestAssetHelper.navigablePageTargetAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.CustomTabsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.DownloadsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.FindInPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.NotificationSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsPasswordsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSavedPasswordsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabHistorySelectors
import org.mozilla.fenix.ui.efficiency.selectors.ToolbarSelectors

class CustomTabsTest : BaseTest() {

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    // Required by verifyDownloadInACustomTabTest: without it the download prompt's Download button
    // resolves and the click reports success, but the click is dropped as prompt abuse, so the dialog
    // stays open and the failure surfaces later as a missing snackbar. See bug 2060299.
    @Before
    fun disablePromptAbuserDetector() {
        PromptAbuserDetector.validationsEnabled = false
    }

    @After
    fun restorePromptAbuserDetector() {
        PromptAbuserDetector.validationsEnabled = true
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/249644
    // Converted from legacy CustomTabsTest.verifyCustomTabMenuItemsTest
    @SmokeTest
    @Test
    fun verifyCustomTabMenuItemsTest() {
        val customMenuItem = "TestMenuItem"
        val customTabPage = mockWebServer.getGenericAsset(1)

        on.customTabs.launchCustomTab(customTabPage.url.toString(), customMenuItem)
        on.customTabs.mozVerify(CustomTabsSelectors.CLOSE_BUTTON)
        on.customTabs.openMainMenu()
        on.customTabs.mozVerifyElementsByGroup("customTabMainMenuItems")
        on.customTabs.mozVerify(CustomTabsSelectors.MENU_CUSTOM_ITEM(customMenuItem))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/249645
    // Converted from legacy CustomTabsTest.openCustomTabInFirefoxTest
    @SmokeTest
    @Test
    fun openCustomTabInFirefoxTest() {
        val customTabPage = mockWebServer.getGenericAsset(1)

        on.customTabs.launchCustomTab(customTabPage.url.toString())
        on.customTabs.mozVerify(CustomTabsSelectors.CLOSE_BUTTON)
        on.customTabs.openMainMenu().mozClick(CustomTabsSelectors.MENU_OPEN_IN_APP)
        // "Open in Firefox" sends the page to the full browser — re-sync confirms we landed there.
        on.browserPage.navigateToPage()
        on.browserPage.verifyPageContent(customTabPage.content)
        on.browserPage.mozVerify(ToolbarSelectors.TAB_COUNTER_WITH_COUNT("1"))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080096
    // Converted from legacy MainMenuTest.verifyTheMainMenuBackButtonFromCustomTabTest
    @SmokeTest
    @Test
    fun verifyTheMainMenuBackButtonFromCustomTabTest() {
        val customTabPage = mockWebServer.getGenericAsset(4)

        on.customTabs.launchCustomTab(customTabPage.url.toString(), "TestMenuItem")
        on.customTabs.clickWebContent("Link 1")
        on.customTabs.openMainMenu().mozClick(CustomTabsSelectors.MENU_BACK)
        on.customTabs.verifyWebContent(customTabPage.content)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080097
    // Converted from legacy MainMenuTest.verifyTheMainMenuForwardButtonFromCustomTabTest
    @SmokeTest
    @Test
    fun verifyTheMainMenuForwardButtonFromCustomTabTest() {
        val firstCustomTabPage = mockWebServer.getGenericAsset(4)
        val secondCustomTabPage = mockWebServer.getGenericAsset(1)

        on.customTabs.launchCustomTab(firstCustomTabPage.url.toString(), "TestMenuItem")
        on.customTabs.clickWebContent("Link 1")
        on.customTabs.openMainMenu().mozClick(CustomTabsSelectors.MENU_BACK)
        on.customTabs.verifyWebContent(firstCustomTabPage.content)
        on.customTabs.openMainMenu().mozClick(CustomTabsSelectors.MENU_FORWARD)
        on.customTabs.verifyWebContent(secondCustomTabPage.content)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080101
    // Converted from legacy MainMenuTest.verifyTheFindInPageMenuItemInACustomTabTest
    @SmokeTest
    @Test
    fun verifyTheFindInPageMenuItemInACustomTabTest() {
        val customTabPage = mockWebServer.getGenericAsset(3)

        on.customTabs.launchCustomTab(customTabPage.url.toString())
        on.findInPage
            .navigateToPage()
            .mozVerify(FindInPageSelectors.FIND_IN_PAGE_NEXT_BUTTON)
            .mozVerify(FindInPageSelectors.FIND_IN_PAGE_PREV_BUTTON)
            .mozVerify(FindInPageSelectors.FIND_IN_PAGE_CLOSE_BUTTON)
        on.findInPage.verifyFindInPageElement("a", 3)
        on.findInPage.mozVerifyElementAbsent(FindInPageSelectors.FIND_IN_PAGE_BAR)
        // Parity gap: the legacy test then reopened find-in-page, searched "3" for a 1/1 result and closed
        // with the device back button. Reopening is not expressible yet — closing the bar returns to the
        // custom tab but leaves PageStateTracker on FindInPagePage, and there is no return edge to re-sync
        // it, so navigateToPage() would look for a FindInPagePage -> FindInPagePage path. Same stateful-nav
        // gap as the BookmarksPage -> BrowserPage one.
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334761
    // Converted from legacy CustomTabsTest.verifyDownloadInACustomTabTest
    @SmokeTest
    @Test
    fun verifyDownloadInACustomTabTest() {
        // Deliberate deviation from legacy, which loads
        // https://storage.googleapis.com/mobile_test_assets/test_app/downloads.html. The local
        // mockWebServer asset serves the same downloads.html with the same PNG link, so
        // coverage is unchanged while the test stops depending on external network.
        val customTabPage = mockWebServer.downloadPageAsset
        val downloadFile = "png_image.png"

        on.customTabs.launchCustomTab(customTabPage.url.toString(), "TestMenuItem")
        on.customTabs.clickWebContent(downloadFile)
        on.downloads
            .mozVerify(DownloadsSelectors.DOWNLOAD_DIALOG_TITLE)
            .mozVerifyElementsByGroup("downloadDialog")
            .mozClick(DownloadsSelectors.DOWNLOAD_DIALOG_CONFIRM_BUTTON)
            .mozVerify(DownloadsSelectors.DOWNLOAD_COMPLETE_SNACKBAR, timeout = 15_000)
        on.notification
            .openNotificationTray()
            .verifyNotificationExists(NotificationSelectors.SYSTEM_NOTIFICATION("Download completed"))
            .closeNotificationTray()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4245663
    // Converted from legacy MainMenuTest.verifyTheMainMenuBackButtonLongClickFromCustomTabTest
    @SmokeTest
    @Test
    fun verifyTheMainMenuBackButtonLongClickFromCustomTabTest() {
        val startPage = mockWebServer.navigablePageStartAsset
        val targetPage = mockWebServer.navigablePageTargetAsset

        on.customTabs.launchCustomTab(startPage.url.toString(), "TestMenuItem")
        on.customTabs.clickWebContent("Go to target page")
        on.customTabs.openMainMenu().mozLongClick(CustomTabsSelectors.MENU_BACK_COMPOSE)

        on.tabHistory
            .mozVerify(TabHistorySelectors.TAB_HISTORY_LIST_UIAUTOMATOR)
            .mozVerify(TabHistorySelectors.TAB_HISTORY_ITEM(startPage.url.toString()))
            .mozVerify(TabHistorySelectors.TAB_HISTORY_ITEM(targetPage.url.toString()))
        // Keeps the test self-contained rather than handing the next one an open sheet.
        on.tabHistory.dismissTabHistorySheet()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3080099
    // Converted from legacy MainMenuTest.verifyTheMainMenuShareButtonFromCustomTabTest
    // @SmokeTest even though the legacy test is a plain @Test: the case is in the TestRail smoke pool, as
    // are its three already-converted custom-tab siblings, so the missing legacy annotation is an oversight.
    @SmokeTest
    @Test
    fun verifyTheMainMenuShareButtonFromCustomTabTest() {
        val customTabPage = mockWebServer.getGenericAsset(1)

        on.customTabs.launchCustomTab(customTabPage.url.toString(), "TestMenuItem")
        // Legacy shared straight after launch, without waiting for the page. The share sheet renders either
        // way, so this is a stabiliser rather than a new assertion: it keeps the sheet's content derived from
        // the intended page instead of whatever the tab happened to be showing.
        on.customTabs.verifyWebContent(customTabPage.content)
        on.customTabs.openMainMenu().mozClick(CustomTabsSelectors.MENU_SHARE)
        on.shareOverlay.mozVerifyElementsByGroup("shareTabLayout")
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/249659
    // Converted from legacy CustomTabsTest.verifyLoginSaveInCustomTabTest
    @SmokeTest
    @Test
    fun verifyLoginSaveInCustomTabTest() {
        on.customTabs.launchCustomTab(LOGIN_FORM_URL, "TestMenuItem")

        // The login-form helpers live on BrowserPage, but they act on device-wide web-id selectors, so they
        // work just as well inside a custom tab — the same reason CustomTabsPage.clickWebContent delegates
        // to BrowserPageSelectors. Reused rather than reimplemented because setLoginFormCredentials carries
        // retry logic for a form that can reload and silently drop what was typed.
        on.browserPage
            .setLoginFormCredentials(LOGIN_USERNAME, LOGIN_PASSWORD)
            .clickSubmitLoginButton()
            .verifySaveLoginPromptIsDisplayed()
            // The legacy test stopped at the prompt's container, which its own preceding wait had already
            // required — so that assertion could not fail on its own. Asserting the Save button as well
            // makes the prompt actually usable, which is what the next click depends on.
            .mozVerify(BrowserPageSelectors.SAVE_LOGIN_PROMPT_CONFIRM_BUTTON)
            .mozClick(BrowserPageSelectors.SAVE_LOGIN_PROMPT_CONFIRM_BUTTON)

        // Re-enter through an external link, as legacy does. Legacy asserted nothing about the result, so a
        // failure here surfaced much later; assert the browser took over instead. We check ENGINE_VIEW rather
        // than the web username field: the external-link intent brings the backgrounded browser to the front
        // through an activity switch, and GeckoView's web-content accessibility nodes are not reliably rebuilt
        // afterwards (the page loads and the toolbar renders, but the form's a11y ids stay absent), so a
        // web-field assertion is flaky here. ENGINE_VIEW is the layout-invariant "browser is loaded" signal.
        on.customTabs.openUrlFromExternalLink(LOGIN_FORM_URL)
        on.browserPage.mozVerify(BrowserPageSelectors.ENGINE_VIEW, timeout = waitingTimeLong)

        // Driven click-by-click instead of on.settingsSavedPasswords.navigateToPage(): every registered edge
        // to that page ends in ClickIfPresent(LOGINS_SECURITY_DIALOG_LATER_BUTTON), which would dismiss the
        // security dialog before it could be asserted — silently dropping a legacy assertion.
        on.browserPage
            .mozClick(BrowserPageSelectors.MAIN_MENU_BUTTON)
            .mozClick(MainMenuSelectors.SETTINGS_BUTTON)
            .mozClick(SettingsSelectors.PASSWORDS_BUTTON)
            .mozClick(SettingsPasswordsSelectors.SAVED_PASSWORDS_OPTION)
            .mozVerify(SettingsSavedPasswordsSelectors.LOGINS_SECURITY_DIALOG_TITLE)
            .mozClick(SettingsSavedPasswordsSelectors.LOGINS_SECURITY_DIALOG_LATER_BUTTON)

        // Scoped to the saved-passwords list. Legacy used a device-wide By.text wait, which the origin row
        // on the same screen could also have satisfied — and which was a wait, not an assertion.
        on.settingsSavedPasswords.mozVerify(SettingsSavedPasswordsSelectors.SAVED_LOGIN_ENTRY(LOGIN_USERNAME))
    }

    private companion object {
        // A live external page, as in the legacy test and the converted LoginsTest; the local
        // password.html asset is not equivalent (it pre-fills a different username and randomises the
        // password, so it cannot cover these credentials).
        const val LOGIN_FORM_URL = "https://mozilla-mobile.github.io/testapp/loginForm"
        const val LOGIN_USERNAME = "mozilla"
        const val LOGIN_PASSWORD = "firefox"
    }
}
