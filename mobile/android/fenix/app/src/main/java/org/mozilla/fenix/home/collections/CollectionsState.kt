/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections

import mozilla.components.feature.tab.collections.TabCollection
import org.mozilla.fenix.components.appstate.AppState

/** State object encapsulating the UI state of the collections section of the homepage. */
sealed class CollectionsState {

    /**
     * State in which a user has collections, and they should be displayed in the UI.
     *
     * @property collections List of [TabCollection] to display.
     * @property expandedCollections List of ids corresponding to [TabCollection]s which are currently expanded.
     */
    data class Content(
        val collections: List<TabCollection>,
        val expandedCollections: Set<Long>,
    ) : CollectionsState()

    /** State in which the Collections to Tab Groups migration card should be displayed in place of the collections. */
    data object MigrationCard : CollectionsState()

    /** State in which no collections section should be displayed. */
    data object Gone : CollectionsState()

    companion object {
        internal fun build(
            appState: AppState,
            showCollections: Boolean,
            shouldShowCollectionsMigrationCard: Boolean,
        ): CollectionsState =
            with(appState) {
                val collections = if (showCollections) this.collections else emptyList()

                when {
                    shouldShowCollectionsMigrationCard -> MigrationCard

                    collections.isNotEmpty() ->
                        Content(
                            collections = collections,
                            expandedCollections = expandedCollections,
                        )

                    else -> Gone
                }
            }
    }
}
