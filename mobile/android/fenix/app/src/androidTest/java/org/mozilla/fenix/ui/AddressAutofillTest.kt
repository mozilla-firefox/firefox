/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui

import org.junit.Rule
import org.junit.Test
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.HomeActivityIntentTestRule
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResId
import org.mozilla.fenix.helpers.MatcherHelper.itemWithResIdContainingText
import org.mozilla.fenix.helpers.TestAssetHelper
import org.mozilla.fenix.helpers.TestHelper.exitMenu
import org.mozilla.fenix.helpers.TestHelper.packageName
import org.mozilla.fenix.helpers.TestSetup
import org.mozilla.fenix.helpers.perf.DetectMemoryLeaksRule
import org.mozilla.fenix.ui.robots.autofillScreen
import org.mozilla.fenix.ui.robots.clickPageObject
import org.mozilla.fenix.ui.robots.homeScreen
import org.mozilla.fenix.ui.robots.navigationToolbar

class AddressAutofillTest : TestSetup() {
    object FirstAddressAutofillDetails {
        var navigateToAutofillSettings = true
        var isAddressAutofillEnabled = true
        var userHasSavedAddress = false
        var name = "Mozilla Fenix Firefox"
        var streetAddress = "Harrison Street"
        var city = "San Francisco"
        var state = "Alaska"
        var zipCode = "94105"
        var country = "United States"
        var phoneNumber = "555-5555"
        var emailAddress = "foo@bar.com"
    }

    object SecondAddressAutofillDetails {
        var navigateToAutofillSettings = false
        var name = "Android Test Name"
        var streetAddress = "Fort Street"
        var city = "San Jose"
        var state = "Arizona"
        var zipCode = "95141"
        var country = "United States"
        var phoneNumber = "777-7777"
        var emailAddress = "fuu@bar.org"
    }

    @get:Rule
    val activityIntentTestRule = HomeActivityIntentTestRule.withDefaultSettingsOverrides()

    @get:Rule
    val memoryLeaksRule = DetectMemoryLeaksRule()

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836845
    @SmokeTest
    @Test
    fun verifyAddressAutofillTest() {
        val addressFormPage =
            TestAssetHelper.getAddressFormAsset(mockWebServer)

        autofillScreen {
            fillAndSaveAddress(
                navigateToAutofillSettings = FirstAddressAutofillDetails.navigateToAutofillSettings,
                isAddressAutofillEnabled = FirstAddressAutofillDetails.isAddressAutofillEnabled,
                userHasSavedAddress = FirstAddressAutofillDetails.userHasSavedAddress,
                name = FirstAddressAutofillDetails.name,
                streetAddress = FirstAddressAutofillDetails.streetAddress,
                city = FirstAddressAutofillDetails.city,
                state = FirstAddressAutofillDetails.state,
                zipCode = FirstAddressAutofillDetails.zipCode,
                country = FirstAddressAutofillDetails.country,
                phoneNumber = FirstAddressAutofillDetails.phoneNumber,
                emailAddress = FirstAddressAutofillDetails.emailAddress,
            )
        }.goBack {
        }.goBack {
        }
        navigationToolbar {
        }.enterURLAndEnterToBrowser(addressFormPage.url) {
            clickPageObject(itemWithResId("streetAddress"))
            clickSelectAddressButton()
            clickPageObject(
                itemWithResIdContainingText(
                    "$packageName:id/address_name",
                    "Harrison Street",
                ),
            )
            verifyAutofilledAddress("Harrison Street")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836856
    @SmokeTest
    @Test
    fun deleteSavedAddressTest() {
        autofillScreen {
            fillAndSaveAddress(
                navigateToAutofillSettings = FirstAddressAutofillDetails.navigateToAutofillSettings,
                isAddressAutofillEnabled = FirstAddressAutofillDetails.isAddressAutofillEnabled,
                userHasSavedAddress = FirstAddressAutofillDetails.userHasSavedAddress,
                name = FirstAddressAutofillDetails.name,
                streetAddress = FirstAddressAutofillDetails.streetAddress,
                city = FirstAddressAutofillDetails.city,
                state = FirstAddressAutofillDetails.state,
                zipCode = FirstAddressAutofillDetails.zipCode,
                country = FirstAddressAutofillDetails.country,
                phoneNumber = FirstAddressAutofillDetails.phoneNumber,
                emailAddress = FirstAddressAutofillDetails.emailAddress,
            )
            clickManageAddressesButton()
            clickSavedAddress("Mozilla")
            clickDeleteAddressButton()
            clickCancelDeleteAddressButton()
            clickDeleteAddressButton()
            clickConfirmDeleteAddressButton()
            verifyAddAddressButton()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836840
    @Test
    fun verifyAddAddressViewTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            clickAddAddressButton()
            verifyAddAddressView()
        }.goBackToAutofillSettings {
            verifyAutofillToolbarTitle()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836841
    @Test
    fun verifyEditAddressViewTest() {
        autofillScreen {
            fillAndSaveAddress(
                navigateToAutofillSettings = FirstAddressAutofillDetails.navigateToAutofillSettings,
                isAddressAutofillEnabled = FirstAddressAutofillDetails.isAddressAutofillEnabled,
                userHasSavedAddress = FirstAddressAutofillDetails.userHasSavedAddress,
                name = FirstAddressAutofillDetails.name,
                streetAddress = FirstAddressAutofillDetails.streetAddress,
                city = FirstAddressAutofillDetails.city,
                state = FirstAddressAutofillDetails.state,
                zipCode = FirstAddressAutofillDetails.zipCode,
                country = FirstAddressAutofillDetails.country,
                phoneNumber = FirstAddressAutofillDetails.phoneNumber,
                emailAddress = FirstAddressAutofillDetails.emailAddress,
            )
            clickManageAddressesButton()
            clickSavedAddress("Mozilla")
            verifyEditAddressView()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836839
    @Test
    fun verifyAddressAutofillToggleTest() {
        val addressFormPage =
            TestAssetHelper.getAddressFormAsset(mockWebServer)

        autofillScreen {
            fillAndSaveAddress(
                navigateToAutofillSettings = FirstAddressAutofillDetails.navigateToAutofillSettings,
                isAddressAutofillEnabled = FirstAddressAutofillDetails.isAddressAutofillEnabled,
                userHasSavedAddress = FirstAddressAutofillDetails.userHasSavedAddress,
                name = FirstAddressAutofillDetails.name,
                streetAddress = FirstAddressAutofillDetails.streetAddress,
                city = FirstAddressAutofillDetails.city,
                state = FirstAddressAutofillDetails.state,
                zipCode = FirstAddressAutofillDetails.zipCode,
                country = FirstAddressAutofillDetails.country,
                phoneNumber = FirstAddressAutofillDetails.phoneNumber,
                emailAddress = FirstAddressAutofillDetails.emailAddress,
            )
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(addressFormPage.url) {
            clickPageObject(itemWithResId("streetAddress"))
            verifySelectAddressButtonExists(true)
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            clickSaveAndAutofillAddressesOption()
            verifyAddressAutofillSection(false, true)
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(addressFormPage.url) {
            clickPageObject(itemWithResId("streetAddress"))
            verifySelectAddressButtonExists(false)
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836847
    @Test
    fun verifyManageAddressesPromptOptionTest() {
        val addressFormPage =
            TestAssetHelper.getAddressFormAsset(mockWebServer)

        autofillScreen {
            fillAndSaveAddress(
                navigateToAutofillSettings = FirstAddressAutofillDetails.navigateToAutofillSettings,
                isAddressAutofillEnabled = FirstAddressAutofillDetails.isAddressAutofillEnabled,
                userHasSavedAddress = FirstAddressAutofillDetails.userHasSavedAddress,
                name = FirstAddressAutofillDetails.name,
                streetAddress = FirstAddressAutofillDetails.streetAddress,
                city = FirstAddressAutofillDetails.city,
                state = FirstAddressAutofillDetails.state,
                zipCode = FirstAddressAutofillDetails.zipCode,
                country = FirstAddressAutofillDetails.country,
                phoneNumber = FirstAddressAutofillDetails.phoneNumber,
                emailAddress = FirstAddressAutofillDetails.emailAddress,
            )
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(addressFormPage.url) {
            clickPageObject(itemWithResId("streetAddress"))
            clickSelectAddressButton()
        }.clickManageAddressButton {
            verifyAutofillToolbarTitle()
        }.goBackToBrowser {
            verifySaveLoginPromptIsNotDisplayed()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836849
    @Test
    fun verifyMultipleAddressesSelectionTest() {
        val addressFormPage =
            TestAssetHelper.getAddressFormAsset(mockWebServer)

        autofillScreen {
            fillAndSaveAddress(
                navigateToAutofillSettings = FirstAddressAutofillDetails.navigateToAutofillSettings,
                isAddressAutofillEnabled = FirstAddressAutofillDetails.isAddressAutofillEnabled,
                userHasSavedAddress = FirstAddressAutofillDetails.userHasSavedAddress,
                name = FirstAddressAutofillDetails.name,
                streetAddress = FirstAddressAutofillDetails.streetAddress,
                city = FirstAddressAutofillDetails.city,
                state = FirstAddressAutofillDetails.state,
                zipCode = FirstAddressAutofillDetails.zipCode,
                country = FirstAddressAutofillDetails.country,
                phoneNumber = FirstAddressAutofillDetails.phoneNumber,
                emailAddress = FirstAddressAutofillDetails.emailAddress,
            )
            clickManageAddressesButton()
            clickAddAddressButton()
            fillAndSaveAddress(
                navigateToAutofillSettings = SecondAddressAutofillDetails.navigateToAutofillSettings,
                name = SecondAddressAutofillDetails.name,
                streetAddress = SecondAddressAutofillDetails.streetAddress,
                city = SecondAddressAutofillDetails.city,
                state = SecondAddressAutofillDetails.state,
                zipCode = SecondAddressAutofillDetails.zipCode,
                country = SecondAddressAutofillDetails.country,
                phoneNumber = SecondAddressAutofillDetails.phoneNumber,
                emailAddress = SecondAddressAutofillDetails.emailAddress,
            )
            verifyManageAddressesToolbarTitle()
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(addressFormPage.url) {
            clickPageObject(itemWithResId("streetAddress"))
            clickSelectAddressButton()
            clickPageObject(
                itemWithResIdContainingText(
                    "$packageName:id/address_name",
                    "Harrison Street",
                ),
            )
            verifyAutofilledAddress("Harrison Street")
            clearAddressForm()
            clickPageObject(itemWithResId("streetAddress"))
            clickSelectAddressButton()
            clickPageObject(
                itemWithResIdContainingText(
                    "$packageName:id/address_name",
                    "Fort Street",
                ),
            )
            verifyAutofilledAddress("Fort Street")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836850
    @Test
    fun verifySavedAddressCanBeEditedTest() {
        autofillScreen {
            fillAndSaveAddress(
                navigateToAutofillSettings = FirstAddressAutofillDetails.navigateToAutofillSettings,
                isAddressAutofillEnabled = FirstAddressAutofillDetails.isAddressAutofillEnabled,
                userHasSavedAddress = FirstAddressAutofillDetails.userHasSavedAddress,
                name = FirstAddressAutofillDetails.name,
                streetAddress = FirstAddressAutofillDetails.streetAddress,
                city = FirstAddressAutofillDetails.city,
                state = FirstAddressAutofillDetails.state,
                zipCode = FirstAddressAutofillDetails.zipCode,
                country = FirstAddressAutofillDetails.country,
                phoneNumber = FirstAddressAutofillDetails.phoneNumber,
                emailAddress = FirstAddressAutofillDetails.emailAddress,
            )
            clickManageAddressesButton()
            clickSavedAddress("Mozilla")
            fillAndSaveAddress(
                navigateToAutofillSettings = SecondAddressAutofillDetails.navigateToAutofillSettings,
                name = SecondAddressAutofillDetails.name,
                streetAddress = SecondAddressAutofillDetails.streetAddress,
                city = SecondAddressAutofillDetails.city,
                state = SecondAddressAutofillDetails.state,
                zipCode = SecondAddressAutofillDetails.zipCode,
                country = SecondAddressAutofillDetails.country,
                phoneNumber = SecondAddressAutofillDetails.phoneNumber,
                emailAddress = SecondAddressAutofillDetails.emailAddress,
            )
            verifyManageAddressesToolbarTitle()
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836848
    @Test
    fun verifyStateFieldUpdatesInAccordanceWithCountryFieldTest() {
        homeScreen {
        }.openThreeDotMenu {
        }.openSettings {
        }.openAutofillSubMenu {
            verifyAddressAutofillSection(true, false)
            clickAddAddressButton()
            verifyCountryOption("United States")
            verifyStateOption("Alabama")
            verifyCountryOptions("Canada", "United States")
            clickCountryOption("Canada")
            verifyStateOption("Alberta")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836858
    @Test
    fun verifyFormFieldCanBeFilledManuallyTest() {
        val addressFormPage =
            TestAssetHelper.getAddressFormAsset(mockWebServer)

        autofillScreen {
            fillAndSaveAddress(
                navigateToAutofillSettings = FirstAddressAutofillDetails.navigateToAutofillSettings,
                isAddressAutofillEnabled = FirstAddressAutofillDetails.isAddressAutofillEnabled,
                userHasSavedAddress = FirstAddressAutofillDetails.userHasSavedAddress,
                name = FirstAddressAutofillDetails.name,
                streetAddress = FirstAddressAutofillDetails.streetAddress,
                city = FirstAddressAutofillDetails.city,
                state = FirstAddressAutofillDetails.state,
                zipCode = FirstAddressAutofillDetails.zipCode,
                country = FirstAddressAutofillDetails.country,
                phoneNumber = FirstAddressAutofillDetails.phoneNumber,
                emailAddress = FirstAddressAutofillDetails.emailAddress,
            )
        }

        exitMenu()

        navigationToolbar {
        }.enterURLAndEnterToBrowser(addressFormPage.url) {
            clickPageObject(itemWithResId("streetAddress"))
            clickSelectAddressButton()
            clickPageObject(
                itemWithResIdContainingText(
                    "$packageName:id/address_name",
                    "Harrison Street",
                ),
            )
            verifyAutofilledAddress("Harrison Street")
            setTextForApartmentTextBox("Ap. 07")
            verifyManuallyFilledAddress("Ap. 07")
        }
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/1836838
    @Test
    fun verifyAutofillAddressSectionTest() {
        autofillScreen {
            fillAndSaveAddress(
                navigateToAutofillSettings = FirstAddressAutofillDetails.navigateToAutofillSettings,
                isAddressAutofillEnabled = FirstAddressAutofillDetails.isAddressAutofillEnabled,
                userHasSavedAddress = FirstAddressAutofillDetails.userHasSavedAddress,
                name = FirstAddressAutofillDetails.name,
                streetAddress = FirstAddressAutofillDetails.streetAddress,
                city = FirstAddressAutofillDetails.city,
                state = FirstAddressAutofillDetails.state,
                zipCode = FirstAddressAutofillDetails.zipCode,
                country = FirstAddressAutofillDetails.country,
                phoneNumber = FirstAddressAutofillDetails.phoneNumber,
                emailAddress = FirstAddressAutofillDetails.emailAddress,
            )
            verifyAddressAutofillSection(true, true)
            clickManageAddressesButton()
            verifyManageAddressesSection(
                "Mozilla",
                "Fenix",
                "Firefox",
                "Harrison Street",
                "San Francisco",
                "Alaska",
                "94105",
                "US",
                "555-5555",
                "foo@bar.com",
            )
        }
    }
}
