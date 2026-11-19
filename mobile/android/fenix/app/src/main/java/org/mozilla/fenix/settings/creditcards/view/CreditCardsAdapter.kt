/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.creditcards.view

import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import mozilla.components.concept.storage.CreditCard
import org.mozilla.fenix.settings.creditcards.interactor.CreditCardsManagementInteractor

/**
 * Adapter for a list of credit cards to be displayed.
 *
 * @param interactor The [CreditCardsManagementInteractor] notified when a credit card is selected.
 * @param shouldShowCvvSaved Whether to show which cards have a saved security code.
 */
class CreditCardsAdapter(
    private val interactor: CreditCardsManagementInteractor,
    private val shouldShowCvvSaved: Boolean = false,
) : ListAdapter<CreditCard, CreditCardItemViewHolder>(DiffCallback) {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): CreditCardItemViewHolder {
        val view = LayoutInflater.from(parent.context).inflate(CreditCardItemViewHolder.LAYOUT_ID, parent, false)
        return CreditCardItemViewHolder(view, interactor, shouldShowCvvSaved)
    }

    override fun onBindViewHolder(holder: CreditCardItemViewHolder, position: Int) {
        holder.bind(getItem(position))
    }

    internal object DiffCallback : DiffUtil.ItemCallback<CreditCard>() {
        override fun areItemsTheSame(oldItem: CreditCard, newItem: CreditCard) = oldItem.guid == newItem.guid

        override fun areContentsTheSame(oldItem: CreditCard, newItem: CreditCard) = oldItem == newItem
    }
}
