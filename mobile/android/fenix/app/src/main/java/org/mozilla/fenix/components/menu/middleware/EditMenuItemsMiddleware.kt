/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedEmptySection
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedMoveAsSubItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ExpansionToggled
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Init
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.menu.CustomizedItemInfo
import org.mozilla.fenix.components.menu.CustomizedSectionInfo
import org.mozilla.fenix.components.menu.MenuCustomizationRepository
import org.mozilla.fenix.components.menu.MenuItemsRegistry
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration

/**
 * [Middleware] showing the items of a menu section as customized by the user, in the screen customizing them, and
 * saving the changes the user makes.
 *
 * @param repository [MenuCustomizationRepository] persisting the user's customization of the browser menu.
 * @param registry [MenuItemsRegistry] providing the items of the section.
 * @param sectionId Id of the menu section whose items are customized.
 * @param scope [CoroutineScope] tied to the lifetime of the screen, on which to read the saved customization.
 * @param appStore [AppStore] showing the snackbars of the screen.
 * @param cannotMoveAsSubItemMessage Localized message shown when the Extensions item, which cannot be in another item,
 *   is dropped inside another item.
 * @param cannotHideLastItemMessage Localized message telling the user the last item shown in the section cannot be
 *   removed from the menu.
 */
class EditMenuItemsMiddleware(
    private val repository: MenuCustomizationRepository,
    private val registry: MenuItemsRegistry,
    private val sectionId: String,
    private val scope: CoroutineScope,
    private val appStore: AppStore,
    private val cannotMoveAsSubItemMessage: String,
    private val cannotHideLastItemMessage: String,
) : Middleware<CustomizableMenuItemsState, CustomizableMenuItemsAction> {
    override fun invoke(
        store: Store<CustomizableMenuItemsState, CustomizableMenuItemsAction>,
        next: (CustomizableMenuItemsAction) -> Unit,
        action: CustomizableMenuItemsAction,
    ) {
        next(action)

        when (action) {
            is Init -> showItems(store)
            is ItemsChanged -> saveItems(action)
            is AttemptedMoveAsSubItem -> appStore.dispatch(SnackbarAction.ShowSnackbar(cannotMoveAsSubItemMessage))
            is AttemptedEmptySection -> appStore.dispatch(SnackbarAction.ShowSnackbar(cannotHideLastItemMessage))
            is Update,
            is ExpansionToggled,
            is PreviewClicked -> Unit
        }
    }

    private fun showItems(store: Store<CustomizableMenuItemsState, CustomizableMenuItemsAction>) = scope.launch {
        val sectionInfo =
            repository.customizedSectionsInfo.filterNotNull().first()[sectionId]
                ?: BrowserMenuConfiguration.defaultSectionsInfo().getValue(sectionId)

        store.dispatch(
            Update(
                items = sectionInfo.items.map { it.toCustomizable() },
                hiddenItems = sectionInfo.hiddenItems.map { it.toCustomizable() },
            )
        )
    }

    private fun saveItems(action: ItemsChanged) {
        repository.save(
            CustomizedSectionInfo(
                sectionId = sectionId,
                items = action.items.map { it.toInfo() },
                hiddenItems = action.hiddenItems.map { it.toInfo() },
            )
        )
    }

    private val topLevelOnlyIds = BrowserMenuConfiguration.topLevelOnlyItemIds()

    private fun CustomizableMenuItem.toInfo() = CustomizedItemInfo(id = id, subItemIds = subItems.map { it.id })

    private fun CustomizedItemInfo.toCustomizable() =
        CustomizableMenuItem(
            id = id,
            item = itemOf(id),
            subItems = subItemIds.map { CustomizableMenuItem(it, itemOf(it), isTopLevelOnly = it in topLevelOnlyIds) },
            isTopLevelOnly = id in topLevelOnlyIds,
        )

    private fun itemOf(id: String) = registry.providers[id]?.itemsFlow?.value?.firstOrNull()
}
