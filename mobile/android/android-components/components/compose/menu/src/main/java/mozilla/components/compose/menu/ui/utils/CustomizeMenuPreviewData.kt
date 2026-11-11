/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.utils

import androidx.annotation.DrawableRes
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR

/** [MenuItemsGroup]s to show in the preview of the menu customization screen. */
internal val customizeMenuPreviewData: List<MenuItemsGroup> =
    listOf(
        MenuItemsGroup.Grid(
            id = "navigation",
            items =
                listOf(
                    previewMenuItem("Back", iconsR.drawable.mozac_ic_back_24),
                    previewMenuItem("Forward", iconsR.drawable.mozac_ic_forward_24),
                    previewMenuItem("Share", iconsR.drawable.mozac_ic_share_android_24),
                    previewMenuItem("Refresh", iconsR.drawable.mozac_ic_arrow_clockwise_24),
                ),
            isSticky = true,
        ),
        MenuItemsGroup.Row(
            id = "vpn",
            items = listOf(previewMenuItem("VPN", iconsR.drawable.mozac_ic_globe_24)),
        ),
        MenuItemsGroup.Row(
            id = "tabs",
            items =
                listOf(
                    previewMenuItem("New tab", iconsR.drawable.mozac_ic_plus_24),
                    previewMenuItem("New private tab", iconsR.drawable.mozac_ic_private_mode_24),
                ),
        ),
        MenuItemsGroup.Row(
            id = "page",
            items =
                listOf(
                    previewMenuItem("Bookmark page", iconsR.drawable.mozac_ic_bookmark_24),
                    previewMenuItem("Find in page", iconsR.drawable.mozac_ic_search_24),
                    previewMenuItem("Desktop site", iconsR.drawable.mozac_ic_device_desktop_24),
                ),
        ),
        MenuItemsGroup.Grid(
            id = "library",
            items =
                listOf(
                    previewMenuItem("History", iconsR.drawable.mozac_ic_history_24),
                    previewMenuItem("Bookmarks", iconsR.drawable.mozac_ic_bookmark_tray_fill_24),
                    previewMenuItem("Downloads", iconsR.drawable.mozac_ic_download_24),
                    previewMenuItem("Passwords", iconsR.drawable.mozac_ic_login_24),
                ),
        ),
        MenuItemsGroup.Row(
            id = "account",
            items =
                listOf(
                    previewMenuItem("Sign in", iconsR.drawable.mozac_ic_avatar_circle_24),
                    previewMenuItem("Settings", iconsR.drawable.mozac_ic_settings_24),
                ),
        ),
    )

private fun previewMenuItem(title: String, @DrawableRes icon: Int): MenuItem =
    StandardMenuItem(
        title = MenuItemTitle(Text.String(title)),
        icon = MenuItemIconRes(icon),
        onClickEvent = object : MenuEvent {},
    )
