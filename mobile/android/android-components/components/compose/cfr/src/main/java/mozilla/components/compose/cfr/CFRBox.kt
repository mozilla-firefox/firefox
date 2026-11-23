/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.cfr

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.sizeIn
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TooltipBox
import androidx.compose.material3.TooltipDefaults
import androidx.compose.material3.TooltipScope
import androidx.compose.material3.TooltipState
import androidx.compose.material3.rememberTooltipState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Matrix
import androidx.compose.ui.graphics.Outline
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathOperation
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.MeasureScope
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.PopupPositionProvider
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.cfr.CFRPopup.IndicatorDirection
import mozilla.components.ui.icons.R as iconsR

// Typealiases for a "CFR" API

@OptIn(ExperimentalMaterial3Api::class) // tooltip state
typealias CFRState = TooltipState

@OptIn(ExperimentalMaterial3Api::class) // tooltip scope
typealias CFRScope = TooltipScope

/**
 * Create and remember [CFRPositionProvider] for [CFRBox]
 *
 * @param indicatorDirection The direction of the CFR indicator
 * @param spacingBetweenCfrAndAnchor Spacing between CFR tooltip and the anchor. Defaults to `4.dp`
 */
@Composable
fun rememberCFRPositionProvider(
    indicatorDirection: IndicatorDirection,
    spacingBetweenCfrAndAnchor: Dp = 4.dp,
): CFRPositionProvider {
    val anchorSpacing = with(LocalDensity.current) { spacingBetweenCfrAndAnchor.roundToPx() }
    return remember(anchorSpacing, indicatorDirection) {
        CFRPositionProviderImpl(
            indicatorDirection,
            anchorSpacing,
        )
    }
}

/** Create and remember [CFRState] for [CFRBox] */
@Composable
@ExperimentalMaterial3Api // rememberTooltipState
fun rememberCFRState(
    initialIsVisible: Boolean = false,
    isPersistent: Boolean = true,
): CFRState = rememberTooltipState(initialIsVisible = initialIsVisible, isPersistent = isPersistent)

/**
 * A CFR (Contextual Feature Recommendation) container built on Material3's TooltipBox.
 *
 * NOTE: This component is meant for use in 100% compose situations. If you need to do a compose-view interop, for
 * example, if your anchor is a view, consider using [CFRPopupLayout].
 *
 * @param positionProvider Controls tooltip placement relative to [anchor].
 * @param cfr The CFR tooltip content, scoped to [CFRScope] so it can use [CFR].
 * @param state Visibility state of the CFR tooltip.
 * @param modifier Modifier for the component.
 * @param onDismissRequest Called when the user taps outside the CFR tooltip.
 * @param focusable Whether the CFR tooltip consumes touch events while shown. If this is set to true, it allows the
 *   tooltip to handle events like back button presses, and it will get dismissed on back-pressed. If you wish to handle
 *   back-press to dismiss the CFR on your own, then set [focusable] to false.
 * @param anchor The anchor composable the CFR tooltip is attached to.
 */
@OptIn(ExperimentalMaterial3Api::class) // TooltipBox
@Composable
fun CFRBox(
    cfr: @Composable CFRScope.() -> Unit,
    state: CFRState,
    modifier: Modifier = Modifier,
    positionProvider: CFRPositionProvider = rememberCFRPositionProvider(IndicatorDirection.UP),
    onDismissRequest: (() -> Unit)? = null,
    focusable: Boolean = true,
    anchor: @Composable () -> Unit,
) {
    TooltipBox(
        positionProvider = positionProvider,
        tooltip = cfr,
        state = state,
        modifier = modifier,
        onDismissRequest = onDismissRequest,
        focusable = focusable,
        enableUserInput = true,
        hasAction = false,
        content = anchor,
    )
}

/**
 * CFR container content with a gradient background, optional title, text, and dismiss button.
 *
 * NOTE: This component is meant for use in 100% compose situations. If you need to do a compose-view interop, for
 * example, if your anchor is a view, consider using [CFRPopupLayout].
 *
 * @param modifier Modifier for the tooltip root.
 * @param title Optional title composable shown above [text].
 * @param showDismissButton Whether to show the "X" dismiss button.
 * @param onDismiss Called when the dismiss button is tapped.
 * @param colors Colors applied to the background, text, title, action, and the dismiss button.
 * @param text The main text content of the tooltip.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CFRScope.CFR(
    modifier: Modifier = Modifier,
    title: (@Composable () -> Unit)? = null,
    showDismissButton: Boolean = true,
    onDismiss: () -> Unit = {},
    colors: CFRColors = CFRDefaults.colors(),
    text: @Composable () -> Unit,
) {
    val indicatorShape: Shape = CFRDefaults.cfrIndicatorShape()
    val containerShape: Shape = CFRDefaults.cfrShape()

    // a significant portion of this code was adapted from
    // https://cs.android.com/androidx/platform/frameworks/support/+/androidx-main:compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/Tooltip.kt;l=398;drc=fdebffc9bad9af1be191ef8a8dd422d1ec34ee8a
    val transformationMatrix = remember { mutableStateOf(Matrix()) }
    val density = LocalDensity.current
    val windowContainerSize = LocalWindowInfo.current.containerSize

    val resolvedShape: Shape =
        remember(containerShape, indicatorShape) {
            CFRShape(transformationMatrix, containerShape, indicatorShape)
        }

    val backgroundBrush =
        when (val background = colors.background) {
            is CFRBackground.Gradient -> background.brush
            is CFRBackground.Colors ->
                Brush.linearGradient(
                    colors = background.colors,
                    start = Offset(Float.POSITIVE_INFINITY, 0f),
                    end = Offset(0f, Float.POSITIVE_INFINITY),
                )
        }

    CFRContentLayout(
        modifier =
            Modifier.layoutIndicator(
                    transformationMatrix = transformationMatrix,
                    density = density,
                    windowContainerSize = windowContainerSize,
                    getAnchorLayoutCoordinates = { obtainAnchorBounds() },
                    positionProvider = obtainPositionProvider() as CFRPositionProvider,
                )
                .then(modifier)
                .sizeIn(
                    minWidth = CFR_MIN_WIDTH,
                    maxWidth = CFR_MAX_WIDTH,
                    minHeight = CFR_MIN_HEIGHT,
                )
                .background(
                    brush = backgroundBrush,
                    shape = resolvedShape,
                )
                .clip(resolvedShape),
        title = title,
        showDismissButton = showDismissButton,
        onDismiss = onDismiss,
        colors = colors,
        text = text,
    )
}

@Composable
private fun CFRContentLayout(
    modifier: Modifier = Modifier,
    title: (@Composable () -> Unit)? = null,
    showDismissButton: Boolean,
    onDismiss: () -> Unit,
    colors: CFRColors,
    text: @Composable () -> Unit,
) {
    Row(modifier) {
        Column(modifier = Modifier.padding(16.dp).weight(1f)) {
            title?.let {
                CompositionLocalProvider(
                    LocalContentColor provides colors.titleContentColor,
                    LocalTextStyle provides AcornTheme.typography.headline8,
                    content = it,
                )
                Spacer(modifier = Modifier.size(8.dp))
            }

            CompositionLocalProvider(
                LocalContentColor provides colors.contentColor,
                LocalTextStyle provides AcornTheme.typography.body2,
                content = text,
            )
        }

        if (showDismissButton) {
            Box(contentAlignment = Alignment.TopEnd) {
                IconButton(
                    onClick = { onDismiss() },
                    contentDescription = stringResource(R.string.mozac_cfr_dismiss_button_content_description),
                    modifier =
                        Modifier.semantics {
                            testTagsAsResourceId = true
                            testTag = "cfr.dismiss"
                        },
                ) {
                    Icon(
                        painter = painterResource(iconsR.drawable.mozac_ic_cross_20),
                        contentDescription = null,
                        tint = colors.dismissButtonColor,
                    )
                }
            }
        }
    }
}

// region Caret

/** Modifier that computes the indicator transformation matrix based on anchor position. */
private fun Modifier.layoutIndicator(
    transformationMatrix: MutableState<Matrix>,
    density: Density,
    windowContainerSize: IntSize,
    getAnchorLayoutCoordinates: MeasureScope.() -> LayoutCoordinates?,
    positionProvider: CFRPositionProvider,
): Modifier =
    this.layout { measurables, constraints ->
        val placeable = measurables.measure(constraints)
        val width = placeable.width
        val height = placeable.height
        val windowContainerWidthInPx = windowContainerSize.width
        val windowContainerHeightInPx = windowContainerSize.height
        val cfrWidth = width.toFloat()
        val cfrHeight = height.toFloat()
        val anchorLayoutCoordinates = getAnchorLayoutCoordinates()

        if (anchorLayoutCoordinates != null) {
            val screenWidthPx: Int
            val tooltipAnchorSpacing: Int
            with(density) {
                screenWidthPx = windowContainerWidthInPx
                tooltipAnchorSpacing = positionProvider.tooltipAnchorSpacing(density = this)
            }
            val anchorBounds = anchorLayoutCoordinates.boundsInWindow()
            val anchorTop = anchorBounds.top
            val anchorBottom = anchorBounds.bottom

            val indicatorY =
                when (positionProvider.indicatorDirection) {
                    IndicatorDirection.UP -> {
                        calculateIndicatorYPositionForUpDirection(
                            anchorBottom = anchorBottom,
                            cfrHeight = cfrHeight,
                            tooltipAnchorSpacing = tooltipAnchorSpacing,
                            windowContainerHeightInPx = windowContainerHeightInPx,
                        )
                    }

                    IndicatorDirection.DOWN -> {
                        calculateIndicatorYPositionForDownDirection(
                            anchorTop = anchorTop,
                            cfrHeight = cfrHeight,
                            tooltipAnchorSpacing = tooltipAnchorSpacing,
                        )
                    }
                }

            val position =
                Offset(
                    x = indicatorX(cfrWidth, screenWidthPx, anchorBounds),
                    y = indicatorY,
                )

            // Translate matrix to position
            val matrix = Matrix()
            matrix.translate(x = position.x, y = position.y)

            // We rotate matrix depending on positioning of the tooltip
            if (indicatorY == 0f) {
                // caret needs to be placed above tooltip
                // Need to rotate it about the x axis by 180 degrees
                matrix.rotateX(CARET_FLIP_DEGREES)
            }
            transformationMatrix.value = matrix
        }
        layout(width, height) { placeable.place(0, 0) }
    }

private fun calculateIndicatorYPositionForDownDirection(
    anchorTop: Float,
    cfrHeight: Float,
    tooltipAnchorSpacing: Int,
): Float =
    if (anchorTop - cfrHeight - tooltipAnchorSpacing < 0) {
        0f
    } else {
        cfrHeight
    }

private fun calculateIndicatorYPositionForUpDirection(
    anchorBottom: Float,
    cfrHeight: Float,
    tooltipAnchorSpacing: Int,
    windowContainerHeightInPx: Int,
): Float =
    if (anchorBottom + cfrHeight + tooltipAnchorSpacing > windowContainerHeightInPx) {
        cfrHeight
    } else {
        0f
    }

private fun PopupPositionProvider.tooltipAnchorSpacing(density: Density): Int {
    return (this as? CFRPositionProviderImpl)?.cfrAnchorSpacing ?: with(density) { 4.dp.roundToPx() }
}

internal fun indicatorX(tooltipWidth: Float, screenWidthPx: Int, anchorBounds: Rect): Float {
    val anchorLeft = anchorBounds.left
    val anchorRight = anchorBounds.right
    val anchorMid = (anchorLeft + anchorRight) / 2
    return if (tooltipWidth >= screenWidthPx) {
        // Tooltip is greater than or equal to the width of the screen
        // The horizontal placement just needs to be in the center of the anchor
        anchorMid
    } else if (anchorMid - tooltipWidth / 2 < 0) {
        // The tooltip needs to be start aligned if it would
        // collide with the left side of screen when attempting to center.
        // We have a horizontal correction for the caret if the tooltip will
        // also collide with the right edge of the screen when start aligned
        val horizontalCorrection = maxOf(tooltipWidth - screenWidthPx, -anchorLeft)
        anchorMid + horizontalCorrection
    } else if (anchorMid + tooltipWidth / 2 > screenWidthPx) {
        // The tooltip needs to be end aligned if it would
        // collide with the right side of the screen when attempting to center.
        // We have a horizontal correction for the caret if the tooltip will
        // also collide with the left edge of the screen when end aligned
        val horizontalCorrection = minOf(tooltipWidth - anchorRight, 0f)
        anchorMid + horizontalCorrection
    } else {
        // Tooltip can centered neatly without colliding with screen edge
        tooltipWidth / 2
    }
}

/** Shape that combines the tooltip container shape with an indicator shape, transformed by [transformationMatrix]. */
private class CFRShape(
    private val transformationMatrix: MutableState<Matrix>,
    private val cfrContainerShape: Shape,
    private val indicatorShape: Shape,
) : Shape {
    private val tooltipPath = Path()
    private val combinedPath = Path()
    private val caretPath = Path()

    override fun createOutline(
        size: Size,
        layoutDirection: LayoutDirection,
        density: Density,
    ): Outline {
        tooltipPath.reset()
        combinedPath.reset()
        caretPath.reset()

        val tooltipOutline = cfrContainerShape.createOutline(size, layoutDirection, density)
        val caretOutline = indicatorShape.createOutline(size, layoutDirection, density)

        when (tooltipOutline) {
            is Outline.Generic -> tooltipPath.addPath(tooltipOutline.path)
            is Outline.Rounded -> tooltipPath.addRoundRect(tooltipOutline.roundRect)
            is Outline.Rectangle -> tooltipPath.addRect(tooltipOutline.rect)
        }

        when (caretOutline) {
            is Outline.Generic -> caretPath.addPath(caretOutline.path)
            is Outline.Rounded -> caretPath.addRoundRect(caretOutline.roundRect)
            is Outline.Rectangle -> caretPath.addRect(caretOutline.rect)
        }

        caretPath.transform(transformationMatrix.value)
        combinedPath.op(path1 = tooltipPath, path2 = caretPath, operation = PathOperation.Union)

        return Outline.Generic(combinedPath)
    }
}

/** The [PopupPositionProvider] for CFRs that helps us place it correctly for the anchor */
sealed interface CFRPositionProvider : PopupPositionProvider {

    /** The [IndicatorDirection] for the CFR */
    val indicatorDirection: IndicatorDirection

    /** The spacing between the indicator and the anchor */
    val cfrAnchorSpacing: Int
}

private class CFRPositionProviderImpl(
    override val indicatorDirection: IndicatorDirection,
    override val cfrAnchorSpacing: Int,
) : CFRPositionProvider {
    override fun calculatePosition(
        anchorBounds: IntRect,
        windowSize: IntSize,
        layoutDirection: LayoutDirection,
        popupContentSize: IntSize,
    ): IntOffset {
        return when (indicatorDirection) {
            IndicatorDirection.UP -> belowPositioning(anchorBounds, popupContentSize, windowSize)
            IndicatorDirection.DOWN -> abovePositioning(anchorBounds, popupContentSize, windowSize)
        }
    }

    private fun abovePositioning(
        anchorBounds: IntRect,
        popupContentSize: IntSize,
        windowSize: IntSize,
    ): IntOffset {
        // Horizontal alignment preference: middle -> start -> end
        // Vertical preference: above -> below

        // Tooltip prefers to be center aligned horizontally.
        var x = anchorBounds.left + (anchorBounds.width - popupContentSize.width) / 2

        if (x < 0) {
            // Make tooltip start aligned if colliding with the
            // left side of the screen
            x = anchorBounds.left
        } else if (x + popupContentSize.width > windowSize.width) {
            // Make tooltip end aligned if colliding with the
            // right side of the screen
            x = anchorBounds.right - popupContentSize.width
        }

        // Tooltip prefers to be above the anchor,
        // but if this causes the tooltip to overlap with the anchor
        // then we place it below the anchor
        var y = anchorBounds.top - popupContentSize.height - cfrAnchorSpacing
        if (y < 0) y = anchorBounds.bottom + cfrAnchorSpacing
        return IntOffset(x, y)
    }

    private fun belowPositioning(
        anchorBounds: IntRect,
        popupContentSize: IntSize,
        windowSize: IntSize,
    ): IntOffset {
        // Horizontal alignment preference: middle -> start -> end
        // Vertical preference: below -> above

        // Tooltip prefers to be center aligned horizontally.
        var x = anchorBounds.left + (anchorBounds.width - popupContentSize.width) / 2

        if (x < 0) {
            // Make tooltip start aligned if colliding with the
            // left side of the screen
            x = anchorBounds.left
        } else if (x + popupContentSize.width > windowSize.width) {
            // Make tooltip end aligned if colliding with the
            // right side of the screen
            x = anchorBounds.right - popupContentSize.width
        }

        // Tooltip prefers to be below the anchor,
        // but if this causes the tooltip to overlap with the anchor
        // then we place it above the anchor
        var y = anchorBounds.bottom + cfrAnchorSpacing
        if (y + popupContentSize.height > windowSize.height) {
            y = anchorBounds.top - popupContentSize.height - cfrAnchorSpacing
        }
        return IntOffset(x, y)
    }
}

// endregion

// region Constants

private const val CARET_FLIP_DEGREES = 180f
private val CFR_MIN_WIDTH = 40.dp
private val CFR_MAX_WIDTH = 320.dp
private val CFR_MIN_HEIGHT = 24.dp

// endregion

/** The background painted in the [CFR]. */
sealed interface CFRBackground {

    /** One or more [colors] painted as a linear gradient. A single color renders as a solid fill. */
    data class Colors(val colors: List<Color>) : CFRBackground

    /** A [brush] painted directly as the background. */
    data class Gradient(val brush: Brush) : CFRBackground
}

/**
 * Color configuration for [CFR].
 *
 * @property background The [CFRBackground] painted in the CFR content.
 * @property contentColor Color for the main text.
 * @property titleContentColor Color for the title text.
 * @property dismissButtonColor Tint for the dismiss "X" icon.
 */
@Immutable
data class CFRColors(
    val background: CFRBackground,
    val contentColor: Color,
    val titleContentColor: Color,
    val dismissButtonColor: Color,
)

/** Default values for CFRs */
object CFRDefaults {

    /** The indicator shape for the CFR. Defaults to a caret */
    @OptIn(ExperimentalMaterial3Api::class)
    @Composable
    fun cfrIndicatorShape(): Shape {
        return TooltipDefaults.caretShape()
    }

    /** The shape of the CFR */
    @Composable
    fun cfrShape(): Shape {
        return MaterialTheme.shapes.medium
    }

    /** Default colors for the CFR */
    @Composable
    fun colors(
        background: CFRBackground = CFRBackground.Gradient(brush = AcornTheme.gradients.cfr.brush),
        contentColor: Color = Color.White,
        titleContentColor: Color = Color.White,
        dismissButtonColor: Color = Color.White,
    ) =
        CFRColors(
            background = background,
            contentColor = contentColor,
            titleContentColor = titleContentColor,
            dismissButtonColor = dismissButtonColor,
        )
}

// region Previews

@Composable
@PreviewLightDark
private fun CFRWithTitlePreview() {
    AcornTheme {
        Surface {
            Box(
                modifier = Modifier.fillMaxSize().padding(16.dp),
                contentAlignment = Alignment.Center,
            ) {
                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    SampleButtonWithCFR(buttonText = "Button 1")
                    SampleButtonWithCFR(
                        direction = IndicatorDirection.DOWN,
                        buttonText = "Button 2",
                    )
                    SampleButtonWithCFR(buttonText = "Button 3")
                    SampleButtonWithCFR(showDismissButton = false, buttonText = "Button 4")
                    SampleButtonWithCFR(buttonText = "Button 5")
                    SampleButtonWithCFR(buttonText = "Button 6")
                }
            }
        }
    }
}

@Composable
@OptIn(ExperimentalMaterial3Api::class)
private fun SampleButtonWithCFR(
    scope: CoroutineScope = rememberCoroutineScope(),
    state: CFRState = rememberCFRState(),
    direction: IndicatorDirection = IndicatorDirection.UP,
    showDismissButton: Boolean = true,
    buttonText: String,
) {
    CFRBox(
        positionProvider = rememberCFRPositionProvider(direction),
        cfr = {
            CFR(
                title = { Text("Did you know?") },
                text = { Text("Lorem ipsum dolor sit amet") },
                showDismissButton = showDismissButton,
                onDismiss = {
                    scope.launch {
                        state.dismiss()
                    }
                },
            )
        },
        state = state,
    ) {
        FilledButton(
            text = buttonText,
            onClick = {
                scope.launch {
                    state.show()
                }
            },
        )
    }

    DisposableEffect(Unit) {
        onDispose {
            state.onDispose()
        }
    }
}

// endregion
