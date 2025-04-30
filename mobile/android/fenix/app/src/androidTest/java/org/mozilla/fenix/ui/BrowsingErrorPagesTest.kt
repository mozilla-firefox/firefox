/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.core.net.toUri
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.setNetworkEnabled
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 * Tests that verify errors encountered while browsing websites: unsafe pages, connection errors, etc
 */
class BrowsingErrorPagesTest : TestSetup() {
    private val malwareWarning = getStringResource(R.string.mozac_browser_errorpages_safe_browsing_malware_uri_title)
    private val phishingWarning = getStringResource(R.string.mozac_browser_errorpages_safe_phishing_uri_title)
    private val unwantedSoftwareWarning =
        getStringResource(R.string.mozac_browser_errorpages_safe_browsing_unwanted_uri_title)
    private val harmfulSiteWarning = getStringResource(R.string.mozac_browser_errorpages_safe_harmful_uri_title)

    @get: Rule
    val mActivityTestRule = HomeActivityTestRule.withDefaultSettingsOverrides()

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Rule
    @JvmField
    val retryTestRule = RetryTestRule(3)

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326774
    @SmokeTest
    @Test
    fun verifyMalwareWebsiteWarningMessageTest() {
        val malwareURl = "http://itisatrap.org/firefox/its-an-attack.html"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(malwareURl.toUri()) {
            verifyPageContent(malwareWarning)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326773
    @SmokeTest
    @Test
    fun verifyPhishingWebsiteWarningMessageTest() {
        val phishingURl = "http://itisatrap.org/firefox/its-a-trap.html"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(phishingURl.toUri()) {
            verifyPageContent(phishingWarning)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326772
    @SmokeTest
    @Test
    fun verifyUnwantedSoftwareWebsiteWarningMessageTest() {
        val unwantedURl = "http://itisatrap.org/firefox/unwanted.html"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(unwantedURl.toUri()) {
            verifyPageContent(unwantedSoftwareWarning)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/329877
    @SmokeTest
    @Test
    fun verifyHarmfulWebsiteWarningMessageTest() {
        val harmfulURl = "https://itisatrap.org/firefox/harmful.html"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(harmfulURl.toUri()) {
            verifyPageContent(harmfulSiteWarning)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/329882
    // Failing with network interruption, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1833874
    // This tests the server ERROR_CONNECTION_REFUSED
    @Test
    fun verifyConnectionInterruptedErrorMessageTest() {
        val testUrl = getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(testUrl.url) {
            waitForPageToLoad()
            verifyPageContent(testUrl.content)
            // Disconnecting the server
            mockWebServer.shutdown()
        }.openThreeDotMenu {
        }.refreshPage {
            waitForPageToLoad()
            verifyConnectionErrorMessage()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/329881
    @Test
    fun verifyAddressNotFoundErrorMessageTest() {
        val url = "ww.example.com"

        navigationToolbar {
        }.enterURLAndEnterToBrowser(url.toUri()) {
            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            verifyAddressNotFoundErrorMessage()
            clickPageObject(itemWithResId("errorTryAgain"))
            verifyAddressNotFoundErrorMessage()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2140588
    @Test
    fun verifyNoInternetConnectionErrorMessageTest() {
        val url = "www.example.com"

        setNetworkEnabled(false)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(url.toUri()) {
            verifyNoInternetConnectionErrorMessage()
        }

        setNetworkEnabled(true)

        browserScreen {
            clickPageObject(itemWithResId("errorTryAgain"))
            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            verifyPageContent("Example Domain")
        }
    }
}
