/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.storage

import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState

/** Class that handles shared storage handling for the TabGroups feature. */
class TabGroupStorage(private val tabDataCoordinator: TabDataCoordinator) {
    /**
     * Handle saving a tab group with a selected set of tab ids.
     *
     * @param formState The current state of the edit form.
     * @param selectedTabIds The set of selected ids to save to a group.
     * @return the id of the new group, if one was created, or null if the group already exists.
     */
    suspend fun handleSaveFromMultiSelection(
        formState: TabGroupFormState,
        selectedTabIds: List<String>,
    ): String? {
        if (formState.tabGroupId != null) {
            tabDataCoordinator.updateTabGroup(
                tabGroupId = formState.tabGroupId,
                title = formState.name,
                theme = formState.theme.toStorageValue(),
            )
            return null
        }

        return tabDataCoordinator.createTabGroup(
            title = formState.name,
            theme = formState.theme.toStorageValue(),
            tabIds = selectedTabIds,
        )
    }

    /**
     * Handle saving a tab group from dragging one tab onto the other.
     *
     * @param formState The current state of the edit form.
     * @param mode The [TabsTrayState] mode, used to determine the destination tab for the drop
     * @return the newly created group's id if successful, or null if the group could not be created.
     */
    suspend fun handleSaveFromDragAndDrop(
        formState: TabGroupFormState,
        mode: TabsTrayState.Mode.DragAndDrop,
    ): String? {
        val destinationId = mode.destinationId ?: return null

        // The dragged tab is sequenced from the tab it was dropped on.
        return tabDataCoordinator.createTabGroup(
            title = formState.name,
            theme = formState.theme.toStorageValue(),
            tabIds = listOf(mode.sourceId, destinationId),
            anchorTabId = destinationId,
        )
    }
}
