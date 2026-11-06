/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils

import androidx.annotation.DrawableRes
import androidx.annotation.VisibleForTesting
import kotlin.math.floor
import kotlin.math.log10
import mozilla.components.support.utils.ext.toCreditCardNumber

/**
 * Information about a credit card issuing network.
 *
 * @param name The name of the credit card issuer network.
 * @param icon The icon of the credit card issuer network.
 */
data class CreditCardIssuerNetwork(
    val name: String,
    @param:DrawableRes val icon: Int,
)

/**
 * Information about a credit card issuer identification numbers.
 *
 * @param startRange The start issuer identification number range.
 * @param endRange The end issuer identification number range.
 * @param cardNumberMaxLength A list of the range of maximum card number lengths.
 */
data class CreditCardIIN(
    val creditCardIssuerNetwork: CreditCardIssuerNetwork,
    val startRange: Int,
    val endRange: Int,
    val cardNumberMaxLength: List<Int>,
)

/**
 * Enum of supported credit card network types. This list mirrors the networks from
 * https://searchfox.org/firefox-main/source/toolkit/modules/CreditCard.jsm
 */
enum class CreditCardNetworkType(val cardName: String) {
    AMEX("amex"),
    CARTEBANCAIRE("cartebancaire"),
    DINERS("diners"),
    DISCOVER("discover"),
    JCB("jcb"),
    MASTERCARD("mastercard"),
    MIR("mir"),
    UNIONPAY("unionpay"),
    VISA("visa"),
    GENERIC(""),
}

/** A mapping of credit card numbers to their respective credit card issuers. */
@Suppress("MagicNumber", "LargeClass")
internal object CreditCardUtils {

    private val GENERIC =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.GENERIC.cardName,
            icon = R.drawable.ic_icon_credit_card_generic,
        )
    private val AMEX =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.AMEX.cardName,
            icon = R.drawable.ic_cc_logo_amex,
        )
    private val CARTEBANCAIRE =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.CARTEBANCAIRE.cardName,
            icon = R.drawable.ic_icon_credit_card_generic,
        )
    private val DINERS =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.DINERS.cardName,
            icon = R.drawable.ic_cc_logo_diners,
        )
    private val DISCOVER =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.DISCOVER.cardName,
            icon = R.drawable.ic_cc_logo_discover,
        )
    private val JCB =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.JCB.cardName,
            icon = R.drawable.ic_cc_logo_jcb,
        )
    private val MIR =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.MIR.cardName,
            icon = R.drawable.ic_cc_logo_mir,
        )
    private val UNIONPAY =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.UNIONPAY.cardName,
            icon = R.drawable.ic_cc_logo_unionpay,
        )
    private val VISA =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.VISA.cardName,
            icon = R.drawable.ic_cc_logo_visa,
        )
    private val MASTERCARD =
        CreditCardIssuerNetwork(
            name = CreditCardNetworkType.MASTERCARD.cardName,
            icon = R.drawable.ic_cc_logo_mastercard,
        )

    /** Map of recognized credit card issuer network name to their [CreditCardIssuerNetwork]. */
    private val creditCardIssuers =
        mapOf(
            CreditCardNetworkType.AMEX.cardName to AMEX,
            CreditCardNetworkType.CARTEBANCAIRE.cardName to CARTEBANCAIRE,
            CreditCardNetworkType.DINERS.cardName to DINERS,
            CreditCardNetworkType.DISCOVER.cardName to DISCOVER,
            CreditCardNetworkType.JCB.cardName to JCB,
            CreditCardNetworkType.MIR.cardName to MIR,
            CreditCardNetworkType.UNIONPAY.cardName to UNIONPAY,
            CreditCardNetworkType.VISA.cardName to VISA,
            CreditCardNetworkType.MASTERCARD.cardName to MASTERCARD,
        )

    /**
     * List of recognized credit card issuer networks.
     *
     * Based on
     * https://searchfox.org/mozilla-central/rev/4275e4bd2b2aba34b2c69b314c4b50bdf83520af/toolkit/modules/CreditCard.jsm#40
     */
    private val creditCardIINs =
        listOf(
                CreditCardIIN(
                    creditCardIssuerNetwork = AMEX,
                    startRange = 34,
                    endRange = 34,
                    cardNumberMaxLength = listOf(15),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = AMEX,
                    startRange = 37,
                    endRange = 37,
                    cardNumberMaxLength = listOf(15),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = CARTEBANCAIRE,
                    startRange = 4035,
                    endRange = 4035,
                    cardNumberMaxLength = listOf(16),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = CARTEBANCAIRE,
                    startRange = 4360,
                    endRange = 4360,
                    cardNumberMaxLength = listOf(16),
                ),
                // We diverge from Wikipedia here, because Diners card
                // support length of 14-19.
                CreditCardIIN(
                    creditCardIssuerNetwork = DINERS,
                    startRange = 300,
                    endRange = 305,
                    cardNumberMaxLength = listOf(14, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = DINERS,
                    startRange = 3095,
                    endRange = 3095,
                    cardNumberMaxLength = listOf(14, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = DINERS,
                    startRange = 36,
                    endRange = 36,
                    cardNumberMaxLength = listOf(14, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = DINERS,
                    startRange = 38,
                    endRange = 39,
                    cardNumberMaxLength = listOf(14, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = DISCOVER,
                    startRange = 6011,
                    endRange = 6011,
                    cardNumberMaxLength = listOf(16, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = DISCOVER,
                    startRange = 622126,
                    endRange = 622925,
                    cardNumberMaxLength = listOf(16, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = DISCOVER,
                    startRange = 624000,
                    endRange = 626999,
                    cardNumberMaxLength = listOf(16, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = DISCOVER,
                    startRange = 628200,
                    endRange = 628899,
                    cardNumberMaxLength = listOf(16, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = DISCOVER,
                    startRange = 64,
                    endRange = 65,
                    cardNumberMaxLength = listOf(16, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = JCB,
                    startRange = 3528,
                    endRange = 3589,
                    cardNumberMaxLength = listOf(16, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = MASTERCARD,
                    startRange = 2221,
                    endRange = 2720,
                    cardNumberMaxLength = listOf(16),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = MASTERCARD,
                    startRange = 51,
                    endRange = 55,
                    cardNumberMaxLength = listOf(16),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = MIR,
                    startRange = 2200,
                    endRange = 2204,
                    cardNumberMaxLength = listOf(16),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = UNIONPAY,
                    startRange = 62,
                    endRange = 62,
                    cardNumberMaxLength = listOf(16, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = UNIONPAY,
                    startRange = 81,
                    endRange = 81,
                    cardNumberMaxLength = listOf(16, 19),
                ),
                CreditCardIIN(
                    creditCardIssuerNetwork = VISA,
                    startRange = 4,
                    endRange = 4,
                    cardNumberMaxLength = listOf(16),
                ),
            )
            .sortedWith { a, b -> b.startRange - a.startRange }

    /**
     * Returns the [CreditCardIIN] for the provided credit card number.
     *
     * Based on
     * https://searchfox.org/mozilla-central/rev/4275e4bd2b2aba34b2c69b314c4b50bdf83520af/toolkit/modules/CreditCard.jsm#229
     *
     * @param cardNumber The credit card number.
     * @return the [CreditCardIIN] for the provided credit card number or null if it does not match any of the
     *   recognized credit card issuers.
     */
    fun getCreditCardIIN(cardNumber: String): CreditCardIIN? {
        val safeCardNumber = cardNumber.toCreditCardNumber()
        for (issuer in creditCardIINs) {
            if (issuer.cardNumberMaxLength.size == 1 && issuer.cardNumberMaxLength[0] != safeCardNumber.length) {
                continue
            } else if (
                issuer.cardNumberMaxLength.size > 1 &&
                    (safeCardNumber.length < issuer.cardNumberMaxLength[0] ||
                        safeCardNumber.length > issuer.cardNumberMaxLength[1])
            ) {
                continue
            }

            val prefixLength = floor(log10(issuer.startRange.toDouble())) + 1
            val prefix = safeCardNumber.substring(0, prefixLength.toInt()).toInt()

            if (prefix >= issuer.startRange && prefix <= issuer.endRange) {
                return issuer
            }
        }

        return null
    }

    /**
     * Returns the [CreditCardIssuerNetwork] for the provided credit card issuer network name.
     *
     * @param cardType The credit card issuer network name.
     * @return the [CreditCardIssuerNetwork] for the provided credit card issuer network.
     */
    fun getCreditCardIssuerNetwork(cardType: String): CreditCardIssuerNetwork = creditCardIssuers[cardType] ?: GENERIC
}

/**
 * Returns the [CreditCardIIN] for the provided credit card number.
 *
 * @return the [CreditCardIIN] for the provided credit card number or null if it does not match any of the recognized
 *   credit card issuers.
 */
fun String.creditCardIIN() = CreditCardUtils.getCreditCardIIN(this)

/**
 * Returns the [CreditCardIssuerNetwork] for the provided credit card type.
 *
 * @return the [CreditCardIssuerNetwork] for the provided credit card type orr null if it does not match any of
 *   recognized credit card type.
 */
fun String.creditCardIssuerNetwork() = CreditCardUtils.getCreditCardIssuerNetwork(this)

/**
 * Returns true if the provided string is a valid credit card by checking if it has a matching credit card issuer
 * network and passes the Luhn algorithm, and false otherwise.
 */
fun String.validateCreditCardNumber(): Boolean {
    val creditCardNumber = this.toCreditCardNumber()

    if (creditCardNumber != this || creditCardNumber.creditCardIIN() == null) {
        return false
    }

    return luhnAlgorithmValidation(creditCardNumber)
}

private const val DECIMAL_BASE = 10

/** Implementation of Luhn Algorithm validation (https://en.wikipedia.org/wiki/Luhn_algorithm) */
@VisibleForTesting
internal fun luhnAlgorithmValidation(creditCardNumber: String): Boolean {
    var checksum = 0
    val reversedCardNumber = creditCardNumber.reversed()

    for (index in reversedCardNumber.indices) {
        val digit = Character.getNumericValue(reversedCardNumber[index])
        checksum += if (index % 2 == 0) digit else (digit * 2).let { (it / DECIMAL_BASE) + (it % DECIMAL_BASE) }
    }

    return (checksum % DECIMAL_BASE) == 0
}
