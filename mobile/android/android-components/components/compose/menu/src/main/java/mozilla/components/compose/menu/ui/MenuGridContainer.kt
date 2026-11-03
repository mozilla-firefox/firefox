/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.BannerMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.ui.icons.R as iconsR

/**
 * Display a grid of [MenuItem]s using [MenuGridItem].
 *
 * @param items The list of [MenuItem]s to display. Items without an icon and [BannerMenuItem]s, which need the full
 *   width of the menu, are not shown.
 * @param onInteraction The callback to invoke when a menu item is interacted with.
 * @param modifier The modifier to apply to this layout.
 * @param isSticky Whether this group is sticky.
 * @param backgroundColor The background color to apply to this group.
 */
@Composable
internal fun MenuGridContainer(
    items: List<MenuItem>,
    onInteraction: (MenuEvent) -> Unit,
    modifier: Modifier = Modifier,
    isSticky: Boolean = false,
    backgroundColor: Color = if (isSticky) MaterialTheme.colorScheme.surface else Color.Transparent,
) {
    val displayedItems = items.displayedInGrid()
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .height(IntrinsicSize.Min)
                .applyGridGroupStyle(isSticky, backgroundColor)
                .menuGridGroupSemantics(displayedItems.size),
        horizontalArrangement =
            Arrangement.spacedBy(if (isSticky) AcornTheme.layout.space.static50 else AcornTheme.layout.space.static25),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        GridGroupContent(displayedItems, onInteraction, isSticky)
    }
}

@Composable
private fun Modifier.applyGridGroupStyle(
    isSticky: Boolean,
    backgroundColor: Color,
): Modifier {
    val horizontal = if (isSticky) AcornTheme.layout.space.static50 else 0.dp
    val vertical = if (isSticky) AcornTheme.layout.space.static150 else 0.dp

    return this.let { if (isSticky) it else it.clip(MaterialTheme.shapes.extraLarge) }
        .background(backgroundColor)
        .padding(horizontal = horizontal, vertical = vertical)
}

@Composable
private fun RowScope.GridGroupContent(
    items: List<MenuItem>,
    onInteraction: (MenuEvent) -> Unit,
    isSticky: Boolean,
) {
    items.forEachIndexed { index, item ->
        val icon = item.icon ?: return@forEachIndexed
        val shape =
            if (isSticky) {
                RectangleShape
            } else {
                getGridItemShape(index, items.size)
            }

        MenuGridItem(
            title = item.title.text,
            contentDescription = item.contentDescription,
            onClickEvent = item.onClickEvent,
            onInteraction = onInteraction,
            modifier = Modifier.weight(1f).fillMaxHeight(),
            index = index,
            icon = icon,
            state = item.state,
            onLongClickEvent = item.onLongClickEvent,
            showContainer = !isSticky,
            shape = shape,
        )
    }
}

@PreviewLightDark
@Composable
private fun MenuGridContainerPreview() = AcornTheme {
    MenuGridContainer(
        items =
            listOf(
                StandardMenuItem(
                    title = MenuItemTitle(Text.String("Back")),
                    contentDescription = Text.String("Back"),
                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_back_24),
                    onClickEvent = object : MenuEvent {},
                    state = MenuItemState.DISABLED,
                ),
                StandardMenuItem(
                    title = MenuItemTitle(Text.String("Forward")),
                    contentDescription = Text.String("Forward"),
                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_forward_24),
                    onClickEvent = object : MenuEvent {},
                    state = MenuItemState.DISABLED,
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
            ),
        onInteraction = {},
        isSticky = true,
    )
}

@PreviewLightDark
@Composable
private fun MenuGridContainerNotStickyPreview() = AcornTheme {
    MenuGridContainer(
        items =
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
            ),
        onInteraction = {},
        isSticky = false,
    )
}
