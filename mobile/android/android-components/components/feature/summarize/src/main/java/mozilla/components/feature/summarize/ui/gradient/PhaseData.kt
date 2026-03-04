/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.ui.gradient

import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.ui.geometry.Offset

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

// G1 (Group 3) is static — same position at all keyframes.
private val DARK_PURPLE_G1_POSE = BlobPose(offsetDp = Offset(135f, 159f), widthDp = 343f)

internal val DARK_PURPLE_G1_PHASES = BlobPhases(
    phase1 = DARK_PURPLE_G1_POSE,
    phase2 = DARK_PURPLE_G1_POSE,
    phase3 = DARK_PURPLE_G1_POSE,
)

private val LIGHT_PURPLE_G1_POSE = BlobPose(offsetDp = Offset(-15f, 4f), widthDp = 439f)

internal val LIGHT_PURPLE_G1_PHASES = BlobPhases(
    phase1 = LIGHT_PURPLE_G1_POSE,
    phase2 = LIGHT_PURPLE_G1_POSE,
    phase3 = LIGHT_PURPLE_G1_POSE,
)

private val ORANGE_G1_POSE = BlobPose(offsetDp = Offset(373f, -27f), widthDp = 269.225f)

internal val ORANGE_G1_PHASES = BlobPhases(
    phase1 = ORANGE_G1_POSE,
    phase2 = ORANGE_G1_POSE,
    phase3 = ORANGE_G1_POSE,
)

// G2 (Group 2) animates. Phase 1 overlaps G1; phases 2/3 from Figma variants 6/4.
// Positions mapped by COLOR (where each color appears in the Figma keyframe).
internal val DARK_PURPLE_G2_PHASES = BlobPhases(
    phase1 = DARK_PURPLE_G1_POSE,
    phase2 = BlobPose(offsetDp = Offset(220f, 143f), widthDp = 439f),
    phase3 = BlobPose(offsetDp = Offset(-63f, 156f), widthDp = 271f),
)

internal val LIGHT_PURPLE_G2_PHASES = BlobPhases(
    phase1 = LIGHT_PURPLE_G1_POSE,
    phase2 = BlobPose(offsetDp = Offset(19f, 16f), widthDp = 343f),
    phase3 = BlobPose(offsetDp = Offset(125f, 156f), widthDp = 405f),
)

internal val ORANGE_G2_PHASES = BlobPhases(
    phase1 = ORANGE_G1_POSE,
    phase2 = BlobPose(offsetDp = Offset(-10f, -13f), widthDp = 372f),
    phase3 = BlobPose(offsetDp = Offset(385f, -33f), widthDp = 341f),
)
