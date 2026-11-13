/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.store

import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/** The [Store] for holding the [MenuState] and applying [MenuAction]s. */
class MenuStore(
    initialState: MenuState,
    middleware: List<Middleware<MenuState, MenuAction>> = listOf(),
) :
    Store<MenuState, MenuAction>(
        initialState = initialState,
        reducer = ::reducer,
        middleware = middleware,
    ) {

    /** [SharedFlow] of one-shot [NavigationEvent]s emitted by middlewares for the menu UI to consume. */
    val navigationEvents: SharedFlow<NavigationEvent>
        field = MutableSharedFlow<NavigationEvent>(extraBufferCapacity = 1)

    init {
        dispatch(MenuAction.InitAction)
    }

    /** Emit a one-shot [NavigationEvent] to be consumed by the menu UI. */
    internal fun emitEvent(event: NavigationEvent) = navigationEvents.tryEmit(event)
}

private fun reducer(state: MenuState, action: MenuAction): MenuState {
    return when (action) {
        is MenuAction.InitAction,
        is MenuAction.AddBookmark,
        is MenuAction.AddShortcut,
        is MenuAction.RemoveShortcut,
        is MenuAction.DeleteBrowsingDataAndQuit,
        is MenuAction.FindInPage,
        is MenuAction.DefaultBrowserMenuBannerClicked,
        is MenuAction.DefaultBrowserMenuBannerDismissed,
        is MenuAction.OpenInApp,
        is MenuAction.OpenInFirefox,
        is MenuAction.InstallAddon,
        is MenuAction.CustomMenuItemAction,
        is MenuAction.CustomizeReaderView,
        is MenuAction.Navigate,
        is MenuAction.OnSummarizationMenuExposed,
        is MenuAction.MoveToNonPrivateTab,
        is MenuAction.IPProtectionToggle,
        is MenuAction.SaveAsPdfRequested,
        is MenuAction.PrintRequested,
        is MenuAction.OnExtensionsMenuClicked,
        is MenuAction.OnTabGroupMenuItemExposed,
        is MenuAction.WebExtensionActionClicked -> state

        is MenuAction.OnMoreMenuClicked -> state.copy(isMoreMenuExpanded = !state.isMoreMenuExpanded)
        is MenuAction.RequestDesktopSite -> state.copy(isDesktopMode = true)

        is MenuAction.RequestMobileSite -> state.copy(isDesktopMode = false)

        is MenuAction.UpdateExtensionState ->
            state.copyWithExtensionMenuState {
                it.copy(recommendedAddons = action.recommendedAddons)
            }

        is MenuAction.UpdateWebExtensionBrowserMenuItems ->
            state.copyWithExtensionMenuState {
                it.copy(browserWebExtensionMenuItem = action.webExtensionBrowserMenuItem)
            }

        is MenuAction.UpdateBookmarkState ->
            state.copyWithBrowserMenuState {
                it.copy(bookmarkState = action.bookmarkState)
            }

        is MenuAction.UpdatePinnedState ->
            state.copyWithBrowserMenuState {
                it.copy(isPinned = action.isPinned)
            }

        is MenuAction.UpdateInstallAddonInProgress ->
            state.copyWithExtensionMenuState {
                it.copy(addonInstallationInProgress = action.addon)
            }

        is MenuAction.InstallAddonFailed ->
            state.copyWithExtensionMenuState {
                it.copy(addonInstallationInProgress = null)
            }

        is MenuAction.InstallAddonSuccess ->
            state.copyWithExtensionMenuState { extensionState ->
                extensionState.copy(
                    recommendedAddons = state.extensionMenuState.recommendedAddons.filter { it != action.addon },
                    availableAddons = state.extensionMenuState.availableAddons.plus(action.addon),
                    addonInstallationInProgress = null,
                )
            }

        is MenuAction.UpdateAvailableAddons ->
            state.copyWithExtensionMenuState {
                it.copy(availableAddons = action.availableAddons)
            }

        is MenuAction.InitializeSummarizationMenuState ->
            state.copyWithSummarizationMenuState {
                action.state
            }

        is MenuAction.UpdateIPProtectionMenuState -> state.copy(ipProtectionMenuState = action.state)
    }
}

@VisibleForTesting
internal inline fun MenuState.copyWithBrowserMenuState(
    crossinline update: (BrowserMenuState) -> BrowserMenuState
): MenuState {
    return this.copy(browserMenuState = this.browserMenuState?.let { update(it) })
}

@VisibleForTesting
internal inline fun MenuState.copyWithExtensionMenuState(
    crossinline update: (ExtensionMenuState) -> ExtensionMenuState
): MenuState {
    return this.copy(extensionMenuState = update(this.extensionMenuState))
}

private inline fun MenuState.copyWithSummarizationMenuState(
    crossinline update: (SummarizationMenuState) -> SummarizationMenuState
): MenuState {
    return this.copy(summarizationMenuState = update(this.summarizationMenuState))
}
