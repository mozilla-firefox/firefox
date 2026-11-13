/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.action

import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction.TabsStorageAction

/** [TabsTrayAction]'s that represent user interactions for the Tab Group feature. */
sealed interface TabGroupAction : TabsTrayAction {
    /** Fired when the user clicks on adding tab(s) to a tab group. */
    data object AddToTabGroup : TabGroupAction

    /** Fired when the user clicks on adding tab(s) to a new tab group. */
    data object AddToNewTabGroup : TabGroupAction

    /** Fired when the user clicks the Tab Groups page FAB to create a starter tab group. */
    data object NewTabGroupFabClicked : TabGroupAction

    /** Fired when the user clicks the normal tabs menu item to create a starter tab group. */
    data object NewTabGroupMenuClicked : TabGroupAction

    /**
     * Navigates to the expanded view of a tab group that was created.
     *
     * @property group The newly created [TabsTrayItem.TabGroup] to open.
     */
    data class OpenCreatedTabGroup(val group: TabsTrayItem.TabGroup) : TabGroupAction

    /** Fired when the user drags a tab onto another to create a new tab group. */
    data class DragAndDropTwoTabs(val sourceTabId: String, val destinationTabId: String) : TabGroupAction

    /** Fired when the drag and drop handling is complete. */
    data object DragAndDropProcessed : TabGroupAction

    /**
     * Fired when the user changes the tab group name.
     *
     * @property name The name of the tab group the user has typed in.
     */
    data class NameChanged(val name: String) : TabGroupAction

    /** Confirms the save of a tab group. */
    data object SaveClicked : TabGroupAction, TabsStorageAction

    /**
     * Fired when the user clicks on a Tab Group.
     *
     * @property group The clicked [TabsTrayItem.TabGroup].
     */
    data class TabGroupClicked(val group: TabsTrayItem.TabGroup) : TabGroupAction

    /**
     * Fired when the user clicks delete on a Tab Group.
     *
     * @property group The clicked [TabsTrayItem.TabGroup].
     */
    data class DeleteClicked(val group: TabsTrayItem.TabGroup) : TabGroupAction

    /**
     * Fired when the user confirms to delete a Tab Group.
     *
     * @property group The clicked [TabsTrayItem.TabGroup].
     */
    data class DeleteConfirmed(val group: TabsTrayItem.TabGroup) : TabGroupAction, TabsStorageAction

    /**
     * Fired when the user clicks ungroup on a Tab Group. Resolved into either [UngroupConfirmationRequested] or
     * [UngroupConfirmed] depending on whether the confirmation dialog is being skipped.
     *
     * @property group The [TabsTrayItem.TabGroup] to be ungrouped.
     */
    data class UngroupRequested(val group: TabsTrayItem.TabGroup) : TabGroupAction, TabManagerUiStateStorageAction

    /**
     * Fired when the ungroup confirmation dialog should be shown for a Tab Group.
     *
     * @property group The [TabsTrayItem.TabGroup] to be ungrouped.
     */
    data class UngroupConfirmationRequested(val group: TabsTrayItem.TabGroup) : TabGroupAction

    /**
     * Fired when the user confirms they want to ungroup a Tab Group.
     *
     * @property group The [TabsTrayItem.TabGroup] to be ungrouped.
     * @property dontAskAgain Whether the user asked to suppress this confirmation in the future.
     */
    data class UngroupConfirmed(
        val group: TabsTrayItem.TabGroup,
        val dontAskAgain: Boolean,
    ) : TabGroupAction, TabManagerUiStateStorageAction, TabsStorageAction

    /**
     * Invoked when the user changes the tab group theme.
     *
     * @property theme The theme of the tab group the user has selected.
     */
    data class ThemeChanged(val theme: TabGroupTheme) : TabGroupAction

    /**
     * Invoked when a new tab group is created.
     *
     * @property id The tab group's id
     */
    data class NewGroupCreated(val id: String) : TabGroupAction

    /**
     * Fired when the user performs an action to add the current collection of multiselected items to an existing Tab
     * Group.
     *
     * @property groupId The ID of the group the tabs are being added into.
     */
    data class SelectedTabsAddedToGroup(val groupId: String) : TabGroupAction, TabsStorageAction

    /**
     * Fired when the user performs an action to add a single item to a new Tab Group, such as from the menu.
     *
     * @property tabId The ID of the tab.
     */
    data class TabAddedToNewTabGroup(val tabId: String) : TabGroupAction, TabsStorageAction

    /**
     * Fired when the user performs an action to add a single item to an existing Tab Group, such as a drag and drop.
     *
     * @property tabId The ID of the tab.
     * @property groupId The ID of the group the tab is being added into.
     * @property groupTitle The title of the group, used to display a SnackBar.
     */
    data class TabAddedToExistingTabGroup(val tabId: String, val groupId: String, val groupTitle: String) :
        TabGroupAction, TabsStorageAction

    /**
     * Fired when the user performs an action to move a tab from one group to another.
     *
     * @property tabId The ID of the tab.
     * @property groupId The ID of the group the tab is being added into.
     * @property groupTitle The title of the group, used to display a SnackBar.
     */
    data class TabMovedToExistingTabGroup(val tabId: String, val groupId: String, val groupTitle: String) :
        TabGroupAction, TabsStorageAction

    /**
     * Invoked when the user clicks to edit a tab group.
     *
     * @property group The [TabsTrayItem.TabGroup] to be edited.
     */
    data class EditTabGroupClicked(val group: TabsTrayItem.TabGroup) : TabGroupAction

    /**
     * Invoked when the user clicks to open a tab group from tab groups page.
     *
     * @property group The [TabsTrayItem.TabGroup] to be opened.
     */
    data class OpenTabGroupClicked(val group: TabsTrayItem.TabGroup) : TabGroupAction, TabsStorageAction

    /**
     * Invoked when the user clicks to close a tab group.
     *
     * @property group The [TabsTrayItem.TabGroup] to be closed.
     */
    data class CloseTabGroupClicked(val group: TabsTrayItem.TabGroup) : TabGroupAction, TabsStorageAction

    /**
     * [TabGroupAction] fired when one [TabsTrayItem] is dropped onto another.
     *
     * @property sourceId The id of the source item
     * @property destinationId The id of the destination item
     */
    data class DragAndDropInitiated(val sourceId: String, val destinationId: String) : TabGroupAction, TabsStorageAction

    /**
     * Fired when the user confirms they want to close the last tab and delete the Tab Group.
     *
     * @property group The [TabsTrayItem.TabGroup] to be deleted.
     */
    data class CloseTabAndDeleteGroupConfirmed(val group: TabsTrayItem.TabGroup) : TabGroupAction, TabsStorageAction

    /**
     * Fired when a user clicks to close a specific tab within an expanded tab group.
     *
     * @property tab The [TabsTrayItem.Tab] that is being closed.
     * @property group The [TabsTrayItem.TabGroup] that contains the tab.
     */
    data class TabClosed(
        val tab: TabsTrayItem.Tab,
        val group: TabsTrayItem.TabGroup,
    ) : TabGroupAction, TabsStorageAction

    /** Invoked when the user dismisses the tab group onboarding card. */
    data object OnboardingDismissed : TabGroupAction, TabManagerUiStateStorageAction

    /** Invoked when the tab group onboarding card is shown to the user. */
    data object OnboardingShown : TabGroupAction, TabManagerUiStateStorageAction

    /** Invoked when a new group's animation is played. */
    data object NewGroupAnimationFinished : TabGroupAction

    /** Invoked when the user dismisses the Collections to Tab Groups migration card. */
    data object CollectionsMigrationCardDismissed : TabGroupAction

    /** Invoked when back navigation is requested. */
    data object NavigateBackInvoked : TabGroupAction

    /** Pushes enriched [TabsTrayItem.TabGroup] objects from a subscription to the store */
    data class TabGroupsUpdated(val tabGroups: List<TabsTrayItem.TabGroup>) : TabGroupAction
}
