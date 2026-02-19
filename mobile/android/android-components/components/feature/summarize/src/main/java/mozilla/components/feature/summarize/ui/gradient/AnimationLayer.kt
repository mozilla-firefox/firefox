/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui.gradient

import android.graphics.BlurMaskFilter
import android.os.Build
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.asAndroidPath
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.util.lerp
import kotlin.math.cos
import kotlin.math.sin
import android.graphics.BlendMode as AndroidBlendMode
import android.graphics.Paint as AndroidPaint

private object GradientDefaults {
    const val ANGLE_DEG = 96.74f
    const val START_STOP = 0.0142f
    const val ALPHA = 0.15f
    const val MIDDLE_STOP = 0.4081f
    const val END_STOP = 0.9963f
}

private object BlobDefaults {
    const val ALPHA = 0.50f
    const val BLUR_DP = 90f
}

private object GradientPalette {
    val gradientStart = Color(0xFF3A0F6E)
    val gradientMiddle = Color(0xFF7543E3)
    val gradientEnd = Color(0xFFFF7638)
}

private data class BlobDrawLayer(
    val spec: BlobSpec,
    val phases: BlobPhases,
    val color: Color,
    val useColorDodge: Boolean = false,
)

private val BLOB_DRAW_LAYERS = listOf(
    BlobDrawLayer(DARK_PURPLE_BLOB, DARK_PURPLE_G1_PHASES, GradientPalette.gradientStart),
    BlobDrawLayer(LIGHT_PURPLE_BLOB, LIGHT_PURPLE_G1_PHASES, GradientPalette.gradientMiddle),
    BlobDrawLayer(ORANGE_BLOB, ORANGE_G1_PHASES, GradientPalette.gradientEnd),
    BlobDrawLayer(DARK_PURPLE_BLOB, DARK_PURPLE_G2_PHASES, GradientPalette.gradientStart, useColorDodge = true),
    BlobDrawLayer(LIGHT_PURPLE_BLOB, LIGHT_PURPLE_G2_PHASES, GradientPalette.gradientMiddle, useColorDodge = true),
    BlobDrawLayer(ORANGE_BLOB, ORANGE_G2_PHASES, GradientPalette.gradientEnd, useColorDodge = true),
)

private data class CircleSegment(
    val start: CirclePhase,
    val end: CirclePhase,
    val progress: Float,
)

private data class BlobSegment(
    val start: BlobPose,
    val end: BlobPose,
    val progress: Float,
)

private data class CirclePxState(
    val centerPx: Offset,
    val radiusPx: Float,
    val alpha: Float,
    val srcOverBlendWeight: Float,
    val screenBlendWeight: Float,
)

/**
 * Modifier that renders an animated gradient loading effect behind the content.
 */
@Suppress("ComposeModifierComposed")
fun Modifier.summaryLoadingGradient(): Modifier = composed {
    val density = LocalDensity.current.density
    val surfaceColor = MaterialTheme.colorScheme.surface

    val blobPaths = remember {
        listOf(DARK_PURPLE_BLOB, LIGHT_PURPLE_BLOB, ORANGE_BLOB).associateWith { spec ->
            PathParser().parsePathString(spec.pathData).toPath()
        }
    }

    val blobTimeMs = rememberBlobTimeMs()
    val circleTimeMs = rememberCircleGradientTimeMs()
    val blobPaint = remember {
        AndroidPaint(AndroidPaint.ANTI_ALIAS_FLAG).apply {
            style = AndroidPaint.Style.FILL
        }
    }
    val blobMaskFilter = remember(density) {
        BlurMaskFilter(BlobDefaults.BLUR_DP * density, BlurMaskFilter.Blur.NORMAL)
    }

    this
        .background(surfaceColor)
        .drawBehind {
            drawBackgroundWash()

            val circle = computeCircleGradientPx(
                tMs = circleTimeMs,
                canvasWidthPx = size.width,
                canvasHeightPx = size.height,
                density = density,
            )
            drawSpotlightCircle(circle = circle, color = surfaceColor)

            for (layer in BLOB_DRAW_LAYERS) {
                val pose = computeBlobPoseAtTime(blobTimeMs, layer.phases)
                drawBlob(
                    path = blobPaths.getValue(layer.spec),
                    spec = layer.spec,
                    positionDp = pose.offsetDp,
                    color = layer.color.copy(alpha = BlobDefaults.ALPHA),
                    density = density,
                    paint = blobPaint,
                    maskFilter = blobMaskFilter,
                    widthDp = pose.widthDp,
                    useColorDodge = layer.useColorDodge,
                )
            }
        }
}

/**
 * Composable that fills its bounds with the animated gradient loading effect.
 */
@Composable
fun GradientAnimationLayer(
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier.summaryLoadingGradient())
}

@Preview(showBackground = true, heightDp = 800)
@Composable
private fun SummaryLoadingGradientPreview() {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.surfaceVariant),
        contentAlignment = Alignment.BottomCenter,
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .height(400.dp),
            shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .summaryLoadingGradient(),
            ) {
                Text(
                    text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. " +
                        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.",
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp)
                        .align(Alignment.Center),
                    textAlign = TextAlign.Center,
                    color = MaterialTheme.colorScheme.onSurface,
                    style = MaterialTheme.typography.bodyLarge,
                )
            }
        }
    }
}

private fun DrawScope.drawSpotlightCircle(
    circle: CirclePxState,
    color: Color,
) {
    val srcOverAlpha = circle.alpha * circle.srcOverBlendWeight
    if (srcOverAlpha > 0f) {
        drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(
                    color.copy(alpha = srcOverAlpha),
                    color.copy(alpha = 0f),
                ),
                center = circle.centerPx,
                radius = circle.radiusPx,
            ),
            center = circle.centerPx,
            radius = circle.radiusPx,
            blendMode = BlendMode.SrcOver,
        )
    }

    val screenAlpha = circle.alpha * circle.screenBlendWeight
    if (screenAlpha > 0f) {
        drawCircle(
            brush = Brush.radialGradient(
                colors = listOf(
                    color.copy(alpha = screenAlpha),
                    color.copy(alpha = 0f),
                ),
                center = circle.centerPx,
                radius = circle.radiusPx,
            ),
            center = circle.centerPx,
            radius = circle.radiusPx,
            blendMode = BlendMode.Screen,
        )
    }
}

private fun DrawScope.drawBackgroundWash() {
    val (start, end) = gradientLineEndpoints(
        widthPx = size.width,
        heightPx = size.height,
    )
    drawRect(
        brush = Brush.linearGradient(
            colorStops = arrayOf(
                GradientDefaults.START_STOP to
                    GradientPalette.gradientStart.copy(alpha = GradientDefaults.ALPHA),
                GradientDefaults.MIDDLE_STOP to
                    GradientPalette.gradientMiddle.copy(alpha = GradientDefaults.ALPHA),
                GradientDefaults.END_STOP to
                    GradientPalette.gradientEnd.copy(alpha = GradientDefaults.ALPHA),
            ),
            start = start,
            end = end,
        ),
    )
}

private fun DrawScope.drawBlob(
    path: Path,
    spec: BlobSpec,
    positionDp: Offset,
    color: Color,
    density: Float,
    paint: AndroidPaint,
    maskFilter: BlurMaskFilter,
    widthDp: Float = spec.widthDp,
    useColorDodge: Boolean = false,
) {
    val xPx = positionDp.x * density
    val yPx = positionDp.y * density
    val widthPx = widthDp * density
    val blobScale = widthPx / spec.originalWidthDp
    val rotationPivot = Offset(spec.originalWidthDp / 2f, spec.originalWidthDp / 2f)

    paint.color = color.toArgb()
    // COLOR_DODGE requires API 29+; on older devices blobs render with normal blending.
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        paint.blendMode = if (useColorDodge) AndroidBlendMode.COLOR_DODGE else null
    }
    paint.maskFilter = maskFilter

    translate(left = xPx, top = yPx) {
        scale(scaleX = blobScale, scaleY = blobScale, pivot = Offset.Zero) {
            rotate(degrees = spec.rotationDeg, pivot = rotationPivot) {
                drawContext.canvas.nativeCanvas.drawPath(path.asAndroidPath(), paint)
            }
        }
    }
}

private fun computeCircleGradientPx(
    tMs: Float,
    canvasWidthPx: Float,
    canvasHeightPx: Float,
    density: Float,
): CirclePxState {
    val segment = when {
        tMs < CircleAnimation.PHASE_1_TO_2_DURATION_MS -> CircleSegment(
            start = CircleAnimation.PHASE_1,
            end = CircleAnimation.PHASE_2,
            progress = tMs / CircleAnimation.PHASE_1_TO_2_DURATION_MS,
        )

        tMs < CircleAnimation.PHASE_1_TO_2_DURATION_MS +
            CircleAnimation.PHASE_2_TO_3_DURATION_MS -> CircleSegment(
            start = CircleAnimation.PHASE_2,
            end = CircleAnimation.PHASE_3,
            progress = (tMs - CircleAnimation.PHASE_1_TO_2_DURATION_MS) /
                CircleAnimation.PHASE_2_TO_3_DURATION_MS,
        )

        tMs < CircleAnimation.PHASE_1_TO_2_DURATION_MS +
            CircleAnimation.PHASE_2_TO_3_DURATION_MS +
            CircleAnimation.PHASE_3_TO_4_DURATION_MS -> CircleSegment(
            start = CircleAnimation.PHASE_3,
            end = CircleAnimation.PHASE_4,
            progress = (
                tMs - CircleAnimation.PHASE_1_TO_2_DURATION_MS -
                    CircleAnimation.PHASE_2_TO_3_DURATION_MS
                ) / CircleAnimation.PHASE_3_TO_4_DURATION_MS,
        )

        else -> CircleSegment(
            start = CircleAnimation.PHASE_4,
            end = CircleAnimation.PHASE_1,
            progress = (
                tMs - CircleAnimation.PHASE_1_TO_2_DURATION_MS -
                    CircleAnimation.PHASE_2_TO_3_DURATION_MS -
                    CircleAnimation.PHASE_3_TO_4_DURATION_MS
                ) / CircleAnimation.PHASE_4_TO_1_DURATION_MS,
        )
    }

    val easedProgress = LinearOutSlowInEasing.transform(segment.progress.coerceIn(0f, 1f))
    val startCenter = phaseCenterPx(segment.start, canvasWidthPx, canvasHeightPx, density)
    val endCenter = phaseCenterPx(segment.end, canvasWidthPx, canvasHeightPx, density)
    val startRadius = (segment.start.sizeDp * density) / 2f
    val endRadius = (segment.end.sizeDp * density) / 2f
    val startScreenWeight = if (segment.start.blendMode == BlendMode.Screen) 1f else 0f
    val endScreenWeight = if (segment.end.blendMode == BlendMode.Screen) 1f else 0f
    val screenBlendWeight = lerp(startScreenWeight, endScreenWeight, easedProgress).coerceIn(0f, 1f)

    return CirclePxState(
        centerPx = lerp(startCenter, endCenter, easedProgress),
        radiusPx = lerp(startRadius, endRadius, easedProgress),
        alpha = lerp(segment.start.alpha, segment.end.alpha, easedProgress),
        srcOverBlendWeight = 1f - screenBlendWeight,
        screenBlendWeight = screenBlendWeight,
    )
}

private fun phaseCenterPx(
    phase: CirclePhase,
    canvasWidthPx: Float,
    canvasHeightPx: Float,
    density: Float,
): Offset {
    val sizePx = phase.sizeDp * density
    val leftPx = when {
        phase.leftDp != null -> phase.leftDp * density
        phase.rightDp != null -> canvasWidthPx - (phase.rightDp * density) - sizePx
        else -> 0f
    }
    val topPx = when {
        phase.topDp != null -> phase.topDp * density
        phase.bottomDp != null -> canvasHeightPx - (phase.bottomDp * density) - sizePx
        else -> 0f
    }
    return Offset(
        x = leftPx + sizePx / 2f,
        y = topPx + sizePx / 2f,
    )
}

private fun lerp(start: Offset, end: Offset, fraction: Float): Offset {
    return Offset(
        x = lerp(start.x, end.x, fraction),
        y = lerp(start.y, end.y, fraction),
    )
}

private fun computeBlobPoseAtTime(tMs: Float, phases: BlobPhases): BlobPose {
    val segment = when {
        tMs < BlobAnimation.PHASE_1_TO_2_DURATION_MS -> BlobSegment(
            start = phases.phase1,
            end = phases.phase2,
            progress = tMs / BlobAnimation.PHASE_1_TO_2_DURATION_MS,
        )

        tMs < BlobAnimation.PHASE_1_TO_2_DURATION_MS +
            BlobAnimation.PHASE_2_TO_3_DURATION_MS -> BlobSegment(
            start = phases.phase2,
            end = phases.phase3,
            progress = (tMs - BlobAnimation.PHASE_1_TO_2_DURATION_MS) /
                BlobAnimation.PHASE_2_TO_3_DURATION_MS,
        )

        else -> BlobSegment(
            start = phases.phase3,
            end = phases.phase1,
            progress = (
                tMs - BlobAnimation.PHASE_1_TO_2_DURATION_MS -
                    BlobAnimation.PHASE_2_TO_3_DURATION_MS
                ) / BlobAnimation.PHASE_3_TO_1_DURATION_MS,
        )
    }

    val easedProgress = BlobAnimation.PHASE_EASING.transform(segment.progress.coerceIn(0f, 1f))
    return BlobPose(
        offsetDp = lerp(segment.start.offsetDp, segment.end.offsetDp, easedProgress),
        widthDp = lerp(segment.start.widthDp, segment.end.widthDp, easedProgress),
    )
}

@Composable
private fun rememberBlobTimeMs(): Float {
    val transition = rememberInfiniteTransition(label = "BlobTime")
    val tMs by transition.animateFloat(
        initialValue = 0f,
        targetValue = BlobAnimation.TOTAL_DURATION_MS,
        animationSpec = infiniteRepeatable(
            animation = tween(
                durationMillis = BlobAnimation.TOTAL_DURATION_MS.toInt(),
                easing = LinearEasing,
            ),
            repeatMode = RepeatMode.Restart,
        ),
        label = "BlobTimeMs",
    )
    return tMs
}

@Composable
private fun rememberCircleGradientTimeMs(): Float {
    val transition = rememberInfiniteTransition(label = "CircleGradientTime")
    val tMs by transition.animateFloat(
        initialValue = 0f,
        targetValue = CircleAnimation.TOTAL_DURATION_MS,
        animationSpec = infiniteRepeatable(
            animation = tween(
                durationMillis = CircleAnimation.TOTAL_DURATION_MS.toInt(),
                easing = LinearEasing,
            ),
            repeatMode = RepeatMode.Restart,
        ),
        label = "CircleGradientMs",
    )
    return tMs
}

private fun gradientLineEndpoints(
    widthPx: Float,
    heightPx: Float,
): Pair<Offset, Offset> {
    val radians = Math.toRadians(GradientDefaults.ANGLE_DEG.toDouble())
    val dx = cos(radians).toFloat()
    val dy = sin(radians).toFloat()
    val centerX = widthPx / 2f
    val centerY = heightPx / 2f
    val halfLength = (maxOf(widthPx, heightPx) / 2f) * 1.5f
    val start = Offset(centerX - dx * halfLength, centerY - dy * halfLength)
    val end = Offset(centerX + dx * halfLength, centerY + dy * halfLength)
    return start to end
}
