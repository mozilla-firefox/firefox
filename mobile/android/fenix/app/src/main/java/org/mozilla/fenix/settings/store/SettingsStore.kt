/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.store

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.settings.store.SettingsAction.DestinationChanged
import org.mozilla.fenix.settings.store.SettingsAction.ItemClicked
import org.mozilla.fenix.settings.store.SettingsAction.ItemToggled
import org.mozilla.fenix.settings.store.SettingsAction.SearchEnded
import org.mozilla.fenix.settings.store.SettingsAction.SearchQueryUpdated
import org.mozilla.fenix.settings.store.SettingsAction.SearchResultsUpdated
import org.mozilla.fenix.settings.store.SettingsAction.SearchStarted
import org.mozilla.fenix.settings.store.SettingsAction.SearchableItemsUpdated
import org.mozilla.fenix.settings.store.SettingsAction.SettingsAreLoading
import org.mozilla.fenix.settings.store.SettingsAction.SettingsLoaded
import org.mozilla.fenix.settings.store.SettingsAction.SettingsUpdateFeedbackAvailable

/**
 * The [Store] for holding the [SettingsState] and applying [SettingsAction]s.
 */
class SettingsStore(
    initialState: SettingsState = SettingsState(),
    middlewares: List<Middleware<SettingsState, SettingsAction>> = emptyList(),
) : Store<SettingsState, SettingsAction>(
    initialState = initialState,
    reducer = SettingsReducer::reduce,
    middleware = middlewares,
)

/**
 * Reducer for [SettingsState].
 */
object SettingsReducer {
    fun reduce(state: SettingsState, action: SettingsAction): SettingsState {
        return when (action) {
            is SettingsAreLoading -> state.copy(isLoading = true)

            is SettingsLoaded -> state.copy(
                isLoading = false,
                settingsItems = action.items,
            )

            is SearchQueryUpdated -> state.copy(
                searchQuery = action.query,
            )

            is SearchableItemsUpdated -> state.copy(
                allSearchableItems = action.items,
            )

            is SearchResultsUpdated -> state.copy(
                filteredItems = action.items,
            )

            // These actions don't change state and handled by middlewares
            is DestinationChanged,
            is ItemClicked,
            is ItemToggled,
            is SettingsUpdateFeedbackAvailable,
            is SearchStarted,
            is SearchEnded,
                -> state
        }
    }
}
