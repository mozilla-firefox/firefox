/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import android.Manifest
import androidx.core.net.toUri
import androidx.test.filters.SdkSuppress
import mozilla.components.concept.engine.mediasession.MediaSession
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestAssetHelper.mutedVideoPageAsset
import org.mozilla.fenix.helpers.TestAssetHelper.videoPageAsset
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.pageObjects.SystemSettingsPage
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSiteSettingsAutoplaySelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSiteSettingsExceptionsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSiteSettingsPermissionsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsSiteSettingsSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SitePermissionsSelectors

/**
 * Both tests assert the behaviour of the *default* autoplay setting ("Block audio only"), so neither changes the
 * setting: a video with sound must not autoplay and needs the page's Play control, while a muted video plays on its
 * own.
 */
class SettingsSitePermissionsTest : BaseTest() {

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    // SystemSettingsPage is not in PageContext (it is not a Fenix page); the landed UploadPermissionsTest
    // constructs it the same way.
    private val systemSettings
        get() = SystemSettingsPage(composeRule)

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2095125
    // The group check replaces legacy's three withEffectiveVisibility(VISIBLE) assertions: these are UIAutomator
    // res-id selectors, and that tree holds only displayed nodes -- see SettingsSiteSettingsAutoplaySelectors.
    @SmokeTest
    @Test
    fun verifyAutoplayBlockAudioOnlySettingOnNotMutedVideoTest() {
        val genericPage = mockWebServer.getGenericAsset(1)
        val videoTestPage = mockWebServer.videoPageAsset

        on.settingsSiteSettingsAutoplay.navigateToPage()
        on.settingsSiteSettingsAutoplay
            .mozVerifyElementsByGroup("autoplayOptions")
            .mozVerifyElementIsNotChecked(SettingsSiteSettingsAutoplaySelectors.ALLOW_AUDIO_AND_VIDEO_RADIO)
            .mozVerifyElementIsNotChecked(SettingsSiteSettingsAutoplaySelectors.BLOCK_AUDIO_AND_VIDEO_ON_CELLULAR_RADIO)
            .mozVerifyElementIsChecked(SettingsSiteSettingsAutoplaySelectors.BLOCK_AUDIO_ONLY_RADIO)
            .mozVerifyElementIsNotChecked(SettingsSiteSettingsAutoplaySelectors.BLOCK_AUDIO_AND_VIDEO_RADIO)

        // Legacy left the settings screen with exitMenu(); the harness equivalent is the registered return edge.
        on.home.navigateToPage()

        on.browserPage.navigateToPage(genericPage.url.toString())
        on.browserPage.verifyPageContent(genericPage.content)

        on.browserPage.navigateToPage(videoTestPage.url.toString())
        on.browserPage.verifyPageContentWithReload(videoTestPage.url.toString(), videoTestPage.content)
        on.browserPage.clickMediaPlayButton()
        on.browserPage.verifyMediaPlaybackState(MediaSession.PlaybackState.PLAYING)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2286807
    // "Media file is playing" is the muted page's own text, printed once playback starts -- the assertion IS the
    // autoplay check, which is why no Play control is tapped here.
    @SmokeTest
    @Test
    fun verifyAutoplayBlockAudioOnlySettingOnMutedVideoTest() {
        val genericPage = mockWebServer.getGenericAsset(1)
        val mutedVideoTestPage = mockWebServer.mutedVideoPageAsset

        on.browserPage.navigateToPage(genericPage.url.toString())
        on.browserPage.verifyPageContent(genericPage.content)

        on.browserPage.navigateToPage(mutedVideoTestPage.url.toString())
        on.browserPage.verifyPageContentWithReload(mutedVideoTestPage.url.toString(), "Media file is playing")
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/246976
    // The Cancel round-trip is the point: legacy dismissed the dialog once and re-opened it, proving Cancel does not
    // clear anything, before confirming with OK.
    @SmokeTest
    @Test
    fun clearAllSitePermissionsExceptionsTest() {
        on.browserPage.navigateToPage(PERMISSIONS_TEST_PAGE)
        on.browserPage.mozClick(
            SitePermissionsSelectors.PAGE_PERMISSION_BUTTON("notify", "Open notifications dialogue")
        )
        on.browserPage
            .mozVerify(SitePermissionsSelectors.NOTIFICATIONS_PERMISSION_PROMPT(PERMISSIONS_TEST_PAGE_HOST))
            .mozClick(SitePermissionsSelectors.PAGE_PERMISSION_DIALOG_ALLOW_BUTTON)

        on.settingsSiteSettingsExceptions.navigateToPage()
        on.settingsSiteSettingsExceptions
            .mozVerify(SettingsSiteSettingsExceptionsSelectors.EXCEPTION_ROW(PERMISSIONS_TEST_PAGE_ORIGIN))
            .mozClick(SettingsSiteSettingsExceptionsSelectors.CLEAR_PERMISSIONS_ON_ALL_SITES_BUTTON)
            .mozVerify(SettingsSiteSettingsExceptionsSelectors.CLEAR_PERMISSIONS_DIALOG_TITLE)
            .mozClick(SettingsSiteSettingsExceptionsSelectors.CLEAR_PERMISSIONS_DIALOG_CANCEL_BUTTON)
            .mozClick(SettingsSiteSettingsExceptionsSelectors.CLEAR_PERMISSIONS_ON_ALL_SITES_BUTTON)
            .mozClick(SettingsSiteSettingsExceptionsSelectors.CLEAR_PERMISSIONS_DIALOG_OK_BUTTON)
            .mozVerify(SettingsSiteSettingsExceptionsSelectors.EMPTY_EXCEPTIONS_LIST)
    }

    // Verifies that you can go to System settings and change the app's permissions from inside the app.
    // The legacy test carries no TestRail link, so none is recorded here.
    @SmokeTest
    @SdkSuppress(minSdkVersion = 29)
    @Test
    fun systemBlockedPermissionsRedirectToSystemAppSettingsTest() {
        on.settingsSiteSettingsPermissions.navigateToPage().verifyBlockedByAndroidSection().goBackToSiteSettings()

        on.settingsSiteSettings.openPermission(SettingsSiteSettingsSelectors.LOCATION_BUTTON)
        on.settingsSiteSettingsPermissions.verifyBlockedByAndroidSection().goBackToSiteSettings()

        on.settingsSiteSettings.openPermission(SettingsSiteSettingsSelectors.MICROPHONE_BUTTON)
        on.settingsSiteSettingsPermissions
            .verifyBlockedByAndroidSection()
            .mozClick(SettingsSiteSettingsPermissionsSelectors.GO_TO_SETTINGS_BUTTON)

        systemSettings.openAppPermissions()
        SYSTEM_PERMISSIONS.forEach { (label, androidPermission) ->
            systemSettings.allowAppPermission(label)
            systemSettings.mozPressBack()
            systemSettings.verifySystemPermissionGranted(label, androidPermission)
        }

        on.settingsSiteSettingsPermissions.returnFromSystemSettings().verifyUnblockedByAndroid().goBackToSiteSettings()

        on.settingsSiteSettings.openPermission(SettingsSiteSettingsSelectors.LOCATION_BUTTON)
        on.settingsSiteSettingsPermissions.verifyUnblockedByAndroid().goBackToSiteSettings()

        on.settingsSiteSettings.openPermission(SettingsSiteSettingsSelectors.CAMERA_BUTTON)
        on.settingsSiteSettingsPermissions.verifyUnblockedByAndroid()
    }

    companion object {
        const val PERMISSIONS_TEST_PAGE = "https://mozilla-mobile.github.io/testapp/v2.0/permissions"

        // Derived from the URL so the three cannot drift. requireNotNull rather than !!, so a malformed URL names
        // itself instead of throwing a bare NPE while the companion object initialises; and the origin is built
        // from the authority, which keeps a port if one ever appears where scheme + host would drop it.
        val PERMISSIONS_TEST_PAGE_HOST =
            requireNotNull(PERMISSIONS_TEST_PAGE.toUri().host) { "no host in $PERMISSIONS_TEST_PAGE" }
        val PERMISSIONS_TEST_PAGE_ORIGIN = PERMISSIONS_TEST_PAGE.toUri().let { "${it.scheme}://${it.authority}" }

        // Label as the settings list shows it, paired with the permission the OS is asked about.
        val SYSTEM_PERMISSIONS =
            listOf(
                "Camera" to Manifest.permission.CAMERA,
                "Location" to Manifest.permission.ACCESS_FINE_LOCATION,
                "Microphone" to Manifest.permission.RECORD_AUDIO,
            )
    }
}
