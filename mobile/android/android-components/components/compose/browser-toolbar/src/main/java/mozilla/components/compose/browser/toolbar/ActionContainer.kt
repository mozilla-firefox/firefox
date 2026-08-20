/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar

import android.graphics.drawable.Drawable
import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.concept.Action
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButton
import mozilla.components.compose.browser.toolbar.concept.Action.ActionButtonRes
import mozilla.components.compose.browser.toolbar.concept.Action.AnimatedPillActionRes
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.ContentDescription.StringContentDescription
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.ContentDescription.StringResContentDescription
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.Icon.DrawableIcon
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction.Icon.DrawableResIcon
import mozilla.components.compose.browser.toolbar.concept.Action.TabCounterAction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.compose.browser.toolbar.ui.ActionButton as ActionButtonComposable
import mozilla.components.compose.browser.toolbar.ui.AnimatedPillButton
import mozilla.components.compose.browser.toolbar.ui.SearchSelector
import mozilla.components.compose.browser.toolbar.ui.TabCounter
import mozilla.components.ui.icons.R as iconsR

/**
 * A container for displaying [Action]s.
 *
 * @param actions List of [Action]s to display in the container.
 * @param onInteraction Callback for handling [BrowserToolbarEvent]s on user interactions.
 * @param modifier Modifier to apply to the container.
 * @param horizontalArrangement The horizontal arrangement of the layout's children.
 */
@Composable
fun ActionContainer(
    actions: List<Action>,
    onInteraction: (BrowserToolbarEvent) -> Unit,
    modifier: Modifier = Modifier,
    horizontalArrangement: Arrangement.Horizontal = Arrangement.Start,
) {
    Row(
        modifier = modifier,
        horizontalArrangement = horizontalArrangement,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        for (action in actions) {
            when (action) {
                is ActionButtonRes -> ActionButtonResItem(action, onInteraction)
                is ActionButton -> ActionButtonItem(action, onInteraction)
                is SearchSelectorAction -> SearchSelectorItem(action, onInteraction)
                is TabCounterAction -> TabCounterItem(action, onInteraction)
                is AnimatedPillActionRes -> AnimatedPillItem(action, onInteraction)
            }
        }
    }
}

@Composable
private fun ActionButtonResItem(
    action: ActionButtonRes,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    action.iconDrawable()?.let {
        ActionButtonComposable(
            icon = it,
            contentDescription = stringResource(action.contentDescription),
            state = action.state,
            onClick = action.onClick,
            highlighted = action.highlighted,
            onLongClick = action.onLongClick,
            onInteraction = { event -> onInteraction(event) },
            testTag = action.testTag,
        )
    }
}

@Composable
private fun ActionButtonItem(
    action: ActionButton,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    action.iconDrawable()?.let {
        ActionButtonComposable(
            icon = it,
            contentDescription = action.contentDescription,
            state = action.state,
            onClick = action.onClick,
            highlighted = action.highlighted,
            onLongClick = action.onLongClick,
            onInteraction = { event -> onInteraction(event) },
            testTag = action.testTag,
        )
    }
}

@Composable
private fun SearchSelectorItem(
    action: SearchSelectorAction,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    SearchSelector(
        icon = action.iconDrawable(),
        shouldTint = (action.icon as? DrawableIcon)?.shouldTint ?: true,
        contentDescription = action.contentDescription(),
        menu = action.menu,
        onInteraction = { event -> onInteraction(event) },
        onClick = action.onClick,
    )
}

@Composable
private fun TabCounterItem(
    action: TabCounterAction,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    TabCounter(
        count = action.count,
        showPrivacyMask = action.showPrivacyMask,
        onClick = action.onClick,
        onLongClick = action.onLongClick,
        onInteraction = { event -> onInteraction(event) },
    )
}

@Composable
private fun AnimatedPillItem(
    action: AnimatedPillActionRes,
    onInteraction: (BrowserToolbarEvent) -> Unit,
) {
    // Recreates the composable when switching pills (e.g., "VPN on" -> "VPN off").
    // Without `key`, Compose reuses the previous pill's state, skipping the new animation.
    key(action.textResId) {
        action.iconDrawable()?.let {
            AnimatedPillButton(
                icon = it,
                overlayIcon = action.overlayDrawable(),
                text = stringResource(action.textResId),
                contentDescription = stringResource(action.contentDescriptionResId),
                highlighted = action.highlighted,
                animated = action.animated,
                onClick = action.onClick,
                onInteraction = onInteraction,
                testTag = action.testTag,
                onAnimationFinished = action.onAnimationFinished,
            )
        }
    }
}

@Composable
private fun ActionButtonRes.iconDrawable(): Drawable? {
    val context = LocalContext.current
    val tint = MaterialTheme.colorScheme.onSurface

    return remember(this, context, tint) {
        AppCompatResources.getDrawable(context, drawableResId)?.apply { mutate().setTint(tint.toArgb()) }
    }
}

@Composable
private fun AnimatedPillActionRes.iconDrawable(): Drawable? {
    val context = LocalContext.current
    return remember(iconResId, context) {
        AppCompatResources.getDrawable(context, iconResId)
    }
}

@Composable
private fun AnimatedPillActionRes.overlayDrawable(): Drawable? {
    val context = LocalContext.current
    return remember(overlayResId, context) {
        AppCompatResources.getDrawable(context, overlayResId)
    }
}

@Composable
private fun ActionButton.iconDrawable(): Drawable? {
    val tint = MaterialTheme.colorScheme.onSurface
    return remember(this, tint) {
        when (shouldTint) {
            true -> drawable?.mutate()?.apply { setTint(tint.toArgb()) }
            false -> drawable
        }
    }
}

@Composable
@ReadOnlyComposable
private fun SearchSelectorAction.contentDescription() =
    when (contentDescription) {
        is StringContentDescription -> contentDescription.text
        is StringResContentDescription -> stringResource(contentDescription.resourceId)
    }

@Composable
private fun SearchSelectorAction.iconDrawable(): Drawable? {
    val context = LocalContext.current
    val tint = MaterialTheme.colorScheme.onSurface

    val drawable =
        remember(this, context, tint) {
            when (icon) {
                is DrawableIcon -> icon.drawable
                is DrawableResIcon ->
                    AppCompatResources.getDrawable(context, icon.resourceId)?.apply { setTint(tint.toArgb()) }
            }
        }
    return drawable
}

@PreviewLightDark
@Composable
private fun ActionContainerPreview() {
    AcornTheme {
        ActionContainer(
            actions =
                listOf(
                    SearchSelectorAction(
                        icon = DrawableResIcon(iconsR.drawable.mozac_ic_search_24),
                        contentDescription = StringContentDescription("Change search engine for this search"),
                        menu = { emptyList() },
                        onClick = null,
                    ),
                    ActionButtonRes(
                        drawableResId = iconsR.drawable.mozac_ic_microphone_24,
                        contentDescription = R.string.mozac_clear_button_description,
                        onClick = object : BrowserToolbarEvent {},
                    ),
                    ActionButton(
                        drawable =
                            AppCompatResources.getDrawable(LocalContext.current, iconsR.drawable.mozac_ic_tool_24),
                        contentDescription = stringResource(R.string.mozac_clear_button_description),
                        onClick = object : BrowserToolbarEvent {},
                    ),
                    TabCounterAction(
                        count = 1,
                        contentDescription = "",
                        showPrivacyMask = false,
                        onClick = object : BrowserToolbarEvent {},
                    ),
                    AnimatedPillActionRes(
                        iconResId = iconsR.drawable.mozac_ic_shield_checkmark_24,
                        overlayResId = iconsR.drawable.mozac_ic_globe_24,
                        textResId = R.string.mozac_clear_button_description,
                        contentDescriptionResId = R.string.mozac_clear_button_description,
                        highlighted = true,
                        onClick = object : BrowserToolbarEvent {},
                    ),
                ),
            onInteraction = {},
            modifier = Modifier.background(color = MaterialTheme.colorScheme.surfaceContainerHighest),
        )
    }
}
