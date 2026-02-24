/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.summarization

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import mozilla.components.feature.summarize.SummarizationMiddleware
import mozilla.components.feature.summarize.SummarizationState
import mozilla.components.feature.summarize.SummarizationStore
import mozilla.components.feature.summarize.summarizationReducer

/**
 * A [ViewModel] that owns and survives configuration changes for a [SummarizationStore].
 *
 * @param initializedFromShake Whether the summarization feature was triggered by a shake gesture.
 */
class SummarizationStoreViewModel(
    initializedFromShake: Boolean,
) : ViewModel() {
    val store = SummarizationStore(
        initialState = SummarizationState.Inert(initializedFromShake),
        reducer = ::summarizationReducer,
        middleware = listOf(SummarizationMiddleware()),
    )

    companion object {
        /**
         * Creates a [ViewModelProvider.Factory] for [SummarizationStoreViewModel].
         *
         * @param initializedFromShake Whether the summarization feature was triggered by a shake gesture.
         */
        fun factory(
            initializedFromShake: Boolean,
        ) = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                return SummarizationStoreViewModel(initializedFromShake) as T
            }
        }
    }
}
