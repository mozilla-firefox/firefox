/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.progressbar.AnimatedProgressBar
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.compose.browser.toolbar.store.ProgressBarGravity.Bottom
import mozilla.components.compose.browser.toolbar.store.ProgressBarGravity.Top
import mozilla.components.compose.browser.toolbar.ui.Origin

private val ROUNDED_CORNER_SHAPE = RoundedCornerShape(8.dp)

/**
 * Sub-component of the [BrowserToolbar] responsible for displaying the URL and related
 * controls ("display mode").
 *
 * @param pageOrigin Details about the website origin.
 * @param colors The color scheme to use in the browser display toolbar.
 * @param progressBarConfig [ProgressBarConfig] configuration for the progress bar.
 * If `null` a progress bar will not be displayed.
 * @param browserActionsStart List of browser [Action]s to be displayed at the start of the
 * toolbar, outside of the URL bounding box.
 * These should be actions relevant to the browser as a whole.
 * See [MDN docs](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction).
 * @param pageActionsStart List of navigation [Action]s to be displayed between [browserActionsStart]
 * and [pageOrigin], inside of the URL bounding box.
 * These should be actions relevant to specific webpages as opposed to [browserActionsStart].
 * See [MDN docs](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/pageAction).
 * @param pageActionsEnd List of page [Action]s to be displayed between [pageOrigin] and [browserActionsEnd],
 * inside of the URL bounding box.
 * These should be actions relevant to specific webpages as opposed to [browserActionsStart].
 * See [MDN docs](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/pageAction).
 * @param browserActionsEnd List of browser [Action]s to be displayed at the end of the toolbar,
 * outside of the URL bounding box.
 * These should be actions relevant to the browser as a whole.
 * See [MDN docs](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/browserAction).
 * @param onInteraction Callback for handling [BrowserToolbarEvent]s on user interactions.
 */
@Composable
fun BrowserDisplayToolbar(
    pageOrigin: PageOrigin,
    colors: BrowserDisplayToolbarColors,
    progressBarConfig: ProgressBarConfig?,
    browserActionsStart: List<Action> = emptyList(),
    pageActionsStart: List<Action> = emptyList(),
    pageActionsEnd: List<Action> = emptyList(),
    browserActionsEnd: List<Action> = emptyList(),
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    Box(
        modifier = Modifier
            .background(color = colors.background)
            .fillMaxWidth(),
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (browserActionsStart.isNotEmpty()) {
                ActionContainer(
                    actions = browserActionsStart,
                    onInteraction = onInteraction,
                )
            }

            Row(
                modifier = Modifier
                    .padding(vertical = 8.dp)
                    .background(
                        color = colors.urlBackground,
                        shape = ROUNDED_CORNER_SHAPE,
                    )
                    .weight(1f),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                if (pageActionsStart.isNotEmpty()) {
                    ActionContainer(
                        actions = pageActionsStart,
                        onInteraction = onInteraction,
                    )
                }

                Origin(
                    hint = pageOrigin.hint,
                    modifier = Modifier
                        .height(48.dp)
                        .weight(1f),
                    url = pageOrigin.url,
                    title = pageOrigin.title,
                    onClick = pageOrigin.onClick,
                    onLongClick = pageOrigin.onLongClick,
                    onInteraction = onInteraction,
                    fadeDirection = pageOrigin.fadeDirection,
                    textGravity = pageOrigin.textGravity,
                )

                if (pageActionsEnd.isNotEmpty()) {
                    ActionContainer(
                        actions = pageActionsEnd,
                        onInteraction = onInteraction,
                    )
                }
            }

            if (browserActionsEnd.isNotEmpty()) {
                ActionContainer(
                    actions = browserActionsEnd,
                    onInteraction = onInteraction,
                )
            }
        }

        if (progressBarConfig != null) {
            AnimatedProgressBar(
                progress = progressBarConfig.progress,
                color = progressBarConfig.color,
                modifier = when (progressBarConfig.gravity) {
                    Top -> Modifier.align(Alignment.TopCenter)
                    Bottom -> Modifier.align(Alignment.BottomCenter)
                }.fillMaxWidth(),
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun BrowserDisplayToolbarPreview() {
    AcornTheme {
        BrowserDisplayToolbar(
            pageOrigin = PageOrigin(
                hint = R.string.mozac_browser_toolbar_search_hint,
                title = null,
                url = null,
                onClick = object : BrowserToolbarEvent {},
            ),
            colors = BrowserDisplayToolbarColors(
                background = AcornTheme.colors.layer1,
                urlBackground = AcornTheme.colors.layer3,
                text = AcornTheme.colors.textPrimary,
            ),
            progressBarConfig = ProgressBarConfig(
                progress = 66,
                gravity = Top,
            ),
            browserActionsStart = listOf(
                Action.ActionButton(
                    icon = mozilla.components.ui.icons.R.drawable.mozac_ic_home_24,
                    contentDescription = android.R.string.untitled,
                    tint = AcornTheme.colors.iconPrimary.toArgb(),
                    onClick = object : BrowserToolbarEvent {},
                ),
            ),
            pageActionsEnd = listOf(
                Action.ActionButton(
                    icon = mozilla.components.ui.icons.R.drawable.mozac_ic_arrow_clockwise_24,
                    contentDescription = android.R.string.untitled,
                    tint = AcornTheme.colors.iconPrimary.toArgb(),
                    onClick = object : BrowserToolbarEvent {},
                ),
            ),
            browserActionsEnd = listOf(
                Action.ActionButton(
                    icon = mozilla.components.ui.icons.R.drawable.mozac_ic_ellipsis_vertical_24,
                    contentDescription = android.R.string.untitled,
                    tint = AcornTheme.colors.iconPrimary.toArgb(),
                    onClick = object : BrowserToolbarEvent {},
                ),
            ),
            onInteraction = {},
        )
    }
}
