/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CollectionInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.collectionInfo
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.isTraversalGroup
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.Hyphens
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import mozilla.components.compose.base.badge.BADGE_SIZE_SMALL
import mozilla.components.compose.base.badge.BadgedIcon
import mozilla.components.compose.base.badge.StatusBadge
import mozilla.components.compose.base.modifier.debouncedClickable
import mozilla.components.compose.base.text.value
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.information
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemState.DISABLED

/**
 * The badge of a menu list item.
 *
 * @param badge The [MenuItemBadge] to show, if any.
 * @param hideFromAccessibility Whether screen readers should skip the badge, for when its text is already announced in
 *   another way.
 */
@Composable
internal fun MenuListItemBadge(
    badge: MenuItemBadge?,
    hideFromAccessibility: Boolean = false,
) {
    if (badge != null) {
        Text(
            text = badge.text.value,
            color = badge.state.contentColor,
            style = AcornTheme.typography.headline8,
            overflow = TextOverflow.Ellipsis,
            maxLines = 1,
            modifier =
                Modifier.clip(shape = MaterialTheme.shapes.extraLarge)
                    .background(badge.state.badgeContainerColor)
                    .padding(
                        horizontal = AcornTheme.layout.space.static200,
                        vertical = AcornTheme.layout.space.static100,
                    )
                    .then(if (hideFromAccessibility) Modifier.clearAndSetSemantics {} else Modifier),
        )
    }
}

/**
 * The indicator shown for a menu list item which is new.
 *
 * @param showNewIndicator Whether to show the indicator.
 * @param state The [MenuItemState] of the menu item, which determines the indicator color.
 */
@Composable
internal fun MenuListItemNewIndicator(
    showNewIndicator: Boolean,
    state: MenuItemState,
) {
    if (showNewIndicator) {
        StatusBadge(
            containerColor = state.newIndicatorContainerColor,
            status = stringResource(R.string.mozac_menu_item_new_indicator_label),
        )
    }
}

/**
 * The title and the optional summary of a menu list item, filling the remaining width of the row.
 *
 * @param title The title of the menu item.
 * @param state The [MenuItemState] of the menu item, which determines the title color.
 * @param summary An optional summary to show below the title.
 */
@Composable
internal fun RowScope.MenuListItemText(
    title: MenuItemTitle,
    state: MenuItemState,
    summary: MenuItemSummary?,
) {
    Column(modifier = Modifier.weight(1f)) {
        Text(
            text = title.text.value,
            style = AcornTheme.typography.subtitle1.copy(hyphens = Hyphens.Auto),
            modifier = Modifier.fillMaxWidth(),
            textAlign = TextAlign.Start,
            overflow = TextOverflow.Ellipsis,
            maxLines = title.maxLines,
            softWrap = true,
            color = state.contentColor,
        )

        if (summary != null) {
            Text(
                text = summary.text.value,
                style = AcornTheme.typography.body2,
                overflow = TextOverflow.Ellipsis,
                maxLines = summary.maxLines,
                color = summary.state.secondaryContentColor,
            )
        }
    }
}

/**
 * The leading icon of a menu list item. Space for the icon is reserved even when there is none, so that the texts of
 * all items in a list line up.
 *
 * @param icon The [MenuItemIcon] to show, if any.
 * @param state The [MenuItemState] of the menu item, which determines the icon tint.
 */
@Composable
internal fun MenuListItemIcon(
    icon: MenuItemIcon?,
    state: MenuItemState,
) {
    if (icon != null) {
        BadgedIcon(
            painter = icon.painter,
            isHighlighted = icon.isHighlighted,
            modifier = Modifier.size(AcornTheme.layout.size.static300),
            size = BADGE_SIZE_SMALL,
            contentDescription = null,
            containerColor = MaterialTheme.colorScheme.information,
            tint =
                if (icon is MenuItemIconRes) {
                    state.secondaryContentColor
                } else Color.Unspecified,
        )
    } else {
        Spacer(Modifier.size(AcornTheme.layout.size.static300))
    }
}

/**
 * Make this clickable, and long-clickable if there is a [onLongClickEvent], as long as the menu item is not disabled.
 * Without events to dispatch, or for a disabled item, this is left as it is.
 *
 * @param state The [MenuItemState] of the menu item.
 * @param role The [Role] of the menu item.
 * @param onClickEvent The [MenuEvent] to dispatch when the menu item is clicked.
 * @param onLongClickEvent The [MenuEvent] to dispatch when the menu item is long-clicked.
 * @param onInteraction The callback to invoke when the menu item is interacted with.
 */
@Composable
internal fun Modifier.menuItemClickableBehavior(
    state: MenuItemState,
    role: Role,
    onClickEvent: MenuEvent?,
    onLongClickEvent: MenuEvent?,
    onInteraction: (MenuEvent) -> Unit,
): Modifier {
    val haptic = LocalHapticFeedback.current
    return if (onLongClickEvent != null) {
        this.combinedClickable(
            enabled = state != DISABLED,
            onClick = { onClickEvent?.let { onInteraction(it) } },
            onLongClick = {
                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                onInteraction(onLongClickEvent)
            },
            role = role,
        )
    } else if (onClickEvent != null && state != DISABLED) {
        this.debouncedClickable(
            onClick = { onInteraction(onClickEvent) },
            role = role,
        )
    } else if (state == DISABLED) {
        this.semantics { disabled() }
    } else {
        this
    }
}

/**
 * The [Shape] of an item in a vertical list: fully rounded corners on the outer sides of the first and last items, and
 * slightly rounded corners where items meet.
 *
 * @param itemIndex The index of the item in the list.
 * @param totalItems The number of items in the list.
 */
@Composable
internal fun getListItemShape(
    itemIndex: Int,
    totalItems: Int,
): Shape {
    if (totalItems == 1) {
        return MaterialTheme.shapes.extraLarge
    }
    return when (itemIndex) {
        0 ->
            MaterialTheme.shapes.extraLarge.copy(
                bottomStart = MaterialTheme.shapes.extraSmall.bottomStart,
                bottomEnd = MaterialTheme.shapes.extraSmall.bottomEnd,
            )

        totalItems - 1 ->
            MaterialTheme.shapes.extraLarge.copy(
                topStart = MaterialTheme.shapes.extraSmall.topStart,
                topEnd = MaterialTheme.shapes.extraSmall.topEnd,
            )

        else -> MaterialTheme.shapes.extraSmall
    }
}

/** Semantics of a vertical list of [itemCount] items. */
internal fun Modifier.menuListGroupSemantics(itemCount: Int): Modifier =
    this.semantics {
        isTraversalGroup = true
        collectionInfo = CollectionInfo(rowCount = itemCount, columnCount = 1)
    }
