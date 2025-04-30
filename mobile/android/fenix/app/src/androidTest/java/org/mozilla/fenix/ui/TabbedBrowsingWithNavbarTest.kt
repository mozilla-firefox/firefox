package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.GleanMetrics.NavigationBar
import org.mozilla.fenix.customannotations.SkipLeaks
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.RetryTestRule
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.clickSnackbarButton
import org.mozilla.fenix.helpers.TestHelper.verifySnackBarText
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.browserScreen
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

class TabbedBrowsingWithNavbarTest : TestSetup() {
    @get:Rule(order = 0)
    val composeTestRule =
        AndroidComposeTestRule(
            HomeActivityIntentTestRule(
                skipOnboarding = true,
                isNavigationToolbarEnabled = true,
                isNavigationBarCFREnabled = false,
            ),
        ) { it.activity }

    @get:Rule(order = 1)
    val memoryLeaksRule = DetectMemoryLeaksRule()

    @Rule(order = 2)
    @JvmField
    val retryTestRule = RetryTestRule(3)

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903591
    @Test
    fun closingPrivateTabsFromNavbarTest() {
        val genericURL = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen { }.togglePrivateBrowsingMode(switchPBModeOn = true)
        navigationToolbar {
        }.enterURLAndEnterToBrowser(genericURL.url) {
        }.openTabDrawer(composeTestRule) {
            verifyExistingOpenTabs("Test_Page_1")
            closeTab()
            verifySnackBarText("Private tab closed")
            clickSnackbarButton(composeTestRule, "UNDO")
        }
        browserScreen {
            verifyTabCounter("1")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/903587
    @Test
    fun verifyPrivateTabsTrayWithOpenTabFromNavbarTest() {
        val website = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openTabDrawerFromRedesignedToolbar(composeTestRule) {
        }.toggleToPrivateTabs {
        }.openNewTab {
        }.submitQuery(website.url.toString()) {
        }.openTabDrawerFromRedesignedToolbar(composeTestRule) {
            verifyNormalBrowsingButtonIsSelected(false)
            verifyPrivateBrowsingButtonIsSelected(true)
            verifySyncedTabsButtonIsSelected(false)
            verifyThreeDotButton()
            verifyNormalTabCounter()
            verifyPrivateTabsList()
            verifyExistingOpenTabs(website.title)
            verifyTabCloseButton()
            verifyTabThumbnail()
            verifyFab()
        }
    }

    @Test
    fun tabsCounterShortcutMenuOptionFromNavbarInNormalModeTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            waitForPageToLoad()
        }
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
            verifyTabButtonShortcutMenuItems()
        }
    }

    @Test
    fun tabsCounterShortcutMenuOptionFromNavbarInPrivateModeTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {
        }.openTabDrawerFromRedesignedToolbar(composeTestRule) {
        }.toggleToPrivateTabs {
        }.openNewTab {
        }.submitQuery(defaultWebPage.url.toString()) {
            waitForPageToLoad()
        }
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
            verifyTabButtonShortcutMenuItems()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2343663
    @Test
    @SkipLeaks
    fun tabsCounterShortcutMenuNewPrivateTabFromNavbarTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {}
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
        }.openNewPrivateTabFromShortcutsMenu {
            verifySearchBarPlaceholder("Search or enter address")
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2343662
    @Ignore("Failing, see: https://bugzilla.mozilla.org/show_bug.cgi?id=1807268")
    @Test
    fun tabsCounterShortcutMenuNewTabFromNavbarTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {}
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
        }.openNewTabFromShortcutsMenu {
            verifySearchBarPlaceholder("Search or enter address")
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2344199
    @Test
    @SkipLeaks
    fun privateTabsCounterShortcutMenuNewPrivateTabFromNavbarTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {}.togglePrivateBrowsingMode(switchPBModeOn = true)
        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            waitForPageToLoad()
        }
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
        }.openNewPrivateTabFromShortcutsMenu {
            verifySearchBarPlaceholder("Search or enter address")
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = true)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2344198
    @Test
    @SkipLeaks
    fun privateTabsCounterShortcutMenuNewTabFromNavbarTest() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)

        homeScreen {}.togglePrivateBrowsingMode(switchPBModeOn = true)
        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) {
            verifyPageContent(defaultWebPage.content)
        }
        navigationToolbar {
        }.openTabButtonShortcutsMenu {
        }.openNewTabFromShortcutsMenu {
            verifySearchToolbar(isDisplayed = true)
        }.dismissSearchBar {
            verifyIfInPrivateOrNormalMode(privateBrowsingEnabled = false)
        }
    }

    @Test
    fun verifyTabsCounterShortcutMenuFromNavbarRecordsTelemetry() {
        val defaultWebPage = TestAssetHelper.getGenericAsset(mockWebServer, 1)
        navigationToolbar {
        }.enterURLAndEnterToBrowser(defaultWebPage.url) { }

        assertNull(NavigationBar.browserTabTrayLongTapped.testGetValue())
        navigationToolbar {
        }.openTabButtonShortcutsMenu { }
        assertNotNull(NavigationBar.browserTabTrayLongTapped.testGetValue())
    }
}
