/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.test.filters.SdkSuppress
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdAndText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clickContextMenuItem
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.longClickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.openEditURLView
import org.mozilla.fenix.ui.robots.searchScreen
import org.mozilla.fenix.ui.robots.shareOverlay

class TextSelectionTest : TestSetup() {
    @get:Rule
    val activityIntentTestRule = HomeActivityIntentTestRule(
        isLocationPermissionEnabled = SitePermissionsRules.Action.BLOCKED,
        isPageLoadTranslationsPromptEnabled = false,
    )

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Rule
    @JvmField
    val retryTestRule = RetryTestRule(3)

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326832
    @SmokeTest
    @Test
    fun verifySelectAllTextOptionTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            longClickPageObject(itemContainingText("content"))
            clickContextMenuItem("Select all")
            clickContextMenuItem("Copy")
        }.openNavigationToolbar {
            openEditURLView()
        }

        searchScreen {
            clickClearButton()
            longClickToolbar()
            clickPasteText()
            // With Select all, white spaces are copied
            // Potential bug https://bugzilla.mozilla.org/show_bug.cgi?id=1821310
            verifyTypedToolbarText("  Page content: 1 ", exists = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326828
    @Test
    fun verifyCopyTextOptionTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            longClickPageObject(itemContainingText("content"))
            clickContextMenuItem("Copy")
        }.openNavigationToolbar {
        }

        searchScreen {
            clickClearButton()
            longClickToolbar()
            clickPasteText()
            verifyTypedToolbarText("content", exists = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326829
    @Test
    fun verifyShareSelectedTextOptionTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            longClickPageObject(itemWithText(genericURL.content))
        }.clickShareSelectedText {
            verifyAndroidShareLayout()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326830
    @Test
    fun verifySearchTextOptionTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            longClickPageObject(itemContainingText("content"))
            clickContextMenuItem("Search")
            mDevice.waitForIdle()
            verifyUrl("content")
            verifyTabCounter("2")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326831
    @SmokeTest
    @Test
    fun verifyPrivateSearchTextTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.togglePrivateBrowsingMode()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            longClickPageObject(itemContainingText("content"))
            clickContextMenuItem("Private Search")
            mDevice.waitForIdle()
            verifyTabCounter("2")
            verifyUrl("content")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326834
    @SdkSuppress(maxSdkVersion = 30)
    @Test
    fun verifySelectAllPDFTextOptionTest() {
        val genericURL =
            TestAssetHelper.getGenericAsset(mockWebServer, 3)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemWithText("PDF form file"))
            longClickPageObject(itemContainingText("Crossing"))
            clickContextMenuItem("Select all")
            clickContextMenuItem("Copy")
        }.openNavigationToolbar {
            openEditURLView()
        }

        searchScreen {
            clickClearButton()
            longClickToolbar()
            clickPasteText()
            verifyTypedToolbarText(
                "Washington Crossing the Delaware Wikipedia linkName: Android",
                exists = true,
            )
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/243839
    @SmokeTest
    @Test
    fun verifyCopyPDFTextOptionTest() {
        val genericURL =
            TestAssetHelper.getGenericAsset(mockWebServer, 3)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemWithText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
            longClickPageObject(itemContainingText("Crossing"))
            clickContextMenuItem("Copy")
        }.openNavigationToolbar {
        }

        searchScreen {
            clickClearButton()
            longClickToolbar()
            clickPasteText()
            verifyTypedToolbarText("Crossing", exists = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326835
    @Test
    fun verifyShareSelectedPDFTextOptionTest() {
        val genericURL =
            TestAssetHelper.getGenericAsset(mockWebServer, 3)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemWithText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
            longClickPageObject(itemContainingText("Crossing"))
        }.clickShareSelectedText {
            verifyAndroidShareLayout()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326836
    @SmokeTest
    @Test
    fun verifySearchPDFTextOptionTest() {
        val genericURL =
            TestAssetHelper.getGenericAsset(mockWebServer, 3)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemWithText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
            longClickPageObject(itemContainingText("Crossing"))
            clickContextMenuItem("Search")
            verifyUrl("Crossing")
            verifyTabCounter("2")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326837
    @Test
    fun verifyPrivateSearchPDFTextOptionTest() {
        val genericURL =
            TestAssetHelper.getGenericAsset(mockWebServer, 3)

        homeScreen {
        }.togglePrivateBrowsingMode()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(itemWithText("PDF form file"))
            clickPageObject(itemWithResIdAndText("android:id/button2", "CANCEL"))
            longClickPageObject(itemContainingText("Crossing"))
            clickContextMenuItem("Private Search")
            verifyUrl("Crossing")
            verifyTabCounter("2")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326813
    @Test
    fun verifyUrlBarTextSelectionOptionsTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openNavigationToolbar {
            longClickEditModeToolbar()
            verifyTextSelectionOptions("Open", "Cut", "Copy", "Share")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326814
    @Test
    fun verifyCopyUrlBarTextSelectionOptionTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openNavigationToolbar {
            longClickEditModeToolbar()
            clickContextMenuItem("Copy")
            clickClearToolbarButton()
            verifyToolbarIsEmpty()
            longClickEditModeToolbar()
            clickContextMenuItem("Paste")
            verifyUrl(genericURL.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326815
    @Test
    fun verifyCutUrlBarTextSelectionOptionTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openNavigationToolbar {
            longClickEditModeToolbar()
            clickContextMenuItem("Cut")
            verifyToolbarIsEmpty()
            longClickEditModeToolbar()
            clickContextMenuItem("Paste")
            verifyUrl(genericURL.url.toString())
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/243845
    @SmokeTest
    @Test
    fun verifyShareUrlBarTextSelectionOptionTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openNavigationToolbar {
            longClickEditModeToolbar()
            clickContextMenuItem("Share")
        }
        shareOverlay {
            verifyAndroidShareLayout()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/414316
    @Test
    fun urlBarQuickActionsTest() {
        val firstWebsite = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        val secondWebsite = TestAssetHelper.getGenericAsset(mockWebServer, 2)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(firstWebsite.url) {
            longClickToolbar()
            clickContextMenuItem("Copy")
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(secondWebsite.url) {
            longClickToolbar()
            clickContextMenuItem("Paste")
        }
        searchScreen {
            verifyTypedToolbarText(firstWebsite.url.toString(), exists = true)
        }.dismissSearchBar {
        }
        browserScreen {
            verifyUrl(secondWebsite.url.toString())
            longClickToolbar()
            clickContextMenuItem("Paste & Go")
            verifyUrl(firstWebsite.url.toString())
        }
    }
}
