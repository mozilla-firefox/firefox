/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestAssetHelper.firstForeignWebPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.secondForeignWebPageAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.MainMenuSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SearchBarSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsTranslationSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TabDrawerSelectors
import org.mozilla.fenix.ui.efficiency.selectors.TranslationsSelectors

class TranslationsTest : BaseTest(isPageLoadTranslationsPromptEnabled = true) {

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

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
            .mozVerifyElementsByGroup("notTranslatedPageTranslationSheet")
            .mozSwipeElementUntilAbsent(BrowserPageSelectors.TRANSLATION_SHEET, SwipeDirection.DOWN, maxSwipes = 3)
        on.browserPage
            .openMainMenu()
            .mozClick(MainMenuSelectors.MORE_BUTTON)
            .mozClick(MainMenuSelectors.TRANSLATE_BUTTON)
            .mozVerifyElementsByGroup("notTranslatedPageTranslationSheet")
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
            .mozVerifyElementsByGroup("notTranslatedPageTranslationSheet")
            .mozClickIfPresent(BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON)
            .mozWaitUntilAbsent(
                BrowserPageSelectors.TRANSLATION_SHEET_TRANSLATE_BUTTON,
                TestAssetHelper.waitingTimeLong,
            )
        on.browserPage.verifyPageContent("Article of the day")
    }

    private companion object {
        // The legacy test hardcodes Bosnian; it must be an offered translations model.
        const val LANGUAGE_TO_DOWNLOAD = "Bosnian"

        // The mock page is fr-FR, so the engine reports French as the detected source language.
        const val LANGUAGE_TO_NEVER_TRANSLATE = "French"
    }
}
