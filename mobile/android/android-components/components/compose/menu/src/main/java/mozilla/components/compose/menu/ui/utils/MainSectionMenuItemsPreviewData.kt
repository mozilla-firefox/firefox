/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.utils

import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemActionButton
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.compose.menu.ui.MenuItemState.ACTIVE
import mozilla.components.ui.icons.R as iconsR

/** [MenuItem]s to show in the main section of the menu composable preview. */
internal val mainSectionMenuItemsPreviewData =
    listOf(
        StandardMenuItem(
            title = MenuItemTitle(Text.String("VPN")),
            contentDescription = Text.String("VPN"),
            summary =
                MenuItemSummary(
                    text = Text.String("1 TB limit reached"),
                    state = MenuItemState.WARNING,
                ),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
            badge = MenuItemBadge(Text.String("Paused")),
            actionButton =
                MenuItemActionButton(
                    icon = iconsR.drawable.mozac_ic_chevron_right_24,
                    contentDescription = Text.String(""),
                    showDivider = true,
                    onClickEvent = object : MenuEvent {},
                ),
            onClickEvent = object : MenuEvent {},
        ),
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Find in page")),
            contentDescription = Text.String("Find in page"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_find_in_page_24),
            onClickEvent = object : MenuEvent {},
        ),
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Desktop site")),
            contentDescription = Text.String("Desktop site"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_device_desktop_fill_24),
            badge =
                MenuItemBadge(
                    Text.String("On"),
                    state = ACTIVE,
                ),
            onClickEvent = object : MenuEvent {},
            state = ACTIVE,
        ),
        ExpandableMenuItem(
            title = MenuItemTitle(Text.String("Extensions")),
            contentDescription = Text.String("Extensions"),
            subMenuItems = emptyList(),
            onClickEvent = object : MenuEvent {},
            summary = MenuItemSummary(Text.String("Summary")),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_extension_24),
            actionButtonText = Text.String("22"),
        ),
    )
