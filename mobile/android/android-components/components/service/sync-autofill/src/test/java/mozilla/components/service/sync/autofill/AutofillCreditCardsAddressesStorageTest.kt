/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.appservices.RustComponentsInitializer
import mozilla.components.concept.storage.CreditCard
import mozilla.components.concept.storage.NewCreditCardFields
import mozilla.components.concept.storage.UpdatableAddressFields
import mozilla.components.concept.storage.UpdatableCreditCardFields
import mozilla.components.lib.dataprotect.SecureAbove22Preferences
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class AutofillCreditCardsAddressesStorageTest {

    private lateinit var storage: AutofillCreditCardsAddressesStorage
    private lateinit var securePrefs: SecureAbove22Preferences

    @Before
    fun setup() {
        RustComponentsInitializer.init()
        // forceInsecure is set in the tests because a keystore wouldn't be configured in the test environment.
        securePrefs = SecureAbove22Preferences(testContext, "autofill", forceInsecure = true)
        storage = AutofillCreditCardsAddressesStorage(testContext, lazy { securePrefs })
    }

    @After
    fun cleanup() {
        storage.close()
    }

    @Test
    fun `add credit card`() = runTest {
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

        assertNotNull(creditCard)

        assertEquals(creditCardFields.billingName, creditCard.billingName)
        assertEquals(creditCardFields.cardNumber, creditCard.cardNumber)
        assertEquals("1111", creditCard.cardNumberLast4)
        assertEquals(creditCardFields.expiryMonth, creditCard.expiryMonth)
        assertEquals(creditCardFields.expiryYear, creditCard.expiryYear)
        assertEquals(creditCardFields.cardType, creditCard.cardType)
        assertEquals(
            CreditCard.ELLIPSES_START +
                CreditCard.ELLIPSIS +
                CreditCard.ELLIPSIS +
                CreditCard.ELLIPSIS +
                CreditCard.ELLIPSIS +
                "1111" +
                CreditCard.ELLIPSES_END,
            creditCard.obfuscatedCardNumber,
        )
    }

    @Test
    fun `get credit card`() = runTest {
        val creditCardFields =
            NewCreditCardFields(
                billingName = "Jon Doe",
                cardNumber = "5500000000000004",
                expiryMonth = 12,
                expiryYear = 2028,
                cardType = "amex",
                cardCvv = null,
            )
        val creditCard = storage.addCreditCard(creditCardFields)

        assertEquals(creditCard, storage.getCreditCard(creditCard.guid))
    }

    @Test
    fun `GIVEN a non-existent credit card guid WHEN getCreditCard is called THEN null is returned`() = runTest {
        assertNull(storage.getCreditCard("guid"))
    }

    @Test
    fun `get all credit cards`() = runTest {
        val creditCardFields1 =
            NewCreditCardFields(
                billingName = "Jane Fields",
                cardNumber = "5500000000000004",
                expiryMonth = 12,
                expiryYear = 2028,
                cardType = "mastercard",
                cardCvv = null,
            )
        val creditCardFields2 =
            NewCreditCardFields(
                billingName = "Banana Apple",
                cardNumber = "4111111111111111",
                expiryMonth = 1,
                expiryYear = 2030,
                cardType = "visa",
                cardCvv = null,
            )
        val creditCardFields3 =
            NewCreditCardFields(
                billingName = "Pineapple Orange",
                cardNumber = "340000000000009",
                expiryMonth = 2,
                expiryYear = 2028,
                cardType = "amex",
                cardCvv = null,
            )
        val creditCard1 = storage.addCreditCard(creditCardFields1)
        val creditCard2 = storage.addCreditCard(creditCardFields2)
        val creditCard3 = storage.addCreditCard(creditCardFields3)

        val creditCards = storage.getAllCreditCards()

        val savedCreditCard1 = creditCards.find { it == creditCard1 }
        assertNotNull(savedCreditCard1)
        val savedCreditCard2 = creditCards.find { it == creditCard2 }
        assertNotNull(savedCreditCard2)
        val savedCreditCard3 = creditCards.find { it == creditCard3 }
        assertNotNull(savedCreditCard3)

        val count = storage.countAllCreditCards()
        assertEquals(3, count)

        assertEquals("5500000000000004", savedCreditCard1.cardNumber)
        assertEquals("4111111111111111", savedCreditCard2.cardNumber)
        assertEquals("340000000000009", savedCreditCard3.cardNumber)
    }

    @Test
    fun `update credit card`() = runTest {
        val creditCardFields =
            NewCreditCardFields(
                billingName = "Jon Doe",
                cardNumber = "4111111111111111",
                expiryMonth = 12,
                expiryYear = 2028,
                cardType = "visa",
                cardCvv = null,
            )

        var creditCard = storage.addCreditCard(creditCardFields)

        // Change every field
        var newCreditCardFields =
            UpdatableCreditCardFields(
                billingName = "Jane Fields",
                cardNumber = "30000000000004",
                expiryMonth = 12,
                expiryYear = 2038,
                cardType = "diners",
                cardCvv = null,
            )

        storage.updateCreditCard(creditCard.guid, newCreditCardFields)

        creditCard = storage.getCreditCard(creditCard.guid)!!

        assertEquals(newCreditCardFields.billingName, creditCard.billingName)
        assertEquals(newCreditCardFields.cardNumber, creditCard.cardNumber)
        assertEquals("0004", creditCard.cardNumberLast4)
        assertEquals(newCreditCardFields.expiryMonth, creditCard.expiryMonth)
        assertEquals(newCreditCardFields.expiryYear, creditCard.expiryYear)
        assertEquals(newCreditCardFields.cardType, creditCard.cardType)

        // Change the name only.
        newCreditCardFields =
            UpdatableCreditCardFields(
                billingName = "Bob Jones",
                cardNumber = creditCard.cardNumber,
                expiryMonth = 12,
                expiryYear = 2038,
                cardType = "diners",
                cardCvv = null,
            )

        storage.updateCreditCard(creditCard.guid, newCreditCardFields)

        creditCard = storage.getCreditCard(creditCard.guid)!!

        assertEquals(newCreditCardFields.billingName, creditCard.billingName)
        assertEquals(newCreditCardFields.cardNumber, creditCard.cardNumber)
        assertEquals("0004", creditCard.cardNumberLast4)
        assertEquals(newCreditCardFields.expiryMonth, creditCard.expiryMonth)
        assertEquals(newCreditCardFields.expiryYear, creditCard.expiryYear)
        assertEquals(newCreditCardFields.cardType, creditCard.cardType)
    }

    @Test
    fun `GIVEN a credit card added without a security code WHEN it is read THEN the security code is null`() = runTest {
        val creditCard = storage.addCreditCard(newCreditCardFields())

        assertNull(creditCard.cardCvv)
        assertNull(storage.getCreditCard(creditCard.guid)!!.cardCvv)
    }

    @Test
    fun `GIVEN a credit card added with a security code WHEN it is read THEN the security code is returned`() =
        runTest {
            val creditCard = storage.addCreditCard(newCreditCardFields(cardCvv = "123"))

            assertEquals("123", creditCard.cardCvv)
            assertEquals("123", storage.getCreditCard(creditCard.guid)!!.cardCvv)
            assertEquals("123", storage.getAllCreditCards().single().cardCvv)
        }

    @Test
    fun `GIVEN a stored security code WHEN the credit card is updated without one THEN the security code is kept`() =
        runTest {
            val creditCard = storage.addCreditCard(newCreditCardFields(cardCvv = "123"))

            storage.updateCreditCard(creditCard.guid, updatableCreditCardFields(billingName = "Bob Jones"))

            val updatedCreditCard = storage.getCreditCard(creditCard.guid)!!
            assertEquals("Bob Jones", updatedCreditCard.billingName)
            assertEquals("123", updatedCreditCard.cardCvv)
        }

    @Test
    fun `GIVEN a stored security code WHEN the credit card is updated with a new one THEN the security code is replaced`() =
        runTest {
            val creditCard = storage.addCreditCard(newCreditCardFields(cardCvv = "123"))

            storage.updateCreditCard(creditCard.guid, updatableCreditCardFields(cardCvv = "456"))

            assertEquals("456", storage.getCreditCard(creditCard.guid)!!.cardCvv)
        }

    @Test
    fun `GIVEN a stored security code WHEN the credit card is updated with an empty one THEN the security code is removed`() =
        runTest {
            val creditCard = storage.addCreditCard(newCreditCardFields(cardCvv = "123"))

            storage.updateCreditCard(creditCard.guid, updatableCreditCardFields(cardCvv = ""))

            assertNull(storage.getCreditCard(creditCard.guid)!!.cardCvv)
        }

    @Test
    fun `GIVEN no stored security code WHEN the credit card is updated with one THEN the security code is stored`() =
        runTest {
            val creditCard = storage.addCreditCard(newCreditCardFields())

            storage.updateCreditCard(creditCard.guid, updatableCreditCardFields(cardCvv = "123"))

            assertEquals("123", storage.getCreditCard(creditCard.guid)!!.cardCvv)
        }

    @Test
    fun `GIVEN a non-existent credit card guid WHEN updateCreditCard is called THEN nothing is stored`() = runTest {
        storage.updateCreditCard("guid", updatableCreditCardFields())

        assertNull(storage.getCreditCard("guid"))
        assertEquals(0, storage.countAllCreditCards())
    }

    @Test
    fun `delete credit card`() = runTest {
        val creditCardFields =
            NewCreditCardFields(
                billingName = "Jon Doe",
                cardNumber = "30000000000004",
                expiryMonth = 12,
                expiryYear = 2028,
                cardType = "diners",
                cardCvv = null,
            )

        val creditCard = storage.addCreditCard(creditCardFields)
        assertNotNull(storage.getCreditCard(creditCard.guid))

        val isDeleteSuccessful = storage.deleteCreditCard(creditCard.guid)

        assertTrue(isDeleteSuccessful)
        assertNull(storage.getCreditCard(creditCard.guid))
    }

    @Test
    fun `add address`() = runTest {
        val addressFields =
            UpdatableAddressFields(
                name = "John Smith",
                organization = "Mozilla",
                streetAddress = "123 Sesame Street",
                addressLevel3 = "",
                addressLevel2 = "",
                addressLevel1 = "",
                postalCode = "90210",
                country = "US",
                tel = "+1 519 555-5555",
                email = "foo@bar.com",
            )
        val address = storage.addAddress(addressFields)

        assertNotNull(address)

        assertEquals(addressFields.name, address.name)
        assertEquals(addressFields.organization, address.organization)
        assertEquals(addressFields.streetAddress, address.streetAddress)
        assertEquals(addressFields.addressLevel3, address.addressLevel3)
        assertEquals(addressFields.addressLevel2, address.addressLevel2)
        assertEquals(addressFields.addressLevel1, address.addressLevel1)
        assertEquals(addressFields.postalCode, address.postalCode)
        assertEquals(addressFields.country, address.country)
        assertEquals(addressFields.tel, address.tel)
        assertEquals(addressFields.email, address.email)
    }

    @Test
    fun `get address`() = runTest {
        val addressFields =
            UpdatableAddressFields(
                name = "John Smith",
                organization = "Mozilla",
                streetAddress = "123 Sesame Street",
                addressLevel3 = "",
                addressLevel2 = "",
                addressLevel1 = "",
                postalCode = "90210",
                country = "US",
                tel = "+1 519 555-5555",
                email = "foo@bar.com",
            )
        val address = storage.addAddress(addressFields)

        assertEquals(address, storage.getAddress(address.guid))
    }

    @Test
    fun `GIVEN a non-existent address guid WHEN getAddress is called THEN null is returned`() = runTest {
        assertNull(storage.getAddress("guid"))
    }

    @Test
    fun `get all addresses`() = runTest {
        val addressFields1 =
            UpdatableAddressFields(
                name = "John Smith",
                organization = "Mozilla",
                streetAddress = "123 Sesame Street",
                addressLevel3 = "",
                addressLevel2 = "",
                addressLevel1 = "",
                postalCode = "90210",
                country = "US",
                tel = "+1 519 555-5555",
                email = "foo@bar.com",
            )
        val addressFields2 =
            UpdatableAddressFields(
                name = "Mary Sue",
                organization = "",
                streetAddress = "1 New St",
                addressLevel3 = "",
                addressLevel2 = "York",
                addressLevel1 = "SC",
                postalCode = "29745",
                country = "US",
                tel = "+19871234567",
                email = "mary@example.com",
            )
        val addressFields3 =
            UpdatableAddressFields(
                name = "Timothy João Berners-Lee",
                organization = "World Wide Web Consortium",
                streetAddress = "Rua Adalberto Pajuaba, 404",
                addressLevel3 = "Campos Elísios",
                addressLevel2 = "Ribeirão Preto",
                addressLevel1 = "SP",
                postalCode = "14055-220",
                country = "BR",
                tel = "+0318522222222",
                email = "timbr@example.org",
            )
        val address1 = storage.addAddress(addressFields1)
        val address2 = storage.addAddress(addressFields2)
        val address3 = storage.addAddress(addressFields3)

        val addresses = storage.getAllAddresses()

        val count = storage.countAllAddresses()
        assertEquals(3, count)

        val savedAddress1 = addresses.find { it == address1 }
        assertNotNull(savedAddress1)
        val savedAddress2 = addresses.find { it == address2 }
        assertNotNull(savedAddress2)
        val savedAddress3 = addresses.find { it == address3 }
        assertNotNull(savedAddress3)
    }

    @Test
    fun `update address`() = runTest {
        val addressFields =
            UpdatableAddressFields(
                name = "John Smith",
                organization = "Mozilla",
                streetAddress = "123 Sesame Street",
                addressLevel3 = "",
                addressLevel2 = "",
                addressLevel1 = "",
                postalCode = "90210",
                country = "US",
                tel = "+1 519 555-5555",
                email = "foo@bar.com",
            )

        var address = storage.addAddress(addressFields)

        val newAddressFields =
            UpdatableAddressFields(
                name = "Mary Sue",
                organization = "",
                streetAddress = "1 New St",
                addressLevel3 = "",
                addressLevel2 = "York",
                addressLevel1 = "SC",
                postalCode = "29745",
                country = "US",
                tel = "+19871234567",
                email = "mary@example.com",
            )

        storage.updateAddress(address.guid, newAddressFields)

        address = storage.getAddress(address.guid)!!

        assertEquals(newAddressFields.name, address.name)
        assertEquals(newAddressFields.organization, address.organization)
        assertEquals(newAddressFields.streetAddress, address.streetAddress)
        assertEquals(newAddressFields.addressLevel3, address.addressLevel3)
        assertEquals(newAddressFields.addressLevel2, address.addressLevel2)
        assertEquals(newAddressFields.addressLevel1, address.addressLevel1)
        assertEquals(newAddressFields.postalCode, address.postalCode)
        assertEquals(newAddressFields.country, address.country)
        assertEquals(newAddressFields.tel, address.tel)
        assertEquals(newAddressFields.email, address.email)
    }

    @Test
    fun `delete address`() = runTest {
        val addressFields =
            UpdatableAddressFields(
                name = "John Smith",
                organization = "Mozilla",
                streetAddress = "123 Sesame Street",
                addressLevel3 = "",
                addressLevel2 = "",
                addressLevel1 = "",
                postalCode = "90210",
                country = "US",
                tel = "+1 519 555-5555",
                email = "foo@bar.com",
            )

        val address = storage.addAddress(addressFields)
        val savedAddress = storage.getAddress(address.guid)
        assertEquals(address, savedAddress)

        val isDeleteSuccessful = storage.deleteAddress(address.guid)
        assertTrue(isDeleteSuccessful)
        assertNull(storage.getAddress(address.guid))
    }

    @Test
    fun `WHEN warmUp method is called THEN the database connection is established`(): Unit = runTest {
        val storageSpy = spy(storage)
        storageSpy.warmUp()

        verify(storageSpy).conn
    }

    private fun newCreditCardFields(cardCvv: String? = null) =
        NewCreditCardFields(
            billingName = "Jon Doe",
            cardNumber = "4111111111111111",
            expiryMonth = 12,
            expiryYear = 2028,
            cardType = "visa",
            cardCvv = cardCvv,
        )

    private fun updatableCreditCardFields(billingName: String = "Jon Doe", cardCvv: String? = null) =
        UpdatableCreditCardFields(
            billingName = billingName,
            cardNumber = "4111111111111111",
            expiryMonth = 12,
            expiryYear = 2028,
            cardType = "visa",
            cardCvv = cardCvv,
        )
}
