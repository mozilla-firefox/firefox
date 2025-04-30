/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.content.res.Configuration.UI_MODE_NIGHT_NO
import android.view.View
import android.view.ViewGroup
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import mozilla.components.browser.menu2.BrowserMenuController
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.button.LongPressIconButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu
import mozilla.components.support.ktx.android.util.dpToPx
import mozilla.components.ui.tabcounter.TabCounter

// Interim composable for a tab counter button that supports showing a menu on long press.
// With this being implemented as an AndroidView the menu can be shown as low to the bottom of the screen as needed.
// To be replaced with a fully Compose implementation in the future that use a DropdownMenu once
// https://github.com/JetBrains/compose-multiplatform/issues/1878 is resolved.

private const val BUTTON_DIMENSIONS_DP = 48

/**
 * Composable that delegates to an AndroidView to display a tab counter button and optionally a menu.
 *
 * @param count The number of tabs to display in the tab counter.
 * @param showPrivacyMask Whether ot not to decorate this button with a privacy mask top-right image.
 * @param onClick [BrowserToolbarInteraction] describing how to handle this button being clicked.
 * @param onLongClick Optional [BrowserToolbarInteraction] describing how to handle this button being long clicked.
 * @param onInteraction Callback for handling [BrowserToolbarEvent]s on user interactions.
 */
@Composable
fun TabCounter(
    count: Int,
    showPrivacyMask: Boolean,
    onClick: BrowserToolbarEvent,
    onLongClick: BrowserToolbarInteraction?,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    val onLongClickMenu = key(onLongClick) { onLongClick.buildMenu(onInteraction) }
    val shouldReactToLongClicks = remember(onLongClick) {
        mutableStateOf(
            onLongClick is BrowserToolbarEvent ||
                onLongClick is CombinedEventAndMenu ||
                (onLongClick is BrowserToolbarMenu && onLongClickMenu != null),
        )
    }
    var showMenu by remember { mutableStateOf(false) }

    // Wrapping the TabCounterView in our button composables to ensure the proper ripple effect.
    when (shouldReactToLongClicks.value) {
        true -> LongPressIconButton(
            onClick = { onInteraction(onClick) },
            onLongClick = {
                when (onLongClick) {
                    is BrowserToolbarEvent -> onInteraction(onLongClick)
                    is BrowserToolbarMenu -> showMenu = true
                    is CombinedEventAndMenu -> {
                        onInteraction(onLongClick.event)
                        showMenu = true
                    }
                    null -> {
                        // no-op. This case is not possible. Just making the compiler happy.
                    }
                }
            },
            contentDescription = "", // Set internally by the TabCounter View for every count change.
        ) {
            TabCounterView(
                count = count,
                showPrivacyMask = showPrivacyMask,
                menuController = onLongClickMenu,
                showMenu = showMenu,
                onMenuShown = { showMenu = false },
            )
        }

        false -> IconButton(
            onClick = { onInteraction(onClick) },
            contentDescription = "", // Set internally by the TabCounter View for every count change.
        ) {
            TabCounterView(
                count = count,
                showPrivacyMask = showPrivacyMask,
            )
        }
    }
}

@Composable
private fun TabCounterView(
    count: Int,
    showPrivacyMask: Boolean,
    menuController: BrowserMenuController? = null,
    showMenu: Boolean = false,
    onMenuShown: () -> Unit = {},
) {
    val context = LocalContext.current
    val isRtl = LocalLayoutDirection.current == LayoutDirection.Rtl

    AndroidView(
        factory = { _ ->
            TabCounter(context).apply {
                val minimumSize = BUTTON_DIMENSIONS_DP.dpToPx(context.resources.displayMetrics)
                layoutParams = ViewGroup.LayoutParams(minimumSize, minimumSize)
            }
        },
        update = { tabCounter ->
            tabCounter.setCount(count)
            tabCounter.toggleCounterMask(showPrivacyMask)
            tabCounter.layoutDirection = if (isRtl) {
                View.TEXT_DIRECTION_RTL
            } else {
                View.TEXT_DIRECTION_LTR
            }
            if (showMenu && menuController != null) {
                menuController.show(anchor = tabCounter)
                onMenuShown()
            }
        },
    )
}

@Preview(uiMode = UI_MODE_NIGHT_NO)
@Composable
private fun TabCounterPreview() {
    AcornTheme {
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Box(modifier = Modifier.background(AcornTheme.colors.layer1)) {
                TabCounter(
                    count = 3,
                    showPrivacyMask = true,
                    onClick = object : BrowserToolbarEvent {},
                    onLongClick = null,
                    onInteraction = {},
                )
            }

            Box(modifier = Modifier.background(AcornTheme.colors.layer1)) {
                TabCounter(
                    count = 234,
                    showPrivacyMask = false,
                    onClick = object : BrowserToolbarEvent {},
                    onLongClick = null,
                    onInteraction = {},
                )
            }
        }
    }
}
