/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.view

import androidx.annotation.LayoutRes
import androidx.core.view.isVisible
import androidx.recyclerview.widget.LinearLayoutManager
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.ComponentCreditCardsBinding
import org.mozilla.fenix.settings.autofill.AutofillFragmentState
import org.mozilla.fenix.settings.creditcards.interactor.CreditCardsManagementInteractor

/**
 * Shows a list of credit cards.
 *
 * @property binding The binding of the credit cards list.
 * @property interactor The [CreditCardsManagementInteractor] notified of user actions.
 * @param shouldShowCvvSaved Whether to show which cards have a saved security code.
 */
class CreditCardsManagementView(
    val binding: ComponentCreditCardsBinding,
    val interactor: CreditCardsManagementInteractor,
    shouldShowCvvSaved: Boolean = false,
) {

    private val creditCardsAdapter = CreditCardsAdapter(interactor, shouldShowCvvSaved)

    init {
        binding.creditCardsList.apply {
            adapter = creditCardsAdapter
            layoutManager = LinearLayoutManager(binding.root.context)
        }

        binding.addCreditCardButton.addCreditCardLayout.setOnClickListener { interactor.onAddCreditCardClick() }
    }

    /** Updates the display of the credit cards based on the given [AutofillFragmentState]. */
    fun update(state: AutofillFragmentState) {
        binding.progressBar.isVisible = state.isLoading
        binding.creditCardsList.isVisible = state.creditCards.isNotEmpty()

        creditCardsAdapter.submitList(state.creditCards)
    }

    companion object {
        @LayoutRes val LAYOUT_ID = R.layout.component_credit_cards
    }
}
