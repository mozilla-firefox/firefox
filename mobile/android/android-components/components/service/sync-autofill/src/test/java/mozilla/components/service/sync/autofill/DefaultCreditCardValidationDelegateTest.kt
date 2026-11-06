/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.sync.autofill

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.runBlocking
import mozilla.appservices.RustComponentsInitializer
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.CreditCardValidationDelegate.Result
import mozilla.components.concept.storage.NewCreditCardFields
import mozilla.components.lib.dataprotect.SecureAbove22Preferences
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class DefaultCreditCardValidationDelegateTest {

    private lateinit var storage: AutofillCreditCardsAddressesStorage
    private lateinit var securePrefs: SecureAbove22Preferences
    private lateinit var validationDelegate: DefaultCreditCardValidationDelegate

    @Before
    fun before() = runBlocking {
        RustComponentsInitializer.init()
        // forceInsecure is set in the tests because a keystore wouldn't be configured in the test environment.
        securePrefs = SecureAbove22Preferences(testContext, "autofill", forceInsecure = true)
        storage = AutofillCreditCardsAddressesStorage(testContext, lazy { securePrefs })
        validationDelegate = DefaultCreditCardValidationDelegate(storage = lazy { storage })
    }

    @Test
    fun `WHEN no credit cards exist in the storage, THEN add the new credit card to storage`() = runBlocking {
        val newCreditCard = createCreditCardEntry(guid = "1")
        val result = validationDelegate.shouldCreateOrUpdate(newCreditCard)

        assertEquals(Result.CanBeCreated, result)
    }

    @Test
    fun `WHEN existing credit card matches by guid and card number, THEN update the credit card in storage`() =
        runBlocking {
            val creditCardFields =
                NewCreditCardFields(
                    billingName = "Pineapple Orange",
                    cardNumber = "4111111111111111",
                    expiryMonth = 12,
                    expiryYear = 2028,
                    cardType = "visa",
                    cardCvv = null,
                )
            val creditCard = storage.addCreditCard(creditCardFields)
            val newCreditCard = createCreditCardEntry(guid = creditCard.guid)
            val result = validationDelegate.shouldCreateOrUpdate(newCreditCard)

            assertEquals(Result.CanBeUpdated(creditCard), result)
        }

    @Test
    fun `WHEN existing credit card matches by guid only, THEN update the credit card in storage`() = runBlocking {
        val creditCardFields =
            NewCreditCardFields(
                billingName = "Pineapple Orange",
                cardNumber = "4111111111111111",
                expiryMonth = 12,
                expiryYear = 2028,
                cardType = "visa",
                cardCvv = null,
            )
        val creditCard = storage.addCreditCard(creditCardFields)
        val newCreditCard = createCreditCardEntry(guid = creditCard.guid)
        val result = validationDelegate.shouldCreateOrUpdate(newCreditCard)

        assertEquals(Result.CanBeUpdated(creditCard), result)
    }

    @Test
    fun `WHEN existing credit card matches by card number only, THEN update the credit card in storage`() =
        runBlocking {
            val creditCardFields =
                NewCreditCardFields(
                    billingName = "Pineapple Orange",
                    cardNumber = "4111111111111111",
                    expiryMonth = 12,
                    expiryYear = 2028,
                    cardType = "visa",
                    cardCvv = null,
                )
            val creditCard = storage.addCreditCard(creditCardFields)
            val newCreditCard = createCreditCardEntry(cardNumber = "4111111111111111")
            val result = validationDelegate.shouldCreateOrUpdate(newCreditCard)

            assertEquals(Result.CanBeUpdated(creditCard), result)
        }

    @Test
    fun `WHEN existing credit card does not match by guid and card number, THEN add the new credit card to storage`() =
        runBlocking {
            val newCreditCard = createCreditCardEntry(guid = "2")
            val creditCardFields =
                NewCreditCardFields(
                    billingName = "Pineapple Orange",
                    cardNumber = "4111111111111111",
                    expiryMonth = 12,
                    expiryYear = 2028,
                    cardType = "visa",
                    cardCvv = null,
                )
            storage.addCreditCard(creditCardFields)

            val result = validationDelegate.shouldCreateOrUpdate(newCreditCard)

            assertEquals(Result.CanBeCreated, result)
        }
}

fun createCreditCardEntry(
    guid: String = "id",
    billingName: String = "Banana Apple",
    cardNumber: String = "4111111111111110",
    expiryMonth: String = "1",
    expiryYear: String = "2030",
    cardType: String = "amex",
) =
    CreditCardEntry(
        guid = guid,
        name = billingName,
        number = cardNumber,
        expiryMonth = expiryMonth,
        expiryYear = expiryYear,
        cardType = cardType,
        cardCvv = null,
    )
