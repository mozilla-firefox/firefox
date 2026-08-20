/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import androidx.core.net.toUri
import androidx.test.espresso.Espresso.pressBack
import androidx.test.filters.SdkSuppress
import mozilla.components.concept.engine.mediasession.MediaSession
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.grantSystemPermission
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.RetryableComposeTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.mutedVideoPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.videoPageAsset
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 * Tests for verifying
 * - site permissions settings sub-menu
 * - the settings effects on the app behavior
 */
class SettingsSitePermissionsTest {
    // Test page created and handled by the Mozilla mobile test-eng team
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    private val browserStore
        get() = fenixTestRule.browserStore

    private val permissionsTestPage = "https://mozilla-mobile.github.io/testapp/v2.0/permissions"
    private val permissionsTestPageOrigin = "https://mozilla-mobile.github.io"
    private val permissionsTestPageHost = "mozilla-mobile.github.io"

    @get:Rule(order = 1) val retryTestRule = RetryTestRule(3)

    @get:Rule(order = 2)
    val retryableComposeTestRule = RetryableComposeTestRule {
        AndroidComposeTestRuleV2(HomeActivityTestRule(isDeleteSitePermissionsEnabled = true)) { it.activity }
    }

    private val composeTestRule
        get() = retryableComposeTestRule.current

    @get:Rule(order = 3) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/246974
    @Test
    fun sitePermissionsItemsTest() {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {
                verifySiteSettingsToolbarTitle()
                verifyToolbarGoBackButton()
                verifyContentHeading()
                verifyAlwaysRequestDesktopSiteOption()
                verifyAlwaysRequestDesktopSiteToggleIsEnabled(enabled = false)
                verifyPermissionsHeading()
                verifySitePermissionOption("Autoplay", "Block audio only")
                verifySitePermissionOption("Camera", "Blocked by Android")
                verifySitePermissionOption("Location", "Blocked by Android")
                verifySitePermissionOption("Microphone", "Blocked by Android")
                verifySitePermissionOption("Notification", "Ask to allow")
                verifySitePermissionOption("Persistent Storage", "Ask to allow")
                verifySitePermissionOption("Cross-site cookies", "Ask to allow")
                verifySitePermissionOption("DRM-controlled content", "Ask to allow")
                verifySitePermissionOption("Exceptions")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247680
    // Verifies that you can go to System settings and change app's permissions from inside the app
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.SettingsSitePermissionsTest#systemBlockedPermissionsRedirectToSystemAppSettingsTest"
            ],
        bug = 2064833,
        since = "2026-08",
        notes =
            "The port also asserts the OS permission state via PackageManager; the legacy row-title assertion " +
                "cannot fail above API 30, where the disambiguating row summary is not rendered.",
    )
    @SmokeTest
    @Test
    @SdkSuppress(minSdkVersion = 29)
    fun systemBlockedPermissionsRedirectToSystemAppSettingsTest() {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {}
            .openCamera {
                verifyBlockedByAndroidSection()
            }
            .goBack {}
            .openLocation {
                verifyBlockedByAndroidSection()
            }
            .goBack {}
            .openMicrophone {
                verifyBlockedByAndroidSection()
                clickGoToSettingsButton()
                openAppSystemPermissionsSettings()
                switchAppPermissionSystemSetting("Camera", "Allow")
                goBackToSystemAppPermissionSettings()
                verifySystemGrantedPermission("Camera")
                switchAppPermissionSystemSetting("Location", "Allow")
                goBackToSystemAppPermissionSettings()
                verifySystemGrantedPermission("Location")
                switchAppPermissionSystemSetting("Microphone", "Allow")
                goBackToSystemAppPermissionSettings()
                verifySystemGrantedPermission("Microphone")
                goBackToPermissionsSettingsSubMenu()
                verifyUnblockedByAndroid()
            }
            .goBack {}
            .openLocation {
                verifyUnblockedByAndroid()
            }
            .goBack {}
            .openCamera {
                verifyUnblockedByAndroid()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2095125
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.SettingsSitePermissionsTest#verifyAutoplayBlockAudioOnlySettingOnNotMutedVideoTest"
            ],
        bug = 2064810,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyAutoplayBlockAudioOnlySettingOnNotMutedVideoTest() {
        val genericPage = mockWebServer.getGenericAsset(1)
        val videoTestPage = mockWebServer.videoPageAsset

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {}
            .openAutoPlay {
                verifySitePermissionsAutoPlaySubMenuItems()
                exitMenu()
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericPage.url) {
                verifyPageContent(genericPage.content)
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(videoTestPage.url) {
                try {
                    verifyPageContent(videoTestPage.content)
                    clickPageObject(composeTestRule, itemWithText("Play"))
                    assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
                } catch (e: AssertionError) {
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            verifyPageContent(videoTestPage.content)
                            clickPageObject(composeTestRule, itemWithText("Play"))
                            assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
                        }
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2286807
    @Converted(
        replacedBy =
            [
                "org.mozilla.fenix.ui.efficiency.tests.SettingsSitePermissionsTest#verifyAutoplayBlockAudioOnlySettingOnMutedVideoTest"
            ],
        bug = 2064810,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyAutoplayBlockAudioOnlySettingOnMutedVideoTest() {
        val genericPage = mockWebServer.getGenericAsset(1)
        val mutedVideoTestPage = mockWebServer.mutedVideoPageAsset

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericPage.url) {
                verifyPageContent(genericPage.content)
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(mutedVideoTestPage.url) {
                try {
                    verifyPageContent("Media file is playing")
                } catch (e: AssertionError) {
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            verifyPageContent("Media file is playing")
                        }
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2095124
    @Test
    fun verifyAutoplayAllowAudioVideoSettingOnNotMutedVideoTestTest() {
        val genericPage = mockWebServer.getGenericAsset(1)
        val videoTestPage = mockWebServer.videoPageAsset

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {}
            .openAutoPlay {
                selectAutoplayOption("Allow audio and video")
                exitMenu()
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(genericPage.url) {
                verifyPageContent(genericPage.content)
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(videoTestPage.url) {
                try {
                    verifyPageContent(videoTestPage.content)
                    assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
                } catch (e: AssertionError) {
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            verifyPageContent(videoTestPage.content)
                            assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
                        }
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2286806
    @Test
    fun verifyAutoplayAllowAudioVideoSettingOnMutedVideoTest() {
        val mutedVideoTestPage = mockWebServer.mutedVideoPageAsset

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {}
            .openAutoPlay {
                selectAutoplayOption("Allow audio and video")
                exitMenu()
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(mutedVideoTestPage.url) {
                try {
                    verifyPageContent("Media file is playing")
                } catch (e: AssertionError) {
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            verifyPageContent("Media file is playing")
                        }
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2095126
    @Test
    fun verifyAutoplayBlockAudioAndVideoSettingOnNotMutedVideoTest() {
        val videoTestPage = mockWebServer.videoPageAsset

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {}
            .openAutoPlay {
                selectAutoplayOption("Block audio and video")
                exitMenu()
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(videoTestPage.url) {
                try {
                    verifyPageContent(videoTestPage.content)
                    clickPageObject(composeTestRule, itemWithText("Play"))
                    assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
                } catch (e: AssertionError) {
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            verifyPageContent(videoTestPage.content)
                            clickPageObject(composeTestRule, itemWithText("Play"))
                            assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
                        }
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2286808
    @Test
    fun verifyAutoplayBlockAudioAndVideoSettingOnMutedVideoTest() {
        val mutedVideoTestPage = mockWebServer.mutedVideoPageAsset

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {}
            .openAutoPlay {
                selectAutoplayOption("Block audio and video")
                exitMenu()
            }
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(mutedVideoTestPage.url) {
                verifyPageContent("Media file not playing")
                clickPageObject(composeTestRule, itemWithText("Play"))
                try {
                    verifyPageContent("Media file is playing")
                } catch (e: AssertionError) {
                    browserScreen(composeTestRule) {}
                        .openThreeDotMenu {}
                        .clickRefreshButton {
                            clickPageObject(composeTestRule, itemWithText("Play"))
                            verifyPageContent("Media file is playing")
                        }
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247362
    @Test
    fun verifyCameraPermissionSettingsTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {}
            .clickStartCameraButton {
                grantSystemPermission()
                verifyCameraPermissionPrompt(permissionsTestPageHost)
                pressBack()
            }
        browserScreen(composeTestRule) {
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickSettingsButton {}
                    .openSettingsSubMenuSiteSettings {}
                    .openCamera {
                        verifySitePermissionsCommonSubMenuItems()
                        selectPermissionSettingOption("Blocked")
                        exitMenu()
                    }
            }
            .clickStartCameraButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Camera not allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247364
    @Test
    fun verifyMicrophonePermissionSettingsTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {}
            .clickStartMicrophoneButton {
                grantSystemPermission()
                verifyMicrophonePermissionPrompt(permissionsTestPageHost)
                pressBack()
            }
        browserScreen(composeTestRule) {
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickSettingsButton {}
                    .openSettingsSubMenuSiteSettings {}
                    .openMicrophone {
                        verifySitePermissionsCommonSubMenuItems()
                        selectPermissionSettingOption("Blocked")
                        exitMenu()
                    }
            }
            .clickStartMicrophoneButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Microphone not allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247363
    @Test
    fun verifyLocationPermissionSettingsTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {}
            .clickGetLocationButton {
                verifyLocationPermissionPrompt(permissionsTestPageHost)
                pressBack()
            }
        browserScreen(composeTestRule) {
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickSettingsButton {}
                    .openSettingsSubMenuSiteSettings {}
                    .openLocation {
                        verifySitePermissionsCommonSubMenuItems()
                        selectPermissionSettingOption("Blocked")
                        exitMenu()
                    }
            }
            .clickGetLocationButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("User denied geolocation prompt")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247365
    @Test
    fun verifyNotificationsPermissionSettingsTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {}
            .clickOpenNotificationButton {
                verifyNotificationsPermissionPrompt(permissionsTestPageHost)
                pressBack()
            }
        browserScreen(composeTestRule) {
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickSettingsButton {}
                    .openSettingsSubMenuSiteSettings {}
                    .openNotification {
                        verifyNotificationSubMenuItems()
                        selectPermissionSettingOption("Blocked")
                        exitMenu()
                    }
            }
            .clickOpenNotificationButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Notifications not allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1923415
    @Test
    fun verifyPersistentStoragePermissionSettingsTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {}
            .clickRequestPersistentStorageAccessButton {
                verifyPersistentStoragePermissionPrompt(permissionsTestPageHost)
                pressBack()
            }
        browserScreen(composeTestRule) {
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickSettingsButton {}
                    .openSettingsSubMenuSiteSettings {}
                    .openPersistentStorage {
                        verifySitePermissionsPersistentStorageSubMenuItems()
                        selectPermissionSettingOption("Blocked")
                        exitMenu()
                    }
            }
            .clickRequestPersistentStorageAccessButton {}
        browserScreen(composeTestRule) {
            verifyPageContent("Persistent storage permission denied")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1923417
    @Test
    fun verifyDRMControlledContentPermissionSettingsTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {}
            .clickRequestDRMControlledContentAccessButton {
                verifyDRMContentPermissionPrompt(permissionsTestPageHost)
                pressBack()
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickSettingsButton {}
                    .openSettingsSubMenuSiteSettings {}
                    .openDRMControlledContent {
                        verifyDRMControlledContentSubMenuItems()
                        selectDRMControlledContentPermissionSettingOption("Blocked")
                        exitMenu()
                    }
                browserScreen(composeTestRule) {}.clickRequestDRMControlledContentAccessButton {}
                browserScreen(composeTestRule) {
                        verifyDRMControlledContentPageContent("DRM-controlled content not allowed")
                    }
                    .openThreeDotMenu {}
                    .clickSettingsButton {}
                    .openSettingsSubMenuSiteSettings {}
                    .openDRMControlledContent {
                        selectDRMControlledContentPermissionSettingOption("Allowed")
                        exitMenu()
                    }
                browserScreen(composeTestRule) {}
                    .openThreeDotMenu {}
                    .clickRefreshButton {}
                    .clickRequestDRMControlledContentAccessButton {}
                browserScreen(composeTestRule) {
                    verifyDRMControlledContentPageContent("DRM-controlled content allowed")
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/246976
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.SettingsSitePermissionsTest#clearAllSitePermissionsExceptionsTest"],
        bug = 2064823,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun clearAllSitePermissionsExceptionsTest() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {}
            .clickOpenNotificationButton {
                verifyNotificationsPermissionPrompt(permissionsTestPageHost)
            }
            .clickPagePermissionButton(true) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {}
            .openExceptions {
                verifyExceptionCreated(permissionsTestPageOrigin, true)
                clickClearPermissionsOnAllSites()
                verifyClearPermissionsDialog()
                clickCancel()
                clickClearPermissionsOnAllSites()
                clickOK()
                verifyExceptionsEmptyList()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247007
    @Test
    fun addAndClearOneWebPagePermission() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {}
            .clickOpenNotificationButton {
                verifyNotificationsPermissionPrompt(permissionsTestPageHost)
            }
            .clickPagePermissionButton(true) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {}
            .openExceptions {
                verifyExceptionCreated(permissionsTestPageOrigin, true)
                openSiteExceptionsDetails(permissionsTestPageOrigin)
                clickClearPermissionsForOneSite()
                verifyClearPermissionsForOneSiteDialog()
                clickCancel()
                clickClearPermissionsForOneSite()
                clickOK()
                verifyExceptionsEmptyList()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/326477
    @Test
    fun clearIndividuallyAWebPagePermission() {
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {}
            .clickOpenNotificationButton {
                verifyNotificationsPermissionPrompt(permissionsTestPageHost)
            }
            .clickPagePermissionButton(true) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openSettingsSubMenuSiteSettings {}
            .openExceptions {
                verifyExceptionCreated(permissionsTestPageOrigin, true)
                openSiteExceptionsDetails(permissionsTestPageOrigin)
                verifyPermissionSettingSummary("Notification", "Allowed")
                openChangePermissionSettingsMenu("Notification")
                clickClearOnePermissionForOneSite()
                verifyResetPermissionDefaultForThisSiteDialog()
                clickOK()
                pressBack()
                verifyPermissionSettingSummary("Notification", "Ask to allow")
                pressBack()
                // This should be changed to false, when https://bugzilla.mozilla.org/show_bug.cgi?id=1826297 is fixed
                verifyExceptionCreated(permissionsTestPageOrigin, true)
            }
    }
}
