/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.os.Build
import android.os.Build.VERSION.SDK_INT
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import androidx.test.espresso.intent.rule.IntentsRule
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertExternalAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.deleteDownloadedFileOnStorage
import org.mozilla.fenix.helpers.AppAndSystemHelper.setNetworkEnabled
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_APPS_PHOTOS
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_DOCS
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.downloadRobot
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.notificationShade

/**
 *  Tests for verifying basic functionality of download
 *
 *  - Initiates a download
 *  - Verifies download prompt
 *  - Verifies download notification and actions
 *  - Verifies managing downloads inside the Downloads listing.
 **/
class DownloadTest : TestSetup() {
    /* Remote test page managed by Mozilla Mobile QA team at https://github.com/mozilla-mobile/testapp */
    private val downloadTestPage =
        "https://storage.googleapis.com/mobile_test_assets/test_app/downloads.html"
    private var downloadFile: String = ""

    @get:Rule
    val activityTestRule =
        AndroidComposeTestRule(
            HomeActivityTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule
    val intentsTestRule = IntentsRule()

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/243844
    @Test
    fun verifyTheDownloadPromptsTest() {
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "web_icon.png")
            verifyDownloadCompleteNotificationPopup()
        }.clickOpen("image/png") {}
        downloadRobot {
            verifyPhotosAppOpens()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2299405
    @Test
    fun verifyTheDownloadFailedNotificationsTest() {
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "1GB.zip")
            setNetworkEnabled(enabled = false)
            verifyDownloadFailedPrompt("1GB.zip")
            setNetworkEnabled(enabled = true)
            clickTryAgainButton()
        }
        mDevice.openNotification()
        notificationShade {
            verifySystemNotificationDoesNotExist("Download failed")
            verifySystemNotificationExists("1GB.zip")
        }.closeNotificationTray {}
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2298616
    @Test
    fun verifyDownloadCompleteNotificationTest() {
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "web_icon.png")
            verifyDownloadCompleteNotificationPopup()
        }
        mDevice.openNotification()
        notificationShade {
            verifySystemNotificationExists("Download completed")
            clickNotification("Download completed")
            assertExternalAppOpens(GOOGLE_APPS_PHOTOS)
            mDevice.pressBack()
            mDevice.openNotification()
            verifySystemNotificationExists("Download completed")
            swipeDownloadNotification(
                direction = "Left",
                shouldDismissNotification = true,
                canExpandNotification = false,
                notificationItem = "web_icon.png",
            )
            verifySystemNotificationDoesNotExist("Firefox Fenix")
        }.closeNotificationTray {}
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/451563
    @SmokeTest
    @Test
    fun pauseResumeCancelDownloadTest() {
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
        }
        mDevice.openNotification()
        notificationShade {
            expandNotificationMessage("3GB.zip")
            clickDownloadNotificationControlButton("PAUSE")
            verifySystemNotificationExists("Download paused")
            clickDownloadNotificationControlButton("RESUME")
            clickDownloadNotificationControlButton("CANCEL")
            verifySystemNotificationDoesNotExist("3GB.zip")
            mDevice.pressBack()
        }
        browserScreen {
        }.openThreeDotMenu {
        }.openDownloadsManager() {
            verifyEmptyDownloadsList(activityTestRule)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2301474
    @Test
    fun openDownloadedFileFromDownloadsMenuTest() {
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "web_icon.png")
            verifyDownloadCompleteNotificationPopup()
        }
        browserScreen {
        }.openThreeDotMenu {
        }.openDownloadsManager() {
            verifyDownloadedFileExistsInDownloadsList(activityTestRule, "web_icon.png")
            clickDownloadedItem(activityTestRule, "web_icon.png")
            verifyPhotosAppOpens()
            mDevice.pressBack()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1114970
    @Test
    fun deleteDownloadedFileTest() {
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "smallZip.zip")
        }
        browserScreen {
        }.openThreeDotMenu {
        }.openDownloadsManager {
            verifyDownloadedFileExistsInDownloadsList(activityTestRule, "smallZip.zip")
            clickDownloadItemMenuIcon(activityTestRule, "smallZip.zip")
            deleteDownloadedItem(activityTestRule, "smallZip.zip")
            clickSnackbarButton(activityTestRule, "UNDO")
            verifyDownloadedFileExistsInDownloadsList(activityTestRule, "smallZip.zip")
            clickDownloadItemMenuIcon(activityTestRule, "smallZip.zip")
            deleteDownloadedItem(activityTestRule, "smallZip.zip")
            verifyEmptyDownloadsList(activityTestRule)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2302662
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1960537")
    @Test
    fun deleteMultipleDownloadedFilesTest() {
        val firstDownloadedFile = "smallZip.zip"
        val secondDownloadedFile = "textfile.txt"

        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = firstDownloadedFile)
            verifyDownloadedFileName(firstDownloadedFile)
        }.closeDownloadPrompt {
        }.clickDownloadLink(secondDownloadedFile) {
            verifyDownloadPrompt(secondDownloadedFile)
        }.clickDownload {
            verifyDownloadedFileName(secondDownloadedFile)
        }
        browserScreen {
        }.openThreeDotMenu {
        }.openDownloadsManager() {
            verifyDownloadedFileExistsInDownloadsList(activityTestRule, firstDownloadedFile)
            verifyDownloadedFileExistsInDownloadsList(activityTestRule, secondDownloadedFile)
            longClickDownloadedItem(activityTestRule, firstDownloadedFile)
            clickDownloadedItem(activityTestRule, secondDownloadedFile)
            openMultiSelectMoreOptionsMenu(activityTestRule)
            clickMultiSelectRemoveButton(activityTestRule)
            clickSnackbarButton(activityTestRule, "UNDO")
            verifyDownloadedFileExistsInDownloadsList(activityTestRule, firstDownloadedFile)
            verifyDownloadedFileExistsInDownloadsList(activityTestRule, secondDownloadedFile)
            longClickDownloadedItem(activityTestRule, firstDownloadedFile)
            clickDownloadedItem(activityTestRule, secondDownloadedFile)
            openMultiSelectMoreOptionsMenu(activityTestRule)
            clickMultiSelectRemoveButton(activityTestRule)
            verifyEmptyDownloadsList(activityTestRule)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2301537
    @Test
    fun fileDeletedFromStorageIsDeletedEverywhereTest() {
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "smallZip.zip")
            verifyDownloadCompleteNotificationPopup()
        }
        browserScreen {
        }.openThreeDotMenu {
        }.openDownloadsManager() {
            verifyDownloadedFileExistsInDownloadsList(activityTestRule, "smallZip.zip")
            deleteDownloadedFileOnStorage("smallZip.zip")
        }.exitDownloadsManagerToBrowser(activityTestRule) {
        }.openThreeDotMenu {
        }.openDownloadsManager() {
            verifyEmptyDownloadsList(activityTestRule)
        }.exitDownloadsManagerToBrowser(activityTestRule) {
        }

        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "smallZip.zip")
            verifyDownloadCompleteNotificationPopup()
        }
        browserScreen {
        }.openThreeDotMenu {
        }.openDownloadsManager() {
            verifyDownloadedFileExistsInDownloadsList(activityTestRule, "smallZip.zip")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2466505
    @Test
    fun systemNotificationCantBeDismissedWhileInProgressTest() {
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
        }
        browserScreen {
        }.openNotificationShade {
            swipeDownloadNotification(direction = "Left", shouldDismissNotification = false, notificationItem = "3GB.zip")
            expandNotificationMessage("3GB.zip")
            clickDownloadNotificationControlButton("PAUSE")
            notificationShade {
            }.closeNotificationTray {
            }
            browserScreen {
            }.openNotificationShade {
                swipeDownloadNotification(direction = "Right", shouldDismissNotification = true, notificationItem = "3GB.zip")
                verifySystemNotificationDoesNotExist("3GB.zip")
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2299297
    @Test
    fun notificationCanBeDismissedIfDownloadIsInterruptedTest() {
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "1GB.zip")
            setNetworkEnabled(enabled = false)
            verifyDownloadFailedPrompt("1GB.zip")
        }

        browserScreen {
        }.openNotificationShade {
            verifySystemNotificationExists("Download failed")
            swipeDownloadNotification(
                direction = "Left",
                shouldDismissNotification = true,
                canExpandNotification = true,
                notificationItem = "1GB.zip",
            )
            verifySystemNotificationDoesNotExist("Firefox Fenix")
        }.closeNotificationTray {}

        downloadRobot {
        }.closeDownloadPrompt {
            verifyDownloadPromptIsDismissed()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1632384
    @Test
    fun warningWhenClosingPrivateTabsWhileDownloadingTest() {
        homeScreen {
        }.togglePrivateBrowsingMode()
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
        }
        browserScreen {
        }.openTabDrawer(activityTestRule) {
            closeTab()
        }
        browserScreen {
            verifyCancelPrivateDownloadsPrompt("1")
            clickStayInPrivateBrowsingPromptButton()
        }.openNotificationShade {
            if (SDK_INT == Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                // On API 34 we first need to expand the system notification before verifying that the app name is displayed
                expandNotificationMessage("3GB.zip")
                verifySystemNotificationExists("Firefox Fenix")
            } else {
                verifySystemNotificationExists("Firefox Fenix")
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2302663
    @Test
    fun cancelActivePrivateBrowsingDownloadsTest() {
        homeScreen {
        }.togglePrivateBrowsingMode()
        downloadRobot {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
        }
        browserScreen {
        }.openTabDrawer(activityTestRule) {
            closeTab()
        }
        browserScreen {
            verifyCancelPrivateDownloadsPrompt("1")
            clickCancelPrivateDownloadsPromptButton()
        }.openNotificationShade {
            verifySystemNotificationDoesNotExist("Firefox Fenix")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2048448
    // Save edited PDF file from the share overlay
    @SmokeTest
    @Test
    fun saveAsPdfFunctionalityTest() {
        val genericURL =
            TestAssetHelper.getGenericAsset(mockWebServer, 3)
        downloadFile = "pdfForm.pdf"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemWithText("PDF form file"))
            waitForPageToLoad()
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
            fillPdfForm("Firefox")
        }.openThreeDotMenu {
        }.clickShareButton {
        }.clickSaveAsPDF {
            verifyDownloadPrompt("pdfForm.pdf")
        }.clickDownload {
        }.clickOpen("application/pdf") {
            assertExternalAppOpens(GOOGLE_DOCS)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/244125
    @Test
    fun restartDownloadFromAppNotificationAfterConnectionIsInterruptedTest() {
        downloadFile = "3GB.zip"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(downloadTestPage.toUri()) {
            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
        }.clickDownloadLink(downloadFile) {
            verifyDownloadPrompt(downloadFile)
            setNetworkEnabled(false)
        }.clickDownload {
            verifyDownloadFailedPrompt(downloadFile)
            setNetworkEnabled(true)
            clickTryAgainButton()
        }
        browserScreen {
        }.openNotificationShade {
            expandNotificationMessage("3GB.zip")
            clickDownloadNotificationControlButton("CANCEL")
        }
    }
}
