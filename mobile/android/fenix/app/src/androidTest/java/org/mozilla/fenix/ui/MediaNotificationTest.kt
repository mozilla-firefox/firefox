/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import mozilla.components.concept.engine.mediasession.MediaSession
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.notificationShade

/**
 *  Tests for verifying basic functionality of media notifications:
 *  - video and audio playback system notifications appear and can pause/play the media content
 *  - a media notification icon is displayed on the homescreen for the tab playing media content
 *  Note: this test only verifies media notifications, not media itself
 */
class MediaNotificationTest : TestSetup() {
    @get:Rule(order = 0)
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityTestRule.withDefaultSettingsOverrides(),
        ) { it.activity }

    @get:Rule(order = 1)
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Rule(order = 2)
    @JvmField
    val retryTestRule = RetryTestRule(3)

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1347033
    @SmokeTest
    @Test
    fun verifyVideoPlaybackSystemNotificationTest() {
        val videoTestPage = TestAssetHelper.getVideoPageAsset(mockWebServer)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(videoTestPage.url) {
            mDevice.waitForIdle()
            clickPageObject(MatcherHelper.itemWithText("Play"))
            assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
        }.openNotificationShade {
            verifySystemNotificationExists(videoTestPage.title)
            clickMediaNotificationControlButton("Pause")
            verifyMediaSystemNotificationButtonState("Play")
        }

        mDevice.pressBack()

        browserScreen {
            assertPlaybackState(browserStore, MediaSession.PlaybackState.PAUSED)
        }.openTabDrawer(composeTestRule) {
            closeTab()
        }

        mDevice.openNotification()

        notificationShade {
            verifySystemNotificationDoesNotExist(videoTestPage.title)
        }

        // close notification shade before the next test
        mDevice.pressBack()
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2316010
    @SmokeTest
    @Test
    fun verifyAudioPlaybackSystemNotificationTest() {
        val audioTestPage = TestAssetHelper.getAudioPageAsset(mockWebServer)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(audioTestPage.url) {
            mDevice.waitForIdle()
            clickPageObject(MatcherHelper.itemWithText("Play"))
            assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
        }.openNotificationShade {
            verifySystemNotificationExists(audioTestPage.title)
            clickMediaNotificationControlButton("Pause")
            verifyMediaSystemNotificationButtonState("Play")
        }

        mDevice.pressBack()

        browserScreen {
            assertPlaybackState(browserStore, MediaSession.PlaybackState.PAUSED)
        }.openTabDrawer(composeTestRule) {
            closeTab()
        }

        mDevice.openNotification()

        notificationShade {
            verifySystemNotificationDoesNotExist(audioTestPage.title)
        }

        // close notification shade before the next test
        mDevice.pressBack()
    }

    // TestRail: https://mozilla.testrail.io/index.php?/cases/view/903595
    @Test
    fun mediaSystemNotificationInPrivateModeTest() {
        val audioTestPage = TestAssetHelper.getAudioPageAsset(mockWebServer)

        homeScreen {
        }.openTabDrawer(composeTestRule) {
        }.toggleToPrivateTabs {
        }.openNewTab {
        }.submitQuery(audioTestPage.url.toString()) {
            mDevice.waitForIdle()
            clickPageObject(MatcherHelper.itemWithText("Play"))
            assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
        }.openNotificationShade {
            verifySystemNotificationExists("A site is playing media")
            clickMediaNotificationControlButton("Pause")
            verifyMediaSystemNotificationButtonState("Play")
        }

        mDevice.pressBack()

        browserScreen {
            assertPlaybackState(browserStore, MediaSession.PlaybackState.PAUSED)
        }.openTabDrawer(composeTestRule) {
            closeTab()
            verifySnackBarText("Private tab closed")
        }

        mDevice.openNotification()

        notificationShade {
            verifySystemNotificationDoesNotExist("A site is playing media")
        }

        // close notification shade before and go back to regular mode before the next test
        mDevice.pressBack()
        homeScreen { }.togglePrivateBrowsingMode()
    }
}
