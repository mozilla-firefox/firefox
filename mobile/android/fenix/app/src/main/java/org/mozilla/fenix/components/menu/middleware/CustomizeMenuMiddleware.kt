/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
import mozilla.components.compose.menu.store.CustomizableMenuAction.BackPressed
import mozilla.components.compose.menu.store.CustomizableMenuAction.CustomizationStatusChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.EditGroupItemsClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultConfirmed
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.CustomizedSectionInfo
import org.mozilla.fenix.components.menu.MenuBuilder
import org.mozilla.fenix.components.menu.MenuCustomizationRepository
import org.mozilla.fenix.components.menu.MenuItemsRegistry
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration

/**
 * [CustomizableMenuStore] middleware responsible with handling the user interactions for customizing the menu
 * structure.
 *
 * @param repository [MenuCustomizationRepository] persisting the user's customization of the browser menu.
 * @param registry [MenuItemsRegistry] with the providers of the items of all the sections of the browser menu.
 * @param isToolbarAtBottom Whether the toolbar is shown at the bottom of the screen.
 * @param isExpandedToolbarEnabled Whether the expanded toolbar is used.
 * @param isToolsMenuCustomizationEnabled Whether the items of the menu sections chosen by the user are shown.
 * @param scope [CoroutineScope] tied to the lifetime of the screen, on which to read the current structure.
 * @param appStore [AppStore] showing the snackbars of the screen.
 * @param cannotHideSectionMessage Localized message telling the user a section cannot be removed from the menu.
 */
class CustomizeMenuMiddleware(
    private val repository: MenuCustomizationRepository,
    private val registry: MenuItemsRegistry,
    private val isToolbarAtBottom: Boolean,
    private val isExpandedToolbarEnabled: Boolean,
    private val isToolsMenuCustomizationEnabled: Boolean,
    private val scope: CoroutineScope,
    private val appStore: AppStore,
    private val cannotHideSectionMessage: String,
) : Middleware<CustomizableMenuState, CustomizableMenuAction> {
    override fun invoke(
        store: Store<CustomizableMenuState, CustomizableMenuAction>,
        next: (CustomizableMenuAction) -> Unit,
        action: CustomizableMenuAction,
    ) {
        next(action)

        when (action) {
            is Init -> showInitialMenuStructure(store)
            is MenuStructureChanged -> saveMenuStructure(store, action)
            is ResetToDefaultConfirmed -> resetToDefault(store)
            is AttemptedRemovalOfMandatorySection ->
                appStore.dispatch(SnackbarAction.ShowSnackbar(title = cannotHideSectionMessage))
            is MenuEvent,
            is Update,
            is BackPressed,
            is PreviewClicked,
            is EditGroupItemsClicked,
            is CustomizationStatusChanged -> Unit
        }
    }

    private fun saveMenuStructure(
        store: Store<CustomizableMenuState, CustomizableMenuAction>,
        action: MenuStructureChanged,
    ) {
        val (hiddenSections, visibleSections) = action.items.partition { it.id in action.hiddenGroupIds }
        val visibleSectionIds = visibleSections.map { it.id }
        // Once moved, the navigation section stays independent of the toolbar, wherever it is shown, until the menu is
        // restored to its default structure.
        val isNavigationSectionMoved =
            repository.customizedMenuInfo.value?.isNavigationSectionMoved == true ||
                BrowserMenuConfiguration.isNavigationSectionMoved(
                    visibleSectionIds,
                    isToolbarAtBottom,
                    isExpandedToolbarEnabled,
                )
        val newMenuInfo =
            CustomizedMenuInfo(
                visibleSectionIds = visibleSectionIds,
                hiddenSectionIds = hiddenSections.map { it.id },
                isNavigationSectionMoved = isNavigationSectionMoved,
            )
        repository.save(newMenuInfo)

        val isCustomized = isCustomized(newMenuInfo, repository.customizedSectionsInfo.value.orEmpty())
        if (isCustomized != store.state.isCustomized) {
            store.dispatch(CustomizationStatusChanged(isCustomized))
        }
    }

    private fun showInitialMenuStructure(store: Store<CustomizableMenuState, CustomizableMenuAction>) = scope.launch {
        val customization = repository.customizedMenuInfo.filterNotNull().first()
        val sectionsCustomization = repository.customizedSectionsInfo.filterNotNull().first()
        dispatchMenuStructure(store, customization, sectionsCustomization)
    }

    private suspend fun dispatchMenuStructure(
        store: Store<CustomizableMenuState, CustomizableMenuAction>,
        customization: CustomizedMenuInfo,
        sectionsCustomization: Map<String, CustomizedSectionInfo>,
    ) {
        val itemsCustomization = if (isToolsMenuCustomizationEnabled) sectionsCustomization else emptyMap()
        val sections = BrowserMenuConfiguration.allSections(itemsCustomization)
        val groups = MenuBuilder(registry, sections).menuStructure.first().associateBy { it.id }
        val shownSections =
            BrowserMenuConfiguration.sections(isToolbarAtBottom, isExpandedToolbarEnabled, customization)
        val hiddenSections = BrowserMenuConfiguration.hiddenSections(customization)

        store.dispatch(
            Update(
                items = (shownSections + hiddenSections).mapNotNull { groups[it.id] },
                mandatoryGroupIds = BrowserMenuConfiguration.REQUIRED_SECTION_IDS,
                hiddenGroupIds = hiddenSections.map { it.id }.toSet(),
                isCustomized = isCustomized(customization, sectionsCustomization),
                editableGroupIds =
                    if (isToolsMenuCustomizationEnabled) {
                        BrowserMenuConfiguration.ITEMS_CUSTOMIZABLE_SECTION_IDS
                    } else {
                        emptySet()
                    },
            )
        )
    }

    private fun isCustomized(
        customization: CustomizedMenuInfo,
        sectionsCustomization: Map<String, CustomizedSectionInfo>,
    ): Boolean {
        val default = BrowserMenuConfiguration.defaultMenuInfo(isToolbarAtBottom, isExpandedToolbarEnabled)
        val shownSections =
            BrowserMenuConfiguration.sections(isToolbarAtBottom, isExpandedToolbarEnabled, customization)
        val defaultShownSections = BrowserMenuConfiguration.sections(isToolbarAtBottom, isExpandedToolbarEnabled)

        return shownSections.map { it.id } != defaultShownSections.map { it.id } ||
            customization.hiddenSectionIds.toSet() != default.hiddenSectionIds.toSet() ||
            customization.isNavigationSectionMoved ||
            isToolsMenuCustomizationEnabled && sectionsCustomization.isCustomized()
    }

    private fun Map<String, CustomizedSectionInfo>.isCustomized(): Boolean {
        val defaults = BrowserMenuConfiguration.defaultSectionsInfo()
        return any { (sectionId, sectionInfo) -> sectionInfo != defaults[sectionId] }
    }

    private fun resetToDefault(store: Store<CustomizableMenuState, CustomizableMenuAction>) = scope.launch {
        repository.reset()

        val defaultInfo = BrowserMenuConfiguration.defaultMenuInfo(isToolbarAtBottom, isExpandedToolbarEnabled)
        dispatchMenuStructure(store, defaultInfo, emptyMap())
    }
}
