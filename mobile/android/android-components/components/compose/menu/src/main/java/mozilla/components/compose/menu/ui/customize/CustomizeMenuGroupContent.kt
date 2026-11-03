/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.Row
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
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
import mozilla.components.compose.menu.ui.DISABLED_ALPHA
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.utils.mainSectionMenuItemsPreviewData
import mozilla.components.ui.icons.R as iconsR

@Composable
internal fun CustomizeMenuGroupContent(
    group: MenuItemsGroup,
    modifier: Modifier = Modifier,
    state: CustomizeMenuGroupState = CustomizeMenuGroupState.SHOWN,
) {
    val isHidden = state == CustomizeMenuGroupState.HIDDEN
    val contentAlpha = if (isHidden) DISABLED_ALPHA else 1f

    CustomizedGroupTheme(containerColor = if (isHidden) Color.Transparent else null) {
        Row(
            modifier = modifier,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            val groupModifier = Modifier.weight(1f).let { if (isHidden) it.hiddenSectionBorder() else it }

            when (group) {
                is MenuItemsGroup.Grid ->
                    CustomizeMenuGridContainer(
                        items = group.items,
                        modifier = groupModifier,
                        contentAlpha = contentAlpha,
                    )

                is MenuItemsGroup.Row ->
                    CustomizeMenuListItemGroup(
                        items = group.items,
                        modifier = groupModifier,
                        showDivider = isHidden,
                        contentAlpha = contentAlpha,
                    )
            }

            CustomizeMenuDragIndicator()
        }
    }
}

/**
 * Override the color which the items inside [content] use as their container, by replacing the theme token value they
 * read.
 *
 * @param containerColor The color to use, or `null` to keep the one from the current theme.
 * @param content The content to show.
 */
@Composable
private fun CustomizedGroupTheme(containerColor: Color?, content: @Composable () -> Unit) {
    if (containerColor == null) {
        content()
        return
    }

    val colorScheme = MaterialTheme.colorScheme
    MaterialTheme(
        colorScheme = remember(colorScheme, containerColor) { colorScheme.copy(surfaceBright = containerColor) },
        content = content,
    )
}

/** Dashed border shown around the sections the user removed from the menu. */
@Composable
internal fun Modifier.hiddenSectionBorder(): Modifier =
    this.dashedBorder(
        color = MaterialTheme.colorScheme.outlineVariant,
        cornerRadius = AcornCorners.extraLarge,
        dashHeight = AcornTheme.layout.border.heavy,
        dashWidth = 6.dp,
        dashGap = 6.dp,
    )

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
