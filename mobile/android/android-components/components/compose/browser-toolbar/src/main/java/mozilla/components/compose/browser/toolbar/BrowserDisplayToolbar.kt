/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar

import androidx.compose.animation.ExperimentalSharedTransitionApi
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.DividerDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.compose.base.theme.privateColorPalette
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity
import mozilla.components.compose.browser.toolbar.ui.FullDisplayToolbar
import mozilla.components.compose.browser.toolbar.utils.DisplayToolbarDataProvider
import mozilla.components.compose.browser.toolbar.utils.DisplayToolbarPreviewModel
import mozilla.components.support.base.log.logger.Logger
import androidx.compose.ui.platform.LocalResources

/**
 * Sub-component of the [BrowserToolbar] responsible for displaying the URL and related
 * controls ("display mode").
 *
 * @param pageOrigin Details about the website origin.
 * @param gravity [ToolbarGravity] for where the toolbar is being placed on the screen.
 * @param progressBarConfig [ProgressBarConfig] configuration for the progress bar.
 * If `null` a progress bar will not be displayed.
 * @param backgroundColor Color of the background.
 * @param outlineColor Color of the divider.
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
@OptIn(ExperimentalSharedTransitionApi::class)
@Composable
@Suppress("LongMethod")
fun BrowserDisplayToolbar(
    pageOrigin: PageOrigin,
    gravity: ToolbarGravity,
    progressBarConfig: ProgressBarConfig?,
    backgroundColor: Color = MaterialTheme.colorScheme.surface,
    outlineColor: Color = DividerDefaults.color,
    browserActionsStart: List<Action> = emptyList(),
    pageActionsStart: List<Action> = emptyList(),
    pageActionsEnd: List<Action> = emptyList(),
    browserActionsEnd: List<Action> = emptyList(),
    onInteraction: (BrowserToolbarEvent) -> Unit,
    useMinimalBottomToolbarWhenEnteringText: Boolean = false,
) {
    LaunchedEffect(useMinimalBottomToolbarWhenEnteringText) {
        if (useMinimalBottomToolbarWhenEnteringText) {
            Logger("BrowserDisplayToolbar").warn("useMinimalBottomToolbarWhenEnteringText is not supported")
        }
    }

    FullDisplayToolbar(
        pageOrigin = pageOrigin,
        gravity = gravity,
        progressBarConfig = progressBarConfig,
        browserActionsStart = browserActionsStart,
        pageActionsStart = pageActionsStart,
        pageActionsEnd = pageActionsEnd,
        browserActionsEnd = browserActionsEnd,
        onInteraction = onInteraction,
        modifier = Modifier.fillMaxWidth(),
        backgroundColor = backgroundColor,
        outlineColor = outlineColor,
        browserActionsStartModifier = Modifier,
        pageActionsStartModifier = Modifier,
        originModifier = Modifier,
        pageActionsEndModifier = Modifier,
        browserActionsEndModifier = Modifier,
    )
}

@PreviewLightDark
@Composable
private fun BrowserDisplayToolbarPreview(
    @PreviewParameter(DisplayToolbarDataProvider::class) config: DisplayToolbarPreviewModel,
) {
    AcornTheme {
        BrowserDisplayToolbar(
            gravity = config.gravity,
            progressBarConfig = ProgressBarConfig(progress = 66),
            browserActionsStart = config.browserStartActions,
            pageActionsStart = config.pageActionsStart,
            pageOrigin = PageOrigin(
                hint = R.string.mozac_browser_toolbar_search_hint,
                title = config.title,
                url = config.url,
                onClick = object : BrowserToolbarEvent {},
            ),
            pageActionsEnd = config.pageActionsEnd,
            browserActionsEnd = config.browserEndActions,
            onInteraction = {},
        )
    }
}

@Preview
@Composable
private fun BrowserDisplayToolbarPrivatePreview(
    @PreviewParameter(DisplayToolbarDataProvider::class) config: DisplayToolbarPreviewModel,
) {
    AcornTheme(
        colors = privateColorPalette,
        colorScheme = acornPrivateColorScheme(),
    ) {
        BrowserDisplayToolbar(
            gravity = config.gravity,
            progressBarConfig = ProgressBarConfig(progress = 66),
            browserActionsStart = config.browserStartActions,
            pageActionsStart = config.pageActionsStart,
            pageOrigin = PageOrigin(
                hint = R.string.mozac_browser_toolbar_search_hint,
                title = config.title,
                url = config.url,
                onClick = object : BrowserToolbarEvent {},
            ),
            pageActionsEnd = config.pageActionsEnd,
            browserActionsEnd = config.browserEndActions,
            onInteraction = {},
        )
    }
}
