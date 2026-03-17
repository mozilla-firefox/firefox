/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.graphics.drawable.Drawable
import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import com.google.accompanist.drawablepainter.rememberDrawablePainter
import mozilla.components.compose.base.badge.BadgedIcon
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.LongPressIconButton
import mozilla.components.compose.base.menu.CustomPlacementPopup
import mozilla.components.compose.base.menu.CustomPlacementPopupVerticalContent
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton.State
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.compose.browser.toolbar.ui.MenuState.CLick
import mozilla.components.compose.browser.toolbar.ui.MenuState.LongClick
import mozilla.components.compose.browser.toolbar.ui.MenuState.None
import mozilla.components.ui.icons.R as iconsR

/**
 * A clickable icon used to represent an action that can be added to the toolbar.
 *
 * @param icon A [Drawable] to use as icon for this button.
 * @param contentDescription A [String] to use as content description for this button.
 * @param state The current [State] of the action button.
 * @param highlighted Whether or not to highlight this button.
 * @param onClick [BrowserToolbarInteraction] describing how to handle this button being clicked.
 * @param onLongClick Optional [BrowserToolbarInteraction] describing how to handle this button being long clicked.
 * @param onInteraction Callback for handling [BrowserToolbarEvent]s on user interactions.
 */
@Composable
@Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
internal fun ActionButton(
    icon: Drawable,
    contentDescription: String,
    state: State = State.DEFAULT,
    highlighted: Boolean = false,
    onClick: BrowserToolbarInteraction? = null,
    onLongClick: BrowserToolbarInteraction? = null,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    val shouldReactToLongClicks = remember(onLongClick) {
        onLongClick != null
    }
    var currentMenuState by remember { mutableStateOf(None) }
    val colors = MaterialTheme.colorScheme
    val tint = remember(state, colors) {
        when (state) {
            State.ACTIVE -> colors.tertiary
            State.DISABLED -> colors.onSurface.copy(alpha = 0.38f)
            State.DEFAULT -> colors.onSurface
        }
    }

    val isEnabled = remember(state) {
        when (state) {
            State.DISABLED -> false
            else -> true
        }
    }

    val handleInteraction: (BrowserToolbarInteraction) -> Unit = { interaction ->
        when (interaction) {
            is BrowserToolbarEvent -> onInteraction(interaction)
            is BrowserToolbarMenu -> {
                when (interaction) {
                    onClick -> currentMenuState = CLick
                    onLongClick -> currentMenuState = LongClick
                    else -> {
                        // no-op. Not possible, just making the compiler happy.
                    }
                }
            }
            is CombinedEventAndMenu -> {
                onInteraction(interaction.event)
                currentMenuState = LongClick
            }
        }
    }

    when (shouldReactToLongClicks) {
        true -> LongPressIconButton(
            onClick = {
                if (onClick != null) {
                    handleInteraction(onClick)
                }
            },
            onLongClick = {
                if (onLongClick != null) {
                    handleInteraction(onLongClick)
                }
            },
            enabled = isEnabled,
            contentDescription = contentDescription,
        ) {
            ActionButtonIcon(icon, tint, highlighted)

            ActionButtonMenu(
                currentMenuState = currentMenuState,
                wantedMenu = LongClick,
                popupData = onLongClick,
                onInteraction = onInteraction,
                onDismissRequest = { currentMenuState = None },
            )
        }

        false -> IconButton(
            onClick = {
                if (onClick != null) {
                    handleInteraction(onClick)
                }
            },
            enabled = isEnabled,
            contentDescription = contentDescription,
        ) {
            ActionButtonIcon(icon, tint, highlighted)

            ActionButtonMenu(
                currentMenuState = currentMenuState,
                wantedMenu = CLick,
                popupData = onClick,
                onInteraction = onInteraction,
                onDismissRequest = { currentMenuState = None },
            )
        }
    }
}

@Composable
private fun ActionButtonIcon(
    icon: Drawable,
    tint: Color,
    isHighlighted: Boolean,
) {
    BadgedIcon(
        modifier = Modifier.padding(8.dp),
        painter = rememberDrawablePainter(icon),
        isHighlighted = isHighlighted,
        tint = Color.Unspecified,
    )
}

@Composable
private inline fun ActionButtonMenu(
    currentMenuState: MenuState,
    wantedMenu: MenuState,
    popupData: BrowserToolbarInteraction?,
    crossinline onInteraction: (BrowserToolbarEvent) -> Unit,
    noinline onDismissRequest: () -> Unit,
) {
    if (currentMenuState == wantedMenu) {
        CustomPlacementPopup(
            isVisible = true,
            onDismissRequest = onDismissRequest,
        ) {
            popupData?.let {
                CustomPlacementPopupVerticalContent {
                    it.toMenuItems().forEach { menuItem ->
                        menuItemComposable(menuItem) { event ->
                            onDismissRequest()
                            onInteraction(event)
                        }.invoke()
                    }
                }
            }
        }
    }
}

private enum class MenuState {
    None, CLick, LongClick
}

@PreviewLightDark
@Composable
private fun ActionButtonPreview() {
    AcornTheme {
        Surface {
            Row {
                ActionButton(
                    icon = AppCompatResources.getDrawable(
                        LocalContext.current,
                        iconsR.drawable.mozac_ic_bookmark_24,
                    )!!,
                    contentDescription = "Test",
                    onClick = object : BrowserToolbarEvent {},
                    onInteraction = {},
                )

                ActionButton(
                    icon = AppCompatResources.getDrawable(
                        LocalContext.current,
                        iconsR.drawable.mozac_ic_bookmark_24,
                    )!!,
                    contentDescription = "Test",
                    state = State.ACTIVE,
                    onClick = object : BrowserToolbarEvent {},
                    onInteraction = {},
                )

                ActionButton(
                    icon = AppCompatResources.getDrawable(
                        LocalContext.current,
                        iconsR.drawable.mozac_ic_bookmark_24,
                    )!!,
                    contentDescription = "Test",
                    state = State.DISABLED,
                    onClick = object : BrowserToolbarEvent {},
                    onInteraction = {},
                )
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun HighlightedActionButtonPreview() {
    AcornTheme {
        Surface {
            ActionButton(
                icon = AppCompatResources.getDrawable(
                    LocalContext.current,
                    iconsR.drawable.mozac_ic_ellipsis_vertical_24,
                )!!,
                contentDescription = "Test",
                onClick = object : BrowserToolbarEvent {},
                highlighted = true,
                onInteraction = {},
            )
        }
    }
}
