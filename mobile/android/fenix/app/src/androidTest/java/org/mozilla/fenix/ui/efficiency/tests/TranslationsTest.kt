/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestAssetHelper.firstForeignWebPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.secondForeignWebPageAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection
import org.mozilla.fenix.ui.efficiency.navigation.LaunchConfig
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.CustomTabsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsTranslationSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TranslationsSelectors

class TranslationsTest : BaseTest(LaunchConfig(isPageLoadTranslationsPromptEnabled = true)) {

    @SmokeTest
    @Test
    fun verifyTheFirstTranslationNotNowButtonFunctionalityTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset
        val url = testPage.url.toString()

        on.browserPage
            .navigateToPage(url)
            .verifyTranslationSheetWithReload(url)
            .mozClickIfPresent(BrowserPageSelectors.TRANSLATION_SHEET_NOT_NOW_BUTTON)
            .mozWaitUntilAbsent(BrowserPageSelectors.TRANSLATION_SHEET_NOT_NOW_BUTTON, TestAssetHelper.waitingTimeLong)
        on.browserPage
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATE_BUTTON)
            .mozVerifyElementsByGroup(BrowserPageSelectors.Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET)
            .mozSwipeElementUntilAbsent(BrowserPageSelectors.TRANSLATION_SHEET, SwipeDirection.DOWN, maxSwipes = 3)
        on.browserPage
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATE_BUTTON)
            .mozVerifyElementsByGroup(BrowserPageSelectors.Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2439667
    @SmokeTest
    @Test
    fun verifyTheDownloadLanguagesFunctionalityTest() {
        val url = mockWebServer.firstForeignWebPageAsset.url.toString()

        on.browserPage
            .navigateToPage(url)
            .verifyTranslationSheetWithReload(url)
            .mozClick(TranslationsSelectors.TRANSLATIONS_OPTIONS_BUTTON)
            .mozClick(TranslationsSelectors.TRANSLATION_SETTINGS_BUTTON)
            .mozClick(SettingsTranslationSelectors.DOWNLOAD_LANGUAGES_BUTTON)
            .mozClick(SettingsTranslationSelectors.DOWNLOAD_LANGUAGE_ROW(LANGUAGE_TO_DOWNLOAD))
            // The download is a real multi-MB model fetch, hence the long timeout (legacy waits 30s).
            .mozVerify(
                SettingsTranslationSelectors.DOWNLOAD_LANGUAGE_ROW(LANGUAGE_TO_DOWNLOAD),
                timeout = TestAssetHelper.waitingTimeLong,
            )
            // Deliberately STRONGER than the legacy assertion, which re-matched the same
            // content-description substring as the click target above and so held whether or not anything
            // downloaded. This one only holds once a model is really on disk.
            .mozVerify(
                SettingsTranslationSelectors.ANY_LANGUAGE_DOWNLOADED,
                timeout = TestAssetHelper.waitingTimeLong,
            )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437991
    @SmokeTest
    @Test
    fun verifyTheNeverTranslateOptionTest() {
        val firstUrl = mockWebServer.firstForeignWebPageAsset.url.toString()
        val secondPage = mockWebServer.secondForeignWebPageAsset

        on.browserPage
            .navigateToPage(firstUrl)
            .verifyTranslationSheetWithReload(firstUrl)
            .mozClick(TranslationsSelectors.TRANSLATIONS_OPTIONS_BUTTON)
            .mozClick(TranslationsSelectors.NEVER_TRANSLATE_LANGUAGE(LANGUAGE_TO_NEVER_TRANSLATE))
            .mozVerify(TranslationsSelectors.NEVER_TRANSLATE_DESCRIPTION)
            .mozVerifyElementIsSelected(TranslationsSelectors.NEVER_TRANSLATE_LANGUAGE(LANGUAGE_TO_NEVER_TRANSLATE))
            .mozClick(TranslationsSelectors.TRANSLATION_SETTINGS_BUTTON)
            .mozClick(SettingsTranslationSelectors.AUTOMATIC_TRANSLATION_BUTTON)
            // The list is alphabetical and long, so the row is below the fold. Legacy only scrolled as a
            // side effect of the retry loop inside its assertion; doing it explicitly keeps the assertion
            // an assertion.
            .mozSwipeTo(
                SettingsTranslationSelectors.AUTOMATIC_TRANSLATION_LANGUAGE(LANGUAGE_TO_NEVER_TRANSLATE),
                SwipeDirection.UP,
            )
            // Legacy verifyNeverAutomaticallyTranslateForLanguage: the language row must carry the
            // "Never translate" summary as a sibling.
            .mozVerifyElementHasSiblingWithText(
                SettingsTranslationSelectors.AUTOMATIC_TRANSLATION_LANGUAGE(LANGUAGE_TO_NEVER_TRANSLATE),
                getStringResource(R.string.automatic_translation_option_never_translate_title_preference),
            )
            .mozClick(SettingsTranslationSelectors.AUTOMATIC_TRANSLATION_LANGUAGE(LANGUAGE_TO_NEVER_TRANSLATE))
            .mozVerifyElementIsSelected(SettingsTranslationSelectors.NEVER_TRANSLATE_PREFERENCE)
            // Back out of the two sub-screens and then the settings screen, as legacy does.
            .mozPressBack()
            .mozPressBack()
            .mozPressBack()
            .mozSwipeElementUntilAbsent(BrowserPageSelectors.TRANSLATION_SHEET, SwipeDirection.DOWN, maxSwipes = 3)

        // A brand-new tab, so the second French page gets its own chance to offer a translation.
        on.tabDrawer
            .navigateToPage()
            .mozVerify(TabDrawerSelectors.TABS_TRAY)
            .mozVerify(TabDrawerSelectors.FAB)
            .mozClick(TabDrawerSelectors.FAB)
            .mozClearAndEnterText(secondPage.url.toString(), SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)
            .mozPressEnter(SearchBarSelectors.TOOLBAR_IN_EDIT_MODE)

        on.browserPage
            .verifyPageContent(secondPage.content)
            // The point of the test. Legacy used assertIsNotDisplayed with no wait, which Compose also
            // satisfies when the node is simply absent — so it could hold before the prompt had any chance
            // to appear. This waits out the offer window instead and requires it to never show.
            .mozVerifyElementStaysAbsent(BrowserPageSelectors.TRANSLATION_SHEET)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437107
    @SmokeTest
    @Test
    fun verifyMainMenuTranslationButtonFunctionalityTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset
        val url = testPage.url.toString()

        on.browserPage
            .navigateToPage(url)
            .verifyTranslationSheetWithReload(url)
            .mozClickIfPresent(BrowserPageSelectors.TRANSLATION_SHEET_NOT_NOW_BUTTON)
        on.browserPage
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATE_BUTTON)
            .mozVerifyElementsByGroup(BrowserPageSelectors.Group.NOT_TRANSLATED_PAGE_TRANSLATION_SHEET)
            .mozClickIfPresent(BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON)
            .mozWaitUntilAbsent(
                BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON,
                TestAssetHelper.waitingTimeLong,
            )
        on.browserPage.verifyPageContent("Article of the day")
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2532977
    @Critical
    @Test
    fun verifyATranslatableCustomTabTest() {
        val customTabPage = mockWebServer.firstForeignWebPageAsset

        on.customTabs.launchCustomTab(customTabPage.url.toString(), "TestMenuItem")
        on.customTabs.openMainMenu().mozClick(CustomTabsSelectors.MENU_OPEN_IN_APP)
        on.browserPage
            .navigateToPage()
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATE_BUTTON)
        on.browserPage.verifyTranslationSheetWithReload(customTabPage.url.toString())
    }

    private companion object {
        // The legacy test hardcodes Bosnian; it must be an offered translations model.
        const val LANGUAGE_TO_DOWNLOAD = "Bosnian"

        // The mock page is fr-FR, so the engine reports French as the detected source language.
        const val LANGUAGE_TO_NEVER_TRANSLATE = "French"
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3105343
    @Critical
    @Test
    fun verifyTranslationBottomSheetNotDisplayedWhenPageIsOnAlwaysListTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset

        on.browserPage
            .navigateToPage(testPage.url.toString())
            .verifyTranslationSheetWithReload(testPage.url.toString())
            .mozClick(TranslationsSelectors.TRANSLATIONS_OPTIONS_BUTTON)
            .mozClick(TranslationsSelectors.ALWAYS_TRANSLATE_LANGUAGE("French"))
            .mozClick(TranslationsSelectors.TRANSLATION_SHEET_BACK_BUTTON)
            .mozSwipeElementUntilAbsent(
                TranslationsSelectors.TRANSLATION_SHEET_HANDLE,
                SwipeDirection.DOWN,
                maxSwipes = 3,
            )
            .mozWaitUntilAbsent(
                TranslationsSelectors.TRANSLATION_IN_PROGRESS_SNACK_BAR,
                TestAssetHelper.waitingTimeLong,
            )
        on.browserPage.verifyPageContent("Article of the day")
        on.tabDrawer.navigateToPage().closeAllTabs()
        on.home.navigateToPage()
        on.browserPage
            .navigateToPage(testPage.url.toString())
            .verifyPageContent("Article of the day")
            .verifyTranslationSheetIsDisplayed(isDisplayed = false)
        on.mainMenu
            .navigateToPage()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATE_BUTTON)
        on.browserPage.verifyTranslationSheetWithReload(testPage.url.toString())
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3167207
    @Critical
    @Test
    fun verifyTheShowOriginalOptionFromTheTranslationBannerTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset

        on.browserPage
            .navigateToPage(testPage.url.toString())
            .verifyTranslationSheetWithReload(testPage.url.toString())
            .mozClickIfPresent(BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON)
            .mozWaitUntilAbsent(BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON)
        on.browserPage
            .verifyPageContent("Article of the day")
            .mozVerify(BrowserPageSelectors.TRANSLATION_BANNER_TRANSLATED_FROM_TO("French", "English"))
            .mozVerifyElementsByGroup(BrowserPageSelectors.Group.TRANSLATION_BANNER)
            .mozClick(BrowserPageSelectors.TRANSLATION_BANNER_OPEN_TRANSLATION_SHEET_CHEVRON)
            .mozVerify(BrowserPageSelectors.TRANSLATED_SHEET_TITLE("French", "English"))
            .mozVerifyElementsByGroup(BrowserPageSelectors.Group.TRANSLATED_PAGE_TRANSLATION_SHEET)
            .mozClick(BrowserPageSelectors.TRANSLATED_SHEET_SHOW_ORIGINAL_BUTTON)
            .mozWaitUntilAbsent(BrowserPageSelectors.TRANSLATED_SHEET_SHOW_ORIGINAL_BUTTON)
        on.browserPage.verifyPageContent(testPage.content)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3105333
    @Critical
    @Test
    fun verifyFirstTranslationBottomSheetTranslateFunctionalityTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset

        on.browserPage
            .navigateToPage(testPage.url.toString())
            .verifyTranslationSheetWithReload(testPage.url.toString())
            .mozClickIfPresent(BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON)
            .mozWaitUntilAbsent(BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON)
        on.browserPage
            .verifyPageContent("Article of the day")
            .mozVerify(BrowserPageSelectors.TRANSLATION_BANNER_TRANSLATED_FROM_TO("French", "English"))
            .mozVerifyElementsByGroup(BrowserPageSelectors.Group.TRANSLATION_BANNER)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3105343
    @Critical
    @Test
    fun verifyTranslationBottomSheetNotDisplayedWhenAlwaysOfferToTranslateIsOffTest() {
        val firstTestPage = mockWebServer.firstForeignWebPageAsset
        // The auto-offer is suppressed once per host for the whole session (an in-memory, non-persisted set that
        // is only cleared on app restart), and all local assets share the same host. The real-world equivalent -
        // restarting the app so the same host offers again - is not feasible to automate here, so instead we
        // re-trigger the offer with a different-host (external) page.
        val secondTestPage = "https://mozilla-mobile.github.io/testapp/v2.0/germanForeignWebPage.html"

        on.browserPage
            .navigateToPage(firstTestPage.url.toString())
            .verifyTranslationSheetWithReload(firstTestPage.url.toString())
            .mozClick(TranslationsSelectors.TRANSLATIONS_OPTIONS_BUTTON)
            .mozVerifyElementIsSelected(TranslationsSelectors.ALWAYS_OFFER_TO_TRANSLATE)
            .mozClick(TranslationsSelectors.ALWAYS_OFFER_TO_TRANSLATE)
            .mozVerifyElementIsNotSelected(TranslationsSelectors.ALWAYS_OFFER_TO_TRANSLATE)
            .mozClick(TranslationsSelectors.TRANSLATION_SHEET_BACK_BUTTON)
            .mozSwipeElementUntilAbsent(
                TranslationsSelectors.TRANSLATION_SHEET_HANDLE,
                SwipeDirection.DOWN,
                maxSwipes = 3,
            )
        on.tabDrawer.navigateToPage().closeAllTabs()
        on.home.navigateToPage()
        on.browserPage
            .navigateToPage(firstTestPage.url.toString())
            .verifyPageContent(firstTestPage.content)
            .verifyTranslationSheetIsDisplayed(isDisplayed = false)
        on.browserPage
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATE_BUTTON)
        on.browserPage
            .verifyTranslationSheetWithReload(firstTestPage.url.toString())
            .mozClick(TranslationsSelectors.TRANSLATIONS_OPTIONS_BUTTON)
            .mozVerifyElementIsNotSelected(TranslationsSelectors.ALWAYS_OFFER_TO_TRANSLATE)
            .mozClick(TranslationsSelectors.ALWAYS_OFFER_TO_TRANSLATE)
            .mozVerifyElementIsSelected(TranslationsSelectors.ALWAYS_OFFER_TO_TRANSLATE)
            .mozClick(TranslationsSelectors.TRANSLATION_SHEET_BACK_BUTTON)
            .mozSwipeElementUntilAbsent(
                TranslationsSelectors.TRANSLATION_SHEET_HANDLE,
                SwipeDirection.DOWN,
                maxSwipes = 3,
            )
        on.tabDrawer.navigateToPage().closeAllTabs()
        on.home.navigateToPage()
        on.browserPage.navigateToPage(secondTestPage).verifyTranslationSheetWithReload(secondTestPage)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437108
    @Critical
    @Test
    fun verifyTranslationNotNowButtonThroughMainMenuTest() {
        composeRule.activityRule.applySettingsExceptions {
            it.enableOrDisablePageLoadTranslationsPrompt(enableTranslationsPrompt = false)
        }

        val firstTestPage = mockWebServer.firstForeignWebPageAsset

        on.browserPage
            .navigateToPage(firstTestPage.url.toString())
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATE_BUTTON)
        on.browserPage
            .verifyTranslationSheetWithReload(firstTestPage.url.toString())
            .mozClickIfPresent(BrowserPageSelectors.TRANSLATION_SHEET_NOT_NOW_BUTTON)
            .mozWaitUntilAbsent(BrowserPageSelectors.TRANSLATION_SHEET_NOT_NOW_BUTTON, TestAssetHelper.waitingTimeLong)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437996
    @Critical
    @Test
    fun verifyTheOfferToTranslateWhenPossibleOptionTest() {
        val firstUrl = mockWebServer.firstForeignWebPageAsset
        val secondPage = mockWebServer.secondForeignWebPageAsset

        on.browserPage
            .navigateToPage(firstUrl.url.toString())
            .verifyTranslationSheetWithReload(firstUrl.url.toString())
            .mozClick(TranslationsSelectors.TRANSLATIONS_OPTIONS_BUTTON)
            .mozClick(TranslationsSelectors.TRANSLATION_SETTINGS_BUTTON)
        on.settingsTranslations
            .mozVerifyElementIsSelected(SettingsTranslationSelectors.OFFER_TO_TRANSLATE_WHEN_POSSIBLE_TOGGLE)
            .mozClick(SettingsTranslationSelectors.OFFER_TO_TRANSLATE_WHEN_POSSIBLE_TOGGLE)
            .mozVerifyElementIsNotSelected(SettingsTranslationSelectors.OFFER_TO_TRANSLATE_WHEN_POSSIBLE_TOGGLE)
        on.browserPage
            .mozPressBackUntilGone(SettingsTranslationSelectors.TOOLBAR_TITLE)
            .mozVerifyElementIsNotSelected(TranslationsSelectors.ALWAYS_OFFER_TO_TRANSLATE)
            .mozSwipeElementUntilAbsent(BrowserPageSelectors.TRANSLATION_SHEET, SwipeDirection.DOWN, maxSwipes = 3)
        on.browserPage
            .navigateToPage(secondPage.url.toString(), forceNavigation = true)
            .verifyTranslationSheetIsDisplayed(isDisplayed = false)
    }
}
