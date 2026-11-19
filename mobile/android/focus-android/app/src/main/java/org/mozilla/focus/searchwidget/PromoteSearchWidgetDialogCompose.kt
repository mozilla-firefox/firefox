/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.searchwidget

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import mozilla.components.compose.base.button.IconButton
import mozilla.components.ui.colors.PhotonColors
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.focus.R
import org.mozilla.focus.ui.theme.FocusTheme
import org.mozilla.focus.ui.theme.focusColors
import org.mozilla.focus.ui.theme.focusDimensions
import org.mozilla.focus.ui.theme.focusTypography

@Composable
@Preview
private fun PromoteSearchWidgetDialogComposePreview() {
    FocusTheme {
        PromoteSearchWidgetDialogCompose({}, {})
    }
}

/**
 * Composable that displays a dialog to promote the search widget.
 *
 * This dialog informs the user about the search widget and provides an option to add it.
 *
 * @param onAddSearchWidgetButtonClick A lambda function to be executed when the "Add Widget" button is clicked.
 * @param onDismiss A lambda function to be executed when the dialog is dismissed.
 */
@Composable
fun PromoteSearchWidgetDialogCompose(
    onAddSearchWidgetButtonClick: () -> Unit,
    onDismiss: () -> Unit,
) {
    val openDialog = remember { mutableStateOf(true) }
    if (openDialog.value) {
        Dialog(
            onDismissRequest = {
                onDismiss()
            },
            DialogProperties(dismissOnBackPress = true, dismissOnClickOutside = false),
        ) {
            Column(
                modifier =
                    Modifier.clip(RoundedCornerShape(20.dp))
                        .wrapContentSize()
                        .background(colorResource(id = R.color.promote_search_widget_dialog_background)),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Box(
                    modifier =
                        Modifier.align(Alignment.End)
                            .padding(top = focusDimensions.paddingSmall, end = focusDimensions.paddingDefault)
                ) {
                    CloseButton(openDialog, onDismiss)
                }
                DialogTitle()
                DialogSubtitle()
                DialogImage()
                ComponentAddWidgetButton(onAddSearchWidgetButtonClick, onDismiss, openDialog)
            }
        }
    }
}

@Composable
private fun DialogTitle() {
    Text(
        text = stringResource(id = R.string.promote_search_widget_dialog_title),
        modifier = Modifier.padding(focusDimensions.paddingDefault),
        color = focusColors.dialogTextColor,
        textAlign = TextAlign.Center,
        style = focusTypography.dialogTitle,
    )
}

@Composable
private fun DialogSubtitle() {
    Text(
        text =
            stringResource(
                id = R.string.promote_search_widget_dialog_subtitle,
                stringResource(R.string.onboarding_short_app_name),
            ),
        modifier =
            Modifier.padding(
                top = focusDimensions.paddingDefault,
                start = focusDimensions.paddingDefault,
                end = focusDimensions.paddingDefault,
            ),
        color = focusColors.dialogTextColor,
        textAlign = TextAlign.Center,
        style = focusTypography.dialogContent,
    )
}

@Composable
private fun DialogImage() {
    Image(
        painter = painterResource(R.drawable.focus_search_widget_promote_dialog),
        contentDescription = stringResource(R.string.promote_search_widget_dialog_picture_content_description),
        modifier =
            Modifier.fillMaxWidth()
                .padding(start = focusDimensions.paddingText, end = focusDimensions.paddingText)
                .background(colorResource(id = R.color.promote_search_widget_dialog_background)),
    )
}

@Composable
private fun ComponentAddWidgetButton(
    onAddSearchWidgetButtonClick: () -> Unit,
    onDismiss: () -> Unit,
    openState: MutableState<Boolean>,
) {
    Button(
        onClick = {
            openState.value = false
            onAddSearchWidgetButtonClick()
            onDismiss()
        },
        modifier = Modifier.fillMaxWidth().padding(focusDimensions.paddingLarge),
        shape = RoundedCornerShape(8.dp),
        colors =
            ButtonDefaults.textButtonColors(
                containerColor = colorResource(R.color.promote_search_widget_dialog_add_widget_button_background)
            ),
    ) {
        Text(
            text = AnnotatedString(stringResource(id = R.string.promote_search_widget_button_text)),
            color = PhotonColors.White,
        )
    }
}

@Composable
private fun CloseButton(
    openState: MutableState<Boolean>,
    onDismiss: () -> Unit,
) {
    IconButton(
        onClick = {
            onDismiss()
            openState.value = false
        },
        contentDescription = stringResource(id = R.string.promote_search_widget_dialog_content_description),
        modifier =
            Modifier.background(
                    colorResource(id = R.color.promote_search_widget_dialog_close_button_background),
                    shape = CircleShape,
                )
                .size(48.dp)
                .padding(focusDimensions.paddingText),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_cross_24),
            contentDescription = null,
            tint = focusColors.closeIcon,
        )
    }
}
