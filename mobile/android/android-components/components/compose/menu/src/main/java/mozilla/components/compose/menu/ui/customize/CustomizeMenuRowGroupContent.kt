/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.DISABLED_ALPHA

@Composable
internal fun CustomizeMenuRowGroupContent(
    group: MenuItemsGroup.Row,
    onInteraction: (MenuEvent) -> Unit,
    modifier: Modifier = Modifier,
    isMandatory: Boolean = false,
    isHidden: Boolean = false,
    backgroundColor: Color = Color.Transparent,
) {
    val isInactive = isMandatory || isHidden
    val rowBackgroundColor = if (isInactive) Color.Transparent else backgroundColor
    val containerColor = if (isInactive) Color.Transparent else MaterialTheme.colorScheme.surfaceBright
    val contentAlpha = if (isHidden) DISABLED_ALPHA else 1f

    Row(
        modifier = modifier,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val rowModifier =
            Modifier.weight(1f)
                .customizeRowGroupStyle(
                    isMandatory = isMandatory,
                    isHidden = isHidden,
                    group = group,
                    backgroundColor = rowBackgroundColor,
                )

        Box(modifier = rowModifier) {
            CustomizeMenuListItemGroup(
                items = group.items,
                onClick = onInteraction,
                containerColor = containerColor,
                showDivider = isInactive,
                contentAlpha = contentAlpha,
                contentModifier = { index ->
                    if (group.items.size > 2 && index == 2) {
                        Modifier.fadeBottomGradient()
                    } else {
                        Modifier
                    }
                },
            )
        }

        CustomizeMenuDragIndicator()
    }
}

@Composable
private fun Modifier.fadeBottomGradient(): Modifier {
    val fadeHeightPx = with(LocalDensity.current) { 40.dp.toPx() }
    return this.graphicsLayer(compositingStrategy = CompositingStrategy.Offscreen).drawWithContent {
        drawContent()
        drawRect(
            brush =
                Brush.verticalGradient(
                    colors =
                        listOf(
                            Color.Black,
                            Color.Transparent,
                        ),
                    startY = maxOf(0f, size.height - fadeHeightPx),
                    endY = size.height,
                ),
            blendMode = BlendMode.DstIn,
        )
    }
}

@Composable
private fun Modifier.customizeRowGroupStyle(
    group: MenuItemsGroup.Row,
    isMandatory: Boolean,
    isHidden: Boolean,
    backgroundColor: Color = Color.Transparent,
): Modifier {
    return this.let { if (group.items.size > 2) it.height(148.dp) else it }
        .clip(shape = MaterialTheme.shapes.extraLarge)
        .background(backgroundColor)
        .customizeGroupBorder(isMandatory = isMandatory, isHidden = isHidden)
}

@PreviewLightDark
@Composable
private fun CustomizeMenuRowGroupContentPreview() {
    AcornTheme {
        Surface {
            CustomizeMenuRowGroupContent(
                group =
                    MenuItemsGroup.Row(
                        id = "row",
                        items =
                            listOf(
                                StandardMenuItem(
                                    title = MenuItemTitle(mozilla.components.compose.base.text.Text.String("Item"))
                                )
                            ),
                    ),
                onInteraction = {},
            )
        }
    }
}
