/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import androidx.core.net.toUri
import mozilla.components.browser.errorpages.R as errorpagesR
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.setNetworkEnabled
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.RetryableComposeTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar

/** Tests that verify errors encountered while browsing websites: unsafe pages, connection errors, etc */
class BrowsingErrorPagesTest {
    private val malwareWarning =
        getStringResource(errorpagesR.string.mozac_browser_errorpages_safe_browsing_malware_uri_title)
    private val phishingWarning = getStringResource(errorpagesR.string.mozac_browser_errorpages_safe_phishing_uri_title)
    private val unwantedSoftwareWarning =
        getStringResource(errorpagesR.string.mozac_browser_errorpages_safe_browsing_unwanted_uri_title)
    private val harmfulSiteWarning =
        getStringResource(errorpagesR.string.mozac_browser_errorpages_safe_harmful_uri_title)

    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1) val retryTestRule = RetryTestRule(3)

    @get:Rule(order = 2)
    val retryableComposeTestRule = RetryableComposeTestRule {
        AndroidComposeTestRuleV2(HomeActivityTestRule.withDefaultSettingsOverrides()) { it.activity }
    }

    private val composeTestRule
        get() = retryableComposeTestRule.current

    @get:Rule(order = 3) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326774
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.BrowsingErrorPagesTest#verifyMalwareWebsiteWarningMessageTest"],
        bug = 2045673,
        since = "2026-06",
    )
    @SmokeTest
    @Test
    fun verifyMalwareWebsiteWarningMessageTest() {
        val malwareURl = "http://itisatrap.org/firefox/its-an-attack.html"

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(malwareURl.toUri()) {
                verifyPageContent(malwareWarning)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326773
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.BrowsingErrorPagesTest#verifyPhishingWebsiteWarningMessageTest"],
        bug = 2045673,
        since = "2026-06",
    )
    @SmokeTest
    @Test
    fun verifyPhishingWebsiteWarningMessageTest() {
        val phishingURl = "http://itisatrap.org/firefox/its-a-trap.html"

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(phishingURl.toUri()) {
                verifyPageContent(phishingWarning)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326772
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.BrowsingErrorPagesTest#verifyUnwantedSoftwareWebsiteWarningMessageTest"
            ],
        bug = 2045673,
        since = "2026-06",
    )
    @SmokeTest
    @Test
    fun verifyUnwantedSoftwareWebsiteWarningMessageTest() {
        val unwantedURl = "http://itisatrap.org/firefox/unwanted.html"

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(unwantedURl.toUri()) {
                verifyPageContent(unwantedSoftwareWarning)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/329877
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.BrowsingErrorPagesTest#verifyHarmfulWebsiteWarningMessageTest"],
        bug = 2045673,
        since = "2026-06",
    )
    @SmokeTest
    @Test
    fun verifyHarmfulWebsiteWarningMessageTest() {
        val harmfulURl = "https://itisatrap.org/firefox/harmful.html"

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(harmfulURl.toUri()) {
                verifyPageContent(harmfulSiteWarning)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/329882
    // Failing with network interruption, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1833874
    // This tests the server ERROR_CONNECTION_REFUSED
    @Test
    fun verifyConnectionInterruptedErrorMessageTest() {
        val testUrl = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testUrl.url) {
                waitForPageToLoad()
                verifyPageContent(testUrl.content)
                // Disconnecting the server
                mockWebServer.close()
            }
            .openThreeDotMenu {}
            .clickRefreshButton {
                waitForPageToLoad()
                verifyConnectionErrorMessage()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/329881
    @Test
    fun verifyAddressNotFoundErrorMessageTest() {
        val url = "ww.example.com"

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(url.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
                verifyAddressNotFoundErrorMessage()
                clickPageObject(composeTestRule, itemWithResId("errorTryAgain"))
                verifyAddressNotFoundErrorMessage()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2140588
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.BrowsingErrorPagesTest#verifyNoInternetConnectionErrorMessageTest"],
        bug = 2086589,
        since = "2026-10",
    )
    @Critical
    @Test
    fun verifyNoInternetConnectionErrorMessageTest() {
        val url = "www.example.com"

        setNetworkEnabled(false)

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(url.toUri()) {
                verifyNoInternetConnectionErrorMessage()
            }

        setNetworkEnabled(true)

        browserScreen(composeTestRule) {
            clickPageObject(composeTestRule, itemWithResId("errorTryAgain"))
            waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            verifyPageContent("Example Domain")
        }
    }
}
