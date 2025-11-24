/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.store

import mozilla.components.lib.state.Action
import org.mozilla.fenix.settings.navigation.SettingsDestination

/**
 * Actions specific to the [SettingsStore].
 */
sealed class SettingsAction : Action {
    /**
     * Loading new settings data is currently in progress.
     */
    data object SettingsAreLoading : SettingsAction()

    /**
     * User navigated to a new settings screen
     *
     * @param destination The [SettingsDestination] that was navigated to.
     */
    data class DestinationChanged(val destination: SettingsDestination) : SettingsAction()

    /**
     * New settings data has been loaded for the current settings screen.
     *
     * @param items The new settings items.
     */
    data class SettingsLoaded(val items: List<SettingsItem>) : SettingsAction()

    /**
     * User clicked on a settings item.
     */
    data class ItemClicked(val item: SettingsItem) : SettingsAction()

    /**
     * User toggled on/off a setting item.
     *
     * @param preferenceKey The key of the preference that was toggled.
     * @param newValue The new value of the preference.
     */
    data class ItemToggled(
        val preferenceKey: Int,
        val newValue: Boolean,
    ) : SettingsAction()

    /**
     * Search started.
     */
    data object SearchStarted : SettingsAction()

    /**
     * Search finished.
     */
    data object SearchEnded : SettingsAction()

    /**
     * The list of all settings items that can be searched for has been updated.
     *
     * @param items The new list of all settings items that can be searched for.
     */
    data class SearchableItemsUpdated(val items: List<SettingsItem>) : SettingsAction()

    /**
     * Search query changed.
     *
     * @param query The new search query.
     */
    data class SearchQueryUpdated(val query: String) : SettingsAction()

    /**
     * Search results updated based on the current user query.
     *
     * @param items The new search results.
     */
    data class SearchResultsUpdated(val items: List<SettingsItem>) : SettingsAction()

    /**
     * Important message about a setting operation.
     *
     * @param message User friendly message about the status or result of a recent setting operation.
     */
    data class SettingsUpdateFeedbackAvailable(val message: String) : SettingsAction()
}
