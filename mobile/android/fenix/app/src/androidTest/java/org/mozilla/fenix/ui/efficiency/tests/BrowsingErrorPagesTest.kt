/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import mozilla.components.browser.errorpages.R as errorpagesR
import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.setNetworkEnabled
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors

class BrowsingErrorPagesTest : BaseTest() {
    private val malwareWarning =
        getStringResource(errorpagesR.string.mozac_browser_errorpages_safe_browsing_malware_uri_title)
    private val phishingWarning = getStringResource(errorpagesR.string.mozac_browser_errorpages_safe_phishing_uri_title)
    private val unwantedSoftwareWarning =
        getStringResource(errorpagesR.string.mozac_browser_errorpages_safe_browsing_unwanted_uri_title)
    private val harmfulSiteWarning =
        getStringResource(errorpagesR.string.mozac_browser_errorpages_safe_harmful_uri_title)

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326774
    @SmokeTest
    @Test
    fun verifyMalwareWebsiteWarningMessageTest() {
        val malwareURl = "http://itisatrap.org/firefox/its-an-attack.html"

        on.browserPage.navigateToPage(malwareURl).verifyPageContent(malwareWarning)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326773
    @SmokeTest
    @Test
    fun verifyPhishingWebsiteWarningMessageTest() {
        val phishingURl = "http://itisatrap.org/firefox/its-a-trap.html"

        on.browserPage.navigateToPage(phishingURl).verifyPageContent(phishingWarning)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326772
    @SmokeTest
    @Test
    fun verifyUnwantedSoftwareWebsiteWarningMessageTest() {
        val unwantedURl = "http://itisatrap.org/firefox/unwanted.html"

        on.browserPage.navigateToPage(unwantedURl).verifyPageContent(unwantedSoftwareWarning)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/329877
    @SmokeTest
    @Test
    fun verifyHarmfulWebsiteWarningMessageTest() {
        val harmfulURl = "https://itisatrap.org/firefox/harmful.html"

        on.browserPage.navigateToPage(harmfulURl).verifyPageContent(harmfulSiteWarning)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2140588
    @Critical
    @Test
    fun verifyNoInternetConnectionErrorMessageTest() {
        val url = "www.example.com"

        // The device is brought online before every test by EfficiencyTestRule.ensureNetworkConnected(),
        // so this starts from a known-online state. Re-enable the network even if the offline
        // assertions throw, so a failure can't leave the device offline for the tests that follow --
        // nothing resets it in teardown.
        setNetworkEnabled(false)
        try {
            on.browserPage
                .navigateToPage(url)
                .mozVerify(BrowserPageSelectors.NO_INTERNET_CONNECTION_ERROR_TITLE, timeout = waitingTimeLong)
                .mozVerify(BrowserPageSelectors.ERROR_TRY_AGAIN_BUTTON)
        } finally {
            setNetworkEnabled(true)
        }

        on.browserPage.clickPageObjectUntilContent(
            selector = BrowserPageSelectors.ERROR_TRY_AGAIN_BUTTON,
            url = url,
            expectedContent = "Example Domain",
        )
    }
}
