/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ai

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowPreview
import mozilla.components.compose.base.button.TextButton
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.LinkText
import org.mozilla.fenix.compose.LinkTextState
import org.mozilla.fenix.compose.list.IconListItem
import org.mozilla.fenix.compose.list.SwitchListItem
import org.mozilla.fenix.compose.settings.SettingsSectionHeader
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import mozilla.components.ui.icons.R as iconsR

@Composable
internal fun AiControlsScreen(
    onBannerLearnMoreClick: () -> Unit,
) {
    Surface {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
        ) {
            AiChoiceBanner(onLearnMoreClick = onBannerLearnMoreClick)

            SwitchListItem(
                label = stringResource(R.string.ai_controls_block_ai_title),
                checked = false,
                description = stringResource(
                    R.string.ai_controls_block_ai_description,
                    stringResource(R.string.app_name),
                ),
                maxDescriptionLines = Int.MAX_VALUE,
                showSwitchAfter = true,
                onClick = {},
            )

            SettingsLink(
                text = stringResource(R.string.ai_controls_see_whats_included),
                onClick = onBannerLearnMoreClick,
            )

            HorizontalDivider()

            AiFeaturesSection()
        }
    }
}

@Composable
private fun AiFeaturesSection() {
    SettingsSectionHeader(
        text = stringResource(R.string.ai_controls_ai_powered_features),
        modifier = Modifier.padding(
            horizontal = FirefoxTheme.layout.space.dynamic200,
            vertical = 8.dp,
        ),
    )

    SwitchListItem(
        label = stringResource(R.string.ai_controls_translations_title),
        checked = true,
        enabled = true,
        description = stringResource(R.string.ai_controls_translations_description),
        maxDescriptionLines = Int.MAX_VALUE,
        showSwitchAfter = true,
        onClick = {},
    )

    SettingsLink(
        text = stringResource(R.string.ai_controls_more_translations_settings),
        onClick = {},
    )

    SwitchListItem(
        label = stringResource(R.string.ai_controls_page_summaries_title),
        checked = false,
        enabled = true,
        description = stringResource(R.string.ai_controls_page_summaries_description),
        showSwitchAfter = true,
        onClick = {},
    )

    SettingsLink(
        text = stringResource(R.string.ai_controls_more_page_summary_settings),
        onClick = {},
    )

    SwitchListItem(
        label = stringResource(R.string.ai_controls_voice_search_title),
        checked = true,
        enabled = true,
        description = stringResource(R.string.ai_controls_voice_search_description),
        maxDescriptionLines = 2,
        showSwitchAfter = true,
        onClick = {},
    )
}

@Composable
private fun AiChoiceBanner(onLearnMoreClick: () -> Unit) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                start = 16.dp,
                end = 16.dp,
                top = 8.dp,
                bottom = 16.dp,
            ),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .background(FirefoxTheme.colors.layerAccentNonOpaque)
                .padding(horizontal = 16.dp),
        ) {
            Column(
                modifier = Modifier
                    .weight(1f)
                    .padding(vertical = 12.dp),
            ) {
                Text(
                    text = stringResource(R.string.ai_controls_banner_headline, stringResource(R.string.app_name)),
                    style = FirefoxTheme.typography.headline7,
                    color = MaterialTheme.colorScheme.onPrimaryContainer,
                )

                val learnMoreText = stringResource(R.string.ai_controls_learn_more)
                val supportingText = stringResource(R.string.ai_controls_banner_supporting_text)
                val fullText = "$supportingText $learnMoreText"

                LinkText(
                    text = fullText,
                    linkTextStates = listOf(
                        LinkTextState(
                            text = learnMoreText,
                            url = "",
                            onClick = { onLearnMoreClick() },
                        ),
                    ),
                    style = FirefoxTheme.typography.body2.copy(
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    ),
                    linkTextDecoration = TextDecoration.Underline,
                    linkTextColor = MaterialTheme.colorScheme.tertiary,
                )
            }

            Image(
                painter = painterResource(iconsR.drawable.mozac_ic_fox_ai_on_state),
                contentDescription = null,
                modifier = Modifier
                    .align(Alignment.Bottom)
                    .width(62.dp)
                    .height(63.dp),
            )
        }
    }
}

@Composable
private fun BlockedInfoBanner(
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(FirefoxTheme.colors.layerWarning)
            .padding(8.dp),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_information_24),
            contentDescription = null,
            modifier = Modifier
                .padding(top = 1.dp)
                .size(24.dp),
            tint = MaterialTheme.colorScheme.onPrimaryContainer,
        )

        Spacer(modifier = Modifier.width(8.dp))

        Text(
            text = stringResource(R.string.ai_controls_blocked_info_banner),
            style = FirefoxTheme.typography.body2,
            color = MaterialTheme.colorScheme.onPrimaryContainer,
        )
    }
}

@Composable
private fun BlockAiDialog() {
    AlertDialog(
        onDismissRequest = { },
        title = {
            Text(
                text = stringResource(R.string.ai_controls_block_dialog_title),
                style = FirefoxTheme.typography.headline5,
                textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                modifier = Modifier.fillMaxWidth(),
            )
        },
        text = {
            Column {
                val appName = stringResource(R.string.app_name)
                val bodyText = stringResource(R.string.ai_controls_block_dialog_body, appName, appName)
                val whatBlocked = stringResource(R.string.ai_controls_block_dialog_what_will_be_blocked)

                Text(
                    text = bodyText,
                    style = FirefoxTheme.typography.body2,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )

                Spacer(modifier = Modifier.height(16.dp))

                Text(
                    text = whatBlocked,
                    style = FirefoxTheme.typography.body2.copy(fontWeight = FontWeight.W700),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )

                Spacer(modifier = Modifier.height(16.dp))

                IconListItem(
                    label = stringResource(R.string.ai_controls_translations_title),
                    beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_translate_24),
                )

                IconListItem(
                    label = stringResource(R.string.ai_controls_page_summaries_title),
                    beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_lightning_24),
                )

                IconListItem(
                    label = stringResource(R.string.ai_controls_voice_search_title),
                    beforeIconPainter = painterResource(iconsR.drawable.mozac_ic_microphone_24),
                )
            }
        },
        dismissButton = {
            TextButton(
                text = stringResource(R.string.ai_controls_block_dialog_cancel),
                onClick = {  },
            )
        },
        confirmButton = {
            TextButton(
                text = stringResource(R.string.ai_controls_block_dialog_block),
                onClick = {  },
                colors = ButtonDefaults.textButtonColors(
                    contentColor = MaterialTheme.colorScheme.error,
                ),
            )
        },
    )
}

@Composable
private fun SettingsLink(
    text: String,
    onClick: () -> Unit,
) {
    Column(
        modifier = Modifier
            .clickable(onClick = onClick)
            .fillMaxWidth()
            .padding(
                start = FirefoxTheme.layout.space.dynamic200,
                end = FirefoxTheme.layout.space.dynamic200,
                top = 4.dp,
            )
            .height(48.dp),
    ) {
        LinkText(
            text = text,
            linkTextStates = listOf(
                LinkTextState(
                    text = text,
                    url = "",
                    onClick = { onClick() },
                ),
            ),
            linkTextDecoration = TextDecoration.Underline,
        )
    }
}

@FlexibleWindowPreview
@Composable
private fun AiControlsScreenPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        AiControlsScreen(
            onBannerLearnMoreClick = {},
        )
    }
}

@Preview
@Composable
private fun BlockAiDialogPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        BlockAiDialog()
    }
}

@Preview
@Composable
private fun BlockedInfoBannerPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        BlockedInfoBanner()
    }
}
