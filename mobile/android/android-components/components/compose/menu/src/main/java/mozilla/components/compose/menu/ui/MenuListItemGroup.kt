/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.BannerMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.utils.MenuListItemsParameterProvider

/**
 * Display a list of [MenuItem]s as a vertical list.
 *
 * @param items The list of [MenuItem]s to display.
 * @param onClick The callback to invoke when a menu item is clicked.
 * @param modifier The modifier to apply to this layout.
 */
@Composable
internal fun ListMenuItemsGroup(
    items: List<MenuItem>,
    onClick: (MenuEvent) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier.clip(shape = MaterialTheme.shapes.extraLarge).menuListGroupSemantics(items.size),
        verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static25),
    ) {
        items.forEachIndexed { index, it ->
            val shape = getListItemShape(itemIndex = index, totalItems = items.size)
            val itemModifier = Modifier.clip(shape)
            when (it) {
                is StandardMenuItem ->
                    MenuListItem(
                        title = it.title,
                        contentDescription = it.contentDescription,
                        modifier = itemModifier,
                        index = index,
                        role = it.role,
                        summary = it.summary,
                        icon = it.icon,
                        showNewIndicator = it.showNewIndicator,
                        badge = it.badge,
                        actionButton = it.actionButton,
                        trailingIcon = it.trailingIcon,
                        state = it.state,
                        onClickEvent = it.onClickEvent,
                        onInteraction = onClick,
                    )

                is ExpandableMenuItem ->
                    ExpandableHeaderItem(
                        title = it.title,
                        contentDescription = it.contentDescription,
                        subMenuItems = it.subMenuItems,
                        modifier = itemModifier,
                        hideOnExpand = it.hideOnExpand,
                        onClickEvent = it.onClickEvent,
                        onInteraction = onClick,
                        role = it.role,
                        summary = it.summary,
                        icon = it.icon,
                        showNewIndicator = it.showNewIndicator,
                        badge = it.badge,
                        actionButtonText = it.actionButtonText,
                        state = it.state,
                    )

                is BannerMenuItem ->
                    MenuBanner(
                        title = it.title.text,
                        summary = it.summary,
                        icon = it.icon,
                        onDismiss = { onClick(it.onDismissEvent) },
                        onClick = { onClick(it.onClickEvent) },
                        modifier = itemModifier,
                    )
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun ListMenuItemsGroupPreview(@PreviewParameter(MenuListItemsParameterProvider::class) items: List<MenuItem>) =
    AcornTheme {
        ListMenuItemsGroup(
            items = items,
            onClick = {},
        )
    }
