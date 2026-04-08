/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.compose.list

import android.content.res.Configuration
import android.widget.Toast
import androidx.annotation.DrawableRes
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItemColors
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.VerticalDivider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.Hyphens
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.badge.BADGE_SIZE_SMALL
import mozilla.components.compose.base.badge.BadgedIcon
import mozilla.components.compose.base.modifier.thenConditional
import mozilla.components.compose.base.theme.information
import org.mozilla.fenix.compose.Favicon
import org.mozilla.fenix.compose.button.RadioButton
import org.mozilla.fenix.theme.FirefoxTheme
import java.util.Locale
import mozilla.components.ui.icons.R as iconsR

private val LIST_ITEM_HEIGHT = 56.dp
private val ICON_SIZE = 24.dp

private const val TOAST_LENGTH = Toast.LENGTH_SHORT

private val EmptyListItemSlot: @Composable RowScope.() -> Unit = {}

/**
 * List item used to display a label with an optional description text and an optional
 * [IconButton] or [Icon] at the end.
 *
 * @param label The label in the list item.
 * @param modifier [Modifier] to be applied to the layout.
 * @param maxLabelLines An optional maximum number of lines for the label text to span.
 * @param overline An optional text shown above the label.
 * @param description An optional description text below the label.
 * @param maxDescriptionLines An optional maximum number of lines for the description text to span.
 * @param enabled Controls the enabled state of the list item. When `false`, the list item will not
 * be clickable.
 * @param minHeight An optional minimum height for the list item.
 * @param onClick Called when the user clicks on the item.
 * @param onLongClick Called when the user long clicks on the item.
 * @param iconPainter [Painter] used to display an icon after the list item.
 * @param iconDescription Content description of the icon.
 * @param iconTint Tint applied to [iconPainter].
 * @param onIconClick Called when the user clicks on the icon. An [IconButton] will be
 * displayed if this is provided. Otherwise, an [Icon] will be displayed.
 */
@Composable
fun TextListItem(
    label: String,
    modifier: Modifier = Modifier,
    maxLabelLines: Int = 1,
    overline: String? = null,
    description: String? = null,
    maxDescriptionLines: Int = 1,
    enabled: Boolean = true,
    minHeight: Dp = LIST_ITEM_HEIGHT,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
    iconPainter: Painter? = null,
    iconDescription: String? = null,
    iconTint: Color = ListItemDefaults.colors().leadingIconColor,
    onIconClick: (() -> Unit)? = null,
) {
    ListItem(
        label = label,
        maxLabelLines = maxLabelLines,
        modifier = modifier,
        overline = overline,
        description = description,
        maxDescriptionLines = maxDescriptionLines,
        enabled = enabled,
        minHeight = minHeight,
        onClick = onClick,
        onLongClick = onLongClick,
    ) {
        if (iconPainter == null) {
            return@ListItem
        }

        if (onIconClick == null) {
            Icon(
                painter = iconPainter,
                contentDescription = iconDescription,
                tint = iconTint,
            )
        } else {
            IconButton(
                onClick = onIconClick,
                modifier = Modifier
                    .size(ICON_SIZE)
                    .clearAndSetSemantics {},
            ) {
                Icon(
                    painter = iconPainter,
                    contentDescription = iconDescription,
                    tint = iconTint,
                )
            }
        }
    }
}

/**
 * List item used to display a label and a [Favicon] with an optional description text and
 * an optional [IconButton] at the end.
 *
 * @param label The label in the list item.
 * @param url Website [url] for which the favicon will be shown.
 * @param modifier [Modifier] to be applied to the layout.
 * @param faviconShape The shape used to clip the favicon. Defaults to a slightly rounded rectangle.
 * @param labelModifier [Modifier] to be applied to the label.
 * @param overline An optional text shown above the label.
 * @param description An optional description text below the label.
 * @param maxDescriptionLines An optional maximum number of lines for the description text to span.
 * @param faviconPainter Optional painter to use when fetching a new favicon is unnecessary.
 * @param onClick Called when the user clicks on the item.
 * @param onLongClick Called when the user long clicks on the item.
 * @param showDivider Whether or not to display a vertical divider line before the [IconButton]
 * at the end.
 * @param iconPainter [Painter] used to display an [IconButton] after the list item.
 * @param iconButtonModifier [Modifier] to be applied to the icon button.
 * @param iconDescription Content description of the icon.
 * @param onIconClick Called when the user clicks on the icon.
 */
@Composable
fun FaviconListItem(
    label: String,
    url: String,
    modifier: Modifier = Modifier,
    faviconShape: Shape = RoundedCornerShape(2.dp),
    labelModifier: Modifier = Modifier,
    overline: String? = null,
    description: String? = null,
    maxDescriptionLines: Int = 1,
    faviconPainter: Painter? = null,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
    showDivider: Boolean = false,
    iconPainter: Painter? = null,
    iconButtonModifier: Modifier = Modifier,
    iconDescription: String? = null,
    onIconClick: (() -> Unit)? = null,
) {
    ListItem(
        label = label,
        modifier = modifier,
        labelModifier = labelModifier,
        overline = overline,
        description = description,
        maxDescriptionLines = maxDescriptionLines,
        onClick = onClick,
        onLongClick = onLongClick,
        beforeListItemAction = {
            if (faviconPainter != null) {
                Image(
                    painter = faviconPainter,
                    contentDescription = null,
                    modifier = Modifier.size(ICON_SIZE),
                )
            } else {
                Favicon(
                    url = url,
                    size = ICON_SIZE,
                    shape = faviconShape,
                )
            }
        },
        afterListItemAction = {
            if (iconPainter == null || onIconClick == null) {
                return@ListItem
            }

            if (showDivider) {
                VerticalDivider()
            }

            IconButton(
                onClick = onIconClick,
                modifier = iconButtonModifier.then(
                    Modifier
                        .size(ICON_SIZE),
                ),
            ) {
                Icon(
                    painter = iconPainter,
                    contentDescription = iconDescription,
                )
            }
        },
    )
}

/**
 * List item used to display a label and an icon at the beginning with an optional description
 * text and an optional [IconButton], [Icon], or Composable at the end.
 *
 * @param label The label in the list item.
 * @param modifier [Modifier] to be applied to the layout.
 * @param labelModifier [Modifier] to be applied to the label.
 * @param colors [ListItemColors] to be applied to the list item.
 * @param overline An optional text shown above the label.
 * @param maxLabelLines An optional maximum number of lines for the label text to span.
 * @param description An optional description text below the label.
 * @param maxDescriptionLines An optional maximum number of lines for the description text to span.
 * @param enabled Controls the enabled state of the list item. When `false`, the list item will not
 * be clickable.
 * @param minHeight An optional minimum height for the list item.
 * @param onClick Called when the user clicks on the item.
 * @param onLongClick Called when the user long clicks on the item.
 * @param beforeIconTint [Color] used to tint the icon.  Note: Color.Unspecified is used when you
 * wish to preserve the original colors of the icon.  This color should NOT be combined with
 * ListItemColors because ListItemDefaults will not allow you to specify Color.Unspecified.
 * @param beforeIconPainter [Painter] used to display an [Icon] before the list item.
 * @param beforeIconDescription Content description of the icon.
 * @param isBeforeIconHighlighted Whether or not the item should be highlighted with a notification icon.
 * @param showDivider Whether or not to display a vertical divider line before the [IconButton]
 * at the end.
 * @param afterIconTint [Color] used to tint the icon.  Note: Color.Unspecified is used when you
 * wish to preserve the original colors of the icon.  This color should NOT be combined with
 * ListItemColors because ListItemDefaults will not allow you to specify Color.Unspecified.
 * @param contentPaddingListItem The spacing values to be applied to the internal content of the list item.
 * @param afterIconPainter [Painter] used to display an icon after the list item.
 * @param afterIconDescription Content description of the icon.
 * @param onAfterIconClick Called when the user clicks on the icon. An [IconButton] will be
 * displayed if this is provided. Otherwise, an [Icon] will be displayed.
 * @param afterListAction Optional Composable for adding UI to the end of the list item.
 */
@Composable
fun IconListItem(
    label: String,
    modifier: Modifier = Modifier,
    labelModifier: Modifier = Modifier,
    colors: ListItemColors = ListItemDefaults.colors(),
    overline: String? = null,
    maxLabelLines: Int = 1,
    description: String? = null,
    maxDescriptionLines: Int = 1,
    enabled: Boolean = true,
    minHeight: Dp = LIST_ITEM_HEIGHT,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
    beforeIconTint: Color = ListItemDefaults.colors().leadingIconColor,
    beforeIconPainter: Painter,
    beforeIconDescription: String? = null,
    isBeforeIconHighlighted: Boolean = false,
    showDivider: Boolean = false,
    afterIconTint: Color = ListItemDefaults.colors().leadingIconColor,
    contentPaddingListItem: PaddingValues = PaddingValues(
        horizontal = FirefoxTheme.layout.space.dynamic200,
        vertical = FirefoxTheme.layout.space.static150,
    ),
    afterIconPainter: Painter? = null,
    afterIconDescription: String? = null,
    onAfterIconClick: (() -> Unit)? = null,
    afterListAction: (@Composable () -> Unit)? = null,
) {
    ListItem(
        label = label,
        modifier = modifier,
        labelModifier = labelModifier,
        colors = colors,
        overline = overline,
        maxLabelLines = maxLabelLines,
        description = description,
        maxDescriptionLines = maxDescriptionLines,
        enabled = enabled,
        minHeight = minHeight,
        onClick = onClick,
        onLongClick = onLongClick,
        contentPadding = contentPaddingListItem,
        beforeListItemAction = {
            IconListItemBeforeIcon(
                isHighlighted = enabled && isBeforeIconHighlighted,
                painter = beforeIconPainter,
                description = beforeIconDescription,
                tint = if (enabled) beforeIconTint else colors.disabledLeadingIconColor,
            )
        },
        afterListItemAction = {
            IconListItemAfterIcon(
                enabled = enabled,
                painter = afterIconPainter,
                description = afterIconDescription,
                tint = if (enabled) afterIconTint else colors.disabledTrailingIconColor,
                onClick = onAfterIconClick,
                listAction = afterListAction,
                showDivider = showDivider,
            )
        },
    )
}

@Composable
private fun IconListItemBeforeIcon(
    isHighlighted: Boolean,
    painter: Painter,
    description: String?,
    tint: Color,
) {
    BadgedIcon(
        painter = painter,
        isHighlighted = isHighlighted,
        tint = tint,
        size = BADGE_SIZE_SMALL,
        contentDescription = description,
        containerColor = MaterialTheme.colorScheme.information,
        modifier = Modifier.size(ICON_SIZE),
    )
}

@Composable
private fun IconListItemAfterIcon(
    showDivider: Boolean,
    enabled: Boolean,
    painter: Painter?,
    description: String?,
    tint: Color,
    onClick: (() -> Unit)?,
    listAction: (@Composable () -> Unit)?,
) {
    listAction?.let {
        it()
        return
    }

    if (painter == null) {
        return
    }

    if (showDivider) {
        VerticalDivider()
    }

    if (onClick == null) {
        Icon(
            painter = painter,
            contentDescription = description,
            tint = tint,
        )
    } else {
        IconButton(
            onClick = onClick,
            modifier = Modifier
                .size(ICON_SIZE)
                .semantics {
                    this.role = Role.Button
                },
            enabled = enabled,
        ) {
            Icon(
                painter = painter,
                contentDescription = description,
                tint = tint,
            )
        }
    }
}

/**
 * List item used to display a label with an optional description text and
 * a [RadioButton] at the beginning or at the end.
 *
 * @param label The label in the list item.
 * @param selected [Boolean] That indicates whether the [RadioButton] is currently selected.
 * @param modifier [Modifier] to be applied to the layout.
 * @param overline An optional text shown above the label.
 * @param maxLabelLines An optional maximum number of lines for the label text to span.
 * @param description An optional description text below the label.
 * @param maxDescriptionLines An optional maximum number of lines for the description text to span.
 * @param enabled Controls the enabled state of the list item. When `false`, the list item will not
 * be clickable.
 * @param showButtonAfter [Boolean] That indicates whether the [RadioButton] is after the [ListItem].
 * @param onClick Called when the user clicks on the item.
 */
@Composable
fun RadioButtonListItem(
    label: String,
    selected: Boolean,
    modifier: Modifier = Modifier,
    overline: String? = null,
    maxLabelLines: Int = 1,
    description: String? = null,
    maxDescriptionLines: Int = 1,
    enabled: Boolean = true,
    showButtonAfter: Boolean = false,
    onClick: (() -> Unit),
) {
    val radioButton: @Composable RowScope.() -> Unit = {
        RadioButton(
            selected = selected,
            modifier = Modifier
                .size(ICON_SIZE)
                .semantics {
                    testTag = "$label.radio.button"
                    testTagsAsResourceId = true
                }
                .clearAndSetSemantics {},
            enabled = enabled,
            onClick = onClick,
        )
    }
    ListItem(
        label = label,
        modifier = modifier
            .semantics(mergeDescendants = true) {
                this.selected = selected
                role = Role.RadioButton
            },
        maxLabelLines = maxLabelLines,
        overline = overline,
        description = description,
        maxDescriptionLines = maxDescriptionLines,
        enabled = enabled,
        onClick = onClick,
        beforeListItemAction = if (showButtonAfter) EmptyListItemSlot else radioButton,
        afterListItemAction = if (showButtonAfter) radioButton else EmptyListItemSlot,
    )
}

/**
 * List item used to display a label with an optional description text and
 * a [Switch] at the beginning or at the end.
 *
 * @param label The label in the list item.
 * @param checked [Boolean] That indicates whether the [Switch] is currently checked.
 * @param modifier [Modifier] to be applied to the layout.
 * @param overline An optional text shown above the label.
 * @param maxLabelLines An optional maximum number of lines for the label text to span.
 * @param description An optional description text below the label.
 * @param maxDescriptionLines An optional maximum number of lines for the description text to span.
 * @param enabled Controls the enabled state of the list item. When `false`, the list item will not
 * be clickable.
 * @param showSwitchAfter [Boolean] That indicates whether the [RadioButton] is after the [ListItem].
 * @param onClick Called when the user clicks the [Switch].
 */
@Composable
fun SwitchListItem(
    label: String,
    checked: Boolean,
    modifier: Modifier = Modifier,
    overline: String? = null,
    maxLabelLines: Int = 1,
    description: String? = null,
    maxDescriptionLines: Int = 1,
    enabled: Boolean = true,
    showSwitchAfter: Boolean = false,
    onClick: (Boolean) -> Unit,
) {
    val switch: @Composable RowScope.() -> Unit = {
        Switch(
            checked = checked,
            onCheckedChange = onClick,
            enabled = enabled,
            colors = SwitchDefaults.colors(),
            modifier = Modifier
                .clearAndSetSemantics {},
        )
    }

    ListItem(
        label = label,
        modifier = modifier.semantics(mergeDescendants = true) {
            this.selected = checked
            role = Role.Switch
        },
        maxLabelLines = maxLabelLines,
        overline = overline,
        description = description,
        maxDescriptionLines = maxDescriptionLines,
        enabled = enabled,
        onClick = { onClick(!checked) },
        beforeListItemAction = if (showSwitchAfter) EmptyListItemSlot else switch,
        afterListItemAction = if (showSwitchAfter) switch else EmptyListItemSlot,
    )
}

/**
 * Selectable list item used to display a label and a [Favicon] with an optional description text
 * at either the beginning or the end and an optional [IconButton] at the end.
 *
 * @param label The label in the list item.
 * @param url Website [url] for which the favicon will be shown.
 * @param isSelected The selected state of the item.
 * @param modifier [Modifier] to be applied to the layout.
 * @param overline An optional text shown above the label.
 * @param description An optional description text below the label.
 * @param faviconPainter Optional painter to use when fetching a new favicon is unnecessary.
 * @param onClick Called when the user clicks on the item.
 * @param onLongClick Called when the user long clicks on the item.
 * @param showDivider Whether or not to display a vertical divider line before the [IconButton]
 * at the end.
 * @param iconPainter [Painter] used to display an [IconButton] after the list item.
 * @param iconDescription Content description of the icon.
 * @param onIconClick Called when the user clicks on the icon.
 * @param iconSlot Slot for Composable to be used if [iconPainter] is not supplied.
 */
@Composable
fun SelectableFaviconListItem(
    label: String,
    url: String,
    isSelected: Boolean,
    modifier: Modifier = Modifier,
    overline: String? = null,
    description: String? = null,
    faviconPainter: Painter? = null,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
    showDivider: Boolean = false,
    iconPainter: Painter? = null,
    iconDescription: String? = null,
    onIconClick: (() -> Unit)? = null,
    iconSlot: (@Composable () -> Unit)? = null,
) {
    ListItem(
        label = label,
        modifier = modifier,
        overline = overline,
        description = description,
        onClick = onClick,
        onLongClick = onLongClick,
        beforeListItemAction = {
            SelectableItemIcon(
                isSelected = isSelected,
                icon = {
                    if (faviconPainter != null) {
                        Image(
                            painter = faviconPainter,
                            contentDescription = null,
                            modifier = Modifier.size(ICON_SIZE),
                        )
                    } else {
                        Favicon(url = url, size = ICON_SIZE)
                    }
                },
            )
        },
        afterListItemAction = {
            if ((iconPainter == null || onIconClick == null) && iconSlot == null) {
                return@ListItem
            }

            if (showDivider) {
                VerticalDivider()
            }

            when {
                iconPainter != null && onIconClick != null -> {
                    IconButton(
                        onClick = onIconClick,
                        modifier = Modifier.size(ICON_SIZE),
                    ) {
                        Icon(
                            painter = iconPainter,
                            contentDescription = iconDescription,
                            tint = ListItemDefaults.colors().trailingIconColor,
                        )
                    }
                }
                iconSlot != null -> iconSlot()
            }
        },
    )
}

/**
 * List item used to display a label and an icon at the beginning with an optional description
 * text and an optional [IconButton] or [Icon] at the end.
 *
 * @param label The label in the list item.
 * @param isSelected The selected state of the item.
 * @param modifier [Modifier] to be applied to the layout.
 * @param labelModifier [Modifier] to be applied to the label layout.
 * @param colors [ListItemColors] to be applied to the list item.
 * @param overline An optional text shown above the label.
 * @param maxLabelLines An optional maximum number of lines for the label text to span.
 * @param description An optional description text below the label.
 * @param enabled Controls the enabled state of the list item. When `false`, the list item will not
 * be clickable.
 * @param minHeight An optional minimum height for the list item.
 * @param onClick Called when the user clicks on the item.
 * @param onLongClick Called when the user long clicks on the item.
 * @param beforeIconTint [Color] used to tint the icon.  Note: Color.Unspecified is used when you
 * wish to preserve the original colors of the icon.  This color should NOT be combined with
 * ListItemColors because ListItemDefaults will not allow you to specify Color.Unspecified.
 * @param beforeIconPainter [Painter] used to display an [Icon] before the list item.
 * @param beforeIconDescription Content description of the icon.
 * @param showDivider Whether or not to display a vertical divider line before the [IconButton]
 * at the end.
 * @param afterIconTint [Color] used to tint the icon.  Note: Color.Unspecified is used when you
 * wish to preserve the original colors of the icon.  This color should NOT be combined with
 * ListItemColors because ListItemDefaults will not allow you to specify Color.Unspecified.
 * @param afterIconPainter [Painter] used to display an icon after the list item.
 * @param afterIconDescription Content description of the icon.
 * @param onAfterIconClick Called when the user clicks on the icon. An [IconButton] will be
 * displayed if this is provided. Otherwise, an [Icon] will be displayed.
 * @param iconSlot Optional Composable slot to be displayed after the list item if [afterIconPainter] is
 * not supplied.
 */
@Composable
@Suppress("CognitiveComplexMethod")
fun SelectableIconListItem(
    label: String,
    isSelected: Boolean,
    modifier: Modifier = Modifier,
    labelModifier: Modifier = modifier,
    colors: ListItemColors = ListItemDefaults.colors(),
    overline: String? = null,
    maxLabelLines: Int = 1,
    description: String? = null,
    enabled: Boolean = true,
    minHeight: Dp = LIST_ITEM_HEIGHT,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
    beforeIconTint: Color = ListItemDefaults.colors().leadingIconColor,
    beforeIconPainter: Painter,
    beforeIconDescription: String? = null,
    showDivider: Boolean = false,
    afterIconTint: Color = ListItemDefaults.colors().trailingIconColor,
    afterIconPainter: Painter? = null,
    afterIconDescription: String? = null,
    onAfterIconClick: (() -> Unit)? = null,
    iconSlot: (@Composable () -> Unit)? = null,
) {
    ListItem(
        label = label,
        modifier = modifier,
        labelModifier = labelModifier,
        colors = colors,
        overline = overline,
        maxLabelLines = maxLabelLines,
        description = description,
        enabled = enabled,
        minHeight = minHeight,
        onClick = onClick,
        onLongClick = onLongClick,
        beforeListItemAction = {
            SelectableItemIcon(
                isSelected = isSelected,
                icon = {
                    Icon(
                        painter = beforeIconPainter,
                        contentDescription = beforeIconDescription,
                        tint = if (enabled) beforeIconTint else colors.disabledLeadingIconColor,
                    )
                },
            )
        },
        afterListItemAction = {
            if (afterIconPainter == null && iconSlot == null) {
                return@ListItem
            }

            val tint = if (enabled) afterIconTint else colors.disabledTrailingIconColor

            if (showDivider) {
                VerticalDivider()
            }

            when {
                afterIconPainter != null -> {
                    if (onAfterIconClick == null) {
                        Icon(
                            painter = afterIconPainter,
                            contentDescription = afterIconDescription,
                            tint = tint,
                        )
                    } else {
                        IconButton(
                            onClick = onAfterIconClick,
                            modifier = Modifier.size(ICON_SIZE),
                            enabled = enabled,
                        ) {
                            Icon(
                                painter = afterIconPainter,
                                contentDescription = afterIconDescription,
                                tint = tint,
                            )
                        }
                    }
                }
                iconSlot != null -> iconSlot()
            }
        },
    )
}

/**
 * List item used to display a selectable item with an icon, label description and an action
 * composable at the end.
 *
 * @param label The label in the list item.
 * @param description The description text below the label.
 * @param icon The icon resource to be displayed at the beginning of the list item.
 * @param isSelected The selected state of the item.
 * @param modifier [Modifier] to be applied to the composable.
 * @param overline An optional text shown above the label.
 * @param descriptionTextColor [Color] to be applied to the description.
 * @param iconTint Tint to be applied to [icon].
 * @param labelOverflow How visual overflow should be handled for the label.
 * @param afterListItemAction Composable for adding UI to the end of the list item.
 * @param belowListItemContent Composable for adding UI to the bottom of the list item content.
 * @param showSelectableItemAfter [Boolean] That indicates whether the [Icon] is after the [ListItem].
 */
@Composable
fun SelectableListItem(
    label: String,
    description: String,
    @DrawableRes icon: Int,
    isSelected: Boolean,
    modifier: Modifier = Modifier,
    overline: String? = null,
    descriptionTextColor: Color = ListItemDefaults.colors().supportingTextColor,
    iconTint: Color = ListItemDefaults.colors().leadingIconColor,
    labelOverflow: TextOverflow = TextOverflow.Ellipsis,
    afterListItemAction: @Composable RowScope.() -> Unit,
    belowListItemContent: @Composable ColumnScope.() -> Unit = {},
    showSelectableItemAfter: Boolean = false,
) {
    val selectableItem: @Composable RowScope.() -> Unit = {
        SelectableItemIcon(
            icon = {
                Icon(
                    painter = painterResource(id = icon),
                    contentDescription = null,
                    tint = iconTint,
                )
            },
            isSelected = isSelected,
        )
    }
    ListItem(
        label = label,
        description = description,
        modifier = modifier,
        overline = overline,
        colors = ListItemDefaults.colors(supportingColor = descriptionTextColor),
        belowListItemContent = belowListItemContent,
        labelOverflow = labelOverflow,
        beforeListItemAction = {
            if (!showSelectableItemAfter) {
                selectableItem()
            }
        },
        afterListItemAction =
            if (showSelectableItemAfter) {
                selectableItem
            } else {
                afterListItemAction
            },
    )
}

/**
 * Icon composable that displays a checkmark icon when the item is selected.
 *
 * @param isSelected The selected state of the item.
 * @param icon Composable to display an icon when the item is not selected.
 */
@Composable
private fun SelectableItemIcon(
    isSelected: Boolean,
    icon: @Composable () -> Unit,
) {
    if (isSelected) {
        Checkbox(
            checked = true,
            onCheckedChange = null,
            modifier = Modifier.size(18.dp),
        )
    } else {
        icon()
    }
}

/**
 * Base list item used to display a label with an optional description text and
 * the flexibility to add custom UI to either end of the item.
 *
 * @param label The label in the list item.
 * @param modifier [Modifier] to be applied to the layout.
 * @param overline An optional text shown above the label.
 * @param colors [ListItemColors] to be applied ot the list item.
 * @param labelModifier [Modifier] to be applied to the label.
 * @param labelOverflow How visual overflow should be handled for the label.
 * @param maxLabelLines An optional maximum number of lines for the label text to span.
 * @param description An optional description text below the label.
 * @param maxDescriptionLines An optional maximum number of lines for the description text to span.
 * @param enabled Controls the enabled state of the list item. When `false`, the list item will not
 * be clickable.
 * @param minHeight An optional minimum height for the list item.
 * @param onClick Called when the user clicks on the item.
 * @param onLongClick Called when the user long clicks on the item.
 * @param contentPadding The spacing values to be applied to the internal content of the list item.
 * @param belowListItemContent Optional Composable for adding UI below the list item content.
 * @param beforeListItemAction Optional Composable for adding UI before the list item.
 * @param afterListItemAction Optional Composable for adding UI to the end of the list item.
 */
@Composable
private fun ListItem(
    label: String,
    modifier: Modifier = Modifier,
    overline: String? = null,
    colors: ListItemColors = ListItemDefaults.colors(),
    labelModifier: Modifier = Modifier,
    labelOverflow: TextOverflow = TextOverflow.Ellipsis,
    maxLabelLines: Int = 1,
    description: String? = null,
    maxDescriptionLines: Int = 1,
    enabled: Boolean = true,
    minHeight: Dp = LIST_ITEM_HEIGHT,
    onClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
    contentPadding: PaddingValues = PaddingValues(
        horizontal = FirefoxTheme.layout.space.dynamic200,
        vertical = FirefoxTheme.layout.space.static150,
    ),
    belowListItemContent: @Composable ColumnScope.() -> Unit = {},
    beforeListItemAction: @Composable RowScope.() -> Unit = {},
    afterListItemAction: @Composable RowScope.() -> Unit = {},
) {
    val haptics = LocalHapticFeedback.current
    val contentColor = if (enabled) {
        ListItemDefaults.contentColor
    } else {
        ListItemDefaults.colors().disabledLeadingIconColor
    }

    CompositionLocalProvider(LocalContentColor provides contentColor) {
        Row(
            modifier = modifier
                .height(IntrinsicSize.Min)
                .defaultMinSize(minHeight = minHeight)
                .thenConditional(
                    modifier = Modifier.combinedClickable(
                        onClick = { onClick?.invoke() },
                        onLongClick = {
                            onLongClick?.let {
                                haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                                it.invoke()
                            }
                        },
                    ),
                    predicate = { (onClick != null || onLongClick != null) && enabled },
                )
                .padding(contentPadding),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
        ) {
            beforeListItemAction()

            ListItemContent(
                label = label,
                modifier = Modifier.weight(1f),
                labelModifier = labelModifier,
                colors = colors,
                overline = overline,
                labelOverflow = labelOverflow,
                maxLabelLines = maxLabelLines,
                description = description,
                maxDescriptionLines = maxDescriptionLines,
                enabled = enabled,
                belowListItemContent = belowListItemContent,
            )

            afterListItemAction()
        }
    }
}

@Composable
private fun ListItemContent(
    label: String,
    modifier: Modifier = Modifier,
    labelModifier: Modifier = Modifier,
    colors: ListItemColors = ListItemDefaults.colors(),
    overline: String? = null,
    labelOverflow: TextOverflow = TextOverflow.Ellipsis,
    maxLabelLines: Int = 1,
    description: String? = null,
    maxDescriptionLines: Int = 1,
    enabled: Boolean = true,
    belowListItemContent: @Composable ColumnScope.() -> Unit = {},
) {
    val locale = remember(LocalConfiguration.current) { Locale.getDefault() }

    Column(
        modifier = modifier,
    ) {
        overline?.let {
            Text(
                text = it.uppercase(locale),
                color = colors.overlineColor,
                style = FirefoxTheme.typography.overline.copy(hyphens = Hyphens.Auto),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }

        Text(
            text = label,
            modifier = labelModifier,
            color = if (enabled) colors.headlineColor else colors.disabledHeadlineColor,
            overflow = labelOverflow,
            style = FirefoxTheme.typography.subtitle1.copy(hyphens = Hyphens.Auto),
            maxLines = maxLabelLines,
        )

        description?.let {
            Text(
                text = description,
                color = if (enabled) colors.supportingTextColor else colors.disabledHeadlineColor,
                overflow = TextOverflow.Ellipsis,
                maxLines = maxDescriptionLines,
                style = FirefoxTheme.typography.body2,
            )
        }

        belowListItemContent()
    }
}

@Composable
@Preview(name = "TextListItem", uiMode = Configuration.UI_MODE_NIGHT_YES)
private fun TextListItemPreview() {
    FirefoxTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surface)) {
            TextListItem(label = "Label only")

            TextListItem(
                label = "Label only - disabled",
                enabled = false,
            )
        }
    }
}

@Composable
@Preview(name = "TextListItem with a description", uiMode = Configuration.UI_MODE_NIGHT_YES)
private fun TextListItemWithDescriptionPreview() {
    FirefoxTheme {
        Surface {
            Column {
                TextListItem(
                    label = "Label + description",
                    description = "Description text",
                )

                TextListItem(
                    label = "Label + description - disabled",
                    description = "Description text",
                    enabled = false,
                )
            }
        }
    }
}

@Composable
@Preview(name = "TextListItem with overline and a description", uiMode = Configuration.UI_MODE_NIGHT_YES)
private fun TextListItemWithOverLineDescriptionPreview() {
    FirefoxTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surface)) {
            TextListItem(
                label = "Label + description",
                overline = "Overline",
                description = "Description text",
            )
        }
    }
}

@Composable
@Preview(name = "TextListItem with a right icon", uiMode = Configuration.UI_MODE_NIGHT_YES)
private fun TextListItemWithIconPreview() {
    FirefoxTheme {
        Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
            val context = LocalContext.current
            TextListItem(
                label = "Label + right icon button",
                onClick = {},
                iconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                iconDescription = "click me",
                onIconClick = { Toast.makeText(context, "icon click", TOAST_LENGTH).show() },
            )

            TextListItem(
                label = "Label + right icon",
                onClick = {},
                iconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                iconDescription = "click me",
            )

            TextListItem(
                label = "Label + right icon",
                overline = "Overline",
                onClick = {},
                iconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                iconDescription = "click me",
            )
        }
    }
}

@Suppress("LongMethod")
@Composable
@PreviewLightDark
private fun IconListItemPreview() {
    FirefoxTheme {
        Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
            IconListItem(
                label = "Left icon list item",
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
            )

            IconListItem(
                label = "Left icon list item highlighted",
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                isBeforeIconHighlighted = true,
            )

            IconListItem(
                label = "Left icon list item",
                colors = ListItemDefaults.colors(headlineColor = MaterialTheme.colorScheme.tertiary),
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
            )

            IconListItem(
                label = "Left icon list item highlighted",
                colors = ListItemDefaults.colors(headlineColor = MaterialTheme.colorScheme.tertiary),
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                isBeforeIconHighlighted = true,
            )

            IconListItem(
                label = "Left icon list item + right icon",
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                showDivider = true,
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
            )

            IconListItem(
                label = "Left icon list item highlighted + right icon",
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                showDivider = true,
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
                isBeforeIconHighlighted = true,
            )

            IconListItem(
                label = "Left icon list item + right icon (disabled)",
                enabled = false,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
            )

            IconListItem(
                label = "Left icon list item highlighted + right icon (disabled)",
                enabled = false,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
                isBeforeIconHighlighted = true,
            )

            IconListItem(
                label = "Left icon list item + right icon (disabled)",
                overline = "Overline",
                enabled = false,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
            )

            IconListItem(
                label = "Colorful icon list item",
                enabled = true,
                onClick = {},
                beforeIconTint = Color.Unspecified,
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_shield_slash_critical_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
            )

            IconListItem(
                label = "Left icon list item highlighted + right icon (disabled)",
                overline = "Overline",
                enabled = false,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
                isBeforeIconHighlighted = true,
            )
        }
    }
}

@Composable
@Preview(
    name = "IconListItem with after list action",
    uiMode = Configuration.UI_MODE_NIGHT_YES,
)
private fun IconListItemWithAfterListActionPreview() {
    FirefoxTheme {
        Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
            val context = LocalContext.current
            IconListItem(
                label = "IconListItem + right icon + clicks",
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = null,
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                afterIconDescription = "click me",
                onAfterIconClick = { Toast.makeText(context, "icon click", TOAST_LENGTH).show() },
            )
        }
    }
}

@Composable
@Preview(
    name = "FaviconListItem with a right icon and onClicks",
    uiMode = Configuration.UI_MODE_NIGHT_YES,
)
private fun FaviconListItemPreview() {
    FirefoxTheme {
        Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
            val context = LocalContext.current
            FaviconListItem(
                label = "Favicon + right icon + clicks",
                url = "",
                description = "Description text",
                onClick = { Toast.makeText(context, "list item click", TOAST_LENGTH).show() },
                iconPainter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                onIconClick = { Toast.makeText(context, "icon click", TOAST_LENGTH).show() },
                showDivider = true,
            )

            FaviconListItem(
                label = "Favicon + painter",
                url = "",
                description = "Description text",
                faviconPainter = painterResource(id = iconsR.drawable.mozac_ic_collection_24),
                onClick = { Toast.makeText(context, "list item click", TOAST_LENGTH).show() },
            )

            FaviconListItem(
                label = "Favicon + painter",
                overline = "Overline",
                url = "",
                description = "Description text",
                faviconPainter = painterResource(id = iconsR.drawable.mozac_ic_collection_24),
                onClick = { Toast.makeText(context, "list item click", TOAST_LENGTH).show() },
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun RadioButtonListItemPreview() {
    val radioOptions =
        listOf("Radio button first item", "Radio button second item", "Radio button third item")
    val (selectedOption, onOptionSelected) = remember { mutableStateOf(radioOptions[1]) }
    FirefoxTheme {
        Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
            radioOptions.forEach { text ->
                RadioButtonListItem(
                    label = text,
                    overline = "Overline",
                    description = "$text description",
                    onClick = { onOptionSelected(text) },
                    selected = (text == selectedOption),
                )
            }
            radioOptions.forEach { text ->
                RadioButtonListItem(
                    label = text,
                    selected = (text == selectedOption),
                    overline = "Overline",
                    description = "$text description",
                    enabled = false,
                    showButtonAfter = true,
                    onClick = { onOptionSelected(text) },
                )
            }
        }
    }
}

@Composable
@PreviewLightDark
private fun SwitchListItemPreview() {
    FirefoxTheme {
        Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
            SwitchListItem(
                label = "Switch item",
                overline = "Overline",
                description = "Switch item description",
                checked = true,
                onClick = { },
            )
            SwitchListItem(
                label = "Switch item",
                overline = "Overline",
                description = "Switch item description",
                checked = true,
                enabled = false,
                showSwitchAfter = true,
                onClick = { },
            )
        }
    }
}

private data class SelectableFaviconListItemPreviewState(
    val label: String,
    val url: String = "",
    val isSelected: Boolean = false,
    val overline: String? = null,
    val description: String? = "Description text",
    val faviconRes: Int? = null,
    val onClick: (() -> Unit)? = { },
    val onLongClick: (() -> Unit)? = { },
    val showFaviconAfter: Boolean = false,
    val iconRes: Int? = null,
    val onIconClick: (() -> Unit)? = { },
)

private class SelectableFaviconListItemParameterProvider :
    PreviewParameterProvider<SelectableFaviconListItemPreviewState> {
    override val values: Sequence<SelectableFaviconListItemPreviewState>
        get() = sequenceOf(
            SelectableFaviconListItemPreviewState(
                label = "Favicon + right icon",
                faviconRes = iconsR.drawable.mozac_ic_collection_24,
                iconRes = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
            ),
            SelectableFaviconListItemPreviewState(
                label = "Favicon + right icon + overline",
                overline = "Overline",
                faviconRes = iconsR.drawable.mozac_ic_collection_24,
                iconRes = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
            ),
            SelectableFaviconListItemPreviewState(
                label = "Selected favicon + right icon",
                isSelected = true,
                faviconRes = iconsR.drawable.mozac_ic_collection_24,
                iconRes = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
            ),
            SelectableFaviconListItemPreviewState(
                label = "Favicon + painter",
                faviconRes = iconsR.drawable.mozac_ic_collection_24,
            ),
            SelectableFaviconListItemPreviewState(
                label = "Selected favicon + painter",
                faviconRes = iconsR.drawable.mozac_ic_collection_24,
                isSelected = true,
            ),
        )
}

@Composable
@PreviewLightDark
private fun SelectableFaviconListItemPreview(
    @PreviewParameter(SelectableFaviconListItemParameterProvider::class) state: SelectableFaviconListItemPreviewState,
) {
    val faviconPainter = state.faviconRes?.let { painterResource(it) }
    val iconPainter = state.iconRes?.let { painterResource(it) }

    FirefoxTheme {
        Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
            SelectableFaviconListItem(
                label = state.label,
                url = state.url,
                isSelected = state.isSelected,
                overline = state.overline,
                description = state.description,
                faviconPainter = faviconPainter,
                onClick = state.onClick,
                onLongClick = state.onLongClick,
                showDivider = false,
                iconPainter = iconPainter,
                onIconClick = {},
            )
        }
    }
}

@Composable
@Preview(name = "SelectableIconListItem", uiMode = Configuration.UI_MODE_NIGHT_YES)
@Suppress("LongMethod")
private fun SelectableIconListItemPreview() {
    FirefoxTheme {
        Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
            SelectableIconListItem(
                label = "Left icon list item",
                isSelected = false,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
            )

            SelectableIconListItem(
                label = "Selected left icon list item",
                isSelected = true,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
            )

            SelectableIconListItem(
                label = "Left icon list item",
                isSelected = false,
                colors = ListItemDefaults.colors(headlineColor = MaterialTheme.colorScheme.tertiary),
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
            )

            SelectableIconListItem(
                label = "Selected left icon list item",
                isSelected = true,
                colors = ListItemDefaults.colors(headlineColor = MaterialTheme.colorScheme.tertiary),
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
            )

            SelectableIconListItem(
                label = "Left icon list item + right icon",
                isSelected = false,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
            )

            SelectableIconListItem(
                label = "Selected left icon list item + right icon",
                isSelected = true,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
            )

            SelectableIconListItem(
                label = "Left icon list item + right icon (disabled)",
                isSelected = false,
                enabled = false,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
            )

            SelectableIconListItem(
                label = "Selected left icon list item + right icon (disabled)",
                isSelected = true,
                enabled = false,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
            )

            SelectableIconListItem(
                label = "Selected left icon list item + right icon (disabled)",
                isSelected = true,
                overline = "Overline",
                enabled = false,
                onClick = {},
                beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_folder_24),
                beforeIconDescription = "click me",
                afterIconPainter = painterResource(iconsR.drawable.mozac_ic_chevron_right_24),
                afterIconDescription = null,
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun SelectableListItemPreview() {
    FirefoxTheme {
        Column(Modifier.background(MaterialTheme.colorScheme.surface)) {
            SelectableListItem(
                label = "Selected item",
                description = "Description text",
                icon = iconsR.drawable.mozac_ic_folder_24,
                isSelected = true,
                afterListItemAction = {},
            )

            SelectableListItem(
                label = "Non selectable item",
                description = "without after action",
                icon = iconsR.drawable.mozac_ic_folder_24,
                isSelected = false,
                afterListItemAction = {},
            )

            SelectableListItem(
                label = "Non selectable item",
                description = "with after action",
                icon = iconsR.drawable.mozac_ic_folder_24,
                isSelected = false,
                afterListItemAction = {
                    IconButton(
                        onClick = {},
                        modifier = Modifier.size(ICON_SIZE),
                    ) {
                        Icon(
                            painter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                            contentDescription = null,
                        )
                    }
                },
            )

            SelectableListItem(
                label = "Non selectable item",
                description = "with after action",
                icon = iconsR.drawable.mozac_ic_folder_24,
                isSelected = false,
                overline = "Overline",
                afterListItemAction = {
                    IconButton(
                        onClick = {},
                        modifier = Modifier.size(ICON_SIZE),
                    ) {
                        Icon(
                            painter = painterResource(iconsR.drawable.mozac_ic_ellipsis_vertical_24),
                            contentDescription = null,
                        )
                    }
                },
            )
            SelectableListItem(
                label = "Selected item",
                description = "Description text",
                icon = iconsR.drawable.mozac_ic_folder_24,
                isSelected = true,
                afterListItemAction = {},
                showSelectableItemAfter = true,
            )

            SelectableListItem(
                label = "Non selectable item",
                description = "without after action",
                icon = iconsR.drawable.mozac_ic_folder_24,
                isSelected = false,
                afterListItemAction = {},
                showSelectableItemAfter = true,
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun IconListItemBeforeIconPreview() {
    FirefoxTheme {
        Surface {
            Row(modifier = Modifier.padding(all = FirefoxTheme.layout.space.static100)) {
                IconListItemBeforeIcon(
                    isHighlighted = false,
                    painter = painterResource(iconsR.drawable.mozac_ic_shield_slash_critical_24),
                    description = "",
                    tint = Color.Unspecified,
                )

                IconListItemBeforeIcon(
                    isHighlighted = true,
                    painter = painterResource(iconsR.drawable.mozac_ic_shield_slash_critical_24),
                    description = "",
                    tint = Color.Unspecified,
                )
            }
        }
    }
}
