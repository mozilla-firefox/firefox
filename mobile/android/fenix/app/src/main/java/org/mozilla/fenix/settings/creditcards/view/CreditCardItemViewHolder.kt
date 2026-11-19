/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.view

import android.view.View
import androidx.annotation.LayoutRes
import androidx.core.view.isVisible
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Locale
import mozilla.components.concept.storage.CreditCard
import mozilla.components.support.utils.creditCardIssuerNetwork
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.CreditCardListItemBinding
import org.mozilla.fenix.settings.creditcards.interactor.CreditCardsManagementInteractor
import org.mozilla.fenix.utils.view.ViewHolder

/**
 * View holder for a credit card list item.
 *
 * @param view The view of the list item.
 * @param interactor The [CreditCardsManagementInteractor] notified when the item is selected.
 * @param shouldShowCvvSaved Whether to show that the card has a saved security code.
 */
class CreditCardItemViewHolder(
    private val view: View,
    private val interactor: CreditCardsManagementInteractor,
    private val shouldShowCvvSaved: Boolean = false,
) : ViewHolder(view) {

    fun bind(creditCard: CreditCard) {
        val binding = CreditCardListItemBinding.bind(view)

        binding.creditCardLogo.setImageResource(creditCard.cardType.creditCardIssuerNetwork().icon)

        binding.creditCardNumber.text = creditCard.obfuscatedCardNumber

        bindCreditCardExpiryDate(creditCard, binding)

        binding.cvvSaved.isVisible = shouldShowCvvSaved && !creditCard.cardCvv.isNullOrEmpty()

        itemView.setOnClickListener {
            interactor.onSelectCreditCard(creditCard)
        }
    }

    /** Set the credit card expiry date formatted according to the locale. */
    private fun bindCreditCardExpiryDate(
        creditCard: CreditCard,
        binding: CreditCardListItemBinding,
    ) {
        val dateFormat = SimpleDateFormat(DATE_PATTERN, Locale.getDefault())

        val calendar = Calendar.getInstance()
        calendar.set(Calendar.DAY_OF_MONTH, 1)
        // Subtract 1 from the expiry month since Calendar.Month is based on a 0-indexed.
        calendar.set(Calendar.MONTH, creditCard.expiryMonth.toInt() - 1)
        calendar.set(Calendar.YEAR, creditCard.expiryYear.toInt())

        binding.expiryDate.text = dateFormat.format(calendar.time)
    }

    companion object {
        @LayoutRes val LAYOUT_ID = R.layout.credit_card_list_item

        // Date format pattern for the credit card expiry date.
        private const val DATE_PATTERN = "MM/yyyy"
    }
}
