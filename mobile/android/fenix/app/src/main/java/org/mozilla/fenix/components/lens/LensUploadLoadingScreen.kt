/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.lens

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Canvas
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.LinearGradientShader
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.hideFromAccessibility
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.ui.colors.NovaColors
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

private const val BACKDROP_SCRIM_ALPHA = 0.7f
private const val BACKDROP_FADE_IN_MILLIS = 300

private const val CARD_MAX_WIDTH_FRACTION = 0.72f
private const val CARD_MAX_HEIGHT_FRACTION = 0.5f
private const val CARD_MIN_ASPECT_RATIO = 0.5f
private const val CARD_MAX_ASPECT_RATIO = 2f
private const val CARD_INITIAL_SCALE = 0.85f
private const val CARD_SPRING_DAMPING_RATIO = 0.72f
private const val CARD_FADE_IN_MILLIS = 200
private val CardCornerRadius = 24.dp
private val CardElevation = 16.dp

private const val CONTROLS_DELAY_AFTER_CARD_MILLIS = 200L
private const val CONTROLS_FALLBACK_DELAY_MILLIS = 400L
private const val CONTROLS_FADE_IN_MILLIS = 250
private const val CANCEL_STAGGER_MILLIS = 80
private val ControlsInitialOffset = 12.dp

private val CardToStatusSpacing = 24.dp
private val StatusToCancelSpacing = 16.dp
private val StatusPillHorizontalPadding = 16.dp
private val StatusPillVerticalPadding = 10.dp
private val StatusPillSpacing = 8.dp
private val StatusPillBackgroundColor = Color.Black.copy(alpha = 0.55f)
private val SpinnerSize = 16.dp
private val SpinnerStrokeWidth = 2.dp
private val CancelButtonContainerColor = Color.White.copy(alpha = 0.2f)

/**
 * Shown by [LensCameraActivity] while a photo picker image is uploaded to Google Lens: the image pops in as a card over
 * a blurred copy of itself, followed by a status pill and a cancel button.
 *
 * @param preview The picked image, or null while it is still being decoded.
 * @param backdrop A tiny copy of the picked image, stretched over the screen so it reads as blurred, or null while it
 *   is still being decoded.
 * @param animateIn Whether the content animates in. False when the screen is recreated mid-upload, so a rotation
 *   doesn't replay the entrance.
 * @param onCancel Invoked when the user taps the cancel button.
 * @param modifier The [Modifier] to be applied to this composable.
 */
@Composable
internal fun LensUploadLoadingScreen(
    preview: ImageBitmap?,
    backdrop: ImageBitmap?,
    animateIn: Boolean,
    onCancel: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var controlsVisible by remember { mutableStateOf(!animateIn) }
    // Shown without waiting for the image, which can take seconds to arrive from a cloud photo provider, so the user
    // can always cancel.
    LaunchedEffect(Unit) {
        delay(CONTROLS_FALLBACK_DELAY_MILLIS)
        controlsVisible = true
    }
    LaunchedEffect(preview != null) {
        if (preview != null) {
            delay(CONTROLS_DELAY_AFTER_CARD_MILLIS)
            controlsVisible = true
        }
    }

    Box(modifier = modifier.fillMaxSize().background(Color.Black)) {
        if (backdrop != null) {
            Backdrop(backdrop = backdrop, animateIn = animateIn)
        }
        Box(modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = BACKDROP_SCRIM_ALPHA)))

        BoxWithConstraints(
            modifier = Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing),
            contentAlignment = Alignment.Center,
        ) {
            val maxCardSize = DpSize(maxWidth * CARD_MAX_WIDTH_FRACTION, maxHeight * CARD_MAX_HEIGHT_FRACTION)
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                if (preview != null) {
                    PreviewCard(
                        preview = preview,
                        size = fitCardSize(preview, maxCardSize),
                        animateIn = animateIn,
                    )
                }
                StatusPill(
                    visible = controlsVisible,
                    modifier = Modifier.padding(top = CardToStatusSpacing),
                )
                CancelButton(
                    visible = controlsVisible,
                    onCancel = onCancel,
                    modifier = Modifier.padding(top = StatusToCancelSpacing),
                )
            }
        }
    }
}

@Composable
private fun Backdrop(backdrop: ImageBitmap, animateIn: Boolean) {
    val alpha = remember { Animatable(if (animateIn) 0f else 1f) }
    LaunchedEffect(Unit) {
        alpha.animateTo(1f, tween(BACKDROP_FADE_IN_MILLIS, easing = LinearOutSlowInEasing))
    }
    Image(
        bitmap = backdrop,
        contentDescription = null,
        contentScale = ContentScale.Crop,
        modifier = Modifier.fillMaxSize().graphicsLayer { this.alpha = alpha.value },
    )
}

@Composable
private fun PreviewCard(preview: ImageBitmap, size: DpSize, animateIn: Boolean) {
    val scale = remember { Animatable(if (animateIn) CARD_INITIAL_SCALE else 1f) }
    val alpha = remember { Animatable(if (animateIn) 0f else 1f) }
    LaunchedEffect(Unit) {
        launch {
            scale.animateTo(
                1f,
                spring(dampingRatio = CARD_SPRING_DAMPING_RATIO, stiffness = Spring.StiffnessMediumLow),
            )
        }
        alpha.animateTo(1f, tween(CARD_FADE_IN_MILLIS))
    }
    Image(
        bitmap = preview,
        contentDescription = null,
        contentScale = ContentScale.Crop,
        modifier =
            Modifier.size(size)
                .graphicsLayer {
                    scaleX = scale.value
                    scaleY = scale.value
                    this.alpha = alpha.value
                }
                .shadow(elevation = CardElevation, shape = RoundedCornerShape(CardCornerRadius), clip = true),
    )
}

@Composable
private fun animateControlFadeIn(visible: Boolean, delayMillis: Int = 0): State<Float> =
    animateFloatAsState(
        targetValue = if (visible) 1f else 0f,
        animationSpec = tween(CONTROLS_FADE_IN_MILLIS, delayMillis, FastOutSlowInEasing),
        label = "LensUploadControlFadeIn",
    )

/**
 * The pill and the cancel button always take up their space, so the card doesn't jump when they fade in. Until then
 * they are hidden from accessibility services.
 */
private fun Modifier.controlFadeIn(progress: State<Float>, visible: Boolean): Modifier {
    val faded = graphicsLayer {
        alpha = progress.value
        translationY = (1f - progress.value) * ControlsInitialOffset.toPx()
    }
    return if (visible) faded else faded.semantics { hideFromAccessibility() }
}

@Composable
private fun StatusPill(visible: Boolean, modifier: Modifier = Modifier) {
    val progress = animateControlFadeIn(visible)
    Row(
        modifier =
            modifier
                .controlFadeIn(progress, visible)
                .clip(CircleShape)
                .background(StatusPillBackgroundColor)
                .padding(horizontal = StatusPillHorizontalPadding, vertical = StatusPillVerticalPadding)
                .semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
        horizontalArrangement = Arrangement.spacedBy(StatusPillSpacing),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CircularProgressIndicator(
            modifier = Modifier.size(SpinnerSize),
            color = Color.White,
            strokeWidth = SpinnerStrokeWidth,
        )
        Text(
            text = stringResource(R.string.lens_camera_loading_status),
            color = Color.White,
            style = FirefoxTheme.typography.body2,
        )
    }
}

@Composable
private fun CancelButton(visible: Boolean, onCancel: () -> Unit, modifier: Modifier = Modifier) {
    val progress = animateControlFadeIn(visible, delayMillis = CANCEL_STAGGER_MILLIS)
    FilledButton(
        text = stringResource(R.string.lens_camera_loading_cancel_button),
        modifier = modifier.controlFadeIn(progress, visible),
        enabled = visible,
        contentColor = Color.White,
        containerColor = CancelButtonContainerColor,
        onClick = onCancel,
    )
}

/**
 * Fits the card to [preview]'s aspect ratio within [maxSize]. Extreme ratios are clamped and cropped so a panorama or a
 * long screenshot still reads as a card.
 */
private fun fitCardSize(preview: ImageBitmap, maxSize: DpSize): DpSize {
    val aspectRatio =
        (preview.width.toFloat() / preview.height.coerceAtLeast(1)).coerceIn(
            CARD_MIN_ASPECT_RATIO,
            CARD_MAX_ASPECT_RATIO,
        )
    val widthForMaxHeight: Dp = maxSize.height * aspectRatio
    return if (widthForMaxHeight <= maxSize.width) {
        DpSize(widthForMaxHeight, maxSize.height)
    } else {
        DpSize(maxSize.width, maxSize.width / aspectRatio)
    }
}

private fun previewImage(width: Int, height: Int): ImageBitmap {
    val image = ImageBitmap(width, height)
    val paint =
        Paint().apply {
            shader =
                LinearGradientShader(
                    from = Offset.Zero,
                    to = Offset(width.toFloat(), height.toFloat()),
                    colors = listOf(NovaColors.Orange50, NovaColors.Violet50),
                )
        }
    Canvas(image).drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)
    return image
}

@Preview
@Composable
private fun LensUploadLoadingScreenPreview() {
    LensUploadLoadingScreen(
        preview = previewImage(width = 300, height = 400),
        backdrop = previewImage(width = 24, height = 32),
        animateIn = false,
        onCancel = {},
    )
}
