/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertExternalAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.clickSystemHomeScreenShortcutAddButton
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_DOCS
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.notificationShade

class PDFViewerTest : TestSetup() {
    private val downloadTestPage =
        "https://storage.googleapis.com/mobile_test_assets/test_app/downloads.html"
    private val pdfFileName = "washington.pdf"
    private val pdfFileURL = "storage.googleapis.com/mobile_test_assets/public/washington.pdf"
    private val pdfFileContent = "Washington Crossing the Delaware"

    @get:Rule
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2048140
    @SmokeTest
    @Test
    fun verifyPDFFileIsOpenedInTheSameTabTest() {
        val genericURL =
            getGenericAsset(mockWebServer, 3)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemContainingText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
            verifyPageContent("Washington Crossing the Delaware")
            verifyTabCounter("1")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2145448
    // Download PDF file using the download toolbar button
    @Test
    fun verifyPDFViewerDownloadButtonTest() {
        val genericURL = getGenericAsset(mockWebServer, 3)
        val downloadFile = "pdfForm.pdf"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemWithText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
        }.clickDownloadPDFButton {
            verifyDownloadedFileName(downloadFile)
        }.clickOpen("application/pdf") {
            assertExternalAppOpens(GOOGLE_DOCS)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2283305
    @Test
    fun pdfFindInPageTest() {
        val genericURL = getGenericAsset(mockWebServer, 3)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(MatcherHelper.itemWithText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
        }.openThreeDotMenu {
            verifyThreeDotMenuExists()
            verifyFindInPageButton()
        }.openFindInPage {
            verifyFindInPageNextButton()
            verifyFindInPagePrevButton()
            verifyFindInPageCloseButton()
            enterFindInPageQuery("l")
            verifyFindInPageResult("1/2")
            clickFindInPageNextButton()
            verifyFindInPageResult("2/2")
            clickFindInPagePrevButton()
            verifyFindInPageResult("1/2")
        }.closeFindInPageWithCloseButton {
            verifyFindInPageBar(false)
        }.openThreeDotMenu {
        }.openFindInPage {
            enterFindInPageQuery("p")
            verifyFindInPageResult("1/1")
        }.closeFindInPageWithBackButton {
            verifyFindInPageBar(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2284297
    @Test
    fun addPDFToHomeScreenTest() {
        navigationToolbar {
        }.enterURLAndEnterToBrowser(downloadTestPage.toUri()) {
            clickPageObject(MatcherHelper.itemContainingText(pdfFileName))
            verifyUrl(pdfFileURL)
            verifyPageContent(pdfFileContent)
        }.openThreeDotMenu {
            expandMenu()
        }.openAddToHomeScreen {
            verifyShortcutTextFieldTitle(pdfFileName)
            clickAddShortcutButton()
            clickSystemHomeScreenShortcutAddButton()
        }.openHomeScreenShortcut(pdfFileName) {
            verifyUrl(pdfFileURL)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2797677
    // Download PDF file using the download toolbar button
    @Test
    fun verifyDownloadedPDFIsOpenedInFirefoxTest() {
        val genericURL = getGenericAsset(mockWebServer, 3)
        val downloadFile = "pdfForm.pdf"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemWithText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
            verifyTabCounter("1")
        }.openThreeDotMenu {
            expandMenu()
        }.openAddToHomeScreen {
            verifyShortcutTextFieldTitle("Untitled document")
            addShortcutName("pdfForm")
            clickAddShortcutButton()
            clickSystemHomeScreenShortcutAddButton()
        }.openHomeScreenShortcut("pdfForm") {
            verifyTabCounter("1")
        }.clickDownloadPDFButton {
            verifyDownloadedFileName(downloadFile)
        }.clickOpen("application/pdf") {
            selectToAlwaysOpenDownloadedFileWithApp(appName = appName)
            verifyUrl("content://media/external_primary/downloads/")
            verifyTabCounter("2")
        }

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemWithText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
        }.clickDownloadPDFButton {
            verifyDownloadedFileName(downloadFile)
        }

        mDevice.openNotification()

        notificationShade {
            expandMultipleDownloadNotification("pdfForm(1).pdf")
            clickNotification("pdfForm(1).pdf")
        }
        browserScreen {
            verifyUrl("content://media/external_primary/downloads/")
            verifyTabCounter("3")
        }.openThreeDotMenu {
        }.openDownloadsManager {
            clickDownloadedItem(composeTestRule, "pdfForm.pdf")
        }
        browserScreen {
            verifyTabCounter("4")
            verifyUrl("content://media/external_primary/downloads/")
        }
    }
}
