/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import androidx.compose.foundation.Image
import androidx.compose.foundation.LocalOverscrollFactory
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.ui.animation.AnimatedIllustration
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.ScrollIndicator
import org.mozilla.fenix.theme.FirefoxTheme

private val TOU_IMAGE_HEIGHT = 176.dp
private val TOU_IMAGE_HEIGHT_SMALL_DEVICE = 130.dp

private val kitImageResources =
    listOf(
        R.drawable.nova_onboarding_tou,
        R.drawable.nova_onboarding_tou_2,
    )

/**
 * A Composable for displaying the terms of service onboarding page content.
 *
 * @param pageState The page content that's displayed.
 * @param eventHandler The event handler for all user interactions of this page.
 */
@Composable
fun TermsOfServiceOnboardingPage(
    pageState: OnboardingPageState,
    eventHandler: OnboardingTermsOfServiceEventHandler,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceBright),
        elevation = CardDefaults.cardElevation(if (!pageState.isSmallDevice) 6.dp else 0.dp),
    ) {
        val verticalPadding = if (pageState.isSmallDevice) 0.dp else FirefoxTheme.layout.space.static300
        Column(
            modifier =
                Modifier.padding(
                    horizontal = FirefoxTheme.layout.space.static200,
                    vertical = verticalPadding,
                ),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            val scrollState = rememberScrollState()

            if (pageState.isSmallDevice) {
                Spacer(modifier = Modifier.height(16.dp))
            } else {
                Spacer(modifier = Modifier.weight(TITLE_TOP_SPACER_WEIGHT))
            }

            // Use a Box to overlay the scrollbar on top of the content column, aligned to the right.
            Box(modifier = Modifier.weight(CONTENT_WEIGHT).fillMaxWidth()) {
                // Disable the overscroll glow/stretch effect to keep the onboarding UI clean.
                CompositionLocalProvider(LocalOverscrollFactory provides null) {
                    Column(
                        modifier =
                            Modifier.fillMaxSize()
                                .padding(end = FirefoxTheme.layout.space.static150)
                                .verticalScroll(scrollState),
                        horizontalAlignment = Alignment.CenterHorizontally,
                    ) {
                        Header(pageState)

                        Spacer(Modifier.weight(1f))

                        pageState.termsOfService?.let { BodyText(it, eventHandler) }

                        Spacer(Modifier.height(26.dp))
                    }

                    ScrollIndicator(
                        scrollState = scrollState,
                        modifier = Modifier.align(Alignment.CenterEnd),
                        enabled = pageState.isSmallDevice,
                    )
                }
            }
            FilledButton(
                text = pageState.primaryButton.text,
                modifier =
                    Modifier.width(width = FirefoxTheme.layout.size.maxWidth.small).semantics {
                        testTag = pageState.title + "onboarding_card.positive_button"
                    },
                onClick = hapticOnClick(pageState.primaryButton.onClick),
            )
        }
    }

    LaunchedEffect(Unit) {
        pageState.onRecordImpressionEvent()
    }
}

@Composable
private fun Header(pageState: OnboardingPageState) {
    if (FeatureFlags.onboardingKitAnimationEnabled) {
        AnimatedIllustration(
            animationResource = R.raw.terms_of_use_hero_animation,
            staticDrawableResource = R.drawable.terms_of_use_hero,
            modifier = Modifier.height(pageState.imageHeight()),
            contentDescription = null, // Decorative image only.
            iterations = 1,
            replayOnClick = true,
        )
    } else {
        KitImage(pageState)
    }

    Spacer(Modifier.height(20.dp))

    Text(
        text = pageState.title,
        textAlign = TextAlign.Center,
        style = FirefoxTheme.typography.headline6,
    )

    Spacer(Modifier.height(20.dp))

    pageState.termsOfService?.subheaderOneText?.let { SubHeader(it) }
}

@Composable
private fun KitImage(pageState: OnboardingPageState) {
    val currentImageIndex = remember { mutableIntStateOf(0) }
    val currentImageRes = kitImageResources[currentImageIndex.intValue]

    Image(
        painter = painterResource(id = currentImageRes),
        contentDescription = null, // Decorative image only.
        modifier =
            Modifier.height(pageState.imageHeight()).clickable(
                role = Role.Button,
                interactionSource = null,
                indication = null, // Prevents onClick press/ripple animation
            ) {
                currentImageIndex.intValue = nextCyclicImageIndex(currentImageIndex.intValue, kitImageResources.size)
            },
    )
}

private fun OnboardingPageState.imageHeight() =
    if (isSmallDevice) {
        TOU_IMAGE_HEIGHT_SMALL_DEVICE
    } else {
        TOU_IMAGE_HEIGHT
    }

/**
 * Advances the image index to the next item, wrapping back to the start when the end of the list is reached. This
 * ensures the index always stays within valid bounds.
 */
private fun nextCyclicImageIndex(
    currentImageIndex: Int,
    imageResourcesSize: Int,
) = (currentImageIndex + 1) % imageResourcesSize

@Composable
private fun SubHeader(text: String) {
    Text(
        text = text,
        style =
            FirefoxTheme.typography.body2.copy(
                color = MaterialTheme.colorScheme.secondary,
                textAlign = TextAlign.Center,
            ),
    )
}

@Composable
private fun BodyText(
    termsOfService: OnboardingTermsOfService,
    eventHandler: OnboardingTermsOfServiceEventHandler,
) {
    with(termsOfService) {
        Column(modifier = Modifier.padding(horizontal = FirefoxTheme.layout.space.static100)) {
            val bodyOneLinkState =
                LinkTextState(
                    text = lineOneLinkText,
                    url = lineOneLinkUrl,
                    onClick = hapticOnLinkClick(eventHandler::onTermsOfServiceLinkClicked),
                )
            BodyLinkText(
                lineOneText.updateFirstPlaceholder(lineOneLinkText),
                bodyOneLinkState,
            )

            val bodyTwoLinkState =
                LinkTextState(
                    text = lineTwoLinkText,
                    url = lineTwoLinkUrl,
                    onClick = hapticOnLinkClick(eventHandler::onPrivacyNoticeLinkClicked),
                )
            BodyLinkText(
                lineTwoText.updateFirstPlaceholder(lineTwoLinkText),
                bodyTwoLinkState,
            )

            val bodyThreeLinkState =
                LinkTextState(
                    text = lineThreeLinkText,
                    url = "", // No URL
                    onClick = hapticOnLinkClick { eventHandler.onManagePrivacyPreferencesLinkClicked() },
                )
            BodyLinkText(
                lineThreeText.updateFirstPlaceholder(lineThreeLinkText),
                bodyThreeLinkState,
            )
        }
    }
}

@Composable
private fun BodyLinkText(
    text: String,
    linkState: LinkTextState,
) {
    val style =
        FirefoxTheme.typography.caption.copy(
            textAlign = TextAlign.Start,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

    LinkText(
        text = text,
        linkTextStates = listOf(linkState),
        style = style,
        linkTextDecoration = TextDecoration.Underline,
        shouldApplyAccessibleSize = true,
    )
}

private fun String.updateFirstPlaceholder(text: String) = replace($$"%1$s", text)

@PreviewLightDark
@Composable
private fun OnboardingPagePreview() {
    FirefoxTheme {
        TermsOfServiceOnboardingPage(
            pageState =
                OnboardingPageState(
                    title = stringResource(id = R.string.onboarding_welcome_to_firefox),
                    description = "",
                    termsOfService =
                        OnboardingTermsOfService(
                            subheaderOneText = stringResource(id = R.string.nova_onboarding_tou_subtitle),
                            lineOneText = stringResource(id = R.string.nova_onboarding_tou_body_line_1),
                            lineOneLinkText = stringResource(id = R.string.nova_onboarding_tou_body_line_1_link_text),
                            lineOneLinkUrl = "URL",
                            lineTwoText = stringResource(id = R.string.nova_onboarding_tou_body_line_2),
                            lineTwoLinkText = stringResource(id = R.string.nova_onboarding_tou_body_line_2_link_text),
                            lineTwoLinkUrl = "URL",
                            lineThreeText = stringResource(id = R.string.nova_onboarding_tou_body_line_3),
                            lineThreeLinkText = stringResource(id = R.string.nova_onboarding_tou_body_line_3_link_text),
                        ),
                    imageRes = R.drawable.nova_onboarding_tou,
                    primaryButton =
                        Action(
                            text = stringResource(id = R.string.nova_onboarding_continue_button),
                            onClick = {},
                        ),
                ),
            eventHandler = object : OnboardingTermsOfServiceEventHandler {},
        )
    }
}
