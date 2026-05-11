/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.redesign.view

import androidx.compose.foundation.Image
import androidx.compose.foundation.LocalOverscrollFactory
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.OutlinedButton
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.ScrollIndicator
import org.mozilla.fenix.onboarding.view.Action
import org.mozilla.fenix.onboarding.view.OnboardingPageState
import org.mozilla.fenix.theme.FirefoxTheme

const val TITLE_TOP_SPACER_WEIGHT = 0.1f
const val CONTENT_WEIGHT = 1f

val CONTENT_IMAGE_HEIGHT = 176.dp

/**
 * A composable for displaying onboarding page content.
 *
 * @param pageState [OnboardingPageState] The page content that's displayed.
 */
@Composable
fun OnboardingPageRedesign(
    pageState: OnboardingPageState,
) {
    CardView(pageState, pageState.isSmallDevice)

    LaunchedEffect(Unit) {
        pageState.onRecordImpressionEvent()
    }
}

@Composable
private fun SecondaryButton(
    title: String,
    secondaryButton: Action,
) {
    OutlinedButton(
        modifier = Modifier
            .width(width = FirefoxTheme.layout.size.maxWidth.small)
            .semantics {
                testTag = title + "onboarding_card_redesign.negative_button"
            },
        text = secondaryButton.text,
        onClick = secondaryButton.onClick,
    )
}

@Composable
private fun CardView(
    pageState: OnboardingPageState,
    isSmallDevice: Boolean,
    ) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        elevation = CardDefaults.cardElevation(if (pageState.shouldShowElevation) 6.dp else 0.dp),
    ) {
            Column(
                modifier = Modifier.padding(
                    horizontal = 16.dp,
                    vertical = if (isSmallDevice) 0.dp else 24.dp,
                ),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                if (pageState.isSmallDevice) {
                    Spacer(modifier = Modifier.height(16.dp))
                } else {
                    Spacer(modifier = Modifier.weight(TITLE_TOP_SPACER_WEIGHT))
                }

                Content(pageState, isSmallDevice)

                pageState.secondaryButton?.let {
                    SecondaryButton(title = pageState.title, secondaryButton = it)
                }

                FilledButton(
                    modifier = Modifier
                        .width(width = FirefoxTheme.layout.size.maxWidth.small)
                        .semantics {
                            testTag = pageState.title + "onboarding_card_redesign.positive_button"
                        },
                    text = pageState.primaryButton.text,
                    onClick = pageState.primaryButton.onClick,
                )
            }
        }
    }

@Composable
private fun ColumnScope.Content(
    pageState: OnboardingPageState,
    isSmallDevice: Boolean,
) {
    val scrollState = rememberScrollState()

    // Use a Box to overlay the scrollbar on top of the content column, aligned to the right.
    Box(
        modifier = Modifier
            .weight(CONTENT_WEIGHT)
            .fillMaxWidth(),
    ) {
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
                Text(
                    text = pageState.title,
                    style = MaterialTheme.typography.headlineSmall,
                )

                Box(
                    modifier = Modifier
                        .height(CONTENT_IMAGE_HEIGHT)
                        .fillMaxWidth(),
                    contentAlignment = Alignment.Center,
                ) {
                    Image(
                        painter = painterResource(pageState.imageRes),
                        contentDescription = null, // Decorative only
                    )
                }

                Text(
                    text = pageState.description,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = FirefoxTheme.typography.subtitle1,
                )
            }
        }

        ScrollIndicator(
            scrollState = scrollState,
            modifier = Modifier.align(Alignment.CenterEnd),
            enabled = isSmallDevice,
        )
    }
}

@PreviewLightDark
@Composable
private fun OnboardingPageSetToDefaultPreview() {
    FirefoxTheme {
        OnboardingPageRedesign(
            pageState = OnboardingPageState(
                imageRes = R.drawable.nova_onboarding_set_to_default,
                title = stringResource(R.string.nova_onboarding_set_to_default_title_2),
                description = stringResource(R.string.nova_onboarding_set_to_default_subtitle),
                primaryButton = Action(
                    text = stringResource(R.string.nova_onboarding_set_to_default_button),
                    onClick = {},
                ),
                secondaryButton = Action(
                    text = stringResource(R.string.nova_onboarding_negative_button),
                    onClick = {},
                ),
                onRecordImpressionEvent = {},
            ),
        )
    }
}

@PreviewLightDark
@Composable
private fun OnboardingPageSyncPreview() {
    FirefoxTheme {
        OnboardingPageRedesign(
            pageState = OnboardingPageState(
                imageRes = R.drawable.nova_onboarding_sync,
                title = stringResource(R.string.nova_onboarding_sync_title),
                description = stringResource(R.string.nova_onboarding_sync_subtitle),
                primaryButton = Action(
                    text = stringResource(R.string.nova_onboarding_sync_button),
                    onClick = {},
                ),
                secondaryButton = Action(
                    text = stringResource(R.string.nova_onboarding_negative_button),
                    onClick = {},
                ),
                onRecordImpressionEvent = {},
            ),
        )
    }
}

@PreviewLightDark
@Composable
private fun OnboardingPageNotificationPreview() {
    FirefoxTheme {
        OnboardingPageRedesign(
            pageState = OnboardingPageState(
                imageRes = R.drawable.nova_onboarding_notifications,
                title = stringResource(R.string.nova_onboarding_add_search_widget_title),
                description = stringResource(R.string.nova_onboarding_add_search_widget_subtitle),
                primaryButton = Action(
                    text = stringResource(R.string.nova_onboarding_notifications_button),
                    onClick = {},
                ),
                secondaryButton = Action(
                    text = stringResource(R.string.nova_onboarding_negative_button),
                    onClick = {},
                ),
                onRecordImpressionEvent = {},
            ),
        )
    }
}

@PreviewLightDark
@Composable
private fun OnboardingPageSearchWidgetPreview() {
    FirefoxTheme {
        OnboardingPageRedesign(
            pageState = OnboardingPageState(
                imageRes = R.drawable.nova_onboarding_widget,
                title = stringResource(R.string.nova_onboarding_add_search_widget_title),
                description = stringResource(R.string.nova_onboarding_add_search_widget_subtitle),
                primaryButton = Action(
                    text = stringResource(R.string.nova_onboarding_add_search_widget_button),
                    onClick = {},
                ),
                secondaryButton = Action(
                    text = stringResource(R.string.nova_onboarding_negative_button),
                    onClick = {},
                ),
                onRecordImpressionEvent = {},
            ),
        )
    }
}
