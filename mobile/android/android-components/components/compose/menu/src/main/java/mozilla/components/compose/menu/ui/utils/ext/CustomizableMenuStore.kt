/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.utils.ext

import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.ui.customize.CustomizeMenuGroups

/** Save the move of the group with [movedGroupId], which changed [groups] into [movedGroups], if it changed them. */
internal fun CustomizableMenuStore.saveMove(
    groups: CustomizeMenuGroups,
    movedGroupId: String,
    movedGroups: CustomizeMenuGroups,
) {
    if (movedGroups != groups) {
        dispatch(
            MenuStructureChanged(
                items = movedGroups.shown + movedGroups.hidden,
                hiddenGroupIds = movedGroups.hidden.map { it.id }.toSet(),
                movedGroupId = movedGroupId,
            )
        )
    }
}
