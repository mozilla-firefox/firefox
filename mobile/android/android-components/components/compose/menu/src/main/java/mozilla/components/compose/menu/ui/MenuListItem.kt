/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.annotation.DrawableRes
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.VerticalDivider
import androidx.compose.material3.minimumInteractiveComponentSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment.Companion.CenterVertically
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.CollectionItemInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.Role.Companion.Button
import androidx.compose.ui.semantics.collectionItemInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.text.value
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemActionButton
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemState.ACTIVE
import mozilla.components.compose.menu.ui.MenuItemState.DEFAULT
import mozilla.components.compose.menu.ui.MenuItemState.DISABLED
import mozilla.components.compose.menu.ui.MenuItemState.WARNING
import mozilla.components.ui.icons.R as iconsR

/**
 * A menu item shown as a row in a vertical list.
 *
 * @param title The title of the menu item.
 * @param modifier The modifier to apply to the menu item.
 * @param index The optional index of this item in the list.
 * @param onClickEvent An optional [MenuEvent] to dispatch when the menu item is clicked.
 * @param onInteraction The callback to invoke when the menu item is interacted with.
 * @param contentDescription An optional content description for the menu item.
 * @param onShownEvent An optional [MenuEvent] to dispatch when the menu item is shown.
 * @param role The [Role] of the menu item.
 * @param summary An optional summary of the menu item.
 * @param icon An optional icon of the menu item.
 * @param showNewIndicator Whether to show a new indicator.
 * @param badge An optional badge to show. For a [Role.Switch] this is expected to show the current state, which screen
 *   readers will announce whenever it changes.
 * @param actionButton An optional action button to show.
 * @param trailingIcon An optional icon to show at the end of the menu item, as a hint of what clicking it does.
 * @param state The state of this menu item.
 */
@Composable
internal fun MenuListItem(
    title: MenuItemTitle,
    modifier: Modifier = Modifier,
    index: Int? = null,
    onClickEvent: MenuEvent?,
    onInteraction: (MenuEvent) -> Unit,
    contentDescription: Text?,
    onShownEvent: MenuEvent? = null,
    role: Role = Button,
    summary: MenuItemSummary? = null,
    icon: MenuItemIcon? = null,
    showNewIndicator: Boolean = false,
    badge: MenuItemBadge? = null,
    actionButton: MenuItemActionButton? = null,
    @DrawableRes trailingIcon: Int? = null,
    state: MenuItemState = DEFAULT,
) {
    LaunchedEffect(onShownEvent) {
        onShownEvent?.let {
            onInteraction(onShownEvent)
        }
    }

    Row(
        modifier = modifier.background(MaterialTheme.colorScheme.surfaceBright).height(IntrinsicSize.Min),
        verticalAlignment = CenterVertically,
    ) {
        val contentDescriptionValue = contentDescription?.value
        val stateDescriptionValue = badge?.text?.value?.takeIf { role == Role.Switch }
        Row(
            modifier =
                Modifier.weight(1f)
                    .defaultMinSize(
                        minHeight =
                            when (summary) {
                                null -> dimensionResource(R.dimen.mozac_menu_item_min_height)
                                else -> dimensionResource(R.dimen.mozac_menu_item_with_summary_min_height)
                            }
                    )
                    .fillMaxHeight()
                    .semantics(mergeDescendants = true) {
                        index?.let {
                            this.collectionItemInfo =
                                CollectionItemInfo(
                                    rowIndex = it,
                                    rowSpan = 1,
                                    columnIndex = 0,
                                    columnSpan = 1,
                                )
                        }
                        contentDescriptionValue?.let { this.contentDescription = it }
                        stateDescriptionValue?.let { this.stateDescription = it }
                    }
                    .menuItemClickableBehavior(
                        state = state,
                        role = role,
                        onClickEvent = onClickEvent,
                        onLongClickEvent = null,
                        onInteraction = onInteraction,
                    )
                    .padding(
                        horizontal = AcornTheme.layout.space.dynamic200,
                        vertical = AcornTheme.layout.space.static100,
                    ),
            verticalAlignment = CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static200),
        ) {
            MenuListItemIcon(icon, state)

            MenuListItemText(title, state, summary)

            MenuListItemNewIndicator(showNewIndicator, state)

            MenuListItemBadge(badge, hideFromAccessibility = stateDescriptionValue != null)

            if (trailingIcon != null) {
                Icon(
                    painter = painterResource(trailingIcon),
                    contentDescription = null,
                    modifier = Modifier.size(AcornTheme.layout.size.static300),
                    tint = state.secondaryContentColor,
                )
            }
        }

        MenuListItemActionButton(actionButton, onInteraction)
    }
}

@Composable
private fun MenuListItemActionButton(
    actionButton: MenuItemActionButton?,
    onClick: (MenuEvent) -> Unit,
) {
    if (actionButton != null) {
        if (actionButton.showDivider) {
            VerticalDivider(
                modifier = Modifier.padding(vertical = AcornTheme.layout.space.static100),
                color = MaterialTheme.colorScheme.outlineVariant,
            )
        }

        IconButton(
            onClick = { onClick(actionButton.onClickEvent) },
            contentDescription = actionButton.contentDescription.value,
            modifier = Modifier.minimumInteractiveComponentSize(),
        ) {
            Icon(
                painter = painterResource(actionButton.icon),
                contentDescription = null,
                modifier = Modifier.size(AcornTheme.layout.space.static300),
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun MenuListItemPreview() {
    AcornTheme {
        MenuListItem(
            title = MenuItemTitle(Text.String("Title")),
            contentDescription = Text.String(""),
            index = 0,
            modifier = Modifier.fillMaxWidth(),
            onClickEvent = object : MenuEvent {},
            onInteraction = {},
            summary = MenuItemSummary(Text.String("Summary")),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
            showNewIndicator = true,
            badge = MenuItemBadge(Text.String("Badge")),
            actionButton =
                MenuItemActionButton(
                    icon = iconsR.drawable.mozac_ic_chevron_right_24,
                    contentDescription = Text.String(""),
                    onClickEvent = object : MenuEvent {},
                    showDivider = true,
                ),
        )
    }
}

@PreviewLightDark
@Composable
private fun ActiveListItemPreview() {
    AcornTheme {
        MenuListItem(
            title = MenuItemTitle(Text.String("Title")),
            contentDescription = Text.String(""),
            index = 0,
            modifier = Modifier.fillMaxWidth(),
            onClickEvent = object : MenuEvent {},
            onInteraction = {},
            summary = MenuItemSummary(Text.String("Summary"), ACTIVE),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
            showNewIndicator = true,
            badge = MenuItemBadge(Text.String("Badge"), ACTIVE),
            actionButton =
                MenuItemActionButton(
                    icon = iconsR.drawable.mozac_ic_chevron_right_24,
                    contentDescription = Text.String(""),
                    onClickEvent = object : MenuEvent {},
                    showDivider = true,
                    state = ACTIVE,
                ),
            state = ACTIVE,
        )
    }
}

@PreviewLightDark
@Composable
private fun DisabledMenuListItemPreview() {
    AcornTheme {
        MenuListItem(
            title = MenuItemTitle(Text.String("Title")),
            contentDescription = Text.String(""),
            index = 0,
            modifier = Modifier.fillMaxWidth(),
            onClickEvent = object : MenuEvent {},
            onInteraction = {},
            summary = MenuItemSummary(Text.String("Summary"), DISABLED),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
            showNewIndicator = true,
            badge = MenuItemBadge(Text.String("Badge"), DISABLED),
            actionButton =
                MenuItemActionButton(
                    icon = iconsR.drawable.mozac_ic_chevron_right_24,
                    contentDescription = Text.String(""),
                    onClickEvent = object : MenuEvent {},
                    showDivider = true,
                    state = DISABLED,
                ),
            state = DISABLED,
        )
    }
}

@PreviewLightDark
@Composable
private fun WarningMenuListItemPreview() {
    AcornTheme {
        MenuListItem(
            title = MenuItemTitle(Text.String("Title")),
            contentDescription = Text.String(""),
            index = 0,
            modifier = Modifier.fillMaxWidth(),
            onClickEvent = object : MenuEvent {},
            onInteraction = {},
            summary = MenuItemSummary(Text.String("Summary"), WARNING),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
            showNewIndicator = true,
            badge = MenuItemBadge(Text.String("Badge"), WARNING),
            actionButton =
                MenuItemActionButton(
                    icon = iconsR.drawable.mozac_ic_chevron_right_24,
                    contentDescription = Text.String(""),
                    onClickEvent = object : MenuEvent {},
                    showDivider = true,
                    state = WARNING,
                ),
            state = WARNING,
        )
    }
}

@PreviewLightDark
@Composable
private fun TitleOnlyMenuListItemPreview() {
    AcornTheme {
        MenuListItem(
            title = MenuItemTitle(Text.String("Title")),
            contentDescription = Text.String(""),
            index = 0,
            onClickEvent = object : MenuEvent {},
            onInteraction = {},
            modifier = Modifier.fillMaxWidth(),
        )
    }
}
