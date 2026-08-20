/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.disableWifiNetworkConnection
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableDataSaverSystemSetting
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.firstForeignWebPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.secondForeignWebPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.translationsRobot

class TranslationsTest {
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(
            HomeActivityIntentTestRule(
                skipOnboarding = true,
                isPageLoadTranslationsPromptEnabled = true,
            )
        ) {
            it.activity
        }

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2436643
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.TranslationsTest#verifyTheFirstTranslationNotNowButtonFunctionalityTest"
            ],
        bug = 2059397,
        since = "2026-07",
    )
    @SmokeTest
    @Test
    fun verifyTheFirstTranslationNotNowButtonFunctionalityTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(testPage.url) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
            }
            .clickNotNowButton {}
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickTranslateButton {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
            }
            .swipeCloseTranslationsSheet {}
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickTranslateButton {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437107
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.TranslationsTest#verifyMainMenuTranslationButtonFunctionalityTest"],
        bug = 2060217,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyMainMenuTranslationButtonFunctionalityTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(testPage.url) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
            }
            .clickNotNowButton {}
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickTranslateButton {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
            }
            .clickTranslateButton {
                verifyPageContent("Article of the day")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2439667
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.TranslationsTest#verifyTheDownloadLanguagesFunctionalityTest"],
        bug = 2063223,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyTheDownloadLanguagesFunctionalityTest() {
        val firstTestPage = mockWebServer.firstForeignWebPageAsset

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(firstTestPage.url) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
                clickTranslationsOptionsButton()
            }
            .clickTranslationSettingsButton {
                clickDownloadLanguagesButton()
                clickLanguageToDownload("Bosnian")
                verifyDownloadedLanguage("Bosnian")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437991
    @SmokeTest
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.TranslationsTest#verifyTheNeverTranslateOptionTest"],
        bug = 2063228,
        since = "2026-08",
    )
    @Test
    fun verifyTheNeverTranslateOptionTest() {
        val firstTestPage = mockWebServer.firstForeignWebPageAsset
        val secondTestPage = mockWebServer.secondForeignWebPageAsset

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(firstTestPage.url) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
                clickTranslationsOptionsButton()
                clickNeverTranslateLanguageOption(languageToTranslate = "French")
                verifyTheNeverTranslateLanguageDescription()
                verifyTheNeverTranslateLanguageOptionIsChecked(isChecked = true)
            }
            .clickTranslationSettingsButton {
                clickAutomaticTranslationButton()
                verifyNeverAutomaticallyTranslateForLanguage(languageToTranslate = "French")
                clickLanguageFromAutomaticTranslationMenu(languageToTranslate = "French")
                verifyNeverTranslateOptionState(isChecked = true)
            }
            .goBackToAutomaticTranslationSubMenu {}
            .goBackToTranslationSettingsSubMenu {}
            .goBackToTranslationOptionSheet {
                closeTranslationsSheet()
            }
        browserScreen(composeTestRule) {}
            .openTabDrawer(composeTestRule) {}
            .openNewTab {}
            .submitQuery(secondTestPage.url.toString()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
                verifyPageContent(secondTestPage.content)
            }
        translationsRobot(composeTestRule) {
            verifyTranslationSheetIsDisplayed(isDisplayed = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2436642
    @Test
    fun verifyFirstTranslationBottomSheetTranslateFunctionalityTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(testPage.url) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
            }
            .clickTranslateButton {
                verifyPageContent("Article of the day")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437112
    @Test
    fun verifyTheShowOriginalTranslationOptionTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(testPage.url) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
            }
            .clickTranslateButton {
                verifyPageContent("Article of the day")
            }
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickTranslatedButton {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
            }
            .clickShowOriginalButton {
                verifyPageContent(testPage.content)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437111
    @Test
    fun changeTheTranslateToLanguageTest() {
        val testPage = mockWebServer.firstForeignWebPageAsset

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(testPage.url) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
            }
            .clickTranslateButton {
                verifyPageContent("Article of the day")
            }
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickTranslatedButton {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
                clickTranslateToDropdown()
                clickTranslateToLanguage("Estonian")
            }
            .clickTranslateButton(pageWasNotPreviouslyTranslated = false) {
                verifyPageContent("Päeva artikkel")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437992
    @Test
    fun verifyTheAlwaysTranslateOptionTest() {
        val firstTestPage = mockWebServer.firstForeignWebPageAsset
        val secondTestPage = mockWebServer.secondForeignWebPageAsset

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(secondTestPage.url) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
                clickTranslationsOptionsButton()
                verifyAlwaysTranslateOptionIsChecked(isChecked = false)
                clickAlwaysTranslateLanguageOption("French")
                verifyTheAlwaysTranslateLanguageDescription()
            }
            .clickTranslationSettingsButton {
                clickAutomaticTranslationButton()
                verifyAlwaysAutomaticallyTranslateForLanguage(languageToTranslate = "French")
                clickLanguageFromAutomaticTranslationMenu("French")
                verifyAlwaysTranslateOptionState(isChecked = true)
            }
            .goBackToAutomaticTranslationSubMenu {}
            .goBackToTranslationSettingsSubMenu {}
            .goBackToTranslationOptionSheet {
                closeTranslationsSheet()
            }
        browserScreen(composeTestRule) {
            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            verifyPageContent("Word of the day")
        }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(firstTestPage.url) {
                verifyPageContent("Article of the day")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2439960
    @Test
    fun verifyTheSiteDeletionFromTheNeverTranslateListTest() {
        val firstTestPage = mockWebServer.firstForeignWebPageAsset

        browserScreen(composeTestRule) {}
            .openTabDrawer(composeTestRule) {}
            .openNewTab {}
            .submitQuery(firstTestPage.url.toString()) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
                clickTranslationsOptionsButton()
                verifyTheNeverTranslateThisSiteOptionIsChecked(isChecked = false)
                clickNeverTranslateThisSiteOption()
                verifyTheNeverTranslateThisSiteOptionIsChecked(isChecked = true)
            }
            .clickTranslationSettingsButton {
                clickNeverTranslateTheseSitesButton()
                verifyNeverTranslateThisSiteRemoveButton("${firstTestPage.url.scheme}://${firstTestPage.url.authority}")
                clickNeverTranslateThisSiteRemoveButton("${firstTestPage.url.scheme}://${firstTestPage.url.authority}")
                verifyDeleteNeverTranslateThisSiteDialog("${firstTestPage.url.scheme}://${firstTestPage.url.authority}")
                clickCancelDeleteNeverTranslateThisSiteDialog()
                clickNeverTranslateThisSiteRemoveButton("${firstTestPage.url.scheme}://${firstTestPage.url.authority}")
                verifyDeleteNeverTranslateThisSiteDialog("${firstTestPage.url.scheme}://${firstTestPage.url.authority}")
                clickConfirmDeleteNeverTranslateThisSiteDialog()
            }
            .goBackToTranslationSettingsSubMenu {}
            .goBackToTranslationOptionSheet {
                verifyAlwaysOfferToTranslateOptionIsChecked(isChecked = true)
                verifyTheNeverTranslateThisSiteOptionIsChecked(isChecked = false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2440963
    @Ignore("Failing, see https://bugzilla.mozilla.org/show_bug.cgi?id=1965222")
    @Test
    fun downloadLanguageWhileDataSaverModeIsOnTest() {
        val firstTestPage = mockWebServer.firstForeignWebPageAsset

        navigationToolbar(composeTestRule) {}.enterURL(firstTestPage.url) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
                clickTranslationsOptionsButton()
            }
            .clickTranslationSettingsButton {
                disableWifiNetworkConnection()
                enableDataSaverSystemSetting(enabled = true)
                clickDownloadLanguagesButton()
                clickLanguageToDownload("Bosnian")
                verifyDownloadLanguageInSavingModePrompt()
                clickCancelDownloadLanguageInSavingModePromptButton()
                clickLanguageToDownload("Bosnian")
                verifyDownloadLanguageInSavingModePrompt()
                clickDownloadLanguageInSavingModePromptButton()
                verifyDownloadedLanguage("Bosnian")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2437990
    @Test
    fun verifyTheAlwaysOfferToTranslateOptionTest() {
        val firstTestPage = mockWebServer.firstForeignWebPageAsset
        val secondTestPage = mockWebServer.secondForeignWebPageAsset

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(firstTestPage.url) {}
        translationsRobot(composeTestRule) {
                verifyTranslationSheetIsDisplayed(isDisplayed = true)
                clickTranslationsOptionsButton()
                verifyAlwaysOfferToTranslateOptionIsChecked(isChecked = true)
                clickAlwaysOfferToTranslateOption()
                verifyAlwaysOfferToTranslateOptionIsChecked(isChecked = false)
                clickGoBackTranslationSheetButton()
            }
            .swipeCloseTranslationsSheet {
                verifyPageContent(firstTestPage.content)
            }

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(secondTestPage.url) {}
        translationsRobot(composeTestRule) {
            verifyTranslationSheetIsDisplayed(isDisplayed = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2443276
    @SmokeTest
    @Test
    fun verifyTheTranslationIsDisplayedAutomaticallyTest() {
        val firstTestPage = mockWebServer.firstForeignWebPageAsset
        val secondTestPage = "https://mozilla-mobile.github.io/testapp/v2.0/germanForeignWebPage.html"

        navigationToolbar(composeTestRule) {}.enterURLAndEnterToBrowser(firstTestPage.url) {}
        translationsRobot(composeTestRule) {
            verifyTranslationSheetIsDisplayed(isDisplayed = true)
            closeTranslationsSheet()
        }
        browserScreen(composeTestRule) {}
            .openTabDrawer(composeTestRule) {}
            .openNewTab {}
            .submitQuery(secondTestPage) {
                waitForPageToLoad()
            }
        translationsRobot(composeTestRule) {
            verifyTranslationSheetIsDisplayed(isDisplayed = true)
        }
    }
}
