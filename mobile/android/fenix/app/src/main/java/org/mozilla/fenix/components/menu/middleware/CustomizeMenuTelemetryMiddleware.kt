/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
import mozilla.components.compose.menu.store.CustomizableMenuAction.BackPressed
import mozilla.components.compose.menu.store.CustomizableMenuAction.CustomizationStatusChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultConfirmed
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.GleanMetrics.CustomizeMenu

private const val SECTIONS_SEPARATOR = ","

/**
 * [CustomizableMenuStore] middleware recording telemetry for how users interact with the screen allowing them to
 * customize the menu.
 */
class CustomizeMenuTelemetryMiddleware : Middleware<CustomizableMenuState, CustomizableMenuAction> {
    override fun invoke(
        store: Store<CustomizableMenuState, CustomizableMenuAction>,
        next: (CustomizableMenuAction) -> Unit,
        action: CustomizableMenuAction,
    ) {
        // Recreating the screen builds the store again with its previous state, and reads the menu structure again.
        val isFirstStructureRead = store.state.menuGroups.isEmpty()

        next(action)

        when (action) {
            is Update -> if (isFirstStructureRead) recordShown(action)
            is MenuStructureChanged -> recordSectionMoved(action)
            is BackPressed -> CustomizeMenu.backPressed.record()
            is ResetToDefaultConfirmed -> CustomizeMenu.resetToDefaultClicked.record()
            is PreviewClicked -> CustomizeMenu.previewClicked.record()
            is MenuEvent,
            is Init,
            is CustomizationStatusChanged,
            is ResetToDefaultClicked,
            is AttemptedRemovalOfMandatorySection -> Unit
        }
    }

    private fun recordSectionMoved(change: MenuStructureChanged) {
        val isHidden = change.movedGroupId in change.hiddenGroupIds
        val sectionsOfSameKind = change.items.filter { (it.id in change.hiddenGroupIds) == isHidden }

        CustomizeMenu.sectionMoved.record(
            CustomizeMenu.SectionMovedExtra(
                section = change.movedGroupId,
                hidden = isHidden,
                position = sectionsOfSameKind.indexOfFirst { it.id == change.movedGroupId } + 1,
            )
        )
    }

    private fun recordShown(update: Update) {
        val (hiddenSections, shownSections) = update.items.partition { it.id in update.hiddenGroupIds }

        CustomizeMenu.shown.record(
            CustomizeMenu.ShownExtra(
                shownSections = shownSections.joinToString(SECTIONS_SEPARATOR) { it.id },
                hiddenSections = hiddenSections.joinToString(SECTIONS_SEPARATOR) { it.id },
            )
        )
    }
}
