/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DividerDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import androidx.window.core.layout.WindowSizeClass
import mozilla.components.compose.base.progressbar.AnimatedProgressBar
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.compose.base.theme.privateColorPalette
import mozilla.components.compose.browser.toolbar.ActionContainer
import mozilla.components.compose.browser.toolbar.R
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_PROGRESSBAR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL_BOX
import mozilla.components.compose.browser.toolbar.concept.PageOrigin
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.ProgressBarConfig
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Bottom
import mozilla.components.compose.browser.toolbar.store.ToolbarGravity.Top
import mozilla.components.compose.browser.toolbar.utils.DisplayToolbarDataProvider
import mozilla.components.compose.browser.toolbar.utils.DisplayToolbarPreviewModel
import androidx.compose.ui.platform.LocalResources

private const val NO_TOOLBAR_PADDING_DP = 0
private const val TOOLBAR_PADDING_DP = 8
private const val LARGE_TOOLBAR_PADDING_DP = 20

@Suppress("LongMethod", "LongParameterList", "CyclomaticComplexMethod", "CognitiveComplexMethod")
@Composable
internal fun FullDisplayToolbar(
    pageOrigin: PageOrigin,
    gravity: ToolbarGravity,
    progressBarConfig: ProgressBarConfig?,
    browserActionsStart: List<Action>,
    pageActionsStart: List<Action>,
    pageActionsEnd: List<Action>,
    browserActionsEnd: List<Action>,
    onInteraction: (BrowserToolbarEvent) -> Unit,
    modifier: Modifier = Modifier,
    backgroundColor: Color = AcornTheme.colors.layer3,
    outlineColor: Color = DividerDefaults.color,
    browserActionsStartModifier: Modifier = Modifier,
    pageActionsStartModifier: Modifier = Modifier,
    originModifier: Modifier = Modifier,
    pageActionsEndModifier: Modifier = Modifier,
    browserActionsEndModifier: Modifier = Modifier,
) {
    val windowSizeClass = currentWindowAdaptiveInfo().windowSizeClass
    val isSmallWidthScreen = remember(windowSizeClass) {
        windowSizeClass.minWidthDp < WindowSizeClass.WIDTH_DP_MEDIUM_LOWER_BOUND
    }

    Surface(color = AcornTheme.colors.layer3) {
        Box(
            modifier = modifier
                .semantics { testTagsAsResourceId = true },
        ) {
            Row(
                modifier = Modifier.padding(start = 12.dp, end = 12.dp, bottom =
                    LocalResources.current.getDimensionPixelSize(com.android.car.navioui.R.dimen.padding_M).dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                if (browserActionsStart.isNotEmpty()) {
                    ActionContainer(
                        actions = browserActionsStart,
                        onInteraction = onInteraction,
                        modifier = browserActionsStartModifier,
                    )
                }

                Row(
                    modifier = Modifier
                        .padding(
                            start = when (browserActionsStart.isEmpty()) {
                                true -> TOOLBAR_PADDING_DP.dp
                                false -> NO_TOOLBAR_PADDING_DP.dp
                            },
                            top = TOOLBAR_PADDING_DP.dp,
                            end = when (browserActionsEnd.isEmpty()) {
                                true -> TOOLBAR_PADDING_DP.dp
                                false -> NO_TOOLBAR_PADDING_DP.dp
                            },
                        )
                        .height(72.dp)
                        .background(
                            color = AcornTheme.colors.controlsInputFocusFill,
                            shape = RoundedCornerShape(999.dp),
                        )
                        .padding(
                            start = when (pageActionsStart.isEmpty()) {
                                true -> TOOLBAR_PADDING_DP.dp
                                false -> NO_TOOLBAR_PADDING_DP.dp
                            },
                            top = NO_TOOLBAR_PADDING_DP.dp,
                            end = when (pageActionsEnd.isEmpty()) {
                                true -> TOOLBAR_PADDING_DP.dp
                                false -> NO_TOOLBAR_PADDING_DP.dp
                            },
                            bottom = NO_TOOLBAR_PADDING_DP.dp,
                        )
                        .weight(1f),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    if (pageActionsStart.isNotEmpty()) {
                        ActionContainer(
                            actions = pageActionsStart,
                            onInteraction = onInteraction,
                            modifier = pageActionsStartModifier.padding(horizontal =
                                LocalResources.current.getDimensionPixelSize(
                                    com.android.car.navioui.R.dimen.padding_M).dp),
                        )
                    }

                    Origin(
                        hint = pageOrigin.hint,
                        modifier = Modifier
                            .height(56.dp)
                            .weight(1f)
                            .testTag(ADDRESSBAR_URL_BOX)
                            .then(originModifier),
                        url = pageOrigin.url,
                        title = pageOrigin.title,
                        textGravity = pageOrigin.textGravity,
                        contextualMenuOptions = pageOrigin.contextualMenuOptions,
                        onClick = pageOrigin.onClick,
                        onLongClick = pageOrigin.onLongClick,
                        onInteraction = onInteraction,
                    )

                    if (pageActionsEnd.isNotEmpty()) {
                        ActionContainer(
                            actions = pageActionsEnd,
                            onInteraction = onInteraction,
                            modifier = pageActionsEndModifier,
                        )
                    }
                }

                if (browserActionsEnd.isNotEmpty()) {
                    ActionContainer(
                        actions = browserActionsEnd,
                        onInteraction = onInteraction,
                        modifier = browserActionsEndModifier,
                    )
                }
            }

            HorizontalDivider(
                modifier = Modifier.align(
                    when (gravity) {
                        Top -> Alignment.BottomCenter
                        Bottom -> Alignment.TopCenter
                    },
                ),
                color = outlineColor,
            )

            if (progressBarConfig != null) {
                AnimatedProgressBar(
                    progress = progressBarConfig.progress,
                    color = progressBarConfig.color,
                    trackColor = Color.Transparent,
                    modifier = Modifier
                        .semantics {
                            testTag = ADDRESSBAR_PROGRESSBAR
                        }
                        .align(
                        when (gravity) {
                            Top -> Alignment.BottomCenter
                            Bottom -> Alignment.TopCenter
                        },
                    ),
                )
            }
        }
    }
}

@PreviewLightDark
@Composable
private fun FullDisplayToolbarPreview(
    @PreviewParameter(DisplayToolbarDataProvider::class) config: DisplayToolbarPreviewModel,
) {
    AcornTheme {
        FullDisplayToolbar(
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
private fun FullDisplayToolbarPrivatePreview(
    @PreviewParameter(DisplayToolbarDataProvider::class) config: DisplayToolbarPreviewModel,
) {
    AcornTheme(
        colors = privateColorPalette,
        colorScheme = acornPrivateColorScheme(),
    ) {
        FullDisplayToolbar(
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
