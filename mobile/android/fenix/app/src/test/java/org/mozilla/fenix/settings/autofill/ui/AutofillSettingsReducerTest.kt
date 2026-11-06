/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.autofill.ui

import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCard
import org.junit.Assert.assertEquals
import org.junit.Test

class AutofillSettingsReducerTest {

    @Test
    fun `WHEN addresses are loaded THEN they are added to state`() {
        val state = AutofillSettingsState.default
        val addresses =
            List(5) {
                Address(
                    guid = "$it",
                    name = "name$it",
                    organization = "organization$it",
                    streetAddress = "street$it",
                    addressLevel1 = "",
                    addressLevel2 = "",
                    addressLevel3 = "",
                    postalCode = "40066$it",
                    country = "country$it",
                    tel = "123456$it",
                    email = "user$it@gmail.com",
                    timeCreated = System.currentTimeMillis(),
                    timeLastUsed = System.currentTimeMillis(),
                    timeLastModified = System.currentTimeMillis(),
                    timesUsed = 1L,
                )
            }

        val result =
            autofillSettingsReducer(
                state,
                UpdateAddresses(addresses = addresses),
            )

        val expected = state.copy(addresses = addresses)
        assertEquals(expected, result)
    }

    @Test
    fun `WHEN creditCards are loaded THEN they are added to state`() {
        val state = AutofillSettingsState.default
        val creditCards =
            List(5) {
                CreditCard(
                    guid = "$it",
                    billingName = "name$it",
                    cardNumber = "4111111111111111",
                    cardNumberLast4 = "211$it",
                    expiryMonth = 12,
                    expiryYear = 2029,
                    cardType = "Visa",
                    timeCreated = System.currentTimeMillis(),
                    timeLastUsed = System.currentTimeMillis(),
                    timeLastModified = System.currentTimeMillis(),
                    timesUsed = 1L,
                    cardCvv = null,
                )
            }

        val result =
            autofillSettingsReducer(
                state,
                UpdateCreditCards(creditCards = creditCards),
            )

        val expected = state.copy(creditCards = creditCards)
        assertEquals(expected, result)
    }

    @Test
    fun `WHEN the back button is clicked THEN remove all addresses from state`() {
        val addresses =
            List(5) {
                Address(
                    guid = "$it",
                    name = "name$it",
                    organization = "organization$it",
                    streetAddress = "street$it",
                    addressLevel1 = "",
                    addressLevel2 = "",
                    addressLevel3 = "",
                    postalCode = "40066$it",
                    country = "country$it",
                    tel = "123456$it",
                    email = "user$it@gmail.com",
                    timeCreated = System.currentTimeMillis(),
                    timeLastUsed = System.currentTimeMillis(),
                    timeLastModified = System.currentTimeMillis(),
                    timesUsed = 1L,
                )
            }

        val state = AutofillSettingsState.default.copy(addresses = addresses)
        val result = autofillSettingsReducer(state, AutofillSettingsBackClicked)

        val expectedResult = state.copy(addresses = listOf())

        assertEquals(result, expectedResult)
    }

    @Test
    fun `WHEN the back button is clicked THEN remove all credit cards from state`() {
        val creditCards =
            List(5) {
                CreditCard(
                    guid = "$it",
                    billingName = "name$it",
                    cardNumber = "4111111111111111",
                    cardNumberLast4 = "211$it",
                    expiryMonth = 12,
                    expiryYear = 2029,
                    cardType = "Visa",
                    timeCreated = System.currentTimeMillis(),
                    timeLastUsed = System.currentTimeMillis(),
                    timeLastModified = System.currentTimeMillis(),
                    timesUsed = 1L,
                    cardCvv = null,
                )
            }

        val state = AutofillSettingsState.default.copy(creditCards = creditCards)
        val result = autofillSettingsReducer(state, AutofillSettingsBackClicked)

        val expectedResult = state.copy(creditCards = listOf())

        assertEquals(result, expectedResult)
    }

    @Test
    fun `WHEN save and fill addresses option is changed THEN this is reflected in the state`() {
        val state = AutofillSettingsState.default.copy(saveFillAddresses = true)
        val result = autofillSettingsReducer(state, ChangeAddressSaveFillPreference(false))

        val expectedResult = state.copy(saveFillAddresses = false)

        assertEquals(result, expectedResult)
    }

    @Test
    fun `WHEN save and fill credit cards option is changed THEN this is reflected in the state`() {
        val state = AutofillSettingsState.default.copy(saveFillAddresses = false)
        val result = autofillSettingsReducer(state, ChangeAddressSaveFillPreference(true))

        val expectedResult = state.copy(saveFillAddresses = true)

        assertEquals(result, expectedResult)
    }

    @Test
    fun `WHEN sync addresses option is changed THEN this is reflected in the state`() {
        val state = AutofillSettingsState.default.copy(syncAddresses = true)
        val result = autofillSettingsReducer(state, UpdateAddressesSyncStatus(false))

        val expectedResult = state.copy(syncAddresses = false)

        assertEquals(result, expectedResult)
    }

    @Test
    fun `WHEN sync credit cards option is changed THEN this is reflected in the state`() {
        val state = AutofillSettingsState.default.copy(syncCreditCards = false)
        val result = autofillSettingsReducer(state, UpdateCreditCardsSyncStatus(true))

        val expectedResult = state.copy(syncCreditCards = true)

        assertEquals(result, expectedResult)
    }

    @Test
    fun `WHEN account authentication succeeds THEN this is reflected in the state`() {
        val state = AutofillSettingsState.default.copy(accountAuthState = AccountAuthState.LoggedOut)
        val result = autofillSettingsReducer(state, AccountAuthenticationAction.Authenticated)

        val expectedResult = state.copy(accountAuthState = AccountAuthState.Authenticated)

        assertEquals(result, expectedResult)
    }

    @Test
    fun `WHEN account authentication encounters a problem THEN this is reflected in the state`() {
        val state = AutofillSettingsState.default.copy(accountAuthState = AccountAuthState.Authenticated)
        val result = autofillSettingsReducer(state, AccountAuthenticationAction.Failed)

        val expectedResult = state.copy(accountAuthState = AccountAuthState.NeedsReauthentication)

        assertEquals(result, expectedResult)
    }

    @Test
    fun `WHEN account authentication fails THEN this is reflected in the state`() {
        val state = AutofillSettingsState.default.copy(accountAuthState = AccountAuthState.Authenticated)
        val result = autofillSettingsReducer(state, AccountAuthenticationAction.NotAuthenticated)

        val expectedResult = state.copy(accountAuthState = AccountAuthState.LoggedOut)

        assertEquals(result, expectedResult)
    }
}
