/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.translations.settings

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.InfoCard
import mozilla.components.compose.base.InfoType
import mozilla.components.compose.base.LinkText
import mozilla.components.compose.base.LinkTextState
import mozilla.components.concept.engine.translate.TranslationError
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.SwitchListItem
import org.mozilla.fenix.compose.list.TextListItem
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.theme.PreviewThemeProvider
import org.mozilla.fenix.theme.Theme
import org.mozilla.fenix.translations.TranslationSettingsScreenOption
import org.mozilla.fenix.translations.TranslationSwitchItem

/**
 * Translation Settings Fragment.
 *
 * @param state The state of the translations settings screen.
 * @param pageSettingsError Could not load page settings error.
 * @param onAutomaticTranslationClicked Invoked when the user clicks on the "Automatic Translation" button.
 * @param onNeverTranslationClicked Invoked when the user clicks on the "Never Translation" button.
 * @param onDownloadLanguageClicked Invoked when the user clicks on the "Download Language" button.
 * @param onFeatureControlToggled Invoked when the user toggles the translations feature on or off.
 * @param onNavigateToUrl Invoked when the user performs an action requiring navigating to a web page.
 */
@Suppress("LongMethod", "CognitiveComplexMethod")
@Composable
fun TranslationSettings(
    state: TranslationsSettingsState,
    pageSettingsError: TranslationError? = null,
    onAutomaticTranslationClicked: () -> Unit,
    onNeverTranslationClicked: () -> Unit,
    onDownloadLanguageClicked: () -> Unit,
    onFeatureControlToggled: (enabled: Boolean) -> Unit = {},
    onNavigateToUrl: (url: String) -> Unit = {},
) {
    val showHeader = state.showAutomaticTranslations || state.showNeverTranslate || state.showDownloads

    Surface {
        LazyColumn {
            item {
                TranslationsControlSwitchItem(
                    enabled = state.translationsEnabled,
                    onFeatureControlToggled = onFeatureControlToggled,
                    onNavigateToUrl = onNavigateToUrl,
                )
            }

            items(state.switchItems) { item: TranslationSwitchItem ->
                SwitchListItem(
                    label = item.textLabel,
                    checked = item.isChecked,
                    enabled = state.translationsEnabled,
                    maxLabelLines = Int.MAX_VALUE,
                    showSwitchAfter = true,
                ) { checked ->
                    item.onStateChange.invoke(
                        item.type,
                        checked,
                    )
                }
            }

            if (pageSettingsError != null) {
                item {
                    TranslationPageSettingsErrorWarning()
                }
            } else {
                if (showHeader) {
                    item {
                        Spacer(modifier = Modifier.size(32.dp))
                        Text(
                            text = stringResource(id = R.string.translation_settings_translation_preference),
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(
                                    horizontal = FirefoxTheme.layout.space.dynamic200,
                                    vertical = FirefoxTheme.layout.space.static100,
                                )
                                .semantics { heading() },
                            color = MaterialTheme.colorScheme.tertiary.copy(
                                alpha = if (state.translationsEnabled) 1.0f else 0.3f,
                            ),
                            style = FirefoxTheme.typography.headline8,
                        )
                    }
                }

                if (state.showAutomaticTranslations) {
                    item {
                        TextListItem(
                            label = stringResource(id = R.string.translation_settings_automatic_translation),
                            enabled = state.translationsEnabled,
                            modifier = Modifier
                                .fillMaxWidth()
                                .defaultMinSize(minHeight = 56.dp)
                                .wrapContentHeight(),
                            onClick = { onAutomaticTranslationClicked() },
                        )
                    }
                }

                if (state.showNeverTranslate) {
                    item {
                        TextListItem(
                            label = stringResource(id = R.string.translation_settings_automatic_never_translate_sites),
                            enabled = state.translationsEnabled,
                            modifier = Modifier
                                .fillMaxWidth()
                                .defaultMinSize(minHeight = 56.dp)
                                .wrapContentHeight(),
                            onClick = { onNeverTranslationClicked() },
                        )
                    }
                }

                if (state.showDownloads) {
                    item {
                        TextListItem(
                            label = stringResource(id = R.string.translation_settings_download_language),
                            enabled = state.translationsEnabled,
                            modifier = Modifier
                                .fillMaxWidth()
                                .defaultMinSize(minHeight = 56.dp)
                                .wrapContentHeight(),
                            onClick = { onDownloadLanguageClicked() },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun TranslationsControlSwitchItem(
    enabled: Boolean = false,
    onFeatureControlToggled: (enabled: Boolean) -> Unit,
    onNavigateToUrl: (url: String) -> Unit,
) {
    SwitchListItem(
        label = stringResource(R.string.translation_settings_control_title),
        description = stringResource(R.string.translation_settings_control_description),
        checked = enabled,
        maxLabelLines = Int.MAX_VALUE,
        showSwitchAfter = true,
        onClick = onFeatureControlToggled,
    )
    Box(
        modifier = Modifier
            .heightIn(min = 32.dp)
            .fillMaxWidth()
            .padding(horizontal = FirefoxTheme.layout.space.dynamic200),
    ) {
        LinkText(
            text = stringResource(R.string.translation_settings_control_learn_more),
            linkTextStates = listOf(
                LinkTextState(
                    text = stringResource(R.string.translation_settings_control_learn_more),
                    url = resolveTranslationsSumoUrl(),
                    onClick = onNavigateToUrl,
                ),
            ),
            textAlign = TextAlign.Start,
            linkTextDecoration = TextDecoration.Underline,
        )
    }
}

/**
 * Resolves a [SupportUtils.SumoTopic] to a url
 */
@Composable
private fun resolveTranslationsSumoUrl(): String {
    return SupportUtils.getSumoURLForTopic(LocalContext.current, SupportUtils.SumoTopic.TRANSLATIONS)
}

@Composable
private fun TranslationPageSettingsErrorWarning() {
    val modifier = Modifier
        .fillMaxWidth()
        .padding(start = 72.dp, end = 16.dp, bottom = 16.dp, top = 16.dp)
        .defaultMinSize(minHeight = 56.dp)
        .wrapContentHeight()

    InfoCard(
        description = stringResource(id = R.string.translation_option_bottom_sheet_error_warning_text),
        type = InfoType.Warning,
        verticalRowAlignment = Alignment.CenterVertically,
        modifier = modifier,
    )
}

/**
 * Return a list of Translation option switch list item.
 */
@Composable
internal fun getTranslationSettingsSwitchList(): List<TranslationSwitchItem> {
    return mutableListOf<TranslationSwitchItem>().apply {
        add(
            TranslationSwitchItem(
                type = TranslationSettingsScreenOption.OfferToTranslate(hasDivider = false),
                textLabel = stringResource(R.string.translation_settings_offer_to_translate),
                isChecked = true,
                isEnabled = true,
                onStateChange = { _, _ -> },
            ),
        )
        add(
            TranslationSwitchItem(
                type = TranslationSettingsScreenOption.AlwaysDownloadInSavingMode(hasDivider = true),
                textLabel = stringResource(R.string.translation_settings_always_download),
                isChecked = false,
                isEnabled = true,
                onStateChange = { _, _ -> },
            ),
        )
    }
}

@Preview
@Composable
private fun TranslationSettingsPreview(
    @PreviewParameter(PreviewThemeProvider::class) theme: Theme,
) {
    FirefoxTheme(theme) {
        TranslationSettings(
            state = TranslationsSettingsState(
                showAutomaticTranslations = true,
                showNeverTranslate = true,
                showDownloads = true,
                translationsEnabled = true,
                switchItems = getTranslationSettingsSwitchList(),
            ),
            onAutomaticTranslationClicked = {},
            onDownloadLanguageClicked = {},
            onNeverTranslationClicked = {},
        )
    }
}
