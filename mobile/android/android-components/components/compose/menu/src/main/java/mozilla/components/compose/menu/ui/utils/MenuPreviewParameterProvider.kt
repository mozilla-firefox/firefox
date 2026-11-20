/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.utils

import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.ui.icons.R as iconsR

/** [MenuItemsGroup]s to be used in the preview of the [Menu] composable. */
internal class MenuPreviewParameterProvider : PreviewParameterProvider<List<MenuItemsGroup>> {
    override val values =
        sequenceOf(
            listOf(
                MenuItemsGroup.Grid(
                    id = "first",
                    items = navigationItems,
                    isSticky = true,
                ),
                MenuItemsGroup.Row(
                    id = "second",
                    items = libraryItems,
                ),
                MenuItemsGroup.Grid(
                    id = "third",
                    items = libraryItems,
                ),
                MenuItemsGroup.Row(
                    id = "fourth",
                    items = navigationItems,
                ),
                MenuItemsGroup.Grid(
                    id = "fifth",
                    items = navigationItems,
                    isSticky = true,
                ),
            )
        )
}

private val navigationItems =
    listOf(
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Back")),
            contentDescription = Text.String("Back"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_back_24),
            state = MenuItemState.DISABLED,
            onClickEvent = object : MenuEvent {},
        ),
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Forward")),
            contentDescription = Text.String("Forward"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_forward_24),
            state = MenuItemState.DISABLED,
            onClickEvent = object : MenuEvent {},
        ),
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Bookmark")),
            contentDescription = Text.String("Bookmark"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_bookmark_fill_24),
            state = MenuItemState.ACTIVE,
            onClickEvent = object : MenuEvent {},
        ),
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Share")),
            contentDescription = Text.String("Share"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_share_android_24),
            onClickEvent = object : MenuEvent {},
        ),
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Refresh")),
            contentDescription = Text.String("Refresh"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_arrow_clockwise_24),
            onClickEvent = object : MenuEvent {},
        ),
    )

private val libraryItems =
    listOf(
        StandardMenuItem(
            title = MenuItemTitle(Text.String("History")),
            contentDescription = Text.String("History"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_history_24),
            onClickEvent = object : MenuEvent {},
        ),
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Save")),
            contentDescription = Text.String("Save"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_save_24),
            onClickEvent = object : MenuEvent {},
        ),
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Downloads")),
            contentDescription = Text.String("Downloads"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_download_24),
            onClickEvent = object : MenuEvent {},
        ),
        StandardMenuItem(
            title = MenuItemTitle(Text.String("Passwords")),
            contentDescription = Text.String("Passwords"),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_lock_24),
            onClickEvent = object : MenuEvent {},
        ),
    )
