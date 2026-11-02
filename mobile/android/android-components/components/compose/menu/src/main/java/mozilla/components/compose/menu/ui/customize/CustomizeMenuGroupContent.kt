/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.modifier.dashedBorder
import mozilla.components.compose.base.theme.AcornCorners
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.DISABLED_ALPHA

@Composable
internal fun CustomizeMenuGroupContent(
    group: MenuItemsGroup,
    onInteraction: (MenuEvent) -> Unit,
    modifier: Modifier = Modifier,
    isMandatory: Boolean = false,
    isHidden: Boolean = false,
    backgroundColor: Color = Color.Transparent,
) {
    Box(modifier = if (isMandatory) modifier.alpha(DISABLED_ALPHA) else modifier) {
        when (group) {
            is MenuItemsGroup.Grid -> {
                CustomizeMenuGridGroupContent(
                    group = group,
                    isMandatory = isMandatory,
                    isHidden = isHidden,
                    backgroundColor = backgroundColor,
                )
            }

            is MenuItemsGroup.Row -> {
                CustomizeMenuRowGroupContent(
                    group = group,
                    onInteraction = onInteraction,
                    isMandatory = isMandatory,
                    isHidden = isHidden,
                    backgroundColor = backgroundColor,
                )
            }
        }
    }
}

@Composable
internal fun Modifier.customizeGroupBorder(isMandatory: Boolean, isHidden: Boolean): Modifier {
    return if (isHidden) {
        this.dashedBorder(
            color = MaterialTheme.colorScheme.outlineVariant,
            cornerRadius = AcornCorners.extraLarge,
            dashHeight = 2.dp,
            dashWidth = 6.dp,
            dashGap = 6.dp,
        )
    } else if (isMandatory) {
        this.border(
            width = 1.dp,
            color = MaterialTheme.colorScheme.outlineVariant,
            shape = MaterialTheme.shapes.extraLarge,
        )
    } else {
        this
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuGroupContentPreview() {
    AcornTheme {
        Surface {
            CustomizeMenuGroupContent(
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
