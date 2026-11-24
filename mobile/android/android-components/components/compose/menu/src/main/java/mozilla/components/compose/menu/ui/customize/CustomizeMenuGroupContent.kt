/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.modifier.dashedBorder
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornCorners
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.utils.DISABLED_ALPHA
import mozilla.components.compose.menu.ui.utils.mainSectionMenuItemsPreviewData
import mozilla.components.ui.icons.R as iconsR

/** Test tag of the button editing the items of a group. */
internal const val EDIT_GROUP_ITEMS_BUTTON_TAG = "edit_group_items_button"

/**
 * Show a group of the customize menu screen, with the handle to drag it by.
 *
 * @param group The group to show.
 * @param modifier The [Modifier] to be applied to the group and its handle.
 * @param state How the group is shown.
 * @param dragState State of dragging the items, or `null` to not drag them.
 * @param isHighlighted Whether to highlight the group, as the one the open actions sheet acts on.
 * @param onEditClick Called when the user taps the button editing the items of the group, or `null` to not show it.
 * @param onDragHandleLongPress Called when the user long presses the drag handle without moving.
 */
@Composable
internal fun CustomizeMenuGroupContent(
    group: MenuItemsGroup,
    modifier: Modifier = Modifier,
    state: CustomizeMenuGroupState = CustomizeMenuGroupState.SHOWN,
    dragState: CustomizeMenuDragState<*>? = null,
    isHighlighted: Boolean = false,
    onEditClick: (() -> Unit)? = null,
    onDragHandleLongPress: () -> Unit = {},
) {
    val isHidden = state == CustomizeMenuGroupState.HIDDEN
    val haptic = LocalHapticFeedback.current
    val hasSmallEndCorners = onEditClick != null
    val sectionShape = MaterialTheme.shapes.extraLarge.withSmallEndCorners(hasSmallEndCorners)
    val currentOnDragHandleLongPress by rememberUpdatedState(onDragHandleLongPress)

    Row(
        modifier = modifier.then(if (onEditClick != null) Modifier.height(IntrinsicSize.Min) else Modifier),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // The Box parent allows the drag gesture surviving a "isHidden" state change.
        Box(
            modifier =
                Modifier.weight(1f)
                    .dragGroupAfterLongPress(dragState, group.id)
                    .then(
                        if (isHighlighted && isHidden) {
                            Modifier.background(
                                MaterialTheme.colorScheme.surfaceContainerHighest,
                                sectionShape,
                            )
                        } else {
                            Modifier
                        }
                    )
                    .then(
                        if (isHighlighted) {
                            Modifier.border(
                                width = AcornTheme.layout.border.heavy,
                                color = MaterialTheme.colorScheme.tertiary,
                                shape = sectionShape,
                            )
                        } else {
                            Modifier
                        }
                    ),
            propagateMinConstraints = true,
        ) {
            CustomizedGroupTheme(
                containerColor =
                    when {
                        isHidden -> Color.Transparent
                        isHighlighted -> MaterialTheme.colorScheme.surfaceContainerHighest
                        else -> null
                    }
            ) {
                CustomizeMenuGroup(
                    group = group,
                    isHidden = isHidden,
                    isHighlighted = isHighlighted,
                    hasSmallEndCorners = hasSmallEndCorners,
                )
            }
        }

        if (onEditClick != null) {
            EditGroupItemsButton(onClick = onEditClick, modifier = Modifier.testTag(EDIT_GROUP_ITEMS_BUTTON_TAG))
        }

        CustomizeMenuDragIndicator(
            modifier =
                Modifier.dragGroup(dragState, group.id) {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    currentOnDragHandleLongPress()
                }
        )
    }
}

@Composable
private fun EditGroupItemsButton(onClick: () -> Unit, modifier: Modifier = Modifier) {
    Box(
        modifier =
            modifier
                .padding(start = AcornTheme.layout.space.static100)
                .fillMaxHeight()
                .width(AcornTheme.layout.size.static600)
                .clip(
                    MaterialTheme.shapes.extraLarge.copy(
                        topStart = MaterialTheme.shapes.extraSmall.topStart,
                        bottomStart = MaterialTheme.shapes.extraSmall.bottomStart,
                    )
                )
                .background(MaterialTheme.colorScheme.surfaceBright)
                .clickable(role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_edit_24),
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurface,
        )
    }
}

@Composable
private fun CustomizeMenuGroup(
    group: MenuItemsGroup,
    isHidden: Boolean,
    isHighlighted: Boolean,
    hasSmallEndCorners: Boolean,
) {
    val contentAlpha = if (isHidden) DISABLED_ALPHA else 1f
    val groupModifier = if (isHidden && !isHighlighted) Modifier.hiddenBorder() else Modifier

    when (group) {
        is MenuItemsGroup.Grid ->
            CustomizeMenuGridContainer(
                items = group.items,
                modifier = groupModifier,
                contentAlpha = contentAlpha,
                showTitles = group.showTitles,
            )

        is MenuItemsGroup.Row ->
            CustomizeMenuListItemGroup(
                items = group.items,
                modifier = groupModifier,
                showDivider = isHidden,
                contentAlpha = contentAlpha,
                hasSmallEndCorners = hasSmallEndCorners,
            )
    }
}

/**
 * Override the colors which the items inside [content] use, by replacing the theme token values they read: the color of
 * their container, and [androidx.compose.material3.ColorScheme.onSurface], replaced by the muted color of their icons.
 * Everything inside reading it is muted, like the titles, badge labels and disabled content of the items, so controls
 * meant to stand out should be placed outside.
 *
 * @param containerColor The container color to use, or `null` to keep the one from the current theme.
 * @param content The content to show.
 */
@Composable
internal fun CustomizedGroupTheme(containerColor: Color?, content: @Composable () -> Unit) {
    val colorScheme = MaterialTheme.colorScheme
    MaterialTheme(
        colorScheme =
            remember(colorScheme, containerColor) {
                colorScheme.copy(
                    surfaceBright = containerColor ?: colorScheme.surfaceBright,
                    onSurface = colorScheme.onSurfaceVariant,
                )
            },
        content = content,
    )
}

/**
 * Dashed border shown around the sections the user removed from the menu.
 *
 * @param isHighlighted Whether to highlight the border, to show that a dragged section is dropped inside.
 */
@Composable
internal fun Modifier.hiddenBorder(isHighlighted: Boolean = false): Modifier =
    this.dashedBorder(
        color = if (isHighlighted) MaterialTheme.colorScheme.tertiary else MaterialTheme.colorScheme.outlineVariant,
        cornerRadius = AcornCorners.extraLarge,
        dashHeight = if (isHighlighted) AcornTheme.layout.border.heaviest else AcornTheme.layout.border.heavy,
        dashWidth = 6.dp,
        dashGap = 6.dp,
    )

@PreviewLightDark
@Composable
private fun CustomizeMenuHighlightedGroupContentPreview() {
    AcornTheme {
        Surface(color = MaterialTheme.colorScheme.surfaceContainer) {
            CustomizeMenuGroupContent(
                group = MenuItemsGroup.Row(id = "row", items = mainSectionMenuItemsPreviewData),
                isHighlighted = true,
                onEditClick = {},
            )
        }
    }
}

private class CustomizeMenuGroupStateProvider : PreviewParameterProvider<CustomizeMenuGroupState> {
    override val values = CustomizeMenuGroupState.entries.asSequence()
}

@PreviewLightDark
@Composable
private fun CustomizeMenuRowGroupContentPreview(
    @PreviewParameter(CustomizeMenuGroupStateProvider::class) state: CustomizeMenuGroupState
) {
    AcornTheme {
        Surface(color = MaterialTheme.colorScheme.surfaceContainer) {
            CustomizeMenuGroupContent(
                group = MenuItemsGroup.Row(id = "row", items = mainSectionMenuItemsPreviewData),
                state = state,
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuGridGroupContentPreview(
    @PreviewParameter(CustomizeMenuGroupStateProvider::class) state: CustomizeMenuGroupState
) {
    AcornTheme {
        Surface(color = MaterialTheme.colorScheme.surfaceContainer) {
            CustomizeMenuGroupContent(
                group =
                    MenuItemsGroup.Grid(
                        id = "grid",
                        items =
                            listOf(
                                StandardMenuItem(
                                    title = MenuItemTitle(Text.String("Back")),
                                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_back_24),
                                ),
                                StandardMenuItem(
                                    title = MenuItemTitle(Text.String("Forward")),
                                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_forward_24),
                                ),
                                StandardMenuItem(
                                    title = MenuItemTitle(Text.String("Share")),
                                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_share_android_24),
                                ),
                                StandardMenuItem(
                                    title = MenuItemTitle(Text.String("Refresh")),
                                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_arrow_clockwise_24),
                                ),
                            ),
                    ),
                state = state,
            )
        }
    }
}
