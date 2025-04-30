/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.Stable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.core.content.ContextCompat
import mozilla.components.browser.menu2.BrowserMenuController
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuDivider
import mozilla.components.concept.menu.MenuStyle
import mozilla.components.concept.menu.candidate.DecorativeTextMenuCandidate
import mozilla.components.concept.menu.candidate.DividerMenuCandidate
import mozilla.components.concept.menu.candidate.DrawableMenuIcon
import mozilla.components.concept.menu.candidate.MenuCandidate
import mozilla.components.concept.menu.candidate.TextMenuCandidate

/**
 * Builds a [BrowserMenuController] from a [BrowserToolbarInteraction].
 * Will return `null` if `this` is not a [BrowserToolbarMenu] or contains no menu items configurations.
 *
 * @param onInteraction Callback for handling [BrowserToolbarEvent]s on user interactions with the menu items.
 */
@Stable
@ReadOnlyComposable
@Composable
internal fun BrowserToolbarInteraction?.buildMenu(
    onInteraction: (BrowserToolbarEvent) -> Unit,
) = when (this) {
    is BrowserToolbarMenu -> buildMenuController(toMenuItems(onInteraction))
    is CombinedEventAndMenu -> buildMenuController(menu.toMenuItems(onInteraction))
    else -> null
}

@Stable
@ReadOnlyComposable
@Composable
private fun BrowserToolbarMenu.toMenuItems(
    onInteraction: (BrowserToolbarEvent) -> Unit,
) = items().mapNotNull {
    when (it) {
        is BrowserToolbarMenuButton -> {
            if (it.icon == null && it.iconResource == null && it.text != null) {
                DecorativeTextMenuCandidate(
                    text = stringResource(it.text),
                )
            } else if ((it.icon != null || it.iconResource != null) && it.text != null) {
                TextMenuCandidate(
                    text = stringResource(it.text),
                    start = it.toDrawableMenuIcon(LocalContext.current),
                    onClick = {
                        it.onClick?.let { onInteraction(it) }
                    },
                )
            } else {
                null
            }
        }
        is BrowserToolbarMenuDivider -> DividerMenuCandidate()
    }
}

private fun buildMenuController(menuItems: List<MenuCandidate>) = when (menuItems.isNotEmpty()) {
    true -> BrowserMenuController(
        style = MenuStyle(
            completelyOverlap = true,
        ),
    ).apply {
        submitList(menuItems)
    }

    false -> null
}

private fun BrowserToolbarMenuButton.toDrawableMenuIcon(context: Context) = DrawableMenuIcon(
    drawable = icon ?: iconResource?.let { ContextCompat.getDrawable(context, it) },
)
