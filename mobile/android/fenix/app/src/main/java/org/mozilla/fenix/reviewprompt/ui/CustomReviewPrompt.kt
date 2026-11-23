/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.reviewprompt.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.BottomSheetDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.SheetState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.compose.base.theme.ThemedValue
import mozilla.components.compose.base.theme.ThemedValueProvider
import mozilla.components.compose.base.theme.surfaceDimVariant
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.rememberModalSheetState
import org.mozilla.fenix.reviewprompt.CustomReviewPromptAction
import org.mozilla.fenix.reviewprompt.CustomReviewPromptState
import org.mozilla.fenix.reviewprompt.CustomReviewPromptState.Feedback
import org.mozilla.fenix.reviewprompt.CustomReviewPromptState.PrePrompt
import org.mozilla.fenix.reviewprompt.CustomReviewPromptState.Rate
import org.mozilla.fenix.reviewprompt.CustomReviewPromptStore
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Prompt that can show either:
 * - initial state asking to rate the experience,
 * - state asking to leave a Play Store rating,
 * - or state asking to leave feedback.
 *
 * @param customReviewPromptState The state (or step) the prompt should be showing.
 * @param onDismissRequest Called when the accessibility affordance to dismiss the prompt is clicked.
 * @param onNegativePrePromptButtonClick Called when the negative button in the pre-prompt is clicked.
 * @param onPositivePrePromptButtonClick Called when the positive button in the pre-prompt is clicked.
 * @param onRateButtonClick Called when the rate on Play Store button is clicked.
 * @param onLeaveFeedbackButtonClick Called when the leave feedback button is clicked.
 * @param modifier The modifier to be applied to the prompt bottom sheet.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CustomReviewPrompt(
    customReviewPromptState: CustomReviewPromptState,
    onDismissRequest: () -> Unit,
    onNegativePrePromptButtonClick: () -> Unit,
    onPositivePrePromptButtonClick: () -> Unit,
    onRateButtonClick: () -> Unit,
    onLeaveFeedbackButtonClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    LaunchedEffect(Unit) {
        sheetState.show()
    }

    BottomSheet(
        sheetState = sheetState,
        customReviewPromptState = customReviewPromptState,
        onDismissRequest = onDismissRequest,
        onNegativePrePromptButtonClick = onNegativePrePromptButtonClick,
        onPositivePrePromptButtonClick = onPositivePrePromptButtonClick,
        onRateButtonClick = onRateButtonClick,
        onLeaveFeedbackButtonClick = onLeaveFeedbackButtonClick,
        modifier = modifier,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun BottomSheet(
    sheetState: SheetState,
    customReviewPromptState: CustomReviewPromptState,
    onDismissRequest: () -> Unit,
    onNegativePrePromptButtonClick: () -> Unit,
    onPositivePrePromptButtonClick: () -> Unit,
    onRateButtonClick: () -> Unit,
    onLeaveFeedbackButtonClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    ModalBottomSheet(
        onDismissRequest = onDismissRequest,
        modifier = modifier,
        sheetState = sheetState,
        containerColor = MaterialTheme.colorScheme.surface,
        dragHandle = { BottomSheetDefaults.DragHandle(color = MaterialTheme.colorScheme.outline) },
    ) {
        Box(modifier = Modifier.padding(horizontal = 20.dp).padding(bottom = 16.dp)) {
            when (customReviewPromptState) {
                PrePrompt ->
                    PrePrompt(
                        onNegativeButtonClick = onNegativePrePromptButtonClick,
                        onPositiveButtonClick = onPositivePrePromptButtonClick,
                    )

                Rate -> RateStep(onRateButtonClick = onRateButtonClick)
                Feedback -> FeedbackStep(onLeaveFeedbackButtonClick = onLeaveFeedbackButtonClick)
            }
        }
    }
}

@Composable
private fun PrePrompt(
    onNegativeButtonClick: () -> Unit,
    onPositiveButtonClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier) {
        Text(
            text =
                stringResource(
                    R.string.review_prompt_pre_prompt_header,
                    stringResource(R.string.firefox),
                ),
            style = FirefoxTheme.typography.headline7,
        )

        Spacer(Modifier.height(20.dp))

        Row {
            FoxEmojiButton(
                emoji = painterResource(R.drawable.review_prompt_negative_button),
                label = stringResource(R.string.review_prompt_negative_button),
                onClick = onNegativeButtonClick,
                modifier = Modifier.weight(1f),
            )

            Spacer(Modifier.width(20.dp))

            FoxEmojiButton(
                emoji = painterResource(R.drawable.review_prompt_positive_button),
                label = stringResource(R.string.review_prompt_positive_button),
                onClick = onPositiveButtonClick,
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
private fun FoxEmojiButton(
    emoji: Painter,
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier
            .height(100.dp)
            .clip(MaterialTheme.shapes.large)
            .border(1.dp, MaterialTheme.colorScheme.outlineVariant, MaterialTheme.shapes.large)
            .background(MaterialTheme.colorScheme.surfaceDimVariant)
            .clickable(onClick = onClick),
        Arrangement.Center,
        Alignment.CenterHorizontally,
    ) {
        Image(painter = emoji, contentDescription = null)

        Spacer(Modifier.height(10.dp))

        Text(
            text = label,
            modifier = Modifier.padding(horizontal = 10.dp),
            textAlign = TextAlign.Center,
            style = FirefoxTheme.typography.caption,
        )
    }
}

@Composable
private fun RateStep(onRateButtonClick: () -> Unit, modifier: Modifier = Modifier) {
    Column(modifier.padding(vertical = 16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Image(
                painter = painterResource(R.drawable.review_prompt_positive_button),
                contentDescription = null,
            )

            Spacer(Modifier.width(10.dp))

            Text(
                text =
                    stringResource(
                        R.string.review_prompt_rate_header,
                        stringResource(R.string.firefox),
                    ),
                style = FirefoxTheme.typography.headline7,
            )
        }

        Spacer(Modifier.height(20.dp))

        FilledButton(
            text =
                stringResource(
                    R.string.review_prompt_rate_button,
                    stringResource(R.string.firefox),
                ),
            modifier = Modifier.fillMaxWidth(),
            onClick = onRateButtonClick,
        )
    }
}

@Composable
private fun FeedbackStep(onLeaveFeedbackButtonClick: () -> Unit, modifier: Modifier = Modifier) {
    Column(modifier.padding(vertical = 16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Image(
                painter = painterResource(R.drawable.review_prompt_negative_button),
                contentDescription = null,
            )

            Spacer(Modifier.width(10.dp))

            Text(
                text =
                    stringResource(
                        R.string.review_prompt_feedback_header,
                        stringResource(R.string.firefox),
                    ),
                style = FirefoxTheme.typography.headline7,
            )
        }

        Spacer(Modifier.height(20.dp))

        FilledButton(
            text = stringResource(R.string.review_prompt_feedback_button),
            modifier = Modifier.fillMaxWidth(),
            onClick = onLeaveFeedbackButtonClick,
        )
    }
}

// *** Code below used for previews only *** //

@OptIn(ExperimentalMaterial3Api::class)
@FlexibleWindowPreview
@Composable
private fun BottomSheetPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme(theme) {
        BottomSheet(
            sheetState = sheetState,
            customReviewPromptState = PrePrompt,
            onDismissRequest = {},
            onNegativePrePromptButtonClick = {},
            onPositivePrePromptButtonClick = {},
            onRateButtonClick = {},
            onLeaveFeedbackButtonClick = {},
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Preview
@Composable
private fun PrePromptPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme(theme) {
        BottomSheet(
            sheetState = sheetState,
            customReviewPromptState = PrePrompt,
            onDismissRequest = {},
            onNegativePrePromptButtonClick = {},
            onPositivePrePromptButtonClick = {},
            onRateButtonClick = {},
            onLeaveFeedbackButtonClick = {},
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Preview
@Composable
private fun RatePromptPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme(theme) {
        BottomSheet(
            sheetState = sheetState,
            customReviewPromptState = Rate,
            onDismissRequest = {},
            onNegativePrePromptButtonClick = {},
            onPositivePrePromptButtonClick = {},
            onRateButtonClick = {},
            onLeaveFeedbackButtonClick = {},
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Preview
@Composable
private fun FeedbackPromptPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme(theme) {
        BottomSheet(
            sheetState = sheetState,
            customReviewPromptState = Feedback,
            onDismissRequest = {},
            onNegativePrePromptButtonClick = {},
            onPositivePrePromptButtonClick = {},
            onRateButtonClick = {},
            onLeaveFeedbackButtonClick = {},
        )
    }
}

@Preview
@Composable
private fun FoxEmojiButtonPreview(@PreviewParameter(FoxEmojiButtonLabelProvider::class) params: ThemedValue<String>) {
    FirefoxTheme(params.theme) {
        Surface {
            FoxEmojiButton(
                emoji = painterResource(R.drawable.review_prompt_positive_button),
                label = params.value,
                onClick = {},
                modifier = Modifier.padding(16.dp).width(176.dp),
            )
        }
    }
}

private class FoxEmojiButtonLabelProvider :
    ThemedValueProvider<String>(
        baseValues = sequenceOf("It’s great!", "It’s great! And the text is very long omg"),
        displayNames = listOf("Single-line", "Multi-line"),
    )

@OptIn(ExperimentalMaterial3Api::class)
@Preview
@Composable
private fun InteractiveBottomSheetPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    val store = CustomReviewPromptStore(PrePrompt)
    val promptState by store.stateFlow.collectAsState()

    val sheetState = rememberModalSheetState(skipPartiallyExpanded = true)

    FirefoxTheme(theme) {
        BottomSheet(
            sheetState = sheetState,
            customReviewPromptState = promptState,
            onDismissRequest = {},
            onNegativePrePromptButtonClick = {
                store.dispatch(CustomReviewPromptAction.PositivePrePromptButtonClicked)
            },
            onPositivePrePromptButtonClick = {
                store.dispatch(CustomReviewPromptAction.NegativePrePromptButtonClicked)
            },
            onRateButtonClick = {
                store.dispatch(CustomReviewPromptAction.RateButtonClicked)
            },
            onLeaveFeedbackButtonClick = {
                store.dispatch(CustomReviewPromptAction.LeaveFeedbackButtonClicked)
            },
        )
    }
}
