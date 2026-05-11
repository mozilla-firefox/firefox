/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.redesign.view

import androidx.compose.foundation.Image
import androidx.compose.foundation.LocalOverscrollFactory
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.ui.colors.PhotonColors
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.ScrollIndicator
import org.mozilla.fenix.nimbus.MarketingCardVariant
import org.mozilla.fenix.onboarding.view.Action
import org.mozilla.fenix.onboarding.view.OnboardingMarketingData
import org.mozilla.fenix.onboarding.view.OnboardingPageState
import org.mozilla.fenix.theme.FirefoxTheme

private val MARKETING_CONTENT_IMAGE_HEIGHT = 150.dp

/**
 * UI for an onboarding page that allows the user to opt out of marketing data analytics.
 *
 * @param state the UI state containing strings etc.
 * @param onMarketingDataLearnMoreClick callback for when the user clicks the learn more text link.
 * @param onMarketingOptInToggle callback for when the user toggles the opt-in checkbox.
 * @param onMarketingDataContinueClick callback for when the user clicks the continue button.
 * @param onMarketingDataSkipClick callback for when the user clicks the skip button.
 */
@Suppress("LongMethod")
@Composable
fun MarketingDataOnboardingPageRedesign(
    state: OnboardingPageState,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
    onMarketingDataSkipClick: () -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(if (state.shouldShowElevation) 6.dp else 0.dp),
    ) {
        Column(
            modifier = Modifier.padding(
                horizontal = 16.dp,
                vertical = if (state.isSmallDevice) 0.dp else 24.dp,
            ),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (state.isSmallDevice) {
                Spacer(modifier = Modifier.height(16.dp))
            } else {
                Spacer(modifier = Modifier.weight(TITLE_TOP_SPACER_WEIGHT))
            }

            var checkboxChecked by remember { mutableStateOf(true) }

            Box(
                modifier = Modifier
                    .weight(CONTENT_WEIGHT)
                    .fillMaxWidth(),
            ) {
                val scrollState = rememberScrollState()

                CompositionLocalProvider(
                    LocalOverscrollFactory provides null,
                ) {
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .verticalScroll(scrollState)
                            .padding(start = 20.dp, end = 32.dp),
                        verticalArrangement = Arrangement.spacedBy(36.dp),
                    ) {
                        val title = getTitleForVariant(state)
                        Text(
                            text = title,
                            textAlign = TextAlign.Start,
                            style = MaterialTheme.typography.headlineSmall,
                        )

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.Center,
                        ) {
                            val imageResource = getImageResourceForVariant(state)
                            Image(
                                modifier = Modifier.height(MARKETING_CONTENT_IMAGE_HEIGHT),
                                painter = painterResource(id = imageResource),
                                contentDescription = null,
                            )
                        }

                        state.marketingData?.let {
                            MarketingDataView(
                                marketingData = it,
                                checkboxChecked = checkboxChecked,
                                onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
                                onMarketingOptInToggle = { isChecked ->
                                    checkboxChecked = isChecked
                                    onMarketingOptInToggle(isChecked)
                                },
                            )
                        }
                    }
                }

                ScrollIndicator(
                    scrollState = scrollState,
                    modifier = Modifier.align(Alignment.CenterEnd),
                    enabled = state.isSmallDevice,
                )
            }

            if (state.marketingData?.marketingCardVariant != MarketingCardVariant.DEFAULT) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Center,
                ) {
                    state.marketingData?.let {
                        LinkText(
                            text = it.bodyOneLinkText,
                            linkTextStates = listOf(
                                LinkTextState(
                                    text = it.bodyOneLinkText,
                                    url = "",
                                    onClick = { onMarketingDataLearnMoreClick() },
                                ),
                            ),
                            linkTextDecoration = TextDecoration.Underline,
                            style = FirefoxTheme.typography.body2,
                            textAlign = TextAlign.Start,
                        )
                    }
                }
            }

            Spacer(Modifier.height(32.dp))

            state.secondaryButton?.let { action ->
                SecondaryButton(state, action, onMarketingDataSkipClick)
            }

            PrimaryButton(state, onMarketingDataContinueClick, checkboxChecked)
        }
    }

    LaunchedEffect(Unit) {
        state.onRecordImpressionEvent()
    }
}

@Composable
private fun SecondaryButton(
    state: OnboardingPageState,
    action: Action,
    onMarketingDataSkipClick: () -> Unit,
) {
    when (state.marketingData?.marketingCardVariant) {
        MarketingCardVariant.DEFAULT,
        null,
            -> Unit

        MarketingCardVariant.TREATMENT_A,
        MarketingCardVariant.TREATMENT_B,
            -> SecondaryButtonFilled(action, state, onMarketingDataSkipClick)

        else -> SecondaryButtonOutline(action, state, onMarketingDataSkipClick)
    }
}

@Composable
private fun getImageResourceForVariant(state: OnboardingPageState): Int {
    val imageResource =
        state.marketingData?.let {
            imageResourceForVariant(
                defaultImageResource = state.imageRes,
                marketingCardVariant = it.marketingCardVariant,
            )
        } ?: state.imageRes
    return imageResource
}

@Composable
private fun getTitleForVariant(state: OnboardingPageState): String {
    val title =
        state.marketingData?.let {
            titleCopyForVariant(
                defaultString = state.title,
                marketingCardVariant = it.marketingCardVariant,
            )
        } ?: state.title
    return title
}

@Composable
private fun PrimaryButton(
    state: OnboardingPageState,
    onMarketingDataContinueClick: (Boolean) -> Unit,
    checkboxChecked: Boolean,
) {
    val buttonText = state.marketingData?.let {
        primaryButtonCopyForVariant(
            defaultString = state.primaryButton.text,
            marketingCardVariant = it.marketingCardVariant,
        )
    } ?: state.primaryButton.text

    if (state.marketingData?.marketingCardVariant == MarketingCardVariant.DEFAULT) {
        FilledButton(
            text = buttonText,
            modifier = Modifier
                .width(width = FirefoxTheme.layout.size.maxWidth.small)
                .semantics {
                    testTag = state.title + "onboarding_card_redesign.positive_button"
                },
            onClick = { onMarketingDataContinueClick(checkboxChecked) },
        )
    } else {
        FilledButton(
            text = buttonText,
            modifier = Modifier
                .width(width = FirefoxTheme.layout.size.maxWidth.small)
                .semantics {
                    testTag = state.title + "onboarding_card_redesign.positive_button"
                },
            icon = painterResource(id = R.drawable.ic_favourite_filled),
            iconModifier = Modifier.size(16.dp),
            iconTint = PhotonColors.Red50,
            onClick = { onMarketingDataContinueClick(true) },
        )
    }
}

@Composable
private fun SecondaryButtonFilled(
    action: Action,
    state: OnboardingPageState,
    onMarketingDataSkipClick: () -> Unit,
) {
    val buttonText = state.marketingData?.let {
        secondaryButtonCopyForVariant(
            defaultString = action.text,
            marketingCardVariant = it.marketingCardVariant,
        )
    } ?: action.text

    FilledButton(
        text = buttonText,
        modifier = Modifier
            .width(width = FirefoxTheme.layout.size.maxWidth.small)
            .semantics {
                testTag = state.title + "onboarding_card_redesign.negative_button"
            },
        onClick = { onMarketingDataSkipClick() },
    )
}

@Composable
private fun SecondaryButtonOutline(
    action: Action,
    state: OnboardingPageState,
    onMarketingDataSkipClick: () -> Unit,
) {
    val buttonText = state.marketingData?.let {
        secondaryButtonCopyForVariant(
            defaultString = action.text,
            marketingCardVariant = it.marketingCardVariant,
        )
    } ?: action.text

    OutlinedButton(
        text = buttonText,
        modifier = Modifier
            .width(width = FirefoxTheme.layout.size.maxWidth.small)
            .semantics {
                testTag = state.title + "onboarding_card_redesign.negative_button"
            },
        onClick = { onMarketingDataSkipClick() },
    )
}

@Composable
private fun MarketingDataView(
    marketingData: OnboardingMarketingData,
    checkboxChecked: Boolean,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
) {
    Column {
        if (marketingData.marketingCardVariant == MarketingCardVariant.DEFAULT) {
            Row(
                Modifier.toggleable(
                    value = checkboxChecked,
                    role = Role.Checkbox,
                    onValueChange = {
                        onMarketingOptInToggle.invoke(!checkboxChecked)
                    },
                ),
            ) {
                Checkbox(
                    modifier = Modifier
                        .align(Alignment.Top)
                        .offset(y = (-12).dp, x = (-12).dp)
                        .clearAndSetSemantics {},
                    checked = checkboxChecked,
                    onCheckedChange = {
                        onMarketingOptInToggle.invoke(!checkboxChecked)
                    },
                )

                LinkText(
                    text = marketingData.bodyOneText.updateFirstPlaceholder(marketingData.bodyOneLinkText),
                    linkTextStates = listOf(
                        LinkTextState(
                            text = marketingData.bodyOneLinkText,
                            url = "",
                            onClick = { onMarketingDataLearnMoreClick() },
                        ),
                    ),
                    linkTextDecoration = TextDecoration.Underline,
                    style = FirefoxTheme.typography.body2,
                    textAlign = TextAlign.Start,
                )
            }
        } else {
            Row {
                val bodyCopyRes = bodyCopyForVariant(marketingData.marketingCardVariant)

                Text(
                    text = stringResource(bodyCopyRes),
                    style = FirefoxTheme.typography.body2,
                    textAlign = TextAlign.Start,
                )
            }
        }
    }
}

@Composable
private fun primaryButtonCopyForVariant(
    defaultString: String,
    marketingCardVariant: MarketingCardVariant,
) = when (marketingCardVariant) {
    MarketingCardVariant.DEFAULT -> defaultString
    MarketingCardVariant.TREATMENT_A,
    MarketingCardVariant.TREATMENT_B,
    MarketingCardVariant.TREATMENT_C,
        -> stringResource(R.string.nova_onboarding_marketing_primary_button_text)
}

@Composable
private fun secondaryButtonCopyForVariant(
    defaultString: String,
    marketingCardVariant: MarketingCardVariant,
) = when (marketingCardVariant) {
    MarketingCardVariant.DEFAULT -> defaultString
    MarketingCardVariant.TREATMENT_A,
    MarketingCardVariant.TREATMENT_B,
    MarketingCardVariant.TREATMENT_C,
        -> stringResource(R.string.nova_onboarding_marketing_secondary_button_text)
}

private fun imageResourceForVariant(
    defaultImageResource: Int,
    marketingCardVariant: MarketingCardVariant,
) = when (marketingCardVariant) {
    MarketingCardVariant.DEFAULT -> defaultImageResource
    MarketingCardVariant.TREATMENT_A,
    MarketingCardVariant.TREATMENT_B,
    MarketingCardVariant.TREATMENT_C,
        -> R.drawable.ic_kit_heart
}

@Composable
private fun titleCopyForVariant(
    defaultString: String,
    marketingCardVariant: MarketingCardVariant,
) = when (marketingCardVariant) {
    MarketingCardVariant.DEFAULT -> defaultString
    MarketingCardVariant.TREATMENT_A,
    MarketingCardVariant.TREATMENT_B,
    MarketingCardVariant.TREATMENT_C,
        -> stringResource(R.string.onboarding_marketing_redesign_title)
}

private fun bodyCopyForVariant(marketingCardVariant: MarketingCardVariant) =
    when (marketingCardVariant) {
        MarketingCardVariant.DEFAULT -> R.string.nova_onboarding_marketing_body_2
        MarketingCardVariant.TREATMENT_A -> R.string.nova_onboarding_marketing_body_3
        MarketingCardVariant.TREATMENT_B,
        MarketingCardVariant.TREATMENT_C,
            -> R.string.nova_onboarding_marketing_body_4
    }

private class BodyResourcePreviewProvider : PreviewParameterProvider<MarketingCardVariant> {
    override val values = sequenceOf(
        MarketingCardVariant.DEFAULT,
        MarketingCardVariant.TREATMENT_A,
        MarketingCardVariant.TREATMENT_B,
        MarketingCardVariant.TREATMENT_C,
    )

    override fun getDisplayName(index: Int): String {
        return values.elementAt(index).name
    }
}

// Uncomment @FlexibleWindowLightDarkPreview below to review changes across multiple screen sizes.
// @FlexibleWindowLightDarkPreview

// Use @PreviewLightDark by default for preview rendering performance and easier preview navigation.
@PreviewLightDark
@Composable
private fun MarketingDataOnboardingPagePreview(
    @PreviewParameter(BodyResourcePreviewProvider::class) variant: MarketingCardVariant,
) {
    FirefoxTheme {
        MarketingDataOnboardingPageRedesign(
            state = OnboardingPageState(
                imageRes = R.drawable.nova_onboarding_marketing,
                title = stringResource(id = R.string.nova_onboarding_marketing_title),
                description = "", // NB: not used in the redesign
                primaryButton = Action(
                    text = stringResource(id = R.string.nova_onboarding_continue_button),
                    onClick = {},
                ),
                secondaryButton = Action(
                    text = "", // NB: value should be set in the secondaryButtonCopyForVariant function
                    onClick = {},
                ),
                marketingData = OnboardingMarketingData(
                    marketingCardVariant = variant,
                    bodyOneText = stringResource(id = R.string.nova_onboarding_marketing_body),
                    bodyOneLinkText = stringResource(id = R.string.nova_onboarding_marketing_body_link_text),
                    bodyTwoText = "", // NB: not used in the redesign
                ),
            ),
            onMarketingDataLearnMoreClick = {},
            onMarketingOptInToggle = {},
            onMarketingDataContinueClick = {},
            onMarketingDataSkipClick = {},
        )
    }
}

private fun String.updateFirstPlaceholder(text: String) = replace($$"%1$s", text)
