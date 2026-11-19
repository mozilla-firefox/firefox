/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.state

import androidx.annotation.VisibleForTesting
import androidx.compose.runtime.Immutable
import mozilla.components.lib.state.State
import org.mozilla.fenix.tabgroups.TabGroupTelemetry
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination
import org.mozilla.fenix.tabstray.syncedtabs.SyncedTabsListItem

@VisibleForTesting internal const val TAB_GROUP_ONBOARDING_IMPRESSION_LIMIT = 3

@VisibleForTesting internal const val MIN_TABS_FOR_TAB_GROUP_ONBOARDING = 2

/**
 * Value type that represents the state of the Tabs Tray.
 *
 * @property selectedPage The current page in the tray can be on.
 * @property mode Whether the browser tab list is in multi-select mode or not with the set of currently selected tabs.
 * @property selectedTabId The ID of the currently selected (active) tab.
 * @property normalTabsState The state of the normal tabs page.
 * @property inactiveTabs The state of inactive tabs, including the list of tabs and UI flags.
 * @property privateBrowsing The state of private browsing, including tabs and locking status.
 * @property tabGroupState The state of the tab group feature.
 * @property sync The state of Synced Tabs, including the list of tabs and sync status.
 * @property config The configuration flags for the Tabs Tray (e.g., grid display, feature flags).
 * @property tabSearchState The state of the tab search feature.
 * @property backStack The navigation history of the Tab Manager feature.
 * @property hasTabDataLoaded Whether the tab data has loaded.
 */
@Immutable
data class TabsTrayState(
    val selectedPage: Page = Page.NormalTabs,
    val mode: Mode = Mode.Normal,
    val selectedTabId: String? = null,
    val normalTabsState: NormalTabsState = NormalTabsState(),
    val inactiveTabs: InactiveTabsState = InactiveTabsState(),
    val privateBrowsing: PrivateBrowsingState = PrivateBrowsingState(),
    val tabGroupState: TabGroupState = TabGroupState(),
    val sync: SyncState = SyncState(),
    val config: TabsTrayConfig = TabsTrayConfig(),
    val tabSearchState: TabSearchState = TabSearchState(),
    val backStack: List<TabManagerNavDestination> = listOf(TabManagerNavDestination.Root),
    val hasTabDataLoaded: Boolean = false,
) : State {

    /** Drops the last entry of [TabsTrayState.backStack]. If [backStack] only has one entry, no changes occur. */
    internal fun popBackStack(): List<TabManagerNavDestination> =
        if (backStack.size > 1) {
            backStack.dropLast(1)
        } else {
            backStack
        }

    /** The current mode that the tabs list is in. */
    @Immutable
    sealed interface Mode {

        /** A set of selected [TabsTrayItem.Tab]s which we would want to perform an action on. */
        val selectedTabs: Set<TabsTrayItem.Tab>

        /** A set of selected [TabsTrayItem.TabGroup]s which we would want to perform an action on. */
        val selectedTabGroups: Set<TabsTrayItem.TabGroup>

        /**
         * Returns true if [item] is selected.
         *
         * @param item The [TabsTrayItem] to search for.
         */
        fun contains(item: TabsTrayItem) = selectedTabs.contains(item) || selectedTabGroups.contains(item)

        /** The default mode the tabs list is in. */
        data object Normal : Mode {
            override val selectedTabs: Set<TabsTrayItem.Tab> = emptySet()

            override val selectedTabGroups: Set<TabsTrayItem.TabGroup> = emptySet()
        }

        /** The multi-select mode that the tabs list is in containing the set of currently selected [TabsTrayItem]s. */
        data class Select(
            override val selectedTabs: Set<TabsTrayItem.Tab> = emptySet(),
            override val selectedTabGroups: Set<TabsTrayItem.TabGroup> = emptySet(),
        ) : Mode

        /**
         * The mode when an item on the tabs list is being dragged
         *
         * @property sourceId: The ID of the tab item being dragged
         * @property destinationId: The ID of a tab item the source item is being dragged onto, if any.
         */
        data class DragAndDrop(
            val sourceId: String,
            val destinationId: String?,
        ) : Mode {
            override val selectedTabs: Set<TabsTrayItem.Tab> = emptySet()

            override val selectedTabGroups: Set<TabsTrayItem.TabGroup> = emptySet()
        }
    }

    /**
     * State specific to normal browsing mode.
     *
     * @property items The list of open [TabsTrayItem]s on the Normal page.
     * @property selectedItemIndex The index of the selected normal item.
     * @property tabCount The total number of open Normal tabs, including inactive tabs and the tabs within tab groups.
     * @property itemFocusIndicatorEnabled Whether the focus indicator may be shown on the Normal tabs page.
     */
    @Immutable
    data class NormalTabsState(
        val items: List<TabsTrayItem> = emptyList(),
        val selectedItemIndex: Int = 0,
        val tabCount: Int = 0,
        val itemFocusIndicatorEnabled: Boolean = true,
    )

    /**
     * State specific to inactive tabs.
     *
     * @property tabs The list of tabs currently considered inactive.
     * @property isExpanded Whether the Inactive Tabs section is expanded in the UI.
     * @property showCFR Whether the Inactive Tabs Contextual Feature Recommendation (CFR) is visible.
     * @property showAutoCloseDialog Whether the dialog to enable auto-closing inactive tabs is visible.
     */
    @Immutable
    data class InactiveTabsState(
        val tabs: List<TabsTrayItem.Tab> = emptyList(),
        val isExpanded: Boolean = false,
        val showCFR: Boolean = false,
        val showAutoCloseDialog: Boolean = false,
    )

    /**
     * State specific to private browsing mode.
     *
     * @property tabs The list of open private tabs.
     * @property selectedItemIndex The index of the selected private tab.
     * @property isLocked Whether Private Browsing Mode is currently locked.
     * @property showLockBanner Whether the banner to enable PBM locking should be displayed.
     */
    @Immutable
    data class PrivateBrowsingState(
        val tabs: List<TabsTrayItem> = emptyList(),
        val selectedItemIndex: Int = 0,
        val isLocked: Boolean = false,
        val showLockBanner: Boolean = false,
    )

    /**
     * State related to the Sync feature.
     *
     * @property isSignedIn Whether the user is currently signed into a Firefox account.
     * @property isSyncing Whether a sync operation is currently in progress.
     * @property syncedTabs The list of tabs retrieved from other synced devices.
     * @property expandedSyncedTabs A list of booleans representing the expansion state of each device section.
     */
    @Immutable
    data class SyncState(
        val isSignedIn: Boolean = false,
        val isSyncing: Boolean = false,
        val syncedTabs: List<SyncedTabsListItem> = emptyList(),
        val expandedSyncedTabs: List<Boolean> = emptyList(),
    )

    /**
     * Configuration and feature flags for the Tabs Tray UI.
     *
     * @property displayTabsInGrid Whether normal and private tabs are displayed in a grid (vs list).
     * @property tabGroupsEnabled Whether the Tab Groups feature is enabled.
     * @property tabGroupsDragAndDropEnabled: Whether drag and drop is enabled for Tab Groups.
     * @property tabGroupsOnboardingEnabled Whether the onboarding card for Tab Groups is enabled.
     * @property tabGroupsLiveReorderEnabled Whether in-place reorder is enabled for drag and drop.
     * @property homepageAsNewTabEnabled Whether the homepage as a new tab feature is enabled, which gates the Tab
     *   Groups create FAB.
     * @property isInDebugMode Whether the app is in a debug state or has secret menu enabled.
     * @property showTabAutoCloseBanner Whether the banner for the tab auto-closer feature is visible.
     */
    data class TabsTrayConfig(
        val displayTabsInGrid: Boolean = false,
        val tabGroupsEnabled: Boolean = false,
        val tabGroupsDragAndDropEnabled: Boolean = false,
        val tabGroupsOnboardingEnabled: Boolean = false,
        val tabGroupsLiveReorderEnabled: Boolean = false,
        val homepageAsNewTabEnabled: Boolean = false,
        val isInDebugMode: Boolean = false,
        val showTabAutoCloseBanner: Boolean = false,
    )

    /**
     * State specific to Tab Groups.
     *
     * @property groups The list of tab groups.
     * @property formState The state of the tab group edit form.
     * @property hasUserDismissedTabGroupOnboarding Whether the user has previously dismissed the onboarding.
     * @property tabGroupOnboardingImpressionCount How many times the user has been presented the onboarding.
     * @property hasUserEverHadOneTabGroup Whether the user has ever had a tab group.
     * @property hasViewedTabGroupsPage Whether the user has viewed the Tab Groups page.
     * @property skipUngroupConfirmation Whether the ungroup confirmation dialog should be skipped.
     * @property hasRecordedOnboardingImpression Whether an onboarding impression has been recorded this session.
     * @property showCollectionsMigrationCard Whether the Collections to Tab Groups migration card is shown.
     * @property enteringGroupId Recently created group id, to be referenced for animations. Cleared after entrance
     *   animations are played.
     * @property dragProcessingState The lifecycle state of tab-group drag handling
     */
    @Immutable
    data class TabGroupState(
        override val groups: List<TabsTrayItem.TabGroup> = emptyList(),
        override val formState: TabGroupFormState? = null,
        internal val hasUserDismissedTabGroupOnboarding: Boolean = false,
        internal val tabGroupOnboardingImpressionCount: Int = 0,
        internal val hasUserEverHadOneTabGroup: Boolean = false,
        internal val hasViewedTabGroupsPage: Boolean = false,
        internal val skipUngroupConfirmation: Boolean = false,
        internal val hasRecordedOnboardingImpression: Boolean = false,
        val showCollectionsMigrationCard: Boolean = false,
        val enteringGroupId: String? = null,
        val dragProcessingState: DragProcessingState = DragProcessingState.UNINITIALIZED,
    ) : TabGroupTelemetry.TabGroupTelemetryContext

    /** State for the drag handling flow for Tab Groups. */
    enum class DragProcessingState {
        UNINITIALIZED,
        DRAG_IN_PROGRESS,
        EDIT_IN_PROGRESS,
        COMPLETED,
    }

    /** Whether the Tab Search button is visible. */
    val searchIconVisible: Boolean
        get() = selectedPage != Page.SyncedTabs

    /** Whether the Tab Search button is enabled. */
    val searchIconEnabled: Boolean
        get() =
            when {
                selectedPage == Page.NormalTabs && normalTabsState.items.isNotEmpty() -> true
                selectedPage == Page.PrivateTabs && privateBrowsing.tabs.isNotEmpty() -> true
                else -> false
            }

    /**
     * Show onboarding for tab groups if these conditions are met:
     * - Onboarding for tab groups is enabled.
     * - Drag and drop to create tab groups is enabled.
     * - The user has a selected tab.
     * - The user has no existing tab groups.
     * - The user has at least [MIN_TABS_FOR_TAB_GROUP_ONBOARDING] tabs.
     * - The user has not dismissed the onboarding.
     * - The user has never had a tab group.
     * - The user has seen the onboarding fewer than [TAB_GROUP_ONBOARDING_IMPRESSION_LIMIT] instances.
     */
    val shouldShowTabGroupOnboarding: Boolean
        get() =
            config.tabGroupsOnboardingEnabled &&
                config.tabGroupsDragAndDropEnabled &&
                normalTabsState.selectedItemIndex in normalTabsState.items.indices &&
                tabGroupState.groups.isEmpty() &&
                normalTabsState.items.count { it is TabsTrayItem.Tab } >= MIN_TABS_FOR_TAB_GROUP_ONBOARDING &&
                !tabGroupState.hasUserDismissedTabGroupOnboarding &&
                !tabGroupState.hasUserEverHadOneTabGroup &&
                tabGroupState.tabGroupOnboardingImpressionCount < TAB_GROUP_ONBOARDING_IMPRESSION_LIMIT

    /** Whether to show the new-content badge on the Tab Groups page button. */
    val shouldShowTabGroupBadge: Boolean
        get() = config.tabGroupsEnabled && !tabGroupState.hasViewedTabGroupsPage && tabGroupState.groups.isNotEmpty()

    /** Whether the floating toolbar should be visible. */
    val isFloatingToolbarVisible: Boolean
        get() {
            if (mode !is Mode.Normal) return false

            return when (selectedPage) {
                Page.NormalTabs -> true
                Page.PrivateTabs -> !privateBrowsing.isLocked
                Page.TabGroups -> config.homepageAsNewTabEnabled
                Page.SyncedTabs -> true
            }
        }
}
