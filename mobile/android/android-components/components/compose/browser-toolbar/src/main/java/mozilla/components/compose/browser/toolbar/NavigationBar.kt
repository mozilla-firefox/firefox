/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.compose.base.theme.privateColorPalette
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Top
import mozilla.components.browser.menu2.R as menu2R
import mozilla.components.ui.icons.R as iconsR

/**
 * Top-level UI for displaying the navigation bar.
 *
 * @param actions List of browser [Action]s to be displayed in the navigation bar.
 * @param toolbarGravity [ToolbarGravity] for where the toolbar is being placed on the screen.
 * @param onInteraction Callback invoked with a [BrowserToolbarEvent] whenever the user interacts
 * with any action in the navigation bar.
 */
@Composable
fun NavigationBar(
    actions: List<Action> = emptyList(),
    toolbarGravity: ToolbarGravity = Top,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    Surface {
        Box(
            modifier = Modifier
                .height(if (toolbarGravity == Top) 160.dp else 248.dp)
                .pointerInput(Unit) {
                    awaitPointerEventScope {
                        while (true) {
                            awaitPointerEvent() // Consume all events
                        }
                    }
                }
                .semantics(mergeDescendants = true) {}
                .fillMaxWidth(),
        ) {
            ActionContainer(
                actions = actions,
                onInteraction = onInteraction,
                modifier = Modifier
                    .padding(horizontal = 12.dp)
                    .fillMaxWidth()
                    .align(Alignment.Center),
                horizontalArrangement = Arrangement.SpaceBetween,
            )

            if (toolbarGravity == Top) {
                HorizontalDivider(
                    modifier = Modifier.align(Alignment.TopCenter),
                )
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun NavigationBarPreview() {
    AcornTheme {
        NavigationBar(
            actions = listOf(
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_bookmark_fill_24,
                    contentDescription = android.R.string.untitled,
                    state = ActionButton.State.ACTIVE,
                    onClick = object : BrowserToolbarEvent {},
                ),
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_share_android_24,
                    contentDescription = android.R.string.untitled,
                    onClick = object : BrowserToolbarEvent {},
                ),
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_plus_24,
                    contentDescription = android.R.string.untitled,
                    onClick = object : BrowserToolbarEvent {},
                ),
                TabCounterAction(
                    count = 99,
                    contentDescription = "",
                    showPrivacyMask = false,
                    onClick = object : BrowserToolbarEvent {},
                    onLongClick = object : BrowserToolbarEvent {},
                ),
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
                    contentDescription = menu2R.string.mozac_browser_menu2_button,
                    onClick = object : BrowserToolbarEvent {},
                ),
            ),
            toolbarGravity = Top,
        ) {}
    }
}

@Preview
@Composable
private fun NavigationBarPrivatePreview() {
    AcornTheme(
        colors = privateColorPalette,
        colorScheme = acornPrivateColorScheme(),
    ) {
        NavigationBar(
            actions = listOf(
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_bookmark_fill_24,
                    contentDescription = android.R.string.untitled,
                    state = ActionButton.State.ACTIVE,
                    onClick = object : BrowserToolbarEvent {},
                ),
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_share_android_24,
                    contentDescription = android.R.string.untitled,
                    onClick = object : BrowserToolbarEvent {},
                ),
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_plus_24,
                    contentDescription = android.R.string.untitled,
                    onClick = object : BrowserToolbarEvent {},
                ),
                TabCounterAction(
                    count = 99,
                    contentDescription = "",
                    showPrivacyMask = false,
                    onClick = object : BrowserToolbarEvent {},
                    onLongClick = object : BrowserToolbarEvent {},
                ),
                ActionButtonRes(
                    drawableResId = iconsR.drawable.mozac_ic_ellipsis_vertical_24,
                    contentDescription = menu2R.string.mozac_browser_menu2_button,
                    onClick = object : BrowserToolbarEvent {},
                ),
            ),
            toolbarGravity = Top,
        ) {}
    }
}
