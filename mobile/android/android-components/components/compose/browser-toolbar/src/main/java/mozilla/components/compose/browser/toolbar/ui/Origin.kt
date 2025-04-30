/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.content.res.Configuration.UI_MODE_NIGHT_YES
import android.content.res.Configuration.UI_MODE_TYPE_NORMAL
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.widget.LinearLayout
import androidx.annotation.StringRes
import androidx.appcompat.widget.AppCompatTextView
import androidx.compose.foundation.background
import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.isVisible
import mozilla.components.browser.menu2.BrowserMenuController
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.R
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.FadeDirection
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.FadeDirection.FADE_DIRECTION_END
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.FadeDirection.FADE_DIRECTION_START
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.TextGravity
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.TextGravity.TEXT_GRAVITY_END
import mozilla.components.compose.browser.toolbar.concept.PageOrigin.Companion.TextGravity.TEXT_GRAVITY_START
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarMenu
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.CombinedEventAndMenu

private const val URL_TEXT_SIZE_ALONE = 15
private const val URL_TEXT_SIZE_WITH_TITLE = 12

/**
 * Custom layout for showing the origin - title and url of a webpage.
 *
 * @param hint [StringRes] the string to show when the URL is not available
 * @param modifier [Modifier] to apply to this composable for further customisation.
 * @param url the URL of the webpage. Can be `null` or empty, in which vase the [hint] will be shown.
 * @param title the title of the webpage. Can be `null` or empty, in which case only [url] or [hint] will be shown.
 * @param onClick [BrowserToolbarEvent] to be dispatched when this layout is clicked.
 * @param onLongClick Optional [BrowserToolbarInteraction] describing how to handle this layout being long clicked.
 * @param onInteraction [BrowserToolbarInteraction] to be dispatched when this layout is interacted with.
 * @param onInteraction Callback for handling [BrowserToolbarEvent]s on user interactions.
 * @param fadeDirection [FadeDirection] How the displayed text should be faded.
 * @param textGravity [TextGravity] How the displayed text should be aligned.
 */
@Composable
internal fun Origin(
    @StringRes hint: Int,
    modifier: Modifier = Modifier,
    url: String? = null,
    title: String? = null,
    onClick: BrowserToolbarEvent,
    onLongClick: BrowserToolbarInteraction?,
    onInteraction: (BrowserToolbarEvent) -> Unit,
    fadeDirection: FadeDirection,
    textGravity: TextGravity,
) {
    OriginView(
        hint = stringResource(hint),
        url = url,
        title = title,
        fadeDirection = fadeDirection,
        textGravity = textGravity,
        onClick = onClick,
        onLongClick = onLongClick,
        onInteraction = onInteraction,
        modifier = modifier,
    )
}

@Composable
@Suppress("LongMethod", "LongParameterList")
private fun OriginView(
    hint: String,
    url: String?,
    title: String?,
    fadeDirection: FadeDirection,
    textGravity: TextGravity,
    onClick: BrowserToolbarEvent,
    onLongClick: BrowserToolbarInteraction?,
    onInteraction: (BrowserToolbarEvent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val shouldShowTitle = remember { title != null && title.isNotEmpty() }
    val urlTextSize = remember(shouldShowTitle) {
        when (shouldShowTitle) {
            true -> URL_TEXT_SIZE_WITH_TITLE.sp
            false -> URL_TEXT_SIZE_ALONE.sp
        }
    }
    val urlGravity = remember(shouldShowTitle) {
        when (shouldShowTitle) {
            true -> Gravity.TOP or Gravity.LEFT
            false -> Gravity.CENTER_VERTICAL or Gravity.LEFT
        }
    }
    val urlToShow = remember(url) {
        when (url == null || url.isBlank()) {
            true -> hint
            else -> url
        }
    }
    val longClickMenu = key(onLongClick) { onLongClick.buildMenu(onInteraction) }
    val textColor = AcornTheme.colors.textPrimary

    AndroidView(
        modifier = modifier,
        factory = { context ->
            LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER_VERTICAL
                isClickable = true
                isFocusable = true
                addView(
                    CustomFadeAndGravityTextView(context, fadeDirection, textGravity).apply {
                        text = title
                        gravity = Gravity.BOTTOM or Gravity.LEFT
                        setSingleLine()
                        isVisible = shouldShowTitle
                        textSize = URL_TEXT_SIZE_ALONE.sp.value
                        setTextColor(textColor.toArgb())
                        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f)
                    },
                )

                addView(
                    CustomFadeAndGravityTextView(context, fadeDirection, textGravity).apply {
                        text = urlToShow
                        gravity = urlGravity
                        setSingleLine()
                        textSize = urlTextSize.value
                        setTextColor(textColor.toArgb())
                        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f)
                    },
                )

                setOnClickListener { onInteraction(onClick) }
                setLongClickHandling(onLongClick, longClickMenu, onInteraction)
            }
        },
        update = { container ->
            (container.getChildAt(0) as? AppCompatTextView)?.apply {
                text = title
                isVisible = shouldShowTitle
            }
            (container.getChildAt(1) as? AppCompatTextView)?.apply {
                text = urlToShow
                gravity = urlGravity
                textSize = urlTextSize.value
            }

            container.setLongClickHandling(onLongClick, longClickMenu, onInteraction)
        },
    )
}

private fun View.setLongClickHandling(
    onLongClick: BrowserToolbarInteraction?,
    longClickMenu: BrowserMenuController?,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    if (onLongClick is BrowserToolbarEvent) {
        setOnLongClickListener {
            onInteraction(onLongClick)
            true
        }
    } else if (onLongClick is BrowserToolbarMenu && longClickMenu != null) {
        setOnLongClickListener {
            longClickMenu.show(anchor = this)
            true
        }
    } else if (onLongClick is CombinedEventAndMenu && longClickMenu != null) {
        setOnLongClickListener {
            onInteraction(onLongClick.event)
            longClickMenu.show(anchor = this)
            true
        }
    } else {
        setOnLongClickListener(null)
    }
}

@Preview(showBackground = true)
@Composable
private fun OriginPreviewWithJustTheHint() {
    AcornTheme {
        Origin(
            hint = R.string.mozac_browser_toolbar_search_hint,
            url = null,
            title = null,
            onClick = object : BrowserToolbarEvent {},
            onLongClick = null,
            onInteraction = {},
            fadeDirection = FADE_DIRECTION_END,
            textGravity = TEXT_GRAVITY_START,
        )
    }
}

@Preview(uiMode = UI_MODE_NIGHT_YES or UI_MODE_TYPE_NORMAL)
@Composable
private fun OriginPreviewWithTitleAndURL() {
    AcornTheme {
        Origin(
            hint = R.string.mozac_browser_toolbar_search_hint,
            modifier = Modifier.background(AcornTheme.colors.layer1),
            url = "https://mozilla.com",
            title = "Test title",
            onClick = object : BrowserToolbarEvent {},
            onLongClick = null,
            onInteraction = {},
            fadeDirection = FADE_DIRECTION_END,
            textGravity = TEXT_GRAVITY_START,
        )
    }
}

@Preview(showBackground = true, widthDp = 160)
@Composable
private fun OriginPreviewWithTitleAndURLStart() {
    AcornTheme {
        Origin(
            hint = R.string.mozac_browser_toolbar_search_hint,
            url = "https://mozilla.com/firefox-browser",
            title = "Test title",
            onClick = object : BrowserToolbarEvent {},
            onLongClick = null,
            onInteraction = {},
            fadeDirection = FADE_DIRECTION_END,
            textGravity = TEXT_GRAVITY_START,
        )
    }
}

@Preview(widthDp = 160, uiMode = UI_MODE_NIGHT_YES or UI_MODE_TYPE_NORMAL)
@Composable
private fun OriginPreviewWithTitleAndURLEnd() {
    AcornTheme {
        Origin(
            hint = R.string.mozac_browser_toolbar_search_hint,
            modifier = Modifier.background(AcornTheme.colors.layer1),
            url = "https://mozilla.com/firefox-browser",
            title = "Test title",
            onClick = object : BrowserToolbarEvent {},
            onLongClick = null,
            onInteraction = {},
            fadeDirection = FADE_DIRECTION_END,
            textGravity = TEXT_GRAVITY_END,
        )
    }
}

@Preview(widthDp = 160)
@Composable
private fun OriginPreviewWithJustURLStart() {
    AcornTheme {
        Origin(
            hint = R.string.mozac_browser_toolbar_search_hint,
            modifier = Modifier.background(AcornTheme.colors.layer1),
            url = "https://mozilla.com/firefox-browser",
            title = null,
            onClick = object : BrowserToolbarEvent {},
            onLongClick = null,
            onInteraction = {},
            fadeDirection = FADE_DIRECTION_END,
            textGravity = TEXT_GRAVITY_START,
        )
    }
}

@Preview(widthDp = 160, uiMode = UI_MODE_NIGHT_YES or UI_MODE_TYPE_NORMAL)
@Composable
private fun OriginPreviewWithJustURLEnd() {
    AcornTheme {
        Origin(
            hint = R.string.mozac_browser_toolbar_search_hint,
            modifier = Modifier.background(AcornTheme.colors.layer1),
            url = "https://mozilla.com/firefox-browser",
            title = null,
            onClick = object : BrowserToolbarEvent {},
            onLongClick = null,
            onInteraction = {},
            fadeDirection = FADE_DIRECTION_START,
            textGravity = TEXT_GRAVITY_END,
        )
    }
}
