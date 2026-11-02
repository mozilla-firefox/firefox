/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import androidx.compose.foundation.layout.Row
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.ui.DISABLED_ALPHA

@Composable
internal fun CustomizeMenuGridGroupContent(
    group: MenuItemsGroup.Grid,
    modifier: Modifier = Modifier,
    isMandatory: Boolean = false,
    isHidden: Boolean = false,
    backgroundColor: Color = Color.Transparent,
) {
    Row(
        modifier = modifier,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val gridModifier = Modifier.weight(1f).customizeGroupBorder(isMandatory = isMandatory, isHidden = isHidden)

        CustomizeMenuGridContainer(
            items = group.items,
            modifier = gridModifier,
            backgroundColor = if (isMandatory || isHidden) Color.Transparent else backgroundColor,
            itemContainerColor =
                if (isMandatory || isHidden) Color.Transparent else MaterialTheme.colorScheme.surfaceBright,
            contentAlpha = if (isHidden) DISABLED_ALPHA else 1f,
        )

        CustomizeMenuDragIndicator()
    }
}

@PreviewLightDark
@Composable
private fun CustomizeMenuGridGroupContentPreview() {
    AcornTheme {
        Surface {
            CustomizeMenuGridGroupContent(
                group =
                    MenuItemsGroup.Grid(
                        id = "grid",
                        items =
                            listOf(
                                StandardMenuItem(
                                    title = MenuItemTitle(mozilla.components.compose.base.text.Text.String("Item"))
                                )
                            ),
                    )
            )
        }
    }
}
