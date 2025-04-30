/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.widget.ImageView
import androidx.annotation.ColorInt
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.viewinterop.AndroidView
import mozilla.components.browser.menu2.BrowserMenuController
import mozilla.components.compose.base.annotation.LightDarkPreview
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.LongPressIconButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.ui.icons.R

// Interim composable for a tab counter button that supports showing a menu on long press.
// With this being implemented as an AndroidView the menu can be shown as low to the bottom of the screen as needed.
// To be replaced with a fully Compose implementation in the future that use a DropdownMenu once
// https://github.com/JetBrains/compose-multiplatform/issues/1878 is resolved.

/**
 * A clickable icon used to represent an action that can be added to the toolbar.
 *
 * @param icon Drawable resource for this button.
 * @param tint Color resource used to tint [icon].
 * @param contentDescription Text used by accessibility services to describe what this button does.
 * @property onClick [BrowserToolbarInteraction] describing how to handle this button being clicked.
 * @param onLongClick Optional [BrowserToolbarInteraction] describing how to handle this button being long clicked.
 * @param onInteraction Callback for handling [BrowserToolbarEvent]s on user interactions.
 */
@Composable
@Suppress("CyclomaticComplexMethod")
fun ActionButton(
    @DrawableRes icon: Int,
    @ColorInt tint: Int,
    @StringRes contentDescription: Int,
    onClick: BrowserToolbarInteraction,
    onLongClick: BrowserToolbarInteraction? = null,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    val onClickMenu = key(onClick) { onClick.buildMenu(onInteraction) }
    val onLongClickMenu = key(onLongClick) { onLongClick.buildMenu(onInteraction) }
    val shouldReactToLongClicks = remember(onLongClickMenu) {
        mutableStateOf(
            onLongClick is BrowserToolbarEvent ||
                onLongClick is CombinedEventAndMenu ||
                (onLongClick is BrowserToolbarMenu && onLongClickMenu != null),
        )
    }
    var currentMenuState by remember { mutableStateOf(MenuState.None) }

    val handleInteraction: (BrowserToolbarInteraction) -> Unit = { interaction ->
        when (interaction) {
            is BrowserToolbarEvent -> onInteraction(interaction)
            is BrowserToolbarMenu -> {
                when (interaction) {
                    onClick -> currentMenuState = MenuState.CLick
                    onLongClick -> currentMenuState = MenuState.LongClick
                    else -> {
                        // no-op. Not possible, just making the compiler happy.
                    }
                }
            }
            is CombinedEventAndMenu -> {
                onInteraction(interaction.event)
                currentMenuState = MenuState.LongClick
            }
        }
    }

    val content: @Composable () -> Unit = {
        ActionButtonView(
            icon = icon,
            tint = tint,
            menuData = when (currentMenuState) {
                MenuState.None -> null
                MenuState.CLick -> {
                    onClickMenu?.let { menuController ->
                        MenuData(
                            menuController = menuController,
                            menuState = currentMenuState,
                            onMenuShown = { currentMenuState = MenuState.None },
                        )
                    }
                }
                MenuState.LongClick -> {
                    onLongClickMenu?.let { menuController ->
                        MenuData(
                            menuController = menuController,
                            menuState = currentMenuState,
                            onMenuShown = { currentMenuState = MenuState.None },
                        )
                    }
                }
            },
        )
    }

    when (shouldReactToLongClicks.value) {
        true -> LongPressIconButton(
            onClick = { handleInteraction(onClick) },
            onLongClick = {
                if (onLongClick != null) {
                    handleInteraction(onLongClick)
                }
            },
            contentDescription = stringResource(contentDescription),
            content = content,
        )

        false -> IconButton(
            onClick = { handleInteraction(onClick) },
            contentDescription = stringResource(contentDescription),
            content = content,
        )
    }
}

private data class MenuData(
    val menuController: BrowserMenuController?,
    val menuState: MenuState,
    val onMenuShown: () -> Unit,
)

private enum class MenuState {
    None, CLick, LongClick
}

@Composable
private fun ActionButtonView(
    @DrawableRes icon: Int,
    @ColorInt tint: Int,
    menuData: MenuData? = null,
) {
    AndroidView(
        factory = { context ->
            ImageView(context)
        },
        update = { imageView ->
            imageView.setImageResource(icon)
            imageView.setColorFilter(tint)

            if (menuData?.menuState == MenuState.CLick || menuData?.menuState == MenuState.LongClick) {
                menuData.menuController?.show(anchor = imageView)
                menuData.onMenuShown()
            }
        },
    )
}

@LightDarkPreview
@Composable
private fun ActionButtonPreview() {
    AcornTheme {
        Box(modifier = Modifier.background(AcornTheme.colors.layer1)) {
            ActionButton(
                icon = R.drawable.mozac_ic_web_extension_default_icon,
                contentDescription = R.string.mozac_error_confused,
                tint = AcornTheme.colors.iconPrimary.toArgb(),
                onClick = object : BrowserToolbarEvent {},
                onInteraction = {},
            )
        }
    }
}
