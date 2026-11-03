/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.CustomizationStatusChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.MenuBuilder
import org.mozilla.fenix.components.menu.MenuCustomizationRepository
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration

/**
 * [CustomizableMenuStore] middleware responsible with handling the user interactions for customizing the menu
 * structure.
 *
 * @param repository [MenuCustomizationRepository] persisting the user's customization of the browser menu.
 * @param menuBuilder [MenuBuilder] providing the items of all the sections of the browser menu.
 * @param isToolbarAtBottom Whether the toolbar is shown at the bottom of the screen.
 * @param isExpandedToolbarEnabled Whether the expanded toolbar is used.
 * @param scope [CoroutineScope] tied to the lifetime of the screen, on which to read the current structure.
 */
class CustomizeMenuMiddleware(
    private val repository: MenuCustomizationRepository,
    private val menuBuilder: MenuBuilder,
    private val isToolbarAtBottom: Boolean,
    private val isExpandedToolbarEnabled: Boolean,
    private val scope: CoroutineScope,
) : Middleware<CustomizableMenuState, CustomizableMenuAction> {

    override fun invoke(
        store: Store<CustomizableMenuState, CustomizableMenuAction>,
        next: (CustomizableMenuAction) -> Unit,
        action: CustomizableMenuAction,
    ) {
        next(action)

        when (action) {
            is Init -> showInitialMenuStructure(store)
            is MenuStructureChanged -> {
                val (hiddenSections, visibleSections) = action.items.partition { it.id in action.hiddenGroupIds }
                val newMenuInfo =
                    CustomizedMenuInfo(
                        visibleSectionIds = visibleSections.map { it.id },
                        hiddenSectionIds = hiddenSections.map { it.id },
                    )
                repository.save(newMenuInfo.visibleSectionIds, newMenuInfo.hiddenSectionIds)

                val isCustomized = isCustomized(newMenuInfo)
                if (isCustomized != store.state.isCustomized) {
                    store.dispatch(CustomizationStatusChanged(isCustomized))
                }
            }
            is MenuEvent,
            is Update,
            is CustomizationStatusChanged -> Unit
        }
    }

    private fun showInitialMenuStructure(store: Store<CustomizableMenuState, CustomizableMenuAction>) = scope.launch {
        val customization = repository.customizedMenuInfo.filterNotNull().first()
        val groups = menuBuilder.menuStructure.first().associateBy { it.id }
        val shownSections =
            BrowserMenuConfiguration.sections(isToolbarAtBottom, isExpandedToolbarEnabled, customization)
        val hiddenSections = BrowserMenuConfiguration.hiddenSections(customization)

        store.dispatch(
            Update(
                items = (shownSections + hiddenSections).mapNotNull { groups[it.id] },
                mandatoryGroupIds = BrowserMenuConfiguration.REQUIRED_SECTION_IDS,
                hiddenGroupIds = hiddenSections.map { it.id }.toSet(),
                isCustomized = isCustomized(customization),
            )
        )
    }

    private fun isCustomized(customization: CustomizedMenuInfo): Boolean {
        val default = BrowserMenuConfiguration.defaultMenuInfo(isToolbarAtBottom, isExpandedToolbarEnabled)
        val shownSections =
            BrowserMenuConfiguration.sections(isToolbarAtBottom, isExpandedToolbarEnabled, customization)
        val defaultShownSections = BrowserMenuConfiguration.sections(isToolbarAtBottom, isExpandedToolbarEnabled)

        return shownSections.map { it.id } != defaultShownSections.map { it.id } ||
            customization.hiddenSectionIds.toSet() != default.hiddenSectionIds.toSet()
    }
}
