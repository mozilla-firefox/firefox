/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.tests

import org.junit.Test
import org.mozilla.fenix.customannotations.Critical
import org.mozilla.fenix.customannotations.SmokeTest
import org.mozilla.fenix.helpers.TestAssetHelper.addressFormAsset
import org.mozilla.fenix.helpers.TestAssetHelper.waitingTimeLong
import org.mozilla.fenix.ui.efficiency.data.AddressTestData
import org.mozilla.fenix.ui.efficiency.helpers.BaseTest
import org.mozilla.fenix.ui.efficiency.selectors.BrowserPageSelectors
import org.mozilla.fenix.ui.efficiency.selectors.SettingsAutofillSelectors

class AddressAutofillTest : BaseTest() {
    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3205329
    @SmokeTest
    @Test
    fun verifyAddressAutofillTest() {
        val addressFormPage = mockWebServer.addressFormAsset
        val address = AddressTestData.FIRST

        on.settingsAutofill.navigateToPage().fillAndSaveAddress(address)

        on.browserPage
            .navigateToPage(addressFormPage.url.toString())
            .clickAddressFormStreetField()
            .clickSelectAddressButton()
            .clickAddressSuggestion(address.streetAddress)
            .verifyAutofilledAddress(address.streetAddress)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/3205332
    @SmokeTest
    @Test
    fun deleteSavedAddressTest() {
        val address = AddressTestData.FIRST

        on.settingsAutofill
            .navigateToPage()
            .fillAndSaveAddress(address)
            .mozClick(SettingsAutofillSelectors.MANAGE_ADDRESSES_BUTTON)
            .mozVerify(SettingsAutofillSelectors.SAVED_ADDRESS(address.name))
            .mozClick(SettingsAutofillSelectors.SAVED_ADDRESS(address.name))
            .mozVerify(SettingsAutofillSelectors.DELETE_ADDRESS_TOOLBAR_BUTTON)
            .mozClick(SettingsAutofillSelectors.DELETE_ADDRESS_TOOLBAR_BUTTON)
            .mozClick(SettingsAutofillSelectors.CANCEL_DELETE_ADDRESS_BUTTON)
            .mozClick(SettingsAutofillSelectors.DELETE_ADDRESS_TOOLBAR_BUTTON)
            .mozClick(SettingsAutofillSelectors.CONFIRM_DELETE_ADDRESS_BUTTON)
            .mozVerify(SettingsAutofillSelectors.ADD_ADDRESS_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4249427
    @Critical
    @Test
    fun verifyTheAddressCaptureNotNowButtonTest() {
        val addressFormPage = mockWebServer.addressFormAsset
        val address = AddressTestData.FIRST

        on.browserPage
            .navigateToPage(addressFormPage.url.toString())
            .fillAddressForm(address)
            .mozClick(BrowserPageSelectors.SUBMIT_LOGIN_BUTTON)
            .mozVerify(BrowserPageSelectors.SAVE_ADDRESS_PROMPT, timeout = waitingTimeLong)
            .mozClick(BrowserPageSelectors.SAVE_ADDRESS_PROMPT_NOT_NOW_BUTTON)
        on.settingsAutofill.navigateToPage().mozVerify(SettingsAutofillSelectors.ADD_ADDRESS_BUTTON)
    }

    // TestRail link: https://mozilla.testrail.io/index.php?/cases/view/4249430
    @Critical
    @Test
    fun verifyTheUpdateAddressCaptureTest() {
        val addressFormPage = mockWebServer.addressFormAsset
        val address = AddressTestData.FIRST

        on.settingsAutofill.navigateToPage().fillAndSaveAddress(address)
        on.browserPage
            .navigateToPage(addressFormPage.url.toString())
            .fillAddressForm(address)
            .mozClearAndEnterText("Apt 5", BrowserPageSelectors.ADDRESS_APARTMENT_WEB_FIELD)
            .mozClick(BrowserPageSelectors.SUBMIT_LOGIN_BUTTON)
            .mozVerify(BrowserPageSelectors.UPDATE_ADDRESS_PROMPT, timeout = waitingTimeLong)
            .mozClick(BrowserPageSelectors.UPDATE_ADDRESS_PROMPT_NOT_NOW_BUTTON)
        on.browserPage
            .fillAddressForm(address)
            .mozClearAndEnterText("Apt 5", BrowserPageSelectors.ADDRESS_APARTMENT_WEB_FIELD)
            .mozClick(BrowserPageSelectors.SUBMIT_LOGIN_BUTTON)
            .mozVerify(BrowserPageSelectors.UPDATE_ADDRESS_PROMPT, timeout = waitingTimeLong)
            .mozClick(BrowserPageSelectors.UPDATE_ADDRESS_PROMPT_UPDATE_BUTTON)
        on.settingsAutofill
            .navigateToPage()
            .mozClick(SettingsAutofillSelectors.MANAGE_ADDRESSES_BUTTON)
            .mozVerify(SettingsAutofillSelectors.SAVED_ADDRESS(address.name))
            .mozVerify(
                SettingsAutofillSelectors.SAVED_ADDRESS_DETAIL(
                    "Harrison Street Apt 5, San Francisco, AK, US, 94105, 555-5555, foo@bar.com"
                )
            )
    }
}
