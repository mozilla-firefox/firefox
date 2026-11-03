/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.semantics.Role.Companion.Button
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.text.value
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemState.DEFAULT
import mozilla.components.ui.icons.R as iconsR

/**
 * A menu item shown as a cell in a grid.
 *
 * @param title The title of the menu item.
 * @param icon An icon of the menu item.
 * @param onClickEvent Optional [MenuEvent] to dispatch when the menu item is clicked.
 * @param onInteraction The callback to invoke when the menu item is interacted with.
 * @param contentDescription Optional custom content description for the menu item.
 * @param index The optional index of this item in the grid.
 * @param modifier The modifier to apply to the menu item.
 * @param state The state of this menu item.
 * @param onLongClickEvent Optional [MenuEvent] to dispatch when the menu item is long-clicked.
 * @param showContainer Whether the menu item should be shown inside a styled container.
 * @param shape The [Shape] to clip the background into.
 */
@Composable
internal fun MenuGridItem(
    title: Text,
    icon: MenuItemIcon,
    onClickEvent: MenuEvent?,
    onInteraction: (MenuEvent) -> Unit,
    modifier: Modifier = Modifier,
    contentDescription: Text?,
    index: Int? = null,
    state: MenuItemState = DEFAULT,
    onLongClickEvent: MenuEvent? = null,
    showContainer: Boolean = false,
    shape: Shape = RectangleShape,
) {
    val description = contentDescription?.value
    val backgroundColor =
        if (showContainer) {
            MaterialTheme.colorScheme.surfaceBright
        } else {
            Color.Transparent
        }

    val containerModifier =
        modifier
            .clip(shape)
            .background(backgroundColor)
            .menuItemClickableBehavior(
                state = state,
                role = Button,
                onClickEvent = onClickEvent,
                onLongClickEvent = onLongClickEvent,
                onInteraction = onInteraction,
            )
            .menuGridItemSemantics(index, description, onClickEvent, onLongClickEvent)

    Column(
        modifier =
            if (showContainer) {
                containerModifier.padding(
                    horizontal = AcornTheme.layout.space.static50,
                    vertical = AcornTheme.layout.space.static150,
                )
            } else {
                containerModifier
            },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = if (title.value.isNotEmpty()) Arrangement.Top else Arrangement.Center,
    ) {
        MenuGridItemContent(title, icon, state)
    }
}

@PreviewLightDark
@Composable
private fun MenuGridItemPreview() {
    AcornTheme {
        Surface {
            MenuGridItem(
                title = Text.String("Title"),
                contentDescription = Text.String(""),
                onClickEvent = object : MenuEvent {},
                onInteraction = {},
                index = 0,
                modifier = Modifier.fillMaxWidth(),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24, isHighlighted = true),
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun MenuGridItemWithContainerPreview() {
    AcornTheme {
        Surface {
            MenuGridItem(
                title = Text.String("Title"),
                contentDescription = Text.String(""),
                onClickEvent = object : MenuEvent {},
                onInteraction = {},
                index = 0,
                modifier = Modifier.fillMaxWidth(),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
                showContainer = true,
                shape = MaterialTheme.shapes.extraLarge,
            )
        }
    }
}
