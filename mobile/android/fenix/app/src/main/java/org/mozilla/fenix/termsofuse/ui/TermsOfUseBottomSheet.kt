/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.termsofuse.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.BottomSheetDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.ModalBottomSheetProperties
import androidx.compose.material3.SheetState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Devices.TABLET
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.OutlinedButton
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.rememberModalSheetState
import org.mozilla.fenix.termsofuse.experimentation.TermsOfUsePromptContent
import org.mozilla.fenix.termsofuse.experimentation.getTreatmentA
import org.mozilla.fenix.termsofuse.experimentation.getTreatmentB
import org.mozilla.fenix.termsofuse.experimentation.getTreatmentC
import org.mozilla.fenix.theme.FirefoxTheme

private val sheetMaxWidth = 450.dp

/**
 * The terms of service prompt.
 *
 * @param showDragHandle If the user should see and be able to use a drag handle to dismiss the prompt.
 * @param termsOfUsePromptContent Configurable data that define the prompt title and "learn more" content.
 * @param onDismiss The callback to invoke when the prompt is dismissed.
 * @param onDismissRequest The callback to invoke when the user clicks outside of the bottom sheet, after sheet animates
 *   to Hidden. See [ModalBottomSheet].
 * @param onAcceptClicked The callback to invoke when the user accepts the prompt.
 * @param onRemindMeLaterClicked The callback to invoke when the user clicks "Remind me later".
 * @param onTermsOfUseClicked The callback to invoke when the user clicks on the terms of use link.
 * @param onPrivacyNoticeClicked The callback to invoke when the user clicks on the privacy notice link.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TermsOfUseBottomSheet(
    showDragHandle: Boolean = true,
    termsOfUsePromptContent: TermsOfUsePromptContent,
    onDismiss: () -> Unit,
    onDismissRequest: () -> Unit,
    onAcceptClicked: () -> Unit,
    onRemindMeLaterClicked: () -> Unit,
    onTermsOfUseClicked: () -> Unit,
    onPrivacyNoticeClicked: () -> Unit,
) {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    LaunchedEffect(Unit) {
        sheetState.show()
    }

    BottomSheet(
        showDragHandle = showDragHandle,
        termsOfUsePromptContent = termsOfUsePromptContent,
        sheetState = sheetState,
        onDismiss = onDismiss,
        onDismissRequest = onDismissRequest,
        onAcceptClicked = onAcceptClicked,
        onRemindMeLaterClicked = onRemindMeLaterClicked,
        onTermsOfUseClicked = onTermsOfUseClicked,
        onPrivacyNoticeClicked = onPrivacyNoticeClicked,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun BottomSheet(
    showDragHandle: Boolean,
    termsOfUsePromptContent: TermsOfUsePromptContent,
    sheetState: SheetState,
    onDismiss: () -> Unit = {},
    onDismissRequest: () -> Unit = {},
    onAcceptClicked: () -> Unit = {},
    onRemindMeLaterClicked: () -> Unit = {},
    onTermsOfUseClicked: () -> Unit = {},
    onPrivacyNoticeClicked: () -> Unit = {},
) {
    ModalBottomSheet(
        sheetGesturesEnabled = showDragHandle,
        dragHandle =
            if (showDragHandle) {
                { BottomSheetDefaults.DragHandle(color = MaterialTheme.colorScheme.outline) }
            } else {
                null
            },
        onDismissRequest = onDismissRequest,
        sheetMaxWidth = sheetMaxWidth,
        sheetState = sheetState,
        containerColor = MaterialTheme.colorScheme.surface,
        properties = ModalBottomSheetProperties(shouldDismissOnClickOutside = false),
    ) {
        BottomSheetContent(
            showDragHandle = showDragHandle,
            termsOfUsePromptContent = termsOfUsePromptContent,
            sheetState = sheetState,
            onDismiss = onDismiss,
            onAcceptClicked = onAcceptClicked,
            onRemindMeLaterClicked = onRemindMeLaterClicked,
            onTermsOfUseClicked = onTermsOfUseClicked,
            onPrivacyNoticeClicked = onPrivacyNoticeClicked,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun BottomSheetContent(
    showDragHandle: Boolean,
    termsOfUsePromptContent: TermsOfUsePromptContent,
    sheetState: SheetState,
    onDismiss: () -> Unit,
    onAcceptClicked: () -> Unit = {},
    onRemindMeLaterClicked: () -> Unit = {},
    onTermsOfUseClicked: () -> Unit = {},
    onPrivacyNoticeClicked: () -> Unit = {},
) {
    val coroutineScope = rememberCoroutineScope()
    val scrollState = rememberScrollState()

    Column(modifier = Modifier.verticalScroll(scrollState).padding(start = 36.dp, end = 36.dp, bottom = 32.dp)) {
        if (!showDragHandle) {
            Spacer(Modifier.size(16.dp))
        }

        Image(
            painter = painterResource(id = R.drawable.ic_firefox),
            contentDescription = null,
            modifier = Modifier.size(36.dp).align(Alignment.CenterHorizontally),
        )

        Spacer(Modifier.size(20.dp))

        Text(
            modifier = Modifier.align(Alignment.CenterHorizontally),
            text = termsOfUsePromptContent.title,
            style = FirefoxTheme.typography.headline6,
        )

        Spacer(Modifier.size(20.dp))

        TermsOfUseContent(onTermsOfUseClicked, onPrivacyNoticeClicked)

        Spacer(Modifier.size(20.dp))

        termsOfUsePromptContent.learnMoreContent()

        Spacer(Modifier.size(34.dp))

        OutlinedButton(
            text = stringResource(R.string.terms_of_use_prompt_postpone),
            modifier = Modifier.fillMaxWidth(),
        ) {
            onRemindMeLaterClicked()

            coroutineScope
                .launch {
                    sheetState.hide()
                }
                .invokeOnCompletion {
                    onDismiss()
                }
        }

        FilledButton(
            modifier = Modifier.fillMaxWidth(),
            text = stringResource(R.string.terms_of_use_prompt_accept),
        ) {
            onAcceptClicked()

            coroutineScope
                .launch {
                    sheetState.hide()
                }
                .invokeOnCompletion {
                    onDismiss()
                }
        }
    }
}

@Composable
private fun TermsOfUseContent(
    onTermsOfUseClicked: () -> Unit,
    onPrivacyNoticeClicked: () -> Unit,
) {
    val termsOfUseLinkState =
        LinkTextState(
            text = stringResource(R.string.terms_of_use_prompt_link_terms_of_use),
            url = "",
            onClick = { onTermsOfUseClicked() },
        )
    val privacyNoticeLinkState =
        LinkTextState(
            text = stringResource(R.string.terms_of_use_prompt_link_privacy_notice),
            url = "",
            onClick = { onPrivacyNoticeClicked() },
        )

    LinkText(
        text =
            stringResource(
                id = R.string.terms_of_use_prompt_message_1,
                stringResource(R.string.firefox),
                stringResource(R.string.terms_of_use_prompt_link_terms_of_use),
                stringResource(R.string.terms_of_use_prompt_link_privacy_notice),
            ),
        linkTextStates =
            listOf(
                termsOfUseLinkState,
                privacyNoticeLinkState,
            ),
        style = FirefoxTheme.typography.body2.copy(color = MaterialTheme.colorScheme.onSurfaceVariant),
        linkTextDecoration = TextDecoration.Underline,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@PreviewLightDark
@Composable
private fun TermsOfUseBottomSheetMobilePortraitPreviewTreatmentA() {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme {
        BottomSheet(
            showDragHandle = true,
            termsOfUsePromptContent = getTreatmentA(LocalContext.current) {},
            sheetState = sheetState,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@PreviewLightDark
@Composable
private fun TermsOfUseBottomSheetMobilePortraitPreviewTreatmentB() {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme {
        BottomSheet(
            showDragHandle = true,
            termsOfUsePromptContent = getTreatmentB(LocalContext.current) {},
            sheetState = sheetState,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@PreviewLightDark
@Composable
private fun TermsOfUseBottomSheetMobilePortraitPreviewTreatmentC() {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme {
        BottomSheet(
            showDragHandle = true,
            termsOfUsePromptContent = getTreatmentC(LocalContext.current) {},
            sheetState = sheetState,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@PreviewLightDark
@Composable
private fun TermsOfUseBottomSheetMobilePortraitNoHandlePreview() {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme {
        BottomSheet(
            showDragHandle = false,
            termsOfUsePromptContent = getTreatmentC(LocalContext.current) {},
            sheetState = sheetState,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
// Preview copied from [androidx.compose.ui.tooling.preview.PreviewScreenSizes].
@Preview(
    name = "Phone - Landscape",
    device = "spec:width=411dp,height=891dp,orientation=landscape,dpi=420",
    showSystemUi = true,
)
@Composable
private fun TermsOfUseBottomSheetMobileLandscapePreview() {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme {
        BottomSheet(
            showDragHandle = true,
            termsOfUsePromptContent = getTreatmentC(LocalContext.current) {},
            sheetState = sheetState,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
// Preview copied from [androidx.compose.ui.tooling.preview.PreviewScreenSizes].
@Preview(
    name = "Tablet - Portrait",
    device = "spec:width=1280dp,height=800dp,dpi=240,orientation=portrait",
    showSystemUi = true,
)
@Composable
private fun TermsOfUseBottomSheetTabletPortraitPreview() {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme {
        BottomSheet(
            showDragHandle = true,
            termsOfUsePromptContent = getTreatmentC(LocalContext.current) {},
            sheetState = sheetState,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
// Preview copied from [androidx.compose.ui.tooling.preview.PreviewScreenSizes].
@Preview(name = "Tablet - Landscape", device = TABLET, showSystemUi = true)
@Composable
private fun TermsOfUseBottomSheetTabletLandscapePreview() {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme {
        BottomSheet(
            showDragHandle = true,
            termsOfUsePromptContent = getTreatmentC(LocalContext.current) {},
            sheetState = sheetState,
        )
    }
}
