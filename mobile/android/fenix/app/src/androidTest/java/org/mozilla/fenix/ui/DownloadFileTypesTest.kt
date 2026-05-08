/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.core.net.toUri
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.Parameterized
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.downloadRobot
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2

/**
 *  Test for verifying downloading a list of different file types:
 *  - Initiates a download
 *  - Verifies download prompt
 *  - Verifies downloading of varying file types and the appearance inside the Downloads listing.
 **/
@RunWith(Parameterized::class)
class DownloadFileTypesTest(fileName: String) {
    // Remote test page managed by Mozilla Mobile QA team at https://github.com/mozilla-mobile/testapp
    @get:Rule(order = 0)
    val fenixTestRule: FenixTestRule = FenixTestRule()

    private val downloadTestPage = "https://storage.googleapis.com/mobile_test_assets/test_app/downloads.html"
    private var downloadFile: String = fileName

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(
            HomeActivityIntentTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    companion object {
        // Creating test data. The test will take each file name as a parameter and run it individually.
        @JvmStatic
        @Parameterized.Parameters
        fun downloadList() = listOf(
            "smallZip.zip",
            "MyDocument.docx",
            "audioSample.mp3",
            "textfile.txt",
            "web_icon.png",
            "videoSample.webm",
            "CSVfile.csv",
            "XMLfile.xml",
            "tAJwqaWjJsXS8AhzSninBMCfIZbHBGgcc001lx5DIdDwIcfEgQ6vE5Gb5VgAled17DFZ2A7ZDOHA0NpQPHXXFt.svg",
        )
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/251028
    @SmokeTest
    @Test
    fun allFilesAppearInDownloadsMenuTest() {
        downloadRobot(composeTestRule) {
            openPageAndDownloadFile(url = downloadTestPage.toUri(), downloadFile = downloadFile)
            verifyDownloadCompleteSnackbar(fileName = downloadFile)
        }

        browserScreen(composeTestRule) {
        }.openThreeDotMenu {
        }.clickDownloadsButton {
            verifyDownloadedFileExistsInDownloadsList(downloadFile)
        }.exitDownloadsManagerToBrowser {
        }
    }
    }
