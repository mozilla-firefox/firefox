/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui.gradient

import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.BlendMode

internal object BlobAnimation {
    const val PHASE_1_TO_2_DURATION_MS = 4000f
    const val PHASE_2_TO_3_DURATION_MS = 4000f
    const val PHASE_3_TO_1_DURATION_MS = 4000f
    const val TOTAL_DURATION_MS =
        PHASE_1_TO_2_DURATION_MS + PHASE_2_TO_3_DURATION_MS + PHASE_3_TO_1_DURATION_MS
    val PHASE_EASING = CubicBezierEasing(0.42f, 0f, 0.58f, 1f)
}

internal data class BlobSpec(
    val pathData: String,
    val originalWidthDp: Float,
    val widthDp: Float,
    val rotationDeg: Float,
)

internal data class BlobPose(
    val offsetDp: Offset,
    val widthDp: Float,
)

internal data class BlobPhases(
    val phase1: BlobPose,
    val phase2: BlobPose,
    val phase3: BlobPose,
)

internal val DARK_PURPLE_BLOB = BlobSpec(
    pathData = BlobPaths.DARK_PURPLE,
    originalWidthDp = 343f,
    widthDp = 343f,
    rotationDeg = 0f,
)

internal val LIGHT_PURPLE_BLOB = BlobSpec(
    pathData = BlobPaths.LIGHT_PURPLE,
    originalWidthDp = 424f,
    widthDp = 439f,
    rotationDeg = 120f,
)

internal val ORANGE_BLOB = BlobSpec(
    pathData = BlobPaths.ORANGE,
    originalWidthDp = 239f,
    widthDp = 269.225f,
    rotationDeg = 240f,
)

internal val DARK_PURPLE_G1_PHASES = BlobPhases(
    phase1 = BlobPose(offsetDp = Offset(135f, 159f), widthDp = 343f),
    phase2 = BlobPose(offsetDp = Offset(19f, 16f), widthDp = 343f),
    phase3 = BlobPose(offsetDp = Offset(-21f, 188f), widthDp = 271f),
)

internal val LIGHT_PURPLE_G1_PHASES = BlobPhases(
    phase1 = BlobPose(offsetDp = Offset(421.225f, 4f), widthDp = 439f),
    phase2 = BlobPose(offsetDp = Offset(220f, 143f), widthDp = 439f),
    phase3 = BlobPose(offsetDp = Offset(167f, 188f), widthDp = 405f),
)

internal val ORANGE_G1_PHASES = BlobPhases(
    phase1 = BlobPose(offsetDp = Offset(373f, -27.322f), widthDp = 269.225f),
    phase2 = BlobPose(offsetDp = Offset(-10.225f, -13.322f), widthDp = 269.225f),
    phase3 = BlobPose(offsetDp = Offset(491f, -1f), widthDp = 277f),
)

internal val DARK_PURPLE_G2_PHASES = BlobPhases(
    phase1 = DARK_PURPLE_G1_PHASES.phase1,
    phase2 = BlobPose(offsetDp = Offset(288f, 214f), widthDp = 259f),
    phase3 = BlobPose(offsetDp = Offset(-21f, -1f), widthDp = 259f),
)

internal val LIGHT_PURPLE_G2_PHASES = BlobPhases(
    phase1 = LIGHT_PURPLE_G1_PHASES.phase1,
    phase2 = BlobPose(offsetDp = Offset(242f, 155f), widthDp = 439f),
    phase3 = BlobPose(offsetDp = Offset(9f, 35f), widthDp = 671f),
)

internal val ORANGE_G2_PHASES = BlobPhases(
    phase1 = ORANGE_G1_PHASES.phase1,
    phase2 = BlobPose(offsetDp = Offset(-61.225f, -13.322f), widthDp = 269.225f),
    phase3 = BlobPose(offsetDp = Offset(208f, 4f), widthDp = 195f),
)

internal object CircleAnimation {
    const val PHASE_1_TO_2_DURATION_MS = 1000f
    const val PHASE_2_TO_3_DURATION_MS = 800f
    const val PHASE_3_TO_4_DURATION_MS = 1200f
    const val PHASE_4_TO_1_DURATION_MS = 900f
    const val TOTAL_DURATION_MS =
        PHASE_1_TO_2_DURATION_MS + PHASE_2_TO_3_DURATION_MS +
            PHASE_3_TO_4_DURATION_MS + PHASE_4_TO_1_DURATION_MS

    val PHASE_1 = CirclePhase(
        sizeDp = 548f,
        alpha = 0.45f,
        blendMode = BlendMode.SrcOver,
        leftDp = -5f,
        topDp = 15f,
    )
    val PHASE_2 = CirclePhase(
        sizeDp = 464.793f,
        alpha = 0.45f,
        blendMode = BlendMode.Screen,
        leftDp = -205f,
        topDp = -23f,
    )
    val PHASE_3 = CirclePhase(
        sizeDp = 464.793f,
        alpha = 0.45f,
        blendMode = BlendMode.SrcOver,
        leftDp = -133f,
        topDp = -276f,
    )
    val PHASE_4 = CirclePhase(
        sizeDp = 464.793f,
        alpha = 0.50f,
        blendMode = BlendMode.Screen,
        rightDp = -170.793f,
        topDp = -251f,
    )
}

internal data class CirclePhase(
    val sizeDp: Float,
    val alpha: Float,
    val blendMode: BlendMode,
    val leftDp: Float? = null,
    val rightDp: Float? = null,
    val topDp: Float? = null,
    val bottomDp: Float? = null,
)
