/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.core.net.toUri
import androidx.test.espresso.Espresso.pressBack
import androidx.test.filters.SdkSuppress
import mozilla.components.concept.engine.mediasession.MediaSession
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.grantSystemPermission
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getMutedVideoPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.getVideoPageAsset
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

/**
 *  Tests for verifying
 *  - site permissions settings sub-menu
 *  - the settings effects on the app behavior
 *
 */
class SettingsSitePermissionsTest : TestSetup() {
    /* Test page created and handled by the Mozilla mobile test-eng team */
    private val permissionsTestPage = "https://mozilla-mobile.github.io/testapp/v2.0/permissions"
    private val permissionsTestPageOrigin = "https://mozilla-mobile.github.io"
    private val permissionsTestPageHost = "mozilla-mobile.github.io"

    @get:Rule
    val activityTestRule = AndroidComposeTestRule(
        HomeActivityTestRule(
            isPWAsPromptEnabled = false,
            isDeleteSitePermissionsEnabled = true,
        ),
    ) { it.activity }

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/246974
    @Test
    fun sitePermissionsItemsTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
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
    @SmokeTest
    @Test
    @SdkSuppress(minSdkVersion = 29)
    fun systemBlockedPermissionsRedirectToSystemAppSettingsTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
        }.openCamera {
            verifyBlockedByAndroidSection()
        }.goBack {
        }.openLocation {
            verifyBlockedByAndroidSection()
        }.goBack {
        }.openMicrophone {
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
        }.goBack {
        }.openLocation {
            verifyUnblockedByAndroid()
        }.goBack {
        }.openCamera {
            verifyUnblockedByAndroid()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2095125
    @SmokeTest
    @Test
    fun verifyAutoplayBlockAudioOnlySettingOnNotMutedVideoTest() {
        val genericPage = getGenericAsset(mockWebServer, 1)
        val videoTestPage = getVideoPageAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
        }.openAutoPlay {
            verifySitePermissionsAutoPlaySubMenuItems()
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericPage.url) {
            verifyPageContent(genericPage.content)
        }.openTabDrawer(activityTestRule) {
            closeTab()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(videoTestPage.url) {
            try {
                verifyPageContent(videoTestPage.content)
                clickPageObject(itemWithText("Play"))
                assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
            } catch (e: AssertionError) {
                navigationToolbar {
                }.openThreeDotMenu {
                }.refreshPage {
                    verifyPageContent(videoTestPage.content)
                    clickPageObject(itemWithText("Play"))
                    assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2286807
    @SmokeTest
    @Test
    fun verifyAutoplayBlockAudioOnlySettingOnMutedVideoTest() {
        val genericPage = getGenericAsset(mockWebServer, 1)
        val mutedVideoTestPage = getMutedVideoPageAsset(mockWebServer)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericPage.url) {
            verifyPageContent(genericPage.content)
        }.openTabDrawer(activityTestRule) {
            closeTab()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(mutedVideoTestPage.url) {
            try {
                verifyPageContent("Media file is playing")
            } catch (e: AssertionError) {
                navigationToolbar {
                }.openThreeDotMenu {
                }.refreshPage {
                    verifyPageContent("Media file is playing")
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2095124
    @Test
    @SkipLeaks
    fun verifyAutoplayAllowAudioVideoSettingOnNotMutedVideoTestTest() {
        val genericPage = getGenericAsset(mockWebServer, 1)
        val videoTestPage = getVideoPageAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
        }.openAutoPlay {
            selectAutoplayOption("Allow audio and video")
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericPage.url) {
            verifyPageContent(genericPage.content)
        }.openTabDrawer(activityTestRule) {
            closeTab()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(videoTestPage.url) {
            try {
                verifyPageContent(videoTestPage.content)
                assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
            } catch (e: AssertionError) {
                navigationToolbar {
                }.openThreeDotMenu {
                }.refreshPage {
                    verifyPageContent(videoTestPage.content)
                    assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2286806
    @Test
    fun verifyAutoplayAllowAudioVideoSettingOnMutedVideoTest() {
        val mutedVideoTestPage = getMutedVideoPageAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
        }.openAutoPlay {
            selectAutoplayOption("Allow audio and video")
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(mutedVideoTestPage.url) {
            try {
                verifyPageContent("Media file is playing")
            } catch (e: AssertionError) {
                navigationToolbar {
                }.openThreeDotMenu {
                }.refreshPage {
                    verifyPageContent("Media file is playing")
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2095126
    @Test
    @SkipLeaks
    fun verifyAutoplayBlockAudioAndVideoSettingOnNotMutedVideoTest() {
        val videoTestPage = getVideoPageAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
        }.openAutoPlay {
            selectAutoplayOption("Block audio and video")
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(videoTestPage.url) {
            try {
                verifyPageContent(videoTestPage.content)
                clickPageObject(itemWithText("Play"))
                assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
            } catch (e: AssertionError) {
                navigationToolbar {
                }.openThreeDotMenu {
                }.refreshPage {
                    verifyPageContent(videoTestPage.content)
                    clickPageObject(itemWithText("Play"))
                    assertPlaybackState(browserStore, MediaSession.PlaybackState.PLAYING)
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2286808
    @Test
    @SkipLeaks
    fun verifyAutoplayBlockAudioAndVideoSettingOnMutedVideoTest() {
        val mutedVideoTestPage = getMutedVideoPageAsset(mockWebServer)

        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
        }.openAutoPlay {
            selectAutoplayOption("Block audio and video")
            exitMenu()
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(mutedVideoTestPage.url) {
            verifyPageContent("Media file not playing")
            clickPageObject(itemWithText("Play"))
            try {
                verifyPageContent("Media file is playing")
            } catch (e: AssertionError) {
                navigationToolbar {
                }.openThreeDotMenu {
                }.refreshPage {
                    clickPageObject(itemWithText("Play"))
                    verifyPageContent("Media file is playing")
                }
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247362
    @Test
    fun verifyCameraPermissionSettingsTest() {
        navigationToolbar {
        }.enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {
        }.clickStartCameraButton {
            grantSystemPermission()
            verifyCameraPermissionPrompt(permissionsTestPageHost)
            pressBack()
        }
        browserScreen {
            navigationToolbar {
            }.openThreeDotMenu {
            }.openSettings {
            }.openSettingsSubMenuSiteSettings {
            }.openCamera {
                verifySitePermissionsCommonSubMenuItems()
                selectPermissionSettingOption("Blocked")
                exitMenu()
            }
        }.clickStartCameraButton {}
        browserScreen {
            verifyPageContent("Camera not allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247364
    @Test
    fun verifyMicrophonePermissionSettingsTest() {
        navigationToolbar {
        }.enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {
        }.clickStartMicrophoneButton {
            grantSystemPermission()
            verifyMicrophonePermissionPrompt(permissionsTestPageHost)
            pressBack()
        }
        browserScreen {
            navigationToolbar {
            }.openThreeDotMenu {
            }.openSettings {
            }.openSettingsSubMenuSiteSettings {
            }.openMicrophone {
                verifySitePermissionsCommonSubMenuItems()
                selectPermissionSettingOption("Blocked")
                exitMenu()
            }
        }.clickStartMicrophoneButton {}
        browserScreen {
            verifyPageContent("Microphone not allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247363
    @Test
    fun verifyLocationPermissionSettingsTest() {
        navigationToolbar {
        }.enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {
        }.clickGetLocationButton {
            verifyLocationPermissionPrompt(permissionsTestPageHost)
            pressBack()
        }
        browserScreen {
            navigationToolbar {
            }.openThreeDotMenu {
            }.openSettings {
            }.openSettingsSubMenuSiteSettings {
            }.openLocation {
                verifySitePermissionsCommonSubMenuItems()
                selectPermissionSettingOption("Blocked")
                exitMenu()
            }
        }.clickGetLocationButton {}
        browserScreen {
            verifyPageContent("User denied geolocation prompt")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/247365
    @Test
    fun verifyNotificationsPermissionSettingsTest() {
        navigationToolbar {
        }.enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {
        }.clickOpenNotificationButton {
            verifyNotificationsPermissionPrompt(permissionsTestPageHost)
            pressBack()
        }
        browserScreen {
            navigationToolbar {
            }.openThreeDotMenu {
            }.openSettings {
            }.openSettingsSubMenuSiteSettings {
            }.openNotification {
                verifyNotificationSubMenuItems()
                selectPermissionSettingOption("Blocked")
                exitMenu()
            }
        }.clickOpenNotificationButton {}
        browserScreen {
            verifyPageContent("Notifications not allowed")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1923415
    @Test
    fun verifyPersistentStoragePermissionSettingsTest() {
        navigationToolbar {
        }.enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {
        }.clickRequestPersistentStorageAccessButton {
            verifyPersistentStoragePermissionPrompt(permissionsTestPageHost)
            pressBack()
        }
        browserScreen {
            navigationToolbar {
            }.openThreeDotMenu {
            }.openSettings {
            }.openSettingsSubMenuSiteSettings {
            }.openPersistentStorage {
                verifySitePermissionsPersistentStorageSubMenuItems()
                selectPermissionSettingOption("Blocked")
                exitMenu()
            }
        }.clickRequestPersistentStorageAccessButton {}
        browserScreen {
            verifyPageContent("Persistent storage permission denied")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1923417
    @Test
    fun verifyDRMControlledContentPermissionSettingsTest() {
        navigationToolbar {
        }.enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {
        }.clickRequestDRMControlledContentAccessButton {
            verifyDRMContentPermissionPrompt(permissionsTestPageHost)
            pressBack()
            browserScreen {
            }.openThreeDotMenu {
            }.openSettings {
            }.openSettingsSubMenuSiteSettings {
            }.openDRMControlledContent {
                verifyDRMControlledContentSubMenuItems()
                selectDRMControlledContentPermissionSettingOption("Blocked")
                exitMenu()
            }
            browserScreen {
            }.clickRequestDRMControlledContentAccessButton {}
            browserScreen {
                verifyPageContent("DRM-controlled content not allowed")
            }.openThreeDotMenu {
            }.openSettings {
            }.openSettingsSubMenuSiteSettings {
            }.openDRMControlledContent {
                selectDRMControlledContentPermissionSettingOption("Allowed")
                exitMenu()
            }
            browserScreen {
            }.openThreeDotMenu {
            }.refreshPage {
            }.clickRequestDRMControlledContentAccessButton {}
            browserScreen {
                verifyPageContent("DRM-controlled content allowed")
            }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/246976
    @SmokeTest
    @Test
    fun clearAllSitePermissionsExceptionsTest() {
        navigationToolbar {
        }.enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {
        }.clickOpenNotificationButton {
            verifyNotificationsPermissionPrompt(permissionsTestPageHost)
        }.clickPagePermissionButton(true) {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
        }.openExceptions {
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
        navigationToolbar {
        }.enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {
        }.clickOpenNotificationButton {
            verifyNotificationsPermissionPrompt(permissionsTestPageHost)
        }.clickPagePermissionButton(true) {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
        }.openExceptions {
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
        navigationToolbar {
        }.enterURLAndEnterToBrowser(permissionsTestPage.toUri()) {
        }.clickOpenNotificationButton {
            verifyNotificationsPermissionPrompt(permissionsTestPageHost)
        }.clickPagePermissionButton(true) {
        }.openThreeDotMenu {
        }.openSettings {
        }.openSettingsSubMenuSiteSettings {
        }.openExceptions {
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
