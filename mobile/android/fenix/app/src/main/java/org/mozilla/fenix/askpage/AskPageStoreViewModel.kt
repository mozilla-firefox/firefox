/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.askpage

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import mozilla.components.concept.llm.LlmProvider
import mozilla.components.concept.llm.LlmSession
import mozilla.components.concept.llm.LlmSessionConfig
import mozilla.components.feature.ask.page.AskPageMiddleware
import mozilla.components.feature.ask.page.AskPageState
import mozilla.components.feature.ask.page.AskPageStore
import mozilla.components.feature.ask.page.askPageReducer
import mozilla.components.lib.llm.harness.create

/**
 * A [ViewModel] that owns and survives configuration changes for an [AskPageStore].
 *
 * @param providers The [LlmProvider]s the session may use to answer the user's questions.
 */
class AskPageStoreViewModel(providers: List<LlmProvider>) : ViewModel() {
    private val session = LlmSession.create(LlmSessionConfig(providers = providers))
        .also { it.launchIn(viewModelScope) }

    val store = AskPageStore(
        initialState = AskPageState.Idle,
        reducer = ::askPageReducer,
        middleware = listOf(AskPageMiddleware(session, viewModelScope)),
    )

    companion object {
        fun factory(provider: LlmProvider) = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T =
                AskPageStoreViewModel(listOf(provider)) as T
        }
    }
}
