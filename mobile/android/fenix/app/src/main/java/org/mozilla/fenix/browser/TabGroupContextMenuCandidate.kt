/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import android.content.res.Resources
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.HitResult
import mozilla.components.feature.contextmenu.ContextMenuCandidate
import mozilla.components.feature.contextmenu.isHttpLink
import mozilla.components.feature.contextmenu.isUrlSchemeAllowed
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.data.findTab
import mozilla.components.feature.tabdata.coordinator.data.findTabGroup
import mozilla.components.feature.tabs.TabsUseCases
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.tabstray.data.TabGroupTheme

/** Provides the "Open in new tab in group" context menu candidate for [BrowserFragment]. */
object TabGroupContextMenuCandidate {

    const val OPEN_IN_TAB_GROUP_ID = "fenix.contextmenu.open_in_tab_group"

    /**
     * Context menu item: "Open in tab group".
     *
     * Opens the long-pressed link in an unselected background tab and adds it to the group the active tab belongs to.
     * When the active tab is not in a group, a new group holding both tabs is created.
     *
     * @param resources [Resources] used to obtain the title of menu item candidate.
     * @param tabGroupsEnabled [Boolean] Used to check whether the Tab Groups feature is enabled.
     * @param tabGroupsStripEnabled Whether the Tab Groups strip is enabled, in which case no snackbar is shown.
     * @param tabsUseCases Used to open the link in a new tab.
     * @param tabDataCoordinator Used to resolve and update which group the new tab belongs to.
     * @param appStore Used to show the snackbar confirming which group the new tab was added to.
     * @param scope Scope used to add the new tab to its group before the snackbar is shown.
     */
    fun createOpenInNewTabInGroupCandidate(
        resources: Resources,
        tabGroupsEnabled: Boolean,
        tabGroupsStripEnabled: Boolean,
        tabsUseCases: TabsUseCases,
        tabDataCoordinator: TabDataCoordinator,
        appStore: AppStore,
        scope: CoroutineScope,
    ) =
        ContextMenuCandidate(
            id = OPEN_IN_TAB_GROUP_ID,
            label = resources.getString(R.string.contextmenu_open_in_tab_group),
            showFor = { tab, hitResult ->
                shouldShowCandidate(tab = tab, hitResult = hitResult, tabGroupsEnabled = tabGroupsEnabled)
            },
            action = { parentTab, hitResult ->
                scope.launch {
                    openLinkInActiveTabsGroup(
                        parentTab = parentTab,
                        hitResult = hitResult,
                        resources = resources,
                        tabsUseCases = tabsUseCases,
                        tabDataCoordinator = tabDataCoordinator,
                        tabGroupsStripEnabled = tabGroupsStripEnabled,
                        appStore = appStore,
                    )
                }
            },
        )

    /**
     * Whether the "Open in tab group" item should be shown for the long-pressed [hitResult] in [tab].
     *
     * @param tab The [SessionState] the context menu was opened from.
     * @param hitResult The [HitResult] describing the long-pressed element.
     * @param tabGroupsEnabled Whether the Tab Groups feature is enabled.
     */
    @VisibleForTesting
    internal fun shouldShowCandidate(
        tab: SessionState,
        hitResult: HitResult,
        tabGroupsEnabled: Boolean,
    ): Boolean =
        tabGroupsEnabled && !tab.content.private && hitResult.isHttpLink() && tab.isUrlSchemeAllowed(hitResult.getUrl())

    /**
     * Opens the [hitResult] link in an unselected background tab and adds it to the group [parentTab] belongs to,
     * creating a new group holding both tabs when [parentTab] is not in one.
     *
     * @param parentTab The [SessionState] the context menu was opened from.
     * @param hitResult The [HitResult] describing the long-pressed element.
     * @param resources [Resources]used to name a newly created group.
     * @param tabsUseCases Used to open the link in a new tab.
     * @param tabDataCoordinator Used to resolve and update which group the new tab belongs to.
     * @param tabGroupsStripEnabled Whether the Tab Groups strip is enabled, in which case no snackbar is shown.
     * @param appStore Used to show the snackbar confirming which group the new tab was added to.
     */
    @VisibleForTesting
    internal suspend fun openLinkInActiveTabsGroup(
        parentTab: SessionState,
        hitResult: HitResult,
        resources: Resources,
        tabsUseCases: TabsUseCases,
        tabDataCoordinator: TabDataCoordinator,
        tabGroupsStripEnabled: Boolean,
        appStore: AppStore,
    ) {
        val newTabId =
            tabsUseCases.addTab(
                url = hitResult.getUrl(),
                selectTab = false,
                startLoading = true,
                textDirectiveUserActivation = true,
                parentId = parentTab.id,
                contextId = parentTab.contextId,
            )

        val snapshot = tabDataCoordinator.tabsSnapshot.filterNotNull().first()
        val parentGroup = snapshot.findTab(id = parentTab.id)?.tabGroupId?.let { snapshot.findTabGroup(id = it) }
        val groupTitle =
            parentGroup?.title
                ?: resources.getString(R.string.create_tab_group_form_default_name, snapshot.tabGroups.size + 1)

        val tabGroupId =
            if (parentGroup != null) {
                tabDataCoordinator.addTabsToTabGroup(tabGroupId = parentGroup.id, tabIds = listOf(newTabId))
                parentGroup.id
            } else {
                tabDataCoordinator.createTabGroup(
                    title = groupTitle,
                    theme = TabGroupTheme.default.toStorageValue(),
                    tabIds = listOf(parentTab.id, newTabId),
                    anchorTabId = parentTab.id,
                )
            }

        if (!tabGroupsStripEnabled) {
            appStore.dispatch(AppAction.TabAddedToGroup(groupTitle = groupTitle, groupId = tabGroupId))
        }
    }
}
