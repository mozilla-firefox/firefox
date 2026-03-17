/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.compose

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CollectionItemInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.collectionItemInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.surfaceDimVariant
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuDialogTestTag.WEB_EXTENSION_ITEM
import org.mozilla.fenix.compose.list.IconListItem
import org.mozilla.fenix.compose.list.TextListItem
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.DURATION_MS_MAIN_MENU_ITEM
import mozilla.components.ui.icons.R as iconsR

private val MENU_ITEM_HEIGHT_WITHOUT_DESC = 52.dp

private val MENU_ITEM_HEIGHT_WITH_DESC = 56.dp

private val BADGE_ROUNDED_CORNER = 100.dp

private val ROUNDED_CORNER_SHAPE = RoundedCornerShape(4.dp)

/**
 * An [IconListItem] wrapper for menu items in a [MenuGroup] with an optional icon at the end.
 *
 * @param label The label in the menu item.
 * @param beforeIconPainter [Painter] used to display an [Icon] before the list item.
 * @param modifier [Modifier] to be applied to the layout.
 * @param labelModifier [Modifier] to be applied to the label.
 * @param beforeIconDescription Content description of the icon.
 * @param isBeforeIconHighlighted Whether or not the menu item should be highlighted with a notification icon.
 * @param description An optional description text below the label.
 * @param maxDescriptionLines An optional maximum number of lines for the description text to span.
 * @param stateDescription Extra content description about state to be added after the label
 * and description.
 * @param state The state of the menu item to display.
 * @param descriptionState The state of menu item description to display.
 * @param onClick Invoked when the user clicks on the item.
 * @param showDivider Whether or not to display a vertical divider line before the [IconButton]
 * at the end.
 * @param afterIconPainter [Painter] used to display an [IconButton] after the list item.
 * @param afterIconDescription Content description of the icon.
 * @param collectionItemInfo [CollectionItemInfo] to be applied to the MenuItem.
 * @param onAfterIconClick Invoked when the user clicks on the icon. An [IconButton] will be
 * displayed if this is provided. Otherwise, an [Icon] will be displayed.
 * @param afterContent Optional Composable for adding UI to the end of the list item.
 */
@Composable
internal fun MenuItem(
    label: String,
    beforeIconPainter: Painter,
    modifier: Modifier = Modifier,
    labelModifier: Modifier = Modifier,
    beforeIconDescription: String? = null,
    isBeforeIconHighlighted: Boolean = false,
    description: String? = null,
    maxDescriptionLines: Int = 2,
    stateDescription: String = "",
    state: MenuItemState = MenuItemState.ENABLED,
    descriptionState: MenuItemState = MenuItemState.ENABLED,
    onClick: (() -> Unit)? = null,
    showDivider: Boolean = false,
    afterIconPainter: Painter? = null,
    afterIconDescription: String? = null,
    collectionItemInfo: CollectionItemInfo? = null,
    onAfterIconClick: (() -> Unit)? = null,
    afterContent: (@Composable () -> Unit)? = null,
) {
    val labelTextColor = getLabelTextColor(state = state)
    val descriptionTextColor = getDescriptionTextColor(state = descriptionState)
    val iconTint = getIconTint(state = state)
    val enabled = state != MenuItemState.DISABLED

    var contentDescription = label

    if (description != null) {
        contentDescription = "$contentDescription $description"
    }

    if (stateDescription.isNotEmpty()) {
        contentDescription = "$contentDescription $stateDescription"
    }

    IconListItem(
        label = label,
        modifier = modifier
            .clearAndSetSemantics {
                if (onClick != null || state == MenuItemState.DISABLED) {
                    role = Role.Button
                }
                this.contentDescription = contentDescription
                if (collectionItemInfo != null) {
                    this.collectionItemInfo = collectionItemInfo
                }
                if (!enabled) {
                    disabled()
                }
            }
            .wrapContentSize()
            .clip(shape = ROUNDED_CORNER_SHAPE)
            .background(
                color = MaterialTheme.colorScheme.surfaceDimVariant,
            ),
        labelModifier = labelModifier,
        colors = ListItemDefaults.colors(
            headlineColor = labelTextColor,
            supportingColor = descriptionTextColor,
            ),
        maxLabelLines = 2,
        description = description,
        maxDescriptionLines = maxDescriptionLines,
        enabled = enabled,
        minHeight = if (description != null) {
            MENU_ITEM_HEIGHT_WITH_DESC
        } else {
            MENU_ITEM_HEIGHT_WITHOUT_DESC
        },
        onClick = onClick,
        beforeIconPainter = beforeIconPainter,
        beforeIconDescription = beforeIconDescription,
        beforeIconTint = iconTint,
        isBeforeIconHighlighted = isBeforeIconHighlighted,
        showDivider = showDivider,
        afterIconPainter = afterIconPainter,
        afterIconDescription = afterIconDescription,
        afterIconTint = iconTint,
        onAfterIconClick = onAfterIconClick,
        afterListAction = afterContent,
    )
}

/**
 * An [IconListItem] wrapper for menu items in a [MenuGroup] with an optional icon at the end.
 *
 * @param label The label in the menu item.
 * @param modifier [Modifier] to be applied to the layout.
 * @param description An optional description text below the label.
 * @param iconPainter [Painter] used to display an [Icon] after the list item.
 * @param onClick Invoked when the user clicks on the item.
 */
@Composable
internal fun MenuTextItem(
    label: String,
    modifier: Modifier = Modifier,
    description: String? = null,
    iconPainter: Painter? = null,
    onClick: (() -> Unit)? = null,
) {
    TextListItem(
        label = label,
        maxLabelLines = 2,
        description = description,
        minHeight = if (description != null) {
            MENU_ITEM_HEIGHT_WITH_DESC
        } else {
            MENU_ITEM_HEIGHT_WITHOUT_DESC
        },
        modifier = modifier
            .clip(shape = ROUNDED_CORNER_SHAPE)
            .background(
                color = MaterialTheme.colorScheme.surfaceDimVariant,
            ),
        iconPainter = iconPainter,
        onClick = onClick,
    )
}

/**
 * A Web extension menu item used to display an image with a title and a badge.
 *
 * @param label The label in the list item.
 * @param iconPainter [Painter] used to display an [Icon] before the list item.
 * @param iconTint Tint color to be applied on the [Icon].
 * @param enabled Controls the enabled state of the list item. When `false`, the list item will not
 * be clickable.
 * @param badgeText WebExtension badge text.
 * @param index The index of the item within the column.
 * @param onClick Called when the user clicks on the item.
 * @param onSettingsClick Called when the user clicks on the settings icon.
 */
@Composable
internal fun WebExtensionMenuItem(
    label: String,
    iconPainter: Painter,
    iconTint: Color = Color.Unspecified,
    enabled: Boolean?,
    badgeText: String?,
    index: Int = 0,
    onClick: (() -> Unit)? = null,
    onSettingsClick: (() -> Unit)? = null,
) {
    IconListItem(
        label = label,
        enabled = enabled == true,
        beforeIconTint = iconTint,
        beforeIconPainter = iconPainter,
        onClick = onClick,
        modifier = Modifier
            .testTag(WEB_EXTENSION_ITEM)
            .clearAndSetSemantics {
                onClick?.let { role = Role.Button }
                contentDescription = label
                collectionItemInfo =
                    CollectionItemInfo(
                        rowIndex = index,
                        rowSpan = 1,
                        columnIndex = 0,
                        columnSpan = 1,
                    )
                testTagsAsResourceId = true
            }
            .wrapContentSize()
            .clip(shape = ROUNDED_CORNER_SHAPE)
            .background(
                color = MaterialTheme.colorScheme.surfaceDimVariant,
            ),
        afterListAction = {
            Row(
                modifier = Modifier.padding(start = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(16.dp, Alignment.End),
            ) {
                if (!badgeText.isNullOrEmpty()) {
                    Badge(
                        badgeText = badgeText,
                    )
                }

                if (onSettingsClick != null) {
                    VerticalDivider()

                    IconButton(
                        modifier = Modifier.size(24.dp),
                        onClick = onSettingsClick,
                    ) {
                        Icon(
                            painter = painterResource(iconsR.drawable.mozac_ic_settings_24),
                            tint = MaterialTheme.colorScheme.onSurface,
                            contentDescription = null,
                        )
                    }
                }
            }
        },
    )
}

@Composable
internal fun MenuBadgeItem(
    label: String,
    description: String,
    badgeText: String,
    checked: Boolean,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    val state: MenuItemState = if (checked) {
        MenuItemState.ACTIVE
    } else {
        MenuItemState.DISABLED
    }

    Row(
        modifier = modifier
            .thenConditional(
                Modifier.clickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = LocalIndication.current,
                ) { onClick() },
                ) { enabled }
            .thenConditional(
                Modifier.semantics { disabled() },
            ) { !enabled }
            .semantics { disabled() }
            .clip(shape = ROUNDED_CORNER_SHAPE)
            .background(
                color = MaterialTheme.colorScheme.surfaceDimVariant,
            )
            .padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(
            modifier = Modifier.weight(1f),
        ) {
            Text(
                text = label,
                modifier = Modifier
                    .defaultMinSize(minHeight = 24.dp)
                    .wrapContentHeight(),
                color = getLabelTextColor(state),
                style = FirefoxTheme.typography.body1,
            )

            Text(
                text = description,
                modifier = Modifier
                    .defaultMinSize(minHeight = 20.dp)
                    .wrapContentHeight(),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = FirefoxTheme.typography.caption,
            )
        }

        Badge(
            badgeText = badgeText,
            state = state,
        )
    }
}

@Composable
internal fun Badge(
    badgeText: String,
    state: MenuItemState = MenuItemState.ENABLED,
) {
    Column(
        modifier = Modifier
            .clip(shape = RoundedCornerShape(BADGE_ROUNDED_CORNER))
            .background(color = getBadgeColor(state))
            .padding(horizontal = 16.dp, vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(
            text = badgeText,
            color = getLabelTextColor(state),
            overflow = TextOverflow.Ellipsis,
            style = FirefoxTheme.typography.headline8,
            maxLines = 1,
        )
    }
}

@Composable
internal fun ExpandableMenuItemAnimation(
    isExpanded: Boolean,
    content: @Composable () -> Unit,
) {
    AnimatedVisibility(
        visible = isExpanded,
        enter = expandVertically(
            expandFrom = Alignment.Top,
            animationSpec = tween(
                durationMillis = DURATION_MS_MAIN_MENU_ITEM,
                easing = LinearEasing,
            ),
        ) + fadeIn(
            animationSpec = tween(
                durationMillis = DURATION_MS_MAIN_MENU_ITEM,
                easing = LinearEasing,
            ),
        ),
        exit = shrinkVertically(
            shrinkTowards = Alignment.Top,
            animationSpec = tween(
                durationMillis = DURATION_MS_MAIN_MENU_ITEM,
                easing = LinearEasing,
            ),
        ) + fadeOut(
            animationSpec = tween(
                durationMillis = DURATION_MS_MAIN_MENU_ITEM,
                easing = LinearEasing,
            ),
        ),
    ) {
        Column {
            content()
        }
    }
}

/**
 * Enum containing all the supported state for the menu item.
 */
enum class MenuItemState {
    /**
     * The menu item is enabled.
     */
    ENABLED,

    /**
     * The menu item is disabled and is not clickable.
     */
    DISABLED,

    /**
     * The menu item is highlighted to indicate the feature behind the menu item is active.
     */
    ACTIVE,

    /**
     * The menu item is highlighted to indicate the feature behind the menu item is destructive.
     */
    WARNING,

    /**
     * The menu item is highlighted to indicate the feature behind the menu item is critical.
     */
    CRITICAL,
}

@Composable
private fun getLabelTextColor(state: MenuItemState): Color {
    return when (state) {
        MenuItemState.ACTIVE -> AcornTheme.colors.accent
        MenuItemState.WARNING -> MaterialTheme.colorScheme.error
        else -> MaterialTheme.colorScheme.onSurface
    }
}

@Composable
private fun getDescriptionTextColor(state: MenuItemState): Color {
    return when (state) {
        MenuItemState.ACTIVE -> AcornTheme.colors.accent
        MenuItemState.WARNING -> MaterialTheme.colorScheme.error
        MenuItemState.DISABLED -> AcornTheme.colors.textNormal.copy(alpha = 0.38f)
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }
}

@Composable
private fun getIconTint(state: MenuItemState): Color {
    return when (state) {
        MenuItemState.ACTIVE -> AcornTheme.colors.accent
        MenuItemState.WARNING -> MaterialTheme.colorScheme.error
        MenuItemState.CRITICAL -> Color.Unspecified
        else -> AcornTheme.colors.iconNormal
    }
}

@Composable
private fun getBadgeColor(state: MenuItemState): Color {
    return when (state) {
        MenuItemState.ACTIVE -> AcornTheme.colors.controlsButtonPrimaryNormal
        else -> AcornTheme.colors.controlsButtonSecondaryNormal
    }
}

@PreviewLightDark
@Composable
private fun WebExtensionMenuItemPreview() {
    FirefoxTheme {
        Column(
            modifier = Modifier
                .background(color = MaterialTheme.colorScheme.surface),
        ) {
            WebExtensionMenuItem(
                label = "label",
                iconPainter = painterResource(iconsR.drawable.mozac_ic_extension_fill_24),
                iconTint = MaterialTheme.colorScheme.onSurface,
                enabled = true,
                badgeText = "17",
                onClick = {},
                onSettingsClick = {},
            )
            // Web extensions may have multi-colored assets with no tint.
            WebExtensionMenuItem(
                label = "colorful icon",
                iconPainter = painterResource(iconsR.drawable.mozac_ic_shield_slash_critical_24),
                iconTint = Color.Unspecified,
                enabled = true,
                badgeText = "17",
                onClick = {},
                onSettingsClick = {},
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun MenuItemPreview() {
    FirefoxTheme {
        Column(
            modifier = Modifier
                .background(color = MaterialTheme.colorScheme.surface)
                .padding(16.dp),
        ) {
            MenuGroup {
                for (state in MenuItemState.entries) {
                    MenuItem(
                        label = stringResource(id = R.string.browser_menu_translations),
                        beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_translate_24),
                        state = state,
                        onClick = {},
                    )
                }

                for (state in MenuItemState.entries) {
                    MenuItem(
                        label = stringResource(id = R.string.browser_menu_extensions),
                        beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_extension_24),
                        state = state,
                        onClick = {},
                        afterIconPainter = painterResource(id = iconsR.drawable.mozac_ic_chevron_right_24),
                    )
                }

                for (state in MenuItemState.entries) {
                    MenuItem(
                        label = stringResource(id = R.string.browser_menu_extensions),
                        beforeIconPainter = painterResource(id = iconsR.drawable.mozac_ic_extension_24),
                        state = state,
                        onClick = {},
                        showDivider = true,
                        afterIconPainter = painterResource(id = iconsR.drawable.mozac_ic_plus_24),
                        onAfterIconClick = {},
                    )
                }
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun MenuBadgeItemPreview() {
    FirefoxTheme {
        Column(
            modifier = Modifier
                .background(color = MaterialTheme.colorScheme.surface)
                .padding(all = FirefoxTheme.layout.space.static200),
        ) {
            MenuBadgeItem(
                label = stringResource(id = R.string.protection_panel_etp_toggle_label),
                description = stringResource(id = R.string.protection_panel_etp_toggle_enabled_description_2),
                badgeText = stringResource(id = R.string.protection_panel_etp_toggle_on),
                checked = true,
                onClick = {},
            )
        }
    }
}
