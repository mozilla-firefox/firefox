/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import android.Manifest
import android.content.Context
import android.hardware.camera2.CameraManager
import android.media.AudioManager
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import androidx.core.net.toUri
import androidx.test.rule.GrantPermissionRule
import mozilla.components.support.ktx.util.PromptAbuserDetector
import org.junit.After
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MockLocationUpdatesRule
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.RetryableComposeTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/** Tests for verifying site permissions prompts & functionality */
class SitePermissionsTest {
    // Test page created and handled by the Mozilla mobile test-eng team
    private val testPage = "https://mozilla-mobile.github.io/testapp/permissions"
    private val testPageHost = "mozilla-mobile.github.io"
    private val cameraManager = appContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    private val micManager = appContext.getSystemService(Context.AUDIO_SERVICE) as AudioManager

    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1) val retryTestRule = RetryTestRule(3)

    @get:Rule(order = 2)
    val retryableComposeTestRule = RetryableComposeTestRule {
        AndroidComposeTestRuleV2(HomeActivityIntentTestRule(isDeleteSitePermissionsEnabled = true)) { it.activity }
    }

    private val composeTestRule
        get() = retryableComposeTestRule.current

    @get:Rule(order = 3) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    @get:Rule
    val grantPermissionRule: GrantPermissionRule =
        GrantPermissionRule.grant(
            Manifest.permission.RECORD_AUDIO,
            Manifest.permission.CAMERA,
            Manifest.permission.ACCESS_COARSE_LOCATION,
            Manifest.permission.ACCESS_FINE_LOCATION,
        )

    @get:Rule val mockLocationUpdatesRule = MockLocationUpdatesRule()

    @Before
    fun setUp() {
        PromptAbuserDetector.validationsEnabled = false
    }

    @After
    fun tearDown() {
        PromptAbuserDetector.validationsEnabled = true
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334295
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.SitePermissionsTest#audioVideoPermissionWithoutRememberingTheDecisionTest"
            ],
        bug = 2064815,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun audioVideoPermissionWithoutRememberingTheDecisionTest() {
        assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartAudioVideoButton {
                verifyAudioVideoPermissionPrompt(testPageHost)
            }
            .clickPagePermissionButton(false) {
                verifyPageContent("Camera and Microphone not allowed")
            }
            .clickStartAudioVideoButton {}
            .clickPagePermissionButton(true) {
                verifyPageContent("Camera and Microphone allowed")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334294
    @Test
    fun blockAudioVideoPermissionRememberingTheDecisionTest() {
        assumeTrue(cameraManager.cameraIdList.isNotEmpty())
        assumeTrue(micManager.microphones.isNotEmpty())

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartAudioVideoButton {
                verifyAudioVideoPermissionPrompt(testPageHost)
                selectRememberPermissionDecision()
            }
            .clickPagePermissionButton(false) {
                verifyPageContent("Camera and Microphone not allowed")
            }
            .openThreeDotMenu {}
            .clickRefreshButton {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartAudioVideoButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Camera and Microphone not allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/251388
    @Test
    fun allowAudioVideoPermissionRememberingTheDecisionTest() {
        assumeTrue(cameraManager.cameraIdList.isNotEmpty())
        assumeTrue(micManager.microphones.isNotEmpty())

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartAudioVideoButton {
                verifyAudioVideoPermissionPrompt(testPageHost)
                selectRememberPermissionDecision()
            }
            .clickPagePermissionButton(true) {
                verifyPageContent("Camera and Microphone allowed")
            }
            .openThreeDotMenu {}
            .clickRefreshButton {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartAudioVideoButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Camera and Microphone allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334189
    @Test
    fun microphonePermissionWithoutRememberingTheDecisionTest() {
        assumeTrue(micManager.microphones.isNotEmpty())

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartMicrophoneButton {
                verifyMicrophonePermissionPrompt(testPageHost)
            }
            .clickPagePermissionButton(false) {
                verifyPageContent("Microphone not allowed")
            }
            .clickStartMicrophoneButton {}
            .clickPagePermissionButton(true) {
                verifyPageContent("Microphone allowed")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334190
    @Test
    fun blockMicrophonePermissionRememberingTheDecisionTest() {
        assumeTrue(micManager.microphones.isNotEmpty())

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartMicrophoneButton {
                verifyMicrophonePermissionPrompt(testPageHost)
                selectRememberPermissionDecision()
            }
            .clickPagePermissionButton(false) {
                verifyPageContent("Microphone not allowed")
            }
            .openThreeDotMenu {}
            .clickRefreshButton {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartMicrophoneButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Microphone not allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/251387
    @Test
    fun allowMicrophonePermissionRememberingTheDecisionTest() {
        assumeTrue(micManager.microphones.isNotEmpty())

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartMicrophoneButton {
                verifyMicrophonePermissionPrompt(testPageHost)
                selectRememberPermissionDecision()
            }
            .clickPagePermissionButton(true) {
                verifyPageContent("Microphone allowed")
            }
            .openThreeDotMenu {}
            .clickRefreshButton {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartMicrophoneButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Microphone allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334076
    @Test
    fun cameraPermissionWithoutRememberingDecisionTest() {
        assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartCameraButton {
                verifyCameraPermissionPrompt(testPageHost)
            }
            .clickPagePermissionButton(false) {
                verifyPageContent("Camera not allowed")
            }
            .clickStartCameraButton {}
            .clickPagePermissionButton(true) {
                verifyPageContent("Camera allowed")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334077
    @Test
    fun blockCameraPermissionRememberingTheDecisionTest() {
        assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartCameraButton {
                verifyCameraPermissionPrompt(testPageHost)
                selectRememberPermissionDecision()
            }
            .clickPagePermissionButton(false) {
                verifyPageContent("Camera not allowed")
            }
            .openThreeDotMenu {}
            .clickRefreshButton {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartCameraButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Camera not allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/251386
    @Test
    fun allowCameraPermissionRememberingTheDecisionTest() {
        assumeTrue(cameraManager.cameraIdList.isNotEmpty())

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartCameraButton {
                verifyCameraPermissionPrompt(testPageHost)
                selectRememberPermissionDecision()
            }
            .clickPagePermissionButton(true) {
                verifyPageContent("Camera allowed")
            }
            .openThreeDotMenu {}
            .clickRefreshButton {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickStartCameraButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Camera allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334074
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.SitePermissionsTest#blockNotificationsPermissionTest"],
        bug = 2064815,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun blockNotificationsPermissionTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {}
            .clickOpenNotificationButton {
                verifyNotificationsPermissionPrompt(testPageHost)
            }
            .clickPagePermissionButton(false) {
                verifyPageContent("Notifications not allowed")
            }
            .openThreeDotMenu {}
            .clickRefreshButton {
                waitForPageToLoad(pageLoadWaitingTime = waitingTimeLong)
            }
            .clickOpenNotificationButton {
                verifyNotificationsPermissionPrompt(testPageHost, true)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/251380
    @Test
    fun allowNotificationsPermissionTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {}
            .clickOpenNotificationButton {
                verifyNotificationsPermissionPrompt(testPageHost)
            }
            .clickPagePermissionButton(true) {
                verifyPageContent("Notifications allowed")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/251385
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.SitePermissionsTest#allowLocationPermissionsTest"],
        bug = 2064815,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun allowLocationPermissionsTest() {
        mockLocationUpdatesRule.setMockLocation()

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {}
            .clickGetLocationButton {
                verifyLocationPermissionPrompt(testPageHost)
            }
            .clickPagePermissionButton(true) {
                verifyPageContent("${mockLocationUpdatesRule.latitude}")
                verifyPageContent("${mockLocationUpdatesRule.longitude}")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2334075
    @Test
    fun blockLocationPermissionsTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {}
            .clickGetLocationButton {
                verifyLocationPermissionPrompt(testPageHost)
            }
            .clickPagePermissionButton(false) {
                verifyPageContent("User denied geolocation prompt")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4024998
    @Test
    fun verifyDoNotAskAgainIsHiddenForLocationPermissionInPrivateModeTest() {
        homeScreen(composeTestRule) {}.togglePrivateBrowsingMode()

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.toUri()) {}
            .clickGetLocationButton {
                verifyLocationPermissionPrompt(testPageHost)
                verifyDoNotAskAgainIsHidden()
            }
    }
}
