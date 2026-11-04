/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

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
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.OutlinedButton
import mozilla.components.ui.colors.PhotonColors
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.ScrollIndicator
import org.mozilla.fenix.nimbus.MarketingCardVariant
import org.mozilla.fenix.theme.FirefoxTheme

private val MARKETING_CONTENT_IMAGE_HEIGHT = 150.dp
private val MARKETING_CONTENT_IMAGE_HEIGHT_TREATMENT_C = 130.dp

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
fun MarketingDataOnboardingPage(
    state: OnboardingPageState,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
    onMarketingDataSkipClick: () -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceBright),
        elevation = CardDefaults.cardElevation(if (!state.isSmallDevice) 6.dp else 0.dp),
    ) {
        val verticalPadding = if (state.isSmallDevice) 0.dp else FirefoxTheme.layout.space.static300
        Column(
            modifier =
                Modifier.padding(
                    horizontal = FirefoxTheme.layout.space.static200,
                    vertical = verticalPadding,
                ),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            if (state.isSmallDevice) {
                Spacer(modifier = Modifier.height(16.dp))
            } else {
                Spacer(modifier = Modifier.weight(TITLE_TOP_SPACER_WEIGHT))
            }

            var checkboxChecked by remember { mutableStateOf(true) }

            Box(modifier = Modifier.weight(CONTENT_WEIGHT).fillMaxWidth()) {
                val scrollState = rememberScrollState()

                CompositionLocalProvider(LocalOverscrollFactory provides null) {
                    val startPadding = 20.dp
                    Column(
                        modifier =
                            Modifier.fillMaxSize()
                                .verticalScroll(scrollState)
                                .padding(start = startPadding, end = FirefoxTheme.layout.space.static400),
                        verticalArrangement = Arrangement.spacedBy(36.dp),
                    ) {
                        Text(
                            text = state.title,
                            textAlign = TextAlign.Start,
                            style = FirefoxTheme.typography.headline6,
                        )

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.Center,
                        ) {
                            state.marketingData?.let {
                                val imageResource = getImageResourceForVariant(state)
                                val imageHeight = imageHeightForTreatment(it)

                                Image(
                                    modifier = Modifier.height(imageHeight),
                                    painter = painterResource(id = imageResource),
                                    contentDescription = null,
                                )
                            }
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

            Spacer(Modifier.height(32.dp))

            if (state.secondaryButton != null) {
                Row(
                    modifier = Modifier.width(FirefoxTheme.layout.size.maxWidth.small),
                    horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static100),
                ) {
                    SecondaryButton(
                        state = state,
                        action = state.secondaryButton,
                        onMarketingDataSkipClick = onMarketingDataSkipClick,
                        modifier = Modifier.weight(1f),
                    )
                    PrimaryButton(
                        state = state,
                        onMarketingDataContinueClick = onMarketingDataContinueClick,
                        checkboxChecked = checkboxChecked,
                        modifier = Modifier.weight(1f),
                    )
                }
            } else {
                PrimaryButton(
                    state = state,
                    onMarketingDataContinueClick = onMarketingDataContinueClick,
                    checkboxChecked = checkboxChecked,
                    modifier = Modifier.width(width = FirefoxTheme.layout.size.maxWidth.small),
                )
            }
        }
    }

    LaunchedEffect(Unit) {
        state.onRecordImpressionEvent()
    }
}

/** We are temporarily adjusting the image size for treatment c only. */
@Composable
private fun imageHeightForTreatment(data: OnboardingMarketingData): Dp {
    val imageHeight =
        if (data.marketingCardVariant == MarketingCardVariant.TREATMENT_C) {
            MARKETING_CONTENT_IMAGE_HEIGHT_TREATMENT_C
        } else {
            MARKETING_CONTENT_IMAGE_HEIGHT
        }
    return imageHeight
}

@Composable
private fun SecondaryButton(
    state: OnboardingPageState,
    action: Action,
    onMarketingDataSkipClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    when (state.marketingData?.marketingCardVariant) {
        MarketingCardVariant.DEFAULT,
        null -> Unit

        else -> SecondaryButtonOutline(action, state, onMarketingDataSkipClick, modifier)
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
private fun PrimaryButton(
    state: OnboardingPageState,
    onMarketingDataContinueClick: (Boolean) -> Unit,
    checkboxChecked: Boolean,
    modifier: Modifier = Modifier,
) {
    val buttonText =
        state.marketingData?.let {
            primaryButtonCopyForVariant(
                defaultString = state.primaryButton.text,
                marketingCardVariant = it.marketingCardVariant,
            )
        } ?: state.primaryButton.text

    if (state.marketingData?.marketingCardVariant == MarketingCardVariant.DEFAULT) {
        FilledButton(
            text = buttonText,
            modifier =
                modifier.semantics {
                    testTag = state.title + "onboarding_card.positive_button"
                },
            onClick = hapticOnClick { onMarketingDataContinueClick(checkboxChecked) },
        )
    } else {
        FilledButton(
            text = buttonText,
            modifier =
                modifier.semantics {
                    testTag = state.title + "onboarding_card.positive_button"
                },
            icon = painterResource(id = R.drawable.ic_favourite_filled),
            iconModifier = Modifier.size(16.dp),
            iconTint = PhotonColors.Red50,
            onClick = hapticOnClick { onMarketingDataContinueClick(true) },
        )
    }
}

@Composable
private fun SecondaryButtonOutline(
    action: Action,
    state: OnboardingPageState,
    onMarketingDataSkipClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val buttonText =
        state.marketingData?.let {
            secondaryButtonCopyForVariant(
                defaultString = action.text,
                marketingCardVariant = it.marketingCardVariant,
            )
        } ?: action.text

    OutlinedButton(
        text = buttonText,
        modifier =
            modifier.semantics {
                testTag = state.title + "onboarding_card.negative_button"
            },
        onClick = hapticOnClick { onMarketingDataSkipClick() },
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
        when (marketingData.marketingCardVariant) {
            MarketingCardVariant.DEFAULT -> {
                DefaultContent(
                    checkboxChecked = checkboxChecked,
                    onMarketingOptInToggle = onMarketingOptInToggle,
                    marketingData = marketingData,
                    onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
                )
            }

            MarketingCardVariant.TREATMENT_C -> {
                TreatmentCContent(
                    marketingData = marketingData,
                    onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
                )
            }
        }
    }
}

@Composable
private fun DefaultContent(
    checkboxChecked: Boolean,
    onMarketingOptInToggle: (Boolean) -> Unit,
    marketingData: OnboardingMarketingData,
    onMarketingDataLearnMoreClick: () -> Unit,
) {
    val onToggle = hapticOnCheckedChange { onMarketingOptInToggle(!checkboxChecked) }

    Row(
        Modifier.toggleable(
            value = checkboxChecked,
            role = Role.Checkbox,
            onValueChange = onToggle,
        )
    ) {
        Checkbox(
            modifier = Modifier.align(Alignment.Top).offset(y = (-12).dp, x = (-12).dp).clearAndSetSemantics {},
            checked = checkboxChecked,
            onCheckedChange = onToggle,
        )

        LinkText(
            text = marketingData.bodyOneText.updateFirstPlaceholder(marketingData.bodyOneLinkText),
            linkTextStates =
                listOf(
                    LinkTextState(
                        text = marketingData.bodyOneLinkText,
                        url = "",
                        onClick = hapticOnLinkClick { onMarketingDataLearnMoreClick() },
                    )
                ),
            linkTextDecoration = TextDecoration.Underline,
            style = FirefoxTheme.typography.body2,
            textAlign = TextAlign.Start,
        )
    }
}

@Composable
private fun TreatmentCContent(
    marketingData: OnboardingMarketingData,
    onMarketingDataLearnMoreClick: () -> Unit,
) {
    val lineOne = marketingData.bodyOneText
    val lineOneLink = marketingData.bodyOneLinkText

    LinkText(
        text = lineOne.updateFirstPlaceholder(lineOneLink),
        linkTextStates =
            listOf(
                LinkTextState(
                    text = lineOneLink,
                    url = "",
                    onClick = hapticOnLinkClick { onMarketingDataLearnMoreClick() },
                )
            ),
        linkTextDecoration = TextDecoration.Underline,
        style = FirefoxTheme.typography.body2,
        textAlign = TextAlign.Start,
    )

    Spacer(Modifier.height(16.dp))

    val lineTwo = marketingData.bodyTwoText
    val lineThree = marketingData.bodyThreeText

    if (!lineTwo.isNullOrBlank() && !lineThree.isNullOrBlank()) {
        Text(
            text = buildTextWithBoldedEnding(lineTwo, lineThree),
            style = FirefoxTheme.typography.body2,
            textAlign = TextAlign.Start,
        )
    }
}

private fun buildTextWithBoldedEnding(
    firstLine: String,
    secondLine: String,
): AnnotatedString = buildAnnotatedString {
    append(firstLine)
    append(' ')
    withStyle(SpanStyle(fontWeight = FontWeight.Bold)) {
        append(secondLine)
    }
}

@Composable
private fun primaryButtonCopyForVariant(
    defaultString: String,
    marketingCardVariant: MarketingCardVariant,
) =
    when (marketingCardVariant) {
        MarketingCardVariant.DEFAULT -> defaultString

        MarketingCardVariant.TREATMENT_C -> stringResource(R.string.nova_onboarding_marketing_primary_button_text_3)
    }

@Composable
private fun secondaryButtonCopyForVariant(
    defaultString: String,
    marketingCardVariant: MarketingCardVariant,
) =
    when (marketingCardVariant) {
        MarketingCardVariant.DEFAULT -> defaultString
        MarketingCardVariant.TREATMENT_C -> stringResource(R.string.nova_onboarding_marketing_secondary_button_text_2)
    }

private fun imageResourceForVariant(
    defaultImageResource: Int,
    marketingCardVariant: MarketingCardVariant,
) =
    when (marketingCardVariant) {
        MarketingCardVariant.DEFAULT -> defaultImageResource
        MarketingCardVariant.TREATMENT_C -> R.drawable.ic_kit_heart
    }

// Uncomment @FlexibleWindowLightDarkPreview below to review changes across multiple screen sizes.
// @FlexibleWindowLightDarkPreview

// Use @PreviewLightDark by default for preview rendering performance and easier preview navigation.
@PreviewLightDark
@Composable
private fun MarketingDataOnboardingPagePreviewDefault() {
    FirefoxTheme {
        MarketingDataOnboardingPage(
            state =
                OnboardingPageState(
                    imageRes = R.drawable.nova_onboarding_marketing,
                    title = stringResource(id = R.string.nova_onboarding_marketing_title),
                    description = "", // NB: not used
                    primaryButton =
                        Action(
                            text = stringResource(id = R.string.nova_onboarding_continue_button),
                            onClick = {},
                        ),
                    secondaryButton =
                        Action(
                            text = stringResource(id = R.string.nova_onboarding_marketing_secondary_button_text),
                            onClick = {},
                        ),
                    marketingData =
                        OnboardingMarketingData(
                            marketingCardVariant = MarketingCardVariant.DEFAULT,
                            bodyOneText = stringResource(id = R.string.nova_onboarding_marketing_body),
                            bodyOneLinkText = stringResource(id = R.string.nova_onboarding_marketing_body_link_text),
                        ),
                ),
            onMarketingDataLearnMoreClick = {},
            onMarketingOptInToggle = {},
            onMarketingDataContinueClick = {},
            onMarketingDataSkipClick = {},
        )
    }
}

/* Treatment C previews */

@Composable
private fun MarketingDataOnboardingPageTreatmentCPreview(
    title: String,
    bodyOneText: String,
    bodyOneLinkText: String,
    bodyTwoText: String,
    bodyThreeText: String,
) {
    FirefoxTheme {
        MarketingDataOnboardingPage(
            state =
                OnboardingPageState(
                    imageRes = R.drawable.nova_onboarding_marketing,
                    title = title,
                    description = "", // NB: not used
                    primaryButton =
                        Action(
                            text = stringResource(id = R.string.nova_onboarding_continue_button),
                            onClick = {},
                        ),
                    secondaryButton =
                        Action(
                            text = stringResource(id = R.string.nova_onboarding_marketing_secondary_button_text),
                            onClick = {},
                        ),
                    marketingData =
                        OnboardingMarketingData(
                            marketingCardVariant = MarketingCardVariant.TREATMENT_C,
                            bodyOneText = bodyOneText,
                            bodyOneLinkText = bodyOneLinkText,
                            bodyTwoText = bodyTwoText,
                            bodyThreeText = bodyThreeText,
                        ),
                ),
            onMarketingDataLearnMoreClick = {},
            onMarketingOptInToggle = {},
            onMarketingDataContinueClick = {},
            onMarketingDataSkipClick = {},
        )
    }
}

@PreviewLightDark
@Composable
private fun MarketingDataOnboardingPagePreviewTreatmentCEnglish() {
    MarketingDataOnboardingPageTreatmentCPreview(
        title = stringResource(R.string.onboarding_marketing_redesign_title),
        bodyOneText = stringResource(R.string.nova_onboarding_marketing_body_7),
        bodyOneLinkText = stringResource(R.string.nova_onboarding_marketing_body_link_text_1),
        bodyTwoText = stringResource(R.string.nova_onboarding_marketing_body_line_two),
        bodyThreeText = stringResource(R.string.nova_onboarding_marketing_body_line_three),
    )
}

/* The below locales use tailored copy outside on translations that will be provided by Nimbus */

@Preview(name = "es", locale = "es")
@Composable
private fun MarketingDataOnboardingPagePreviewTreatmentCSpanish() {
    MarketingDataOnboardingPageTreatmentCPreview(
        title = "Ayude a Firefox a crecer",
        bodyOneText =
            "Firefox puede llegar a más personas si permite que Mozilla indique a la plataforma desde la que " +
                $$"llegó que usted usa Firefox. %1$s",
        bodyOneLinkText = "Más información",
        bodyTwoText = "Firefox es independiente y defiende una web abierta y libre de monopolios tecnológicos.",
        bodyThreeText = "Su permiso ayuda a que más personas elijan Firefox.",
    )
}

@Preview(name = "fr", locale = "fr")
@Composable
private fun MarketingDataOnboardingPagePreviewTreatmentCFrench() {
    MarketingDataOnboardingPageTreatmentCPreview(
        title = "Soutenez Firefox",
        bodyOneText =
            "Aidez-nous à faire découvrir Firefox à encore plus de personnes. Autorisez Mozilla à informer " +
                $$"la plateforme par laquelle vous êtes arrivé que vous utilisez Firefox. %1$s",
        bodyOneLinkText = "En savoir plus",
        bodyTwoText =
            "Firefox est indépendant et défend un Web ouvert, plutôt que de le laisser aux mains des " +
                "géants de la tech.",
        bodyThreeText = "Votre soutien nous aide à renforcer Firefox.",
    )
}

@Preview(name = "de", locale = "de")
@Composable
private fun MarketingDataOnboardingPagePreviewTreatmentCGerman() {
    MarketingDataOnboardingPageTreatmentCPreview(
        title = "Deine Hilfe für Firefox",
        bodyOneText =
            "Hilf uns, noch mehr Menschen für Firefox zu gewinnen. Erlaube Mozilla, der Plattform, " +
                $$"über die du zu uns gekommen bist, mitzuteilen, dass du Firefox nutzt. %1$s",
        bodyOneLinkText = "Mehr erfahren",
        bodyTwoText =
            "Firefox ist unabhängig und setzt sich für ein offenes Web ein, statt es den " +
                "Tech-Giganten zu überlassen.",
        bodyThreeText = "Mit deiner Zustimmung hilfst du uns dabei und stärkst Firefox.",
    )
}

private fun String.updateFirstPlaceholder(text: String) = replace($$"%1$s", text)
