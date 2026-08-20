/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Ignore
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest

class SettingsPrivateBrowsingTest : BaseTest() {

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @Ignore("Covered by verifyNavigationReachability[1: SettingsPrivateBrowsingPage (TBD) — Navigation Reachability]")
    @Test
    fun verifyTheSettingsPrivateBrowsingTest() {
        on.settingsPrivateBrowsing.navigateToPage()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/652556
    @SmokeTest
    @Test
    fun allowScreenshotsInPrivateBrowsingTest() {
        val defaultWebPage = mockWebServer.getGenericAsset(1)

        on.home.navigateToPage()
        on.home.switchToPrivateBrowsingMode()
        on.browserPage.navigateToPage(defaultWebPage.url.toString())
        on.browserPage.verifyScreenshotsAllowed(false)

        on.settingsPrivateBrowsing.navigateToPage()
        on.settingsPrivateBrowsing.toggleAllowScreenshotsInPrivateBrowsing()

        on.browserPage.navigateToPage()
        on.browserPage.verifyScreenshotsAllowed(true)
    }
}
