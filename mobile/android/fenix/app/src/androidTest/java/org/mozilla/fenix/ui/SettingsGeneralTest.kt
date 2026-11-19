/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import androidx.compose.ui.test.junit4.v2.AndroidComposeTestRule as AndroidComposeTestRuleV2
import androidx.core.net.toUri
import androidx.core.os.LocaleListCompat
import androidx.preference.R as preferenceR
import org.junit.Ignore
import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.FenixApplication
import org.mozilla.fenix.R
import org.mozilla.fenix.customannotations.Converted
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.AppAndSystemHelper.registerAndCleanupIdlingResources
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithSystemLocaleChanged
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.FenixTestRule
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.RecyclerViewIdlingResource
import org.mozilla.fenix.helpers.TestAssetHelper.loremIpsumAsset
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.helpers.TestHelper.waitForAppWindowToBeUpdated
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.checkTextSizeOnWebsite
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar
import org.mozilla.fenix.ui.util.FRENCH_FOLLOW_DEVICE_LANGUAGE_OPTION
import org.mozilla.fenix.ui.util.FRENCH_LANGUAGE_HEADER
import org.mozilla.fenix.ui.util.FR_SETTINGS
import org.mozilla.fenix.ui.util.ROMANIAN_LANGUAGE_HEADER

/** Tests for verifying the General section of the Settings menu */
class SettingsGeneralTest {
    @get:Rule(order = 0) val fenixTestRule: FenixTestRule = FenixTestRule()

    private val mockWebServer
        get() = fenixTestRule.mockWebServer

    @get:Rule(order = 1)
    val composeTestRule =
        AndroidComposeTestRuleV2(HomeActivityIntentTestRule.withDefaultSettingsOverrides()) { it.activity }

    @get:Rule(order = 2) val memoryLeaksRule = DetectMemoryLeaksRule(composeTestRule = { composeTestRule })

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/2092697
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.SettingsGeneralTest#verifyGeneralSettingsItemsTest"],
        bug = 2075192,
        since = "2026-09",
    )
    @Critical
    @Test
    fun verifyGeneralSettingsItemsTest() {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {
                verifySettingsToolbar()
                verifyGeneralHeading()
                verifySearchButton()
                verifySettingsOptionSummary("Search", "Google")
                verifyTabsButton()
                verifySettingsOptionSummary("Tabs", "Close manually")
                verifyHomepageButton()
                verifySettingsOptionSummary("Homepage", "Open on homepage after four hours")
                verifyCustomizeButton()
                verifyLoginsAndPasswordsButton()
                verifyAutofillButton()
                verifyAccessibilityButton()
                verifyLanguageButton()
                verifyPageSummariesButton()
                verifySetAsDefaultBrowserButton()
                verifyDefaultBrowserToggle(false)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/344213
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.SettingsGeneralTest#verifyFontSizingChangeTest"],
        bug = 2062580,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun verifyFontSizingChangeTest() {
        // Goes through the settings and changes the default text on a webpage, then verifies if the text has changed.
        val fenixApp = composeTestRule.activity.applicationContext as FenixApplication
        val webpage = mockWebServer.loremIpsumAsset.url

        // This value will represent the text size percentage the webpage will scale to. The default value is 100%.
        val textSizePercentage = 180

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {}
            .openAccessibilitySubMenu {
                verifyFontSizingMenuItems(
                    composeTestRule,
                    isTheAutomaticFontSizingToggleChecked = true,
                    isTheFontSizingSliderEnabled = false,
                    isTheZoomOnAllWbsitesToggleChecked = false,
                )
                clickFontSizingSwitch()
                verifyFontSizingMenuItems(
                    composeTestRule,
                    isTheAutomaticFontSizingToggleChecked = false,
                    isTheFontSizingSliderEnabled = true,
                    isTheZoomOnAllWbsitesToggleChecked = false,
                )
                changeTextSizeSlider(textSizePercentage, composeTestRule)
                verifyTextSizePercentage(textSizePercentage, composeTestRule)
            }
            .goBack {}
            .goBack(composeTestRule) {}
        navigationToolbar(composeTestRule) {}
            .enterURLAndEnterToBrowser(webpage) {
                checkTextSizeOnWebsite(textSizePercentage, fenixApp.components)
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/516079
    @Converted(
        replacedBy =
            ["org.mozilla.fenix.ui.efficiency.tests.SettingsGeneralTest#setAppLanguageDifferentThanSystemLanguageTest"],
        bug = 2040932,
        since = "2026-05",
    )
    @SmokeTest
    @Test
    fun setAppLanguageDifferentThanSystemLanguageTest() {
        val enLanguageHeaderText = getStringResource(R.string.preferences_language)

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {
                waitForAppWindowToBeUpdated()
            }
            .openLanguageSubMenu {
                waitForAppWindowToBeUpdated()
                registerAndCleanupIdlingResources(
                    RecyclerViewIdlingResource(
                        composeTestRule.activity.findViewById(R.id.locale_list),
                        2,
                    )
                ) {
                    selectLanguage("Romanian")
                    verifyLanguageHeaderIsTranslated(ROMANIAN_LANGUAGE_HEADER)
                    selectLanguage("Français")
                    verifyLanguageHeaderIsTranslated(FRENCH_LANGUAGE_HEADER)
                    selectLanguage(FRENCH_FOLLOW_DEVICE_LANGUAGE_OPTION)
                    verifyLanguageHeaderIsTranslated(enLanguageHeaderText)
                }
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/516080
    @Test
    fun searchInLanguagesListTest() {
        val systemLocaleDefault = getStringResource(R.string.default_locale_text)

        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {
                waitForAppWindowToBeUpdated()
                registerAndCleanupIdlingResources(
                    RecyclerViewIdlingResource(composeTestRule.activity.findViewById(preferenceR.id.recycler_view), 1)
                ) {
                    verifyLanguageButton()
                }
            }
            .openLanguageSubMenu {
                verifyLanguageListIsDisplayed()
                openSearchBar()
                typeInSearchBar("French")
                verifySearchResultsContains(systemLocaleDefault)
                selectLanguageSearchResult("Français")
                verifyLanguageHeaderIsTranslated(FRENCH_LANGUAGE_HEADER)
                verifyLanguageListIsDisplayed()
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/516078
    @Ignore("Failing: https://bugzilla.mozilla.org/show_bug.cgi?id=2021700")
    @Test
    fun verifyFollowDeviceLanguageTest() {
        val frenchLocale = LocaleListCompat.forLanguageTags("fr")

        runWithSystemLocaleChanged(frenchLocale) {
            navigationToolbar(composeTestRule) {}
                .enterURLAndEnterToBrowser("test".toUri()) {}
                .openThreeDotMenu {}
                .clickSettingsButton(localizedText = FR_SETTINGS) {
                    waitForAppWindowToBeUpdated()
                    registerAndCleanupIdlingResources(
                        RecyclerViewIdlingResource(
                            composeTestRule.activity.findViewById(preferenceR.id.recycler_view),
                            1,
                        )
                    ) {
                        verifyLanguageButton(localizedText = FRENCH_LANGUAGE_HEADER)
                    }
                }
                .openLanguageSubMenu(localizedText = FRENCH_LANGUAGE_HEADER) {
                    verifyLanguageHeaderIsTranslated(FRENCH_LANGUAGE_HEADER)
                    verifySelectedLanguage(FRENCH_FOLLOW_DEVICE_LANGUAGE_OPTION)
                }
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1360557
    @Test
    fun tabsSettingsMenuItemsTest() {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {
                verifyTabsButton()
                verifySettingsOptionSummary("Tabs", "Close manually")
            }
            .openTabsSubMenu {
                verifyTabLayoutOptions()
                verifyCloseTabsOptions()
                verifyTabOrganizationOptions()
                verifySelectedCloseTabsOption("Never")
                clickClosedTabsOption("After one day")
                verifySelectedCloseTabsOption("After one day")
            }
            .goBack {
                verifySettingsOptionSummary("Tabs", "Close after one day")
            }
            .openTabsSubMenu {
                clickClosedTabsOption("After one week")
                verifySelectedCloseTabsOption("After one week")
            }
            .goBack {
                verifySettingsOptionSummary("Tabs", "Close after one week")
            }
            .openTabsSubMenu {
                clickClosedTabsOption("After one month")
                verifySelectedCloseTabsOption("After one month")
            }
            .goBack {
                verifySettingsOptionSummary("Tabs", "Close after one month")
            }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/243583
    // For API>23
    // Verifies the default browser switch opens the system default apps menu.
    @Converted(
        replacedBy = ["org.mozilla.fenix.ui.efficiency.tests.SettingsGeneralTest#changeDefaultBrowserSetting"],
        bug = 2062580,
        since = "2026-08",
    )
    @SmokeTest
    @Test
    fun changeDefaultBrowserSetting() {
        homeScreen(composeTestRule) {}
            .openThreeDotMenu {}
            .clickSettingsButton {
                verifyDefaultBrowserToggle(false)
                clickDefaultBrowserSwitch()
                verifyAndroidDefaultAppsMenuAppears()
            }
        // Dismiss the request
        mDevice.pressBack()
    }
}
