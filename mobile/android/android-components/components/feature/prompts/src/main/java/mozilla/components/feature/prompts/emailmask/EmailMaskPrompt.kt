/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.emailmask

import android.view.View
import android.view.ViewTreeObserver
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.minimumInteractiveComponentSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.layout.AcornWindowSize.Companion.isLargeWindow
import mozilla.components.compose.cfr.CFRPopup
import mozilla.components.compose.cfr.CFRPopupBackground
import mozilla.components.compose.cfr.CFRPopupContent
import mozilla.components.feature.prompts.R
import mozilla.components.support.utils.ext.isLandscape
import mozilla.components.ui.icons.R as iconsR

/** Offset to align the CFR indicator and the email chip. */
private val indicatorArrowStartOffset = 35.dp

/**
 * A bar for displaying email mask related actions.
 *
 * @param shouldShowCfr whether the CFR should be displayed.
 * @param cfrText the text to display in the CFR.
 * @param onCfrDismiss callback for when the CFR is dismissed.
 * @param onMaskEmailClicked a mask email chip click listener.
 * @param modifier Optional modifier for the bar.
 */
@Composable
fun EmailMaskPromptBar(
    shouldShowCfr: Boolean,
    cfrText: String,
    onCfrDismiss: () -> Unit,
    onMaskEmailClicked: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val isImeVisible = rememberImeIsVisible()

    AnimatedVisibility(
        visible = isImeVisible,
        enter = slideInVertically(initialOffsetY = { it }),
        exit = slideOutVertically(targetOffsetY = { it }),
    ) {
        var isDismissed by remember { mutableStateOf(false) }
        val canShowCfr = !isLandscapeNotTablet()

        Surface(color = Color.Transparent) {
            Column {
                AnimatedVisibility(
                    visible = shouldShowCfr && canShowCfr && !isDismissed,
                    enter = slideInVertically(initialOffsetY = { it }),
                    exit = slideOutVertically(targetOffsetY = { it }),
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Cfr(
                            onDismiss = {
                                isDismissed = true
                                onCfrDismiss()
                            },
                            cfrText = cfrText,
                        )
                    }
                }

                Surface(color = MaterialTheme.colorScheme.surface) {
                    Row(modifier = modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
                        MaskEmailChip(
                            onClick = {
                                isDismissed = true
                                onCfrDismiss()
                                onMaskEmailClicked()
                            }
                        )
                    }
                }
            }
        }
    }
}

/** Don't show the CFR on landscape unless it's at least the size of a tablet. */
@Composable
private fun isLandscapeNotTablet(): Boolean {
    val isLandscape = LocalContext.current.isLandscape()
    val isTablet = isLargeWindow()
    return isLandscape && !isTablet
}

@Composable
private fun Cfr(onDismiss: () -> Unit, cfrText: String) {
    CFRPopupContent(
        popupBodyColors = CFRPopupBackground.Gradient(brush = AcornTheme.gradients.cfr.brush),
        showDismissButton = true,
        dismissButtonColor = Color.White.toArgb(),
        indicatorDirection = CFRPopup.IndicatorDirection.DOWN,
        indicatorArrowStartOffset = indicatorArrowStartOffset,
        onDismiss = { onDismiss() },
        title = {
            Text(
                text = cfrText,
                color = Color.White,
                style = AcornTheme.typography.headline8,
            )
        },
        text = {},
        action = {},
    )
}

@Composable
private fun MaskEmailChip(onClick: () -> Unit) {
    Surface(
        modifier = Modifier.clickable(onClick = onClick).minimumInteractiveComponentSize(),
        shape = RoundedCornerShape(20.dp),
        color = MaterialTheme.colorScheme.primaryContainer,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                painter = painterResource(id = iconsR.drawable.mozac_ic_email_mask_24),
                contentDescription = null, // talkback should focus on the whole element
                modifier = Modifier.size(16.dp),
            )

            Spacer(modifier = Modifier.size(8.dp))

            Text(
                text = stringResource(id = R.string.mozac_feature_relay_chip_text),
                style = AcornTheme.typography.headline8,
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun EmailMaskPromptBarPreview() {
    AcornTheme {
        EmailMaskPromptBar(
            shouldShowCfr = true,
            cfrText =
                stringResource(
                    R.string.mozac_feature_relay_email_masks_cfr,
                    stringResource(R.string.firefox_relay),
                ),
            onCfrDismiss = {},
            onMaskEmailClicked = {},
        )
    }
}

@Composable
private fun rememberImeIsVisible(): Boolean {
    val view = LocalView.current
    var isImeVisible by remember {
        val insets = ViewCompat.getRootWindowInsets(view)
        mutableStateOf(insets?.isVisible(WindowInsetsCompat.Type.ime()) ?: false)
    }

    DisposableEffect(view) {
        val layoutListener = ViewTreeObserver.OnGlobalLayoutListener {
            val insets = ViewCompat.getRootWindowInsets(view)
            isImeVisible = insets?.isVisible(WindowInsetsCompat.Type.ime()) ?: false
        }
        var observer: ViewTreeObserver? = null
        fun startListening() {
            if (observer == null) {
                observer = view.viewTreeObserver.also { it.addOnGlobalLayoutListener(layoutListener) }
            }
        }
        fun stopListening() {
            observer?.takeIf { it.isAlive }?.removeOnGlobalLayoutListener(layoutListener)
            observer = null
        }
        // A view reaches the ViewTreeObserver of its window only while it is attached. It can leave the window before
        // this effect is disposed, and this effect can start while the view is being attached, before its attach
        // listeners are called, so the listener follows the view in and out of its window and is added only once.
        val attachListener =
            object : View.OnAttachStateChangeListener {
                override fun onViewAttachedToWindow(v: View) = startListening()

                override fun onViewDetachedFromWindow(v: View) = stopListening()
            }
        if (view.isAttachedToWindow) {
            startListening()
        }
        view.addOnAttachStateChangeListener(attachListener)

        onDispose {
            view.removeOnAttachStateChangeListener(attachListener)
            stopListening()
        }
    }

    return isImeVisible
}
