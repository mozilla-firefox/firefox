/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui.gradient

import android.graphics.BlurMaskFilter
import androidx.compose.animation.core.LinearEasing
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
    val gradientStart = Color(0xFF9059FF)
    val gradientMiddle = Color(0xFFFF4AA2)
    val gradientEnd = Color(0xFFFFA436)
}

private data class BlobDrawLayer(
    val spec: BlobSpec,
    val phases: BlobPhases,
    val color: Color,
)

private val BLOB_DRAW_LAYERS = listOf(
    BlobDrawLayer(DARK_PURPLE_BLOB, DARK_PURPLE_G1_PHASES, GradientPalette.gradientStart),
    BlobDrawLayer(LIGHT_PURPLE_BLOB, LIGHT_PURPLE_G1_PHASES, GradientPalette.gradientMiddle),
    BlobDrawLayer(ORANGE_BLOB, ORANGE_G1_PHASES, GradientPalette.gradientEnd),
    BlobDrawLayer(DARK_PURPLE_BLOB, DARK_PURPLE_G2_PHASES, GradientPalette.gradientStart),
    BlobDrawLayer(LIGHT_PURPLE_BLOB, LIGHT_PURPLE_G2_PHASES, GradientPalette.gradientMiddle),
    BlobDrawLayer(ORANGE_BLOB, ORANGE_G2_PHASES, GradientPalette.gradientEnd),
)

private data class BlobSegment(
    val start: BlobPose,
    val end: BlobPose,
    val progress: Float,
)

/**
 * Modifier that renders an animated gradient loading effect behind the content.
 */
@Suppress("ComposeModifierComposed")
fun Modifier.summaryLoadingGradient(alpha: Float = 1f): Modifier = composed {
    val density = LocalDensity.current.density
    val surfaceColor = MaterialTheme.colorScheme.surface

    val blobPaths = remember {
        listOf(DARK_PURPLE_BLOB, LIGHT_PURPLE_BLOB, ORANGE_BLOB).associateWith { spec ->
            PathParser().parsePathString(spec.pathData).toPath()
        }
    }

    val blobTimeMs = rememberBlobTimeMs()
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
            drawBackgroundWash(alpha)

            for (layer in BLOB_DRAW_LAYERS) {
                val pose = computeBlobPoseAtTime(blobTimeMs, layer.phases)
                drawBlob(
                    path = blobPaths.getValue(layer.spec),
                    spec = layer.spec,
                    positionDp = pose.offsetDp,
                    color = layer.color.copy(alpha = BlobDefaults.ALPHA * alpha),
                    density = density,
                    paint = blobPaint,
                    maskFilter = blobMaskFilter,
                    widthDp = pose.widthDp,
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

private fun DrawScope.drawBackgroundWash(alpha: Float = 1f) {
    val (start, end) = gradientLineEndpoints(
        widthPx = size.width,
        heightPx = size.height,
    )
    drawRect(
        brush = Brush.linearGradient(
            colorStops = arrayOf(
                GradientDefaults.START_STOP to
                    GradientPalette.gradientStart.copy(alpha = GradientDefaults.ALPHA * alpha),
                GradientDefaults.MIDDLE_STOP to
                    GradientPalette.gradientMiddle.copy(alpha = GradientDefaults.ALPHA * alpha),
                GradientDefaults.END_STOP to
                    GradientPalette.gradientEnd.copy(alpha = GradientDefaults.ALPHA * alpha),
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
) {
    val xPx = positionDp.x * density
    val yPx = positionDp.y * density
    val widthPx = widthDp * density
    val blobScale = widthPx / spec.originalWidthDp
    val rotationPivot = Offset(spec.originalWidthDp / 2f, spec.originalWidthDp / 2f)

    paint.color = color.toArgb()
    paint.maskFilter = maskFilter

    translate(left = xPx, top = yPx) {
        scale(scaleX = blobScale, scaleY = blobScale, pivot = Offset.Zero) {
            rotate(degrees = spec.rotationDeg, pivot = rotationPivot) {
                drawContext.canvas.nativeCanvas.drawPath(path.asAndroidPath(), paint)
            }
        }
    }
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
