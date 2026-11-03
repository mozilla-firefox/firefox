/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.utils.inComposePreview
import mozilla.components.compose.browser.toolbar.ActionContainer
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.ContentDescription
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Icon
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarMenuItem.BrowserToolbarMenuButton.Text
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.DisplayState
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.wallpapers.WallpaperTheme

/**
 * A simple browser toolbar that displays only the browser end actions configured in [store].
 *
 * @param store The [BrowserToolbarStore] to observe the UI state from.
 * @param appStore The [AppStore] to observe the wallpaper state from.
 */
@Composable
fun BrowserSimpleToolbar(
    store: BrowserToolbarStore,
    appStore: AppStore,
) {
    val uiState by store.observeAsComposableState { it }

    val pageOrigin = uiState.displayState.pageOrigin
    val browserActionsStart = uiState.displayState.browserActionsStart
    val browserActionsEnd = uiState.displayState.browserActionsEnd
    val onInteraction: (BrowserToolbarEvent) -> Unit = { store.dispatch(it) }

    val isPrivateMode = appStore.observeAsComposableState { it.mode.isPrivate }.value
    // Tint the browser action icons with the wallpaper text color. WallpaperTheme.onWallpaper falls
    // back to onSurface for the default wallpaper, in previews, and anywhere LocalWallpaperState is
    // not provided (e.g. tab previews).
    val browserActionsColor =
        if (!inComposePreview && !isPrivateMode) {
            WallpaperTheme.onWallpaper
        } else {
            MaterialTheme.colorScheme.onSurface
        }

    MaterialTheme(colorScheme = MaterialTheme.colorScheme.copy(onSurface = browserActionsColor)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (browserActionsStart.isNotEmpty()) {
                ActionContainer(
                    actions = browserActionsStart,
                    onInteraction = onInteraction,
                )
            }

            Spacer(
                modifier =
                    Modifier.clickable { onInteraction(requireNotNull(pageOrigin.onClick)) }.height(64.dp).weight(1f)
            )

            if (browserActionsEnd.isNotEmpty()) {
                ActionContainer(
                    actions = browserActionsEnd,
                    onInteraction = onInteraction,
                )
            }
        }
    }
}

private fun editEndActions(): List<Action> {
    return listOf(
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_cross_24,
            contentDescription = android.R.string.untitled,
            onClick = object : BrowserToolbarEvent {},
        )
    )
}

private fun searchEndActions(): List<Action> {
    return listOf(
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_microphone_24,
            contentDescription = android.R.string.untitled,
            onClick = object : BrowserToolbarEvent {},
        ),
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_qr_code_24,
            contentDescription = android.R.string.untitled,
            onClick = object : BrowserToolbarEvent {},
        ),
    )
}

private fun initialActions(): List<Action> {
    return listOf(
        TabCounterAction(
            count = 1,
            contentDescription = "Tabs open: 1",
            showPrivacyMask = false,
            onClick = object : BrowserToolbarEvent {},
        ),
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_more_vertical_round_24,
            contentDescription = android.R.string.untitled,
            onClick =
                BrowserToolbarMenu {
                    listOf(
                        BrowserToolbarMenuButton(
                            icon = Icon.DrawableResIcon(iconsR.drawable.mozac_ic_settings_24),
                            text = Text.StringResText(android.R.string.untitled),
                            contentDescription =
                                ContentDescription.StringResContentDescription(android.R.string.untitled),
                            onClick = object : BrowserToolbarEvent {},
                        )
                    )
                },
        ),
    )
}

@Composable
private fun SimpleBrowserToolbarPreview(actions: List<Action>, theme: Theme) {
    val store =
        BrowserToolbarStore(
            initialState = BrowserToolbarState(displayState = DisplayState(browserActionsEnd = actions))
        )
    FirefoxTheme(theme = theme) {
        Surface {
            BrowserSimpleToolbar(store = store, appStore = AppStore())
        }
    }
}

@Preview
@Composable
private fun BrowserSimpleToolbarPreview_Edit(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    SimpleBrowserToolbarPreview(editEndActions(), theme = theme)
}

@Preview
@Composable
private fun BrowserSimpleToolbarPreview_Initial(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    SimpleBrowserToolbarPreview(initialActions(), theme = theme)
}

@Preview
@Composable
private fun BrowserSimpleToolbarPreview_Search(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    SimpleBrowserToolbarPreview(searchEndActions(), theme = theme)
}
