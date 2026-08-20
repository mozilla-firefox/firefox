/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import mozilla.components.support.ktx.util.PromptAbuserDetector
import org.junit.After
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.TestAssetHelper.htmlControlsFormAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.pageObjects.SystemSettingsPage
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors

class UploadPermissionsTest : BaseTest() {

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    // Instantiated locally rather than taken from PageContext, as SearchTest does: this page is reached by
    // a system dialog appearing over the app, not by any navigable click path.
    private val systemSettings
        get() = SystemSettingsPage(composeRule)

    // Mirrors the legacy class, which disables this around the whole test. Tapping the file input opens a
    // system picker, and the prompt-abuse detector can drop that as an unsolicited prompt.
    @Before
    fun disablePromptAbuserDetector() {
        PromptAbuserDetector.validationsEnabled = false
    }

    @After
    fun restorePromptAbuserDetector() {
        PromptAbuserDetector.validationsEnabled = true
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2121537
    // Converted from legacy UploadPermissionsTest.fileUploadPermissionTest
    @SmokeTest
    @Test
    fun fileUploadPermissionTest() {
        val testPage = mockWebServer.htmlControlsFormAsset

        on.browserPage.navigateToPage(testPage.url.toString())
        on.browserPage.mozClick(BrowserPageSelectors.UPLOAD_FILE_WEB_INPUT)

        // Legacy called grantSystemPermission() once, which handles a single dialog. On this platform the
        // tap fans out into a chain (audio, then music-and-audio), so one grant leaves the run parked on
        // the second dialog. The count is deliberately not asserted: runtime grants are system state that
        // outlives the app profile, so the dialogs appear on a fresh device and are absent on every run
        // after -- asserting on them would make the test pass once and then fail forever.
        systemSettings.grantAllPendingSystemPermissions()

        // Legacy asserted the picker with assertExternalAppOpens, which catches the AssertionFailedError
        // from intended() and only logs it, so on any device that has the picker installed -- every
        // supported image -- that assertion could not fail. It never noticed that a bare
        // <input type="file"> does not open a picker directly at all: Android offers a chooser first
        // (Camera, Camcorder, and one content picker), so the legacy test has been ending on that chooser,
        // or on an undismissed permission dialog, while reporting success.
        //
        // Which content picker the chooser offers is resolved from the device rather than hard-coded: on
        // API 37 it is "Files" (documentsui), on API 34 -- what CI and Firebase run -- it is "Media" (the
        // photo picker), and there is no "Files" row at all. Asserting the resolved package still cannot
        // pass vacuously: the row has to be present and its app has to come to the foreground.
        val picker = systemSettings.openResolvedContentPicker()

        // openResolvedContentPicker has already established that a real picker is in front; this re-asserts it
        // through the harness verb so the run report carries it, and force-stops the picker afterwards -- the
        // same cleanup legacy did in its finally.
        on.browserPage.mozVerifyNativeAppOpens(picker)
        // Legacy also branched to "com.android.documentsui" below API 29. That branch is not carried: the
        // resolution above makes the package a property of the device rather than of the API level.
    }
}
