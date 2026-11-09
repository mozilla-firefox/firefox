/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.SheetState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import java.time.LocalDate
import kotlinx.coroutines.launch
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import org.mozilla.fenix.R
import org.mozilla.fenix.ipprotection.helpers.formatPromoDate
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptState
import org.mozilla.fenix.ipprotection.store.IPProtectionPromptState.PromptDetails
import org.mozilla.fenix.theme.FirefoxTheme

private val bannerImageSize = 170.dp
private val sheetMaxHeight = 154.dp
private val sheetMaxWidth = 450.dp

/**
 * The IP Protection onboarding prompt.
 *
 * @param state The state representing the onboarding prompt.
 * @param onDismiss The callback to invoke when the prompt is dismissed.
 * @param onDismissRequest The callback to invoke when the user clicks outside of the bottom sheet, after sheet animates
 *   to Hidden. See [ModalBottomSheet].
 * @param onLearnMoreClicked The callback to invoke when user clicks on the hyperlink that points to an article about
 *   VPN on Firefox.
 * @param onGetStartedClicked The callback to invoke when user clicks on "Get started" to start the VPN authentication
 *   or authorization process.
 * @param onNotNowClicked The callback to invoke when user clicks on "Not now" to dismiss the prompt.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun IPProtectionBottomSheet(
    state: IPProtectionPromptState,
    onDismiss: () -> Unit,
    onDismissRequest: () -> Unit,
    onLearnMoreClicked: () -> Unit,
    onGetStartedClicked: () -> Unit,
    onNotNowClicked: () -> Unit,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    LaunchedEffect(Unit) {
        sheetState.show()
    }

    BottomSheet(
        details = state.details,
        sheetState = sheetState,
        onDismiss = onDismiss,
        onDismissRequest = onDismissRequest,
        onLearnMoreClicked = onLearnMoreClicked,
        onGetStartedClicked = onGetStartedClicked,
        onNotNowClicked = onNotNowClicked,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun BottomSheet(
    details: PromptDetails,
    sheetState: SheetState,
    onDismiss: () -> Unit = {},
    onDismissRequest: () -> Unit = {},
    onLearnMoreClicked: () -> Unit = {},
    onGetStartedClicked: () -> Unit = {},
    onNotNowClicked: () -> Unit = {},
) {
    ModalBottomSheet(
        onDismissRequest = onDismissRequest,
        sheetMaxWidth = sheetMaxWidth,
        sheetState = sheetState,
        containerColor = MaterialTheme.colorScheme.surface,
    ) {
        BottomSheetContent(
            details = details,
            sheetState = sheetState,
            onDismiss = onDismiss,
            onLearnMoreClicked = onLearnMoreClicked,
            onGetStartedClicked = onGetStartedClicked,
            onNotNowClicked = onNotNowClicked,
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun BottomSheetContent(
    details: PromptDetails,
    sheetState: SheetState,
    onDismiss: () -> Unit,
    onLearnMoreClicked: () -> Unit,
    onGetStartedClicked: () -> Unit,
    onNotNowClicked: () -> Unit,
) {
    val coroutineScope = rememberCoroutineScope()
    val scrollState = rememberScrollState()

    Column(modifier = Modifier.padding(bottom = 16.dp, start = 16.dp, end = 16.dp)) {
        Box(modifier = Modifier.weight(1f, fill = false)) {
            Column(modifier = Modifier.verticalScroll(state = scrollState, reverseScrolling = true)) {
                PromoBannerCard()

                Spacer(Modifier.height(32.dp))

                Text(
                    text =
                        stringResource(
                            R.string.ip_protection_onboarding_card_headline,
                            stringResource(R.string.firefox),
                        ),
                    style = MaterialTheme.typography.bodyLarge,
                )

                Spacer(Modifier.height(16.dp))

                when (details) {
                    is PromptDetails.UnlimitedDataPromo ->
                        IPProtectionPromoContent(
                            formattedDate = details.promoDeadline.formatPromoDate(),
                            onLearnMoreClicked = onLearnMoreClicked,
                        )
                    is PromptDetails.Regular ->
                        IPProtectionContent(
                            maxGib = details.dataLimitGigabytes,
                            onLearnMoreClicked = onLearnMoreClicked,
                        )
                }
            }
        }

        Spacer(Modifier.height(16.dp))

        IPProtectionButtons(
            onNotNowClicked = {
                onNotNowClicked()
                coroutineScope
                    .launch {
                        sheetState.hide()
                    }
                    .invokeOnCompletion {
                        onDismiss()
                    }
            },
            onGetStartedClicked = {
                onGetStartedClicked()
                coroutineScope
                    .launch {
                        sheetState.hide()
                    }
                    .invokeOnCompletion {
                        onDismiss()
                    }
            },
        )
    }
}

@Composable
private fun IPProtectionPromoContent(
    formattedDate: String,
    onLearnMoreClicked: () -> Unit,
) {
    val learnMoreText = stringResource(R.string.ip_protection_onboarding_body_link_promo)
    LinkText(
        text = stringResource(id = R.string.ip_protection_onboarding_body_promo, formattedDate, learnMoreText),
        linkTextStates =
            listOf(
                LinkTextState(
                    text = learnMoreText,
                    url = "",
                    onClick = { onLearnMoreClicked() },
                )
            ),
        style = FirefoxTheme.typography.body2.copy(color = MaterialTheme.colorScheme.onSurfaceVariant),
        linkTextDecoration = TextDecoration.Underline,
    )
}

@Composable
private fun IPProtectionContent(
    maxGib: Int,
    onLearnMoreClicked: () -> Unit,
) {
    val learnMoreText = stringResource(R.string.ip_protection_onboarding_body_link)

    LinkText(
        text =
            stringResource(
                id = R.string.ip_protection_onboarding_body,
                learnMoreText,
                maxGib,
            ),
        linkTextStates =
            listOf(
                LinkTextState(
                    text = learnMoreText,
                    url = "",
                    onClick = { onLearnMoreClicked() },
                )
            ),
        style = FirefoxTheme.typography.body2.copy(color = MaterialTheme.colorScheme.onSurfaceVariant),
        linkTextDecoration = TextDecoration.Underline,
    )
}

@Composable
private fun IPProtectionButtons(
    onNotNowClicked: () -> Unit,
    onGetStartedClicked: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.End,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TextButton(
            text = stringResource(R.string.ip_protection_onboarding_not_now_button),
            onClick = onNotNowClicked,
        )

        Spacer(Modifier.width(8.dp))
        FilledButton(
            text = stringResource(R.string.ip_protection_get_started),
            onClick = onGetStartedClicked,
        )
    }
}

@Composable
private fun PromoBannerCard() {
    Box(
        modifier = Modifier.fillMaxWidth(),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier =
                Modifier.fillMaxWidth()
                    .height(sheetMaxHeight)
                    .clip(MaterialTheme.shapes.large)
                    .background(MaterialTheme.colorScheme.primaryContainer)
        ) {
            BetaBadge()
        }

        Image(
            painter = painterResource(id = R.drawable.ic_kit_shield_on_state),
            contentDescription = null,
            modifier = Modifier.size(bannerImageSize).offset(y = 16.dp),
        )
    }
}

@Composable
private fun BetaBadge() {
    Box(modifier = Modifier.padding(8.dp)) {
        Surface(
            shape = CircleShape,
            color = Color.Transparent,
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.tertiary),
        ) {
            Text(
                text = stringResource(R.string.preferences_ip_protection_beta_badge_label),
                modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                color = MaterialTheme.colorScheme.tertiary,
                style = FirefoxTheme.typography.body2,
            )
        }
    }
}

@FlexibleWindowPreview
@Composable
private fun IPProtectionBottomSheetPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionBottomSheet(
            state = IPProtectionPromptState(PromptDetails.Regular(dataLimitGigabytes = 50)),
            onDismiss = {},
            onDismissRequest = {},
            onGetStartedClicked = {},
            onLearnMoreClicked = {},
            onNotNowClicked = {},
        )
    }
}

@FlexibleWindowPreview
@Composable
private fun IPProtectionPromoBottomSheetPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme = theme) {
        IPProtectionBottomSheet(
            state = IPProtectionPromptState(PromptDetails.UnlimitedDataPromo(LocalDate.parse("2026-06-09"))),
            onDismiss = {},
            onDismissRequest = {},
            onGetStartedClicked = {},
            onLearnMoreClicked = {},
            onNotNowClicked = {},
        )
    }
}
