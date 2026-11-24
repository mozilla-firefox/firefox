/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.utils

import androidx.annotation.DrawableRes
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR

/** The shown items to show in the preview of the screen customizing the items of a menu section. */
internal val editMenuItemsPreviewData: List<CustomizableMenuItem> =
    listOf(
        previewItem("bookmark", "Bookmark page", iconsR.drawable.mozac_ic_bookmark_24),
        previewItem("find", "Find in page", iconsR.drawable.mozac_ic_search_24),
        CustomizableMenuItem(
            id = "more",
            item =
                ExpandableMenuItem(
                    title = MenuItemTitle(Text.String("More")),
                    onClickEvent = object : MenuEvent {},
                    subMenuItems = emptyList(),
                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_ellipsis_horizontal_24),
                ),
            subItems = listOf(previewItem("desktop", "Desktop site", iconsR.drawable.mozac_ic_device_desktop_24)),
        ),
    )

/** The hidden items to show in the preview of the screen customizing the items of a menu section. */
internal val editMenuItemsHiddenPreviewData: List<CustomizableMenuItem> =
    listOf(previewItem("save", "Save to collection", iconsR.drawable.mozac_ic_collection_24))

private fun previewItem(id: String, title: String, @DrawableRes icon: Int) =
    CustomizableMenuItem(
        id = id,
        item =
            StandardMenuItem(
                title = MenuItemTitle(Text.String(title)),
                icon = MenuItemIconRes(icon),
                onClickEvent = object : MenuEvent {},
            ),
    )
