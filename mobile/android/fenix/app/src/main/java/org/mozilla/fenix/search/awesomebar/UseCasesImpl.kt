/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search.awesomebar

import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.feature.search.SearchUseCases
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabs.TabsUseCases

/**
 * Awesome Bar implementation for the [SessionUseCases.LoadUrlUseCase] that uses the
 * [AwesomeBarInteractor]
 */
internal class AwesomeBarLoadUrlUseCase(
    private val interactor: AwesomeBarInteractor,
) : SessionUseCases.LoadUrlUseCase {
    override fun invoke(
        url: String,
        flags: EngineSession.LoadUrlFlags,
        additionalHeaders: Map<String, String>?,
        originalInput: String?,
    ) {
        interactor.onUrlTapped(url, flags)
    }
}

/**
 * Awesome Bar implementation for the [SearchUseCases.SearchUseCase] that uses the
 * [AwesomeBarInteractor]
 */
internal class AwesomeBarSearchUseCase(
    private val interactor: AwesomeBarInteractor,
) : SearchUseCases.SearchUseCase {
    override fun invoke(
        searchTerms: String,
        searchEngine: SearchEngine?,
        parentSessionId: String?,
    ) {
        interactor.onSearchTermsTapped(searchTerms)
    }
}

/**
 * Awesome Bar implementation for the [SearchUseCases.SearchUseCase] that uses the
 * [AwesomeBarInteractor]
 */
internal class AwesomeBarSelectTabUseCase(
    private val interactor: AwesomeBarInteractor,
) : TabsUseCases.SelectTabUseCase {
    override fun invoke(tabId: String) {
        interactor.onExistingSessionSelected(tabId)
    }
}
