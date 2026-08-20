/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.os.Build
import android.os.Build.VERSION.SDK_INT
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import androidx.core.net.toUri
import androidx.test.espresso.intent.rule.IntentsRule
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertExternalAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertNativeAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.deleteDownloadedFileOnStorage
import org.mozilla.fenix.helpers.AppAndSystemHelper.setNetworkEnabled
import org.mozilla.fenix.helpers.Constants.PackageName.GMAIL_APP
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_APPS_PHOTOS
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_DOCS
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.downloadPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.loremIpsumAsset
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.downloadRobot
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.notificationShade

/**
 * Tests for verifying basic functionality of download
 *
 * - Initiates a download
 * - Verifies download prompt
 * - Verifies download notification and actions
 * - Verifies managing downloads inside the Downloads listing.
 */
class DownloadTest {
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    // Remote test page used by large-file tests (3GB.zip); managed by Mozilla Mobile QA team
    // at https://github.com/mozilla-mobile/testapp
    private val downloadTestPage = "https://storage.googleapis.com/mobile_test_assets/test_app/downloads.html"
    private var downloadFile: String = ""

    @get:Rule(order = 1)
    val composeTestRule = AndroidComposeTestRuleV2(HomeActivityTestRule.withDefaultSettingsOverrides()) { it.activity }

    @get:Rule val intentsTestRule = IntentsRule()

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/243844
    @Test
    fun verifyTheDownloadPromptsTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = mockWebServer.downloadPageAsset.url, downloadFile = "png_image.png")
            verifyDownloadCompleteSnackbar(fileName = "png_image.png")
            clickSnackbarButton(composeTestRule = this@DownloadTest.composeTestRule, "OPEN")
            verifyPhotosAppOpens()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2299405
    @Ignore("Failing, see https://bugzilla.mozilla.org/show_bug.cgi?id=1987355")
    @Test
    fun verifyTheDownloadFailedNotificationsTest() {
        downloadRobot(composeTestRule) {
                openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "1GB.zip")
                setNetworkEnabled(enabled = false)
                verifyDownloadFailedSnackbar(fileName = "1GB.zip")
                clickSnackbarButton(composeTestRule, "DETAILS")
            }
            .openNotificationShade {
                verifySystemNotificationExists("Download failed")
            }
            .closeNotificationTray(composeTestRule) {}
        downloadRobot(composeTestRule) {
            verifyDownloadFileFailedMessage("1GB.zip")
            setNetworkEnabled(enabled = true)
            clickTryAgainDownloadMenuButton()
            verifyPauseDownloadMenuButtonButton()
        }
        downloadRobot(composeTestRule) {}
            .openNotificationShade {
                expandNotificationMessage("1GB.zip")
                clickDownloadNotificationControlButton("CANCEL")
                verifySystemNotificationDoesNotExist("1GB.zip")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2298616
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=2006672"])
    @Ignore("https://bugzilla.mozilla.org/show_bug.cgi?id=1999369")
    @Test
    fun verifyDownloadCompleteNotificationTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = mockWebServer.downloadPageAsset.url, downloadFile = "png_image.png")
            verifyDownloadCompleteSnackbar(fileName = "png_image.png")
            waitUntilDownloadSnackbarGone()
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
                    composeTestRule,
                    direction = "Left",
                    shouldDismissNotification = true,
                    canExpandNotification = false,
                    notificationItem = "png_image.png",
                )
                verifySystemNotificationDoesNotExist("Firefox Fenix")
            }
            .closeNotificationTray(composeTestRule) {}
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/451563
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.DownloadTest#pauseResumeCancelDownloadTest"],
        bug = 2063195,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun pauseResumeCancelDownloadTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
            verifySnackBarText("Download in progress")
            waitUntilDownloadSnackbarGone()
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
        browserScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickDownloadsButton {
                verifyEmptyDownloadsListAfterCancel()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2301474
    @Test
    fun openDownloadedFileFromDownloadsMenuTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = mockWebServer.downloadPageAsset.url, downloadFile = "png_image.png")
            verifyDownloadCompleteSnackbar(fileName = "png_image.png")
        }
        browserScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickDownloadsButton {
                verifyDownloadedFileExistsInDownloadsList("png_image.png")
                clickDownloadedItem("png_image.png")
                verifyPhotosAppOpens()
                mDevice.pressBack()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1114970
    @Test
    @SkipLeaks(reasons = ["https://bugzilla.mozilla.org/show_bug.cgi?id=2004099"])
    fun deleteDownloadedFileTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = mockWebServer.downloadPageAsset.url, downloadFile = "zip_small.zip")
        }
        browserScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickDownloadsButton {
                verifyDownloadedFileExistsInDownloadsList("zip_small.zip")
                clickDownloadItemMenuIcon("zip_small.zip")
                deleteDownloadedItem("zip_small.zip")
                confirmDeleteDownloadDialogIfDisplayed()
                clickSnackbarButton(composeTestRule, "Undo")
                verifyDownloadedFileExistsInDownloadsList("zip_small.zip")
                clickDownloadItemMenuIcon("zip_small.zip")
                deleteDownloadedItem("zip_small.zip")
                confirmDeleteDownloadDialogIfDisplayed()
                verifyEmptyDownloadsList()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2302662
    @Test
    fun deleteMultipleDownloadedFilesTest() {
        val firstDownloadedFile = "zip_small.zip"
        val secondDownloadedFile = "txt_file.txt"

        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = mockWebServer.downloadPageAsset.url, downloadFile = firstDownloadedFile)
            verifyDownloadCompleteSnackbar(fileName = firstDownloadedFile)
        }
        browserScreen(composeTestRule) {}
            .clickDownloadLink(secondDownloadedFile) {}
            .clickDownload(composeTestRule) {
                verifyDownloadCompleteSnackbar(fileName = secondDownloadedFile)
            }
        browserScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickDownloadsButton {
                verifyDownloadedFileExistsInDownloadsList(firstDownloadedFile)
                verifyDownloadedFileExistsInDownloadsList(secondDownloadedFile)
                longClickDownloadedItem(firstDownloadedFile)
                clickDownloadedItem(secondDownloadedFile)
                openMultiSelectMoreOptionsMenu()
                clickMultiSelectRemoveButton()
                confirmDeleteDownloadDialogIfDisplayed()
                clickSnackbarButton(composeTestRule, "Undo")
                verifyDownloadedFileExistsInDownloadsList(firstDownloadedFile)
                verifyDownloadedFileExistsInDownloadsList(secondDownloadedFile)
                longClickDownloadedItem(firstDownloadedFile)
                clickDownloadedItem(secondDownloadedFile)
                openMultiSelectMoreOptionsMenu()
                clickMultiSelectRemoveButton()
                confirmDeleteDownloadDialogIfDisplayed()
                verifyEmptyDownloadsList()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2301537
    @Test
    fun fileDeletedFromStorageIsDeletedEverywhereTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = mockWebServer.downloadPageAsset.url, downloadFile = "zip_small.zip")
            verifyDownloadCompleteSnackbar(fileName = "zip_small.zip")
        }
        browserScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickDownloadsButton {
                verifyDownloadedFileExistsInDownloadsList("zip_small.zip")
                deleteDownloadedFileOnStorage("zip_small.zip")
            }
            .exitDownloadsManagerToBrowser {}
            .openThreeDotMenu {}
            .clickDownloadsButton {
                verifyEmptyDownloadsList()
            }
            .exitDownloadsManagerToBrowser {}

        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = mockWebServer.downloadPageAsset.url, downloadFile = "zip_small.zip")
            verifyDownloadCompleteSnackbar(fileName = "zip_small.zip")
        }
        browserScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickDownloadsButton {
                verifyDownloadedFileExistsInDownloadsList("zip_small.zip")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2466505
    @Test
    fun systemNotificationCantBeDismissedWhileInProgressTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
        }
        browserScreen(composeTestRule) {}
            .openNotificationShade {
                swipeDownloadNotification(
                    composeTestRule,
                    direction = "Left",
                    shouldDismissNotification = false,
                    notificationItem = "3GB.zip",
                )
                expandNotificationMessage("3GB.zip")
                clickDownloadNotificationControlButton("PAUSE")
                notificationShade {}.closeNotificationTray(composeTestRule) {}
                browserScreen(composeTestRule) {}
                    .openNotificationShade {
                        swipeDownloadNotification(
                            composeTestRule,
                            direction = "Right",
                            shouldDismissNotification = true,
                            notificationItem = "3GB.zip",
                        )
                        verifySystemNotificationDoesNotExist("3GB.zip")
                    }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2299297
    @Ignore("Failing, see https://bugzilla.mozilla.org/show_bug.cgi?id=1987355")
    @Test
    fun notificationCanBeDismissedIfDownloadIsInterruptedTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "1GB.zip")
            setNetworkEnabled(enabled = false)
            verifyDownloadFailedSnackbar(fileName = "1GB.zip")
        }
        browserScreen(composeTestRule) {}
            .openNotificationShade {
                verifySystemNotificationExists("Download failed")
                swipeDownloadNotification(
                    composeTestRule,
                    direction = "Left",
                    shouldDismissNotification = true,
                    canExpandNotification = true,
                    notificationItem = "1GB.zip",
                )
                verifySystemNotificationDoesNotExist("Firefox Fenix")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1632384
    @Test
    fun warningWhenClosingPrivateTabsWhileDownloadingTest() {
        homeScreen(composeTestRule) {}.togglePrivateBrowsingMode()
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
        }
        browserScreen(composeTestRule) {}
            .openTabDrawer(composeTestRule) {
                closeTab()
            }
        browserScreen(composeTestRule) {
                verifyCancelPrivateDownloadsPrompt("1")
                clickStayInPrivateBrowsingPromptButton()
            }
            .openNotificationShade {
                if (SDK_INT == Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                    // On API 34 we first need to expand the system notification before verifying that the app name is
                    // displayed
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
        homeScreen(composeTestRule) {}.togglePrivateBrowsingMode()
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
        }
        browserScreen(composeTestRule) {}
            .openTabDrawer(composeTestRule) {
                closeTab()
            }
        browserScreen(composeTestRule) {
                verifyCancelPrivateDownloadsPrompt("1")
                clickCancelPrivateDownloadsPromptButton()
            }
            .openNotificationShade {
                verifySystemNotificationDoesNotExist("Firefox Fenix")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2048448
    // Save edited PDF file from the share overlay
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.DownloadTest#saveAsPdfFunctionalityTest"],
        bug = 2060299,
        since = "2026-08",
        notes =
            "Legacy verifyDownloadPrompt also asserted the prompt named the file and that Cancel " +
                "was displayed; the share-sheet prompt is a different dialog from the main-menu route and " +
                "needs its own selectors, so those two checks did not carry over.",
    )
    @SmokeTest
    @Test
    fun saveAsPdfFunctionalityTest() {
        val genericURL = mockWebServer.loremIpsumAsset
        downloadFile = "Lorem"

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericURL.url) {}
            .openThreeDotMenu {}
            .clickShareButton {}
            .clickSaveAsPDF(composeTestRule) {
                verifyDownloadPrompt(composeTestRule, downloadFile)
            }
            .clickDownload(composeTestRule) {
                verifyDownloadCompleteSnackbar(fileName = downloadFile)
                clickSnackbarButton(composeTestRule = composeTestRule, "OPEN")
                assertExternalAppOpens(GOOGLE_DOCS)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/244125
    @Ignore("Failing, see https://bugzilla.mozilla.org/show_bug.cgi?id=1983769")
    @Test
    fun restartDownloadFromAppNotificationAfterConnectionIsInterruptedTest() {
        downloadFile = "3GB.zip"

        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
            setNetworkEnabled(false)
            verifyDownloadFailedSnackbar(fileName = "3GB.zip")
            setNetworkEnabled(true)
            clickSnackbarButton(composeTestRule, "DETAILS")
            verifyDownloadFileFailedMessage("3GB.zip")
            setNetworkEnabled(enabled = true)
            clickTryAgainDownloadMenuButton()
            verifyPauseDownloadMenuButtonButton()
        }
        downloadRobot(composeTestRule) {}
            .openNotificationShade {
                expandNotificationMessage("3GB.zip")
                clickDownloadNotificationControlButton("CANCEL")
                verifySystemNotificationDoesNotExist("3GB.zip")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2981843
    @Test
    fun verifyTheDownloadFiltersTest() {
        val firstDownloadedFile = "zip_small.zip"
        val secondDownloadedFile = "png_image.png"

        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = mockWebServer.downloadPageAsset.url, downloadFile = firstDownloadedFile)
            verifyDownloadCompleteSnackbar(fileName = firstDownloadedFile)
        }
        browserScreen(composeTestRule) {}
            .clickDownloadLink(secondDownloadedFile) {}
            .clickDownload(composeTestRule) {
                verifyDownloadCompleteSnackbar(fileName = secondDownloadedFile)
            }
        browserScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickDownloadsButton {
                clickDownloadsFilter("Images")
                verifyDownloadedFileExistsInDownloadsList(secondDownloadedFile)
                verifyDownloadFileIsNotDisplayed(firstDownloadedFile)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2987000
    @Test
    fun shareDownloadedFileTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = mockWebServer.downloadPageAsset.url, downloadFile = "png_image.png")
            verifyDownloadCompleteSnackbar(fileName = "png_image.png")
        }
        browserScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickDownloadsButton {
                verifyDownloadedFileExistsInDownloadsList("png_image.png")
                clickDownloadItemMenuIcon("png_image.png")
            }
            .shareDownloadedItem("png_image.png") {
                expandAndroidShareLayout("Gmail")
                clickSharingApp("Gmail", GMAIL_APP)
                assertNativeAppOpens(composeTestRule, GMAIL_APP)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/457111
    @Ignore("Failing, see https://bugzilla.mozilla.org/show_bug.cgi?id=1983769")
    @Test
    fun downloadRestartAfterConnectionIsReestablishedTest() {
        downloadFile = "3GB.zip"

        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = "3GB.zip")
            downloadRobot(composeTestRule) {}
                .openNotificationShade {
                    expandNotificationMessage("3GB.zip")
                    clickDownloadNotificationControlButton("PAUSE")
                    setNetworkEnabled(false)
                    verifySystemNotificationExists("Download paused")
                    clickDownloadNotificationControlButton("RESUME")
                    verifySystemNotificationExists("Download failed")
                    setNetworkEnabled(enabled = true)
                    clickDownloadNotificationControlButton("TRY AGAIN")
                    expandNotificationMessage("3GB.zip")
                    clickDownloadNotificationControlButton("CANCEL")
                    verifySystemNotificationDoesNotExist("3GB.zip")
                }
        }
    }
}
