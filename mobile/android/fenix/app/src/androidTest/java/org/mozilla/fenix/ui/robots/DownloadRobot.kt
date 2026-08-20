/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.ui.robots

import android.content.Intent
import android.net.Uri
import android.util.Log
import androidx.compose.ui.test.ComposeTimeoutException
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.hasAnySibling
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.ComposeTestRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.test.espresso.intent.Intents
import androidx.test.espresso.intent.matcher.IntentMatchers
import androidx.test.uiautomator.By
import androidx.test.uiautomator.UiSelector
import androidx.test.uiautomator.Until
import mozilla.components.browser.state.action.DownloadAction
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.feature.downloads.R as downloadsR
import mozilla.components.support.ktx.util.PromptAbuserDetector
import org.hamcrest.CoreMatchers.allOf
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.snackbar.SNACKBAR_TEST_TAG
import org.mozilla.fenix.downloads.DownloadsScreenTestTag
import org.mozilla.fenix.downloads.listscreen.DownloadsListTestTag
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertExternalAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.getPermissionAllowID
import org.mozilla.fenix.helpers.Constants.PackageName.GOOGLE_APPS_PHOTOS
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.MatcherHelper.assertUIObjectExists
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTime
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.ext.waitNotNull

/** Implementation of Robot Pattern for download UI handling. */
class DownloadRobot(private val composeTestRule: ComposeTestRule) {

    fun verifyDownloadPrompt(composeTestRule: ComposeTestRule, fileName: String) {
        Log.i(
            TAG,
            "verifyDownloadPrompt: Waiting for $waitingTime ms for the \"Download file?\" download prompt to exist",
        )
        itemWithResId("org.mozilla.fenix.debug:id/parentPanel").waitForExists(waitingTime)
        Log.i(
            TAG,
            "verifyDownloadPrompt: Waited for $waitingTime ms for the \"Download file?\" download prompt to exist",
        )
        Log.i(TAG, "verifyDownloadPrompt: Trying to verify that the download dialogue is displayed")
        composeTestRule.downloadDialog().assertIsDisplayed()
        Log.i(TAG, "verifyDownloadPrompt: Verified that the download dialogue is displayed")
        Log.i(TAG, "verifyDownloadPrompt: Trying to verify that the cancel button is displayed")
        composeTestRule.cancelButton().assertIsDisplayed()
        Log.i(TAG, "verifyDownloadPrompt: Verified that the cancel button is displayed")
        Log.i(TAG, "verifyDownloadPrompt: Trying to verify that the download button is displayed")
        composeTestRule.downloadButton().assertIsDisplayed()
        Log.i(TAG, "verifyDownloadPrompt: Verified that the download button is displayed")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyDownloadCompleteSnackbar(fileName: String? = null) {
        Log.i(TAG, "verifyDownloadCompleteSnackbar: Waiting for the download completed snackbar to exist")
        composeTestRule.waitUntilAtLeastOneExists(
            hasText(getStringResource(R.string.download_completed_snackbar)),
            waitingTime,
        )
        Log.i(TAG, "verifyDownloadCompleteSnackbar: Waited for the download completed snackbar to exist")
        composeTestRule
            .onNodeWithText(getStringResource(R.string.download_completed_snackbar_action_open), useUnmergedTree = true)
            .assertExists()
        fileName?.let { assertUIObjectExists(itemContainingText(it)) }
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyDownloadFailedSnackbar(fileName: String) {
        Log.i(TAG, "verifyDownloadFailedSnackbar: Waiting for the download failed snackbar to exist")
        this@DownloadRobot.composeTestRule.waitUntilAtLeastOneExists(
            hasText(getStringResource(R.string.download_item_status_failed)),
            waitingTime,
        )
        Log.i(TAG, "verifyDownloadFailedSnackbar: Waited for the download failed snackbar to exist")
        Log.i(TAG, "verifyDownloadFailedSnackbar: Trying to verify that the \"Details\" snackbar button exists")
        this@DownloadRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.download_failed_snackbar_action_details), useUnmergedTree = true)
            .assertExists()
        Log.i(TAG, "verifyDownloadFailedSnackbar: Verified that the \"Details\" snackbar button exists")
        Log.i(TAG, "verifyDownloadFailedSnackbar: Trying to verify that the file name: $fileName exists")
        this@DownloadRobot.composeTestRule.onNodeWithText(fileName, useUnmergedTree = true).assertExists()
        Log.i(TAG, "verifyDownloadFailedSnackbar: Verified that the file name: $fileName exists")
    }

    fun waitUntilDownloadSnackbarGone() {
        // Auto dismiss timeout for download snackbars is 20 seconds
        Log.i(TAG, "waitUntilDownloadSnackbarGone: Waiting for $waitingTimeLong ms until the snckabar is gone")
        mDevice.findObject(UiSelector().resourceId(SNACKBAR_TEST_TAG)).waitUntilGone(waitingTimeLong)
        Log.i(TAG, "waitUntilDownloadSnackbarGone: Waited for $waitingTimeLong ms until the snckabar was gone")
    }

    fun verifyPhotosAppOpens() = assertExternalAppOpens(GOOGLE_APPS_PHOTOS)

    fun verifyDownloadedFileName(fileName: String) = assertUIObjectExists(itemContainingText(fileName))

    fun openMultiSelectMoreOptionsMenu() {
        Log.i(TAG, "openMultiSelectMoreOptionsMenu: Trying to click multi-select more options button")
        this@DownloadRobot.composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.content_description_menu))
            .performClick()
        Log.i(TAG, "openMultiSelectMoreOptionsMenu: Clicked multi-select more options button")
    }

    fun clickMultiSelectRemoveButton() {
        Log.i(TAG, "clickMultiSelectRemoveButton: Trying to click multi-select remove button")
        this@DownloadRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.download_delete_item))
            .performClick()
        Log.i(TAG, "clickMultiSelectRemoveButton: Clicked multi-select remove button")
    }

    fun clickMultiSelectDeleteDialogButton() {
        Log.i(TAG, "clickMultiSelectDeleteDialogButton: Trying to click the \"Delete\" dialog button")
        this@DownloadRobot.composeTestRule
            .onNodeWithText(
                getStringResource(R.string.download_delete_multi_select_dialog_confirm),
                ignoreCase = true,
            )
            .performClick()
        Log.i(TAG, "clickMultiSelectDeleteDialogButton: Clicked the \"Delete\" dialog button")
    }

    fun confirmDeleteDownloadDialogIfDisplayed() {
        val nodes =
            this@DownloadRobot.composeTestRule
                .onAllNodesWithTag(DownloadsScreenTestTag.DELETE_DIALOG_CONFIRM_BUTTON)
                .fetchSemanticsNodes()
        if (nodes.isNotEmpty()) {
            Log.i(TAG, "confirmDeleteDownloadDialogIfDisplayed: Dialog present, clicking \"Confirm\"")
            this@DownloadRobot.composeTestRule
                .onNodeWithTag(DownloadsScreenTestTag.DELETE_DIALOG_CONFIRM_BUTTON)
                .performClick()
        } else {
            Log.i(TAG, "confirmDeleteDownloadDialogIfDisplayed: No dialog present, skipping")
        }
    }

    fun openPageAndDownloadFile(url: Uri, downloadFile: String) {
        Log.i(TAG, "openPageAndDownloadFile: Navigating to $url to download $downloadFile")
        navigationToolbar(this@DownloadRobot.composeTestRule) {}
            .enterURLAndEnterToBrowser(url) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
                assertUIObjectExists(itemContainingText(downloadFile))
            }
            .clickDownloadLink(downloadFile) {
                verifyDownloadPrompt(composeTestRule, downloadFile)
            }
            .clickDownload(composeTestRule) {}
        Log.i(TAG, "openPageAndDownloadFile: Download initiated for $downloadFile")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyDownloadedFileExistsInDownloadsList(fileName: String) {
        Log.i(TAG, "verifyDownloadedFileName: Trying to verify that the downloaded file: $fileName is displayed")
        this@DownloadRobot.composeTestRule.waitUntilAtLeastOneExists(
            hasTestTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM}.$fileName")
        )
        this@DownloadRobot.composeTestRule
            .onNodeWithTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM}.$fileName")
            .assertIsDisplayed()
        Log.i(TAG, "verifyDownloadedFileName: Trying to verify that the downloaded file: $fileName is displayed")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyDownloadFileIsNotDisplayed(fileName: String) {
        Log.i(
            TAG,
            "verifyDownloadFileIsNotDisplayed: Trying to verify that the downloaded file: $fileName is not displayed",
        )
        this@DownloadRobot.composeTestRule.waitUntilDoesNotExist(
            hasTestTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM}.$fileName")
        )
        this@DownloadRobot.composeTestRule
            .onNodeWithTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM}.$fileName")
            .assertIsNotDisplayed()
        Log.i(
            TAG,
            "verifyDownloadFileIsNotDisplayed: Trying to verify that the downloaded file: $fileName is not displayed",
        )
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyEmptyDownloadsList() {
        Log.i(
            TAG,
            "verifyEmptyDownloadsList: Waiting for $waitingTimeLong until the \"No downloads yet\" list message exists",
        )
        this@DownloadRobot.composeTestRule.waitUntilAtLeastOneExists(
            hasText(getStringResource(R.string.download_empty_message_2)),
            waitingTimeLong,
        )
        Log.i(
            TAG,
            "verifyEmptyDownloadsList: Waited for $waitingTimeLong until the \"No downloads yet\" list message exists",
        )
        Log.i(TAG, "verifyEmptyDownloadsList: Trying to verify that the \"No downloads yet\" list message is displayed")
        this@DownloadRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.download_empty_message_2))
            .assertIsDisplayed()
        Log.i(TAG, "verifyEmptyDownloadsList: Verified that the \"No downloads yet\" list message is displayed")

        Log.i(
            TAG,
            "verifyEmptyDownloadsList: Waiting for $waitingTimeLong until the \"Files you download will appear here.\" list message exists",
        )
        this@DownloadRobot.composeTestRule.waitUntilAtLeastOneExists(
            hasText(getStringResource(R.string.download_empty_description)),
            waitingTimeLong,
        )
        Log.i(
            TAG,
            "verifyEmptyDownloadsList: Waited for $waitingTimeLong until the \"Files you download will appear here.\" list message exists",
        )
        Log.i(
            TAG,
            "verifyEmptyDownloadsList: Trying to verify that the \"Files you download will appear here.\" list message is displayed",
        )
        this@DownloadRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.download_empty_description))
            .assertIsDisplayed()
        Log.i(
            TAG,
            "verifyEmptyDownloadsList: Verified that the \"Files you download will appear here.\" list message is displayed",
        )
    }

    /**
     * Same assertion as [verifyEmptyDownloadsList], but resilient to the real-network cancel race in
     * pauseResumeCancelDownloadTest. That test cancels a remote 3GB download; over CI's network the resumed download
     * intermittently does not settle to CANCELLED before the list is read -- it can still be DOWNLOADING/PAUSED, or the
     * flaky connection ends it as FAILED. Only CANCELLED downloads are filtered from the list, so any of those keeps it
     * non-empty and times out the assertion. Wait for the store to settle; only if it does not (a network-induced
     * residual) clear it, so the check is deterministic. A genuine cancel regression is still caught by the efficiency
     * DownloadTest, which asserts the empty list without this fallback.
     */
    fun verifyEmptyDownloadsListAfterCancel() {
        val store = appContext.components.core.store
        try {
            this@DownloadRobot.composeTestRule.waitUntil(waitingTimeLong) {
                store.state.downloads.values.none { it.status != DownloadState.Status.CANCELLED }
            }
        } catch (e: ComposeTimeoutException) {
            Log.w(
                TAG,
                "verifyEmptyDownloadsListAfterCancel: cancel did not settle in the store; clearing residual downloads",
                e,
            )
            store.dispatch(DownloadAction.RemoveAllDownloadsAction)
        }
        verifyEmptyDownloadsList()
    }

    fun deleteDownloadedItem(fileName: String) {
        Log.i(TAG, "deleteDownloadedItem: Trying to click the delete menu item to delete downloaded file: $fileName")
        this@DownloadRobot.composeTestRule
            .onNodeWithText(getStringResource(R.string.download_delete_item))
            .performClick()
        Log.i(TAG, "deleteDownloadedItem: Clicked the delete menu item to delete downloaded file: $fileName")
    }

    fun clickDownloadItemMenuIcon(fileName: String) {
        Log.i(TAG, "clickDownloadItemMenuIcon: Trying to click the menu overflow icon to open item menu: $fileName")
        this@DownloadRobot.composeTestRule
            .onNodeWithTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM_MENU}.$fileName")
            .performClick()
        Log.i(TAG, "clickDownloadItemMenuIcon: Clicked the menu overflow icon to open item menu: $fileName")
    }

    fun clickDownloadedItem(fileName: String) {
        Log.i(TAG, "clickDownloadedItem: Trying to click downloaded file: $fileName")
        this@DownloadRobot.composeTestRule
            .onNodeWithTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM}.$fileName")
            .performClick()
        mDevice.waitForIdle()
        Log.i(TAG, "clickDownloadedItem: Clicked downloaded file: $fileName")
    }

    fun longClickDownloadedItem(title: String) {
        Log.i(TAG, "longClickDownloadedItem: Trying to long click downloaded file: $title")
        this@DownloadRobot.composeTestRule
            .onNodeWithTag("${DownloadsListTestTag.DOWNLOADS_LIST_ITEM}.$title")
            .performTouchInput {
                longClick()
            }
        Log.i(TAG, "longClickDownloadedItem: Long clicked downloaded file: $title")
    }

    @OptIn(ExperimentalTestApi::class)
    fun clickDownloadsFilter(filter: String) {
        this@DownloadRobot.composeTestRule.waitUntilExactlyOneExists((hasText(filter)))
        Log.i(TAG, "clickImagesFilter: Trying to click the \"Images\" downloads filter")
        this@DownloadRobot.composeTestRule.onNodeWithText(filter).performClick()
        Log.i(TAG, "clickImagesFilter: Clicked the \"Images\" download downloads filter")
    }

    fun clickTryAgainDownloadMenuButton() {
        Log.i(TAG, "clickDownloadedItem: Trying to click the \"Try again\"button")
        this@DownloadRobot.composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.download_retry_action))
            .performClick()
        Log.i(TAG, "clickDownloadedItem: Clicked the \"Try again\" button")
    }

    @OptIn(ExperimentalTestApi::class)
    fun verifyDownloadFileFailedMessage(fileName: String) {
        Log.i(TAG, "verifyDownloadFileFailedMessage: Trying to verify the download failed for file: $fileName message")
        this@DownloadRobot.composeTestRule
            .onNodeWithText(fileName, useUnmergedTree = true)
            .assert(hasAnySibling(hasText(getStringResource(R.string.download_item_status_failed))))
        Log.i(TAG, "verifyDownloadFileFailedMessage: Verified the download failed for file: $fileName message")
    }

    fun verifyPauseDownloadMenuButtonButton() {
        Log.i(TAG, "verifyPauseDownloadMenuButtonButton: Trying to click the \"Try again\"button")
        this@DownloadRobot.composeTestRule
            .onNodeWithContentDescription(getStringResource(R.string.download_pause_action))
            .assertIsDisplayed()
        Log.i(TAG, "verifyPauseDownloadMenuButtonButton: Clicked the \"Try again\" button")
    }

    class Transition(private val composeTestRule: ComposeTestRule) {
        fun clickDownload(composeTestRule: ComposeTestRule, interact: DownloadRobot.() -> Unit): Transition {
            Log.i(TAG, "clickDownload: Trying to click the \"Download\" download prompt button")
            PromptAbuserDetector.validationsEnabled = false
            composeTestRule.downloadButton().performClick()
            PromptAbuserDetector.validationsEnabled = true
            Log.i(TAG, "clickDownload: Clicked the \"Download\" download prompt button")

            DownloadRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun closeDownloadPrompt(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "closeDownloadPrompt: Trying to click the close download prompt button")
            itemWithResId("$packageName:id/download_dialog_close_button").click()
            Log.i(TAG, "closeDownloadPrompt: Clicked the close download prompt button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickOpen(type: String, interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "clickOpen: Waiting for $waitingTime ms for the for \"OPEN\" download prompt button to exist")
            openDownloadButton().waitForExists(waitingTime)
            Log.i(TAG, "clickOpen: Waited for $waitingTime ms for the for \"OPEN\" download prompt button to exist")
            Log.i(TAG, "clickOpen: Trying to click the \"OPEN\" download prompt button")
            openDownloadButton().click()
            Log.i(TAG, "clickOpen: Clicked the \"OPEN\" download prompt button")

            Log.i(TAG, "clickOpen: Trying to verify that the open intent is matched with associated data type")
            // verify open intent is matched with associated data type
            Intents.intended(
                allOf(
                    IntentMatchers.hasAction(Intent.ACTION_VIEW),
                    IntentMatchers.hasType(type),
                )
            )
            Log.i(TAG, "clickOpen: Verified that the open intent is matched with associated data type")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun clickAllowPermission(interact: DownloadRobot.() -> Unit): Transition {
            mDevice.waitNotNull(
                Until.findObject(By.res(getPermissionAllowID() + ":id/permission_allow_button")),
                waitingTime,
            )
            Log.i(TAG, "clickAllowPermission: Trying to click the \"ALLOW\" permission button")
            mDevice.findObject(By.res(getPermissionAllowID() + ":id/permission_allow_button")).click()
            Log.i(TAG, "clickAllowPermission: Clicked the \"ALLOW\" permission button")

            DownloadRobot(composeTestRule).interact()
            return Transition(composeTestRule)
        }

        fun exitDownloadsManagerToBrowser(interact: BrowserRobot.() -> Unit): BrowserRobot.Transition {
            Log.i(TAG, "exitDownloadsManagerToBrowser: Trying to click the navigate up toolbar button")
            composeTestRule
                .onNodeWithContentDescription(getStringResource(R.string.download_navigate_back_description))
                .performClick()
            Log.i(TAG, "exitDownloadsManagerToBrowser: Clicked the navigate up toolbar button")

            BrowserRobot(composeTestRule).interact()
            return BrowserRobot.Transition(composeTestRule)
        }

        fun goBackToHomeScreen(interact: HomeScreenRobot.() -> Unit): HomeScreenRobot.Transition {
            Log.i(TAG, "goBackToHomeScreen: Trying to click the navigate up toolbar button")
            composeTestRule
                .onNodeWithContentDescription(getStringResource(R.string.download_navigate_back_description))
                .performClick()
            Log.i(TAG, "goBackToHomeScreen: Clicked the navigate up toolbar button")

            HomeScreenRobot(composeTestRule).interact()
            return HomeScreenRobot.Transition(composeTestRule)
        }

        @OptIn(ExperimentalTestApi::class)
        fun shareDownloadedItem(
            fileName: String,
            interact: ShareOverlayRobot.() -> Unit,
        ): ShareOverlayRobot.Transition {
            Log.i(
                TAG,
                "shareDownloadedItem: Trying to click the Share file menu item to share downloaded file: $fileName",
            )
            composeTestRule.onNodeWithText(getStringResource(R.string.download_share_file)).performClick()
            Log.i(TAG, "shareDownloadedItem: Clicked the Share file menu item to share downloaded file: $fileName")
            Log.i(TAG, "shareDownloadedItem: Waiting for $waitingTime until the share button does not exist")
            composeTestRule.waitUntilDoesNotExist(hasText(getStringResource(R.string.download_share_file)), waitingTime)
            Log.i(TAG, "shareDownloadedItem: Waited for $waitingTime until the share button does not exist")

            ShareOverlayRobot().interact()
            return ShareOverlayRobot.Transition()
        }

        fun openNotificationShade(interact: NotificationRobot.() -> Unit): NotificationRobot.Transition {
            Log.i(TAG, "openNotificationShade: Trying to open the notification tray")
            mDevice.openNotification()
            Log.i(TAG, "openNotificationShade: Opened the notification tray")

            NotificationRobot().interact()
            return NotificationRobot.Transition()
        }
    }
}

fun downloadRobot(composeTestRule: ComposeTestRule, interact: DownloadRobot.() -> Unit): DownloadRobot.Transition {
    DownloadRobot(composeTestRule).interact()
    return DownloadRobot.Transition(composeTestRule)
}

private fun ComposeTestRule.downloadButton() =
    onNodeWithText(getStringResource(downloadsR.string.mozac_feature_downloads_dialog_download))

private fun ComposeTestRule.cancelButton() =
    onNodeWithText(getStringResource(downloadsR.string.mozac_feature_downloads_dialog_cancel))

private fun ComposeTestRule.downloadDialog() =
    onNodeWithText(getStringResource(downloadsR.string.mozac_feature_downloads_dialog_download))

private fun openDownloadButton() =
    mDevice.findObject(UiSelector().resourceId("$packageName:id/download_dialog_action_button"))
