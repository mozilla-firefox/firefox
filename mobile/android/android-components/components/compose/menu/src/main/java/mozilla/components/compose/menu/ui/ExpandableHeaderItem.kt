/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Alignment.Companion.CenterVertically
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.res.dimensionResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.Role.Companion.Button
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.R as composeBaseR
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.text.value
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemState.DEFAULT
import mozilla.components.ui.icons.R as iconsR

private const val ANIMATION_DURATION_MS = 300

/**
 * A menu item that can be expanded to show others or collapsed to hide them.
 *
 * @param title The title of the menu item.
 * @param subMenuItems List of [StandardMenuItem] to show when this item is expanded.
 * @param modifier The modifier to apply to the menu item.
 * @param hideOnExpand Whether to automatically hide the menu item when it is expanded so that only its [subMenuItems]
 *   will remain displayed. This also means that once expanded the list of [subMenuItems] cannot be collapsed again.
 * @param onClickEvent [MenuEvent] to dispatch when the menu item is clicked.
 * @param onInteraction The callback to invoke when the menu item is interacted with.
 * @param role The [Role] of the menu item.
 * @param contentDescription Optional custom content description for the menu item.
 * @param summary An optional summary of the menu item.
 * @param icon An optional icon of the menu item.
 * @param showNewIndicator Whether to show a new indicator.
 * @param badge An optional badge to show.
 * @param actionButtonText An optional text to show in the expanded button indicator.
 * @param state The state of this menu item.
 */
@Composable
fun ExpandableHeaderItem(
    title: MenuItemTitle,
    subMenuItems: List<StandardMenuItem>,
    modifier: Modifier = Modifier,
    hideOnExpand: Boolean = false,
    onClickEvent: MenuEvent? = null,
    onInteraction: (MenuEvent) -> Unit = {},
    role: Role = Button,
    contentDescription: Text? = null,
    summary: MenuItemSummary? = null,
    icon: MenuItemIcon? = null,
    showNewIndicator: Boolean = false,
    badge: MenuItemBadge? = null,
    actionButtonText: Text? = null,
    state: MenuItemState = DEFAULT,
) {
    var isExpanded by rememberSaveable { mutableStateOf(false) }
    var headerHeight by remember { mutableIntStateOf(0) }

    if (!hideOnExpand || !isExpanded) {
        ExpandableMenuItemHeader(
            title = title,
            isExpanded = isExpanded,
            onClick = {
                isExpanded = !isExpanded
                onClickEvent?.let { onInteraction(it) }
            },
            modifier = modifier.onSizeChanged { headerHeight = it.height },
            role = role,
            contentDescription = contentDescription,
            summary = summary,
            icon = icon,
            showNewIndicator = showNewIndicator,
            badge = badge,
            actionButtonText = actionButtonText,
            state = state,
        )
    }

    // Sub-items replacing their header start from its height, so that the menu only grows when expanding.
    MenuItemAnimation(isExpanded = isExpanded, initialHeight = if (hideOnExpand) headerHeight else 0) {
        subMenuItems.forEach {
            MenuListItem(
                title = it.title,
                contentDescription = it.contentDescription,
                modifier = Modifier.clip(MaterialTheme.shapes.extraSmall),
                role = it.role,
                summary = it.summary,
                icon = it.icon,
                showNewIndicator = it.showNewIndicator,
                badge = it.badge,
                actionButton = it.actionButton,
                trailingIcon = it.trailingIcon,
                state = it.state,
                onClickEvent = it.onClickEvent,
                onShownEvent = it.onShownEvent,
                onInteraction = onInteraction,
            )
        }
    }
}

/**
 * The header of a menu item that can be expanded to show others or collapsed to hide them.
 *
 * @param title The title of the menu item.
 * @param isExpanded Whether the menu item is expanded.
 * @param onClick Called when the header is clicked.
 * @param modifier The modifier to apply to the header.
 * @param role The [Role] of the menu item.
 * @param contentDescription Optional custom content description for the menu item.
 * @param summary An optional summary of the menu item.
 * @param icon An optional icon of the menu item.
 * @param showNewIndicator Whether to show a new indicator.
 * @param badge An optional badge to show.
 * @param actionButtonText An optional text to show in the expanded button indicator.
 * @param state The state of this menu item.
 */
@Composable
internal fun ExpandableMenuItemHeader(
    title: MenuItemTitle,
    isExpanded: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    role: Role = Button,
    contentDescription: Text? = null,
    summary: MenuItemSummary? = null,
    icon: MenuItemIcon? = null,
    showNewIndicator: Boolean = false,
    badge: MenuItemBadge? = null,
    actionButtonText: Text? = null,
    state: MenuItemState = DEFAULT,
) {
    val contentDescriptionValue = contentDescription?.value
    val expandedDescription = stringResource(composeBaseR.string.mozac_compose_base_a11y_state_label_expanded)
    val collapsedDescription = stringResource(composeBaseR.string.mozac_compose_base_a11y_state_label_collapsed)

    Row(
        modifier =
            modifier
                .background(MaterialTheme.colorScheme.surfaceBright)
                .defaultMinSize(
                    minHeight =
                        when (summary) {
                            null -> dimensionResource(R.dimen.mozac_menu_item_min_height)
                            else -> dimensionResource(R.dimen.mozac_menu_item_with_summary_min_height)
                        }
                )
                .semantics(mergeDescendants = true) {
                    contentDescriptionValue?.let { this.contentDescription = it }
                    this.stateDescription = if (isExpanded) expandedDescription else collapsedDescription
                    this.role = role
                }
                .clickable(
                    enabled = state != MenuItemState.DISABLED,
                    onClickLabel =
                        if (isExpanded) {
                            stringResource(composeBaseR.string.mozac_compose_base_a11y_action_label_collapse)
                        } else {
                            stringResource(composeBaseR.string.mozac_compose_base_a11y_action_label_expand)
                        },
                    onClick = onClick,
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

        MenuListItemBadge(badge)

        ChevronBadge(actionButtonText?.value, isExpanded)
    }
}

/**
 * Animates showing or hiding sub-items.
 *
 * @param isExpanded Whether the sub-items should be shown.
 * @param initialHeight Height, in pixels, from which the sub-items start expanding.
 * @param content The sub-items to show.
 */
@Composable
private fun MenuItemAnimation(
    isExpanded: Boolean,
    initialHeight: Int,
    content: @Composable ColumnScope.() -> Unit,
) {
    AnimatedVisibility(
        visible = isExpanded,
        enter =
            expandVertically(
                expandFrom = Alignment.Top,
                initialHeight = { fullHeight -> initialHeight.coerceAtMost(fullHeight) },
                animationSpec =
                    tween(
                        durationMillis = ANIMATION_DURATION_MS,
                        easing = LinearEasing,
                    ),
            ) +
                fadeIn(
                    animationSpec =
                        tween(
                            durationMillis = ANIMATION_DURATION_MS,
                            easing = LinearEasing,
                        )
                ),
        exit =
            shrinkVertically(
                shrinkTowards = Alignment.Top,
                animationSpec =
                    tween(
                        durationMillis = ANIMATION_DURATION_MS,
                        easing = LinearEasing,
                    ),
            ) +
                fadeOut(
                    animationSpec =
                        tween(
                            durationMillis = ANIMATION_DURATION_MS,
                            easing = LinearEasing,
                        )
                ),
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static25)) {
            content()
        }
    }
}

@Composable
private fun ChevronBadge(
    text: String?,
    isExpanded: Boolean,
) {
    val extraSmallPadding = dimensionResource(R.dimen.mozac_menu_extra_small_padding)
    Row(
        modifier =
            Modifier.background(
                    color = MaterialTheme.colorScheme.surfaceContainerHigh,
                    shape = MaterialTheme.shapes.large,
                )
                .padding(
                    start = if (text != null) AcornTheme.layout.size.static100 else extraSmallPadding,
                    top = extraSmallPadding,
                    bottom = extraSmallPadding,
                    end = extraSmallPadding,
                ),
        horizontalArrangement = Arrangement.spacedBy(AcornTheme.layout.size.static100),
        verticalAlignment = CenterVertically,
    ) {
        if (text != null) {
            Text(
                text = text,
                color = MaterialTheme.colorScheme.onSurface,
                overflow = TextOverflow.Ellipsis,
                style = AcornTheme.typography.caption,
                maxLines = 1,
            )
        }

        Icon(
            painter =
                if (isExpanded) {
                    painterResource(id = iconsR.drawable.mozac_ic_chevron_up_20)
                } else {
                    painterResource(id = iconsR.drawable.mozac_ic_chevron_down_20)
                },
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@PreviewLightDark
@Composable
private fun ExpandableHeaderItemPreview() = AcornTheme {
    Surface {
        ExpandableHeaderItem(
            title = MenuItemTitle(Text.String("Title")),
            contentDescription = Text.String(""),
            subMenuItems = emptyList(),
            summary = MenuItemSummary(Text.String("Summary")),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_extension_24),
            actionButtonText = Text.String("2"),
        )
    }
}
