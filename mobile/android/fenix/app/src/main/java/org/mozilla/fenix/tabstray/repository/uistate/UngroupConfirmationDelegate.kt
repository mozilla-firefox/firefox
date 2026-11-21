/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.repository.uistate

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction

/**
 * Resolves side effects from the ungroup confirmation dialog and sets the user's preference to skip it, if checked.
 *
 * @param repository Source of the skip preference.
 * @param scope Used to persist preference changes.
 */
class UngroupConfirmationDelegate(
    private val repository: TabManagerUiStateRepository,
    private val scope: CoroutineScope,
) {
    private val logger = Logger("UngroupConfirmationDelegate")

    /**
     * Dispatches the next ungroup action, either confirming when the user chose to skip the confirmation or requesting
     * the confirmation.
     *
     * @param group The [TabsTrayItem.TabGroup] being ungrouped.
     * @param dispatch Dispatches the resolved [TabGroupAction].
     */
    fun resolve(group: TabsTrayItem.TabGroup, dispatch: (TabGroupAction) -> Unit) {
        if (repository.uiState.value?.skipUngroupConfirmation == true) {
            dispatch(TabGroupAction.UngroupConfirmed(group = group, dontAskAgain = true))
        } else {
            dispatch(TabGroupAction.UngroupConfirmationRequested(group = group))
        }
    }

    /**
     * Persists the user preference that the ungroup confirmation should be skipped in the future.
     *
     * @param dontAskAgain Whether the user asked to suppress the confirmation.
     */
    fun setSkip(dontAskAgain: Boolean) {
        if (dontAskAgain && repository.uiState.value?.skipUngroupConfirmation != true) {
            scope.launch {
                val success = repository.setSkipUngroupConfirmation()
                if (!success) {
                    logger.debug("Failed to update the skip ungroup confirmation preference")
                }
            }
        }
    }
}
