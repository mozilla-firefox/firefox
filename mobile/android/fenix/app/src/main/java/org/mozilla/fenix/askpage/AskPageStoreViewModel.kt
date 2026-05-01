/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.askpage

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import mozilla.components.concept.llm.CloudLlmProvider
import mozilla.components.feature.ask.page.AskPageMiddleware
import mozilla.components.feature.ask.page.AskPageState
import mozilla.components.feature.ask.page.AskPageStore
import mozilla.components.feature.ask.page.askPageReducer

/**
 * A [ViewModel] that owns and survives configuration changes for an [AskPageStore].
 *
 * @param llmProvider The [CloudLlmProvider] used to answer the user's questions.
 */
class AskPageStoreViewModel(llmProvider: CloudLlmProvider) : ViewModel() {
    val store = AskPageStore(
        initialState = AskPageState.Idle,
        reducer = ::askPageReducer,
        middleware = listOf(AskPageMiddleware(llmProvider, viewModelScope)),
    )

    companion object {
        fun factory(llmProvider: CloudLlmProvider) = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T =
                AskPageStoreViewModel(llmProvider) as T
        }
    }
}
