/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import mozilla.components.feature.sitepermissions.SitePermissionsRules
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemContainingText
import org.mozilla.fenix.helpers.MatcherHelper.itemWithText
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.RetryableComposeTestRule
import org.mozilla.fenix.helpers.TestAssetHelper.getGenericAsset
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.clearTextFieldItem
import org.mozilla.fenix.ui.robots.clickContextMenuItem
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.longClickPageObject
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.robots.openEditURLView
import org.mozilla.fenix.ui.robots.searchScreen
import org.mozilla.fenix.ui.robots.shareOverlay
import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2

class TextSelectionTest {
    @get:Rule(order = 0)
    val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val retryTestRule = RetryTestRule(3)

    @get:Rule(order = 2)
    val retryableComposeTestRule = RetryableComposeTestRule {
        AndroidComposeTestRuleV2(
            HomeActivityTestRule(
                isLocationPermissionEnabled = SitePermissionsRules.Action.BLOCKED,
                isPageLoadTranslationsPromptEnabled = false,
                // workaround for toolbar at top position by default
                // remove with https://bugzilla.mozilla.org/show_bug.cgi?id=1917640
                shouldUseBottomToolbar = true,
            ),
        ) { it.activity }
    }

    private val composeTestRule get() = retryableComposeTestRule.current

    @get:Rule(order = 3)
    val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326832
    @Ignore("Disabled after enabling the composable toolbar and main menu: https://bugzilla.mozilla.org/show_bug.cgi?id=2006295")
    @SmokeTest
    @Test
    fun verifySelectAllTextOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            longClickPageObject(composeTestRule, itemContainingText("content"))
            clickContextMenuItem("Select all")
            clickContextMenuItem("Copy")
        }.openNavigationToolbar {
            openEditURLView()
        }

        searchScreen(composeTestRule) {
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
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            longClickPageObject(composeTestRule, itemContainingText("content"))
            clickContextMenuItem("Copy")
        }.openNavigationToolbar {
        }

        searchScreen(composeTestRule) {
            clickClearButton()
            longClickToolbar()
            clickPasteText()
            verifyTypedToolbarText("content", exists = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326829
    @Test
    fun verifyShareSelectedTextOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            longClickPageObject(composeTestRule, itemWithText(genericURL.content))
        }.clickShareSelectedText {
            verifyAndroidShareLayout()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326830
    @Test
    fun verifySearchTextOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            longClickPageObject(composeTestRule, itemContainingText("content"))
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
        val genericURL = mockWebServer.getGenericAsset(1)

        homeScreen(composeTestRule) {
        }.togglePrivateBrowsingMode()

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            verifyPageContent(genericURL.content)
            longClickPageObject(composeTestRule, itemContainingText("content"))
            clickContextMenuItem("Private Search")
            verifyTabCounter("2", isPrivateBrowsingEnabled = true)
            verifyUrl("content")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326834
    @Ignore("Disabled after enabling the composable toolbar and main menu: https://bugzilla.mozilla.org/show_bug.cgi?id=2006295")
    @Test
    fun verifySelectAllPDFTextOptionTest() {
        val genericURL =
            mockWebServer.getGenericAsset(3)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(composeTestRule, itemWithText("PDF form file"))
            clickPageObject(composeTestRule, itemContainingText("Cancel"))
            waitForPageToLoad()
            longClickPageObject(composeTestRule, itemContainingText("Crossing"))
            clickContextMenuItem("Select all")
            clickContextMenuItem("Copy")
        }.openNavigationToolbar {
            openEditURLView()
        }

        searchScreen(composeTestRule) {
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
            mockWebServer.getGenericAsset(3)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(composeTestRule, itemWithText("PDF form file"))
            clickPageObject(composeTestRule, itemContainingText("Cancel"))
            longClickPageObject(composeTestRule, itemContainingText("Crossing"))
            clickContextMenuItem("Copy")
        }.openNavigationToolbar {
        }

        searchScreen(composeTestRule) {
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
            mockWebServer.getGenericAsset(3)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(composeTestRule, itemWithText("PDF form file"))
            clickPageObject(composeTestRule, itemContainingText("Cancel"))
            longClickPageObject(composeTestRule, itemContainingText("Crossing"))
        }.clickShareSelectedText {
            verifyAndroidShareLayout()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326836
    @SmokeTest
    @Test
    fun verifySearchPDFTextOptionTest() {
        val genericURL =
            mockWebServer.getGenericAsset(3)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(composeTestRule, itemWithText("PDF form file"))
            clickPageObject(composeTestRule, itemContainingText("Cancel"))
            longClickPageObject(composeTestRule, itemContainingText("Crossing"))
            clickContextMenuItem("Search")
            verifyUrl("Crossing")
            verifyTabCounter("2")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326837
    @Test
    fun verifyPrivateSearchPDFTextOptionTest() {
        val genericURL =
            mockWebServer.getGenericAsset(3)

        homeScreen(composeTestRule) {
        }.togglePrivateBrowsingMode()

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
            clickPageObject(composeTestRule, itemWithText("PDF form file"))
            clickPageObject(composeTestRule, itemContainingText("Cancel"))
            longClickPageObject(composeTestRule, itemContainingText("Crossing"))
            clickContextMenuItem("Private Search")
            verifyUrl("Crossing")
            verifyTabCounter("2", isPrivateBrowsingEnabled = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326813
    @Ignore("Disabled after enabling the composable toolbar and main menu: https://bugzilla.mozilla.org/show_bug.cgi?id=2006295")
    @Test
    fun verifyUrlBarTextSelectionOptionsTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openNavigationToolbar {
            longClickEditModeToolbar()
            verifyTextSelectionOptions("Open", "Cut", "Copy", "Share")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326814
    @Test
    fun verifyCopyUrlBarTextSelectionOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openNavigationToolbar {
        }
        searchScreen(retryableComposeTestRule.current) {
            longClickToolbar()
            clickContextMenuItem("Copy")
            clickClearButton()
            verifySearchBarPlaceholder("Search or enter address")
            longClickToolbar()
            clickContextMenuItem("Paste")
            verifyTypedToolbarText(genericURL.url.toString(), exists = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2326815
    @Ignore("Disabled after enabling the composable toolbar and main menu: https://bugzilla.mozilla.org/show_bug.cgi?id=2006295")
    @Test
    fun verifyCutUrlBarTextSelectionOptionTest() {
        val genericURL = mockWebServer.getGenericAsset(1)

        navigationToolbar(composeTestRule) {
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

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/414316
    @Ignore("Disabled after enabling the composable toolbar and main menu: https://bugzilla.mozilla.org/show_bug.cgi?id=2006295")
    @Test
    fun urlBarQuickActionsTest() {
        val firstWebsite = mockWebServer.getGenericAsset(1)
        val secondWebsite = mockWebServer.getGenericAsset(2)

        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(firstWebsite.url) {
            longClickToolbar()
            clickContextMenuItem("Copy")
        }
        navigationToolbar(composeTestRule) {
        }.enterURLAndEnterToBrowser(secondWebsite.url) {
            longClickToolbar()
            clickContextMenuItem("Paste")
        }
        searchScreen(composeTestRule) {
            verifyTypedToolbarText(firstWebsite.url.toString(), exists = true)
        }.dismissSearchBar {
        }
        browserScreen(composeTestRule) {
            verifyUrl(secondWebsite.url.toString())
            longClickToolbar()
            clickContextMenuItem("Paste & Go")
            verifyUrl(firstWebsite.url.toString())
        }
    }
}
