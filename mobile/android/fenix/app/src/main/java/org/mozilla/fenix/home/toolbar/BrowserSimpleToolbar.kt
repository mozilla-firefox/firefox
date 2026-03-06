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
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
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
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import mozilla.components.ui.icons.R as iconsR

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
    val browserActionsEnd = uiState.displayState.browserActionsEnd
    val onInteraction: (BrowserToolbarEvent) -> Unit = { store.dispatch(it) }

    val currentWallpaperTextColor = appStore.observeAsComposableState { state ->
        state.wallpaperState.currentWallpaper.textColor
    }
    val defaultWallpaperTextColor = MaterialTheme.colorScheme.onSurface
    val buttonsColor by remember(currentWallpaperTextColor.value) {
        derivedStateOf {
            currentWallpaperTextColor.value?.let { Color(it) } ?: defaultWallpaperTextColor
        }
    }
    val materialColors = MaterialTheme.colorScheme
    val colorScheme = remember(buttonsColor, materialColors) {
        materialColors.copy(
            onSurface = buttonsColor,
        )
    }

    MaterialTheme(colorScheme = colorScheme) {
        Row(
            modifier = Modifier
                .fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Spacer(
                modifier = Modifier
                    .clickable { onInteraction(requireNotNull(pageOrigin.onClick)) }
                    .height(64.dp)
                    .weight(1f),
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
        ),
    )
}

private fun searchEndActions(): List<Action> {
    return listOf(
        ActionButtonRes(
            drawableResId = iconsR.drawable.mozac_ic_microphone_24,
            contentDescription = android.R.string.untitled,
            onClick = object : BrowserToolbarEvent {},
        ),
//        ActionButtonRes(
//            drawableResId = iconsR.drawable.mozac_ic_qr_code_24,
//            contentDescription = android.R.string.untitled,
//            onClick = object : BrowserToolbarEvent {},
//        ),
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
            drawableResId = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
            contentDescription = android.R.string.untitled,
            onClick = BrowserToolbarMenu {
                listOf(
                    BrowserToolbarMenuButton(
                        icon = Icon.DrawableResIcon(iconsR.drawable.mozac_ic_settings_24),
                        text = Text.StringResText(android.R.string.untitled),
                        contentDescription = ContentDescription.StringResContentDescription(
                            android.R.string.untitled,
                        ),
                        onClick = object : BrowserToolbarEvent {},
                    ),
                )
            },
        ),
    )
}

@Composable
private fun SimpleBrowserToolbarPreview(actions: List<Action>, theme: Theme) {
    val store = BrowserToolbarStore(
        initialState = BrowserToolbarState(
            displayState = DisplayState(browserActionsEnd = actions),
        ),
    )
    FirefoxTheme(theme = theme) {
        Surface {
            BrowserSimpleToolbar(store = store, appStore = AppStore())
        }
    }
}

@Preview
@Composable
private fun BrowserSimpleToolbarPreview_Edit(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    SimpleBrowserToolbarPreview(editEndActions(), theme = theme)
}

@Preview
@Composable
private fun BrowserSimpleToolbarPreview_Initial(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    SimpleBrowserToolbarPreview(initialActions(), theme = theme)
}

@Preview
@Composable
private fun BrowserSimpleToolbarPreview_Search(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    SimpleBrowserToolbarPreview(searchEndActions(), theme = theme)
}
