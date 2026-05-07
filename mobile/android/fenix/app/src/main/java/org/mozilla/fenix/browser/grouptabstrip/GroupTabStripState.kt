/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.grouptabstrip

import android.graphics.Bitmap
import mozilla.components.browser.state.selector.normalTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import org.mozilla.fenix.tabgroups.storage.database.StoredTabGroup
import org.mozilla.fenix.tabstray.data.TabGroupTheme

/**
 * The UI state for the bottom group tab strip shown while browsing inside a tab group.
 *
 * @property groupId The ID of the group the strip represents.
 * @property groupTitle The display title of the group, or empty if the group has no title.
 * @property groupTheme The group's [TabGroupTheme] used for the accent.
 * @property tabs The tabs that belong to this group, in storage order.
 */
data class GroupTabStripState(
    val groupId: String,
    val groupTitle: String,
    val groupTheme: TabGroupTheme,
    val tabs: List<GroupTabStripItem>,
)

/**
 * A tab item displayed in the group tab strip.
 *
 * @property id The tab id.
 * @property title The tab's title, falling back to the URL when blank.
 * @property url The tab's url.
 * @property icon The tab's favicon, if available.
 * @property isSelected Whether this tab is the currently active tab.
 */
data class GroupTabStripItem(
    val id: String,
    val title: String,
    val url: String,
    val icon: Bitmap?,
    val isSelected: Boolean,
)

/**
 * Computes the [GroupTabStripState] for the currently selected tab. Returns null when:
 *  - the selected tab is private (private tabs do not belong to groups),
 *  - the selected tab has no group assignment,
 *  - the selected tab's group is closed or unknown.
 *
 * @param tabGroups The list of [StoredTabGroup]s observed from the repository.
 * @param tabGroupAssignments The map of `tabId -> tabGroupId` observed from the repository.
 */
internal fun BrowserState.toGroupTabStripState(
    tabGroups: List<StoredTabGroup>,
    tabGroupAssignments: Map<String, String>,
): GroupTabStripState? {
    val selected = selectedTab ?: return null
    if (selected.content.private) return null

    val groupId = tabGroupAssignments[selected.id] ?: return null
    val group = tabGroups.firstOrNull { it.id == groupId } ?: return null
    if (group.closed) return null

    val tabsInGroup = normalTabs
        .filter { tabGroupAssignments[it.id] == groupId }
        .map { tab ->
            GroupTabStripItem(
                id = tab.id,
                title = tab.content.title.ifBlank { tab.content.url },
                url = tab.content.url,
                icon = tab.content.icon,
                isSelected = tab.id == selected.id,
            )
        }

    if (tabsInGroup.isEmpty()) return null

    return GroupTabStripState(
        groupId = group.id,
        groupTitle = group.title,
        groupTheme = group.theme.toTabGroupThemeOrDefault(),
        tabs = tabsInGroup,
    )
}

private fun String.toTabGroupThemeOrDefault(): TabGroupTheme = try {
    TabGroupTheme.valueOf(this)
} catch (_: IllegalArgumentException) {
    TabGroupTheme.default
}
