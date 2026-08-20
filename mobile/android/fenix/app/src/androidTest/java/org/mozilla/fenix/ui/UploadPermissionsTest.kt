package org.mozilla.fenix.ui

import android.os.Build
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import androidx.test.filters.SdkSuppress
import mozilla.components.support.ktx.util.PromptAbuserDetector
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.assertExternalAppOpens
import org.mozilla.fenix.helpers.AppAndSystemHelper.closeSystemPhotoAndVideoPicker
import org.mozilla.fenix.helpers.AppAndSystemHelper.denyPermission
import org.mozilla.fenix.helpers.AppAndSystemHelper.grantSystemPermission
import org.mozilla.fenix.helpers.AppAndSystemHelper.verifySystemPhotoAndVideoPickerExists
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.TestAssetHelper.htmlControlsFormAsset
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar

class UploadPermissionsTest {

    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(HomeActivityIntentTestRule.withDefaultSettingsOverrides()) { it.activity }

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    @Before
    fun setUp() {
        PromptAbuserDetector.validationsEnabled = false
    }

    @After
    fun tearDown() {
        PromptAbuserDetector.validationsEnabled = true
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2121537
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.UploadPermissionsTest#fileUploadPermissionTest"],
        bug = 2063263,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun fileUploadPermissionTest() {
        val testPage = mockWebServer.htmlControlsFormAsset

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.url) {
                clickPageObject(composeTestRule, itemWithResId("upload_file"))
                // Grant app permission to access storage
                grantSystemPermission()
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    assertExternalAppOpens("com.google.android.documentsui")
                } else {
                    assertExternalAppOpens("com.android.documentsui")
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2751914
    @Test
    fun uploadSelectedAudioFilesWhileNoPermissionGrantedTest() {
        val testPage = mockWebServer.htmlControlsFormAsset

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.url) {
                clickPageObject(composeTestRule, itemWithResId("audioFileUpload"))
                // Deny app access to voice recording
                denyPermission()
                // Deny app access to audio files storage
                denyPermission()
                verifyPageContent("Choose audio file to upload")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2779525
    @Test
    fun uploadSelectedAudioFilesWhenStoragePermissionGrantedTest() {
        val testPage = mockWebServer.htmlControlsFormAsset

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.url) {
                clickPageObject(composeTestRule, itemWithResId("audioFileUpload"))
                // Deny app access to voice recording
                denyPermission()
                // Grant app access to audio files storage
                grantSystemPermission()
                assertExternalAppOpens("com.google.android.documentsui")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2751915
    // The photo picker is only available on devices with API level 33 (TIRAMISU) or higher
    @SdkSuppress(minSdkVersion = 33)
    @Test
    fun uploadSelectedVideoOrImageFilesWhenStoragePermissionGrantedTest() {
        val testPage = mockWebServer.htmlControlsFormAsset

        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(testPage.url) {
                clickPageObject(composeTestRule, itemWithResId("photosUpload"))
                // Deny app access to pictures and video recordings
                denyPermission()
                verifySystemPhotoAndVideoPickerExists()
                closeSystemPhotoAndVideoPicker()
            }
    }
}
