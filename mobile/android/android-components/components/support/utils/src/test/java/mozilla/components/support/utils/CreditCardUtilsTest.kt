/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CreditCardUtilsTest {

    @Test
    fun `GIVEN a list of recognized card numbers and their respective card type WHEN creditCardIIN is called for a given a card number THEN the correct cardType name is returned`() {
        /**
         * Test cases based on
         * https://searchfox.org/firefox-main/source/toolkit/modules/tests/xpcshell/test_CreditCard.js
         */
        val recognizedCards =
            listOf(
                // Edge cases
                Pair("2221000000000000", "mastercard"),
                Pair("2720000000000000", "mastercard"),
                Pair("2200000000000000", "mir"),
                Pair("2204000000000000", "mir"),
                Pair("340000000000000", "amex"),
                Pair("370000000000000", "amex"),
                Pair("3000000000000000", "diners"),
                Pair("3050000000000000", "diners"),
                Pair("3095000000000000", "diners"),
                Pair("36000000000000", "diners"),
                Pair("3800000000000000", "diners"),
                Pair("3900000000000000", "diners"),
                Pair("3528000000000000", "jcb"),
                Pair("3589000000000000", "jcb"),
                Pair("4035000000000000", "cartebancaire"),
                Pair("4360000000000000", "cartebancaire"),
                Pair("4000000000000000", "visa"),
                Pair("4999999999999999", "visa"),
                Pair("5400000000000000", "mastercard"),
                Pair("5500000000000000", "mastercard"),
                Pair("5100000000000000", "mastercard"),
                Pair("5399999999999999", "mastercard"),
                Pair("6011000000000000", "discover"),
                Pair("6221260000000000", "discover"),
                Pair("6229250000000000", "discover"),
                Pair("6240000000000000", "discover"),
                Pair("6269990000000000", "discover"),
                Pair("6282000000000000", "discover"),
                Pair("6288990000000000", "discover"),
                Pair("6400000000000000", "discover"),
                Pair("6500000000000000", "discover"),
                Pair("6200000000000000", "unionpay"),
                Pair("8100000000000000", "unionpay"),
                // Valid according to Luhn number
                Pair("2204941877211882", "mir"),
                Pair("2720994326581252", "mastercard"),
                Pair("374542158116607", "amex"),
                Pair("36006666333344", "diners"),
                Pair("3541675340715696", "jcb"),
                Pair("3543769248058305", "jcb"),
                Pair("4035501428146300", "cartebancaire"),
                Pair("4111111111111111", "visa"),
                Pair("5346755600299631", "mastercard"),
                Pair("5495770093313616", "mastercard"),
                Pair("5574238524540144", "mastercard"),
                Pair("6011029459267962", "discover"),
                Pair("6278592974938779", "unionpay"),
                Pair("8171999927660000", "unionpay"),
                Pair("30569309025904", "diners"),
                Pair("38520000023237", "diners"),
                Pair("3   8 5 2 0 0 0 0 0 2 3 2 3 7", "diners"),
            )

        for ((cardNumber, cardType) in recognizedCards) {
            assertEquals(cardNumber.creditCardIIN()?.creditCardIssuerNetwork?.name, cardType)
        }

        val unrecognizedCards =
            listOf(
                "411111111111111",
                "41111111111111111",
                "",
                "9111111111111111",
            )

        for (cardNumber in unrecognizedCards) {
            assertNull(cardNumber.creditCardIIN())
        }
    }

    @Test
    fun `GIVEN a various card type strings WHEN creditCardIssuerNetwork is called THEN the correct CreditCardIssuerNetwork is returned`() {
        val amexCard =
            CreditCardIssuerNetwork(
                name = CreditCardNetworkType.AMEX.cardName,
                icon = R.drawable.ic_cc_logo_amex,
            )

        assertEquals(amexCard, CreditCardNetworkType.AMEX.cardName.creditCardIssuerNetwork())

        val genericCard =
            CreditCardIssuerNetwork(
                name = CreditCardNetworkType.GENERIC.cardName,
                icon = R.drawable.ic_icon_credit_card_generic,
            )

        assertEquals(genericCard, "".creditCardIssuerNetwork())
        assertEquals(genericCard, "blah".creditCardIssuerNetwork())
    }

    @Test
    fun `validateCreditCardNumber returns true for valid credit card numbers `() {
        val americanExpressCard = "371449635398431"
        val dinnersClubCard = "30569309025904"
        val discoverCard = "6011111111111117"
        val jcbCard = "3530111333300000"
        val masterCardCard = "5555555555554444"
        val visaCard = "4111111111111111"

        assertTrue(americanExpressCard.validateCreditCardNumber())
        assertTrue(dinnersClubCard.validateCreditCardNumber())
        assertTrue(discoverCard.validateCreditCardNumber())
        assertTrue(jcbCard.validateCreditCardNumber())
        assertTrue(masterCardCard.validateCreditCardNumber())
        assertTrue(visaCard.validateCreditCardNumber())
    }

    @Test
    fun `validateCreditCardNumber returns false got invalid credit card numbers `() {
        val shortCardNumber = "12345678901"
        val longCardNumber = "12345678901234567890"

        val americanExpressCardInvalid = "371449635398432"
        val dinnersClubCardInvalid = "30569309025905"
        val discoverCardInvalid = "6011111111111118"
        val jcbCardInvalid = "3530111333300001"
        val masterCardCardInvalid = "5555555555554445"
        val visaCardInvalid = "4111111111111112"
        val voyagerCardInvalid = "869941728035896"

        assertFalse(shortCardNumber.validateCreditCardNumber())
        assertFalse(longCardNumber.validateCreditCardNumber())

        assertFalse(americanExpressCardInvalid.validateCreditCardNumber())
        assertFalse(dinnersClubCardInvalid.validateCreditCardNumber())
        assertFalse(discoverCardInvalid.validateCreditCardNumber())
        assertFalse(jcbCardInvalid.validateCreditCardNumber())
        assertFalse(masterCardCardInvalid.validateCreditCardNumber())
        assertFalse(visaCardInvalid.validateCreditCardNumber())
        assertFalse(voyagerCardInvalid.validateCreditCardNumber())
    }

    @Test
    fun `luhnAlgorithmValidation returns false for invalid identification numbers `() {
        // "4242424242424242" is a valid identification number
        assertFalse(luhnAlgorithmValidation("4242424242424240"))
        assertFalse(luhnAlgorithmValidation("4242424242424241"))
        assertFalse(luhnAlgorithmValidation("4242424242424243"))
        assertFalse(luhnAlgorithmValidation("4242424242424244"))
        assertFalse(luhnAlgorithmValidation("4242424242424245"))
        assertFalse(luhnAlgorithmValidation("4242424242424246"))
        assertFalse(luhnAlgorithmValidation("4242424242424247"))
        assertFalse(luhnAlgorithmValidation("4242424242424248"))
        assertFalse(luhnAlgorithmValidation("4242424242424249"))
        assertFalse(luhnAlgorithmValidation("1"))
        assertFalse(luhnAlgorithmValidation("12"))
        assertFalse(luhnAlgorithmValidation("123"))
    }

    @Test
    fun `luhnAlgorithmValidation returns true for valid identification numbers `() {
        assertTrue(luhnAlgorithmValidation("0"))
        assertTrue(luhnAlgorithmValidation("00"))
        assertTrue(luhnAlgorithmValidation("18"))
        assertTrue(luhnAlgorithmValidation("0000000000000000"))
        assertTrue(luhnAlgorithmValidation("4242424242424242"))
        assertTrue(luhnAlgorithmValidation("42424242424242426"))
        assertTrue(luhnAlgorithmValidation("424242424242424267"))
        assertTrue(luhnAlgorithmValidation("4242424242424242675"))
        assertTrue(luhnAlgorithmValidation("000000018"))
        assertTrue(luhnAlgorithmValidation("99999999999999999999"))
        assertTrue(luhnAlgorithmValidation("1234567812345670"))
    }
}
