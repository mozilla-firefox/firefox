/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import mozilla.components.support.ktx.util.PromptAbuserDetector
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.Constants
import org.mozilla.fenix.helpers.TestAssetHelper.downloadPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.loremIpsumAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.DownloadsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.NotificationSelectors
import org.mozilla.fenix.ui.efficiency.selectors.ShareOverlaySelectors

class DownloadTest : BaseTest() {

    // Required for the download prompt: without it the Download button's click is treated as prompt
    // abuse and silently dropped -- the button resolves and the click reports success, but the dialog
    // stays open and no download starts. The legacy DownloadRobot.clickDownload toggles this around the
    // click for the same reason.
    @Before
    fun disablePromptAbuserDetector() {
        PromptAbuserDetector.validationsEnabled = false
    }

    @After
    fun restorePromptAbuserDetector() {
        PromptAbuserDetector.validationsEnabled = true
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2048448
    // Converted from legacy DownloadTest.saveAsPdfFunctionalityTest
    @SmokeTest
    @Test
    fun saveAsPdfFunctionalityTest() {
        val genericURL = mockWebServer.loremIpsumAsset

        on.browserPage.navigateToPage(genericURL.url.toString())
        // Save as PDF is reached through the share sheet here, not the main menu's More submenu --
        // that second route is covered by MainMenuTest.verifyTheSaveAsPDFSubMenuOptionTest.
        // SAVE_AS_PDF_LABEL, not SAVE_AS_PDF_BUTTON: the latter is UIAUTOMATOR_WITH_RES_ID with the
        // value "Save as PDF", so it resolves to the res-id "<pkg>:id/Save as PDF" and can never match.
        // It has no other callers. SAVE_AS_PDF_LABEL matches on text, like the legacy robot did.
        on.shareOverlay.navigateToPage().mozClick(ShareOverlaySelectors.SAVE_AS_PDF_LABEL)
        on.downloads
            .mozVerifyElementsByGroup(DownloadsSelectors.Group.DOWNLOAD_DIALOG)
            .mozClick(DownloadsSelectors.DOWNLOAD_DIALOG_CONFIRM_BUTTON)
            .mozVerify(DownloadsSelectors.DOWNLOAD_COMPLETE_SNACKBAR, timeout = 15_000)
            .mozClick(DownloadsSelectors.DOWNLOAD_SNACK_BAR_OPEN_BUTTON)
            .mozVerifyFileOpensInExternalApp(Constants.PackageName.GOOGLE_DOCS)
        // Parity gap: legacy verifyDownloadPrompt also asserted the prompt named the file ("Lorem") and
        // that the Cancel button was displayed. The downloadDialog group covers the dialog title and the
        // Download button only; there is no filename or cancel-button selector yet.
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/451563
    @SmokeTest
    @Test
    fun pauseResumeCancelDownloadTest() {
        // Unlike the other download tests, this one keeps the remote testapp page and its 3GB.zip
        // instead of the local mockWebServer assets. Pause/resume/cancel need a download that stays in
        // progress, and every local asset is a few KB and completes instantly. The download is only
        // started and then cancelled -- never completed -- so the remote page's slow completion, which
        // pushed the other download tests onto mockWebServer, is not a factor here.
        val downloadTestPage = "https://storage.googleapis.com/mobile_test_assets/test_app/downloads.html"
        val downloadFile = "3GB.zip"

        on.browserPage
            .navigateToPage(downloadTestPage)
            .clickDownloadLink(downloadFile, downloadTestPage)
            .verifyDownloadPrompt()
            .clickDownloadPromptConfirmButton()
            .verifyDownloadInProgressSnackbar()
            .waitUntilDownloadSnackbarGone()

        // clickNotificationActionButton(PAUSE) tolerates the Pause button already being gone: over the
        // real network the 3GB download stalls and Fenix auto-pauses it before the shade opens, so the
        // download can already be paused when we arrive. Either way the paused state is asserted next,
        // then the download is resumed and cancelled.
        on.notification
            .openNotificationTray()
            .expandNotification(downloadFile)
            .clickNotificationActionButton(NotificationSelectors.DOWNLOAD_NOTIFICATION_PAUSE_BUTTON)
            .verifyNotificationExists(NotificationSelectors.DOWNLOAD_PAUSED_NOTIFICATION)
            .clickNotificationActionButton(NotificationSelectors.DOWNLOAD_NOTIFICATION_RESUME_BUTTON)
            .clickNotificationActionButton(NotificationSelectors.DOWNLOAD_NOTIFICATION_CANCEL_BUTTON)
            .verifyNotificationDoesNotExist(NotificationSelectors.SYSTEM_NOTIFICATION(downloadFile))
            .closeNotificationTray()

        on.downloads.navigateToPage().mozVerifyElementsByGroup(DownloadsSelectors.Group.EMPTY_DOWNLOADS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2298616
    @Critical
    @Test
    fun verifyDownloadCompleteNotificationTest() {
        val downloadTestPage = mockWebServer.downloadPageAsset.url.toString()
        val downloadFile = "png_image.png"

        on.browserPage
            .navigateToPage(downloadTestPage)
            .clickDownloadLink(downloadFile, downloadTestPage)
            .verifyDownloadPrompt()
            .clickDownloadPromptConfirmButton()
            .verifyDownloadCompleteSnackbar(downloadFile)

        on.notification
            .openNotificationTray()
            .verifyNotificationExists(NotificationSelectors.DOWNLOAD_COMPLETED_NOTIFICATION)
            .clickNotification(NotificationSelectors.DOWNLOAD_COMPLETED_NOTIFICATION)
            // UIAutomator check, not Espresso: the system fires the notification's intent, so Firefox is
            // backgrounded and Espresso's intended() would throw NoActivityResumedException.
            .mozVerifyNativeAppOpens(Constants.PackageName.GOOGLE_APPS_PHOTOS)
        on.notification
            .openNotificationTray()
            .verifyNotificationDoesNotExist(NotificationSelectors.DOWNLOAD_COMPLETED_NOTIFICATION)
            .closeNotificationTray()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1114970
    @Critical
    @Test
    fun deleteDownloadedFileTest() {
        // Local mockWebServer asset, like the legacy test: zip_small.zip completes instantly, so no
        // remote network or in-progress snackbar is involved -- the download-list assertions are the check.
        val downloadTestPage = mockWebServer.downloadPageAsset.url.toString()
        val downloadFile = "zip_small.zip"

        on.browserPage
            .navigateToPage(downloadTestPage)
            .clickDownloadLink(downloadFile, downloadTestPage)
            .verifyDownloadPrompt()
            .clickDownloadPromptConfirmButton()

        // Delete once and Undo, confirming the file returns, then delete again and confirm the list empties.
        on.downloads
            .navigateToPage()
            .verifyDownloadedFileExistsInDownloadsList(downloadFile)
            .deleteDownloadedFile(downloadFile)
            .clickUndoDeleteSnackbarButton()
            .verifyDownloadedFileExistsInDownloadsList(downloadFile)
            .deleteDownloadedFile(downloadFile)
            .mozVerifyElementsByGroup(DownloadsSelectors.Group.EMPTY_DOWNLOADS)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3978402
    @Critical
    @Test
    fun verifyAskWhenIDeleteFilesFirstChoiceTest() {
        val downloadTestPage = mockWebServer.downloadPageAsset.url.toString()
        val downloadFile = "zip_small.zip"

        on.browserPage
            .navigateToPage(downloadTestPage)
            .clickDownloadLink(downloadFile, downloadTestPage)
            .verifyDownloadPrompt()
            .clickDownloadPromptConfirmButton()
        on.downloads
            .navigateToPage()
            .verifyDownloadedFileExistsInDownloadsList(downloadFile)
            .mozClick(DownloadsSelectors.MORE_OPTIONS_BUTTON)
            .mozClick(DownloadsSelectors.MORE_OPTIONS_DELETE_BUTTON)
            .mozVerifyElementsByGroup(DownloadsSelectors.Group.DELETE_DOWNLOADED_ITEM_DIALOG)
            .mozClick(DownloadsSelectors.DELETE_FILE_DIALOG_CANCEL_BUTTON)
            .mozVerifyElementsByGroupAbsent(DownloadsSelectors.Group.DELETE_DOWNLOADED_ITEM_DIALOG)
            .mozClick(DownloadsSelectors.MORE_OPTIONS_BUTTON)
            .mozClick(DownloadsSelectors.MORE_OPTIONS_DELETE_BUTTON)
            .mozVerifyElementsByGroup(DownloadsSelectors.Group.DELETE_DOWNLOADED_ITEM_DIALOG)
            .mozClick(DownloadsSelectors.DELETE_FILE_DIALOG_CONFIRM_BUTTON)
            .mozVerify(DownloadsSelectors.DELETE_DOWNLOAD_SNACKBAR_MESSAGE(downloadFile))
            .mozVerifyElementsByGroup(DownloadsSelectors.Group.EMPTY_DOWNLOADS)
        on.downloads.verifyDownloadedFileIsDeletedFromStorage(downloadFile)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3978403
    @Critical
    @Test
    fun verifyAskWhenIDeleteFilesSecondChoiceTest() {
        val downloadTestPage = mockWebServer.downloadPageAsset.url.toString()
        val downloadFile = "zip_small.zip"

        on.browserPage
            .navigateToPage(downloadTestPage)
            .clickDownloadLink(downloadFile, downloadTestPage)
            .verifyDownloadPrompt()
            .clickDownloadPromptConfirmButton()
        on.downloads
            .navigateToPage()
            .verifyDownloadedFileExistsInDownloadsList(downloadFile)
            .mozClick(DownloadsSelectors.MORE_OPTIONS_BUTTON)
            .mozClick(DownloadsSelectors.MORE_OPTIONS_DELETE_BUTTON)
            .mozVerifyElementsByGroup(DownloadsSelectors.Group.DELETE_DOWNLOADED_ITEM_DIALOG)
            .mozClick(DownloadsSelectors.DELETE_FILE_DIALOG_ONLY_REMOVE_FROM_DOWNLOAD_HISTORY_OPTION)
            .mozClick(DownloadsSelectors.DELETE_FILE_DIALOG_CONFIRM_BUTTON)
            .mozClick(DownloadsSelectors.UNDO_DELETE_SNACKBAR_BUTTON)
        on.downloads
            .verifyDownloadedFileExistsInDownloadsList(downloadFile)
            .mozClick(DownloadsSelectors.MORE_OPTIONS_BUTTON)
            .mozClick(DownloadsSelectors.MORE_OPTIONS_DELETE_BUTTON)
            .mozVerifyElementsByGroup(DownloadsSelectors.Group.DELETE_DOWNLOADED_ITEM_DIALOG)
            .mozClick(DownloadsSelectors.DELETE_FILE_DIALOG_ONLY_REMOVE_FROM_DOWNLOAD_HISTORY_OPTION)
            .mozClick(DownloadsSelectors.DELETE_FILE_DIALOG_CONFIRM_BUTTON)
            .mozVerify(DownloadsSelectors.DELETE_DOWNLOAD_SNACKBAR_MESSAGE(downloadFile))
            .mozVerifyElementsByGroup(DownloadsSelectors.Group.EMPTY_DOWNLOADS)
        on.downloads.verifyDownloadedFileStillExistsOnStorage(downloadFile)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2298617
    @Critical
    @Test
    fun cancelTheDownloadFromPromptTest() {
        val downloadTestPage = mockWebServer.downloadPageAsset.url.toString()
        val downloadFile = "zip_small.zip"

        on.browserPage
            .navigateToPage(downloadTestPage)
            .clickDownloadLink(downloadFile, downloadTestPage)
            .verifyDownloadPrompt()
            .mozClick(DownloadsSelectors.DOWNLOAD_DIALOG_CANCEL_BUTTON)
        on.downloads.navigateToPage().mozVerifyElementsByGroup(DownloadsSelectors.Group.EMPTY_DOWNLOADS)
    }
}
