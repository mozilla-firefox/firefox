/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import androidx.core.net.toUri
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.Config
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertExternalAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.clickSystemHomeScreenShortcutAddButton
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithCondition
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_DOCS
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.notificationShade

class PDFViewerTest {
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    private val downloadTestPage = "https://storage.googleapis.com/mobile_test_assets/test_app/downloads.html"
    private val pdfFileName = "washington.pdf"
    private val pdfFileURL = "storage.googleapis.com/mobile_test_assets/public/washington.pdf"
    private val pdfFileContent = "Washington Crossing the Delaware"

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(HomeActivityIntentTestRule.withDefaultSettingsOverrides()) { it.activity }

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2048140
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.PDFViewerTest#verifyPDFFileIsOpenedInTheSameTabTest"],
        bug = 2060844,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyPDFFileIsOpenedInTheSameTabTest() {
        val genericURL = mockWebServer.getGenericAsset(3)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {
                clickPageObject(composeTestRule, itemContainingText("PDF form file"))
                clickPageObject(composeTestRule, itemContainingText("Stay in"))
                verifyPageContent("Washington Crossing the Delaware")
                verifyTabCounter("1")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2145448
    // Download PDF file using the download toolbar button
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.PDFViewerTest#verifyPDFViewerDownloadButtonTest"],
        bug = 2083697,
        since = "2026-10",
    )
    @Critical
    @Test
    fun verifyPDFViewerDownloadButtonTest() {
        val genericURL = mockWebServer.getGenericAsset(3)
        val downloadFile = "pdfForm.pdf"

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {
                clickPageObject(composeTestRule, itemWithText("PDF form file"))
                clickPageObject(composeTestRule, itemContainingText("Stay in"))
            }
            .clickDownloadPDFButton {
                verifyDownloadCompleteSnackbar(fileName = downloadFile)
                clickSnackbarButton(composeTestRule = composeTestRule, "OPEN")
                assertExternalAppOpens(GOOGLE_DOCS)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2283305
    @Test
    fun pdfFindInPageTest() {
        val genericURL = mockWebServer.getGenericAsset(3)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {
                clickPageObject(composeTestRule, itemWithText("PDF form file"))
                clickPageObject(composeTestRule, itemContainingText("Stay in"))
            }
            .openThreeDotMenu {}
            .clickFindInPageButton {
                verifyFindInPageNextButton()
                verifyFindInPagePrevButton()
                verifyFindInPageCloseButton()
                enterFindInPageQuery("l")
                verifyFindInPageResult("1/2")
                clickFindInPageNextButton()
                verifyFindInPageResult("2/2")
                clickFindInPagePrevButton()
                verifyFindInPageResult("1/2")
            }
            .closeFindInPageWithCloseButton(composeTestRule) {
                verifyFindInPageBar(false)
            }
            .openThreeDotMenu {}
            .clickFindInPageButton {
                enterFindInPageQuery("p")
                verifyFindInPageResult("1/1")
            }
            .closeFindInPageWithBackButton(composeTestRule) {
                verifyFindInPageBar(false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2284297
    @Test
    fun addPDFToHomeScreenTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(downloadTestPage.toUri()) {
                clickPageObject(composeTestRule, itemContainingText(pdfFileName))
                verifyUrl(pdfFileURL)
                verifyPageContent(pdfFileContent)
            }
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickAddToHomeScreenButton {
                verifyShortcutTextFieldTitle(pdfFileName)
                clickAddShortcutButton()
                clickSystemHomeScreenShortcutAddButton()
            }
            .openHomeScreenShortcut(pdfFileName) {
                verifyUrl(pdfFileURL)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2797677
    // Download PDF file using the download toolbar button
    @Test
    fun verifyDownloadedPDFIsOpenedInFirefoxTest() {
        val genericURL = mockWebServer.getGenericAsset(3)
        val downloadFile = "pdfForm.pdf"
        val secondDownloadNotificationTitle = "pdfForm(1).pdf"

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {
                clickPageObject(composeTestRule, itemWithText("PDF form file"))
                clickPageObject(composeTestRule, itemContainingText("Stay in"))
                verifyTabCounter("1")
            }
            .openThreeDotMenu {
                clickTheMoreButton()
            }
            .clickAddToHomeScreenButton {
                verifyShortcutTextFieldTitle("Untitled document")
                addShortcutName("pdfForm")
                clickAddShortcutButton()
                clickSystemHomeScreenShortcutAddButton()
            }
            .openHomeScreenShortcut("pdfForm") {
                verifyTabCounter("1")
            }
            .clickDownloadPDFButton {
                verifyDownloadCompleteSnackbar(fileName = downloadFile)
                clickSnackbarButton(composeTestRule = composeTestRule, "OPEN")
            }
        browserScreen(composeTestRule) {
            selectToAlwaysOpenDownloadedFileWithApp(appName = appName)
            verifyUrl("content://media/external_primary/downloads/")
            verifyTabCounter("2")
        }

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {
                clickPageObject(composeTestRule, itemWithText("PDF form file"))
                clickPageObject(composeTestRule, itemContainingText("Stay in"))
            }
            .clickDownloadPDFButton(confirmDownloadPrompt = false) {
                runWithCondition(Config.channel.isNightlyOrDebug) {
                    clickDownloadAgain()
                }
            }

        mDevice.openNotification()

        notificationShade {
            expandMultipleDownloadNotification(secondDownloadNotificationTitle)
            clickNotification(secondDownloadNotificationTitle)
        }
        browserScreen(composeTestRule) {
                verifyUrl("content://media/external_primary/downloads/")
                verifyTabCounter("3")
            }
            .openThreeDotMenu {}
            .clickDownloadsButton {
                clickDownloadedItem(downloadFile)
            }
        browserScreen(composeTestRule) {
            verifyTabCounter("4")
            verifyUrl("content://media/external_primary/downloads/")
        }
    }
}
