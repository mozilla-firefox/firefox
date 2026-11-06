/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.lens

import android.content.Context
import android.view.TextureView
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import mozilla.components.compose.base.button.IconButton
import mozilla.components.feature.qr.R as qrR
import mozilla.components.ui.colors.NovaColors
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

private val ShutterButtonSize = 64.dp
private val ButtonSize = 48.dp
private val TopBarPadding = 24.dp
private val TopBarSpacing = 12.dp
private val CloseButtonStartPadding = 12.dp
private val TitleHorizontalPadding = ButtonSize + CloseButtonStartPadding
private val ShutterBorderWidth = 3.dp
private val ShutterBorderColor = Color.White.copy(alpha = 0.5f)

private val BottomBarPadding = 24.dp

private val BottomControlsTopPadding = 24.dp
private val BottomControlsHorizontalPadding = 32.dp

private val ToggleHeight = 40.dp
private val ToggleHalfWidth = 64.dp
private val ToggleInnerPadding = 4.dp
private val ToggleBackgroundColor = Color.Black.copy(alpha = 0.45f)
private val ToggleActiveBackgroundColor = Color.White
private val ToggleActiveIconColor = Color.Black
private val ToggleInactiveIconColor = Color.White

private val ViewfinderBorderWidth = 2.dp
private val ViewfinderBorderRadius = 12.dp
private val ViewfinderBorderColor = Color.White.copy(alpha = 0.9f)
private const val VIEWFINDER_WIDTH_FRACTION = 0.7f

/**
 * UI state for [LensCameraScreen].
 *
 * @property showError Whether to display the camera error message.
 * @property mode Active capture mode; controls which controls and overlays are shown.
 * @property previewAspectRatio Display-oriented width/height ratio for the camera preview, or null if not yet
 *   determined.
 * @property showModeToggle Whether the toggle for switching between Lens and QR modes is shown.
 */
data class LensCameraState(
    val showError: Boolean,
    val mode: CameraMode,
    val previewAspectRatio: Float?,
    val showModeToggle: Boolean = true,
)

/**
 * Camera screen for Google Lens image capture and QR scanning.
 *
 * @param state Bundled UI state for the screen.
 * @param onModeChange Invoked when the user taps the mode toggle.
 * @param onClose Callback when the close button is tapped.
 * @param onShutter Callback when the shutter button is tapped (Lens mode only).
 * @param onGallery Callback when the gallery button is tapped. Available in both Lens and QR modes; the host
 *   distinguishes the two via the active camera mode at tap time.
 * @param textureViewProvider Factory that creates the preview [TextureView]; the caller is responsible for retaining
 *   the returned reference for camera-session wiring.
 */
@Composable
fun LensCameraScreen(
    state: LensCameraState,
    onModeChange: (CameraMode) -> Unit,
    onClose: () -> Unit,
    onShutter: () -> Unit,
    onGallery: () -> Unit,
    textureViewProvider: (Context) -> TextureView,
) {
    Box(
        modifier =
            Modifier.windowInsetsPadding(WindowInsets.safeDrawing).fillMaxSize().background(Color.Black).clipToBounds()
    ) {
        val previewModifier =
            if (state.previewAspectRatio != null) {
                Modifier.align(Alignment.Center).aspectRatio(state.previewAspectRatio)
            } else {
                Modifier.fillMaxSize()
            }
        AndroidView(
            factory = textureViewProvider,
            modifier = previewModifier,
        )

        if (state.showError) {
            Text(
                text = stringResource(qrR.string.mozac_feature_qr_scanner_no_camera),
                color = NovaColors.White,
                modifier = Modifier.align(Alignment.Center),
            )
        }

        if (state.mode == CameraMode.QR) {
            QrViewfinderOverlay()
        }

        Column(
            modifier = Modifier.align(Alignment.TopCenter),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(TopBarSpacing),
        ) {
            CameraTopBar(
                mode = state.mode,
                onClose = onClose,
            )
            if (state.showModeToggle) {
                ModeToggle(
                    mode = state.mode,
                    onModeChange = onModeChange,
                )
            }
        }

        val bottomControlsModifier =
            Modifier.align(Alignment.BottomCenter)
                .windowInsetsPadding(WindowInsets.navigationBars)
                .padding(bottom = BottomBarPadding)
        when (state.mode) {
            CameraMode.LENS ->
                BottomControls(
                    onShutter = onShutter,
                    onGallery = onGallery,
                    modifier = bottomControlsModifier,
                )
            CameraMode.QR ->
                QrBottomControls(
                    onGallery = onGallery,
                    modifier = bottomControlsModifier,
                )
        }
    }
}

/**
 * Camera-less stand-in for [LensCameraScreen], showing only the Google Lens header. Used as the backdrop for
 * [GoogleLensOptOutBottomSheet] so no camera preview is started while the sheet is up.
 *
 * @param onClose Callback when the close button is tapped.
 * @param modifier The [Modifier] to be applied to this composable.
 */
@Composable
fun LensOptOutBackdrop(
    onClose: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier.windowInsetsPadding(WindowInsets.safeDrawing).fillMaxSize().background(Color.Black)) {
        CameraTopBar(
            mode = CameraMode.LENS,
            onClose = onClose,
            modifier = Modifier.align(Alignment.TopStart),
        )
    }
}

@Composable
private fun CameraTopBar(
    mode: CameraMode,
    onClose: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .windowInsetsPadding(WindowInsets.statusBars)
                .padding(top = TopBarPadding)
                .height(ButtonSize)
    ) {
        IconButton(
            onClick = onClose,
            contentDescription = stringResource(R.string.content_description_close_button),
            modifier = Modifier.align(Alignment.CenterStart).padding(start = CloseButtonStartPadding).size(ButtonSize),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_back_24),
                contentDescription = null,
                tint = NovaColors.White,
            )
        }
        Text(
            text =
                when (mode) {
                    CameraMode.LENS -> stringResource(R.string.lens_camera_title_lens)
                    CameraMode.QR -> stringResource(R.string.lens_camera_title_qr)
                },
            color = NovaColors.White,
            style = FirefoxTheme.typography.headline5,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier =
                Modifier.align(Alignment.Center).padding(horizontal = TitleHorizontalPadding).semantics { heading() },
        )
    }
}

@Composable
private fun ModeToggle(
    mode: CameraMode,
    onModeChange: (CameraMode) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier =
            modifier
                .height(ToggleHeight)
                .clip(CircleShape)
                .background(ToggleBackgroundColor)
                .padding(ToggleInnerPadding),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ToggleHalf(
            selected = mode == CameraMode.LENS,
            iconRes = iconsR.drawable.mozac_ic_logo_google_lens_24,
            contentDesc = stringResource(R.string.lens_camera_mode_lens),
            onClick = { onModeChange(CameraMode.LENS) },
        )
        ToggleHalf(
            selected = mode == CameraMode.QR,
            iconRes = iconsR.drawable.mozac_ic_qr_code_24,
            contentDesc = stringResource(R.string.lens_camera_mode_qr),
            onClick = { onModeChange(CameraMode.QR) },
        )
    }
}

@Composable
private fun ToggleHalf(
    selected: Boolean,
    iconRes: Int,
    contentDesc: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val background = if (selected) ToggleActiveBackgroundColor else Color.Transparent
    val tint = if (selected) ToggleActiveIconColor else ToggleInactiveIconColor
    Box(
        modifier =
            modifier
                .width(ToggleHalfWidth)
                .fillMaxHeight()
                .clip(CircleShape)
                .background(background)
                .selectable(selected = selected, role = Role.Tab, onClick = onClick)
                .semantics(mergeDescendants = true) {
                    this.contentDescription = contentDesc
                },
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            painter = painterResource(iconRes),
            contentDescription = null,
            tint = tint,
        )
    }
}

@Composable
private fun QrViewfinderOverlay(modifier: Modifier = Modifier) {
    Box(modifier = modifier.fillMaxSize()) {
        Box(
            modifier =
                Modifier.align(Alignment.Center)
                    .fillMaxWidth(VIEWFINDER_WIDTH_FRACTION)
                    .aspectRatio(1f)
                    .border(
                        width = ViewfinderBorderWidth,
                        color = ViewfinderBorderColor,
                        shape = RoundedCornerShape(ViewfinderBorderRadius),
                    )
        )
    }
}

@Composable
private fun BottomControlsScaffold(
    modifier: Modifier = Modifier,
    horizontalArrangement: Arrangement.Horizontal = Arrangement.Start,
    content: @Composable RowScope.() -> Unit,
) {
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(
                    top = BottomControlsTopPadding,
                    start = BottomControlsHorizontalPadding,
                    end = BottomControlsHorizontalPadding,
                ),
        horizontalArrangement = horizontalArrangement,
        verticalAlignment = Alignment.CenterVertically,
        content = content,
    )
}

@Composable
private fun GalleryButton(
    onGallery: () -> Unit,
    modifier: Modifier = Modifier,
) {
    IconButton(
        onClick = onGallery,
        contentDescription = stringResource(R.string.content_description_gallery),
        modifier = Modifier.size(ButtonSize).then(modifier),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_image_24),
            contentDescription = null,
            tint = NovaColors.White,
        )
    }
}

@Composable
private fun QrBottomControls(
    onGallery: () -> Unit,
    modifier: Modifier = Modifier,
) {
    BottomControlsScaffold(
        modifier = modifier,
        horizontalArrangement = Arrangement.Center,
    ) {
        GalleryButton(onGallery = onGallery)
    }
}

@Composable
private fun BottomControls(
    onShutter: () -> Unit,
    onGallery: () -> Unit,
    modifier: Modifier = Modifier,
) {
    BottomControlsScaffold(modifier = modifier) {
        GalleryButton(
            onGallery = onGallery,
            modifier = Modifier.weight(1f),
        )

        IconButton(
            onClick = onShutter,
            contentDescription = stringResource(R.string.content_description_take_photo),
            modifier =
                Modifier.size(ShutterButtonSize)
                    .border(ShutterBorderWidth, ShutterBorderColor, CircleShape)
                    .background(Color.White, CircleShape),
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_search_24),
                contentDescription = null,
                tint = Color.Black,
            )
        }

        Spacer(modifier = Modifier.size(ButtonSize).weight(1f))
    }
}
