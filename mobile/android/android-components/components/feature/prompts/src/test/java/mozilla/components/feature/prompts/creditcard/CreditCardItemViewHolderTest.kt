/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.creditcard

import android.view.LayoutInflater
import android.view.View
import android.widget.ImageView
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.feature.prompts.R
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.CreditCardNetworkType
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class CreditCardItemViewHolderTest {

    private lateinit var view: View
    private lateinit var cardLogoView: ImageView
    private lateinit var cardNumberView: TextView
    private lateinit var expirationDateView: TextView
    private lateinit var onCreditCardSelected: (CreditCardEntry) -> Unit

    private val creditCard =
        CreditCardEntry(
            guid = "1",
            name = "Banana Apple",
            number = "4111111111111111",
            expiryMonth = "5",
            expiryYear = "2030",
            cardType = CreditCardNetworkType.VISA.cardName,
            cardCvv = null,
        )

    @Before
    fun setup() {
        view = LayoutInflater.from(testContext).inflate(CreditCardItemViewHolder.LAYOUT_ID, null)
        cardLogoView = view.findViewById(R.id.credit_card_logo)
        cardNumberView = view.findViewById(R.id.credit_card_number)
        expirationDateView = view.findViewById(R.id.credit_card_expiration_date)
        onCreditCardSelected = mock()
    }

    @Test
    fun `GIVEN a credit card item WHEN bind is called THEN set the card number, logo and expiry date`() {
        CreditCardItemViewHolder(view, onCreditCardSelected).bind(creditCard)

        assertNotNull(cardLogoView.drawable)
        assertEquals(creditCard.obfuscatedCardNumber, cardNumberView.text)
        assertEquals("0${creditCard.expiryMonth}/${creditCard.expiryYear}", expirationDateView.text)
    }

    @Test
    fun `GIVEN a credit card item WHEN a credit item is clicked THEN onCreditCardSelected is called with the given credit card item`() {
        var onCreditCardSelectedCalled: CreditCardEntry? = null
        val onCreditCardSelected = { creditCard: CreditCardEntry ->
            onCreditCardSelectedCalled = creditCard
        }
        CreditCardItemViewHolder(view, onCreditCardSelected).bind(creditCard)

        view.performClick()

        assertEquals(creditCard, onCreditCardSelectedCalled)
    }
}
