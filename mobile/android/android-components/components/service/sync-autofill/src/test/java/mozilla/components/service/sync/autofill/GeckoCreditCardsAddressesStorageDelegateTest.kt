/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.runTest
import mozilla.appservices.RustComponentsInitializer
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCard
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.CreditCardValidationDelegate
import mozilla.components.concept.storage.NewCreditCardFields
import mozilla.components.concept.storage.UpdatableAddressFields
import mozilla.components.concept.storage.UpdatableCreditCardFields
import mozilla.components.lib.dataprotect.SecureAbove22Preferences
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class GeckoCreditCardsAddressesStorageDelegateTest {

    private lateinit var storage: AutofillCreditCardsAddressesStorage
    private lateinit var securePrefs: SecureAbove22Preferences
    private lateinit var delegate: GeckoCreditCardsAddressesStorageDelegate
    private val validationDelegate: DefaultCreditCardValidationDelegate = mock()

    init {
        testContext.getDatabasePath(AUTOFILL_DB_NAME)!!.parentFile!!.mkdirs()
    }

    @Before
    fun before() {
        RustComponentsInitializer.init()
        // forceInsecure is set in the tests because a keystore wouldn't be configured in the test environment.
        securePrefs = SecureAbove22Preferences(testContext, "autofill", forceInsecure = true)
        storage = spy(AutofillCreditCardsAddressesStorage(testContext, lazy { securePrefs }))
        delegate = GeckoCreditCardsAddressesStorageDelegate(lazy { storage }, validationDelegate)
    }

    @Test
    fun `GIVEN a newly added credit card WHEN it is read back THEN it contains the plain credit card number`() =
        runTest {
            val creditCardFields =
                NewCreditCardFields(
                    billingName = "Jon Doe",
                    cardNumber = "4111111111111111",
                    expiryMonth = 12,
                    expiryYear = 2028,
                    cardType = "amex",
                    cardCvv = null,
                )
            val creditCard = storage.addCreditCard(creditCardFields)

            assertEquals("4111111111111111", creditCard.cardNumber)
        }

    @Test
    fun `GIVEN autofill enabled WHEN onCreditCardsFetch is called THEN it returns all stored cards`() = runTest {
        val storage: AutofillCreditCardsAddressesStorage = mock()
        val storedCards = listOf<CreditCard>(mock())
        doReturn(storedCards).`when`(storage).getAllCreditCards()
        delegate =
            GeckoCreditCardsAddressesStorageDelegate(
                lazy { storage },
                isCreditCardAutofillEnabled = { true },
            )

        val result = delegate.onCreditCardsFetch()

        verify(storage, times(1)).getAllCreditCards()
        assertEquals(storedCards, result)
    }

    @Test
    fun `GIVEN autofill disabled WHEN onCreditCardsFetch is called THEN it returns an empty list of cards`() = runTest {
        val storage: AutofillCreditCardsAddressesStorage = mock()
        val storedCards = listOf<CreditCard>(mock())
        doReturn(storedCards).`when`(storage).getAllCreditCards()
        delegate =
            GeckoCreditCardsAddressesStorageDelegate(
                lazy { storage },
                isCreditCardAutofillEnabled = { false },
            )

        val result = delegate.onCreditCardsFetch()

        verify(storage, never()).getAllCreditCards()
        assertEquals(emptyList<CreditCard>(), result)
    }

    @Test
    fun `GIVEN a new credit card WHEN onCreditCardSave is called THEN it adds a new credit card in storage`() {
        runTest {
            val billingName = "Jon Doe"
            val cardNumber = "4111111111111111"
            val expiryMonth = 12L
            val expiryYear = 2028L
            val cardType = "amex"
            val cardCvv = "123"

            val creditCardEntry =
                CreditCardEntry(
                    name = billingName,
                    number = cardNumber,
                    expiryMonth = expiryMonth.toString(),
                    expiryYear = expiryYear.toString(),
                    cardType = cardType,
                    cardCvv = cardCvv,
                )

            doReturn(CreditCardValidationDelegate.Result.CanBeCreated)
                .`when`(validationDelegate)
                .shouldCreateOrUpdate(creditCardEntry)

            delegate.onCreditCardSave(creditCardEntry)

            verify(storage, times(1))
                .addCreditCard(
                    creditCardFields =
                        NewCreditCardFields(
                            billingName = billingName,
                            cardNumber = cardNumber,
                            expiryMonth = expiryMonth,
                            expiryYear = expiryYear,
                            cardType = cardType,
                            cardCvv = cardCvv,
                        )
                )
        }
    }

    @Test
    fun `GIVEN an existing credit card WHEN onCreditCardSave is called THEN it updates the existing credit card in storage`() {
        runTest {
            val billingName = "Jon Doe"
            val cardNumber = "4111111111111111"
            val expiryMonth = 12L
            val expiryYear = 2028L
            val cardType = "amex"
            val cardCvv = "123"

            val creditCardEntry =
                CreditCardEntry(
                    name = billingName,
                    number = cardNumber,
                    expiryMonth = expiryMonth.toString(),
                    expiryYear = expiryYear.toString(),
                    cardType = cardType,
                    cardCvv = cardCvv,
                )

            val creditCard =
                storage.addCreditCard(
                    NewCreditCardFields(
                        billingName = billingName,
                        cardNumber = cardNumber,
                        expiryMonth = expiryMonth,
                        expiryYear = expiryYear,
                        cardType = cardType,
                        cardCvv = null,
                    )
                )
            doReturn(CreditCardValidationDelegate.Result.CanBeUpdated(creditCard))
                .`when`(validationDelegate)
                .shouldCreateOrUpdate(creditCardEntry)

            delegate.onCreditCardSave(creditCardEntry)

            verify(storage, times(1))
                .updateCreditCard(
                    guid = creditCard.guid,
                    creditCardFields =
                        UpdatableCreditCardFields(
                            billingName = billingName,
                            cardNumber = "4111111111111111",
                            expiryMonth = expiryMonth,
                            expiryYear = expiryYear,
                            cardType = cardType,
                            cardCvv = cardCvv,
                        ),
                )
        }
    }

    @Test
    fun `GIVEN an invalid credit card entry WHEN onCreditCardSave is called THEN the request is ignored`() {
        runTest {
            val billingName = "Jon Doe"
            val cardNumber = ""
            val expiryMonth = ""
            val expiryYear = ""
            val cardType = "amex"

            val creditCardEntry =
                CreditCardEntry(
                    name = billingName,
                    number = cardNumber,
                    expiryMonth = expiryMonth,
                    expiryYear = expiryYear,
                    cardType = cardType,
                    cardCvv = null,
                )

            delegate.onCreditCardSave(creditCardEntry)

            verify(storage, times(0)).addCreditCard(any())
            verify(storage, times(0)).updateCreditCard(any(), any())
        }
    }

    @Test
    fun `GIVEN an address without a guid WHEN onAddressSave is called THEN it adds a new address in storage`() =
        runTest {
            val storage: AutofillCreditCardsAddressesStorage = mock()
            delegate = GeckoCreditCardsAddressesStorageDelegate(lazy { storage }, validationDelegate)

            val address =
                Address(
                    guid = "",
                    name = "John Doe",
                    organization = "Mozilla",
                    streetAddress = "999 Test Street",
                    addressLevel3 = "",
                    addressLevel2 = "Mountain View",
                    addressLevel1 = "CA",
                    postalCode = "94016",
                    country = "US",
                    tel = "+15551234567",
                    email = "john@example.com",
                )

            delegate.onAddressSave(address)

            verify(storage, times(1))
                .addAddress(
                    UpdatableAddressFields(
                        name = "John Doe",
                        organization = "Mozilla",
                        streetAddress = "999 Test Street",
                        addressLevel3 = "",
                        addressLevel2 = "Mountain View",
                        addressLevel1 = "CA",
                        postalCode = "94016",
                        country = "US",
                        tel = "+15551234567",
                        email = "john@example.com",
                    )
                )
            verify(storage, never()).updateAddress(any(), any())
        }

    @Test
    fun `GIVEN an address with a guid WHEN onAddressSave is called THEN it updates the existing address in storage`() =
        runTest {
            val storage: AutofillCreditCardsAddressesStorage = mock()
            delegate = GeckoCreditCardsAddressesStorageDelegate(lazy { storage }, validationDelegate)

            val address =
                Address(
                    guid = "test-guid",
                    name = "John Doe",
                    organization = "Mozilla",
                    streetAddress = "999 Test Street",
                    addressLevel3 = "",
                    addressLevel2 = "Mountain View",
                    addressLevel1 = "CA",
                    postalCode = "94016",
                    country = "US",
                    tel = "+15551234567",
                    email = "john@example.com",
                )

            delegate.onAddressSave(address)

            verify(storage, times(1))
                .updateAddress(
                    guid = "test-guid",
                    address =
                        UpdatableAddressFields(
                            name = "John Doe",
                            organization = "Mozilla",
                            streetAddress = "999 Test Street",
                            addressLevel3 = "",
                            addressLevel2 = "Mountain View",
                            addressLevel1 = "CA",
                            postalCode = "94016",
                            country = "US",
                            tel = "+15551234567",
                            email = "john@example.com",
                        ),
                )
            verify(storage, never()).addAddress(any())
        }

    @Test
    fun `GIVEN address autofill is enabled WHEN onAddressesFetch is called THEN it returns all stored addresses`() =
        runTest {
            val storage: AutofillCreditCardsAddressesStorage = mock()
            val storedAddresses = listOf<Address>(mock(), mock())
            doReturn(storedAddresses).`when`(storage).getAllAddresses()
            delegate =
                GeckoCreditCardsAddressesStorageDelegate(
                    lazy { storage },
                    isAddressAutofillEnabled = { true },
                )

            val result = delegate.onAddressesFetch()

            verify(storage, times(1)).getAllAddresses()
            assertEquals(storedAddresses, result)
        }

    @Test
    fun `GIVEN address autofill is disabled WHEN onAddressesFetch is called THEN it returns an empty list of addresses`() =
        runTest {
            val storage: AutofillCreditCardsAddressesStorage = mock()
            val storedCards = listOf<CreditCard>(mock())
            doReturn(storedCards).`when`(storage).getAllCreditCards()
            delegate =
                GeckoCreditCardsAddressesStorageDelegate(
                    lazy { storage },
                    isAddressAutofillEnabled = { false },
                )

            val result = delegate.onAddressesFetch()

            verify(storage, never()).getAllAddresses()
            assertEquals(emptyList<CreditCard>(), result)
        }
}
