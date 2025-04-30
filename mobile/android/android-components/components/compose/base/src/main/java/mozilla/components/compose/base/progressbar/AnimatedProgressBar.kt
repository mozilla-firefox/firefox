/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.progressbar

import android.content.res.Configuration.UI_MODE_NIGHT_YES
import android.content.res.Configuration.UI_MODE_TYPE_NORMAL
import android.view.View
import androidx.annotation.IntRange
import androidx.compose.animation.core.AnimationSpec
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateIntAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material.Divider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.LinearGradientShader
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.PaintingStyle
import androidx.compose.ui.graphics.Shader
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.R
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.support.utils.ColorUtils.lighten
import kotlin.time.Duration
import kotlin.time.Duration.Companion.milliseconds

// Implementation based on
// https://medium.com/@kappdev/creating-a-smooth-animated-progress-bar-in-jetpack-compose-canvas-drawing-and-gradient-animation-ddf07f77bb56

private const val DEFAULT_STROKE_WIDTH = 5
private const val DEFAULT_GLOW_RADIUS = 2
private const val DEFAULT_ANIMATION_SPEED = 1000
private const val MAX_PERCENTAGE = 100
private const val LIGHTEN_TRACK_COLOR_FACTOR = 0.2f

/**
 * A composable that displays a progress bar with an animated gradient.
 *
 * @param progress The current progress value (0 to 100).
 * For a `0` progress an accessibility announcement of loading being started will be made.
 * @param modifier The modifier to be applied to the layout.
 * @param color The list of colors defining the gradient animation.
 * If `null`, a default gradient will be used.
 * @param trackColor The background of the progressbar that spans for the entire width.
 * If `null`, a default color will be used - a lighter version of the application theme's background color.
 * @param strokeWidth The width of the progress line.
 * @param glowRadius The radius of the glow effect. If null, no glow effect will be applied.
 * @param strokeCap How the progress bar should be terminated.
 * @param gradientAnimationSpeed The speed of the gradient animation. Higher values result in a slower animation.
 * @param progressAnimSpec The animation behavior when values change. By default the change is shown instantly.
 */
@Composable
@Suppress("LongMethod")
fun AnimatedProgressBar(
    @IntRange(from = 0, to = 100) progress: Int,
    modifier: Modifier = Modifier.fillMaxWidth(),
    color: List<Color>? = null,
    trackColor: Color? = null,
    strokeWidth: Dp = DEFAULT_STROKE_WIDTH.dp,
    glowRadius: Dp? = DEFAULT_GLOW_RADIUS.dp,
    strokeCap: StrokeCap = StrokeCap.Butt,
    gradientAnimationSpeed: Duration = DEFAULT_ANIMATION_SPEED.milliseconds,
    progressAnimSpec: AnimationSpec<Int> = tween(durationMillis = 0),
) {
    val layoutDirection = LocalLayoutDirection.current
    val view = LocalView.current
    LaunchedEffect(progress) {
        view.announceProgressForAccessibility(progress)
    }

    val backgroundColor = AcornTheme.colors.layer1
    val trackBrush = remember(trackColor) {
        SolidColor(trackColor ?: backgroundColor.lighten(LIGHTEN_TRACK_COLOR_FACTOR))
    }

    @Suppress("MagicNumber")
    val colors = remember {
        val colorList = color ?: listOf(
            Color(0xFFF10366),
            Color(0xFFFF9100),
            Color(0xFF6173FF),
        )

        if (layoutDirection == LayoutDirection.Rtl) {
            colorList.asReversed()
        } else {
            colorList
        }
    }

    val animatedProgress by animateIntAsState(
        targetValue = progress.coerceIn(0, MAX_PERCENTAGE),
        animationSpec = progressAnimSpec,
    )

    val infiniteTransition = rememberInfiniteTransition()
    val offset by infiniteTransition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(
                durationMillis = gradientAnimationSpeed.inWholeMilliseconds.toInt(),
                easing = LinearEasing,
            ),
            repeatMode = RepeatMode.Restart,
        ),
    )

    val brush: ShaderBrush = remember(offset) {
        object : ShaderBrush() {
            override fun createShader(size: Size): Shader {
                val step = 1f / colors.size
                val start = step / 2

                val originalSpots = List(colors.size) { start + (step * it) }
                val transformedSpots = originalSpots.map { spot ->
                    val shiftedSpot = (spot + offset)
                    if (shiftedSpot > 1f) shiftedSpot - 1f else shiftedSpot
                }

                val pairs = colors.zip(transformedSpots).sortedBy { it.second }

                val margin = size.width / 2
                return LinearGradientShader(
                    colors = pairs.map { it.first },
                    colorStops = pairs.map { it.second },
                    from = Offset(-margin, 0f),
                    to = Offset(size.width + margin, 0f),
                )
            }
        }
    }

    Canvas(modifier) {
        val width = this.size.width
        val height = this.size.height

        val paint = Paint().apply {
            this.isAntiAlias = true
            this.style = PaintingStyle.Stroke
            this.strokeWidth = strokeWidth.toPx()
            this.strokeCap = strokeCap
            this.shader = brush.createShader(size)
        }

        glowRadius?.let { radius ->
            paint.asFrameworkPaint().apply {
                setShadowLayer(radius.toPx(), 0f, 0f, backgroundColor.toArgb())
            }
        }

        if (animatedProgress > 0 && animatedProgress < MAX_PERCENTAGE) {
            trackBrush.let { tBrush ->
                drawLine(
                    brush = tBrush,
                    start = Offset(0f, height / 2f),
                    end = Offset(width, height / 2f),
                    cap = strokeCap,
                    strokeWidth = strokeWidth.toPx(),
                )
            }
            val progressWidth = width * animatedProgress / MAX_PERCENTAGE
            drawIntoCanvas { canvas ->
                val startOffset: Offset
                val endOffset: Offset

                if (layoutDirection == LayoutDirection.Ltr) {
                    startOffset = Offset(0f, height / 2f)
                    endOffset = Offset(progressWidth, height / 2f)
                } else {
                    startOffset = Offset(width, height / 2f)
                    endOffset = Offset(width - progressWidth, height / 2f)
                }

                canvas.drawLine(
                    p1 = startOffset,
                    p2 = endOffset,
                    paint = paint,
                )
            }
        }
    }
}

private fun View.announceProgressForAccessibility(progress: Int) {
    if (progress == 0) {
        @Suppress("DEPRECATION")
        announceForAccessibility(
            context.getString(R.string.mozac_compose_base_progress_loading),
        )
    }
}

@PreviewLightDark
@Composable
@Suppress("MagicNumber")
private fun AnimatedProgressBarPreview() {
    AcornTheme {
        Column(
            modifier = Modifier
                .background(AcornTheme.colors.layer1)
                .height(60.dp)
                .fillMaxWidth(),
        ) {
            AnimatedProgressBar(25)

            Divider(thickness = 20.dp)

            AnimatedProgressBar(50)

            Divider(thickness = 20.dp)

            AnimatedProgressBar(75)

            Divider(thickness = 20.dp)

            AnimatedProgressBar(99)
        }
    }
}

@Preview(locale = "ar", uiMode = UI_MODE_NIGHT_YES or UI_MODE_TYPE_NORMAL)
@Preview(locale = "ar")
@Composable
@Suppress("MagicNumber")
private fun AnimatedProgressBarRTLPreview() {
    AcornTheme {
        Column(
            modifier = Modifier
                .background(AcornTheme.colors.layer1)
                .height(60.dp)
                .fillMaxWidth(),
        ) {
            AnimatedProgressBar(25)

            Divider(thickness = 20.dp)

            AnimatedProgressBar(50)

            Divider(thickness = 20.dp)

            AnimatedProgressBar(75)

            Divider(thickness = 20.dp)

            AnimatedProgressBar(99)
        }
    }
}
