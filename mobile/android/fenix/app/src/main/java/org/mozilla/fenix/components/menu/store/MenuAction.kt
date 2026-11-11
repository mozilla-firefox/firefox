/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.store

import android.app.PendingIntent
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.feature.addons.Addon
import mozilla.components.service.fxa.manager.AccountState
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint

/** Actions to dispatch through the [MenuStore] to modify the [MenuState]. */
sealed class MenuAction : MenuEvent {

    /**
     * [MenuAction] dispatched to indicate that the store is initialized and ready to use. This action is dispatched
     * automatically before any other action is processed. Its main purpose is to trigger initialization logic in
     * middlewares.
     */
    data object InitAction : MenuAction()

    /** [MenuAction] dispatched when a bookmark is to be added. */
    data object AddBookmark : MenuAction()

    /** [MenuAction] dispatched when reader view customization controls should be displayed. */
    data object CustomizeReaderView : MenuAction()

    /**
     * [MenuAction] dispatched when a bookmark state is updated.
     *
     * @property bookmarkState The new [BookmarkState] to be updated.
     */
    data class UpdateBookmarkState(val bookmarkState: BookmarkState) : MenuAction()

    /** [MenuAction] dispatched when a site is to be added to shortcuts. */
    data object AddShortcut : MenuAction()

    /** [MenuAction] dispatched when a site is to be removed from shortcuts. */
    data object RemoveShortcut : MenuAction()

    /**
     * [MenuAction] dispatched when a pinned shortcut state is updated.
     *
     * @property isPinned The new [isPinned] state to be updated.
     */
    data class UpdatePinnedState(val isPinned: Boolean) : MenuAction()

    /** [MenuAction] dispatched to delete browsing data and quit the browser. */
    data object DeleteBrowsingDataAndQuit : MenuAction()

    /** [MenuAction] dispatched when a site is to be opened in an external app. */
    data object OpenInApp : MenuAction()

    /** [MenuAction] dispatched when a custom tab is to be opened in the browser. */
    data object OpenInFirefox : MenuAction()

    /** [MenuAction] dispatched to launch find in page feature for the current site. */
    data object FindInPage : MenuAction()

    /** [MenuAction] dispatched to move the current private tab to a non-private tab. */
    data object MoveToNonPrivateTab : MenuAction()

    /** [MenuAction] dispatched when the default browser banner is clicked. */
    data object DefaultBrowserMenuBannerClicked : MenuAction()

    /** [MenuAction] dispatched when default browser menu banner is dismissed. */
    data object DefaultBrowserMenuBannerDismissed : MenuAction()

    /**
     * [MenuAction] dispatched when the extension state is updated.
     *
     * @property recommendedAddons The recommended [Addon]s to suggest.
     */
    data class UpdateExtensionState(val recommendedAddons: List<Addon>) : MenuAction()

    /**
     * [MenuAction] dispatched when browser web extension items list is updated.
     *
     * @property webExtensionBrowserMenuItem browserMenuItem The list of [WebExtensionMenuItem] to be shown in the menu.
     */
    data class UpdateWebExtensionBrowserMenuItems(val webExtensionBrowserMenuItem: List<WebExtensionMenuItem>) :
        MenuAction()

    /**
     * [MenuAction] dispatched when installed addons list is updated.
     *
     * @property availableAddons The list of installed and enabled [Addon]s to be shown in the menu.
     */
    data class UpdateAvailableAddons(val availableAddons: List<Addon>) : MenuAction()

    /**
     * [MenuAction] dispatched when an addon is to be installed.
     *
     * @property addon The [Addon] to install.
     * @property addonName The localized name of the addon.
     */
    data class InstallAddon(
        val addon: Addon,
        val addonName: String? = null,
    ) : MenuAction()

    /**
     * [MenuAction] dispatched when an addon installation is in progress.
     *
     * @property addon The [Addon] to install.
     */
    data class UpdateInstallAddonInProgress(val addon: Addon) : MenuAction()

    /**
     * [MenuAction] dispatched when an addon installation was completed with success.
     *
     * @property addon The [Addon] that was installed.
     */
    data class InstallAddonSuccess(val addon: Addon) : MenuAction()

    /**
     * [MenuAction] dispatched when an addon installation failed.
     *
     * @property addon The [Addon] whose installation failed.
     */
    data class InstallAddonFailed(val addon: Addon) : MenuAction()

    /**
     * [MenuAction] dispatched when a custom item is tapped in the custom tab menu.
     *
     * @property intent The [PendingIntent] from the custom menu item.
     * @property url The [String] URL of the current custom tab.
     */
    data class CustomMenuItemAction(
        val intent: PendingIntent,
        val url: String?,
    ) : MenuAction()

    /** [MenuAction] dispatched when requesting to switch to the desktop version of the current page. */
    data object RequestDesktopSite : MenuAction()

    /** [MenuAction] dispatched when requesting to switch to the mobile version of the current page. */
    data object RequestMobileSite : MenuAction()

    /** [MenuAction] dispatched when the menu is clicked. */
    data object OnMoreMenuClicked : MenuAction()

    /** [MenuAction] dispatched to record that the summarize page menu has been exposed to the user */
    data object OnSummarizationMenuExposed : MenuAction()

    /**
     * [MenuAction] dispatched to initialize the state of the summarize page menu.
     *
     * @property state The [SummarizationMenuState] to set up
     */
    data class InitializeSummarizationMenuState(val state: SummarizationMenuState) : MenuAction()

    /**
     * [MenuAction] dispatched to update the IP protection menu item state.
     *
     * @property state The new [IPProtectionMenuState].
     */
    data class UpdateIPProtectionMenuState(val state: IPProtectionMenuState) : MenuAction()

    /** [MenuAction] dispatched when the user clicks the IP protection menu item. */
    data object IPProtectionToggle : MenuAction()

    /**
     * [MenuAction] dispatched when the user expands or collapses the extensions menu item. The expansion itself is
     * owned by the menu item, this only allows observers to react to it.
     */
    data object OnExtensionsMenuClicked : MenuAction()

    /**
     * [MenuAction] dispatched when the user clicks what an extension offers for the current page.
     *
     * @property extensionId The id of the extension owning the clicked action.
     * @property isPageAction Whether the clicked action is a page action, as opposed to a browser action.
     */
    data class WebExtensionActionClicked(
        val extensionId: String,
        val isPageAction: Boolean,
    ) : MenuAction()

    /** [MenuAction] dispatched when the user asks to save the current webpage content as a PDF. */
    data object SaveAsPdfRequested : MenuAction()

    /** [MenuAction] dispatched when the user asks to print the current webpage. */
    data object PrintRequested : MenuAction()

    /** [MenuAction] dispatched when a navigation event occurs for a specific destination. */
    sealed class Navigate : MenuAction() {

        /**
         * [Navigate] action dispatched when navigating to Mozilla account.
         *
         * @property accountState The [AccountState] of a Mozilla account.
         * @property accesspoint The access point that was used to navigate to the menu.
         */
        data class MozillaAccount(
            val accountState: AccountState,
            val accesspoint: MenuAccessPoint,
        ) : Navigate()

        /** [Navigate] action dispatched when navigating to the settings. */
        data object Settings : Navigate()

        /** [Navigate] action dispatched when navigating to the homepage settings. */
        data object CustomizeHomepage : Navigate()

        /** [Navigate] action dispatched when navigating to bookmarks. */
        data object Bookmarks : Navigate()

        /** [Navigate] action dispatched when navigating to history. */
        data object History : Navigate()

        /** [Navigate] action dispatched when navigating to downloads. */
        data object Downloads : Navigate()

        /** [Navigate] action dispatched when navigating to passwords. */
        data object Passwords : Navigate()

        /**
         * [Navigate] action dispatched when navigating to edit the existing bookmark.
         *
         * @property guidToEdit The guid of the bookmark to edit if known.
         */
        data class EditBookmark(val guidToEdit: String? = null) : Navigate()

        /** [Navigate] action dispatched when navigating to add site to home screen. */
        data object AddToHomeScreen : Navigate()

        /** [Navigate] action dispatched when opening the tab group flow. */
        data class OpenTabGroupFlow(val entryPoint: TabGroupFlowEntryPoint) : Navigate()

        /**
         * [Navigate] action dispatched when navigating to save a site to a collection.
         *
         * @property hasCollection Whether or not there are any existing tab collections.
         */
        data class SaveToCollection(val hasCollection: Boolean) : Navigate()

        /** [Navigate] action dispatched when navigating to translations dialog. */
        data object Translate : Navigate()

        /** [Navigate] action dispatched when navigating to the share sheet. */
        data object Share : Navigate()

        /** [Navigate] action dispatched when navigating to the extensions manager. */
        data object ManageExtensions : Navigate()

        /** [Navigate] action dispatched when navigating to the AMO page. */
        data object DiscoverMoreExtensions : Navigate()

        /**
         * [Navigate] action dispatched when navigating to the given [addon] details.
         *
         * @property addon The [Addon] details to display.
         */
        data class AddonDetails(val addon: Addon) : Navigate()

        /**
         * [Navigate] action dispatched when navigating to the given installed [addon] details.
         *
         * @property addon The [Addon] details to display.
         */
        data class InstalledAddonDetails(val addon: Addon) : Navigate()

        /** [Navigate] action dispatched when the user clicks to report a broken site. */
        data object WebCompatReporter : Navigate()

        /** [Navigate] action dispatched when the user clicks to summarize the current page. */
        data object Summarizer : Navigate()

        /**
         * [Navigate] action dispatched when navigating back from the current page.
         *
         * @property viewHistory Whether the tab history menu should be displayed.
         */
        data class Back(val viewHistory: Boolean) : Navigate()

        /**
         * [Navigate] action dispatched when navigating forward from the current page.
         *
         * @property viewHistory Whether the tab history menu should be displayed.
         */
        data class Forward(val viewHistory: Boolean) : Navigate()

        /**
         * [Navigate] action dispatched when reloading the current page.
         *
         * @property bypassCache Whether or not the cache should be bypassed when reloading.
         */
        data class Reload(val bypassCache: Boolean) : Navigate()

        /** [Navigate] action dispatched when stopping the current page from loading. */
        data object Stop : Navigate()

        /** [Navigate] action dispatched when navigating to the IP Protection settings screen. */
        data object IPProtectionSettings : Navigate()

        /** [Navigate] action dispatched when opening a new normal tab. */
        data object NewTab : Navigate()

        /** [Navigate] action dispatched when opening a new private tab. */
        data object NewPrivateTab : Navigate()
    }
}
