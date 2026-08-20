/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import android.Manifest
import android.content.Context
import android.hardware.camera2.CameraManager
import androidx.core.net.toUri
import androidx.test.rule.GrantPermissionRule
import mozilla.components.support.ktx.util.PromptAbuserDetector
import org.junit.After
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.MockLocationUpdatesRule
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.SitePermissionsSelectors

/**
 * The page under test is the test-eng team's live permissions page, not a mock-server asset: these prompts are raised
 * by real getUserMedia/Notification/geolocation calls, which no local fixture reproduces.
 */
class SitePermissionsTest : BaseTest() {

    @get:Rule
    val grantPermissionRule: GrantPermissionRule =
        GrantPermissionRule.grant(
            Manifest.permission.RECORD_AUDIO,
            Manifest.permission.CAMERA,
            Manifest.permission.ACCESS_COARSE_LOCATION,
            Manifest.permission.ACCESS_FINE_LOCATION,
        )

    @get:Rule val mockLocationUpdatesRule = MockLocationUpdatesRule()

    private val cameraManager = appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager

    // Without this, the second prompt in a test is suppressed as abuse and the assertion reads as a missing prompt.
    @Before
    fun disablePromptAbuseDetection() {
        PromptAbuserDetector.validationsEnabled = false
    }

    @After
    fun reenablePromptAbuseDetection() {
        PromptAbuserDetector.validationsEnabled = true
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334074
    @SmokeTest
    @Test
    fun blockNotificationsPermissionTest() {
        on.browserPage.navigateToPage(TEST_PAGE)
        on.browserPage.mozClick(
            SitePermissionsSelectors.PAGE_PERMISSION_BUTTON("notify", "Open notifications dialogue")
        )
        on.browserPage
            .mozVerify(SitePermissionsSelectors.NOTIFICATIONS_PERMISSION_PROMPT(TEST_PAGE_HOST))
            .mozVerify(SitePermissionsSelectors.PROMPT_DENY_BUTTON_LABELLED("Never"))
            .mozVerify(SitePermissionsSelectors.PROMPT_ALLOW_BUTTON_LABELLED("Always"))
            .mozClick(SitePermissionsSelectors.PAGE_PERMISSION_DIALOG_DENY_BUTTON)
        on.browserPage.verifyPageContent("Notifications not allowed")

        on.browserPage.navigateToPage(TEST_PAGE, forceNavigation = true)
        on.browserPage.mozClick(
            SitePermissionsSelectors.PAGE_PERMISSION_BUTTON("notify", "Open notifications dialogue")
        )
        // "Never" is remembered, so the prompt must not come back -- this absence IS the test.
        on.browserPage.mozVerifyElementAbsent(SitePermissionsSelectors.NOTIFICATIONS_PERMISSION_PROMPT(TEST_PAGE_HOST))
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/251385
    @SmokeTest
    @Test
    fun allowLocationPermissionsTest() {
        mockLocationUpdatesRule.setMockLocation()

        on.browserPage.navigateToPage(TEST_PAGE)
        on.browserPage.mozClick(SitePermissionsSelectors.PAGE_PERMISSION_BUTTON("location", "Get Location"))
        on.browserPage
            .mozVerify(SitePermissionsSelectors.LOCATION_PERMISSION_PROMPT(TEST_PAGE_HOST))
            .mozVerify(SitePermissionsSelectors.PROMPT_DENY_BUTTON_LABELLED("Don’t allow"))
            .mozVerify(SitePermissionsSelectors.PROMPT_ALLOW_BUTTON_LABELLED("Allow"))
            .mozClick(SitePermissionsSelectors.PAGE_PERMISSION_DIALOG_ALLOW_BUTTON)
        on.browserPage.verifyPageContent("${mockLocationUpdatesRule.latitude}")
        on.browserPage.verifyPageContent("${mockLocationUpdatesRule.longitude}")
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334295
    @SmokeTest
    @Test
    fun audioVideoPermissionWithoutRememberingTheDecisionTest() {
        assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        on.browserPage.navigateToPage(TEST_PAGE)
        on.browserPage.mozClick(SitePermissionsSelectors.PAGE_PERMISSION_BUTTON("audioVideo", "Camera & Microphone"))
        on.browserPage
            .mozVerify(SitePermissionsSelectors.AUDIO_VIDEO_PERMISSION_PROMPT(TEST_PAGE_HOST))
            .mozVerify(SitePermissionsSelectors.PROMPT_DENY_BUTTON_LABELLED("Don’t allow"))
            .mozVerify(SitePermissionsSelectors.PROMPT_ALLOW_BUTTON_LABELLED("Allow"))
            .mozClick(SitePermissionsSelectors.PAGE_PERMISSION_DIALOG_DENY_BUTTON)
        on.browserPage.verifyPageContent("Camera and Microphone not allowed")

        // The decision was not remembered, so the same click must prompt again -- that is what this test checks.
        on.browserPage.mozClick(SitePermissionsSelectors.PAGE_PERMISSION_BUTTON("audioVideo", "Camera & Microphone"))
        on.browserPage
            .mozVerify(SitePermissionsSelectors.AUDIO_VIDEO_PERMISSION_PROMPT(TEST_PAGE_HOST))
            .mozClick(SitePermissionsSelectors.PAGE_PERMISSION_DIALOG_ALLOW_BUTTON)
        on.browserPage.verifyPageContent("Camera and Microphone allowed")
    }

    companion object {
        // The pre-v2.0 page: that is the one the legacy class under conversion used.
        const val TEST_PAGE = "https://mozilla-mobile.github.io/testapp/permissions"

        // Derived from the URL so the two cannot drift. requireNotNull rather than !!, so a malformed URL names
        // itself instead of throwing a bare NPE while the companion object initialises.
        val TEST_PAGE_HOST = requireNotNull(TEST_PAGE.toUri().host) { "no host in $TEST_PAGE" }
    }
}
