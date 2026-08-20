/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.graphics.drawable.Drawable
import android.view.SoundEffectConstants
import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.google.accompanist.drawablepainter.rememberDrawablePainter
import kotlinx.coroutines.delay
import mozilla.components.compose.base.badge.BadgedIcon
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarInteraction.BrowserToolbarEvent
import mozilla.components.ui.icons.R as iconsR

const val FADE_OUT_DURATION_MILLIS = 600
const val ANIMATION_DELAY_MILLIS = 400L

/**
 * A transient pill-shaped button that displays an [icon] alongside a [text] label, then animates away automatically:
 * after [ANIMATION_DELAY_MILLIS] the label and pill fade out while the pill shrinks to a circle, causing the parent to
 * reflow its children.
 *
 * @param icon The main icon to display.
 * @param overlayIcon A smaller optional icon overlaid at the bottom-end of [icon].
 * @param text The label text shown initially beside the icon.
 * @param contentDescription Accessibility content description for the button.
 * @param animated Whether to animate the collapsing transition or present in 'post-animation' state.
 * @param highlighted Whether a highlight badge should be drawn on top of [icon].
 * @param onClick Interaction dispatched when the button is tapped.
 * @param onInteraction Callback for dispatching [BrowserToolbarEvent]s to the store.
 * @param testTag Optional test tag for this button.
 * @param onAnimationFinished Optional [BrowserToolbarEvent] dispatched via [onInteraction] once the pill has fully
 *   collapsed on screen (or immediately, if [animated] is false).
 */
@Suppress("CognitiveComplexMethod")
@Composable
internal fun AnimatedPillButton(
    icon: Drawable,
    overlayIcon: Drawable? = null,
    text: String,
    contentDescription: String,
    animated: Boolean = true,
    highlighted: Boolean = false,
    onClick: BrowserToolbarInteraction,
    onInteraction: (BrowserToolbarEvent) -> Unit,
    testTag: String? = null,
    onAnimationFinished: BrowserToolbarEvent? = null,
) {
    // refactoring planned in https://bugzilla.mozilla.org/show_bug.cgi?id=2030770
    val view = LocalView.current
    val density = LocalDensity.current
    var fullWidthPx by remember { mutableIntStateOf(0) }
    val contractionProgress = remember { Animatable(if (animated) 1f else 0f) }

    // We use a single background color for both the pill and overlay background so that they match.
    val containerColor =
        lerp(
            MaterialTheme.colorScheme.surfaceContainerHighest,
            MaterialTheme.colorScheme.surfaceContainerLowest,
            contractionProgress.value,
        )

    LaunchedEffect(fullWidthPx) {
        if (fullWidthPx == 0) return@LaunchedEffect
        if (animated) {
            delay(ANIMATION_DELAY_MILLIS)
            contractionProgress.animateTo(0f, tween(durationMillis = FADE_OUT_DURATION_MILLIS))
        }

        onAnimationFinished?.let(onInteraction)
    }

    val animatedWidthDp = animatedWidth(fullWidthPx, COLLAPSED_WIDTH, contractionProgress.value, density)

    Box(
        contentAlignment = Alignment.Center,
        modifier =
            Modifier.padding(horizontal = 4.dp)
                .height(40.dp)
                .then(if (animatedWidthDp != Dp.Unspecified) Modifier.width(animatedWidthDp) else Modifier)
                .onSizeChanged { size ->
                    if (fullWidthPx == 0 && size.width > 0) fullWidthPx = size.width
                }
                .clip(CircleShape)
                .background(containerColor)
                .clickable {
                    view.playSoundEffect(SoundEffectConstants.CLICK)
                    if (onClick is BrowserToolbarEvent) {
                        onInteraction(onClick)
                    }
                }
                .semantics(mergeDescendants = true) {
                    this.contentDescription = contentDescription
                    if (testTag != null) {
                        this.testTag = testTag
                    }
                },
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            LayeredIcon(
                painter = rememberDrawablePainter(icon),
                overlayPainter = overlayIcon?.let { rememberDrawablePainter(it) },
                overlayBackground = containerColor,
                isHighlighted = highlighted,
            )

            Spacer(modifier = Modifier.width(4.dp))

            Text(
                text = text,
                modifier = Modifier.alpha(contractionProgress.value),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.tertiary,
                maxLines = 1,
                softWrap = false,
            )
        }
    }
}

private val COLLAPSED_WIDTH = 40.dp

private fun animatedWidth(
    fullWidthPx: Int,
    collapsedWidthDp: Dp,
    contractionProgress: Float,
    density: Density,
) =
    if (fullWidthPx > 0) {
        val collapsedPx = with(density) { collapsedWidthDp.toPx() }
        with(density) { (collapsedPx + (fullWidthPx - collapsedPx) * contractionProgress).toDp() }
    } else {
        Dp.Unspecified
    }

/**
 * Renders an icon with an optional [overlayPainter] icon layered at the bottom-end corner.
 *
 * @param painter The main icon.
 * @param overlayPainter The optional overlay icon layered at the bottom-end corner.
 * @param tint Tint applied to [painter].
 * @param overlayTint Tint applied to [overlayPainter].
 * @param overlayBackground Solid fill drawn behind [overlayPainter] as a circle, used to occlude the base icon.
 * @param isHighlighted Whether to render a highlight badge.
 */
@Composable
private fun LayeredIcon(
    painter: Painter,
    overlayPainter: Painter?,
    tint: Color = MaterialTheme.colorScheme.onSurface,
    overlayTint: Color = MaterialTheme.colorScheme.tertiary,
    overlayBackground: Color = Color.Unspecified,
    isHighlighted: Boolean = false,
) {
    Box {
        BadgedIcon(
            painter = painter,
            isHighlighted = isHighlighted,
            tint = tint,
        )

        if (overlayPainter != null) {
            Icon(
                painter = overlayPainter,
                contentDescription = null,
                modifier =
                    Modifier.align(Alignment.BottomEnd)
                        .padding(end = 1.dp)
                        .size(11.dp)
                        .clip(CircleShape)
                        .background(overlayBackground),
                tint = overlayTint,
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun AnimatedPillButtonPreview() {
    AcornTheme {
        AnimatedPillButton(
            icon =
                AppCompatResources.getDrawable(
                    LocalContext.current,
                    iconsR.drawable.mozac_ic_shield_checkmark_24,
                )!!,
            overlayIcon =
                AppCompatResources.getDrawable(
                    LocalContext.current,
                    iconsR.drawable.mozac_ic_globe_24,
                )!!,
            text = "VPN On",
            contentDescription = "VPN On",
            onClick = object : BrowserToolbarEvent {},
            onInteraction = {},
        )
    }
}

@PreviewLightDark
@Composable
private fun AnimatedPillButtonHighlightedPreview() {
    AcornTheme {
        AnimatedPillButton(
            icon =
                AppCompatResources.getDrawable(
                    LocalContext.current,
                    iconsR.drawable.mozac_ic_shield_checkmark_24,
                )!!,
            overlayIcon =
                AppCompatResources.getDrawable(
                    LocalContext.current,
                    iconsR.drawable.mozac_ic_globe_24,
                )!!,
            text = "VPN On",
            contentDescription = "VPN On",
            highlighted = true,
            onClick = object : BrowserToolbarEvent {},
            onInteraction = {},
        )
    }
}
